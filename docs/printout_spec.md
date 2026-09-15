# Console printout spec (KnA2, k18 rep engine)

STATUS: IMPLEMENTED. Sections 1-8 shipped 2026-08-27, sections 9-12 on 2026-08-28;
where an older section contradicted the code it has been rewritten in place and dated.
Scope: printout only. Applies to KnA2; Tt4 is not touched.

PARTLY SUPERSEDED 2026-08-30 by `docs/log_table_spec.md`, which turns the run log into a
TABLE. It replaces sections 3, 4, 5, 10 and 11 (the per-block line, the DONE lines, the
periodic `[rep]` line, the per-type line, and `processed=`), and drops the `[SUBSET]` line
entirely -- its two facts became the Symmetry and Type columns for the |Aut| > 2 cases and
the table title for the block-driven ones, and section 1 still governs their wording.
Sections 2, 6, 8, 9 and 12 stand as written.

Section 7's closing line lost one word on 2026-08-30. It now reads
`P1F: 0 result(s) written to result.txt (Total time=345min)`: "done," was dropped as
redundant, since the line states the outcome and `End of job` below it is the sentinel that
says the run was not interrupted. The samples further down still show the old wording.

Goal: a reader who knows the general problem, but not this program, should
understand every line. No program-internal jargon without a plain gloss.

---

## 1. `[SUBSET]` -- replaces the `[K18-REP] order=...` line

States, once per searched type, exactly which family of solutions this run enumerates.
FORMAL NAME ONLY. The everyday glosses this section originally specified were removed on
2026-08-28: the log is a progress report for a specialist, and every description now lives
in the test's own `ReadMe.md`.

    [SUBSET] K18, |Aut| = 2, symmetry type 2^9, branch a=4
    [SUBSET] K18, |Aut| = 2, symmetry type 2^8 1^2, branch a=0
    [SUBSET] K18, symmetry of order 4, type 4^4 1^2
    [SUBSET] K18, symmetry of order 17, type 17 1
    [SUBSET] K18, symmetry group V4, shape fx2,0,0 F4

Why the glosses went, taking "(returns to the start after 4 applications)" as the example:
it is derivable from the type (the order is the least common multiple of the cycle lengths),
it is not unique to order 4 (a 2^9 symmetry also returns after 4 applications -- order means
the SMALLEST such count), and it is ambiguous about what returns (the relabeling, not the
design, which is unchanged by every single application).

Notes:
- `|Aut| = 2` added 2026-08-29, and only under the block driver (same gate as the rejection
  it names): the subject of those cases is the classes whose FULL automorphism group has
  order 2, and a cover with a larger group is rejected as a duplicate rather than written.
  It belongs in the family the line states, not in a filter note further down. The other
  `[SUBSET]` forms -- order 4, order 17, V4 -- are unchanged: there the larger group IS the
  subject.
- `branch a` is our internal label, and the log prints it bare. Its definition is
  `|F0 intersect P(alpha)| = d = pairs - 2a`, and it must land in each block-driven test's
  ReadMe.md, since the log no longer states it anywhere.
- V4 / S3 are standard mathematical terms; E9 is our shorthand for C3xC3.

## 2. The block range -- one line, no explanation

    Blocks 131-133 selected for processing (REP_F3START=131 REP_F3STOP=133)

Superseded 2026-08-29. `Total = <N> blocks` is gone, and with it the last tier-1 line that
was printed only under the owner filter. What replaces it states the range this run will
actually walk, and then the two variables that set it: the LEFT half is clipped to the
column, the RIGHT half is what was asked for, so a stop past the last block shows up as a
difference between the halves instead of passing unnoticed. A whole column reads
`Blocks 0-65039 selected for processing (REP_F3START=0 REP_F3STOP=65039)` -- the same fact the
old total carried, since the last block c is the count minus one.

The 2026-08-28 note it replaces, kept for its reasoning: the four-line `[OWNER]` banner this section originally specified is
gone, and so is its tag: what remains is tier-1 progress information, because it says what
the per-block numbers are out of. The credit rule it explained -- the same solution is
reachable from many blocks, so it is credited to exactly one, the first, and every other
block that runs into it rejects it -- moved to each block-driven test's ReadMe.md.

Counts (RAW blocks): t9 a=4 = 65040, t9 a=0 = 57216, t8 a=0 = 57752 (runtime value =
`creps.size()`); the last c, and so the default REP_F3STOP, is one less than each.
The old diagnostic tail (prefix=..., anchor {..}, F1 diagonals=..., sigma0 fixed points=...)
is dropped.

DECIDED: "blocks" everywhere -- this line says blocks too, so one run uses one word.

## 3. Per-block progress line -- drop the rejected breakdown

old:

    [F3COMPLETE] block 4.0.63610  results=0  saved=0  rejected=0 (foreign=0, in-block=0)  nodes=28513280

new:

    [F3COMPLETE] block 63610  results=0  saved=0  duplicates=0  nodes=28513280

The word became `duplicates` on 2026-08-28 -- see section 9. Which KIND of duplicate it was
is on the `[OWNER]` lines above it under REP_OWNERALL, never on this line.

## 4. DONE lines

old:

    [F3COMPLETE] DONE: a=4, 393 blocks completed, 0 skipped as duplicates, 0 classes saved (last block 4.0.63999)
    [F3COMPLETE] DONE: owner filter -- saved=0  rejected=3 (foreign=3, in-block=0); this range owns
                 what it wrote and nothing else, so it is disjoint from every other range of this column

new (two lines, NOT merged):

    [F3COMPLETE] DONE: all blocks in requested range 63607-63999 processed, number of canonical blocks in this range 393
    [F3COMPLETE] DONE: saved=0  duplicates=3
    [F3COMPLETE] DONE: duplicates aut{16:1}

(the third line is section 9, added 2026-08-28, and is omitted when nothing was rejected
for its reason)

- Range bounds from REP_F3START / REP_F3STOP, clipped to column size; whole column = 0-65039.
- "number of canonical blocks in this range" = the blocks actually completed (`completed`).
- The skipped-block count is dropped. NB: those blocks are skipped because they are
  relabelings of an earlier block (NOT canonical), not because they are duplicate solutions.
- `last block completed` is dropped: `lastCompletedC` never advances over a trailing run of
  skipped blocks, so it was not a true high-water mark.

OPEN: on the saved line, keep only the owner `saved` (what this run wrote), or also print the
full in-memory class total, which differs when a previous run's results were preloaded?

## 5. Periodic `[rep]` line (every 5 min)

old:

    [rep] o2 type 0/9 "" elapsed=600s nodes=209345536 (357424/s) results=0 rejected=0

new:

    [rep] elapsed=10min nodes=209M (357/ms) results=0 saved=0 duplicates=0 processed=62.31% |Aut|=2:5 16:1

- Drop `o2`, `type i/n`, and the empty type string: `[SUBSET]` already says all of it.
- No block info on this line.
- `elapsed` always in whole minutes; `nodes` always in M; rate per ms.
- `saved` moves into the fixed part.
- Tail keeps only `|Aut|=...`; `(+N new)`, `est-total~...` and its `(f1=... f2=...)` Chao1
  terms are dropped.

## 6. Block numbering in output

Console prints use the bare c value, never `a.0.c` -- per-block line, DONE range,
`[OWNER]` verbose lines, the OWNER FATAL self-check, `[BLOCKSFOR]`.

Exception: the to-process list file written by REP_F3LIST keeps `a.0.c` lines (tooling reads it).

## 7. End-of-run summary

old:

    [K18-REP] order=2: 0 classes saved  (20728.2s)  [8 of 9 types skipped]
    P1F: done, 0 result(s) written to result.txt

new:

    P1F: done, 0 result(s) written to result.txt (Total time=345min)

The whole `[K18-REP] order=...` summary line is dropped (the type counter goes with it).

Total elapsed time moves onto the `P1F: done` line as `(Total time=Xmin)`, in whole minutes.

## 8. runs\*\run.bat -- explicit block range, range-stamped file names

Applies to every block-driven case (K18-t9-a4, K18-t9-a0, K18-t8-a0).

1. Both range variables are always set explicitly. No "unset = run to the end":

       SET "REP_F3START=0"          first raw block
       SET "REP_F3STOP=<last>"      last raw block, INCLUSIVE

   Defaults are 0 and the LAST raw block c of that column (2026-08-29; they were 0 and the
   raw block COUNT while the second variable was REP_F3MAX):

       K18-t9-a4   65039   (65040 raw blocks)
       K18-t9-a0   57215   (57216)
       K18-t8-a0   57751   (57752)

2. Log and result names carry the range:

       <CASE>_<START>-<STOP>.log
       <CASE>_<START>-<STOP>.txt

   e.g. `K18-t9-a4_63607-63999.log` and `K18-t9-a4_63607-63999.txt`
   (2026-08-29: the second field was the block COUNT until REP_F3MAX became REP_F3STOP, so
   logs written before that date read `_63607-393`)
   (replaces the fixed `%CASE%.log` and `result.txt`).

3. The .bat refuses to start if its .log already exists -- checked before the exe is
   launched, message and non-zero exit. This mirrors the engine's existing refusal on an
   existing result file; both now key on the same range-stamped name, so a completed
   range cannot be silently overwritten and two different ranges never collide.

OPEN: K18Aut3-All is not block-driven -- keep plain `<CASE>.log` / `result.txt` there,
with the .log-exists refusal still applied?
NOTE: the result file name is what the AllResults tooling reads; renaming it means the
comparison scripts need the same naming rule.

### 8a. Clarifications

- Superseded 2026-08-29: the range is now given by its ENDS. `REP_F3STOP` is the last raw
  block c, inclusive; the engine derives the count from it, and `REP_F3MAX` is REFUSED by
  checkRemovedEnv() rather than read, because a count taken for a stop runs a different
  range and still reports success. (Until that date the engine took `REP_F3START` +
  `REP_F3MAX` = "start block" + "number of blocks", and only the .bat files had changed.)
- The `.log`-exists refusal lives in the .bat and applies to `runs\` only.
  `regression\*` is out of scope and keeps its current naming.
- The result-file refusal stays where it is, in the program. Because the bat now sets
  `RESULT=<CASE>_<START>-<STOP>.txt`, that refusal automatically keys on the range-stamped name.
- K18Aut3-All (not block-driven) keeps `K18Aut3-All.log` / `result.txt`, and DOES get the
  .log-exists refusal.

---

# IMPLEMENTED 2026-08-27

All of the above is in the code (KnA2 only; Tt4 untouched). Build is clean, smoke-run verified
on all three subsets. Where the code differs from the text above, the code is:

- `[SUBSET]` is printed by `printSubset()` in `source/k18a2rep.cpp`, once per cycle type that is
  actually searched -- not at the old startup point, which does not yet know the type. It derives
  the gloss from the type's fixed-point count, so t9 and t8 wording comes out of one code path.
- The branch clause is printed only under `REP_F3COMPLETE`, and the "exactly one of those pairs"
  wording generalizes as `d = pairs - 2a`.
- The DONE range is clipped to the column, and a `REP_F3STOP` below `REP_F3START` prints
  `0-0` rather than an inverted range.
- `lastCompletedC` and `skippedDup` are deleted, along with the now-unused Chao1 tail, the
  end-of-order `autNote` / `skipNote` and their `secs`. Harvest mode keeps a one-line notice,
  because "stopped at a target" is a different outcome from "finished".
- `source/p1f.cpp` times the whole run and closes with `(Total time=Xmin)`.
- README's file-and-git section now describes the range-stamped names.

Verified output, blocks 0-2 of t9 a=4:

    [SUBSET] K18, symmetry type 2^9 (all 18 players moved, in 9 pairs),
             branch a=4 (the one round contains exactly one of those 9 pairs)
    [F3COMPLETE] a=4: 65040 raw blocks, completing the canonical ones
    [OWNER]  65040 blocks, or starters (the distinct ways of filling in the opening rounds;
             each one is searched to completion). The same solution can be reached from
             many of them, so each solution is credited to exactly one - first - and
             every other block that runs into it rejected.
    [F3COMPLETE] block 0  results=0  saved=0  rejected=0  nodes=13843456
    [F3COMPLETE] DONE: all blocks in requested range 0-2 processed, number of canonical blocks in this range 3
    [F3COMPLETE] DONE: saved=0  rejected=0
    P1F: done, 0 result(s) written to smoke_a4.txt (Total time=2min)

RESOLVED:
1. The `[F3COMPLETE] a=...: N raw blocks` line is DROPPED.
2. DONE `saved=` is the only class count, and that is complete: the in-memory class set is filled
   only AFTER both owner rejections, and KnA2 has no REP_PRELOAD, so with the owner filter on it
   equals exactly what the run wrote. They diverge only under REP_OWNER=0, where nothing is
   rejected and `saved` is not meaningful anyway.

Verified on the 480-490 range (block 483 finds one class, owned by block 133, so rejected):

    [F3COMPLETE] block 483  results=1  saved=0  rejected=1  nodes=17947648
        [rep] elapsed=5min nodes=116M (388/ms) results=1 saved=0 rejected=1
    [F3COMPLETE] DONE: all blocks in requested range 480-490 processed, number of canonical blocks in this range 11
    [F3COMPLETE] DONE: saved=0  rejected=1
    P1F: done, 0 result(s) written to smoke_483.txt (Total time=8min)

---

# 2026-08-28 -- the three tiers, and what changed under them

GOVERNING PRINCIPLE, and the test to apply to any future output decision. Each fact belongs
to exactly one place:

1. **Log** = see the process PROGRESS. Terse, formal, specialist. Counts and status only.
2. **ReadMe.md, one per test** = understand the NUANCES. Type descriptions, skip reasons,
   branch and parameter definitions, expected time, how to change the thread count.
3. **Source code** = the specific detail. Anyone who wants more reads it.

This repository does not publish the research technology, but a reader should be able to
understand WHAT IS CALCULATED. For anything beyond that: the paper and the source.

Sections 1 and 2 above have been rewritten to match. Every prose banner is gone from the
log, and each of the four cases in `runs\` now carries a `ReadMe.md`.

## 9. One word for a rejected result: `duplicates`

Everything a run declines to write is a duplicate of something already accounted for, so
one word covers all of it -- on the per-type line, on the per-block line, on the DONE line
and in the summary. `rejected=` is gone.

There are three ways to earn it, and the log does not distinguish them on the counter line:

1. **Another leg of this run already wrote it.** The engine's class set is cleared per
   REP_ORDERS token, so before this a multi-order run wrote the same class once per leg that
   found it. Now one never-cleared set per RUN admits each class once. Measured effect:
   K14 35 records to 21, K16 31 to 30, K20 243 to 230, K18 533 to 531 -- and the deduped
   counts are the known-correct censuses.
2. **Another block owns it.** The block-column filter, unchanged.
3. **Its full symmetry group is larger than order 2.** Only under `REP_F3COMPLETE`, and
   deliberately not gated on order == 2: K14Aut3-All-1 also sweeps order 2 and takes 16 of
   its 21 classes from those legs, nearly all with |Aut| > 2. It is the CASE that is the
   order-2 census, not the order.

Reason 3 is reported once, at the end of the run, as a histogram over the size of the full
symmetry group:

    [F3COMPLETE] DONE: duplicates aut{4:144 8:22 16:12 272:1}

The check is applied AFTER the owner test, so each class reaches it exactly once, from the
block that owns it. A full-column t8 run therefore re-derives the |Aut| > 2 census by a
completely different route than the case that publishes it. Per-class reject lines are NOT
printed: t8 would emit 179 of them, and the log is a progress report.

## 10. `results = saved + duplicates`, and the per-type line

    [SUBSET] K18, symmetry of order 4, type 4^4 1^2
        results=131  saved=131  duplicates=0  nodes=1821756  time=8.0s  aut{4:96 8:22 16:12 272:1}
    [SUBSET] K18, symmetry of order 17, type 17 1
        results=2  saved=1  duplicates=1  nodes=17  time=0.0s  aut{17:1}
    [SUBSET] K18, symmetry of order 3, type 3^2 1^12
        skipped

- Replaces `[K18-REP]  type 17 1  new +2 (cum 2) nodes 17 0.0s`, whose `new +2` was
  misleading: two results were found and one was written.
- Same key=value vocabulary as the block lines, indented four spaces like `[rep]`, and the
  identity `results = saved + duplicates` holds on every one of them, so the result file is
  reconstructible from the log alone.
- The type is NOT repeated on the result line; the `[SUBSET]` header above it names it.
- `cum` is DELETED. It read as a run total but was per-leg, because the class set is cleared
  per REP_ORDERS token. The run total belongs only in the closing `P1F: done` line.
- `aut{...}` counts what the leg WROTE, not what it found, so the per-leg histograms sum to
  exactly the published census. It is omitted when nothing was written.
- `skipped` is bare. The reason for each skipped type is in the test's ReadMe.md.

## 11. `processed=NN.NN%` on the periodic line

    [rep] elapsed=50min nodes=677M (206/ms) results=2288 saved=351 duplicates=1937 processed=86.42% |Aut|=3:351

A percentage, not `1234/1436`: it does not tell you the time, it gives you an idea, and it
beats a ratio.

ONLY the block driver prints it. Its denominator is a range of blocks: fixed before the
work starts, and evenly spent, because blocks are broadly comparable in cost. That covers
every multi-week census run, which is where a percentage is worth having.

The other fan-outs were tried and are not printed.

- The two work-queue paths (over-cap, block threading) count tasks CREATED against tasks
  COMPLETED, and children are pushed as the tree opens, so the denominator GROWS and the
  percentage can fall back late in a long run.
- The ENUMERABLE path does have a fixed denominator, the number of representatives, and it
  was implemented and measured. On the order-3 `3^6` leg of K18Aut3-All it still read 0%
  after 25 minutes of a 64-minute leg that was by then 35% of the way through its nodes:
  the work behind one representative is wildly uneven. A number that says 0 for the first
  half hour is worse than no number, so that source was removed rather than shipped.

Where nothing is published the field is omitted entirely, not printed as 0.

## 12. Not coded, on purpose

- A `REP_ORDERS` split across separate PROCESSES still duplicates when the outputs are
  concatenated. An in-process set cannot see another process. Split by block range instead:
  ownership makes those disjoint by construction.
- `REP_CANONFILE` keeps bypassing the emit path entirely, so re-canonicalization stays 1:1.
