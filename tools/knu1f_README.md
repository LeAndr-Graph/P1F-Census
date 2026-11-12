# knu1f — exhaustive search for uniform 1-factorisations of K_N

`tools/knu1f.cpp`, C++17, standard library only (AVX2 for the type test); every even N from 4
to 32. **Formerly `k18u1f`** (renamed 2026-11-11): Zenodo v1.2.0 and the submitted U1F paper
cite `tools/k18u1f.cpp`, which is this program; see the git history before the rename.

A 1-factorisation is **uniform** of a cycle type T when every two of its factors form T
(T = "4.14" means a 4-cycle and a 14-cycle); **perfect** is the Hamiltonian type. A **partial**
is a set of factors ("rows"), every two of which form T.

## Build

```
tools\build_knu1f.bat         -> tools\knu1f.exe    (MSVC: cl /O2 /arch:AVX2 /std:c++17)
```

Never rebuild while a run uses the exe (Windows locks it, and a run must not change binary).

## How it works (one paragraph)

Any two factors of the type can be relabelled to one **standard pair** (R1, R2), so R1 and R2
are fixed and only the explicit group of that pair is used; there is no isomorphism test during
the search. Every other row is named by the partner of vertex 0 in it; the candidates are all
matchings that form T with R1 and R2, grouped by that partner. The roots (one per orbit of the
pair's symmetries) are independent **jobs**. Per job, the candidates compatible with the root and
their pairwise compatibility are kept as a bit matrix; a depth-first search adds one row at a
time from the group with the fewest live candidates and cuts a branch as soon as too few groups
are left. Classes are written in canonical form with |Aut|, so shard files merge exactly.

## Modes

| Command | What it does | What the answer proves |
|---|---|---|
| `knu1f N T` | full search for N-1 rows (uses the pair symmetry) | 0 classes = no uniform 1-factorisation of type T |
| `knu1f N T rows [threads] [hold]` | counts labelled partials with `rows` rows containing (R1, R2); no symmetry | exact counts (validation) |
| `knu1f N T max [threads]` | largest number of rows any partial reaches, then all classes at that size | maximum partial size + all its classes |
| `knu1f N T max threads K first last` | one shard: the K-row pass for root jobs [first, last) | all K-row classes rooted in the shard |
| `knu1f N T extend classes.txt [threads]` | per class, the number of matchings compatible with all its rows | 0 = class cannot take one more row |
| `knu1f N T continue threads f1 [f2 ...]` | completes each class (starters) to N-1 rows; reads `.txt` and `.part` | no class completes = no U1F through these starters |
| `knu1f merge out.txt in1.txt ...` | union of shard files, as one run writes it | — |

Default threads: 8.

## Environment options

Search restrictions (each changes what is searched — read the "proves" column):

| Variable | Effect | Proves |
|---|---|---|
| `U1F_CYCLE4FIRST=1` | keep only candidates with vertex 0 on a 4-cycle with R1 (types with a 4-cycle); orbits use only symmetries fixing 0, R1, R2; files tagged `_c4first` | **subset only**: a find is real, 0 proves nothing about the full type |
| `U1F_SMALLROWS=1` (max shard) | each job keeps only its K-3 rows with the fewest root-compatible candidates; matrix ~ (K/N)^2; stops a job at its first find; prints `HIT job j` | job without HIT has no U1F (every U1F has a row in each of those groups) |
| `U1F_SMALLKEEP=1` (with SMALLROWS) | do not stop at the first find: keep every K-row class (canonical hash only), appended to `<file>.part`; finished jobs to `<file>.done`; **resumable** | the kept classes are the starters for `continue` |
| `U1F_FIXEDROWS=1` (rows < N-1) | whole run on the rows-2 partner groups with the fewest candidates; roots = all of the smallest group; stops at first find | 0 = no U1F of type T; a find proves nothing |
| `U1F_JOBS="first last"` (with FIXEDROWS) | run only root jobs [first, last) | — |

Output and diagnostics:

| Variable | Effect |
|---|---|
| `U1F_QUIET=1` | no "k task exit ok" at the end (shards) |
| `U1F_THREADLINES=1` | per-thread lines under each progress line: job, branch x of n, deepest rows |
| `U1F_BUILDONLY=1` | build each job's matrix, print its size and time, skip the search |
| `U1F_POOLSTAT=s` | per job, kept list and pool sizes for s sampled candidates; skip the search |
| `U1F_CHECKPOW=1` | run the power test and the cycle walk on every pair; stop at the first difference |
| `U1F_WALK=1` | use the cycle walk instead of the power test |
| `U1F_NOEDGE=1` | no edge-overlap prefilter (only with the walk) |
| `U1F_LOCAL=n` | subtree size for the cache-sized local matrix (default 4096; 0 = off) |

Compile-time: `/D U1F_ALIGNED_AND=1` (aligned AVX2 AND in the local matrix; measured no gain).

## Output files

| File | Written by |
|---|---|
| `K<N>_U1F_<T>.txt`, `K<N>_U1F_deepest_<T>.txt` | full search: classes found; deepest partials held |
| `K<N>_U1F_max_<T>[_c4first].txt`, `..._k<K>_<first>-<last>.txt` | max pass; one max shard |
| `K<N>_U1F_small_<T>[_c4first]_k<K>_<first>-<last>.txt` (+ `.part`, `.done`) | SMALLROWS shard (classes with SMALLKEEP) |
| `K<N>_U1F_continued_<T>.txt` | continue: every completed uniform 1-factorisation |

Class file format: `=== type T: n class(es) with R rows`, then per class `#i |Aut| = a` and one
line per row as vertex pairs `v w` with v < w. `.part` format: one class per line, `aut` then R
rows of N partners each.

Progress lines (every minute): `t s: done of total jobs, found f; rows 3..K: n3 ... nK` — the
branches that reached each number of rows (live, including running jobs); with SMALLKEEP also
`classes recorded: c`. The RESULT line counts finished jobs only.

## Restart

- Max/SMALLROWS shards: a shard whose `.txt` exists is skipped by the run scripts.
- SMALLKEEP shards resume inside the shard: run the same command again; it prints
  `resumed: c classes, j jobs done before`, skips finished jobs and redoes the interrupted ones
  (classes found again are not recorded twice). Tested: kill + resume gives a byte-identical file.

## Examples

```
knu1f 18 4.14                                   K18 type 4.14, full search (0 = none)
knu1f 18 4.14 max 8                             largest partials of 4.14 (11 rows, 707 classes)
set U1F_CYCLE4FIRST=1 & set U1F_SMALLROWS=1 & set U1F_SMALLKEEP=1
knu1f 20 4.16 max 6 11 1 1001                   K20 4.16 subset, 11-row starters of jobs 1-1000
knu1f 20 4.16 continue 6 K20_U1F_small_*.txt    second pass: complete the starters to 19 rows
```

Run scripts: `runs/K18/K18U1F/`, `runs/K20/K20U1F/` (e.g. `run_4.16c4_small11.bat`, resumable).

## Validation

- Known answers for 2n <= 16 (one class each for K8 4.4, K10 4.6, K12 6.6, K16 4.4.4.4; none
  for the other K14/K16 types).
- K18: all seven non-Hamiltonian types give 0 (first found with an independent canonizer-based
  program).
- Power test = cycle walk (U1F_CHECKPOW) on the full searches of K18 4.4.4.6, 4.4.10, 4.6.8,
  6.6.6, 8.10.
- `continue` reproduces the known K20 4.6.10 U1F (|Aut| 171) from its first 11 rows.
- Labelled counts (rows mode) agree with classes and |Aut| for K16 4.4.4.4 and K18 4.4.4.6.
