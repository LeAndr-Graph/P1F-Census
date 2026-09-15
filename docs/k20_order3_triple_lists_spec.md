# Spec (draft) -- precalculated triple lists for the K20 order-3 search

STATUS: DRAFT 2026-09-06 evening, numbers from block 0 (`REP_FANOUT`), design not yet approved,
nothing implemented. Sequel to `k20_order3_speedup_road.md` ("Measured and KEPT") and to the
retracted partition attempt (`k20_order3_partition_attempt_2026-09-06.md`), which this does NOT
reopen: that was a partition, this is a change to what one node costs.

## 1. The idea, and the two rules

Today every node of the cover regenerates its candidate rows from scratch: `genM` builds every
matching through the lowest uncovered edge, edge by edge, under the closure ban against all
committed factors, and the survivors are grouped into sigma-orbits ("triples"). The proposal is
to build the admissible triples ONCE below a block root and thereafter FILTER: each node inherits
its parent's list and keeps the entries that are edge-disjoint from, and Hamiltonian with, the
orbit the parent just committed. A node's candidates are then a list scan, not a search.

The two rules (Leonid, 2026-09-06):

1. **A triple is one object, represented by its base row.** The base row is the member chosen to
   stand for the orbit (the one through the lowest uncovered edge at generation time, or the
   lexicographically least member in a stored list); its two images under sigma are kept AFTER
   it, in sigma order, never as entries of their own. A triple is generated and listed once, and
   its edge bitmap is the union of its three rows (30 of the 190 edges).
2. **The sigma-fixed factor is a triple of size one.** The row through edge {18,19} is its own
   orbit; the list carries it as an ordinary entry with a 10-edge bitmap and no images, so
   "commit an orbit" is the only operation and the fixed row is not a special case.

## 2. The measured tree shape (block 0, prune on, DFS sample of 41.8M nodes)

Indexed by the number of committed factors (3 per orbit; 7, 10, 13 are where the fixed factor
has joined). "Expanded" = the generator produced at least one row.

| factors | nodes expanded | rows generated = distinct orbits | orbits per expanded node |
|---|---|---|---|
| 3 | 1 | 232,932 | 232,932 |
| 6 | 178 | 730,666 | 4,105 |
| 7 | 635 | 519,749 | 819 |
| 9 | 712,143 | 24,536,934 | 34.5 |
| 10 | 1,244,090 | 9,277,607 | 7.5 |
| 12 | 5,326,298 | 6,307,821 | 1.18 |
| 13 | 217,529 | 220,479 | 1.01 |
| 15 | 187,888 | 193,659 | 1.03 |
| 18 | 4 | 4 | 1.00 |

Two facts the table carries:

- **The lists shrink by ~60x per level**: 233k after one orbit, 4.1k after two, 34 after three,
  1.2 after four. Stable to two digits between the 18M-node and the 41.8M-node samples.
- **Most nodes are not in the table.** 41.8M nodes were counted, 7.7M expanded: ~80% of all
  nodes are dead ends where the generator ran and produced nothing.

**With dead ends counted** (second run, same block, first progress row, 19.0M nodes = the
run's own count):

| factors | nodes, ALL | orbits found | orbits per node |
|---|---|---|---|
| 3 | 1 | 232,932 | -- |
| 6 | 86 | 345,607 | 4,019 |
| 7 | 311 | 250,681 | 806 |
| 9 | 322,096 | 11,026,787 | 34.2 |
| 10 | 580,681 | 4,330,471 | 7.5 |
| 12 | 10,696,651 | 2,914,285 | 0.27 |
| 13 | 5,677,816 | 103,578 | 0.02 |
| 15 | 1,566,957 | 93,151 | 0.06 |
| 16 | 196,606 | 2 | 0.00 |
| 18 | 123 | 1 | -- |

**96% of all nodes are at depths 12-16**, where the inherited candidate list is at most 34
entries, and 18M of the 19M nodes are dead ends or single-child nodes there. Each pays a
full `genM` call today (~90 us average per node at 90 nodes/ms on 8 threads); with a filtered
list each would pay a scan of <= 34 bitmaps.
- Every generated row was its own orbit (rows = orbits at every depth): the anchor edge is in
  the base row only, so no orbit is generated twice. Rule 1 is already how the engine behaves.

## 3. The arithmetic

Per node, the filter costs one pass over the parent's list; per DFS descent the lists are
233k, 4.1k, 34, 1.2 (and the dead ends inherit the same short lists). Summing list length x
ALL nodes over the 19.0M-node stretch of the second run:

    depth 6:      86 x 233k  =  2.0e7
    depth 9:    322k x 4.1k  =  1.3e9
    depth 10:   581k x 34    =  2.0e7
    depth 12:  10.7M x 34    =  3.6e8   (the parent list at depth 9->12 is <= 34)
    depth 13+:  7.4M x ~2    =  1.5e7
    total                    ~  1.7e9 bitmap tests

plus a Hamiltonicity check (3 rows, ~60 instructions each) for the entries that survive the
disjointness AND -- a fraction of each list, and at depth >= 12 a handful per node. Against the
generator's cost for the same stretch: 19.0M nodes in 212 s on 8 threads = ~1,700
thread-seconds = ~5e12 instructions, 96% of it spent at depths 12-16 on nodes whose candidate
list has <= 34 entries. At 10 instructions per bitmap test and 200 per surviving check the
filtered form is ~1e11: **one to two orders of magnitude less work on paper.** That would move
the order-3 census from ~8 months on the 32-core box to weeks or days -- which is exactly why
it has to be measured, not believed. The dominant remaining cost would be the 4.1k-entry scans
at depth 9.

Why this contradicts the K18 a=4 result ("bitset/clique replacement 1.0-5.4x slower",
2026-08): that was order-2 PAIRS under an involution with a different fan-out profile, and the
record does not say whether the lists were filtered hierarchically or rebuilt from the root
list at every node. It does not transfer; it also does not excuse skipping the measurement.

## 4. Design sketch

- Per block root: enumerate the admissible orbits (the level-2 list, ~233k) once, each as
  {base row, 30-edge bitmap (3 x uint64), a 3-bit "rows present" for the size-1 case}. ~10 MB.
- Per DFS level: the child list = parent list filtered by (a) bitmap AND covered == 0 and (b)
  Hamiltonian with each of the 3 (or 1) rows just committed, via the same `is_perfect` as the
  leaf check. Lists are per thread, one per depth, reused (no allocation).
- The anchor rule stays: at a node the cover branches on the lowest uncovered edge, so the
  candidates are the list entries whose bitmap contains that edge; keeping the list sorted by
  base-row lowest edge makes that a range, not a scan.
- Symmetry breaking (setwiseStab, canonKey) unchanged: it acts on orbits, and the orbits are
  the same objects.
- Correctness oracle: K14 order 3 (17 classes / 10,975 nodes) and K16 order 3 (20 / 1,349,431)
  must reproduce exactly, node counts included, since the tree is unchanged.
- Speed: paired A/B on K20 block 0, idle machine, rows at equal elapsed, as for the loop change.

## 5. Prototype (2026-09-06 21:20, branch `proto/triple-lists`, commit 6f17837, k20 only)

Built as `REP_PRECALC=1` on the enumerable block-threading pool, trivial-stabilizer nodes only
(so `REP_PRUNELEVEL=1`). The branch also carries the cherry-picked level-2 slice and sampling
code (63691a7, d3054c4) as the TEST HARNESS only -- master stays without them.

- Seed list: for every uncovered edge, `genM` under the closure ban, orbit-key dedup, each
  orbit once as {sorted members (base row first), size 1 or 3, symmetric 20-word edge bitmap}.
- Node: candidates = list entries whose bitmap holds the anchor edge; each child inherits
  the parent's index list filtered by `tripCompat` (disjoint bitmaps, base row Hamiltonian
  with every row of the committed entry). Queue items carry {shared_ptr to the seed list,
  index vector}; the local drain recurses with the same routine.

**Oracle PASSED:** level-2 slice, 23 branches at stride 1M (the head branch excluded), 8
threads: generator and lists walk the identical tree -- 5,573,765 nodes, equal per branch --
and find the identical class set. Wall clock 113.0 s vs 61.6 s including the shared 45 s
level build; the 23 branches alone 68 s vs ~10 s after ~6 s of per-seed list building
(14k-17k orbits per level-2 seed, 1.3-2.2 s each, one thread per seed). **~7x on the search**
on the first, unoptimized cut (per-child index-vector copies, full 20-word ANDs, `chosen`
copied per child, tally on).

**First cut FAILED on block 0** (materialized every child's list at the seed: 232,932 filters
of the 1.19M-entry list on one thread, 5.2 GB and climbing, no progress row in 12 min;
killed). Second cut (93f44c3): a queue item carries the parent's list and the committed
entry, the node filters lazily on expansion; bitmaps packed as 3 x uint64 over edge ids.
Slice oracle unchanged (identical 5,573,765 nodes, same class; branches done 12 s after the
pool starts).

**Block-0 A/B, 2026-09-06 21:47, idle machine, 8 threads, prune on:** seed list 1,185,472
orbits built in 51 s; then

| elapsed | control (generator) | lists | ratio |
|---|---|---|---|
| ~226 s | 17,770,496 nodes, 3 classes | 124,437,504 nodes, 28 classes | 7.0x cumulative |
| ~526 s | 40,014,848 nodes, 9 classes | 331,547,648 nodes, 68 classes | 8.3x cumulative |
| rate between the rows | 74 nodes/ms | 690 nodes/ms | **9.3x** |

Block 1 took 84 min to reach 338M nodes; the lists reach 331M in under 9 min. On the
5.9e10-node block estimate: ~24 h per block on the laptop (was ~10 days), ~100 laptop-days
or ~25 box-days for the 104 blocks, before the heads and before any tuning.

The run was allowed to continue and stopped at 34 min (the laptop could not run overnight):
545M nodes / 103 classes at 14 min, 978M / 186 at 24 min, **1,417,225,216 nodes / 284
classes at 34 min, 699 nodes/ms and still rising**. 284 classes of order 3 from one third
of a day of one block, against 598 in the whole order-3 bank from the summer's harvests.
The per-depth tally over the 1.4e9 nodes has the same ratios as the 19M-node sample
(4,104 / 852 / 35.7 / 7.9 / 0.26 / 0.02 / 0.05), so the block does not change character
with depth. Files: `scratchpad/ab/trip_b0_35min_killed.{log,txt}` (session f3534e74).

## 6. Next

1. A full block to completion on a day the laptop (or the box) can be left alone: the first
   exact block size and per-block class count.
2. The head branches (level-2 indices 0..~10,000 of a block), unfinishable with the
   generator: with the lists they should finish in minutes, which closes the "plus the
   heads" gap in the cell estimate.
3. Mirror to k14 / k16 / k18 and run the K14 and K16 order-3 oracles (identical node counts
   and class sets), then the merge and a fresh cell estimate by the stride-1M sample.
   -- DONE 2026-09-07, section 7.
4. Tuning, in the order the profile says: avoid the per-child `chosen` copy and the per-node
   index-vector allocation; the depth-6 filter of the 1.19M list (5,713 x 1.19M so far).
   -- SUPERSEDED by section 8. This order was a GUESS, written before anything was profiled,
   and it was wrong: the per-child `chosen` copy is 1.9% of the run. Do not work this list.

## 7. The mirror and the oracles (2026-09-07) -- BOTH PASS

The k20 block-0 run of section 5 was stopped at 224 min to free the machine for this step.
Its log and result are kept as the baseline in
`runs/K20Aut3/baseline_2026-09-07_triples_224min/` (md5-verified copies):
**8,800,802,816 nodes / 1,862 classes {3:1862}, 0 duplicates, 655 nodes/ms.** For scale, the
whole summer order-3 harvest was 598 classes; this is a third of one block of 104.

**The mirror.** The TRIPLES block -- the `#include`s, `TripEnt`/`TripList`/`IdxList`/`TripTask`
/`tripCompat`, `buildTripList`/`splitNodeT`/`coverT`, the env read, and the `qTrip` deque in
lockstep with `queue` at all three sites (both seedings and the worker loop) -- is now
character-identical in k14, k16, k18 and k20, per the sibling rule. Two forced deviations,
both local and commented at the point of deviation:

- **k14** has no `nV4`, so its `buildTripList` guard is `gElems.size() > 2` alone.
- **k16** has no `REP_PRUNELEVEL` -- it was left without it deliberately (see the note where
  k20a2rep.cpp reads the variable), and that decision was NOT reversed here.

`test_env_reset.bat` did not clear `REP_PRECALC`, `REP_FANOUT` or `REP_SAMPLE`; now it does.
Before this, a leftover `REP_PRECALC` in a shell would have silently changed any case in any
of the four engines -- and the regression compare is byte-exact, so it would have looked like
a code fault.

**K14 order 3 -- the identical-tree oracle.** Same binary, one thread, run twice with
`REP_PRECALC` unset and then set; everything else identical:

    REP_ORDER=3  REP_LEVEL=1  REP_RANGE=0:8  REP_PRUNELEVEL=1   p1f.exe 14 1

(`REP_RANGE=0:8` is the whole level-1 space -- 8 root reps -- so the run goes through the
block-threading pool, the only path REP_PRECALC touches. `REP_PRUNELEVEL=1` is what makes the
two sides comparable: the prototype declares every node trivial, so only with the prune set
does the generator skip the same per-node dedup and walk the same tree. One thread keeps both
node counts and both result files deterministic.) The driver bat lives in `scratch/`, which is
gitignored -- the four lines above are the whole of it.

| | nodes | classes | result file |
|---|---|---|---|
| control (generator) | 11,532 | 17 {3:5 6:5 12:5 84:1 156:1} | -- |
| lists | 11,532 | 17 {3:5 6:5 12:5 84:1 156:1} | byte-identical to the control |

17 is the catalogue count of K14 classes with |Aut| divisible by 3, so this is a whole-column
oracle and not merely a self-comparison.

**K16 order 3 -- stronger than expected.** Same shape, 4 threads, no prune (k16 has none):

    REP_ORDER=3  REP_LEVEL=1  REP_RANGE=0:1000000                p1f.exe 16 4

(the range end is deliberately past the root-rep count; the engine clamps it). The two sides
were expected to walk DIFFERENT trees, with only the class set required to match. They did
not: the node counts are identical as well.

| | nodes | rate | classes |
|---|---|---|---|
| control (generator) | 1,349,431 | 233/ms | 20 {3:19 15:1} |
| lists | 1,349,431 | 726/ms | 20 {3:19 15:1} |

Result files identical after sorting. The trees agree because the block-root stabilizer on
these types is already trivial, so the generator's `setwiseStab` returns {} anyway and the
missing prune costs nothing -- which is also why 1,349,431 is exactly the figure section 4
quotes for this oracle. The **3.1x** is a free second data point on the speedup, on a
different N and a different tree from the k20 9.3x.

**k18** is mirrored and compiles, but has NO order-3 oracle run yet: the natural case,
`runs/K18Aut3-All` (531 classes), is far heavier than K14/K16. Open.

**Nothing else moved.** The full `regression/run_all.bat` with `REP_PRECALC` unset:
**8 passed, 0 failed** -- K14-t6-a0 (44,047 nodes), K14-t7-a0 (13,656), K14Aut3-All,
K16Aut3-All, K18-t9-a4-block1777, K18-t9-a4-owner, K20Aut4-All (230 classes), owner-guards.
That is the check that the ported code is inert when the flag is off.
Log: `scratch/regress_all_2026-09-07.log`.

## 8. The profile, and the tuning it dictated (2026-09-07) -- 1.55x, k20 only

Section 6.4 named three things to tune. It had measured none of them, and it was wrong.

**The profile.** VTune hotspots on the shipped case -- block 0, `REP_PRECALC=1`,
`REP_PRUNELEVEL=1`, 8 threads, 632 s wall / 4,407 s CPU (~87% utilisation), result in
`scratch/prof_k20_triples_r001`, harness `scratch/prof_k20_triples.bat`:

| function | CPU s | % | |
|---|---|---|---|
| `tripCompat` | 1971.4 | 44.7% | the parent-list filter |
| `is_perfect` + its `array::operator[]` | 401.3 | 9.1% | Hamiltonicity, called from `tripCompat` |
| `malloc_base` | 434.2 | 9.9% | per-node allocation |
| `splitNodeT` self | 401.3 | 9.1% | |
| `unique_lock` ctor+dtor | 265.0 | 6.0% | the shared queue mutex |
| `commitOrbit` + `rollbackTo` | 186.5 | 4.2% | |
| `shared_ptr` refcounts | 160.4 | 3.6% | `IdxList` + `TripList` |
| `this_thread::yield` | 90.3 | 2.0% | 87 s of it spin -- workers starving |
| the per-child `chosen` copy | 85.6 | 1.9% | **section 6.4's first item** |
| `genM` | 8.7 | 0.2% | the old bottleneck |

`genM` at 0.2% is the whole point of section 5: the lists did their job, and the compatibility
test -- 53.8% with `is_perfect` -- is now the program. The first ~150 s of CPU is setup and the
single-threaded seed-list build (that is the 32 s of `_Hash::find` doing `orbitKey` dedup), so
~96% of the profile is genuinely the search.

**What was changed, in `k20a2rep.cpp` ONLY.** The sibling rule is deliberately suspended for
this pass, on the user's instruction: k14/k16/k18 keep the plain prototype from section 7 until
a winner is mirrored on purpose.

1. The edge bitmaps moved out of `TripEnt` into a parallel `std::vector<TripBits>` of
   32-byte-aligned records. The filter loop reads nothing but bitmaps and was dragging ~85 bytes
   of struct through cache to reach 24 bytes of them. `bitsDisjoint` is now one aligned AVX2
   load each, `vpand`, `vptest`.
2. `splitNodeT`'s two passes over the list -- filter, then re-scan for the anchor bit -- fused
   into one.
3. The local recursion (`coverTLocal`) allocates nothing: per-thread index buffers indexed by
   recursion depth, reused, instead of a `make_shared<IdxList>` per node. This is where nearly
   every node is, since the pool expands one level per queue item and drains subtrees locally
   once the queue is at QCAP.
4. A child is committed straight from `ce.rows` via a new `commitRows(const Match*, int)`. The
   old path built a whole child cover (`chosen` + the rows) and then sliced the 1-3 rows back
   out of it -- two copies of the partial cover to recover rows it already had.

**Correctness.** Ten COMPLETE level-2 branches, run with each binary in turn:

    REP_ORDER=3  REP_LEVEL=2  REP_RANGE=11000000:11000010
    REP_PRUNELEVEL=1  REP_PRECALC=1                          p1f.exe 20 8

They are taken from the middle of the
level (indices 11,000,000+, away from the block heads), so the node count is exact and
deterministic rather than a snapshot -- **2,438,745 nodes on both sides.** The tree did not move
by one node. The block-0 A/B below independently rebuilds the same 1,185,472-orbit seed list.

**Two further passes, after re-profiling.** A 60 s VTune attach to the RUNNING case (no
restart: `-target-pid`, which profiles steady state with no setup or seed-list build in it)
showed malloc, the shared_ptr refcounts and the queue mutex all gone from the profile
entirely, the filter grown to ~66% of the run, and two things still worth taking:

5. **Hoist the committed entry.** `tripCompat(L, j, c)` re-derived `L.bits[c]`, `L.ent[c]` and
   both `vector::operator[]` on EVERY entry of the parent list, though `c` is fixed for the
   whole pass. It now arrives resolved, with the planes as raw pointers. Measured effect:
   `vector<TripEnt>::operator[]` went from 3.4% to below the reporting floor.
6. **Make the scan contiguous (`NodeList`).** The filter walked an index list and loaded
   `bits[j]` for scattered `j` -- one dependent load per entry into a 38 MB plane, which is
   what `bitsDisjoint`'s 28.1% was really waiting on. Each node now copies its survivors'
   32-byte records into its own buffer, so every deeper scan is a LINEAR read of a small block
   (<= 34 entries, ~1 KB, below depth 12; ~4.1k at depth 9, where most bitmap tests happen).

**Speed.** Paired, back to back, idle laptop, 8 threads, block 0, rows at equal elapsed (the
project rule: never final totals, since neither side finishes). Changes 1-4 against HEAD:

| elapsed | control (HEAD) | changes 1-4 |
|---|---|---|
| ~202 s / ~217 s | 75,502,592 nodes, 18 classes | 140,917,760 nodes, 31 classes |
| ~502 s / ~517 s | 235,401,216 nodes, 49 classes | 388,332,544 nodes, 81 classes |
| rate between the rows | 533 nodes/ms | **825 nodes/ms** |

**1.55x**, against this laptop's ~18% drift; the cumulative rates give 1.60x, so the two agree.
Seed-list build unchanged (57.3 s vs 56.0 s). Then, same measurement on the same block after
changes 5 and 6 -- 319,368,192 nodes at 216.1 s and 903,455,744 at 516.1 s, so **1,947
nodes/ms** between the rows.

| build | nodes/ms, steady state, block 0 |
|---|---|
| HEAD (the section 5 prototype) | 533 |
| changes 1-4 | 825 |
| + 5 and 6 | **1,947** |

**Read the last row with the right caution.** 533 -> 825 is a true paired A/B, two binaries back
to back on an idle machine. The 1,947 is a SINGLE run measured the same way (between progress
rows) but not paired against a control on the day, so drift is not cancelled -- it is strong
evidence, not the same grade of number. On its face that is 3.65x over the prototype and ~34x
over the original generator; treat "about 2x on top of the paired 1.55x" as the defensible claim
until someone runs 5+6 against 1-4 back to back.

**Not done, and next if this is pushed further:** `is_perfect` (still ~10% and now the largest
non-filter item) and the queue mutex, which vanishes from the steady-state profile because every
node below QCAP runs through `coverTLocal` and never takes the lock -- its cost is confined to
the early phase at depths 6-9. Both were left alone deliberately: changes 1-6 are representation
and allocation only and cannot move the tree, which is why one node-count oracle covers them
all, whereas touching the queue changes how work is shared and would need its own argument.
