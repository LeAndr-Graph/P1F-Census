# K20Aut2-Shard -- the order-2 leg as shards

Same cell as `K20Aut2` (`2^9.1^2`, the only live order-2 type at K20), run as slices of a
partition instead of one sweep. It exists because the single sweep produced nothing.

## The measurement that motivated it

`runs/K20Aut2`, 2026-09-03, `REP_ORDER=2:100`, 10 threads:

    287 min, 683,098,112 nodes, 0 classes, 0 duplicates
    ~37 min of that is setup before the first node is traversed

For contrast `K20Aut3` on `3^6.1^2` wrote 14 classes in 104.5M nodes -- about 7.5M nodes each.
At that density 683M nodes would have given ~91 classes. Zero is consistent with the walk
sitting in a barren region, and a single sweep has no way out: there is no checkpoint, and no
way to say "skip ahead".

## The partition

`REP_LEVEL=L` enumerates the search tree level by level, `L` counted in orbit-blocks committed.
The engine's own note is the specification:

> Level-L nodes are a complete, disjoint, deterministic partition -> split ranges across
> machines; the count guides the shard granularity. No disk / no checkpoint (position == the
> range you run). -- `k20a2rep.cpp:1513-1516`

Two consequences worth being explicit about:

- **Slices compose exactly.** `0:N` then `N:2N` covers the space once, no rework and no gap.
- **Resuming is running the next slice.** There is no state to reload, so a killed shard costs
  only that shard.

Both knobs are live for K20. `REP_RANGE` is refused by k18 only.

## Phase 1 -- count, and pick L

`REP_LEVEL=L` with **no** `REP_RANGE` prints the node count at each level `1..L` and stops. No
search.

**This has been run** -- 2026-09-03, shard label `count`:

    SETUP centralizer        0.0s  n=0        <- over-cap confirmed
    SETUP collectTasks       1.1s  n=5937
    SETUP orbit-collapse     0.0s  n=5937     <- no collapse; all already distinct
    SETUP BSGS+edgeStab      0.0s  n=4        <- 4 generators
    SETUP schreierDedup      0.0s  n=5        <- 5 root reps
    LEVEL 1  branches    5    (0.0s)
    LEVEL 2  branches 7944    (2005.9s = 33.4 min, parallel)

Level 3 was killed unfinished after 17 minutes with nothing printed.

### What phase 1 settled

**Setup is 1.1 seconds, so there is nothing there worth caching.** The parent case's "37 minutes
of silence" was never setup -- it was the tree walk grinding through the first two orbit-block
levels at roughly half a node per second. Its first `~` row read 1,024 nodes at 37 min, then
1.96M in the next five, because deeper subtrees are far cheaper per node.

**Level 2 is the shard level, and deeper would be worse.** 7944 branches is ample granularity,
and every invocation must rebuild the frontier up to `L` before processing any slice -- 33 min at
`L=2`, and 33 min *plus* level 3 at `L=3`. Shallow keeps the fixed cost per run down. The usual
reason to go deeper, uneven subtrees trapping one shard, is already handled: the chunk timeout is
checked mid-block.

**The 33-minute level-2 frontier is the one artifact worth caching.** It is 7944 partial covers,
fully deterministic, and every separate invocation pays for it again. That is the "resumable
frontier checkpoint" idea with a measured price on it.

## Phase 2 -- sweep a slice

**As shipped**, from the phase-1 counts:

    SET "SHARD=L2_s01"
    SET "REP_LEVEL=2"
    SET "REP_RANGE=0:7944:200(900,1)"

40 chunks of 200 branches across the whole level-2 frontier, each abandoned after 15 minutes or
1 new class. All 40 run inside **one** process, so the 33-minute frontier rebuild is paid once
rather than 40 times -- which is the whole reason to prefer chunks over separate invocations.

The general form. Two variants:

    start:end                        process that slice, plainly
    start:end:step(timeout,results)  chunked density sweep

The chunked form walks `[start,end)` in `step`-block chunks and advances to the next chunk the
instant **either** cap is hit -- `timeout` seconds or `results` new classes -- checked
**mid-block**, so a barren block is abandoned rather than finished. Either cap may be 0 to
disable it. Per chunk it prints

    +classes ...  TIMED-OUT | RESULT-CAP | range-done

**This form is the point of the case.** For an existence hunt it beats one long sweep: instead of
six hours in one region you sample many, and the per-chunk line tells you where the density is
non-zero -- which is information the 287-minute run could not produce at all.

## What this does not buy

Not speed. `|C(alpha)|` is about `3.7e8` for `2^9.1^2`, and every order-2 type is OVER-CAP by
construction (`k20a2rep.cpp:32` names "every order-2 type"), so the per-node dedup cost is
exactly what it was. Sharding buys **coverage** and **the ability to stop and resume**. If the
cell is simply too sparse, sharding will show that faster; it will not fix it.

Two things do not help here, both measured: the pattern filter cannot reduce this leg, and
pinning `D` was already a dead end.

## Per-shard files

The log and result file are named for `SHARD`, so successive shards never collide and no shard's
results are destroyed to make room for the next. That matters more here than usual: a class is
written the moment it is found, and its result file is the only copy.

The existing-log guard is per shard too -- re-running the same label asks you to pick another.

## No Compare step

Same reason as the parent case. `AllResults` holds only `K20_P1F_aut_gt3.txt`, scope `|Aut| > 3`,
so every harvested `|Aut| = 2` class is outside it and `compare_to_catalog.pl` would report a
Fault on a correct run. There is no catalog for this stratum to look up in.

The check that applies is the run's own `= TOTAL` row and its `|Aut|` histogram.

## Is a re-run repeatable

Yes, within a shard. No RNG is involved -- the randomized mode (`REP_DIVE=<budget>`, random
restarts from fresh roots with shuffled candidate order, seeded per run and per thread from
`std::random_device`) is opt-in and this bat does not set it. The level-L partition is
deterministic, so a given `REP_RANGE` slice always covers the same nodes.

Within a slice the work still goes through the over-cap shared work-queue, whose pop order
depends on thread timing, so the order of discovery inside a shard can vary.

## Expected time

**Not measured.** Phase 1 has never been run at K20 order 2, so even the level counts are
unknown. The only datum for this cell is the parent case's 683M nodes at ~45/ms instantaneous
for no classes.

## Changing the thread count

`run.bat` line `SET "RUNARGS=20 10"`. First number is N and must stay 20; second is the thread
count, **shipped at 8**.

It was 10 until 2026-09-03, when a 10-thread K20 order-2 run drove the machine to a near-crash.
These cases run for hours; leave the headroom. Note that the timings recorded above -- including
the 33-minute level-2 frontier rebuild -- were measured at 10 threads and will be slower here.
