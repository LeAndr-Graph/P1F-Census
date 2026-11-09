# K18U1F

**No uniform 1-factorization of K18 has a non-Hamiltonian cycle type.** The seven such types are
4.4.4.6, 4.4.10, 4.6.8, 4.14, 6.6.6, 6.12 and 8.10; every search below finds nothing.

`run.bat` builds `tools/k18u1f.cpp` if needed and runs all seven types. The program reads no input
file and uses no canonizer. Times are for 8 threads on a laptop.

| type | candidates | root jobs | found | time |
|---|---:|---:|---:|---:|
| 4.4.4.6 | 700 | 3 | 0 | 0.2 s |
| 4.4.10 | 13,580 | 30 | 0 | 0.4 s |
| 4.6.8 | 26,728 | 35 | 0 | 0.4 s |
| 6.6.6 | 2,080 | 8 | 0 | 0.3 s |
| 8.10 | 155,440 | 309 | 0 | 118 s |
| 6.12 | 191,584 | 460 | 0 | 251 s |
| 4.14 | 320,192 | 894 | 0 | 63.5 min |

## The search

Any two factors of a uniform factorization can be relabelled to one standard pair for the type, so
rows 1 and 2 are fixed. Every matching compatible with both is listed, grouped by the partner of
vertex 0. The row with the fewest orbits under the symmetries of the fixed pair is the root row, and
it is tried once per orbit. For each root, a depth-first search picks one compatible matching per
remaining row, always branching on the row with the fewest candidates.

## Controls

The same program, `k18u1f N type`:

- K16 4.4.4.4 finds a factorization, as known.
- K16 4.12, 6.10, 8.8, 4.4.8, 4.6.6 and K14 4.10, 6.8, 4.4.6 find nothing.
