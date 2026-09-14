#!/usr/bin/perl
# subtract_classes.pl -- set difference over class files, in the run's own record format.
#
#   perl tools/subtract_classes.pl <minuend> <subtrahend> [<subtrahend> ...] > <out>
#
# Writes every record of <minuend> whose MATRIX does not appear in any <subtrahend>, renumbering
# #<seq> from 1 and preserving the minuend's record order. Records are matched on the matrix alone,
# so the file layouts may differ (the catalog carries a prose header, a run's output does not).
#
# Use --aut=<n> to restrict the minuend to records with that automorphism order first.
#
# Why this exists: the classes of a stratum that was swept on another machine can still be
# recovered from a superset. The K18 |Aut| > 1 catalog holds all 10,710 classes; the t9 census
# files hold the 727 (a = 0) and 6 (a = 4) it contributed; the difference over |Aut| = 2 is the
# t8 stratum. Nothing here re-derives mathematics -- it is bookkeeping over files that already
# agree, and the caller is expected to check the resulting count against a known total.
use strict;
use warnings;

my $autOnly;
@ARGV = grep { $_ =~ /^--aut=(\d+)$/ ? (($autOnly = $1), 0)[1] : 1 } @ARGV;
die "usage: perl subtract_classes.pl [--aut=N] <minuend> <subtrahend> [...] > out\n" unless @ARGV >= 2;

my ($minuend, @subs) = @ARGV;

# read records: matrix key -> undef, plus the ordered list for the minuend
sub read_file {
    my ($path) = @_;
    open my $fh, '<', $path or die "cannot open $path: $!\n";
    my (@recs, $aut, @rows);
    my $flush = sub {
        return unless @rows;
        push @recs, { aut => $aut, rows => [@rows], key => join('|', map { join(' ', @$_) } @rows) };
        @rows = (); $aut = undef;
    };
    while (my $l = <$fh>) {
        $l =~ s/\r?\n$//;
        # capture BEFORE flushing: $flush is a closure and anything it runs may clobber $1
        if ($l =~ /^\s*#\s*\d+\s+\|Aut\|\s*=\s*(\d+)/) { my $a = $1; $flush->(); $aut = $a; next }  # RESULT
        if ($l =~ /^\s*#\s*block\b.*\|Aut\|\s+(\d+)/)  { my $a = $1; $flush->(); $aut = $a; next }  # REP_INFO
        next if $l =~ /^\s*#/;
        my @v = ($l =~ /(\d+)/g);
        push @rows, \@v if @v >= 4;
    }
    $flush->();
    close $fh;
    return \@recs;
}

my $keep = read_file($minuend);
my %drop;
for my $s (@subs) {
    my $r = read_file($s);
    $drop{ $_->{key} } = 1 for @$r;
    printf STDERR "  minus %s: %d records\n", $s, scalar @$r;
}

my ($seq, $skippedAut, $removed) = (0, 0, 0);
for my $r (@$keep) {
    if (defined $autOnly && $r->{aut} != $autOnly) { $skippedAut++; next }
    if ($drop{ $r->{key} })                        { $removed++;    next }
    printf "#%d |Aut| = %d\n", ++$seq, $r->{aut};
    for my $row (@{ $r->{rows} }) {
        print ' "';
        print(($_ % 2 ? ' ' : '  ') . sprintf('%2d', $row->[$_])) for 0 .. $#$row;
        print " \"\n";
    }
}
printf STDERR "  %s: %d records", $minuend, scalar @$keep;
printf STDERR ", %d not |Aut| = %d", $skippedAut, $autOnly if defined $autOnly;
printf STDERR ", %d subtracted -> %d written\n", $removed, $seq;
