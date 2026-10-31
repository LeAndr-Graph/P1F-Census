# tools -- checking a result file

Two standalone C++ checks. Neither links the engine or any DLL: one `cl` line each, standard
library only, so they build anywhere the compiler does.

```
build_check.bat    -> p1f_check.exe   <result.txt> [--quiet]
build_atomcat.bat  -> atomcat.exe     <result.txt>
```

## p1f_check -- is the file what it says it is

Reads every record and **recomputes** from the matrix: that each row is a 1-factor, that every pair
of rows is a single Hamiltonian cycle (i.e. the factorization really is perfect), and the order of
the automorphism group. It then compares that to the `|Aut|` the engine wrote, and canonizes to find
records that appear more than once.

```
121006 records read, 0 malformed row(s)
valid P1Fs 121006, invalid 0, |Aut| mismatches 0, distinct classes 121006,
classes recorded more than once 0 (0 extra records)
recomputed |Aut| histogram: 3:120873 6:99 9:29 18:3 171:2
CLEAN
```

Independent of the run that produced the file: it agrees with the log's `= TOTAL` row only if both
are right. Slow on a large file -- the canonization is the cost -- so run it in the background.

## atomcat -- which classes fold to an ATOMIC Latin square

For each class and each vertex `j`, builds the Gill-Wanless fold `I(F,j)` and tests row-Hamiltonicity.
Prints the record number, `|Aut|`, and the vertices at which the fold is atomic.

```
   #9323  |Aut|=272  j=[0]            <- AGL(1,17)
   #9527  |Aut|=16   j=[0]            <- BMW P155
```

`VALIDATION failures` must be 0: the fold of a perfect factorization is symbol-Hamiltonian by
construction, so any failure means the input is not what it claims and nothing below it is
meaningful.

## The chain for a new block batch

```
p1f_check  result.txt --quiet                          # genuine P1Fs, correct |Aut|, no duplicates
atomcat    result.txt                                  # atomic classes, and at which vertices
perl services/compare_to_catalog.pl --n 20 result.txt  # known or new
```

Reading the third one for a K20 order-3 batch: `AllResults/K20_P1F_aut_gt3.txt` has scope
`|Aut| > 3`, so every `|Aut| = 3` class is reported `not present` and the verdict line says
**Fault** on a perfectly correct run. Read the `|Aut| > 3` rows -- those are the ones the catalogue
can speak about.

To decide whether an atomic hit is new, compare it against the catalogue's own atomic classes:
`atomcat AllResults/K20_P1F_aut_gt3.txt` gives #114 (342), #128 (18), #228 (57, at j=17 only) and
#229 (171); the K18 catalogue gives #9323 and #9527. A hit matching one of those on `|Aut|` and on
the set of atomic vertices is a re-find.
