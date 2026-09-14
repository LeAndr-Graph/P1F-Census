# K16Aut3-All -- what this case computes

Every one-factorization of K16 whose full symmetry group is larger than order 2. A full run
gives 30 classes, `{3:19, 5:5, 7:4, 14:1, 15:1}`. That is the published K16 count and it
matches the literature exactly, which is what makes this case the engine's external check at
a size where nothing else can be verified from outside.

Classes with a group of exactly order 2 are out of scope **for this case**, but they are no
longer out of reach for K16: `runs/K16Aut2` is the order-2 leg, and it finishes in about
seven minutes by stopping at the 60th class rather than proving there is no 61st. Since
2026-09-01 the catalog both cases are checked against is their union,
`AllResults/K16_P1F_aut_gt1.txt` -- 89 classes, `{2:59, 3:19, 5:5, 7:4, 14:1, 15:1}`, which
is the complete K16 classification.

K18 and K20 are still the hard ones. K18's `|Aut| = 2` classes come from its three
block-driven census cases; K20's have never been enumerated.

The log is a progress report and nothing else. Everything that explains the words on it
is here. Anything more specific than this is in the source.

## Reading a row

The log is one table. Each leg of the run is one row, and its first two cells -- `Symmetry`
and `Type` -- name the family that leg enumerates:

    Factorization of K16 P1F |Aut| > 2

    ----------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Leg nodes (rate)       | Total saved(duplicates)
      order 3  | 3 1¹³          | skipped
      order 3  | 3² 1¹⁰         | skipped
      order 3  | 3³ 1⁷          | skipped
      order 3  | 3⁴ 1⁴          |    0.7s | 171,989 (245/ms)       | 0(0)
      order 3  | 3⁵ 1           |    4.0s | 1,177,442 (362/ms)     | 20(0) |Aut|={3:19 15:1}
      order 5  | 5 1¹¹          | skipped
      order 5  | 5² 1⁶          |    4.0s | 3 (-)                  | 20(0) |Aut|={3:19 15:1}
      order 5  | 5³ 1           |    4.1s | 8,906 (93/ms)          | 25(1) |Aut|={3:19 5:5 15:1(1)}
      order 7  | 7² 1²          |    4.1s | 456 (6/ms)             | 30(1) |Aut|={3:19 5:5 7:4 14:1 15:1(1)}

    ----------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Total nodes (average)  | Saved
    = TOTAL    |                |    4.1s | 1,358,796 (329/ms)     | 30 |Aut|={3:19 5:5 7:4 14:1 15:1}
    ----------------------------------------------------------------------------------------

That is a real run, every leg of it. The first column is the marker:
blank is a finished leg, `~` a leg still running, `=` the run total. WHICH quantity a column
holds is fixed by its NAME, not by the marker. `Elapsed` and `Total saved(duplicates)` are
CUMULATIVE -- the run so far, which is the number you want when reading a log as it runs; a
leg's own is one subtraction from the row above. `Leg nodes` and its rate are that leg's own,
because they say how fast THAT subset searched. Above the `=` row the names change to
`Total nodes (average)` and plain `Saved`, which is what that row holds.

The column names repeat every 50 rows and once above the `=` row.

## The order set, and why only these

`REP_ORDERS=3,5,7` in `run.bat`. Every K16 automorphism group order is 3, 5, 7, 14 or 15, so
those three primes reach all of them: `|Aut| = 14` contains an element of order 7 and is found
by the order-7 leg, and `|Aut| = 15` contains elements of order 3 and of order 5 and is found
by both.

There is no order-4 leg and no V4 sweep. K16 has no automorphism group order divisible by 4,
and N = 16 is a multiple of 4, so the order-2 parity theorem rules out the fixed-point-free
involutions a Klein four-group would need.

## Legs that report `skipped`

A leg whose row reads `skipped` in `Elapsed`, with every following cell blank, searched nothing
at all -- either the type is empty by the odd-prime parity theorem, or it is arithmetically
possible but no opening configuration survives the first constraints. The log deliberately does
not say which; it is a progress report.

## Duplicates

`|Aut| = 15` is found by both the order-3 and the order-5 leg. It is written by the first and
counted as a duplicate by the second, so 31 records are emitted for 30 distinct classes.
The `Total saved(duplicates)` column carries both halves, so the file is reconstructible from
the log. Its saved half counts what the run has actually written, so the last data row shows
exactly the 30 and the `=` row repeats them.

## Expected time

Under 10 seconds at 10 threads.

## The Compare step

The bat finishes by looking every class it wrote up in the catalog,
`AllResults/K16_P1F_aut_gt1.txt`, and reporting **Ok** or **Fault** with a histogram over the
size of the symmetry group. `--n 16` picks that catalog: there is one per N, each named for
what it holds.

It checks present or not present, and nothing else. It cannot tell you a class the run should
have produced and did not. A Fault means the run wrote something the catalog does not have, or
wrote a known class under a different group order, and it fails the bat with exit code 3.

If the catalog file is ever missing, the step reports `Compare skipped`, names it, and leaves
the bat at exit 0: the run's results are unaffected and only the look-up was impossible.

## Changing the thread count

`run.bat` line `SET "RUNARGS=16 10"`. The first number is N and must stay 16. The second is
the thread count, shipped at 10.
