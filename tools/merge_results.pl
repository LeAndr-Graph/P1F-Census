#!/usr/bin/perl
# merge_results.pl -- combine shard result files into one, dropping repeats.
#
#   perl tools/merge_results.pl <out> <in> [<in> ...]
#
# Shards of one sweep are NOT disjoint. Each engine process dedups against its own memory only,
# and a single class is reachable from many blocks, so two shards that searched different block
# ranges can both record the same class. Plain concatenation therefore over-counts.
#
# This merges on the MATRIX: the first file to carry a class wins, later repeats are dropped, and
# the output is renumbered #1..#N in the order classes were first seen. Input order is otherwise
# preserved, and the output is the same record format a run writes, so it can be compared with a
# stored census directly.
#
# A class appearing in two shards with DIFFERENT |Aut| would mean one of them is wrong; that is
# reported as a conflict and the merge fails rather than silently picking one.
#
# (When the owner filter lands, shards become disjoint by construction
# and this becomes a plain concatenation -- but it stays correct either way.)
use strict;
use warnings;

my ($outF, @ins) = @ARGV;
die "usage: perl merge_results.pl <out> <in> [<in> ...]\n" unless @ins;
die "merge_results: $outF exists -- refusing to overwrite\n" if -e $outF;

my (%seen, @order, $conflicts, $total);
for my $path (@ins) {
    open my $fh, '<', $path or die "cannot open $path: $!\n";
    # NOT `my ($aut, @rows, $n, $dup) = (...)`: an array in a list assignment slurps everything
    # after it, leaving $n and $dup undef and @rows holding their intended values.
    my $aut; my @rows; my ($n, $dup) = (0, 0);
    my $flush = sub {
        return unless @rows;
        $n++; $total++;
        my $key = join('|', map { join(' ', @$_) } @rows);
        if (exists $seen{$key}) {
            $dup++;
            if (defined $aut && $seen{$key}{aut} != $aut) {
                printf "CONFLICT %s: a result recorded with |Aut| = %d here and %d in %s\n",
                       $path, $aut, $seen{$key}{aut}, $seen{$key}{from};
                $conflicts++;
            }
        } else {
            $seen{$key} = { aut => $aut, rows => [@rows], from => $path };
            push @order, $key;
        }
        @rows = (); $aut = undef;
    };
    while (my $l = <$fh>) {
        $l =~ s/\r?\n$//;
        if ($l =~ /^\s*#\s*\d+\s+\|Aut\|\s*=\s*(\d+)/) { my $v = $1; $flush->(); $aut = $v; next }
        if ($l =~ /^\s*#\s*block\b.*\|Aut\|\s+(\d+)/)  { my $v = $1; $flush->(); $aut = $v; next }
        next if $l =~ /^\s*#/;
        my @v = ($l =~ /(\d+)/g);
        push @rows, \@v if @v >= 4;
    }
    $flush->();
    close $fh;
    printf "  %-40s %6d records, %6d already seen\n", $path, $n, $dup;
}
die "merge_results: $conflicts conflict(s) -- nothing written\n" if $conflicts;

open my $o, '>', $outF or die "cannot write $outF: $!\n";
my ($seq, %h) = (0);
for my $key (@order) {
    my $r = $seen{$key};
    $h{ $r->{aut} }++ if defined $r->{aut};
    printf $o "#%d |Aut| = %d\n", ++$seq, (defined $r->{aut} ? $r->{aut} : 0);
    for my $row (@{ $r->{rows} }) {
        print $o ' "';
        print $o(($_ % 2 ? ' ' : '  ') . sprintf('%2d', $row->[$_])) for 0 .. $#$row;
        print $o " \"\n";
    }
}
close $o;
printf "%s: %d distinct of %d records read (%d repeats dropped), |Aut| %s\n",
       $outF, $seq, $total, $total - $seq,
       join(' ', map { "$_:$h{$_}" } sort { $a <=> $b } keys %h);
