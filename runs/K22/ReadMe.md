# `runs\K22\` -- the K22 cases

The theorems used here are proved at the end of this file. The planned
scope was all P1F classes with `|Aut| > 3`. On 2026-10-29 it was narrowed to what is complete (below):
orders 4 and V4 proved out of reach on this laptop (months), and order 5 needs ~34 days.

**Complete:** every K22 P1F class with an automorphism of order 7 or 11 -- **541 classes**,
`|Aut|` = {7:536, 21:3, 110:1, 220:1}; every class with an automorphism of order 10 or 20 -- **146
more** (687 in all; order 10: 148 `{10:126, 20:20, 110:1, 220:1}`, of which the 110 and 220 classes are the order-11
ones; order 20: 17 `{20:17}`, all inside the order-10 set) -- plus: no K22 P1F has S3 in its
automorphism group, and none has the V4 shape `fx2,2,2 F4`. Orders 13, 14, 15, 17, 19 and 40 cannot
occur (fixed-point lemma).

**|Aut| > 6 is complete (2026-10-30):** the generic group legs D8, Q8, C4xC2, C2xC2xC2, A4, D10 add 0 classes
(2 min), so the K22 P1F classes with |Aut| > 6 are exactly these 687, `|Aut|` = {7:536, 10:126,
20:20, 21:3, 110:1, 220:1}. Why these legs suffice: the last section below.

| case | automorphism | classes | wall (8 threads) | status |
|---|---|---|---|---|
| `K22Aut21` | type `21 1` (1-rotational) | 3 `{21:3}` | ~15 s | done 2026-10-25; = Pike-Shalaby's 3 starter classes, and an independent starter enumerator gives the same 3 |
| `K22Aut11` | type `11^2` | 2 `{110:1, 220:1}` | ~45 s | done 2026-10-25; `p1f_check` CLEAN |
| order 7 | type `7^3 1` | 539 `{7:536, 21:3}` | 45 min | done 2026-10-26 (includes the 3 order-21 classes) |
| `K22AutS3` | S3 shapes | 0 | 76 min | done 2026-10-26 |
| `K22AutV4` | V4 shape `fx2,2,2 F4` | 0 | 101 s | done |
| `K22AutV4split` | V4 shape `fx2,0,0 F5` | 1 so far (the 220 class) | months | NOT SEARCHED: 26 level-3 branches of 46,656 in 19.5 h; with REP_MRVWALK still ~20 days for block 0 alone |
| order 4 | type `4^5 1^2` | 45 found in 1 h `{4:45}` | months | NOT SEARCHED: 70,684 level-2 branches, all 48 sampled over 250k nodes (>= 27 days lower bound) |
| order 4 | type `4^5 2` (`REP_ONLYTYPES=10`) | | | NOT SEARCHED: 3 blocks, but the search passes 4 GB in 40 s (huge block stabilizers) |
| order 10 | `10^2 1^2`, `10^2 2` | 148 `{10:126, 20:20, 110:1, 220:1}` | 5 min | done 2026-10-29 |
| order 20 | `20 1^2`, `20 2` | 17 `{20:17}` | 2 min | done 2026-10-29 |
| groups D8, Q8, C4xC2, C2xC2xC2, A4, D10 | 6 shapes (Q8, C4xC2, C2xC2xC2: none pass the lemma) | 0 | 2 min | done 2026-10-30 |
| order 5 | type `5^4 1^2` | 268 in a 16-block sample | ~34 days | NOT SEARCHED: 25,460 blocks of ~930 s each (one thread), very uniform; REP_MRV ~3x slower here |

Orders 6, 9 and the group E9 are empty at K22 by the fixed-point lemma (below). Orders 13, 17
and 19 cannot occur. `|Aut| = 2` and `|Aut| = 3` exactly are out of scope.

The first-run classes are in `results_2026-10-25\`. The catalog is `AllResults\K22_P1F_aut_gt6.txt`
(687 classes, `p1f_check` CLEAN); every bat that finds classes checks its result against it
(`compare_to_catalog.pl --n 22`), and a second run of every bat checked Ok. Literature: Pike and
Shalaby (JCMCC 44, 2003) give 3 classes from starters and 17 from even starters at K22 -- exactly
our order-21 leg and our order-20 leg. No K22 P1F classification was found in the literature.

---

## Why these legs are enough for `|Aut| > 6`

**Fixed-point lemma.** Let `a` be a non-identity automorphism of a P1F of K_2n with `f >= 1` fixed
vertices `S`. An edge inside `S` lies in a factor that `a` maps to itself; a fixed factor pairs `S`
within itself; two fixed factors would make `S` a union of components of their union, a Hamiltonian
cycle, so `S` = all vertices. Hence at most one fixed factor, `C(f,2) <= f/2`, and **`f <= 2`**.
Applied to every power of an element, this decides which cycle types can occur. Second rule (odd
prime `p` not dividing `N-1`): the number of `p`-cycles is even.

**Consequences at K22** (`N-1 = 21`):

* Primes: only `5^4 1^2`, `7^3 1`, `11^2` and the 1-rotational `21 1` survive; 13, 17, 19 leave
  more than 2 fixed points, so they never divide `|Aut|`.
* Order 6 is empty (the cube fixes at most 2 points, so no 3-cycles; the square then needs
  `6a >= 20`, but 22 is not 0, 1 or 2 mod 6). Order 9 is empty (`9a + c = 22` needs `c = 4`). The
  group `C3 x C3` is empty (all 8 elements are `3^7 1`; Burnside gives `(22 + 8)/9` orbits, not an
  integer). Orders 14, 15, 40 do not occur; C8 does not occur (its 4th power fixes every point
  outside the 8-cycles, and `8a + f = 22` with `f <= 2` has no solution).
* 25 never divides `|Aut|` (no element of order 25, and `C5 x C5` has no orbit of size 25 on 22
  points; a 5-orbit's stabilizer would fix 5 points).

**Case split** for a group `G` of automorphisms with `|G| > 6`:

1. 7 or 11 divides `|G|`: found by the order-7 or order-11 leg.
2. 5 divides `|G|`, `P` a subgroup of order 5. If an involution normalizes `P`, it centralizes it
   (an element of order 10: order-10 leg) or inverts it (a D10: D10 leg). Otherwise the normalizer
   of `P` has odd order and equals the centralizer, which is `P` itself (an element of order 3 in it
   would give order 15). Then `P` is central in its normalizer, and Burnside's transfer theorem gives
   a normal complement `K` with `|K| = |G|/5` on which `P` acts. If `P` centralizes an element of
   order 2 or 3 of `K`, `G` has an element of order 10 or 15; otherwise `|K| = 1 mod 5`: `|K| = 6` is
   impossible (Aut of C6 and of S3 has no element of order 5), and every other such `|K|` is
   divisible by 8 or 9 -- case 3.
3. `|G| = 2^a 3^b`: 9 dividing `|G|` is empty (above). 8 dividing `|G|` gives a subgroup of order 8:
   not C8 (above), so D8, Q8, C4xC2 or C2xC2xC2 -- one leg each. What is left is `|G| = 12`: A4
   (leg); D12 contains S3 (S3 leg: 0); C12, C6xC2 and Dic3 contain an element of order 6 (empty).

So every class with `|Aut| > 6` is found by one of the legs 7, 11, 10, 20, D8, Q8, C4xC2, C2xC2xC2,
A4, D10 (order 20 is a cross-check of order 10; S3 is searched too). The generic group legs were
validated on K18: order 8 through C8, D8, Q8, C4xC2, C2xC2xC2 gives exactly the catalog's 35
classes with 8 dividing `|Aut|`. `|Aut| > 3` would also need orders 4, 5 and V4 shape
`fx2,0,0 F5`, all out of reach on this laptop (table above).
