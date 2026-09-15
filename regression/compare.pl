#!/usr/bin/perl
# compare.pl -- verdict for one p1f regression case.
#
#   perl compare.pl <case> <actual> <expected> [<diff-out>]
#
# Compares the run's record file against the frozen baseline in three steps, so
# the cheap answer comes first and sorting only happens WHEN NEEDED:
#
#   1. byte compare            -> PASS "byte-identical"
#   2. sort records, compare   -> PASS "record order differs, content identical"
#      (the engine appends REP_INFO records in thread-discovery order, so two
#       correct runs of a multi-order case routinely disagree on order alone)
#   3. still different         -> FAIL + a content report, and the full sorted
#                                 diff written to <diff-out>
#
# Handles the record file p1f writes now, and the two legacy layouts its frozen
# baselines were captured in:
#
#   RESULT      "#7 |Aut| = 12" + quoted matrix rows -- the only thing p1f writes
#               today. The leading #<seq> counts write order within one file and
#               renumbers whenever records land in a different order, so it is
#               stripped before comparing, exactly like the P-file counter.
#
# LEGACY (read-only: no p1f build produces these any more)
#   REP_INFO    "# block <a.b.c>  pattern <SW...>  |Aut| <n>  new" + matrix rows
#               ("# dup block ..." attribution lines are compared as their own
#                one-line records)
#   KNA2_RESULT "    7: |Aut(M)| =  0,  Cycles:153(18)" + quoted matrix rows
#               (the leading counter renumbers when order changes, so it is
#                stripped before comparing -- K16/K20 have no REP_INFO)
#
# Exit 0 = PASS, 1 = FAIL, 2 = usage/missing file.
use strict;
use warnings;

my ($case, $actual, $expected, $diffOut) = @ARGV;
die "usage: perl compare.pl <case> <actual> <expected> [<diff-out>]\n" unless defined $expected;

for my $f ($actual, $expected) {
    unless (-e $f) {
        print "FAIL $case: $f does not exist\n";
        print "      (freeze a baseline with:  copy $actual $expected)\n" if $f eq $expected;
        exit 2;
    }
}

# ---- step 1: byte compare ---------------------------------------------------
my $ra = slurp($actual);
my $rb = slurp($expected);
my @A  = records($ra);
my @B  = records($rb);
if ($ra eq $rb) {
    printf "PASS %s  (%s, byte-identical)\n", $case, tally(\@A);
    exit 0;
}

# ---- step 2: sorted compare -------------------------------------------------
my @SA = sort { $a->{key} cmp $b->{key} } @A;
my @SB = sort { $a->{key} cmp $b->{key} } @B;
my $sa = join('', map { $_->{norm} } @SA);
my $sb = join('', map { $_->{norm} } @SB);
if ($sa eq $sb) {
    printf "PASS %s  (%s; record ORDER differs between runs, content identical)\n",
           $case, tally(\@A);
    exit 0;
}

# ---- step 3: report ---------------------------------------------------------
my (%inA, %inB);
$inA{$_->{key}}++ for @A;
$inB{$_->{key}}++ for @B;
my @onlyA = grep { ($inA{$_->{key}} || 0) > ($inB{$_->{key}} || 0) } dedup(@SA);
my @onlyB = grep { ($inB{$_->{key}} || 0) > ($inA{$_->{key}} || 0) } dedup(@SB);

print "FAIL $case\n";
printf "  compared      : %s  vs  %s   (sorted, record order ignored)\n", $actual, $expected;
printf "  class records : got %d, expected %d\n", count(\@A), count(\@B);
printf "  |Aut|         : got %s\n                  expected %s\n", hist(\@A), hist(\@B);
printf "  dup lines     : got %d, expected %d\n", dups(\@A), dups(\@B);
show("only in $actual",   \@onlyA);
show("only in $expected", \@onlyB);
if (defined $diffOut) {
    if (open my $D, '>', $diffOut) {
        print $D "=== $actual (sorted) ===\n", $sa, "\n=== $expected (sorted) ===\n", $sb;
        close $D;
        print "  full sorted dump: $diffOut\n";
    }
}
exit 1;

# -----------------------------------------------------------------------------
sub slurp { my ($f) = @_; open my $H, '<', $f or die "cannot read $f: $!\n"; local $/; my $s = <$H>; close $H; $s =~ s/\r\n/\n/g; return $s }

# Split a record file into records. A record starts at a REP_INFO '# block' /
# '# dup block' header or at a KNA2_RESULT '<n>: |Aut(M)|' header; every later
# line (matrix rows, '# canon:' lines) belongs to the record above it. The sort
# key is the record BODY -- the canonical matrix, which is the class identity
# and is identical across runs -- so headers carrying run-local numbering never
# affect the ordering.
sub records {
    my ($text) = @_;
    my (@recs, $cur);
    for my $line (split /\n/, $text) {
        my $isHdr = ($line =~ /^#\d+\s+\|Aut\| *=/)            # RESULT  "#7 |Aut| = 12"
                 || ($line =~ /^# (?:dup )?block /)                # REP_INFO (legacy baselines)
                 || ($line =~ /^\s*\d+:\s*\|Aut\(M\)\|/);            # P-file  (legacy baselines)
        if ($isHdr) { push @recs, $cur if $cur; $cur = { hdr => $line, body => [] }; next }
        next unless $cur;
        next if $line =~ /^\s*$/;
        push @{ $cur->{body} }, $line;
    }
    push @recs, $cur if $cur;
    for my $r (@recs) {
        my $hdr = $r->{hdr};
        $hdr =~ s/^\s*\d+:/RESULT:/;                       # strip the P-file counter
        $hdr =~ s/^#\d+\s+/RESULT: /;                       # strip the RESULT #<seq>: it renumbers
                                                            # with record order, which is thread-local
        $r->{dup}  = ($r->{hdr} =~ /^# dup block /) ? 1 : 0;
        $r->{aut}  = ($r->{hdr} =~ /\|Aut\|\s*=?\s*(\d+)/) ? $1 : undef;
        $r->{key}  = join("\n", @{ $r->{body} }) || $hdr;  # body = canonical matrix; dup lines key on their text
        $r->{norm} = $hdr . "\n" . $r->{key} . "\n";
    }
    return @recs;
}
sub dedup { my %seen; return grep { !$seen{$_->{key}}++ } @_ }
sub count { my ($r) = @_; return scalar grep { !$_->{dup} } @$r }
sub dups  { my ($r) = @_; return scalar grep {  $_->{dup} } @$r }
sub hist {
    my ($r) = @_; my %h;
    $h{ $_->{aut} }++ for grep { !$_->{dup} && defined $_->{aut} } @$r;
    return '(none recorded -- P-file carries no |Aut|)' unless %h;
    return join ' ', map { "$_:$h{$_}" } sort { $a <=> $b } keys %h;
}
sub show {
    my ($label, $r) = @_;
    printf "  %s (%d)%s\n", $label, scalar(@$r), @$r ? ':' : '';
    my $n = 0;
    for my $rec (@$r) {
        if (++$n > 10) { printf "      ... and %d more\n", scalar(@$r) - 10; last }
        my $first = $rec->{body}[0] // '';
        printf "      %s\n        %s\n", $rec->{hdr}, $first;
    }
}
sub tally {
    my ($r) = @_;
    my $c = count($r); my $d = dups($r);
    return $d ? "$c class records + $d dup lines" : "$c class records";
}
