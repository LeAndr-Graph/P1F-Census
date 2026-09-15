# K18 doubles -- a precalculated orbit list for order 2

The K20 order-3 engine precalculates, once per block seed, every orbit `{F, sigmaF, sigma^2 F}`
admissible there, and then walks the block by FILTERING that list instead of regenerating
candidates at each node with `genM`. Measured 9.3x on K20 block 0.

On K18 `sigma` is an involution, so an orbit is a DOUBLE `{F, sigmaF}`, or a single factor when
`sigma` fixes it. This is the design for the same idea on the t8 and t9 legs, and the numbers it
rests on.

The point of precalculating is twofold and it is worth keeping both in view: **do not recompute**
what is fixed for the whole run, and **push as much of the per-node test as possible into bitmap
intrinsics**. The first goal is fully met below. The second is met only in part, and section 4
says exactly where it stops.

---

## 1. What an admissible orbit is

Straight from the P1F condition that any two factors union to a Hamiltonian cycle:

    F != sigmaF   F and sigmaF edge-disjoint, and F + sigmaF a single 18-cycle
    F == sigmaF   a sigma-invariant factor; the orbit has size 1

---

## 2. The universe -- measured, not estimated

`tools/k18_doubles.cpp` in the Tt4 repository enumerates all 34,459,425 perfect matchings of K18
and counts the orbits.

| | t9 (`2^9`) | t8 (`2^8 1^2`) |
|---|---|---|
| admissible orbits | **5,187,745** | **5,166,897** |
| of which doubles | 5,160,960 | 5,160,960 |
| of which sigma-fixed | 26,785 | 5,937 |
| list memory at 68 B/entry | 336 MB | 335 MB |

The doubles count is the same for both types and is exactly half of `2^8 * 8! = 10,321,920` --
the same closed form as the row-pairing count `2^(m-1)(m-1)!` already verified at four sizes, with
m = 9. The enumeration is therefore corroborated independently, not merely plausible.

### 2a. It is ONE universe, not a per-block list and not a sum over blocks

An orbit's admissibility depends only on `sigma` and N. It does not depend on which block is being
searched. So all blocks draw from the SAME 5.19M pool, their lists overlap heavily, and the pool
can be built **once per run** and shared read-only across every thread and every block.

This is the main structural difference from K20, where the list is built per seed. It matters
because K18 has far more blocks -- t8 has 25,530 canonical, t9 has 8,713, against K20's 104 -- so a
per-block build would have been paid two orders of magnitude more often and would have sunk the
idea. It is not paid per block.

---

## 3. Shrink per level

After committing one orbit, how many of the rest survive against it. Both gates applied, as the
engine applies them.

| | t9 | t8 |
|---|---|---|
| survive | 812,400 | 809,672 |
| shrink | **6.4x** | **6.4x** |
| spread over samples | 417,076 min, no filtering at worst | similar |

Applying 6.4x down the tree, against 8 doubles plus 1 fixed factor:

| level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| entries | 5.19M | 812k | 127k | 19.8k | 3.1k | 484 | 76 | 12 | 2 |

**6.4x against K20's ~60x is not the objection it first appears.** K20 pays its shrink from a
full-size list because its block IS the whole search. A K18 block prefix already commits about five
factors, i.e. two and a half orbit levels, so a block starts near 50k entries and is cache-resident
within four more levels. The shallow expensive part of the table is skipped entirely. (The 50k is
arithmetic from the 6.4x, not a measurement -- see section 5.)

---

## 4. How much of this the intrinsics can do

K18 has 153 edges, so an edge set is 3 words: one 256-bit register with room spare, one `vpand`
and one `vptest`. Identical in shape to `bitsDisjoint` in the K20 engine.

But the bitmap is only half the test, and the measurement says how much:

| gate | survivors of 5,187,745 | cut |
|---|---|---|
| edge bitmap (AVX) | 1,870,822 | 2.8x |
| Hamiltonian (row walk) | 812,400 | further 2.3x |

⚠️ **The intrinsics reject 64% and hand 1.87M candidates per filter pass to the part that cannot be
vectorised.** Anyone sizing this from edge-disjointness alone gets an answer more than twice too
optimistic about how much the bitmaps carry.

The Hamiltonian gate resists SIMD for a structural reason. For two edge-disjoint perfect matchings
the union is one 18-cycle exactly when the product of the two involutions has precisely two
9-cycles. That is permutation composition, not a mask test, and at 18 points it straddles the
16-byte lane `pshufb` works in. It is the same wall as the path-end machinery, already recorded as
un-SIMD-able with the transpose idea rejected.

So: precalculating removes the recompute cost completely, and thoroughly, since the universe is
built once per run. It does NOT turn the Hamiltonian gate into a bitmap operation, and that gate is
where the time goes -- `tripCompat` was 44.7% of the K20 profile and this is the non-`bitsDisjoint`
part of it.

---

## 5. Result: 5.2x on t8 a=0, same tree, same classes

Implemented in `completeBlockF3` (k18a2rep.cpp), gated on `REP_PRECALC`, taking branches in
sequence off a per-block list. **Measured on t8 a=0, blocks 0-9, laptop, 10 threads:**

| | plain | doubles |
|---|---|---|
| wall | 530.1 s | **102.6 s** |
| nodes | 243,116,032 | 243,112,960 |
| node rate | 459/ms | **2,369/ms** |
| classes | 22, all `|Aut|=2` | 22, all `|Aut|=2` |

`regression/compare.pl` PASSES in both directions: 22 class records, content identical, only
the record ORDER differs (threads write as they find, and the two schedulers reach them in a
different sequence). Both result files are 26,127 bytes.

**The win is entirely per node.** The two runs walk the same tree -- 3,072 nodes apart out of 243
million, 0.0013% -- so this is the same search done 5.2x faster, not a smaller one. The residual
3,072 is unexplained and worth a look; it is presumably how the first split is counted.

Per-block lists came out at 26,144 / 27,368 / 27,096 / 27,632 orbits, each built in 0.3 s, giving
about 4,200-4,500 branches. So section 3's ~50k arithmetic was the right order, and the build cost
is negligible against a 10 s block -- the universe-once refinement of section 2a is **not needed**
for this to pay, though it remains available.

### 5a. a=4 also WINS, once the prunes are wired into the list walk

⚠️ **This section previously recorded a=4 as a 5x LOSS. That was a measurement error, not a
result.** It compared a plain run with EVERY accelerator on against a doubles run that had none of
them, then attributed the difference to the list. Anyone reading the earlier text, or commits
`aa25460` and `15aa7c8`, should treat their a=4 conclusions as withdrawn.

The like-for-like comparison, t9 a=4 block 1777, 10 threads, both paths with NO accelerators:

| | plain | doubles |
|---|---|---|
| wall | 1,259 s | **163 s** |
| nodes | 377,203,712 | 377,203,712 |
| rate | 300/ms | **2,328/ms** |

Identical node counts to the digit, byte-identical results, **7.7x faster** -- the largest gain
measured anywhere in this work, larger than either census leg. The list never behaved differently
at a=4; only the flag sets being compared differed.

The two prunes are now applied inside the list walk, and the progression on this block is:

| doubles configuration | nodes | wall |
|---|---|---|
| no prunes | 377,203,712 | 163 s |
| + REP_DIAGPRUNE (`splitNodeT` calls `diagPruned()`) | 141,905,920 | 143 s |
| + REP_TYPEMASK (`typeMaskOK` per candidate entry) | 18,241,536 | **21.6 s** |

Against plain with the same flags -- 18,240,512 nodes, 30.3 s -- that is the same tree (1,024
apart, one flush unit of the counter artifact in section 5d) and **1.4x faster**.

Two things worth keeping in view. The accelerators are worth 20.7x on this block, FAR more than
the 2.6x and 1.59x averages quoted from other contexts, which is what made the bad comparison look
plausible. And the list's edge shrinks as the prunes bite -- 7.7x with no flags, 1.4x with all of
them -- because once 95% of the tree is gone what remains is the hard part. The list pays best
where there is bulk.

⛔ Not implemented: `REP_SGEN` and `REP_PATAPPLY` are still not applied in the list walk. The
measurements above have them SET, and the tree matches plain anyway, so on this block they cost
nothing -- `REP_PATAPPLY=oneperpair` is a known no-op under the type mask. Do not assume that holds
on every block.

### 5b. Precondition, not optional

The prototype covers trivial-stabilizer nodes only and needs `REP_PRUNELEVEL=1`. t8 and t9 already
run with it, so both paths walk the same tree. WITHOUT it the trees diverge badly -- measured at
K14 order 2, where the plain path with the prune flag off blows up identically to the list path, so
it is the missing dedup and not the list.

### 5c. a=4 over a real range: 2,201 blocks, 2026-09-09

`runs\K18-t9-a4` blocks 0-2238, laptop, 10 threads, with `REP_PRECALC`:

| | |
|---|---|
| wall | 839 min (14 h) |
| canonical blocks | 2,201 |
| per canonical block | 22.9 s |
| classes | 5, all `|Aut| = 2` |
| `duplicates by |Aut| > 2` | `aut{16:1}` -- P47, owned by block 2237 |

⚠️ **P47 is NOT in the RESULT file, and that is correct.** RESULT is the `|Aut| = 2` census; a
class with a bigger group is counted in the closing duplicates histogram instead. The range was
chosen to end one block past P47 precisely as a correctness check, and `aut{16:1}` is that check
passing. Anyone looking for an order-16 record in the result file will wrongly conclude the run
missed it.

⚠️ **The full-column projection was wrong in commit `435798c`, which said the orbit lists take a=4
"from about 15 days to 11".** That double-counted: the 21.6 s block it extrapolated from ALREADY
had `REP_PRECALC` on, and the 1.3x was then applied a second time. At the 22.9 s measured here the
column is about **16 days**, against roughly 21 without the lists.

That 16 is likely pessimistic. The node rate climbed steadily across the range, 564/ms near block
133 to 850/ms at the end, so blocks later in the column are cheaper and extrapolating from its
front over-states the total.

### 5d. Still open

1. **The node counter undercounts under threads.** Always by an exact multiple of 1024, the flush
   granularity, and at 2 threads it is exactly 2,048 three runs out of three -- deterministic, so
   not a plain race, and my flush-remainder story does not fully explain it. At ONE thread the two
   paths agree exactly (53,785,600 for blocks 0-1, four runs of four). Classes are unaffected.
2. Whether the speedups hold beyond the front of each range; this is 20 blocks of 25,530 (t8) and
   8,713 (t9), all low-numbered. **a=4 is no longer a single block**: see 5d.
3. Thread scaling. On the laptop it saturates by ~4 threads: 1.7x at 2, 2.1x at 4, 2.8x at 10.
   NOT a distribution problem -- the queue path and the sequential path measure the same, and
   4 processes x 2 threads came out 13% WORSE than 1 process x 8. Needs the 32-core box to
   separate a thermal ceiling from a memory-bandwidth one.
4. The per-block list build is 0.3 s single-threaded with the other threads idle, about 3% of a
   10 s block, paid 25,530 times on a full t8 sweep. Overlapping it with the previous block is the
   obvious fix and is not done.

Not open, and not to be re-litigated: the universe size, the 6.4x shrink, the 64/36 bitmap split,
and the measured speedups (5.2x t8 a=0, 5.6x t9 a=0, 1.4x a=4 all-flags, 7.7x a=4 no-flags).
