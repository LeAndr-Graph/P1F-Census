# `runs\K26\` -- the K26 cases

K26 is **not** classified by `|Aut|` here. What is complete is **per leg**: every K26 P1F class
with an automorphism of type `25 1` (1-rotational: one 25-cycle and a fixed vertex), and every class
with an automorphism of type `24 1^2` (one 24-cycle and two fixed vertices). Together they are
**59 classes**, `|Aut|` = {24:36, 25:23}, catalog `AllResults\K26_P1F_rot25_24.txt` (`p1f_check`
CLEAN, no atomic class). Each bat checks its result against it (`compare_to_catalog.pl --n 26`).

Both counts agree with Pike and Shalaby (JCMCC 44, 2003): 23 non-isomorphic P1Fs from starters in
Z_25 (their Table 1) and 36 from even starters in Z_24 (their Table 2).

| case | automorphism | classes | wall (8 threads) | status |
|---|---|---|---|---|
| `K26Aut25` | type `25 1` (1-rotational) | 23 `{25:23}` | ~5 min (setup) | done |
| `K26Aut24` | type `24 1^2` (`REP_ORDERS=24`, `REP_ONLYTYPES=1`) | 36 `{24:36}` | ~4 min (setup) | done |
| order 24 | type `24 2` | | | NOT SEARCHED: one root block, unfinished after 56 min on one thread |
| order 13 | type `13^2` | 0 in 3.3% of the blocks | ~13 h | NOT SEARCHED: 2,238,214 root blocks after 29 min of setup |

## Restart

Both K26 cases use the block walk.

**Block walk.** Every 5 minutes the log's `~ so far` row ends with `Block restart #(%)` = k, the
lowest root block not finished. Move the old log and result file (`RESULT` in `run.bat`) aside, add
`SET "REP_BLOCKS=k-last"` to `run.bat`, and run it; the new result plus the old one,
merged by `tools/make_catalog.pl` (which removes duplicates), is the full answer. Blocks above
k that were already done are walked again. `REP_BLOCKS` applies to every leg and type of the
run, so in a run with several legs also set `REP_ORDERS` and `REP_ONLYTYPES` to the
interrupted one, and run the legs the log never reached as their own run. A run shorter than
5 minutes prints no restart row: run it again from the start.

## Why there is no `|Aut|` statement

By the fixed-point lemma (proof in [`runs\K22\ReadMe.md`](../K22/ReadMe.md)) and the parity rules,
a K26 P1F with `|Aut| > 3` has an automorphism of prime type `5^5 1` or `13^2`, or contains C4
(`4^6 1^2`, `4^6 2`), V4, C6 (`6^4 2`, `6^4 1^2`) or S3 (`N-1 = 25`, `N = 2 mod 4`; 7, 11, 17,
19, 23 leave more than 2 fixed points, and C9 and C3xC3 cannot act). The type `25 1` lies inside
the order-5 leg `5^5 1`. Of these, only the two rotational types above are cheap on this laptop:
the order-5, order-4, V4, order-6 and S3 legs are not estimated yet, and `13^2` alone needs about
13 hours. So the catalog states exactly what was searched, leg by leg.
