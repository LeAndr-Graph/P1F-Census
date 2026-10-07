# K20 cases

| Case | What it does |
|---|---|
| [`K20Aut4-All`](K20Aut4-All/) | the K20 \|Aut\| > 3 classification, by group size |
| [`K20Aut4-NoSkip`](K20Aut4-NoSkip/) | the same result re-derived by search instead of by theorem |
| [`K20Aut3`](K20Aut3/) | the K20 order-3 cell, swept in 104 independent blocks — complete (2026-09-21) |

## What is open

Whether K20 has a perfect 1-factorization with automorphism group of order exactly 2 is open, and
no case here searches for one. Three strategies were tried and all returned nothing: a plain sweep
of the `2^9 1^2` cell, the same cell partitioned into 7,944 level-2 shards, and randomized
backtracking dives. The result is weak evidence at best, because the fraction of the space those
runs covered is unknown and cannot be measured with these tools: the tree-size estimator supports
only the enumerable path, and every order-2 type at K20 is over-cap by construction.
