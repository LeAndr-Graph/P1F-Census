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

Two of the 10,710 K18 classes fold to atomic Latin squares of order 17: **#9323**, the classical
AGL(1,17) factorization with |Aut| = 272, and **#9527**. Record **#10710** is the cyclic
(Skolem-starter) class with |Aut| = 17.

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

This locates MSBuild through `vswhere` and builds `P1F-Census.sln` in Release|x64, producing
`x64\Release\P1F-Census.exe`. Pass `nopause` to skip the closing prompt when scripting it.

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
| `runs/K20/K20Aut2`, `K20Aut2-Shard`, `K20Aut2-Dive` | three approaches to the K20 order-2 leg |

Runtimes span seconds to days; the K18 \|Aut\| = 2 strata and the K20 order-3 cell are the long
ones, and both are designed to be split across machines. Read the case's `ReadMe.md` first.

A case ends by comparing what it found against the catalogue, so a successful run prints
`Compare Ok` and a table of the classes by |Aut|.

### Overriding paths

Each `run.bat` derives the repository root from its own location, so the tree works anywhere and
needs no setup. Two variables override that when you need them to:

```bat
SET "P1F_ROOT=D:\somewhere\P1F-Census"
SET "P1F_EXE=D:\somewhere\P1F-Census\x64\Debug\P1F-Census.exe"
```

## Layout

```
source/       the engine: one solver per graph size, plus the driver
include/      shared headers
runs/         one folder per computation, grouped by graph
regression/   fast correctness suite with frozen expected output
tools/        Perl helpers: catalogue comparison, merging, canonical formatting
docs/         env_variables.txt -- every environment variable the engine reads
AllResults/   the catalogues
```

One tool has no data here: `tools/k1717_count.pl` works on the K17,17 bipartite catalogue, which
is not mirrored in this repository because of its size. Download it from
[10.5281/zenodo.22548329](https://doi.org/10.5281/zenodo.22548329) and pass it as the argument.

## Regression suite

```bat
regression\run_all.bat
```

Eight cases, seconds to minutes in total, each comparing a fresh run against frozen expected
output. Exit code 0 means all passed. Run it after any change to the engine.

## Configuration

The engine takes two positional arguments — `P1F-Census.exe [N] [kThreads]`, N one of 14, 16, 18,
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
