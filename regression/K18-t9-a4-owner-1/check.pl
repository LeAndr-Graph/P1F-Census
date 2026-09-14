#!/usr/bin/perl
# check.pl -- assert the block filters' behavior over four single-block runs.
#
# Two filters act at the same point, and between them these blocks exercise every path:
#
#   4.0.133    1 cover,  class #1, owner 133   -> save 1, reject 0
#   4.0.1777   1 cover,  class #1, owner 133   -> save 0, reject 1 as FOREIGN
#   4.0.2237   1 cover,  P47 |Aut|=16, owns it -> save 0, reject 1 as |Aut| != 2
#   4.0.17607  2 covers, class #2, owner 182   -> save 0, reject 2 = 1 foreign + 1 IN-BLOCK
#
# 4.0.17607 is the same-block case: the block genuinely yields the same class twice, so the
# canonized class key -- not ownership -- has to catch the second one. Ownership cannot: both
# covers have the same owner, so an owner test alone would accept or reject them together.
# 4.0.1777 is the case ownership exists for: a class this block really contains but does not
# own, which the old global g_harvest could only reject by remembering every class ever found.
#
# 4.0.2237 is the only known a=4 class with |Aut| > 2, so it is the only test that the general
# involution enumeration (rather than the tau = sigma0 shortcut) returns the right owner. Its
# RESULT is now EMPTY: the |Aut|>2 case defines and writes that class, so the census rejects it
# as a duplicate. The proof that the general-tau path still ran and still found P47 is the
# table row -- Saved(Duplicates) reading 0(1) -- plus the |Aut| histogram naming 16. The
# |Aut| filter is applied AFTER the owner test, so reaching it at all means the owner came back
# as 2237 itself. (expected_2237.txt is therefore gone: nothing produces that record here any
# more, and P47's record lives in AllResults/K18_P1F_aut_gt1.txt.)
use strict;
use warnings;

my $fail = 0;
sub ok  { my ($what) = @_; printf "  PASS  %s\n", $what }
sub bad { my ($what, $got) = @_; $fail++; printf "  FAIL  %s\n        got: %s\n", $what, $got }

sub records {                       # count "#n |Aut| = k" headers in a RESULT file
    my ($f) = @_;
    return (-1, "missing file $f") unless -e $f;
    open my $h, '<', $f or return (-1, "cannot open $f: $!");
    my $n = 0;
    while (<$h>) { $n++ if /^#\d+\s*\|Aut\|\s*=\s*\d+/ }
    close $h;
    return ($n, '');
}

sub slurp { my ($f) = @_; open my $h, '<', $f or return ''; local $/; my $s = <$h>; close $h; $s }

# ---- 1. record counts ---------------------------------------------------------------
my %want = ('result133.txt' => 1, 'result1777.txt' => 0, 'result2237.txt' => 0, 'result17607.txt' => 0);
for my $f (sort keys %want) {
    my ($n, $err) = records($f);
    if    ($n == $want{$f}) { ok "$f holds $want{$f} record(s)" }
    elsif ($n < 0)          { bad "$f should hold $want{$f} record(s)", $err }
    else                    { bad "$f should hold $want{$f} record(s)", "$n record(s)" }
}

# ---- 2. the one saved record is the right class --------------------------------------
for my $p (['result133.txt', 'expected_133.txt']) {
    my ($got, $exp) = @$p;
    my $g = slurp($got); my $e = slurp($exp);
    s/\r//g for ($g, $e);
    if (length $e && $g eq $e) { ok "$got matches $exp" }
    else { bad "$got must match $exp", (length $g ? "differs (" . length($g) . " vs " . length($e) . " bytes)" : "empty or missing") }
}

# ---- 3. the log must REPORT each rejection, not just drop it -------------------------
# Saved and Duplicates together account for everything a block found, so the RESULT file is
# reconstructible from the log alone. Which KIND of duplicate it was is on the [OWNER] lines.
#
# The log is a table now. Columns are separated by " | ", there is no
# Results column (it was Saved + Duplicates, both of which the next column shows), and Saved and
# Duplicates share ONE column written Total saved(duplicates), so a block row reads
#     <block> | <elapsed> | <block nodes> (<rate>) | <Saved>(<Duplicates>) ... | <Done%>
# and blockRow() builds the regex for one. Elapsed, nodes and rate are volatile and matched
# loosely; the two counts are the assertion.
#
# That column is CUMULATIVE over the range. Every case here runs ONE block, so cumulative and
# per-block are the same number and these assertions are unchanged in value -- but a multi-block
# case added later must expect running totals, not per-block figures.
sub blockRow {
    my ($blk, $dups, $saved) = @_;
    return qr/^\s*\Q$blk\E\s*\|\s*\S+\s*\|\s*[\d,]+ \([^)]*\)\s*\|\s*\Q$saved\E\(\Q$dups\E\)/m;
}
my @logs = (
    ['owner133.log',   blockRow(133, 0, 1),
                       'block 133 row: Saved(1) Duplicates(0)'],
    ['owner1777.log',  blockRow(1777, 1, 0),
                       'block 1777 row: Saved(0) Duplicates(1)'],
    ['owner1777.log',  qr/\[OWNER\][^\n]*owned by 133\b/,
                       'block 1777 names the owning block 133'],
    ['owner2237.log',  blockRow(2237, 1, 0),
                       'block 2237 row: Saved(0) Duplicates(1) (P47 found, then rejected)'],
    ['owner2237.log',  qr/\[OWNER\][^\n]*\|Aut\|=16 -- rejected/,
                       'block 2237 says WHY it was rejected: |Aut|=16, not 2'],
    ['owner2237.log',  qr/duplicates by \|Aut\| > 2: aut\{16:1\}/,
                       'block 2237 end-of-run footnote reads aut{16:1}'],
    ['owner17607.log', blockRow(17607, 2, 0),
                       'block 17607 row: Saved(0) Duplicates(2)'],
    ['owner17607.log', qr/\[OWNER\][^\n]*owned by 182\b/,
                       'block 17607 reports the foreign class as such'],
    ['owner17607.log', qr/\[OWNER\][^\n]*already found in this block/,
                       'block 17607 reports the same-block repeat as such'],
);
for my $t (@logs) {
    my ($f, $re, $what) = @$t;
    my $s = slurp($f);
    if    (!length $s) { bad $what, "log $f missing or empty" }
    elsif ($s =~ $re)  { ok $what }
    else {
        my @l = grep { /^\s*(\d+|= TOTAL)\s|\[OWNER\]|\[F3COMPLETE\]/ } split /\r?\n/, $s;
        bad $what, (@l ? join(' | ', @l) : "no block row or OWNER line in $f");
    }
}

# ---- 4. the filters must not disturb the search ---------------------------------------
# They drop results; they never prune. Node counts must match the pre-filter run exactly.
# The count is the table's Nodes column, which carries thousands separators for the reader --
# strip them before comparing, as any tool reading it must.
my %nodes = ('owner133.log' => 10446848, 'owner1777.log' => 18240512, 'owner2237.log' => 10770432, 'owner17607.log' => 44960768);
for my $f (sort keys %nodes) {
    my $s = slurp($f);
    if ($s =~ /^= TOTAL\s*\|[^|]*\|\s*([\d,]+) \(/m) {
        my $got = $1; $got =~ tr/,//d;
        # one 1024-per-worker flush quantum of slack per thread, as the other a=4 cases allow
        if (abs($got - $nodes{$f}) <= 10240) { ok "$f nodes=$got (unchanged by the filters)" }
        else { bad "$f nodes should stay $nodes{$f}", $got }
    } else { bad "$f has a nodes count", "no = TOTAL row" }
}

print $fail ? "\nK18-t9-a4-owner-1: FAILED ($fail check(s))\n" : "\nK18-t9-a4-owner-1: PASSED\n";
exit($fail ? 1 : 0);
