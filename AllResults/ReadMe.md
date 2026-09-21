# `AllResults\` -- the catalogs a finished run is checked against

This folder holds **one catalog per size**: K14, K16, K18 and K20. Every case in `runs\` finishes
by looking its own results up in the catalog for its N -- `compare_to_catalog.pl --n <N>` picks it
-- so a run tells you whether what it found is already known without anyone comparing files by hand.

Each is named for **what it holds**, because the scope differs by size and the name should not
have to be looked up:

| file | scope | why that scope |
|---|---|---|
| `K14_P1F_aut_gt1.txt` | `\|Aut\| > 1` | K14's order-2 leg finishes, so the classification is complete |
| `K16_P1F_aut_gt1.txt` | `\|Aut\| > 1` | since 2026-09-01: `runs\K16Aut2` harvests the 59 `\|Aut\| = 2` classes in 7 minutes |
| `K18_P1F_aut_gt1.txt` | `\|Aut\| > 1` | the `\|Aut\| = 2` half comes from the three block-driven census cases |
| `K20_P1F_aut_gt3.txt` | `\|Aut\| > 3` | the cells the theorems settle |
| `K20_P1F_aut_eq3.txt.gz` | `\|Aut\| = 3` | the one cell no theorem settles. Its census (`runs\K20\K20Aut3`, 104 blocks) **completed 2026-09-21**. Gzipped: the file is 206 MB, over GitHub's 100 MB limit |

K20 is the one size that takes two files. They are **disjoint** -- `= 3` and `> 3` -- so together
they are every K20 P1F class with `|Aut| >= 3`, and concatenating them needs no dedup. K20 has no
`aut_gt1` catalog because `|Aut| = 2` is still out of reach.

**All five are complete for the scope they state.** K18's `|Aut| = 2` half stopped growing on
2026-09-01, when the last open stratum -- type 9, `a = 4` -- finished and found nothing the
catalog did not already have.

`K16_P1F_aut_gt1.txt` replaced `K16_P1F_aut_gt2.txt` (30 classes) on 2026-09-01, and the old
file was deleted in the same commit: it is a strict subset of the new one, and one catalog per
size is the rule this folder keeps. It remains in git history if it is ever wanted.
`compare_to_catalog.pl --n 16` points at the new one, and both K16 cases check out against it.

Nothing in the repo reads this folder except that check, and no case writes to it: a run writes
its `RESULT` into its own `runs\<case>\` folder.

**The records are not edited.** A catalog is a curated document, produced once from the finished
case runs, and a class is added only when a run finds one the catalog does not have. Banners and
summary tables are prose and may be corrected; the records are not touched. All four are tracked in
git, and `.gitattributes` pins `AllResults/*.txt` to `-text` so their bytes survive the round trip
unchanged.

Until 2026-08-28 this folder also carried the raw output of three cases: `K18-t8-a0.txt` with
9,626 records, `K18-t9-a0.txt` with 727 and `K18-t9-a4.txt` with 6. Every class in all three is in
the catalog, verified with the compare step below immediately before they were removed. They
remain in git history. What they held that the catalog does not is per-case attribution, which leg
produced which class, and that is recoverable only by re-deriving it.

---

## 1. The files

| file | records | classes | aut |
|---|---|---|---|
| `K14_P1F_aut_gt1.txt` | 21 | 21 | `{2:3, 3:5, 4:1, 6:5, 12:5, 84:1, 156:1}` |
| `K16_P1F_aut_gt1.txt` | 89 | 89 | `{2:59, 3:19, 5:5, 7:4, 14:1, 15:1}` |
| `K18_P1F_aut_gt1.txt` | 10,710 | 10,710 | `{2:10179, 3:351, 4:144, 8:22, 16:12, 17:1, 272:1}` |
| `K20_P1F_aut_gt3.txt` | 230 | 230 | `{6:168, 9:46, 18:9, 19:3, 57:1, 171:2, 342:1}` |
| `K20_P1F_aut_eq3.txt.gz` | 149,606 | 149,606 | `{3:149606}` |

`K20_P1F_aut_eq3.txt.gz` unpacks to 205.9 MB; md5 of the gzip is
`c572399252609d5348288f6a9ad8926f`, of the file inside `68efe2ea579f43087aa087830b83fd0c`.

### The `|Aut| = 3` census

Every K20 P1F class whose automorphism group has order exactly 3. The search partitions the
`3^6.1^2` cell into 104 level-1 blocks and sweeps all of them; the last finished 2026-09-21. A
class sits in several blocks, so the 104 runs emit 582,900 records for 149,833 distinct classes
(mean multiplicity 3.9), of which 149,606 have `|Aut| = 3` and the other 227 are already in
`K20_P1F_aut_gt3.txt`.

Those 227 are the check on the whole sweep. They are exactly the `> 3` classes whose order is
divisible by 3, and they come back **identical, as the same canonical forms** -- 227 in both,
none in either alone. The three the sweep does not find are the `|Aut| = 19` classes, and 3 does
not divide 19.

**No atomic Latin square has `|Aut| = 3`.** Scanning all 149,606 classes gives zero, over
2,992,120 symbol-Hamiltonicity checks and no validation failure. Every atomic K20 square found so
far has `|Aut| > 3` and is in the other file.

**Two of them are published datasets with DOIs** (2026-09-01). Cite the *version* DOI wherever a
record number appears -- record numbers are positions within a version:

| catalog | version DOI | concept DOI |
|---|---|---|
| `K18_P1F_aut_gt1.txt` | `10.5281/zenodo.22238984` | `10.5281/zenodo.22238983` |
| `K20_P1F_aut_gt3.txt` | `10.5281/zenodo.22215011` | `10.5281/zenodo.22215010` |

The deposited copies are byte-identical to the files here -- md5
`2676a03f897bf09f2263246bfbd24f53` and `a415a57c6e0d7314b70dd5d69ebcd648`. If either file is ever
edited, the published record and this folder diverge, and the record numbers cited in the papers
stop matching; treat that as a reason not to edit them. Deposit paperwork is in `zenodo\`.

`K20_P1F_aut_eq3.txt.gz` is **not yet deposited**. When it is, the uncompressed file goes to
Zenodo rather than the gzip: 206 MB is nothing against Zenodo's 50 GB limit, and a plain text
file stays readable there without tooling. The gzip exists only because GitHub refuses the file
at full size.

The three smaller ones were assembled the same way -- dedup on the canonical form, sort on it,
emit with the header -- by a script kept outside this repository, K14 and K20 on 2026-08-30 from
a single run of their own `runs\` case, K16 on 2026-09-01 from two: `K16Aut3-All` for the 30 with
`|Aut| > 2` and `K16Aut2` for the 60 with an involution, which overlap in the one class of
`|Aut| = 14` -- 90 records read, 1 duplicate, 89 written. This repository publishes the answers
and the means to check them, not the bookkeeping that produced the archives.

They carry external corroboration that K18 has none of. K16's 89 is the published total number
of perfect one-factorizations of K16, and its `|Aut| > 2` part, 30, is the exact literature
count -- so 30 + 59 = 89 closes the size from both ends, and K16 has no class with a trivial
automorphism group. K14's 21 is 23 P1Fs less the 2 rigid ones; K20's 230 is its `|Aut| > 3`
census -- `|Aut| = 3` is a separate, unenumerated cell.

The merged K16 file was verified the same way as the deposited catalogues, on 2026-09-01: 89
records numbered contiguously with no duplicates; every row a perfect matching, every edge once,
and all 9,345 pairs of rows a single Hamiltonian cycle; and `|Aut|` recomputed from the matrices
alone with 0 mismatches, giving `{2:59, 3:19, 5:5, 7:4, 14:1, 15:1}` and every group cyclic.

The rest of this section is about `K18_P1F_aut_gt1.txt`, the one with a history.

Every K18 P1F class known to have `|Aut| > 1`, each in the canonical form produced by the project
canonizer, sorted in ascending lexicographic order of that form. **This is the reference
numbering**: catalog `#N` anywhere in this ReadMe, or in project notes, means the `#N` of this
file. Its headers run contiguously `#1 .. #10710`, so `#N` is simply the `N`-th record.

It carries a title, a total, an aut summary table and a note on the ordering, and it separates
records with a blank line. That is why it is a catalog and not a run's output: rewriting it into
run format would strip all of it.

It was assembled once, from the banks of the runs listed in section 1.1, by merging them into a
single canonizer input and then canonizing, deduplicating, sorting and emitting. Its inputs needed
that canonizing and merging first, which the other three did not. None of those steps is part of
this repository; the file itself is the record, and section 6 is how it was verified.

### 1.1 Where its 10,710 came from

    K18-t8-a0      9,626      type 2^8.1^2, a = 0
    K18-t9-a0        727      type 2^9, a = 0
    K18-t9-a4          6      type 2^9, a = 4 -- partial, blocks 0-2255
    shared            -1      catalog #1981, found by both (see 4.1)
    ---------------------
    with involution 10,358
    K18Aut3-All      +352     its other 179 classes are already in t8-a0
    ---------------------
    catalog         10,710

The 352 that only `K18Aut3-All` contributes are the 351 order-3 classes and the single order-17
class: neither has an involution, so no `|Aut| = 2` census can ever see them.

The `|Aut| = 2` half stands at 9,447 + 727 + 5 = **10,179 known classes**, and the type-9 `a = 4`
stratum is the only place that number can still grow.

---

## 2. Format

The engine emits the project canonical form, so a record a run writes and the same class in the
catalog are **byte-identical over their matrix rows**. That is what makes a plain lookup work.

    #<seq> |Aut| = <aut>
     "   0  1   2  3   4  5   6  7   8  9  10 11  12 13  14 15  16 17 "
     ... 17 rows total

A run writes records and nothing else: no header, no summary table, no blank lines between them.
`<seq>` counts **write order within one file** and restarts at 1 in every file; it is not a class
identifier. Records land in thread discovery order, so two correct runs of the same case routinely
disagree on `<seq>` alone.

**A class is identified by its canonical matrix, never by a sequence number.**

---

## 3. The compare step

Every case bat ends with

    perl ..\..\tools\compare_to_catalog.pl "%RESULT%"

which looks each record of the run up in the catalog by matrix and reports **Ok** or **Fault**
with a histogram over `|Aut|`. Ok prints one column, the results found. Fault prints two, present
and not present, and fails the run with exit code 3. A record whose matrix is in the catalog under
a *different* `|Aut|` is named separately, because that is a different kind of wrong from a class
the catalog has never seen.

A result file with **no results is Ok**. Most block ranges of a census own nothing and write
nothing, and that is the normal outcome.

### 3.1 What it does not check

Only **present or not present**. It cannot report a class the run should have produced and did
not: that needs the block each class belongs to and a reading of the run log, and is left for
later. What a full column must total is in each case's `ReadMe.md`.

### 3.2 Testing the compare step

`--against <file>` replaces the reference, so the two files can be switched and the check run the
other way round. That is a manual test of the script, not part of any case run.

Both directions were exercised on 2026-08-28 against the three case files, unedited, before they
were removed. Forward, all three Ok:

    perl tools/compare_to_catalog.pl AllResults/K18-t8-a0.txt      Ok, 9,626 of 9,626
    perl tools/compare_to_catalog.pl AllResults/K18-t9-a0.txt      Ok, 727 of 727
    perl tools/compare_to_catalog.pl AllResults/K18-t9-a4.txt      Ok, 6 of 6

Switched, the catalog looked up in the type-8 file:

    perl tools/compare_to_catalog.pl --against AllResults/K18-t8-a0.txt AllResults/K18_P1F_aut_gt1.txt

    Compare Fault: can't find 1084 of 10710 results of K18_P1F_aut_gt1.txt in K18-t8-a0.txt

         |Aut|   present   not present
             2      9447           732
             3         0           351
             4       144             0
             8        22             0
            16        12             0
            17         0             1
           272         1             0
         total      9626          1084

That histogram is itself a cross-check on the compare. The 732 are the 727 classes of
`K18-t9-a0` plus the 5 of `K18-t9-a4`; the remaining 352 are the order-3 and order-17 classes,
which have no involution and which a type-8 sweep can therefore never reach. Every group order the
type-8 file does own -- 4, 8, 16 and 272 -- shows zero absent.

---

## 4. Duplicates

Three different things get called duplicates. Only one of them would be a bug, and it does not
happen.

### 4.1 One class satisfying two legs

A class is found by a leg whenever its automorphism group contains what that leg prescribes, and a
class can satisfy two legs at once. Across the four cases this happens **exactly once**, at catalog
`#1981`, and for a structural reason: it is the **only class in the entire `|Aut| > 2` family whose
automorphism group contains a fixed-point-free (`2^9`) involution**, and it also contains a
`2^8.1^2` one, so both involution legs find it.

Every other `|Aut| > 2` class has either exactly one involution -- 177 of them, all `2^8.1^2`,
hence type-8 only -- or none at all, which is the 351 order-3 classes and the single order-17 one.

`K18-t8-a0` also shares 179 classes with `K18Aut3-All`: those are simply the type-8 sweep's
`|Aut| > 2` records. Since 2026-08-28 the census cases **reject** them, because the `|Aut| > 2`
case defines and writes them, so a fresh type-8 run writes 9,447 rather than 9,626. It reports the
179 instead as `duplicates aut{4:144 8:22 16:12 272:1}`, an independent re-derivation of that
census.

### 4.2 The same class twice inside one file

This happened in a **multi-order** case before 2026-08-28: the class set was cleared per
`REP_ORDERS` token, so a class found by two legs was written once per leg. `K18Aut3-All` wrote 533
records for 531 classes -- `#1981` through legs `4` and `V4`, and `#9323` with `|Aut| = 272`
through legs `4` and `17`. That is now rejected too, and the record count equals the class count.
The frozen regression baselines were re-cut with it: K14 35 to 21, K16 31 to 30, K20 243 to 230,
and the deduped counts are the known-correct censuses.

### 4.3 The same class in two ranges of one case

This does not happen. A block-driven case dedups by **ownership**: a class belongs to the lowest
block of its column, every block computes that the same way, and a block that re-finds a class
owned elsewhere writes nothing. Separately run ranges of one case are disjoint and simply
concatenate, with no merge and no dedup.

---

## 5. What is known, by catalog index

**Every `#N` here is a record number in `K18_P1F_aut_gt1.txt`**, the `N`-th record, whose headers
run `#1 .. #10710`.

`|Aut|` has been recomputed independently for **all 10,710 classes**, directly from the matrices in
the catalog, without reference to the search that produced them. **All 10,710 agree with the
recorded value**, and the recomputed spectrum is the `{2:10179, 3:351, 4:144, 8:22, 16:12, 17:1,
272:1}` of section 1. An earlier pass had covered only the 531 with `|Aut| > 2`; the full pass was
run 2026-09-01 (see 6).

### 5.1 The two atomic classes

From a P1F `F` and a vertex `j` one builds the symmetric idempotent Latin square `I(F,j)` of order
17. Because `F` is perfect, `I(F,j)` is atomic exactly when it is row-Hamiltonian. Testing every
class in the catalog against every `j`, **exactly two are atomic**:

| record | aut | atomic for | what it is |
|---|---|---|---|
| **`#9323`** | 272 | `j = 0` | the `AGL(1,17)` factorization; yields the classical **Desarguesian** square |
| **`#9527`** | 16 | `j = 0` | `Aut` is cyclic `C16`; **not** diagonally cyclic, since the class admits no automorphism of order 17 |

These are the only two, and each is atomic for one choice of `j` only. They are the two
even-starter P1Fs of K18 of Bryant, Maenhaut and Wanless (JCTA 113, 2006), confirmed by
reconstructing both from that paper and matching autoparatopism group orders 27744 and 96
respectively. `#9527` lies outside the known cyclotomic and diagonally cyclic constructions.

Re-measured 2026-09-01 with `tools/nu_latin.cpp` on the squares in `tools\atomic_sq_P139_AGL17.txt`
and `tools\atomic_sq_P155_Aut16.txt`: `#9323` gives `|Atop| = 4624 = 17 x 272` and `nu = 27744`,
`#9527` gives `|Atop| = 16` and `nu = 96`. Controls on the K20 squares return the published 6498 /
38988 and 171 / 342. Note `nu` is not `6 |Atop|` in general -- for K20 `#229` at `j = 7` only two
of the six conjugates contribute -- so `|Atop|` has to be read off the tool, not divided out.

The sweep covered all 10,710 entries, so none of the 10,179 currently known `|Aut| = 2` classes is
atomic either. That stratum is not complete, so this is a statement about what is known, not a
theorem; for the `|Aut| > 2` family it *is* exhaustive.

### 5.2 Previously known classes

The census recovers six previously known P1Fs of K18; the remaining 525 of the 531 are new. **All
six are now pinned to a catalog index**, by developing each published starter and looking the
result up (see 6.1 for the method):

| catalog | aut | starter | what it is |
|---|---|---|---|
| `#9323` | 272 | `P1F09` | `AGL(1,17)` (Anderson). 1-rotational, atomic. Project name **P139**. |
| `#9488` | 16 | `P1F18` | even starter. Project name **P148**. |
| `#9527` | 16 | `P1F23` | the second Bryant-Maenhaut-Wanless even starter, atomic. Project name **P155**. |
| `#9622` | 16 | `P1F20` | even starter. Project name **P175**. |
| `#9628` | 16 | `P1F21` | even starter. Project name **P178**. |
| `#10710` | 17 | `P1F01` | the cyclic factorization from a Skolem starter (Pike; Pike and Shalaby). 1-rotational. |

The starter column names the record of the `iso.18` list each one develops from. The project
names come from a separate 2026-07-27 identification of the same six, which reached the four
even-starter classes by matching automorphism structure rather than by canonical form. It
independently assigned `P1F23` to P155, and this lookup independently puts `P1F23` at `#9527`,
which is the record P155 names -- two routes, one answer.

`#9323` and `#10710` are the **only** two 1-rotational classes; the other 529 are not. Four of the
six have `|Aut| = 16`, so they are 4 of the catalog's 12 classes of that order; the other 8 are new.

### 5.3 `#1981` -- the P47 class

`|Aut| = 16`, and the only class in the `|Aut| > 2` family whose automorphism group contains a
fixed-point-free `2^9` involution: 3 involutions in total, one `2^8.1^2` and two `2^9`, against
exactly one for every other class in the family. That is what makes it the single cross-case
duplicate of 4.1 and the only class the `V4` leg contributes in 4.2.

It sits at block `4.0.2237` of the type-9 `a = 4` column and remains the positive control for that
leg. Since 2026-08-28 the census **rejects** it rather than writing it, so a run of that block
gives that block a row whose `Total saved(duplicates)` reads `0(1)`, and the footnote
`duplicates by |Aut| > 2: aut{16:1}`, which is the same proof that the search reached it. It is
**not** atomic.

---

## 6. How the numbers above were checked

* `|Aut|`: recomputed for **all 10,710 classes** from the catalog matrices alone (2026-09-01), by
  the frame method -- since the factorization is perfect the union of two factors is a Hamiltonian
  cycle, so an automorphism is pinned by the image of one frame; every candidate frame is tried and
  kept when it preserves the set of factors. **10,710 of 10,710 agree** with the recorded value, 0
  mismatches, and the recomputed spectrum matches section 1 exactly. The same enumeration gives the
  element orders, hence which groups are cyclic:

  | `\|Aut\|` | cyclic | non-cyclic | the non-cyclic ones |
  |---:|---:|---:|---|
  | 2 | 10,179 | 0 | |
  | 3 | 351 | 0 | |
  | 4 | 144 | 0 | (so `V4` never occurs as a full automorphism group) |
  | 8 | 17 | 5 | `#1259`, `#1260`, `#1266`, `#2260`, `#2288` |
  | 16 | 9 | 3 | `#1268`, `#1740`, `#1981` |
  | 17 | 1 | 0 | |
  | 272 | 0 | 1 | `#9323` = `AGL(1,17)` |

  Involution counts and cycle types come from the same enumeration.
* Structure of every record (2026-09-01): each of the 17 rows of each of the 10,710 records is a
  perfect matching on the 18 vertices; across the 17 rows each of the 153 edges occurs exactly
  once; and the union of every one of the 136 pairs of rows is a single Hamiltonian cycle --
  1,456,560 pairs tested, 0 failures. So every record is a perfect one-factorization. The records
  are numbered contiguously `#1 .. #10710` and no two are identical. The same check on
  `K20_P1F_aut_gt3.txt` passes over its 39,330 pairs, and its `|Aut|` recomputation agrees on all
  230 with the seven `C3 x C3` classes coming out as `#180`, `#190`, `#202`, `#204`, `#205`,
  `#213`, `#226`.
* Atomicity: `I(F,j)` built for all 10,710 classes and all 18 choices of `j`, tested for
  row-Hamiltonicity on every one of the 136 row pairs. Two classes survive, matching the two the
  paper identifies.
* Coverage: every record of the three case files matched against the catalog by matrix. All 10,359
  are in the catalog with the same `|Aut|`, 0 misses and 0 mismatches, re-verified on 2026-08-28
  with `tools\compare_to_catalog.pl` immediately before those files were removed.

### 6.1 How the six known classes were pinned (2026-08-28)

The literature gives those six as starters -- a short list of pairs developed into all 17 rounds by
adding a constant. Each was developed from its published starter into a matrix, collected in
`known.txt`, and then:

    REP_CANONFILE=known.txt RESULT=known_canon.txt p1f.exe 18 4
    perl tools/compare_to_catalog.pl --index known_canon.txt

The engine re-canonicalizes them first, because a matrix from any other source is in a different
labeling and matches nothing until it has been. `--index` reports which catalog record each one is.

Three independent checks agree, so this is not a bare assertion.

1. Development produced 6 valid factorizations of K18, 17 rounds and 153 distinct edges each.
2. The canonizer recomputed the automorphism groups from the matrices alone and returned
   `aut{16:4 17:1 272:1}`, matching the counts an unrelated script got in July.
3. The lookup **reproduced the three indices already on record** -- `#10710`, `#9323` and `#9527` --
   which were established earlier by a different route. That is the control, and it is what makes
   the other three trustworthy.

The three that were not previously indexed were each canonized and looked up **on their own** as
well, so the name-to-index assignment does not rest on record order being preserved through the
canonizer.
