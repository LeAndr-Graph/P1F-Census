# P1F-Census

Exhaustive, isomorph-free classification of **perfect one-factorizations** (P1Fs) of complete
graphs with non-trivial automorphism group, for K14, K16, K18 and K20 — the engine that produced
the published catalogues, the run scripts that reproduce them, and the catalogues themselves.

A *one-factorization* of K<sub>2n</sub> partitions the edges into 2n−1 perfect matchings
(*factors*). It is **perfect** when the union of every pair of distinct factors is a single
Hamiltonian cycle. Folding a P1F at a vertex gives a symmetric Latin square of order 2n−1, which
is *atomic* exactly when the factorization is perfect and the fold has no proper subsquare — so
this census also settles which atomic Latin squares of orders 17 and 19 arise this way.

## Results

Every class below is pairwise non-isomorphic, and each census is complete for its stated scope.

| Graph | Scope | Classes | Distribution of \|Aut\| |
|---|---|---:|---|
| K14 | \|Aut\| > 1 | 21 | 2:3, 3:5, 4:1, 6:5, 12:5, 84:1, 156:1 |
| K16 | \|Aut\| > 1 | 89 | 2:59, 3:19, 5:5, 7:4, 14:1, 15:1 |
| K18 | \|Aut\| > 1 | 10,710 | 2:10179, 3:351, 4:144, 8:22, 16:12, 17:1, 272:1 |
| K20 | \|Aut\| > 3 | 230 | 6:168, 9:46, 18:9, 19:3, 57:1, 171:2, 342:1 |

The catalogues are in [`AllResults/`](AllResults/), one file per graph, in the format every run of
this engine writes: a `#N |Aut| = k` header followed by the factor rows. Records are sorted
ascending by canonical form and numbered contiguously from 1, so **a record number is a position,
not a name** — new classes are appended, never inserted.

The K14 and K16 rows are not new: they reproduce the classifications of Dinitz and Garnick (K14)
and of Gill and Wanless (K16, 3,155 classes in all), and serve as the engine's external check.

Two of the 10,710 K18 classes fold to atomic Latin squares of order 17, both previously known:
**#9323**, the classical AGL(1,17) factorization with |Aut| = 272, and **#9527**, the even-starter
square of Bryant, Maenhaut and Wanless (J. Combin. Theory Ser. A 113 (2006) 608–624). Record
**#10710** is the cyclic (Skolem-starter) class with |Aut| = 17.

### Theorems the search relies on

The engine skips cells that a theorem proves empty, and the case ReadMes refer to these results
by the names below. None of them is ours: all are due to E. C. Ihrig, *Symmetry groups related to
the construction of perfect one factorizations of K<sub>2n</sub>*, J. Combin. Theory Ser. B 40
(1986) 121–151, and several were generalized by Duncan and Ihrig, Rocky Mountain J. Math. 30
(2000) 529–553.

| Name used here | Statement | Ihrig 1986 |
|---|---|---|
| involution fixed-point lemma | an involutory automorphism fixes 0 or 2 vertices | Lemma 3.5 (from Thm 3.3) |
| order-2 parity theorem | a fixed-point-free involution needs 2n ≡ 2 (mod 4) | Cor 3.9 |
| order-4 parity theorem | an automorphism of order divisible by 4 needs 2n ≡ 2 (mod 4) | immediate from Lemma 3.5 and Cor 3.9 |
| Klein-four theorem | for 2n ≡ 0 (mod 4), no Klein four-group of automorphisms | Thm 6.2(b) |
| odd-prime parity theorem | an automorphism of odd prime order p with an odd number of p-cycles needs p \| 2n−1 | Thm 3.3 + Cor 3.4(b) |
| dichotomy theorem | a fixed-point-free involution (n odd) fixes 1 factor, containing all n of its transposition edges, or n factors, each containing exactly one | Thm 3.11 + 3.13(b) |
| two-fixed-points theorem | an involution with 2 fixed points fixes exactly one factor: its transposition edges plus the edge joining the fixed points | Thm 3.10 + 3.13(a) |

`runs/K20/K20Aut4-NoSkip` re-derives the K20 result by search instead of relying on these.

### Archived versions

The K18 and K20 catalogues, and a derived K17,17 bipartite catalogue, are deposited with DOIs.
Cite the **version** DOI whenever a record number is involved:

| Dataset | Version DOI |
|---|---|
| K18, \|Aut\| > 1 — complete catalogue | [10.5281/zenodo.22238984](https://doi.org/10.5281/zenodo.22238984) |
| K20, \|Aut\| > 3 — complete catalogue | [10.5281/zenodo.22215011](https://doi.org/10.5281/zenodo.22215011) |
| K17,17 perfect 1-factorizations, from the K18 catalogue | [10.5281/zenodo.22548329](https://doi.org/10.5281/zenodo.22548329) |

## Build

Windows, Visual Studio 2022 or later, x64. No external dependencies.

```bat
build.bat
```

This locates MSBuild through `vswhere` and builds `p1f.sln` in Release|x64, producing
`x64\Release\p1f.exe`. Pass `nopause` to skip the closing prompt when scripting it.

## Reproducing a result

Each folder under [`runs/`](runs/) is one computation: a `run.bat` that sets the environment and
starts the engine, and a `ReadMe.md` explaining what it searches, what it should find and roughly
how long it takes. Run one by executing its `run.bat` — nothing else needs configuring.

| Case | What it does |
|---|---|
| `runs/K14/K14Aut2-All` | the full K14 \|Aut\| > 1 classification |
| `runs/K14/K14-t6-a0`, `K14-t7-a3` | independent checks of the search engines used for K18 |
| `runs/K16/K16Aut2` | the K16 order-2 leg: every class with an involution |
| `runs/K16/K16Aut3-All` | the full K16 \|Aut\| > 2 classification |
| `runs/K18/K18Aut3-All` | the full K18 \|Aut\| > 2 classification |
| `runs/K18/K18-t8-a0`, `K18-t9-a0`, `K18-t9-a4` | the three K18 \|Aut\| = 2 strata |
| `runs/K20/K20Aut4-All` | the K20 \|Aut\| > 3 classification, by group size |
| `runs/K20/K20Aut4-NoSkip` | the same result re-derived by search instead of by theorem |
| `runs/K20/K20Aut3` | the K20 order-3 cell, swept in 104 independent blocks |

Runtimes span seconds to days; the K18 \|Aut\| = 2 strata and the K20 order-3 cell are the long
ones, and both are designed to be split across machines. Read the case's `ReadMe.md` first.

A case ends by comparing what it found against the catalogue, so a successful run prints
`Compare Ok` and a table of the classes by |Aut|.

### What is not here

Whether K20 has a perfect 1-factorization with automorphism group of order exactly 2 is open, and
no case here searches for one. Three strategies were tried and all returned nothing: a plain sweep
of the `2^9 1^2` cell, the same cell partitioned into 7,944 level-2 shards, and randomized
backtracking dives. The result is weak evidence at best, because the fraction of the space those
runs covered is unknown and cannot be measured with these tools: the tree-size estimator supports
only the enumerable path, and every order-2 type at K20 is over-cap by construction.

### Overriding paths

Each `run.bat` derives the repository root from its own location, so the tree works anywhere and
needs no setup. Two variables override that when you need them to:

```bat
SET "P1F_ROOT=D:\somewhere\P1F-Census"
SET "P1F_EXE=D:\somewhere\P1F-Census\x64\Debug\p1f.exe"
```

## Layout

```
source/       the engine: one solver per graph size, plus the driver
include/      shared headers
runs/         one folder per computation, grouped by graph
regression/   fast correctness suite with frozen expected output
tools/        the check script: a run's results looked up in the catalogue for its N
docs/         env_variables.txt -- every environment variable the engine reads
AllResults/   the catalogues
```

## Regression suite

```bat
regression\run_all.bat
```

Eight cases, seconds to minutes in total, each comparing a fresh run against frozen expected
output. Exit code 0 means all passed. Run it after any change to the engine.

## Configuration

The engine takes two positional arguments — `p1f.exe [N] [kThreads]`, N one of 14, 16, 18,
20 — and reads everything else from environment variables. All of them are documented in
[`docs/env_variables.txt`](docs/env_variables.txt); the `run.bat` files are the maintained
examples of how they combine.

## Citing

If you use the catalogues, cite the dataset DOI for the graph in question, for example:

> Ivanov, A. V., & Tertitski, L. M. (2026). *Perfect one-factorizations of K18 with non-trivial
> automorphism group: a complete catalogue* (Version 1.0.0) [Data set]. Zenodo.
> https://doi.org/10.5281/zenodo.22238984

## Authors

Andrei V. Ivanov ([0000-0002-1574-6716](https://orcid.org/0000-0002-1574-6716)) and
Leonid M. Tertitski ([0009-0002-7556-9552](https://orcid.org/0009-0002-7556-9552)).

## License

Source code: MIT, see [`LICENSE`](LICENSE). The catalogue files under `AllResults/` are data, not
software, and are released under CC0 1.0, matching the Zenodo deposits they mirror.
