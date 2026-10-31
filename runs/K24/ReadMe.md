# `runs\K24\` -- the K24 cases

By the theorems at the end of this file, at K24 (N-1 = 23, 4 | N) the only surviving
legs are order 23 (type `23 1`), order 11 (type `11^2 1^2`) and S3.

**|Aut| > 3 is complete:** 561 classes, `|Aut|` = {11:531, 22:21, 23:4, 253:4, 506:1},
catalog `AllResults\K24_P1F_aut_gt3.txt`. Every bat checks its result against it
(`compare_to_catalog.pl --n 24`). Order 3 (`|Aut| = 3`) is out of reach on this laptop.

| case | automorphism | classes | wall (8 threads) | status |
|---|---|---|---|---|
| `K24Aut23` | type `23 1` (1-rotational) | 9 `{23:4, 253:4, 506:1}` | ~2 s | done; = Pike-Shalaby Table 1 (9 starters classes) |
| `K24Aut11` | type `11^2 1^2` | 557 `{11:531, 22:21, 253:4, 506:1}` | ~62 min | done; includes the 22 even-starter classes (Pike-Shalaby Table 2) and 5 of the 9 above; the 4 with `\|Aut\| = 23` have no element of order 11 |
| `K24AutS3` | S3 shapes | 0 | ~34 min | done; needs the GSYM prune (before it, shape `t2 d0 R3 f0` ~330 h) |

## Restart

All K24 cases use the block walk.

**Block walk.** Every 5 minutes the log's `~ so far` row ends with `Block restart #(%)` = k, the
lowest root block not finished. Move the old log and result file (`RESULT` in `run.bat`) aside, add
`SET "REP_BLOCKS=k-last"` to `run.bat`, and run it; the new result plus the old one,
merged by `tools/make_catalog.pl` (which removes duplicates), is the full answer. Blocks above
k that were already done are walked again. `REP_BLOCKS` applies to every leg and type of the
run, so in a run with several legs also set `REP_ORDERS` and `REP_ONLYTYPES` to the
interrupted one, and run the legs the log never reached as their own run. A run shorter than
5 minutes prints no restart row: run it again from the start.

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
