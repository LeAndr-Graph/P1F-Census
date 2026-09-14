# K20Aut4-All -- what this case computes

Every one-factorization of K20 whose symmetry group has **more than 3 elements**. A full run
gives 230 classes, `{6:168, 9:46, 18:9, 19:3, 57:1, 171:2, 342:1}`. The `|Aut| = 9` row is 46
= 39 of C9 type plus 7 of C3xC3 type, the latter found by the `E9` leg.

## What `|Aut| > 3` excludes

**`|Aut| = 3` exactly is NOT here, and it is not empty.** The order-3 leg is the one cell no
theorem settles and no run finishes: its type `3^6 1^2` is about 4.9e12 cover-nodes. A capped
harvest has already found on the order of 100 such classes, so the cell is known to be
populated and its size is unknown. A class with `|Aut| = 3` admits an automorphism of order 3
but has only 3 elements, so it falls outside `> 3` -- which is why this case is named and
scoped by GROUP SIZE and not by "admits an automorphism of order >= 3". Those two predicates
differ by exactly this cell, and stating the second would claim the run does not deliver.

`|Aut| = 2` is out of scope as well, and for the usual reason: the order-2 search does not
finish at K20 any more than at K16 or K18.

What is NOT a gap here, though it is at K18: the elementary-abelian 2-groups. V4, C2^3 and the
rest have more than 3 elements and contain no element of order 3 or more, so no cyclic leg
sees them -- but the V4 sweep does, and it comes back empty, because both arithmetic V4 shapes
contain a fixed-point-free involution and 4 divides 20 (order-2 parity theorem). Every larger
elementary-abelian 2-group contains a V4, so all of them are empty too.

The log is a progress report and nothing else. Everything that explains the words on it
is here. Anything more specific than this is in the source.

## Reading a row

The log is one table. Each leg of the run is one row, and its first two cells -- `Symmetry`
and `Type` -- name the family that leg enumerates:

    Factorization of K20 P1F |Aut| > 2

    ----------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Leg nodes (rate)       | Total saved(duplicates)
      order 19 | 19 1           |    0.3s | 65 (0/ms)              | 7(0) |Aut|={19:3 57:1 171:2 342:1}
      E9       | k2,2,0,0 R0 f8 | skipped
      E9       | k2,2,2,0 R0 f2 | skipped
      E9       | k4,2,0,0 R0 f2 | skipped
      E9       | k0,0,0,0 R2 f2 |    4.4s | 9,276 (2/ms)           | 14(0) |Aut|={9:7 19:3 57:1 171:2 342:1}
      S3       | t2 d7 R0 f0    | skipped
      S3       | t0 d6 R1 f2    | skipped
      S3       | t2 d4 R1 f0    | skipped
      S3       | t0 d3 R2 f2    | skipped
      S3       | t2 d1 R2 f0    |   16.0s | 61,350 (5/ms)          | 14(0) |Aut|={9:7 19:3 57:1 171:2 342:1}
      S3       | t0 d0 R3 f2    |   16.9s | 1,100 (1/ms)           | 14(0) |Aut|={9:7 19:3 57:1 171:2 342:1}
      order 5  | 5² 1¹⁰         | skipped
    ~ so far   |                |  271.5s | 5,914,624 (23/ms)      | 14(0) |Aut|={9:7 19:3 57:1 171:2 342:1}
    ~ so far   |                |  571.5s | 13,118,464 (24/ms)     | 14(0) |Aut|={9:7 19:3 57:1 171:2 342:1}
      order 5  | 5⁴             |   14min | 20,759,456 (24/ms)     | 14(0) |Aut|={9:7 19:3 57:1 171:2 342:1}
      order 6  | 6³ 1²          |   15min | 1,642,081 (55/ms)      | 191(1) |Aut|={6:168 9:7 18:9 19:3 57:1 171:2 342:1(1)}
      order 6  | 6² 2³ 1²       |   15min | 4 (0/ms)               | 191(1) |Aut|={6:168 9:7 18:9 19:3 57:1 171:2 342:1(1)}
      order 6  | 6 2⁶ 1²        |   15min | 3 (0/ms)               | 191(1) |Aut|={6:168 9:7 18:9 19:3 57:1 171:2 342:1(1)}
      order 7  | 7² 1⁶          |   15min | 12,803 (121/ms)        | 191(1) |Aut|={6:168 9:7 18:9 19:3 57:1 171:2 342:1(1)}
      order 9  | 9² 1²          |   15min | 11,219 (3/ms)          | 230(13) |Aut|={6:168 9:46 18:9(9) 19:3 57:1 171:2(2) 342:1(2)}

    ----------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Total nodes (average)  | Saved
    = TOTAL    |                |   15min | 22,497,357 (25/ms)     | 230 |Aut|={6:168 9:46 18:9 19:3 57:1 171:2 342:1}
    ----------------------------------------------------------------------------------------

That is a real run of the shipped order set. The first column is the marker:
blank is a finished leg, `~` a leg still running, `=` the run total. WHICH quantity a column
holds is fixed by its NAME, not by the marker. `Elapsed` and `Total saved(duplicates)` are
CUMULATIVE -- the run so far, which is the number you want when reading a log as it runs; a
leg's own is one subtraction from the row above. `Leg nodes` and its rate are that leg's own,
because they say how fast THAT subset searched. Above the `=` row the names change to
`Total nodes (average)` and plain `Saved`, which is what that row holds.

The column names repeat every 50 rows and once above the `=` row.

The two `~` rows are the fourteen-minute order-5 leg reported while it ran: the timer fires
every five minutes, and the counts stand still because that leg finds nothing.

`Elapsed` on the `=` row is the sum of the legs' own times, not the wall clock. The closing
`P1F-Census:` result line says 19 minutes, and the difference is the setup each leg does before its
search starts. Summing the legs is what makes the `=` row exactly the total of the table above.

The last five rows all read `15min`, and that is the formatting, not a stalled clock. `Elapsed`
prints seconds below 600 and whole minutes at or above; being cumulative, once a run passes ten
minutes every later row rounds to the same whole minute unless a leg takes 30 seconds or more.
Those four legs took about 4 seconds between them. `Leg nodes` is where their sizes show.

The title says `|Aut| > 2` on every size; it is the engine's own wording, and this case's real
scope is the paragraph above.

## The order set, and why these

`REP_ORDERS=19,V4,E9,S3,5,6,7,9` in `run.bat`. That is every cell not settled by a theorem.

- **19, 6, 9, E9** produce all 230 classes between them.
- **5 and 7** are expected-EMPTY. They are searched anyway. Order 5 is 14 of the run's 19
  minutes, all of it the type `5⁴`; order 7 costs a tenth of a second. Running them is the difference
  between a classification that *states* those cells are empty and one that *asserts* it.
- **V4 and S3** cost almost nothing. Both arithmetic V4 shapes contain a fixed-point-free
  involution and are skipped citing the order-2 parity theorem; S3 yields nothing.

To reproduce just the productive legs in about four minutes, edit `run.bat` to
`SET "REP_ORDERS=19,V4,E9,S3,6,9"`. That still gives 230 -- it is the set
`regression\K20Aut4-All-1` uses, for exactly that reason.

## Duplicates

A class can be reached by more than one leg: `|Aut| = 342` is found by orders 6 and 9 and by
order 19, and the nine `|Aut| = 18` classes by orders 6 and 9. It is written by the first leg
that reaches it and counted as a duplicate by the rest, so 243 records are emitted for 230
distinct classes. The `Total saved(duplicates)` column carries both halves, so the file is
reconstructible from the log. Its saved half counts what the run has actually written, so the
last data row shows exactly the 230 and the `=` row repeats them.

## Expected time

About 19 minutes at 10 threads, 14 of them the empty order-5 leg. Dropping orders 5 and 7 as
above brings it to about 4 minutes and still gives 230.

## The Compare step

The bat finishes by looking every class it wrote up in the catalog,
`AllResults/K20_P1F_aut_gt3.txt`, and reporting **Ok** or **Fault** with a histogram over the
size of the symmetry group. `--n 20` picks that catalog: there is one per N, each named for
what it holds.

It checks present or not present, and nothing else. It cannot tell you a class the run should
have produced and did not. A Fault means the run wrote something the catalog does not have, or
wrote a known class under a different group order, and it fails the bat with exit code 3.

If the catalog file is ever missing, the step reports `Compare skipped`, names it, and leaves
the bat at exit 0: the run's results are unaffected and only the look-up was impossible.

## Changing the thread count

`run.bat` line `SET "RUNARGS=20 10"`. The first number is N and must stay 20. The second is
the thread count, shipped at 10.
