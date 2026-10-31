# `runs\K24\` -- the K24 cases

By the theorems at the end of this file, at K24 (N-1 = 23, 4 | N) the only surviving
legs are order 23 (type `23 1`), order 11 (type `11^2 1^2`) and S3.

**|Aut| > 3 is complete (2026-10-30):** 561 classes, `|Aut|` = {11:531, 22:21, 23:4, 253:4, 506:1},
catalog `AllResults\K24_P1F_aut_gt3.txt`. Every bat checks its result against it
(`compare_to_catalog.pl --n 24`). Order 3 (`|Aut| = 3`) is out of reach on this laptop.

| case | automorphism | classes | wall (8 threads) | status |
|---|---|---|---|---|
| `K24Aut23` | type `23 1` (1-rotational) | 9 `{23:4, 253:4, 506:1}` | ~2 s | done 2026-10-30; = Pike-Shalaby Table 1 (9 starters classes) |
| `K24Aut11` | type `11^2 1^2` | 557 `{11:531, 22:21, 253:4, 506:1}` | ~62 min | done 2026-10-30; includes the 22 even-starter classes (Pike-Shalaby Table 2) and 5 of the 9 above; the 4 with `\|Aut\| = 23` have no element of order 11 |
| `K24AutS3` | S3 shapes | 0 | ~34 min | done 2026-10-30; needs the GSYM prune (before it, shape `t2 d0 R3 f0` ~330 h) |

`results_2026-10-26\order23.txt` is the first probe of the order-23 leg (the same 9 classes).

---

## Why these legs are enough for `|Aut| > 3`

**Fixed-point lemma** (proof in [`runs\K22\ReadMe.md`](../K22/ReadMe.md)): every non-identity
automorphism, and so every power of one, fixes at most 2 vertices. Two parity rules add to it: a
fixed-point-free involution is impossible when 4 divides `N` (K24: yes), and for an odd prime `p`
not dividing `N-1` the number of `p`-cycles is even.

A group of order > 3 contains an element of prime order `p >= 5`, or one of C4, V4, C9, C3xC3, C6,
S3. At K24 (`N-1 = 23`):

| family | surviving types | why |
|---|---|---|
| prime `p >= 5` | `23 1`, `11^2 1^2` | 5, 7, 13, 17, 19 leave more than 2 fixed points |
| C4 | none | its square would fix exactly 2 points (a fixed-point-free involution is forbidden), forcing `4a = 22` |
| V4 | none | every Klein-four action on 24 points contains a fixed-point-free involution when 4 divides `N` |
| C9, C3xC3 | none | C9: its cube fixes at least 6; C3xC3: Burnside gives `(24 + 0)/9` orbits, not an integer |
| C6 | none | its cube fixes 2, so no 3-cycles; `6a + 2 = 24` has no solution |
| S3 | 3-cycles `3^8`, involution fixing 2 points | the only small-group leg |

So the K24 classes with `|Aut| > 3` are exactly those found by the legs 23, 11 and S3.
