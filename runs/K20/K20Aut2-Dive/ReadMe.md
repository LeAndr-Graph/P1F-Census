# K20Aut2-Dive -- one goal, one class

Find a **single** class of K20 with `|Aut| = 2` exactly. That settles open problem (ii) of the
companion paper: does K20 have a perfect one-factorization whose automorphism group is exactly
order 2. Nothing here is a census, and nothing here is reproducible.

## Why dives

    SET "REP_DIVE=1000000"

`REP_DIVE=B` replaces the exhaustive tree walk with randomized backtracking DFS restarts across
all threads. Each restart starts at a uniform-random root, explores in random candidate order,
backtracks on dead ends, gives up after `B` nodes, and emits every complete cover it reaches.
The global `canonKey` still deduplicates.

Three consequences, and all three are the reason to use it here:

**It is much faster per node.** Dives use **no `setwiseStab`**. On `2^9.1^2` the centralizer has
order about `3.7e8`, and that call dominates the per-node cost of the ordinary over-cap path.
Removing it is the speedup.

**It is immune to the open dedup question.** No `setwiseStab` means no `schreierDedup`, so the
orbit quotient whose completeness has never been verified simply never runs. A class found here
stands regardless of how that question resolves -- which is not true of any other K20 order-2
result in this repo.

**It escapes barren regions.** Both earlier attempts stalled in one:

    K20Aut2         plain sweep     878,635,008 nodes / 377 min / 0 classes
    K20Aut2-Shard   level-2 shards  146,202,624 nodes / 164 min / 0 classes

A fresh random root per restart is exactly the thing neither of them could do.

## The cost: random, so not reproducible

A re-run explores elsewhere and need not find the same class, or any class. The RNG is seeded per
run and per thread from `std::random_device`.

That is acceptable **here and nowhere else in this repo.** For an existence question one class is
a permanent answer: the object either exists or it does not, and a witness settles it. The
reproducibility a census needs is not needed to prove a thing exists. Every other case in
`runs\` is systematic for exactly that reason.

## The tool is validated -- at K16, against a known answer

Forcing K16's `2⁷1²` over-cap with `REP_SYMCAP=1000000` puts it on this same dive path. It
recovered the complete published census:

    [K16-REP]  FAST SEARCH: 134 random restarts, 60 new distinct classes
      order 2  | 2⁷ 1²  | 158.2s | 129,517,525 (819/ms) | 60 |Aut|={2:59 14:1}

About two restarts per class, and it went **straight for `|Aut| = 2`** -- 59 of the 60 -- rather
than the symmetric ones. So a null result at K20 is not the tool failing.

The same run also cleared the other doubt: the over-cap `setwiseStab` + `schreierDedup` dedup,
run separately on K16 at the same forced cap, returned the same 60. It drops nothing.

## B is calibrated, and small is right

Measured at K16, target 5 classes, dive path forced with `REP_SYMCAP=1000000`:

| `B` | restarts (nodes/B) | time to 5 classes |
|---:|---:|---|
| 1e6 | ~134 → all 60 | 158s |
| 1e5 | ~47 | 4.3s |
| **1e4** | **~285** | **3.2s** ← fastest |
| 1e3 | ~1,206 | 6.8s |
| 1e2 | ~18,000 | 85.7s |

**Dives complete across five orders of magnitude of `B`, down to 100 nodes per restart.** Budget
is not what stops a descent. The variable that matters is **restarts per unit time**, and `1e4`
is the optimum.

### This corrects two earlier K20 runs

    B = 1e6    ~120 restarts / 50 min    0 classes
    B = 1e7      ~2 restarts /  5 min    0 classes    <- worse

Raising `B` spends draws to buy depth that was never the constraint. At `B = 1e4`, one hour is
roughly **12,000 restarts**. The dive has never had a real number of draws at K20.

## What to watch, in order

**1. Any class at all, of any `|Aut|`.** 178 of the 230 catalogued classes contain an involution,
so if descents complete at K20 at all, thousands of draws should emit *something*.

**2. Then the `|Aut|` histogram.** A `|Aut| = 2` row settles open problem (ii).

**3. Silence across ~10k restarts is itself a result.** It would say something about K20's
completion *rate* -- the one thing the K16 calibration cannot predict, since K20's tree is 2
orbit-blocks deeper with far larger candidate sets.

**Do not wait for a closing line.** `FAST SEARCH: N random restarts, ...` prints only when the leg
*ends* -- target reached, or the engine stops it -- not when you kill the run. While it runs the
only signals are the `~` row's `saved(duplicates)` count and its node count; **nodes / B**
estimates the restarts done so far.

`RUNLABEL` names the log and result file, so runs at different `B` do not collide.

## What to watch

The `|Aut|` histogram, and nothing else. The count of classes is not the result.

- **Any `|Aut| = 2` row settles open problem (ii).** Stop and keep the file.
- Rows at 6, 18 or 342 are re-finds of already catalogued classes. They mean the hunt is working
  but has not reached the target stratum -- 178 of the 230 catalogued classes contain an
  involution, so they are what a working dive finds first.
- Nothing at all, for a long time, is what the two systematic attempts already produced. It is
  weak evidence of anything: `2^9.1^2` is non-empty by construction (GK20 carries such an
  involution), so absence here means the search has not arrived, not that the stratum is empty.

## Requirements

The dive path needs `shardLevel == 0`, so this case sets **no** `REP_LEVEL` and **no**
`REP_RANGE`. Do not add them -- use `K20Aut2-Shard` for the systematic form.

## No Compare step

`AllResults` holds only `K20_P1F_aut_gt3.txt`, scope `|Aut| > 3`, so every `|Aut| = 2` class is
outside it and `compare_to_catalog.pl` would report a Fault on a correct run. Same as the other
two K20 order-2 cases.

## Back up result before re-running

The engine refuses to write `RESULT` if the file exists. **Move it, do not delete it.** A random
hunt may not produce the same class again, and each class is written the moment it is found.

## Changing the thread count

`run.bat` line `SET "RUNARGS=20 10"`. First number is N and must stay 20; second is the thread
count, **shipped at 8**.

It was 10 until 2026-09-03, when a 10-thread dive drove the machine to a near-crash (789 MB, CPU
pegged) and had to be killed at 5.5 minutes. This case runs for hours; leave the headroom. The
restart-per-hour estimates above were computed from 10-thread rates and will be lower here --
roughly 10,000/hour rather than 12,000.
