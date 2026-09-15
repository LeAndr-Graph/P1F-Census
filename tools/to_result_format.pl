#!/usr/bin/perl
# to_result_format.pl -- rewrite a class file into exactly what a p1f run writes.
#
#   perl tools/to_result_format.pl <file> [<file> ...]
#
# Every file under AllResults\ should be byte-for-byte the format a run of the matching
# runs\<case>\run.bat produces, so a stored census and a fresh one can be compared, concatenated
# or read by the same tools without a special case. That format is records and nothing else:
#
#   #<seq> |Aut| = <aut>
#    "   0  1   2  3 ... "        <- NM rows, ' "' + (i%2 ? " " : "  ") + %2d per vertex + ' "'
#
# No header, no |Aut| summary table, no blank lines between records, and the record ORDER of the
# input is preserved -- a run emits in thread-discovery order and nothing may re-sort that.
# <seq> is renumbered 1..N over the records as they appear.
#
# Reads the three layouts the project has produced:
#
#   RESULT    "#7 |Aut| = 12"                              -> renumbered, rows re-emitted
#   REP_INFO  "# block 4.0.133  pattern SSW..  |Aut| 2  new" + bare "0  1  2  3 .." rows
#             ('# canon:' and '# dup block' lines are dropped -- block coordinates and duplicate
#              attribution are internal, and a run does not write them)
#   P-file    "    7: |Aut(M)| =  0,  Cycles:153(18)" + quoted rows
#             REFUSED by default: that header always carries |Aut| = 0 because |Aut| never reached
#             the result callback, so converting it would fabricate an automorphism order. Pass
#             --aut-from=<file> to take each class's |Aut| from a file that does record it, matched
#             on the MATRIX; a class the authority does not hold is an error, never a guess.
#
# The file is rewritten in place only after the whole conversion succeeds.
#
# DO NOT run this on AllResults\K18_P1F_aut_gt1.txt. That catalog is a curated document, not a run
# output: its records are already in this exact format, but it deliberately carries a title, a
# total, an |Aut| summary table and a note on the ordering. Converting it would strip all of that.
# The script refuses it by name.
use strict;
use warnings;

die "usage: perl to_result_format.pl <file> [<file> ...]\n" unless @ARGV;

my %PROTECTED = map { lc($_) => 1 } ('k18_p1f_aut_gt1.txt');

# --aut-from=<file>: matrix -> |Aut|, for sources whose own header cannot carry it (P-files).
my ($autFrom, %AUTOF);
@ARGV = grep { $_ =~ /^--aut-from=(.+)$/ ? (($autFrom = $1), 0)[1] : 1 } @ARGV;
if (defined $autFrom) {
    open my $ah, '<', $autFrom or die "cannot open $autFrom: $!\n";
    my ($a, @rows);
    my $bank = sub {
        return unless @rows;
        die "$autFrom: matrix with no |Aut| header\n" unless defined $a;
        $AUTOF{ join('|', map { join(' ', @$_) } @rows) } = $a;
        @rows = (); $a = undef;
    };
    while (my $l = <$ah>) {
        $l =~ s/\r?\n$//;
        if ($l =~ /^\s*#\s*\d+\s+\|Aut\|\s*=\s*(\d+)/) { my $v = $1; $bank->(); $a = $v; next }
        if ($l =~ /^\s*#\s*block\b.*\|Aut\|\s+(\d+)/)  { my $v = $1; $bank->(); $a = $v; next }
        next if $l =~ /^\s*#/;
        my @v = ($l =~ /(\d+)/g);
        push @rows, \@v if @v >= 4;
    }
    $bank->();
    close $ah;
    printf STDERR "authority %s: %d results with a recorded |Aut|\n", $autFrom, scalar keys %AUTOF;
}

for my $path (@ARGV) {
    (my $base = $path) =~ s{.*[\\/]}{};
    if ($PROTECTED{ lc $base }) {
        print "SKIP $path: curated catalog -- its records are already in this format, but its\n",
              "     title, total and |Aut| summary table are meant to stay. Not touched.\n";
        next;
    }
    open my $fh, '<', $path or die "cannot open $path: $!\n";
    my (@recs, $aut, @rows, $seenPfile);
    my $flush = sub {
        return unless @rows;
        die "$path: record before any |Aut| header\n" unless defined $aut;
        push @recs, { aut => $aut, rows => [@rows] };
        @rows = (); $aut = undef;
    };
    while (my $l = <$fh>) {
        $l =~ s/\r?\n$//;
        if ($l =~ /\|Aut\(M\)\|/) {
            unless (defined $autFrom) { $seenPfile = 1; last }
            $flush->(); $aut = -1;                  # resolved from the authority once the rows are in
            next;
        }
        if ($l =~ /^#\s*\d+\s+\|Aut\|\s*=\s*(\d+)/) { $flush->(); $aut = $1; next }   # RESULT header
        if ($l =~ /^#\s*block\b.*\|Aut\|\s+(\d+)/)  { $flush->(); $aut = $1; next }   # REP_INFO header
        next if $l =~ /^#/;                        # '# canon:', '# dup block', prose, table rows
        my @v = ($l =~ /(\d+)/g);
        push @rows, \@v if @v >= 4;                # a matrix row in either spacing
    }
    close $fh;
    if ($seenPfile) {
        print "SKIP $path: P-file layout -- its header records |Aut| = 0 for every result, so it\n",
              "     cannot be converted without fabricating the automorphism order.\n";
        next;
    }
    $flush->();
    unless (@recs) { print "SKIP $path: no records found\n"; next }

    # every record must have the same row count, and every row the same even width
    my $nm = scalar @{ $recs[0]{rows} };
    my $n  = scalar @{ $recs[0]{rows}[0] };
    for my $i (0 .. $#recs) {
        die "$path: record ", $i + 1, " has ", scalar @{ $recs[$i]{rows} }, " rows, expected $nm\n"
            unless @{ $recs[$i]{rows} } == $nm;
        for my $r (@{ $recs[$i]{rows} }) {
            die "$path: record ", $i + 1, " has a row of ", scalar @$r, " values, expected $n\n"
                unless @$r == $n;
        }
    }

    # resolve the placeholder |Aut| of a P-file's records against the authority
    if (defined $autFrom) {
        my $missing = 0;
        for my $r (@recs) {
            next unless $r->{aut} < 0;
            my $key = join('|', map { join(' ', @$_) } @{ $r->{rows} });
            if (defined(my $a = $AUTOF{$key})) { $r->{aut} = $a } else { $missing++ }
        }
        die "$path: $missing of ", scalar @recs, " classes are not in $autFrom -- refusing to\n",
            "guess their |Aut|. The authority does not cover this file.\n" if $missing;
    }

    my $out = '';
    my $seq = 0;
    for my $r (@recs) {
        $out .= sprintf("#%d |Aut| = %d\n", ++$seq, $r->{aut});
        for my $row (@{ $r->{rows} }) {
            $out .= ' "';
            $out .= ($_ % 2 ? ' ' : '  ') . sprintf('%2d', $row->[$_]) for 0 .. $#$row;
            $out .= " \"\n";
        }
    }
    open my $o, '>', $path or die "cannot write $path: $!\n";
    print $o $out; close $o;
    my %h; $h{ $_->{aut} }++ for @recs;
    printf "%s: %d records, %d rows each, |Aut| %s\n", $path, scalar @recs, $nm,
           join(' ', map { "$_:$h{$_}" } sort { $a <=> $b } keys %h);
}
