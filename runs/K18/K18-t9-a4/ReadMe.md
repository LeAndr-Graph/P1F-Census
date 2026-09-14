# K18-t9-a4 -- what this case computes

One-factorizations of K18 whose full symmetry group has order 2, whose symmetry is
fixed-point-free, and which fall in branch a = 4.

The log is a progress report and nothing else. Everything that explains the words on it
is here. Anything more specific than this is in the source.

## Reading a row

The log is one table, one row per block:

    Factorization of K18 P1F |Aut| = 2, symmetry type 2⁹, branch a=4
    Blocks 0-65039 selected for processing (REP_F3START=0 REP_F3STOP=65039)

    ------------------------------------------------------------------------------------------
      Block  | Elapsed | Block nodes (rate)     | Total saved(duplicates)    | Done%
      0      |   64.0s | 25,768,960 (403/ms)    | 0(0)                       |  0.00
      1      |  142.4s | 31,591,424 (403/ms)    | 3(0) |Aut|={2:3}           |  0.00
      2      |  213.2s | 28,526,592 (403/ms)    | 3(0) |Aut|={2:3}           |  0.01
      3      |  292.2s | 31,818,752 (403/ms)    | 9(0) |Aut|={2:9}           |  0.01
      ...

    ------------------------------------------------------------------------------------------
      Block  | Elapsed | Total nodes (average)  | Saved                      | Done%
    = TOTAL  |  717min | 11,643,608,576 (271/ms)| 860 |Aut|={2:860}          |  1.03
    ------------------------------------------------------------------------------------------

    [F3COMPLETE] canonical blocks in range: 448

The block numbers and counts above are from a real `K18-t8-a0` run; the shape of the table is
the same in every block-driven case.

The first column is the marker: blank is a finished block, `=` is the run total. WHICH quantity
a column holds is fixed by its NAME, not by the marker. `Elapsed` and `Total saved(duplicates)`
are CUMULATIVE -- the range so far, which is the number you want when reading a log as it runs;
this block's own is one subtraction from the row above. `Block nodes` and its rate are that
block's own, because they say how fast THAT block searched.

There is no periodic row here. A block row lands every minute or so and already carries the
cumulative figures, so a five-minute line would repeat the row above it. The `|Aut| > 2` cases
keep theirs, where one leg can run for an hour with nothing else printed.

`Block` is the raw block number, so the numbers have gaps: a block that is a relabeling of an
earlier one is skipped and produces no row. `Done%` counts blocks WALKED against the raw range
asked for -- skipped ones included, because they have still been got past -- so it never walks
backwards. It is the raw range, not the canonical count, because the canonical count is not
known until the run ends.

The footnote under the table says how many blocks in the range were canonical, i.e. actually
searched. A second footnote, `duplicates by |Aut| > 2`, appears when a class was found whose
symmetry group is bigger than order 2: it belongs to K18Aut3-All, not here, so it is rejected.

## The symmetry: type 2^9

A one-factorization is counted here if some relabeling of the 18 players, applied twice,
gives back the labeling you started with, and that relabeling maps the design to itself.
Type `2^9` says the relabeling swaps the players in 9 pairs and leaves nobody in place.
The log writes the type and the branch once, in the table title, and never repeats them.

## The branch: a = 4

A symmetry of order 2 leaves exactly one round of the schedule unchanged. Write P(alpha)
for the 9 pairs the symmetry swaps and F0 for that fixed round. Then

    |F0 intersect P(alpha)| = d = 9 - 2a

is how many of the swapped pairs the fixed round is built from, and it names the branch.
For a = 4 the fixed round contains exactly one of the 9 pairs. The other 8 games of that
round each pair up two different swapped pairs.

Only two branches exist. The dichotomy theorem shows d is either 9 or 1, so a is 0 or 4
and nothing else -- a = 1, 2 and 3 are impossible by proof, not merely unfound. The a = 0
branch is the sibling case K18-t9-a0.

## Blocks, and who gets credit for a solution

The column is 65,040 blocks, or starters: the distinct ways of filling in the opening
rounds. Each one is searched to completion, and the log reports one line per block. The
same solution can be reached from many of them, so each solution is credited to exactly
one block -- the first -- and every other block that runs into it rejects it. The credited
block is the lowest-numbered one of the whole column that contains the solution, and every
block works that number out the same way, so two ranges run separately are disjoint and
their results simply concatenate. No merge and no dedup afterwards.

That is why a range is self-contained and reads no baseline, and why `results = saved +
duplicates` holds on every line of the log: the file the run writes is reconstructible
from the log alone.

Of the 65,040 raw blocks, 59,872 are canonical and actually completed. The rest are
relabelings of an earlier block, are recognized as such in about a minute of scanning,
and are skipped with one summary line per consecutive run of them.

## The two kinds of duplicate

Both are reported with the same word, because both are the same thing: a result this run
declines to write because it is already accounted for.

1. **Another block owns it.** The solution really is in this block, but a lower-numbered
   block gets the credit. `REP_OWNERALL=1` names the owner of each; `REP_OWNER=0` switches
   the filter off and records every re-find instead.
2. **Its symmetry group is bigger than order 2.** A design whose full group has order 4,
   16, 272 and so on contains a symmetry of order 2, so this sweep finds it -- but it is
   defined and published by the K18Aut3-All case, not by this one. Writing it here would
   duplicate that case's output.

The end of the run prints one histogram of the second kind, `duplicates aut{...}`, keyed
by the size of the full symmetry group. Each class is counted there exactly once, by the
block that owns it, so a full-column run re-derives the |Aut| > 2 census of this branch by
a completely different route than the case that publishes it. For this branch the whole
column yields exactly one such class, P47, with a group of order 16, in block 2237.

## Expected time

About one minute per block on this machine at 10 threads: a 1,000-block range measured
997 minutes. The full column is therefore multi-week and is normally run as ranges. Set
the range with `REP_F3START` and `REP_F3STOP` in `run.bat` (the stop is the last block, included); both are always explicit and
there is no run-to-the-end default. To resume an interrupted run, take the last block the
log printed and start from its number plus one.

## The Compare step

The bat finishes by looking every class it wrote up in the catalog,
`AllResults/K18_P1F_aut_gt1.txt`, and reporting **Ok** or **Fault** with a histogram over the
size of the symmetry group. A result file with no results is Ok, which is the normal outcome for
most block ranges.

It checks present or not present, and nothing else. It cannot tell you a class the run should
have produced and did not: that needs the block each class belongs to and a reading of the log.
A Fault means the run wrote something the catalog does not have, or wrote a known class under a
different group order, and it fails the bat with exit code 3.

On this leg the first of those two is not necessarily an error. This is the one open stratum of
the classification, so a class the catalog does not have is a NEW class and the point of the
sweep. The report distinguishes the two: a class the catalog has never seen is counted under
`not present`, while a known class recorded under a different group order is named on its own
line. A discovery still fails the bat, deliberately, because it means the catalog is out of date
and somebody has to look.

## Changing the thread count

`run.bat` line `SET "RUNARGS=18 10"`. The first number is N and must stay 18. The second
is the thread count, shipped at 10.
