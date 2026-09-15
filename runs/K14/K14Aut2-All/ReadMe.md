# K14Aut2-All -- what this case computes

Every one-factorization of K14 whose symmetry group is larger than the trivial one. A full
run gives 21 classes, `{2:3, 3:5, 4:1, 6:5, 12:5, 84:1, 156:1}`.

This is the **whole** non-rigid classification for K14, `|Aut| = 2` included, and it is reached
in one pass because K14's order-2 leg is cheap. K16's also finishes -- `K16Aut2` does it in
about 80 minutes -- but it is already slow enough that the case ships with a harvest target
rather than a full sweep. At K18 the order-2 search does not finish at all, and the `|Aut| = 2`
classes need a block-driven census instead: the three `K18-t8` / `K18-t9` cases. At K20 it has
not been done by any route. That is why this case is `|Aut| > 1` while its siblings are
`|Aut| > 2` and `|Aut| > 3`.

**Aut2, not Aut3.** A folder name here says `|Aut| >= N`: `K16Aut3-All` is `|Aut| > 2`,
`K20Aut4-All` is `|Aut| > 3`. This case reaches `|Aut| >= 2`, so `Aut2` is the honest name. It
was `K14Aut3-All` until 2026-08-30 -- a name that excluded three of the 21 classes it produces.
The regression case is still `regression\K14Aut3-All-1`; it runs the same order set, and
renaming it would churn a frozen baseline for nothing.

The log is a progress report and nothing else. Everything that explains the words on it
is here. Anything more specific than this is in the source.

## Reading a row

The log is one table. Each leg of the run is one row, and its first two cells -- `Symmetry`
and `Type` -- name the family that leg enumerates:

    Factorization of K14 P1F |Aut| > 2

    ----------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Leg nodes (rate)       | Total saved(duplicates)
      order 2  | 2⁶ 1²          |    2.4s | 193,661 (80/ms)        | 15(0) |Aut|={2:2 4:1 6:5 12:5 84:1 156:1}
      order 2  | 2⁷             |   23.1s | 1,414,215 (68/ms)      | 16(0) |Aut|={2:3 4:1 6:5 12:5 84:1 156:1}
      order 3  | 3² 1⁸          | skipped
      order 3  | 3⁴ 1²          |   23.3s | 10,975 (55/ms)         | 21(12) |Aut|={2:3 3:5 4:1 6:5(5) 12:5(5) 84:1(1) 156:1(1)}
      order 7  | 7²             |   23.3s | 39 (-)                 | 21(13) |Aut|={2:3 3:5 4:1 6:5(5) 12:5(5) 84:1(2) 156:1(1)}
      order 13 | 13 1           |   23.3s | 1 (-)                  | 21(14) |Aut|={2:3 3:5 4:1 6:5(5) 12:5(5) 84:1(2) 156:1(2)}

    ----------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Total nodes (average)  | Saved
    = TOTAL    |                |   23.3s | 1,618,891 (69/ms)      | 21 |Aut|={2:3 3:5 4:1 6:5 12:5 84:1 156:1}
    ----------------------------------------------------------------------------------------

That is a real run. The first column is the marker: blank is a finished leg, `~` a leg still
running, `=` the run total. WHICH quantity a column holds is fixed by its NAME, not by the
marker. `Elapsed` and `Total saved(duplicates)` are CUMULATIVE -- the run so far, which is the
number you want when reading a log as it runs; a leg's own is one subtraction from the row
above. `Leg nodes` and its rate are that leg's own, because they say how fast THAT subset
searched. Above the `=` row the names change to `Total nodes (average)` and plain `Saved`,
which is what that row holds.

So the last data row carries `21(14)` -- everything found, split into written and rejected --
and the `=` row repeats the written half alone, which is the census. The column names repeat
every 50 rows and once above the `=` row.

The title says `|Aut| > 2` on every size -- it is the engine's own wording for "bigger than
order 2", and at K14 the order-2 leg makes it an understatement: three classes of `|Aut| = 2`
are in the result too.

## The order set, and why only these

`REP_ORDERS=2,3,7,13` in `run.bat`. Those are the primes dividing any K14 automorphism group
order, and their union is the complete classification. Composite orders need no leg of their
own, by power reduction: a group of order 6 contains an element of order 3, so the order-3 leg
finds it; the same argument covers 4, 12, 84 and 156.

Order 2 is what makes K14 special. `2^7` is fixed-point-free and `2^6 1^2` fixes two players,
and both complete -- 16 classes between them, proven complete, which is the oracle every other
size's engine is validated against.

## Duplicates

A class can be reached by more than one leg -- `|Aut| = 84` is found by order 2, order 3 and
order 7, and `|Aut| = 156` by order 2, order 3 and order 13. It is written by the first leg
that reaches it and counted as a duplicate by the rest, so the result file holds each class
once: 35 records are emitted for 21 distinct classes. The `Total saved(duplicates)` column
carries both halves, so the file is reconstructible from the log.

Its saved half counts what the run has actually **written**, so the last data row shows exactly
the 21 and the `=` row repeats them.

## Expected time

About 20 seconds at 10 threads, nearly all of it the `2^7` leg.

## The Compare step

The bat finishes by looking every class it wrote up in the catalog,
`AllResults/K14_P1F_aut_gt1.txt`, and reporting **Ok** or **Fault** with a histogram over the
size of the symmetry group. `--n 14` picks that catalog: there is one per N, each named for
what it holds.

It checks present or not present, and nothing else. It cannot tell you a class the run should
have produced and did not. A Fault means the run wrote something the catalog does not have, or
wrote a known class under a different group order, and it fails the bat with exit code 3.

If the catalog file is ever missing, the step reports `Compare skipped`, names it, and leaves
the bat at exit 0: the run's results are unaffected and only the look-up was impossible.

## Changing the thread count

`run.bat` line `SET "RUNARGS=14 10"`. The first number is N and must stay 14. The second is
the thread count, shipped at 10.
