# K18U1F

**No uniform 1-factorization of K18 has a non-Hamiltonian cycle type.** The seven such types are
4.4.4.6, 4.4.10, 4.6.8, 4.14, 6.6.6, 6.12 and 8.10; every search below finds nothing.

`run.bat` builds `tools/k18u1f.cpp` if needed, runs all seven types and checks the result. The
program reads no input file and uses no canonizer. Times are for 8 threads on a laptop (Intel Core
i7-12700H).

| type | candidates | root jobs | found | deepest reached | time |
|---|---:|---:|---:|---:|---:|
| 4.4.4.6 | 700 | 3 | 0 | 3 | 0.1 s |
| 4.4.10 | 13,580 | 30 | 0 | 5 | 1.3 s |
| 4.6.8 | 26,728 | 35 | 0 | 5 | 0.3 s |
| 6.6.6 | 2,080 | 8 | 0 | 4 | 0.1 s |
| 8.10 | 155,440 | 309 | 0 | 12 | 101 s |
| 6.12 | 191,584 | 460 | 0 | 7 | 235 s |
| 4.14 | 320,192 | 894 | 0 | 8 | 40 min |

**Deepest reached** is the largest number of rows any branch of this search held before it was cut.
It measures the pruning, not the mathematics: a branch is cut as soon as any unfilled row has no
candidate left, so a better prune gives a smaller number, and the true largest partials (next section)
are larger. The search keeps up to 1000 classes at that depth in `K18_U1F_deepest_<type>.txt`; the
fifth argument of `k18u1f` changes the limit. `run.bat` checks both the zero and this depth.

## The search

Any two factors of a uniform factorization can be relabelled to one standard pair for the type, so
rows 1 and 2 are fixed. Every matching compatible with both is listed, grouped by the partner of
vertex 0. The row with the fewest orbits under the symmetries of the fixed pair is the root row, and
it is tried once per orbit. For each root, a depth-first search picks one compatible matching per
remaining row, always branching on the row with the fewest candidates.

## Controls

`run_controls.bat` runs known answers: one class each for K8 4.4, K10 4.6, K12 6.6 and K16 4.4.4.4,
and nothing for K16 4.12, 6.10, 8.8, 4.4.8, 4.6.6 and K14 4.10, 6.8, 4.4.6.

## Largest partial factorizations

A partial with k rows is k pairwise edge-disjoint perfect matchings of K18, every two of which
have the cycle type. `run_max.bat` runs `k18u1f 18 <type> max` per type and checks each output
byte for byte against `AllResults/K18_U1F_max_<type>.txt`, one record per isomorphism class with |Aut|.

| type | largest k | classes | time |
|---|---:|---:|---:|
| 4.4.4.6 | 7 | 5 | 1 s |
| 4.6.8 | 7 | 239 | 3 s |
| 4.4.10 | 9 | 1 | 1 s |
| 6.6.6 | 15 | 2 | 1 s |
| 8.10 | 15 | 2 | 5 min |
| 6.12 | 10 | 15 | 24 min |
| 4.14 | 11 or more | not finished | |

**Long runs.** `run_max_shards.bat TYPE K FIRST LAST STEP` runs the K-row pass in shards of STEP root
jobs, one process each, and merges them when all are done. It can be stopped at any time: run the
same command again and finished shards are skipped. For 4.14:

    run_max_shards.bat 4.14 12 0 3443 20

Any class in the merged file means 12 rows exist; then run K = 13 the same way. If K = 12 gives
none, the largest is 11, and the same command with K = 11 collects all classes at 11 rows. Shards
of a finished type merge to exactly its file in `AllResults`.
