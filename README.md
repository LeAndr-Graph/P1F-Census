# P1F-Census: Perfect 1-Factorizations of K18 and K20 with Non-Trivial Automorphisms

**K18:** there are exactly **10,710** pairwise non-isomorphic perfect 1-factorizations (P1Fs) of
K18 with a non-trivial automorphism group (|Aut| > 1).

| \|Aut\| | 2 | 3 | 4 | 8 | 16 | 17 | 272 |
|---|---:|---:|---:|---:|---:|---:|---:|
| classes | 10,179 | 351 | 144 | 22 | 12 | 1 | 1 |

Catalogue: [`AllResults/K18_P1F_aut_gt1.txt`](AllResults/K18_P1F_aut_gt1.txt) ·
dataset DOI [10.5281/zenodo.22238984](https://doi.org/10.5281/zenodo.22238984) ·
runs: [`runs/K18/`](runs/K18/) · paper: submitted to *Electron. J. Combin.*

**K20:** there are exactly **149,836** pairwise non-isomorphic P1Fs of K20 with |Aut| >= 3:
**230** with |Aut| > 3 and **149,606** with |Aut| = 3.

| \|Aut\| | 3 | 6 | 9 | 18 | 19 | 57 | 171 | 342 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| classes | 149,606 | 168 | 46 | 9 | 3 | 1 | 2 | 1 |

Catalogues: [`AllResults/K20_P1F_aut_gt3.txt`](AllResults/K20_P1F_aut_gt3.txt),
[`AllResults/K20_P1F_aut_eq3.txt.gz`](AllResults/K20_P1F_aut_eq3.txt.gz) ·
dataset DOIs [10.5281/zenodo.22215011](https://doi.org/10.5281/zenodo.22215011) (|Aut| > 3),
[10.5281/zenodo.22880728](https://doi.org/10.5281/zenodo.22880728) (|Aut| = 3) ·
runs: [`runs/K20/`](runs/K20/)

**Derived:** 104,088 P1Fs of K17,17 from the K18 catalogue,
[10.5281/zenodo.22548329](https://doi.org/10.5281/zenodo.22548329).

**Verification:** the same engine reproduces the known K14 classification (Dinitz and Garnick, 1996)
and the K16 classes with |Aut| > 1 (Gill and Wanless, 2020):
[`AllResults/K14_P1F_aut_gt1.txt`](AllResults/K14_P1F_aut_gt1.txt) (21),
[`AllResults/K16_P1F_aut_gt1.txt`](AllResults/K16_P1F_aut_gt1.txt) (89),
[`runs/K14/`](runs/K14/), [`runs/K16/`](runs/K16/), [`regression/`](regression/).

## Authors

Andrei V. Ivanov, ORCID [0000-0002-1574-6716](https://orcid.org/0000-0002-1574-6716), died September 2026.<br>
Leonid M. Tertitski, ORCID [0009-0002-7556-9552](https://orcid.org/0009-0002-7556-9552) (corresponding).

## Where the details are

| Topic | File |
|---|---|
| definitions, catalogue format, record numbering, atomic Latin squares | [`AllResults/ReadMe.md`](AllResults/ReadMe.md) |
| each computation: what it searches, expected result, runtime | `ReadMe.md` in each [`runs/`](runs/) folder |
| theorems the search relies on (Ihrig 1986) | [`docs/theorems.md`](docs/theorems.md) |
| build, configuration, regression suite | [`docs/build.md`](docs/build.md) |
| what is open (K20 \|Aut\| = 2) | [`runs/K20/ReadMe.md`](runs/K20/ReadMe.md) |

## Citing

Cite the dataset DOI for the graph in question, for example:

> Ivanov, A. V., & Tertitski, L. M. (2026). *Perfect one-factorizations of K18 with non-trivial
> automorphism group: a complete catalogue* (Version 1.0.0) [Data set]. Zenodo.
> https://doi.org/10.5281/zenodo.22238984

Questions, missing or extra classes, failed reproductions:
[open an issue](https://github.com/LeAndr-Graph/P1F-Census/issues).

## License

Source code: MIT ([`LICENSE`](LICENSE)). Catalogues in `AllResults/`: CC0 1.0.
