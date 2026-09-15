# Spec -- deeper partition levels on the enumerable path (K20 order 3, 3^6 1^2)

STATUS: approved in discussion 2026-09-06, implementation to follow. Sequel to
`k20_order3_speedup_road.md` and `runs/K20Aut3/ReadMe.md`.

## 1. The gap

On a type whose centralizer is small enough to enumerate -- K20 `3^6 1^2`, |C(sigma)| =
1,049,760 -- the engine partitions the search tree at level 1 only: the 104 C(sigma)-orbit
representatives of the first factor-orbit (three factors, the orbit of the factor through
edge {0,1}). `REP_LEVEL > 1` is refused with a log line, because the level-L frontier is
built by `splitNode`, which needs the root BSGS, and the enumerable setup builds one only
when `REP_RANGE` is set (for block threading).

So today one block is ten days of work and there is no finer unit. The K18 census ran on
depth-3 blocks; the K20 analogue -- three orbits, nine factors, fixed in advance -- does not
exist. This spec adds it. It is a partition change, not a pruning change: total work is
unchanged, granularity, resumability and a canonical block list are what it buys.

## 2. The change

In every engine's enumerable path (k14, k16, k18, k20 -- identical source, the sibling rule):

1. **Setup builds the root BSGS when `REP_LEVEL > 1`**, not only when `REP_RANGE` is set. Same
   calls, same place (the BLOCK THREADING block after the C-orbit reps).
2. **The enumerable branch gains the two over-cap shard modes for `REP_LEVEL > 1`**, with the
   root reps as level 1:
   - **COUNT** (`REP_RANGE` unset): level-synchronous expansion by `splitNode`, printing
     `LEVEL k branches n` for k = 2..L; the final level is counted, not stored. Nothing
     searched.
   - **RANGE** (`REP_RANGE=start:end`): a lazy in-order DFS to depth L that keeps only the
     branches with index in [start, end), then seeds the shared work queue with them (the
     existing block-threading pool). The chunked form stays ignored here.
   The code is the over-cap code with the seeds taken from `reps` instead of the generator
   path's tasks. `REP_RAWSHARD` keeps its meaning (trivial descent, superset frontier).
3. `REP_LEVEL=1` is unchanged: root reps, no BSGS.

Determinism: level-L indices are DFS order over (rep index, child order), the same contract
as the over-cap path, so slices compose exactly and a block index is stable across runs of
the same binary. Note that with `REP_PRUNELEVEL=d` set, `splitNode` past factor depth d
skips the stabilizer dedup, so the frontier is larger (a superset) -- the count must be
quoted with the prune setting that produced it.

## 3. Checks, before any K20 number is quoted

- **K14 order 3, the whole-column oracle** (`REP_ORDER=3`, catalogue |Aut| divisible by 3:
  17 classes). Full run gives 17 classes; `REP_LEVEL=2` and `3` counts print consistent
  levels (level 1 = 8 reps as measured 2026-09-05); a range sweep at level 2 (and one at
  level 3) split into two or three slices unions to exactly the 17 classes with the same
  |Aut| histogram, and running a slice twice gives the same result file.
- **A K20 order-3 block-0 spot check is NOT required** for this change (block 0 is a
  level-1 range and that path is untouched).

## 4. The count run (the deliverable of this step)

`runs/K20Aut3` with `REP_LEVEL=3`, `REP_RANGE` unset, `REP_PRUNELEVEL` unset first, then
set: ~97 s setup plus the level-2 and level-3 expansions. Output: `LEVEL 2 branches`,
`LEVEL 3 branches`, and the time each took. That number decides whether the level-3 list is
a few thousand entries (blocks of hours) or a million (blocks of minutes), and it is the
input for the next steps -- owner dedup ported from k18 for a canonical block list, and the
fixed-factor pin, which are separate specs.

## 5. Not in scope

The sigma-fixed factor pin (a pruning change), owner dedup / the a.b.c coordinate for K20,
and any change to the over-cap path.
