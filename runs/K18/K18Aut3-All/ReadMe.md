# K18Aut3-All -- what this case computes

Every one-factorization of K18 whose full symmetry group is larger than order 2. A full run
gives 531 classes, `aut{3:351 4:144 8:22 16:12 17:1 272:1}`.

Classes with a group of exactly order 2 are out of scope here. They are the three census
cases K18-t8-a0, K18-t9-a0 and K18-t9-a4, and the four result files together are the whole
K18 classification.

The log is a progress report and nothing else. Everything that explains the words on it
is here. Anything more specific than this is in the source.

## Reading a row

The log is one table. Each leg of the run is one row, and its first two cells -- `Symmetry`
and `Type` -- name the family that leg enumerates:

    Factorization of K18 P1F |Aut| > 2

    --------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Leg nodes (rate)       | Total saved(duplicates)
      order 4  | 4⁴ 1²          |    8.0s | 1,821,756 (228/ms)     | 131(0) |Aut|={4:96 8:22 16:12 272:1}
      order 5  | 5² 1⁸          | skipped
      order 17 | 17 1           |   89.9s | 17 (-)                 | 180(1) |Aut|={4:144 8:22 16:12 17:1 272:1(1)}
    ~ so far   |                |    6min | 67,000,000 (223/ms)    | 363(2) |Aut|={3:183 4:144(1) 8:22 16:12 17:1 272:1(1)}
      order 3  | 3⁶             |   64min | 829,826,761 (222/ms)   | 531(2) |Aut|={3:351 4:144(1) 8:22 16:12 17:1 272:1(1)}

    --------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Total nodes (average)  | Saved
    = TOTAL    |                |   64min | 836,214,247 (218/ms)   | 531 |Aut|={3:351 4:144 8:22 16:12 17:1 272:1}
    --------------------------------------------------------------------------------------

Most of that is a real run of 2026-08-29, with rows left out; `Elapsed` and the duplicate halves
are reconstructed, because the old log did not record them.

The first column is the marker: blank is a finished leg, `~` a leg still running, `=` the run
total. WHICH quantity a column holds is fixed by its NAME, not by the marker. `Elapsed` and
`Total saved(duplicates)` are CUMULATIVE -- the run so far, which is the number you want when
reading a log as it runs; a leg's own is one subtraction from the row above. `Leg nodes` and its
rate are that leg's own, because they say how fast THAT subset searched. Above the `=` row the
names change to `Total nodes (average)` and plain `Saved`, which is what that row holds.

The column names repeat every 50 rows and once above the `=` row.

**"symmetry of order k"** means a relabeling of the 18 players that maps the design to
itself and, applied k times and not fewer, gives back the labeling you started with. It is
the relabeling that comes back, not the design: the design is unchanged by every single
application. The smallest k is what "order" means, so an order-4 symmetry is one that
returns after 4 applications and not after 1 or 2.

**"type"** is the shape of that relabeling, written as cycle lengths with how many of each.
`4⁴ 1²` is four 4-cycles and two players left in place; `17 1` is a single 17-cycle and one
fixed player, the exponent 1 being left off. Players left in place are always shown -- `3⁵ 1`
is not the same statement as `3⁵`. The order is the least common multiple of the cycle
lengths, so the type determines the order but not the other way round: order 4 has both
`4⁴ 1²` and `4⁴ 2`.

**"symmetry group V4"** is the Klein four-group: four symmetries, each one undoing itself
when applied twice. Its shape is written `fx<a>,<b>,<c> F<n>`: how many players each of the
three involutions leaves in place, and how many free 4-orbits the rest forms. The engine
also knows `E9 = C3xC3`, nine symmetries each returning to the start after three
applications, and `S3`, the six ways to rearrange three things; neither occurs for K18.

## Which orders are searched, and why only those

`REP_ORDERS=4,5,7,17,V4,3` in `run.bat`. That set is exactly the cells not settled by a
theorem. Orders 11 and 13 are empty by the odd-prime parity theorem. Composite orders are
covered by power reduction: a group of order 6 contains an element of order 3, so it is
found by the order-3 leg. Klein four-groups have no single element generating them, which
is why V4 is swept separately.

## Legs that report `skipped`

A leg whose row reads `skipped` in `Elapsed`, with every following cell blank, searched
nothing at all. Two different things cause that, and the log deliberately does not say
which -- it is a progress report.

- **Empty by proof.** The odd-prime parity theorem rules out a type with an odd number of
  p-cycles when p does not divide N-1: counting factor orbits forces a fixed factor, and a
  fixed factor has to pair the fixed players among themselves, which needs an even count.
  `REP_NOSKIP=1` searches those types anyway. For V4 the order-2 parity theorem and the
  Klein-four counting theorem rule out shapes containing a fixed-point-free involution when
  N is a multiple of 4; N = 18 is not, so no V4 shape is dropped for that reason here.
- **No admissible start.** The type is arithmetically possible but no opening configuration
  survives the first constraints, so the search has nothing to begin from and reports zero
  nodes. `5² 1⁸`, `3² 1¹²` and `3⁴ 1⁶` end this way for K18.

A leg that searched and found nothing is not skipped: its row carries an elapsed time, a node
count and a `Total saved(duplicates)` cell unchanged from the row above, as `7² 1⁴` does.

## Duplicates

A class can be reached by more than one leg -- the order-4 leg and the order-17 leg both
find the class with the group of order 272, and both find P47 through order 4 and through
V4. It is written by the first leg that reaches it, and counted as a duplicate by the rest,
so the result file holds each class once. The `Total saved(duplicates)` column carries both
halves, so the file is reconstructible from the log.

Saved and duplicates share one column -- `531(2)` -- because they are read against each other
and never apart. Its histogram merges the same way, ascending in `|Aut|`: a group order that was
saved prints bare, one that was rejected prints in parentheses, and one that was both prints
`k:saved(dups)`.

The saved half counts what the run has actually **written**, not what it found, so the last data
row's saved histogram is exactly the published census -- and the `=` row repeats that half
alone. A found-based histogram would total 533 and reconcile with nothing. The duplicate half
matters because a leg can find a class and write none, and there its `|Aut|` is the only class
information that leg contributes.

## Expected time

About 66 minutes at 10 threads. The order-3 `3⁶` leg alone is 64 of them: it is a full
enumeration of the even-t types and dominates everything else. To run the rest on its own,
set `REP_ORDERS=4,5,7,17,V4`.

All 531 classes are in the catalog, `AllResults/K18_P1F_aut_gt1.txt`, and the Compare step below
checks a run against it. A re-run has to reproduce these classes, not read them.

## The Compare step

The bat finishes by looking every class it wrote up in the catalog,
`AllResults/K18_P1F_aut_gt1.txt`, and reporting **Ok** or **Fault** with a histogram over the
size of the symmetry group. A result file with no results is Ok, which is the normal outcome for
most block ranges.

It checks present or not present, and nothing else. It cannot tell you a class the run should
have produced and did not: that needs the block each class belongs to and a reading of the log.
A Fault means the run wrote something the catalog does not have, or wrote a known class under a
different group order, and it fails the bat with exit code 3.

## Changing the thread count

`run.bat` line `SET "RUNARGS=18 10"`. The first number is N and must stay 18. The second
is the thread count, shipped at 10.
