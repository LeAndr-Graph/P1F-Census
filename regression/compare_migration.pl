#!/usr/bin/perl
# compare_migration.pl -- ONE-TIME proof that moving to the single RESULT output lost nothing.
#
#   perl compare_migration.pl <case> <new RESULT file> <old frozen baseline> [expected-aut-histogram]
#
# The four frozen result_expected.txt files ARE the oracle for this migration. Regenerating them
# from the new binary would prove only that the binary agrees with itself, so instead we compare
# CONTENT across the format change: the set of class matrices must be identical.
#
# Three record layouts have to be read, because that is exactly what the change is about:
#
#   new RESULT   "#7 |Aut| = 12"                       + rows ' "   0  1   2  3 ... "'
#   old REP_INFO "# block 4.0.133  pattern SW..  |Aut| 2  new" + rows '0 1  2 3  4 5 ...'
#   old P-file   "    7: |Aut(M)| =  0,  Cycles:153(18)"       + rows '  "   0   1    2   3 "'
#
# All three are normalized to "one record = the ints of each row, in row order". Row order is kept
# (both writers print the same canonical matrix), record order is NOT (records are appended in
# thread-discovery order, so two correct runs routinely disagree on it).
#
# The |Aut| column has NO baseline: every old P-file prints "|Aut(M)| = 0" because |Aut| never
# reached the result callback. So it is checked against the literature/oracle histogram passed as
# the 4th argument, e.g. "3:19,5:5,7:4,14:1,15:1" for K16.
#
# Exit 0 = PASS, 1 = FAIL, 2 = usage/missing file.
use strict;
use warnings;

# --subset: the new file's records must all APPEAR in the oracle, without exhausting it. Needed
# where the oracle is a class list and the run only reaches part of it -- block 4.0.1777 yields one
# cover of a class that block 4.0.133 owns, so its RESULT is 1 of seed6.txt's 6.
my $subset = 0;
@ARGV = grep { $_ eq '--subset' ? (($subset = 1), 0)[1] : 1 } @ARGV;

my ($case, $newf, $oldf, $wantAut) = @ARGV;
die "usage: perl compare_migration.pl [--subset] <case> <new> <old> [aut-histogram]\n" unless defined $oldf;
for my $f ($newf, $oldf) {
    unless (-e $f) { print "FAIL $case: $f does not exist\n"; exit 2 }
}

# ---- read a file of records in any of the three layouts ---------------------
# A line that holds ONLY integers (and quotes/spaces) is a matrix row; anything else starts a new
# record. Returns a hash: normalized record text -> count, plus the |Aut| values seen in headers.
sub read_records {
    my $fn = shift;
    open my $fh, '<', $fn or die "cannot open $fn: $!";
    my (%recs, @auts, @cur, $pendingAut);
    my $flush = sub {
        return unless @cur;
        $recs{ join("|", @cur) }++;
        push @auts, $pendingAut if defined $pendingAut;
        @cur = (); $pendingAut = undef;
    };
    while (my $l = <$fh>) {
        if ($l =~ /^\s*[#0-9]/ && $l !~ /"/ && $l !~ /^\s*\d+(\s+\d+)+\s*$/) {
            # a header line: "#7 |Aut| = 12", "# block .. |Aut| 2 new", "  7: |Aut(M)| =  0, .."
            $flush->();
            if    ($l =~ /\|Aut\(M\)\|\s*=\s*(\d+)/) { $pendingAut = $1 }
            elsif ($l =~ /\|Aut\|\s*=?\s*(\d+)/)     { $pendingAut = $1 }
            next;
        }
        my @v = ($l =~ /(\d+)/g);
        if (@v >= 4) { push @cur, join(" ", @v); next }   # a matrix row
        $flush->();                                       # blank / separator
    }
    $flush->();
    close $fh;
    return (\%recs, \@auts);
}

my ($newR, $newA) = read_records($newf);
my ($oldR, undef) = read_records($oldf);

# ---- compare the record SETS ------------------------------------------------
my $fail = 0;
my @onlyNew = grep { !exists $oldR->{$_} } keys %$newR;
my @onlyOld = grep { !exists $newR->{$_} } keys %$oldR;
my @countDiff = grep { exists $oldR->{$_} && $oldR->{$_} != $newR->{$_} } keys %$newR;

my $nNew = 0; $nNew += $_ for values %$newR;
my $nOld = 0; $nOld += $_ for values %$oldR;

if ($subset) {
    if (@onlyNew) {
        $fail = 1;
        printf "FAIL %s: %d of %d record(s) are NOT in the oracle\n", $case, scalar @onlyNew, scalar keys %$newR;
        printf "      first: %s...\n", substr($onlyNew[0], 0, 96);
    } else {
        printf "PASS %s: all %d record(s) appear in the oracle (%d of its %d classes)\n",
               $case, $nNew, scalar keys %$newR, scalar keys %$oldR;
    }
} elsif (@onlyNew || @onlyOld || @countDiff) {
    $fail = 1;
    printf "FAIL %s: matrices differ -- %d record(s) only in the new file, %d only in the baseline, %d with a different multiplicity\n",
           $case, scalar @onlyNew, scalar @onlyOld, scalar @countDiff;
    for my $r ((@onlyNew ? $onlyNew[0] : ()), (@onlyOld ? $onlyOld[0] : ())) {
        my $where = exists $newR->{$r} ? "new only" : "baseline only";
        printf "      first differing record (%s): %s\n", $where, substr($r, 0, 96) . "...";
    }
} else {
    printf "PASS %s: matrices identical across the format change (%d records, %d distinct)\n",
           $case, $nNew, scalar keys %$newR;
}
printf "      records: new=%d baseline=%d\n", $nNew, $nOld if !$subset && $nNew != $nOld;

# ---- check the |Aut| column against the oracle ------------------------------
my %got; $got{$_}++ for @$newA;
my $gotStr = join(",", map { "$_:$got{$_}" } sort { $a <=> $b } keys %got);
if (defined $wantAut && length $wantAut) {
    my %want; for my $t (split /,/, $wantAut) { my ($k, $v) = split /:/, $t; $want{$k} = $v }
    my $wantStr = join(",", map { "$_:$want{$_}" } sort { $a <=> $b } keys %want);
    if ($gotStr eq $wantStr) {
        print  "PASS $case: |Aut| histogram matches the oracle -- $gotStr\n";
    } else {
        $fail = 1;
        print  "FAIL $case: |Aut| histogram is $gotStr, oracle says $wantStr\n";
    }
} else {
    print "     $case: |Aut| histogram (no oracle given) -- $gotStr\n";
}
exit($fail ? 1 : 0);
