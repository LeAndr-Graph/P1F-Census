# Assert the TOTAL node count of a run log.
#
# Why this exists.  compare.pl checks the CLASSES a case wrote, which is the coarse
# signal: a change that leaves the class set alone but explores ten times the tree --
# a broken prune, a lost symmetry cut -- passes it silently.  The node count is the
# sensitive one, and regression\K18-t9-a4-block1777-1 already documents an
# expected count for exactly that reason.
#
# The count is exact, not approximate.  progressTick flushes a worker's local counter
# only on a 1024-node boundary, so a K14 block -- far smaller than that -- used to
# report nothing at all and drop its residual.  The k14 copy settles every worker's
# residual when a block finishes, which makes the total exact and independent of how
# the threads happened to interleave.  Verified stable over repeated runs.
#
#   perl check_nodes.pl <case> <log> <expected>
use strict; use warnings;

my ($case, $log, $want) = @ARGV;
@ARGV == 3 or die "usage: check_nodes.pl <case> <log> <expected>\n";

open my $f, '<', $log or do { print "FAIL $case  (cannot read $log: $!)\n"; exit 1 };
my $got;
while (my $l = <$f>) { $got = $1 if $l =~ /^=\s*TOTAL\s*\|[^|]*\|\s*([\d,]+)\s*\(/ }
close $f;

unless (defined $got) { print "FAIL $case  (no TOTAL node count found in $log)\n"; exit 1 }
(my $n = $got) =~ s/,//g;

if ($n == $want) { print "PASS $case  (nodes $got, as expected)\n"; exit 0 }
print "FAIL $case  (nodes $got, expected $want)\n";
print "     A different node count with the SAME classes means the search tree changed:\n";
print "     a prune, an ordering or a symmetry cut. Find out which before re-baselining.\n";
exit 1;
