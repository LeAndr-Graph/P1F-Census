# Building and running P1F-Census

## Build

Windows x64, a CPU with **AVX2**, and **Visual Studio 2026** (Community is fine) with the
*Desktop development with C++* workload — the project uses its toolset, `v145`. The code is C++20
and uses MSVC intrinsics, so it does not build with MinGW or clang as-is. No external libraries.

```bat
build.bat
```

This locates MSBuild through `vswhere` and builds `p1f.sln` in Release|x64, producing
`x64\Release\p1f.exe`. Pass `nopause` to skip the closing prompt when scripting it.

**Visual Studio 2022** also works, with its own toolset `v143`: from a VS 2022 Developer Command
Prompt run

```bat
MSBuild p1f.sln -p:Configuration=Release -p:Platform=x64 -p:PlatformToolset=v143
```

Both builds pass the regression suite.

**Perl** is needed to run the cases: each `run.bat` ends by checking its results with a Perl
script. Any `perl` on `PATH` works, as do [Strawberry Perl](https://strawberryperl.com/) and the
one bundled with [Git for Windows](https://gitforwindows.org/). Without it the search still runs
and writes its results; only the check at the end fails.

To run `p1f.exe` on a machine without Visual Studio, install the current *Microsoft Visual C++
Redistributable* (x64).

## Reproducing a result

Each folder under [`runs/`](../runs/) is one computation: a `run.bat` that sets the environment and
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
| `runs/K20/K20Aut3` | the K20 order-3 cell, swept in 104 independent blocks — **complete** (2026-09-21); its 149,606 \|Aut\| = 3 classes are `AllResults/K20_P1F_aut_eq3.txt.gz` |

Runtimes span seconds to days; the K18 \|Aut\| = 2 strata and the K20 order-3 cell are the long
ones, and both are designed to be split across machines. Read the case's `ReadMe.md` first.

A case ends by comparing what it found against the catalogue, so a successful run prints
`Compare Ok` and a table of the classes by |Aut|.

## Overriding paths

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
docs/         this file, theorems.md, env_variables.txt
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
[`env_variables.txt`](env_variables.txt); the `run.bat` files are the maintained
examples of how they combine.
