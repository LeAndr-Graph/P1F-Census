# K20Aut2 -- what this case computes

A **sample** of one-factorizations of K20 whose symmetry group contains an involution.
`run.bat` stops at 100 distinct classes. It is not a census and does not claim to be.

## Why a sample and not a census

The complete order-2 search does not finish at K20. `K20Aut4-All` states this in its own
header: *"the order-2 search does not finish at K20 any more than at K16 or K18."* At K16
the same leg costs an hour and completes; at K20 it does not complete at all, so this case
never attempts it.

The K18 `|Aut| = 2` classes come from block-driven census cases instead. K20 has no such
route yet, which is why 100 harvested examples is the whole of what this case offers.

## What the 100 counts

    SET "REP_ORDER=2:100"

The `:100` is a harvest target: the run stops the moment the 100th **distinct class
containing an involution** is found. Not the 100th class with `|Aut| = 2`. This is the same
semantics that made `K16Aut2`'s 60 come out as `{2:59, 14:1}` -- the `|Aut| = 14` class is in
there because 14 is even.

Of the 230 catalogued classes with `|Aut| > 3`, **178 have even group order** and therefore hold
an involution:

    |Aut|      6      168 classes
    |Aut|     18        9 classes
    |Aut|    342        1 class
    total             178

Nothing in the engine excludes them. There is no `|Aut|` reject on this path: the only such
predicate is `g_aut2Only`, enabled by `REP_F3COMPLETE`, and it exists solely in the K14 and K18
engines -- the K20 engine has none and this bat sets no such variable.

**But the order-3 evidence says they will not crowd the quota.** `K20Aut3`'s July harvest of
about 600 classes contained exactly one with `|Aut| = 6`: the `|Aut| = 3` stratum so far
outweighs the catalogued classes that the search essentially never lands on one. If the
`|Aut| = 2` stratum is comparably large, `2:100` will likewise return 100 classes at exactly
order 2.

**Which makes the histogram the result, either way.** A mix dominated by `|Aut| = 2` says the
stratum is large and settles the open problem. A mix dominated by 6, 18 and 342 -- the known
classes, because there was nothing else to find -- is real evidence that the `|Aut| = 2` stratum
is thin or empty. Read the `|Aut|` histogram on the `= TOTAL` row, not the count of 100.

## The target is reachable

Those 178 classes exist, so the run cannot hang waiting for a hundredth class that is not
there. It will reach 100 and stop.

## What would make this case interesting

An `|Aut| = 2` row in the histogram. Whether K20 has a perfect one-factorization whose group
is **exactly** order 2 is open -- it is problem (ii) of the companion paper, and no run has
answered it. The first harvested class reporting `|Aut| = 2` settles it. Everything after
that is a question of how big the stratum is, which a capped harvest cannot answer.

## One cycle type

`2^9.1^2` is the only non-empty involution type at K20:

- an involution of a perfect one-factorization fixes at most 2 vertices, which retires
  `2^1` through `2^8`;
- the fixed-point-free `2^10` is empty by the order-2 parity theorem.

The type is non-empty -- GK20, the `|Aut| = 342` class, carries such an involution -- so the
leg has something to find.

## No Compare step

Every other case bat ends by looking its results up in an archived catalog and reporting
**Ok** or **Fault**. This one does not, deliberately.

`AllResults` holds only `K20_P1F_aut_gt3.txt`, scope `|Aut| > 3`. Every harvested `|Aut| = 2`
class is outside it, so `compare_to_catalog.pl` would report

    Compare Fault: can't find N of N results of result.txt in ...

on a **correct** run, and fail the bat with exit code 3. There is no catalog for this stratum, so
there is nothing to look up in.

The check that does apply is the one the run prints for itself -- the `= TOTAL` row must read
`Saved 100`, and its `|Aut|` histogram is the actual result of the case.

## Is a re-run repeatable

**No RNG is involved.** The engine has a randomized mode -- `REP_DIVE=<budget>` gives random
restarts from fresh roots with a shuffled candidate order, seeded per run and per thread from
`std::random_device` -- but it is opt-in and this bat does not set it. The traversal here is
systematic.

That said, this case is **not** as repeatable as `K20Aut3`. Every order-2 type takes the
**OVER-CAP** path (`k20a2rep.cpp:32`, which names "every order-2 type" explicitly): `|C(alpha)|`
is about `3.7e8`, far past the cap, so the work is handed out through a shared work-queue rather
than the atomic task index the enumerable types use. Pop order depends on thread timing, so a
re-run sweeps the same space but need not reach a given subtree at the same point.

Consequence: a class found here **will** be found again by a comparable sweep -- the search is
systematic, not a lottery -- but not necessarily at the same node count, and not necessarily
before you give up.

## Back up result.txt before re-running

The engine refuses to write `RESULT` if the file already exists, so a re-run needs the old one
out of the way. **Move it, do not delete it.**

An `|Aut| = 2` record would be the answer to open problem (ii), and at the measured density this
case may not produce another for hours or at all. `result.txt` is the only copy: each class is
written the moment it is found, and nothing else in the tree retains it.

## Expected time

**Not measured.** No timed run of this leg at K20 exists. Treat it as open-ended and watch the
300 s progress rows; the order-2 legs at K16 and K18 are the expensive ones, and `2^9.1^2` has
two fixed points, which at K18 is the shape where every pruning lever was rejected and
`PRUNELEVEL=1` is the ceiling. Expect worse nodes-per-class than the order-3 leg.

## Changing the thread count

`run.bat` line `SET "RUNARGS=20 10"`. The first number is N and must stay 20. The second is the
thread count, **shipped at 8**.

It was 10 until 2026-09-03, when a 10-thread K20 order-2 run drove the machine to a near-crash.
These cases run for hours; leave the headroom. Note that the timings recorded above were measured
at 10 threads and will be slower here.
