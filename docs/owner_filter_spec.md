# Owner-filter block sweep + single result file — KnA2 implementation spec

Date: 2026-08-24. Status (2026-08-26): **the owner filter is IMPLEMENTED** as `REP_OWNER` /
`REP_OWNERALL` (§5, §6.1–§6.6), on top of the single-`RESULT` work of §7–§9 which landed earlier.
Covered by `regression/K18-t9-a4-owner-1`: four single-block runs that between them exercise
every path — a block that owns its class, a block holding a class owned elsewhere, a block whose
class has |Aut| > 2 (the general involution enumeration), and a block reaching the same class by
two covers (the per-block key).

Deliberate deviations from what follows:

* **§5.1's `|Aut| == 2` fast path is not implemented.** The general enumeration costs microseconds
  against a multi-second block, so a second code path that could disagree with the first was not
  worth its own existence. `lastAut` is left alone.
* **§6.5's per-block set is consulted FIRST, before the owner test**, not after. Both orders are
  correct; this one skips the owner computation on every repeat, and — the real reason — it makes
  the same-block path observable, since two covers of one class in one block share an owner and
  are otherwise indistinguishable in the log.
* **`REP_OWNERTEST` (§7.1) is not implemented.** `blockmap.pl` already is that tool; §11.1's
  agreement was obtained by comparing its owners against the engine's.
* **§3.1's canonical restriction is redundant** — see the proof added there. Implemented anyway,
  for the reason given.
* **The filter is ON BY DEFAULT (2026-08-26), not opt-in.** `REP_OWNER` as a switch to *enable*
  has no work left to do: every `REP_F3COMPLETE` run dedups with no variable set. Opt-in made
  silence mean "record every re-find", which is the answer nobody wants — a range that re-found a
  class owned by another block wrote it anyway, and the run read like a discovery. `REP_OWNER=0`
  is the opt-out, and the only value that disables. The host guard of §7.1 still validates
  explicit requests only, since the default is conditioned on `REP_F3COMPLETE`, which is k18-only
  and so never reaches it.
* **§12.1 is CLOSED (2026-08-27): t8 works.** `ownerAllTaus` takes the column's fixed-point count
  as a parameter and branches on "u is fixed" as well as "u pairs with w", so it enumerates any
  involution type. One number is enough: `pi tau pi^-1 == sigma0` forces a shared cycle type, and
  on N points an involution's cycle type *is* its number of fixed points. `nfix = 0` takes the
  pairing branch only and reproduces the old fpf enumeration exactly, so t9 is untouched. Nothing
  else needed generalizing — the diagonal-count test on `X` counts `X[u] == tau[u]`, which a fixed
  point can never satisfy, and `ownerIsos` never sees `tau`. With no column left that ownership
  cannot serve, the refusal §12.1 asked for has nothing to refuse and is gone.

Still open: the full-column reproduction of §11.8.

Repo scope: **KnA2 standalone only.** Nothing here is done in TripleSys/Tt4; Tt4 keeps
`REP_PRELOAD`, `REP_INFO`, the P-file layout and every research switch it has today. See §12.4 —
this is the first deliberate divergence between the two copies of the engines.

The block-ownership rule (§3-§5) is derived from `Tt4/MD/TBD.md:98` and its validated prototype
`Tt4/scratchpad/blockmap.pl`. That note is a design sketch against the Tt4 tree; this document is
the buildable version for KnA2, together with the output simplification it enables (§7-§9).

**Design principle for the whole document:** a public user of the standalone tool knows about
**classes** and nothing else. Not `a.b.c` block coordinates, not stay/swap patterns, not canonical
frames, not preload baselines, not the legacy P-file layout. Anything that needs those concepts —
locating a class's block, merging files, assigning stable IDs — belongs in `tools/`, invoked when
someone actually asks for it, not in the engine's default output.

---

## 1. Goals

1. **Any block range, no baseline.** A sweep must be startable at any block, with no file of
   previously found classes: no `REP_PRELOAD`, no seeded `g_harvest`, no "preload and record must
   name the same file" coupling.
2. **One output, one format, every mode.** A run writes exactly one file, `RESULT`, in one simple
   layout, whatever the mode and whatever `N`. No second file, no append, no baseline input.

Goal 1 is what makes goal 2 safe: without ownership, dropping the preload would make a resumed or
sharded run re-report classes it had already found, and the single output file would be wrong.

Today the census is only correct as a *single logical run over the whole column*: the engine decides
"is this class new?" by asking a global set that must have been seeded from every earlier segment.
That is what forces `REP_PRELOAD=result.txt` + `REP_INFO=result.txt` + a frozen `preload727.txt`
copy in `runs/K18-t9-a0/run.bat`. After this change a run over `[lo,hi)` is a **self-contained,
stateless computation** whose output depends on nothing but `lo`, `hi` and the column.

---

## 2. Background — column, block, and why the baseline exists

A block `a.b.c` is a fixed prefix of committed sigma0-orbits (`commitOrbit`, `k18a2rep.cpp:994`):
every cover found in that block contains those factors. The F3COMPLETE driver
(`k18a2rep.cpp:3036-3200`) walks `c = 0 .. creps.size()-1`, canonizes each raw block with
`canonV_fast` (`:161`), completes only **first-occurrence (canonical)** blocks and skips the rest
with a count.

A single class is reachable in MANY blocks. For t9 a=0 each class has exactly one fpf involution in
its automorphism group and **288 column labelings, collapsing to 144 distinct covers — one per raw
block** (`pi` and `sigma0.pi` give the same image); of those 144 raw blocks on average **21.51 are
canonical**, so a full a=0 sweep completes ~15,800 covers to save 727 classes. Every cover after the
first is a re-detection that must be recognized and dropped — and *that* is the only thing the
global set is used for.

The observation that removes it: **the block a class is re-found in is computable from the cover
alone.**

---

## 3. The ownership rule

> Every class has a canonical home in a column: the **minimum block index over all its column
> labelings**. When a block completes a cover, compute the cover's owner and save the class **only
> if `owner == this block`.**

**Correctness.** The cover was found in block `c`, so `c` is one of the blocks holding the class, so
`min <= c`: the owner is never in the future of the sweep. It is also not merely *not in the future*
— it is a fixed, labeling-independent property of the class and the column, so it is the same value
whichever block, shard, machine or run computes it.

**Consequences.**

* A class is written by **exactly one block** in the whole column ⇒ dedup with no global state.
* A shard `[lo,hi)` writes exactly the classes whose owner lies in `[lo,hi)` ⇒ shards are **disjoint
  by construction**; totals add; the union over a partition of `[0, creps.size())` is the complete
  census. No coordination, no merge-time dedup.
* Resume needs only the block index, never a class list.
* Free corollary (§10.2): a per-block *prediction* of the result count, usable as a regression test
  and as a new-class detector applied retroactively to already-swept blocks.

**What it does NOT buy: speed.** ~15,800 owner computations against ~2e11 search nodes — minutes on
a multi-day run. This is a correctness/operability change, not a performance one.

### 3.1 Ownership must be taken over COMPLETED blocks, not raw blocks

The sweep completes only canonical blocks; duplicate starters are skipped unsearched. If the raw
minimum happened to be a skipped duplicate, the class would be owned by a block nobody searches and
would be **lost**. So the rule is:

> `owner(R) = min { c : block c holds R AND c is a first-occurrence (canonical) block }`

Well-defined, and still `owner <= c`, because the current block `c` is itself canonical (only
canonical blocks are completed) and holds `R`. Both facts the implementation needs — "does block `c`
hold `R`" and "is `c` canonical" — are already computed by the driver for every `c <= i`, and
`owner <= i` means no lookup beyond `i` is ever required.

With `REP_F3RAW` set, every raw block is completed, the canonical filter is the identity, and the
rule degenerates to `min` over all raw `c`. Both modes are sound; they own classes at different
blocks, so **do not mix owner-mode outputs produced with and without `REP_F3RAW`.**

**Correction (2026-08-26): the loss this section guards against cannot happen, and the "empirical
fact" below is a theorem.** The driver scans `i = 0 … creps.size()-1` and `canonKeys.emplace` keeps
the FIRST occurrence, so the canonical representative of a key class is by construction the lowest
raw `c` in it. Two raw blocks sharing a `canonV_fast` key are related by a relabeling carrying one
committed set onto the other — which is exactly why skipping one of them is sound: they hold the
same classes. Therefore if a non-canonical block `c` holds `R`, its twin `c' < c` also holds `R` and
`c` was never the minimum. **The raw minimum is always canonical.**

Verified on the a=4 column against a full RAW block map (all 65,040 blocks, not just the 59,872
canonical ones): min over all raw blocks equals min over canonical blocks for all 6 known classes —
133, 182, 254, 434, 801, 2237 either way. The 13/10/12/12/11/2 non-canonical blocks each class also
sits in are every one of them shadowed by a smaller twin.

The restriction is implemented regardless. It costs one byte per block and one branch, and it makes
correctness independent of the "canonical == lowest `c`" ordering detail instead of resting on it.
What must NOT be inferred is that it is a safety net: it is not, and the `owner <= c` self-check of
§5.3 is the thing that actually catches a broken owner function.

---

## 4. Definitions used below

| name | meaning | where it lives today |
|---|---|---|
| `sigma0` | the standard fpf involution `(0 1)(2 3)...(16 17)` | `sh.alpha` |
| `PFX` | the column prefix: `dw.chosen[0 .. base3-1]`, constant across all blocks of the column | `k18a2rep.cpp:3111` (`base3`) |
| `npfx` | `base3` | as above |
| `D1` | number of diagonal pairs of `PFX[0]` w.r.t. `sigma0` (9 for a=0, 1 for a=4) | derivable; `a = (9 - D1)/2` |
| `(u0,v0)` | the level-3 anchor edge | `childReps(...)` out-params, `:3017` |
| `ckeyIdx` | `orbitKey(F) -> raw block index c` | `:3016`, already built |
| `isCanonC[c]` | was raw block `c` a first occurrence | derivable from `canonKeys`, `:3020` / `:3138` |
| `c` | the raw block currently being completed | loop var `i`, `:3112` |

---

## 5. The owner function

`long long ownerOf(const std::vector<Match>& cover)` — a pure function of the cover and the column
context. Direct port of `blockmap.pl`'s per-result loop.

```
ownerOf(cover):
  best = +inf
  for each tau in autInvolutions(cover):                       # 5.1
      for each factor X in cover:
          if apply(X, tau) != X:            continue           # X must be tau-invariant
          if diagCount(X, tau) != D1:       continue           # ... with the column's diag count
          for each factor Y in cover, Y != X:
              for each pi in isos(X, Y -> PFX[0], PFX[1]):     # 5.2
                  if pi . tau . pi^-1 != sigma0:      continue
                  img = { apply(F, pi) : F in cover }
                  if not PFX subset-of img:           continue
                  F3 = the factor of img covering edge {u0,v0}
                  it = ckeyIdx.find(orbitKey(F3)); if none: continue
                  cc = it->second
                  if not isCanonC[cc]:                continue # 3.1
                  best = min(best, cc)
  return best
```

`isos` must be enumerated, not just found once: a class typically has 288 valid `pi`, landing on 144
distinct blocks.

### 5.1 `autInvolutions` — and the fast path that skips it

`tau` ranges over the involutions of `Aut(cover)` **with the same cycle type as `sigma0`** (t9: fpf
`2^9`; t8: `2^8.1^2`). Implemented as `ownerAllTaus(fac, nfix, out)` — one parameter, the column's
fixed-point count, which pins the cycle type on its own (§12.1).

**Fast path — the common case.** `canonKey()` (`:1021`) already sets `lastAut = 2*|Aut|` on the cover
`emit()` has in hand. When `|Aut| == 2`, `Aut = {id, sigma0}` and `sigma0` is fpf, so **`tau =
sigma0` is the unique candidate** and the whole enumeration is skipped. For t9 a=0 that is every one
of the 727 classes; the general routine never runs there.

**General path.** Port `all_taus` from `blockmap.pl:96-131`: color each vertex pair by the factor
covering it, then backtrack over `tau[u] = w`, propagating the induced factor permutation and
rejecting on contradiction. Needed for a=4 and for any stratum with `|Aut| > 2`.

### 5.2 `isos` — column labelings from a factor pair

Port `alt_cycles` + `isos` (`blockmap.pl:134-183`). `X u Y` and `PFX[0] u PFX[1]` are 2-regular;
decompose both into alternating cycles, match cycles of equal length, and for each matching enumerate
rotations by even offsets and both directions (so position 0 still leaves along a `PFX[0]` edge,
preserving the `X -> PFX[0]` alternation). Each complete assignment is one `pi`.

### 5.3 Mandatory self-check

The identity relabeling is always a member of the enumeration: `X = PFX[0]` is `sigma0`-fixed with
diagonal count `D1`, `Y = PFX[1]`, `pi = id` conjugates `sigma0` to itself, and the anchor factor is
this block's own orbit representative. Therefore:

> **`ownerOf` must return a value `<= c`, and the block set it enumerates must contain `c`.**

Implement that as a hard assertion, not a comment. If it ever fails, the owner function is wrong and
the run is silently losing classes — **abort the run**, printing the block and the cover. It costs
one comparison per cover and converts the single dangerous failure mode into a loud one.

---

## 6. Engine changes

Line numbers are `source/k18a2rep.cpp` unless stated otherwise.

**6.1 Column context (new, file-scope).** A small read-only struct filled once per column, just after
`childReps` at `:3017`: `PFX` (copy of `dw.chosen[0..base3-1]`), `npfx`, `D1`, `u0`, `v0`,
`const ckeyIdx*`. Plus `std::vector<char> isCanonC(creps.size(), 0)` and an atomic
`long long g_ownerBlock` = the raw `c` currently being completed. Workers only read these.

**6.2 Fill `isCanonC`.** At `:3138`, where `canonKeys.emplace` decides `isNew`, set
`isCanonC[i] = isNew`. Already in the loop; no extra work. Note this runs for every `i` from 0
regardless of `REP_F3START` — the driver canonizes the full column (~1 min) on every start — so
`isCanonC[cc]` is populated for every `cc <= i`, which §3.1 shows is all that is ever queried.

**6.3 Set the current block.** At `:3156`, next to the existing `g_curBlock` label, store
`g_ownerBlock = (long long)i`; clear it at `:3159`.

**6.4 The decision, in `emit()` (`:1143`).** After `canonKey()` (`:1156`, which sets `lastAut`) and
before the `g_harvest_mtx` block at `:1168`:

```
if (g_ownerMode) {
    long long own = ownerOf(chosen);            // uses lastAut for the |Aut|==2 fast path
    if (own > g_ownerBlock) FATAL("owner self-check");        // 5.3
    if (own != g_ownerBlock) { g_foreign++; return; }         // owned elsewhere -> drop, silently
    if (!g_blockSeen.insert(key).second) { g_dupInBlock++; return; }   // 6.5
    g_owned++;
}
```

**6.5 Per-block dedup — the only state that remains.** A block may in principle reach the same class
by more than one cover. `g_blockSeen` is a `std::set<std::string>` of canonKeys **cleared at block
start** (`:3156`); it holds at most a handful of entries and never crosses blocks, so it is not
global state and does not affect shard independence. (For t9 a=0 the multiplicity is exactly 1 per
block and this set is expected to stay a no-op; it must still exist, because a=4 and higher-`|Aut|`
strata are not covered by that observation.)

**6.6 Counters.** `saved=` in the block line and the `[rep]` line currently means "distinct classes
in `g_harvest`", which in owner mode counts foreign classes the run will never write. Replace with
`owned=` (written by this run) and `foreign=` (dropped, owned elsewhere). Keep `g_harvest` running as
a **diagnostic only** — it is 727 entries and it feeds the existing f1/f2 and `aut{}` reporting — but
it must no longer influence any save decision.

**6.7 Carry |Aut| to the result callback — all four engines, identical edit.** Every engine binds

```
g_sendResult = [this](const unsigned char* s) { if (resultCallback) resultCallback(cbClass, s, 0, 1, 2); };
```

(`k14a2rep.cpp:1876`, `k16A2rep.cpp:1487`, `k18a2rep.cpp:2420`, `k20a2rep.cpp:1435`)
and calls it as `g_sendResult(src)` from `emit()`. Widen the function object to
`std::function<void(const unsigned char*, int)>`, pass `lastAut / 2` at the call site, and forward it
through the **already-free `r4` slot** of `ResultCallback` (`include/kBase.h:10`, currently hardcoded
`0`). One line per engine, no new per-engine writer, no signature change to `ResultCallback` itself.

This is why the format of §8 is writable at all: `lastAut` is set by `canonKey()` in **all four**
engines, but only k14 and k18 have a record path (`repInfoWrite`), which is exactly why k16/k20
regression cases must compare a P-file that prints `|Aut(M)| = 0`. Routing |Aut| through the callback
gives one host-side writer for all of them.

**6.8 Env guards, at startup, before any work.**
* `RESULT` unset ⇒ print and **stop**. A run whose results go nowhere is never what was meant.
* `RESULT` names an existing file ⇒ print and **stop** (§8.3).
* `REP_OWNER` without `REP_F3COMPLETE` ⇒ print and **stop**. There is no column, no `creps`, no
  anchor without it.
* `REP_PRELOAD`, `REP_INFO` or `KNA2_RESULT` present in the environment ⇒ print "removed in KnA2,
  see docs/owner_filter_spec.md" and **stop**. They are deleted (§7.2), and silently ignoring a
  variable someone deliberately set is how a run quietly produces the wrong file.

---

## 7. Env contract

### 7.1 New

```
RESULT=<path>        The ONE output file of a KnA2 run, in every mode and for every N (see 8).
                     Written by the host from the engine callback, never appended to: if <path>
                     already exists the run reports an error and exits before any work.

REP_OWNER            Owner-filter mode, ON BY DEFAULT under REP_F3COMPLETE. A completed cover is
                     written only if this block is the class's owner (minimum canonical block over
                     all column labelings). No baseline needed: any REP_F3START/REP_F3STOP range is
                     self-contained and two ranges never write the same class. REP_OWNER=0 is the
                     opt-out and the only value that disables; naming the variable explicitly (any
                     other value) also turns a column it cannot serve into a hard stop. Requires
                     REP_F3COMPLETE. k18 only until the siblings are realigned (12.3).

REP_OWNERTEST=<file> Offline check, no search. Load each class from <file>, print its owner and
                     the count of canonical blocks holding it, then exit. The engine-side twin of
                     blockmap.pl and the primary validation tool (11.1). Developer switch: it
                     prints block coordinates and is not part of the public surface.

REP_OWNERALL         With REP_OWNERTEST: print every block of each class, not just the owner.
```

### 7.2 Removed from KnA2

| variable | why it goes |
|---|---|
| `REP_PRELOAD` | The baseline exists only to recognize re-detections. Ownership does that from the cover alone, statelessly. With it gone, the engine never *reads* a result file. |
| `REP_INFO` | A second, richer output carrying block, stay/swap pattern and `# dup` attribution — all internal concepts (see the design principle above). `RESULT` replaces it. |
| `KNA2_RESULT` | The legacy P-file layout, whose header prints `|Aut(M)| = 0`. Superseded by `RESULT`. |

Delete their handling rather than aliasing it, and drop `KNA2_RESULT`/`REP_PRELOAD`/`REP_INFO` from
`docs/env_variables.txt`. Keep them in `test_env_reset.bat`'s clear list (a stale one must not reach
the guard in §6.8 from an unrelated shell) and **add the unprefixed `RESULT` there** — that list
clears `REP_*` plus `KNA2_RESULT` by name, so `RESULT` would otherwise be missed and a leftover value
would silently redirect a case's output.

---

## 8. Output

### 8.1 One file, one format

One record per distinct class:

```
#1 |Aut| = 2
 "   0  1   2  3   4  5   6  7   8  9  10 11  12 13  14 15  16 17 "
 "   0  2   1  4   3  6   5  8   7 10   9 12  11 14  13 16  15 17 "
 ...
```

* Header: `#<seq> |Aut| = <aut>`, `seq` counting from 1 in write order within this file.
* Then `NM` row lines in `src` pair layout: `" \""`, then for each `i` in `0..N-1`
  `(i % 2 ? " " : "  ")` followed by `%2d`, then `" \"\n"`. That is `p1f.cpp:saveResult`'s existing
  row loop with `%3d` narrowed to `%2d`; the two rows above are its verified output for K18 rows 1
  and 2.
* No blank-line separators, no trailing summary, nothing else in the file. Counts, histograms and
  block bookkeeping stay in the log.

Written by `saveResult` in `source/p1f.cpp`, which now receives |Aut| through §6.7 — one writer,
every engine, every mode.

Records are written **live and flushed per record**, not buffered to the end: a 30-hour run that is
killed must leave a valid, readable, correct prefix. This is not optional polish — it is a property
`REP_INFO` has today and two `runs/` cases document by name ("`REP_INFO` flushes per class, so a
kill loses nothing", `K18-t8-a0/run.bat:9`). `RESULT` inherits the obligation along with the role.

### 8.2 No append, no resume into a file

`RESULT` is opened `"w"` after an explicit existence test. **If the file exists, print an error and
exit immediately** — before the ~1 min column canonization and before any search, so a mistake costs
a second and not a day. (`p1f.cpp` opens the result file `"a"` today; that becomes `"w"` plus the
test.)

A run is therefore never resumed into its own output. A killed range is re-run into a NEW file;
owner mode makes that correct without a baseline, which is the whole point.

### 8.3 What "no merge" means

The engine performs no merging: it never reads a result file, never dedups against one, never
rebuilds one through `tmp.txt`. The `REP_PRELOAD`/`REP_INFO` same-file coupling disappears with them.

It does not mean a long census lives in one file — the finished a=0 sweep took three legs across two
days, which under this contract is three files. Owner mode makes those files disjoint by
construction, so combining them is pure concatenation with no dedup, no ordering and no key. **That
combination is a `tools/` job, not the engine's** — as are block lookup, class IDs, and anything
else that needs the `a.b.c` vocabulary.

### 8.4 Cases to re-baseline

`regression/K14Aut3-All-1` and `K18-t9-a4-block1777-1` compare a `REP_INFO` record file;
`K16Aut3-All-1` and `K20Aut4-All-1` compare the `KNA2_RESULT` P-file. All four now compare a
`RESULT` file instead, so all four baselines are re-cut once, in the same commit as the change, with
the diff inspected rather than blind-copied. The `runs/` cases (`K18-t8-a0`, `K18-t9-a0`,
`K18-t9-a4`, `K18Aut3-All`) lose their `REP_PRELOAD`/`REP_INFO`/`KNA2_RESULT` lines, the
`copy /Y preload727.txt result.txt` seeding and the "727 preloaded + any new" `find /c` arithmetic;
their class count becomes a plain record count. `preload727.txt` stays in the repo as a **reference
answer**, not as an input.

---

## 9. Log format

Per completed block, extend the existing `[F3COMPLETE] block ...` line:

```
[F3COMPLETE] block 0.0.19724 (canon #7123): results=2  owned=1 foreign=1  nodes=26955776 ...
```

At DONE, replace `saved=` with the two totals and state the invariant explicitly:

```
[F3COMPLETE] DONE: ... | owned=727 foreign=15111 | range [0,47926) is COMPLETE and disjoint
                        from every other range
```

---

## 10. Cost

* Per cover: one `canonKey` (already paid), then the `tau`/`X`/`Y`/`pi` enumeration. With the
  `|Aut|==2` fast path this is one factor-pair sweep producing ~288 labelings — microseconds.
* Per full a=0 column: ~15,800 covers ⇒ seconds of owner computation against ~2e11 search nodes.
* Memory: `isCanonC` is one byte per raw block (57,216 for t9 a=0). `g_blockSeen` is a handful of
  entries. Nothing grows with the number of classes found.

### 10.1 What it is not

It does not prune the search, does not skip a block, and does not change node counts. A range that
took 31.7 h will still take 31.7 h.

### 10.2 Free corollary — per-block result prediction

Given the known class set, the predicted number of covers in block `c` is the number of classes whose
block list contains `c`. VERIFIED on block 0.0.1: prediction 2, engine `results=2`. Excess actual
over predicted = **a new class in that block**; equality = nothing new there. It is a post-search
test, so it cannot justify skipping a block, but it detects new classes with no dedup at all and can
be applied retroactively to the already-swept a=0 logs. A natural first `tools/` tool.

---

## 11. Validation ladder

Cheap first; do not proceed past a failure.

**11.1 Owner function vs the prototype (no search).** `REP_OWNERTEST=preload727.txt REP_OWNERALL=1`
against `perl blockmap.pl --dump=t9_a0_raw_dump.txt --list=t9_a0_raw_list.txt
--canon=t9_a0_canon_list.txt` with `SVC_ALLBLOCKS=1`. The two block lists must match **exactly** for
all 727 classes. This is the whole correctness burden, settled offline in ~1 minute, and it is why
`blockmap.pl` was written and validated 6/6 first (including the duplicate pairs 133<->1777 and
483<->182).

**11.2 Self-check never fires.** §5.3's assertion must hold on every cover of 11.1 and of every run
below.

**11.3 Format and |Aut|.** Re-run `K16Aut3-All-1` and `K20Aut4-All-1`: the new `RESULT` files must
carry the **real** automorphism orders — K16 `{3:19, 5:5, 7:4, 14:1, 15:1}` over 30 classes — where
the old P-file printed `0`. This is the direct test of §6.7.

**11.4 Per-block prediction.** For ~20 blocks with known result counts from the finished a=0 logs,
predicted == logged `results`.

**11.5 Small-N oracle.** K14 owner-mode sweep of a full column: the union of written classes must
equal the known K14 class set, each exactly once. Requires §12.3 first.

**11.6 Single-block equivalence.** `REP_F3START=19724 REP_F3STOP=19724` (written `REP_F3MAX=1` when the experiment was run) with and without `REP_OWNER`:
same `results=`, same nodes; owner mode writes a subset, and every class it writes must be one whose
owner (per 11.1) is 19724.

**11.7 Shard disjointness.** Three adjacent ranges of ~50 blocks, three `RESULT` files. No class
appears in two of them; the concatenation equals a single run over the union, up to record order and
per-file `#seq` renumbering.

**11.8 Full-column reproduction (the deliverable).** Owner mode over the whole a=0 column must write
**exactly 727 records, each exactly once, with no baseline of any kind.** Strictly stronger than the
completed 2026-08-24 run, which needed `preload727.txt` to make "0 new" meaningful.

---

## 12. Scope limits

**12.1 t9 only in v1 — ✅ LIFTED 2026-08-27.** As written: "the `tau` cycle type is hardcoded as
fpf; for **t8** `sigma0` has two fixed points, so `autInvolutions` must enumerate `2^8.1^2`
involutions and the diagonal-count test changes — leave the cycle type a parameter so t8 is a
fill-in, not a rewrite, and refuse `REP_OWNER` with a clear message when the type is not fpf."
Done as the fill-in, minus the refusal, which now has nothing to refuse: `ownerAllTaus(fac, nfix,
out)` enumerates involutions with exactly `nfix` fixed points, `OwnerColumn::nfix` carries the
column's own count, and the diagonal-count test needed no change after all (it counts
`X[u] == tau[u]`, which a fixed point cannot satisfy, so it already measured 2-cycles only).

**12.2 a=4.** The rule is stratum-independent (`D1 = 1`), but the `|Aut| == 2` fast path does not
cover a=4 classes with larger automorphism groups, so §5.1's general `all_taus` must be implemented
before a=4 uses this mode. `blockmap.pl` already validates against the stored a=4 block numbers, so
11.1 extends to a=4 with the existing `a4_blocks_dump.txt` / `a4_blocklist.txt`.

**12.3 Siblings inside KnA2.** The `RESULT` work of §6.7 and §8 is uniform across k14/k16/k18/k20
— one identical line per engine plus one host writer — and must land in all four together.
The owner filter of §5-§6.6 is k18-only for now, because the block-column driver is; nothing in it
is N-specific (it is written against `N`, `NM`, `mPairs`, `sh.alpha`, `orbitKey`, `canonV_fast`), so
copy it verbatim when the other engines get a column driver rather than letting it become a second
`genS`-style deviation.

**12.4 Divergence from Tt4 — deliberate, and it must be written down.** `README.md` currently states
that the five rep sources are *byte-identical copies of Tt4 at `85ade43`* (verified true as of
2026-08-24). §6.7 breaks that for all four, and §7.2 removes three environment variables Tt4 keeps.
Tt4 is explicitly not being changed: it stays the research tree, with `REP_INFO`, the stay/swap
pattern attribution the pattern-filter work depends on, and `REP_PRELOAD`. Update `README.md` in the
same commit to say the sources are *derived from* `85ade43` and list the KnA2-only deltas, so the
next person diffing the trees finds the answer instead of a surprise.

---

## 13. Open questions

1. **Multiplicity > 1 per block.** For t9 a=0 each block holds each of its classes exactly once, so
   §6.5's per-block set is a no-op there. Whether that holds for a=4 is unverified — `blockmap.pl`
   already prints per-block cover counts (`SVC_ALLBLOCKS=1` shows `c:covers`), so this is one offline
   query, worth answering before a=4 uses owner mode.
2. **Interaction with `REP_F3L4`** (level-4 block sharding) — ✅ **SETTLED 2026-08-27 by deletion.**
   Two shards of the same block would both see themselves as `c` and both write an owned class. The
   spec offered "refuse the combination, or move the per-block set behind a shared file"; the
   variable was removed instead, together with `REP_F3CAP` and `REP_CANDCAP`, which fail the other
   way — a block that returns less than it holds loses the class it *owns*, silently, while the run
   still reports that the range owns what it wrote. All three now stop the run via
   `checkRemovedEnv()`. A switch that trades completeness for a diagnostic has no place in a tool
   whose single output is a census; `tools/` is where sampling belongs.

### 13.1 Settled, not open

**"No additional outputs" covers the result files only** (decided 2026-08-24). §7.2 removes the two
*result* outputs. The developer dump switches — `REP_F3LIST`, `REP_F3DUMP`, `REP_CDUMP`, `REP_PDUMP`,
`REP_ESTFILE`, `REP_BLOCKDUMP`, `REP_EVENTFILE`, `REP_DUMPFIRST` (`dump_cover.txt`) and the rest —
**stay**. They are off by default, so a public user never sees a second file, and they are the raw
material `tools/` consumes: `blockmap.pl`'s inputs are `REP_F3RAW` dumps, and validation 11.1
would have no inputs without them.

---

## 14. Pointers

* Design note this derives from: `Tt4/MD/TBD.md:98`.
* Prototype and its documentation: `Tt4/scratchpad/blockmap.pl`,
  `Tt4/MD/block_class_incidence_tools.md` (also `blockid.pl`, `blockshare.pl`) — the natural seed of
  `tools/`.
* Driver to modify: `source/k18a2rep.cpp:3036-3200`; decision point `emit()` `:1143`.
* Existing pieces reused: `orbitKey` `:207`, `cRankCanon` `:236`, `canonV_fast` `:161`, `canonKey`
  `:1021` (sets `lastAut`), `childReps` `:1860`, `ckeyIdx` `:3016`.
* Result path to rewrite: `source/p1f.cpp` (`saveResult`, `g_resFp`, the `"a"` open),
  `include/kBase.h:10` (`ResultCallback`, the free `r4` slot), and the five `g_sendResult` bindings
  listed in §6.7.
* Mechanisms being deleted: `REP_PRELOAD` loader `:2435`, `REP_INFO`/`REP_PRELOAD` same-file guard
  `:2313`.
* Reference answer: `runs/K18-t9-a0/preload727.txt` and the completed 2026-08-24 logs beside it.
