# K20 cases

| Case | What it does |
|---|---|
| [`K20Aut4-All`](K20Aut4-All/) | the K20 \|Aut\| > 3 classification, by group size |
| [`K20Aut4-NoSkip`](K20Aut4-NoSkip/) | the same result re-derived by search instead of by theorem |
| [`K20Aut3`](K20Aut3/) | the K20 order-3 cell, swept in 104 independent blocks — complete (2026-09-21) |

## Restart

`K20Aut3` is split into 104 blocks (`BStart`, `BLast` in `run.bat`). A block is the unit:
an interrupted block has no restart point inside it, so delete its log and result and run that
block again; finished blocks keep their own files. `K20Aut4-All` and `K20Aut4-NoSkip` use the
block walk:

**Block walk.** Every 5 minutes the log's `~ so far` row ends with `Block restart #(%)` = k, the
lowest root block not finished. Move the old log and result file (`RESULT` in `run.bat`) aside, add
`SET "REP_BLOCKS=k-last"` to `run.bat`, and run it; the new result plus the old one,
merged by `tools/make_catalog.pl` (which removes duplicates), is the full answer. Blocks above
k that were already done are walked again. `REP_BLOCKS` applies to every leg and type of the
run, so in a run with several legs also set `REP_ORDERS` and `REP_ONLYTYPES` to the
interrupted one, and run the legs the log never reached as their own run. A run shorter than
5 minutes prints no restart row: run it again from the start.

## What is open

Whether K20 has a perfect 1-factorization with automorphism group of order exactly 2 is open, and
no case here searches for one. Three strategies were tried and all returned nothing: a plain sweep
of the `2^9 1^2` cell, the same cell partitioned into 7,944 level-2 shards, and randomized
backtracking dives. The result is weak evidence at best, because the fraction of the space those
runs covered is unknown and cannot be measured with these tools: the tree-size estimator supports
only the enumerable path, and every order-2 type at K20 is over-cap by construction.
