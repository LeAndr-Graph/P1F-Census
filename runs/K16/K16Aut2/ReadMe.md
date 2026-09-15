# K16Aut2 -- what this case computes

Every one-factorization of K16 whose symmetry group contains an involution. A run gives
60 classes, `{2:59, 14:1}`.

The 59 are every K16 class whose group is exactly order 2. The sixtieth has `|Aut| = 14`
and is reached here because 14 is even; it is the only even group order K16 has above 2,
and `K16Aut3-All` finds it too, by its order-7 leg.

**With `K16Aut3-All` this is the whole of the K16 non-rigid classification.** That case gives
the 30 classes with `|Aut| > 2`, `{3:19, 5:5, 7:4, 14:1, 15:1}`; this one gives the 59 with
`|Aut| = 2`. 59 + 30 = **89**, the published number of K16 classes whose automorphism group is
non-trivial. Neither case alone is that classification.

Their union is `AllResults/K16_P1F_aut_gt1.txt`, built 2026-09-01 and the catalog both cases
are now checked against: 89 classes, `{2:59, 3:19, 5:5, 7:4, 14:1, 15:1}`.

That file is **not** a census of all P1Fs of K16. K16 has 3,155 classes up to isomorphism, so
the 3,066 whose automorphism group is trivial are outside it -- and outside every case in this
repository. The representative method starts from an assumed automorphism, so a factorization
that has none is unreachable by it, at any size.

## The 60 limit, and what it buys

`run.bat` sets

    SET "REP_ORDER=2:60"

The `:60` is a harvest target: the run stops the moment the 60th distinct class is found.

Without it the search continues over the whole range to establish there is no 61st, and that
is nearly all of the wall time. Measured on the complete run, the 60th and last class arrives
at about **600 s** while the run itself takes **4,909 s**; the same complete run finished in
**36 minutes** on a second machine. Either way, roughly an hour of the time is spent proving
a negative after the last class is already in hand.

So the target is there to avoid an hour of waiting for a result that is known. The 60 classes
are the same with or without it.

## What the limit does not do

Stopping at 60 **reproduces** the answer; it does not **prove** it. A run that stops when it
reaches its target cannot tell you nothing else exists, because it stopped looking. The
completeness of the 60 rests on the full run, which was done once and gave exactly these.

To repeat that proof rather than the reproduction, set

    SET "REP_ORDER=2"

and expect the hour.

Setting a target also changes how the range is walked. With one, the engine loops the range,
re-diving and deduplicating, until the target is met. Without one it makes a single systematic
pass. That is why the node counts in the log will not match between the two modes.

The engine says all of this itself, on the two lines that bracket the run:

    [K16-REP] HARVEST MODE: stop after 60 distinct classes (COMPLETE only if 60 == true count, else a LOWER BOUND)
    [K16-REP] HARVESTED 60 classes (target 60 reached -- NOT proven complete)

## The log

One real run, in full:

    Factorization of K16 P1F |Aut| > 2

    ----------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Leg nodes (rate)       | Total saved(duplicates)
    [K16-REP] HARVEST MODE: stop after 60 distinct classes (COMPLETE only if 60 == true count, else a LOWER BOUND)
    ~ so far   |                |  299.1s | 157,875,200 (528/ms)   | 58(0) |Aut|={2:58}
      order 2  | 2⁷ 1²          |  390.5s | 205,153,668 (525/ms)   | 60(0) |Aut|={2:59 14:1}
    [K16-REP] HARVESTED 60 classes (target 60 reached -- NOT proven complete)

    ----------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Total nodes (average)  | Saved
    = TOTAL    |                |  390.5s | 205,153,668 (525/ms)   | 60 |Aut|={2:59 14:1}
    ----------------------------------------------------------------------------------------

One leg, one type. `2⁷ 1²` is the only non-empty involution type at K16: an involution of a
perfect one-factorization fixes at most 2 vertices, which retires `2¹` through `2⁶`, and the
fixed-point-free `2⁸` is empty because 16 is divisible by 4. So the whole order-2 search is
this single row.

The `~ so far` row is the 300 s progress print, not a finished leg. It shows 58 classes with
the sixtieth still to come -- the last two arrive in the following 90 s.

**The title line reads `|Aut| > 2` and is wrong for this case.** It is a fixed banner the
engine prints for every K16 run, and this case searches order 2. Ignore it; the `= TOTAL` row
is the one that describes what was actually found.

## Why order 2 is a case of its own here

At K14 the order-2 leg finishes quickly and lives inside `K14Aut2-All` with the odd legs. At
K16 it does not: it is the one leg that costs an hour, so it is split out rather than made a
condition of running the 10-second `K16Aut3-All`.

At K18 and K20 the order-2 search is intractable outright. K18's `|Aut| = 2` classes come from
its three block-driven census cases instead; K20's have never been enumerated.

## The Compare step

The bat finishes by looking every class it wrote up in the catalog,
`AllResults/K16_P1F_aut_gt1.txt`, and reporting **Ok** or **Fault** with a histogram over the
size of the symmetry group. `--n 16` picks that catalog: there is one per N, each named for
what it holds. A healthy run reports

    Compare Ok: all 60 results of result.txt present in the full catalog

         |Aut|   results
             2        59
            14         1
         total        60

It checks present or not present, and nothing else. It cannot tell you a class the run should
have produced and did not. A Fault means the run wrote something the catalog does not have, or
wrote a known class under a different group order, and it fails the bat with exit code 3.

**What this check is worth.** The catalog's `|Aut| = 2` half was built from this case's own
first run, on 2026-09-01. So Compare is a reproducibility test of later runs against that one;
it is not independent evidence that the original 60 were right. What supports those is the
untargeted run that found exactly 60 and stopped, and the arithmetic that 59 + 30 = 89 matches
the published number of K16 classes with a non-trivial automorphism group.

Until 2026-09-01 the K16 catalog was `K16_P1F_aut_gt2.txt`, scope `|Aut| > 2`, and this case
could not be checked at all: 59 of its 60 classes were outside it.

**The other check is the census the run prints for itself.** The `= TOTAL` row must read
`Saved 60` with `|Aut|={2:59 14:1}`. Nothing less than 60 means the target was not reached;
anything other than that split means the leg found something it should not have.

## Expected time

**390.5 s at 10 threads -- 7 minutes wall, 205,153,668 nodes at 525/ms.** Measured 2026-09-01,
the run that produced the log above.

Against roughly an hour for the untargeted run, which is the point of the target.

## Changing the thread count

`run.bat` line `SET "RUNARGS=16 10"`. The first number is N and must stay 16. The second is
the thread count, shipped at 10.
