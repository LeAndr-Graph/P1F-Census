# Test-result log spec -- the run log is a TABLE

STATUS: IMPLEMENTED 2026-08-30 (agreed the same day), then REVISED the same day after reading
the first real output. The revisions are folded into the text below, and were:

- `P1F: env ...` became `P1F: Requested ...` (section 1c).
- A rule of `-` above every column-name row and below the `=` row (section 2e).
- `Duplicates` and `Saved` merged into ONE column, counts and histograms both (2e, 2f), and
  `Results` dropped entirely -- it was their sum, and the merged cell shows both halves.
- Columns separated by ` | `, none before the first or after the last (section 2e).
- `P1F: done, N result(s)` became `P1F: N result(s)` (section 5).
- `Elapsed` and the merged column became CUMULATIVE on data rows while nodes stayed per-unit,
  and the columns were renamed to say which is which -- `Leg nodes (rate)` /
  `Block nodes (rate)`, `Total saved(duplicates)` (2c, 2e). The `=` row gets its own header
  naming what IT holds: `Total nodes (average)` and plain `Saved`.
- The periodic `~` row is column set A only (2d). Column set B's rows already carry the
  cumulative figures every 30-100 s, so it had nothing left to add.

Where the code differs from the text below, the code is:

- The header block's line 1 (`<CASE>: EXE=... LOG=...`) is printed by `run.bat` to the CONSOLE
  and is NOT in the `.log` file, which the .bat writes from the exe's output alone. Lines 2 and
  3 are in both. Nothing was changed to "fix" this: the split in section 1 is about who knows
  what, not about where the line lands.
- Column widths are the implementation's -- section 2e fixes the RULES, not the numbers. Set A
  is Symmetry 8, Type 14, Elapsed 7, nodes 22, saved(duplicates) unpadded; set B is Block 6,
  Elapsed 7, nodes 22, saved(duplicates) 26, Done% 5. The data-row and `=`-row name arrays
  share those widths so the two headers line up over the same columns.
- Section 2 says the table is the only content between header and closing lines. The `[OWNER]`
  rejection lines survive inside it: they say WHICH KIND of duplicate a rejection was, which no
  column carries, and `regression\K18-t9-a4-owner-1` tests for them.
- The `=` TOTAL row of column set A is emitted by a new `finishRepTable()`, called after the
  REP_ORDERS loop. `runRepresentativeMethod` runs once per TOKEN, so its own end would have
  printed one TOTAL per token and no total for the run.
- Only the skipped types the engine actually reaches get a `skipped` row. Types dropped earlier
  by a theorem or by `REP_ONLYTYPES` produce no row, as before; the old `[N of M types skipped]`
  note went with the rest of the per-order lines, and why a type is skipped is in the test's
  ReadMe, per section 3.
- The type string is power notation with the fixed points kept -- `2⁷`, `2⁶ 1²`, `3⁵ 1`, `9² 1²`
  -- and the superscripts are real UTF-8, in the `.log` as well as on screen.

  CORRECTION, 2026-08-30: an earlier draft of this line said the difference between
  `2 2 2 2 2 2 2` and `2^7` was terminal-vs-pipe rendering. It was not. Two engines built the
  string two ways: k18a2rep compacted it to powers, while k14/k16/k20 printed one number per
  cycle AND DROPPED THE FIXED POINTS -- `3 3 3 3 3` was all a K16 reader saw of `3⁵ 1`, and
  `9 9` all a K20 reader saw of `9² 1²`. The k18 form is now in all four.

  Superscripts are a second, separate thing, and they were suppressed everywhere: rendering was
  gated on `g_useColors`, set from `_isatty(_fileno(stdout))`, and a bat always pipes. Ungating
  it required the bats to set `[Console]::OutputEncoding` to UTF-8 -- without that PowerShell
  decodes the engine's UTF-8 as the OEM code page and re-encodes it, turning `⁹` (E2 81 B9)
  into CE 93 C3 BC E2 95 A3 in the log. Both halves are needed; neither works alone.

  `LogTable` therefore pads by DISPLAY WIDTH, not `size()`: `2⁹` is 4 bytes and 2 columns, and
  byte padding would step every column right of it out of line.

Scope: the console / `.log` output of every case under `runs\`. KnA2 only; Tt4 is not
touched. The result file (`RESULT=...`) is unchanged -- this spec is about the log and
nothing else.

Supersedes, in `docs/printout_spec.md`: sections 3 (per-block line), 4 (DONE lines),
5 (periodic `[rep]` line), 10 (per-type line) and 11 (`processed=`). Sections 1, 2, 6, 7,
8, 9 and 12 stand, amended only where noted. The three-tier governing principle of
`printout_spec.md` -- log = progress, ReadMe = nuance, source = detail -- is unchanged, and
this spec is written under it.

---

## 0. Why

The log already reported the right facts, in three different shapes: a per-leg line, a
per-block line, and a periodic `[rep]` line. Worse, the three shapes used the SAME WORDS
for DIFFERENT QUANTITIES, so the numbers a reader compared did not compare. On the order-3
leg of `K18Aut3-All` the live line said `results=2742 duplicates=2391` and the closing line
for that same leg said `results=351 duplicates=0`.

    word         live [rep] line (k18a2rep.cpp:1071)   final per-type line (:4054-4060)
    results      g_emits      -- covers emitted        gcanon.size()-before -- distinct classes
    duplicates   g_dupCovers  -- rejected covers       g_crossDup delta     -- classes re-found
    |Aut|        g_autHist    -- all distinct classes  g_savedAut           -- classes written

One table, with one set of column meanings, fixes both problems at once.

DECIDED: the columns carry the CLASS-level meaning throughout -- saved = classes written,
duplicates = classes found and not written, and the two together are everything the search
found. The cover-level counters stop being reported (section 7).

A `Results` column held that sum while this spec was first written, and it is gone: both
halves now print side by side in one cell, `531(2)`, so a column for their total said nothing
the row did not already say. What the words mean did not change -- only how many columns say
it.

---

## 1. The header block -- four lines, nothing repeated

    K18Aut3-All: EXE=..\..\x64\Release\p1f.exe  LOG=K18Aut3-All.log
    P1F: built 2026-08-26 14:03:22, N=18, kThreads=10, RESULT saved to result.txt
    P1F: started 2026-08-26 15:11:04 on BOX32
    P1F: Requested REP_ORDERS=4,5,7,17,V4,3

Line 1 is printed by `run.bat`; lines 2 to 4 by the exe. The split is forced: only the
.bat knows `CASE`, `EXE` and `LOG`; only the exe knows its own build stamp, when it began
and what environment it actually saw. Field names are the .bat's own variable names.

DROPPED as duplicated or wrong:

- `RUNARGS=18 10` -- it IS `N=18, kThreads=10`, which the exe prints.
- the second `kThreads` (the `Init:` line repeated the banner's).
- `Init:` and `K18A2 solver` -- the solver follows from `N=18`.
- `-- live below` -- the reader is looking at it.
- `results -> result.txt` becomes `RESULT saved to result.txt`.

### 1a. Build stamp -- the .exe's mtime, not `__DATE__`

`built <YYYY-MM-DD hh:mm:ss>` is the modification time of the running executable, read at
startup via `GetModuleFileName()` + `_stat()`.

`__DATE__` / `__TIME__` are the compile time of the one translation unit that holds the
banner (`source/k18a2.cpp`). If that file was not touched, the stamp is stale while the
binary is new -- exactly the failure it was meant to prevent. The .exe's mtime is the
build, for every path that produced it.

### 1b. Git version -- dropped from the log

`GIT_VERSION` is gone from the banner. `include/gitVersion.h` is maintained by hand and was
already stale when this was decided -- it read `260711a trivial-stab-skip (8bc9ee4)` while
the repository had moved several commits past it -- and a hand-maintained string cannot
distinguish a build from a clean checkout from a build with local edits.

CONSIDERED AND REJECTED: generating it from `git describe --always --dirty`, which would
give a true stamp and a `-dirty` suffix. Rejected because the log is a progress report, the
provenance question is answered by the build time plus the repository, and the zip ships
`gitVersion.h` for no-git remote builds, so the generator needs a fallback that reintroduces
the stale-string case it was meant to remove.

`include/gitVersion.h` is not deleted by this spec; only its use in the banner is.

### 1c. `P1F: Requested ...`

Echoes EVERY `REP_*` variable set at startup, one line, `NAME=VALUE` separated by two
spaces, in the order the engine reads them. Omitted entirely when none is set.

This is not decoration. `test_env_reset.bat` exists because a leftover knob silently
changing a run is a real failure mode of this repository; a run whose log does not state its
own knobs cannot be reproduced from the log.

### 1d. Start stamp and host -- when the run began, and where

`P1F: started <YYYY-MM-DD hh:mm:ss> on <HOST>` is local time read at startup, before any
work, followed by the machine name from `GetComputerName()`.

The table's `Elapsed` column is relative to the run, so nothing in a log says where on the
clock it sits. That is fine for one run and useless for a set: the thread-starvation work
compares runs against each other, and a staggered set -- one process started every half
hour to load the box gradually -- can only be read if each log states its own zero. Two
logs whose windows overlap were competing for the same cores, and their rates are not
comparable with a log that had the machine to itself.

The host is not decoration either. Rates in this repository are only ever read comparatively,
and thread counts differ per machine, so a log that does not name its host cannot be placed
at all. On 2026-09-09 a 10-thread K18 t8 log was read next to 8- and 30-thread K20 runs from
another machine before anyone noticed it recorded nothing about where it came from; its
`kThreads=10` is the program's default, not a signature. The absolute numbers in that
comparison were worthless.

Both live on one line, separate from `built`, so that `grep "P1F: started" *.log` lines up a
whole directory of runs with their machines beside them.

The finish time is deliberately NOT printed: the closing line already carries
`Total time=<n>min`, and start plus elapsed is the end.

---

## 2. Table grammar -- common to both column sets

One table per run. Between the header block and the closing lines the log contains only:
the table title, the table's captions, column-name rows, data rows, and the footnote lines
of section 4.

### 2a. Table title -- one per table

A single line naming what the table enumerates, blank line above it:

    Factorization of K18 P1F |Aut| > 2
    Factorization of K18 P1F |Aut| = 2, symmetry type 2⁸ 1², branch a=0
    Factorization of K18 P1F |Aut| = 2, symmetry type 2⁹, branch a=4

Composed by the engine, not by the .bat, from `N` and the subset actually searched -- a
title supplied by the caller can drift from what the run really did. For the block-driven
cases this title REPLACES the `[SUBSET]` line, whose two facts it already carries;
`printout_spec.md` section 1 continues to govern the wording of the symmetry type and the
branch clause.

### 2b. Column-name row -- repeated every 50 data rows

Printed immediately below the title (and below the range caption, where there is one), then
again after every 50 data rows, and once more immediately before the `=` TOTAL row.

Rows of every marker count toward the 50. One line in 50 is under 2% of a census log, and it
is what keeps a table that is 57,752 rows long readable at any scroll position.

### 2c. The marker column says what a row IS; the column NAME says what it holds

The table's first column is one character wide.

    (blank)   a completed UNIT -- one leg (column set A) or one block (column set B)
    ~         a unit still open, reported mid-search (column set A only -- see 2d)
    =         the final total for the table

The identity columns read `so far` on a `~` row and `TOTAL` on the `=` row.

WHICH QUANTITY a column holds is fixed by its NAME, not by the marker, and the two kinds sit
side by side on every row:

    Elapsed                   CUMULATIVE -- the run so far, not this unit's own
    Total saved(duplicates)   CUMULATIVE -- ditto
    Leg nodes / Block nodes   THIS UNIT'S OWN, and the rate beside them is its own throughput
    Done%                     CUMULATIVE by construction

DECIDED 2026-08-30, and it reverses the first draft. That draft made every column per-unit and
put the running totals only on `~` and `=` rows. Reading a log AS IT RUNS, the number wanted is
the run's total so far -- and recovering it from per-unit rows means finding the last periodic
line and adding everything since. The reverse recovery is one subtraction from the row above,
so the cumulative choice costs less.

The old per-row/cumulative ambiguity is still gone, because it was never about mixing the two
kinds: it was that `results=` and `saved=` used the same words for a per-block and a run figure,
with nothing on the line saying which. Here the column names say which, and they differ again
above the `=` row -- `Saved`, not `Total saved(duplicates)`, because that row holds only the
census.

### 2d. Cadence -- column set A only

`~` rows are emitted on the existing 5-minute progress timer, for column set A. A `~` row is
emitted only while a leg is open; a leg that completes inside the interval prints its blank row
and no `~` row. The `=` row is emitted once, when the table ends.

COLUMN SET B HAS NO `~` ROWS. A block row lands every 30-100 s and already carries the
cumulative elapsed, the cumulative saved(duplicates) and `Done%`, so a five-minute row would
duplicate the row above it. Column set A keeps them because one leg can run an hour --
`K18Aut3-All`'s order-3 `3⁶` is 62 minutes -- and nothing else prints in that time.

### 2e. Formatting rules

- Columns are space-padded to a fixed width and separated by ` | `, with no separator before
  the first column or after the last. Text columns are left-aligned; every numeric column is
  right-aligned.
- A rule of `-` as wide as the column-name row sits ABOVE every column-name row and once
  BELOW the `=` row, so the table has a visible top and bottom and does not run into the
  prose around it.
- A row renders only as far as its last non-empty cell. A `skipped` row is
  `order 3  | 3 3            | skipped` and stops there rather than trailing bare separators.
- `Elapsed`: below 600 s, `%.1fs`; at or above, `%.0fmin`. One column, one rule, so a leg
  and a block are comparable at a glance. CUMULATIVE: the run so far, so the last data row
  already carries what the `=` row will say.
- `Nodes`: the exact count with thousands separators, then the rate as `(%.0f/ms)`. THIS UNIT'S
  OWN, both of them, and the column name says so -- `Leg nodes (rate)`, `Block nodes (rate)`.
  They answer "how fast did this subset search", which is what a reader watches them for. The
  rate is that unit's nodes over that unit's elapsed, so it cannot be recomputed from the row:
  `Elapsed` beside it is cumulative. That is the price of the cumulative choice in 2c, and it
  is worth it -- a per-unit elapsed column that existed only to make the rate checkable would
  earn its width on no other row. On the `=` row both become the run's, and the name changes
  to `Total nodes (average)` to say the rate there is an average. When elapsed rounds to `0.0s`
  the rate prints `(-)`; a rate over a zero denominator is not printed as a number.
  NOTE: the separators are for the reader. Any tool that parses this column must strip them.
  Nothing in the repository parses the log today -- `compare_to_catalog.pl` reads the RESULT
  file -- and that is why it is safe.
- There is no `Results` column: it was `Saved + Duplicates`, and the next column shows both
  halves, so `5(12)` says 17 found as plainly as a column of its own did. `Saved` and
  `Duplicates` share that column, written `Total saved(duplicates)`, because they are read
  against each other and never apart. BOTH counts always print, `0` included, so nothing has
  to be inferred from a blank.
- `|Aut|` histogram: appended to a cell as ` |Aut|={k:n k:n ...}`, ascending in k,
  space-separated. Omitted when the count is zero. In the merged column the two histograms
  merge on k the same way the counts do: a k that was saved prints bare, a k that was
  rejected prints in parentheses, a k that was both prints `k:saved(dups)` --
  `|Aut|={3:5 6:(5) 12:(5) 84:(1) 156:(1)}`, and on a `=` row `|Aut|={2:3 3:5 ... 84:1(2)}`.
- All percentages: `%.2f`, bare number, the `%` in the column name.
- Numbers are never abbreviated. The old `[rep]` line printed `nodes=829M`; the table prints
  the count, and the column is wide enough.

### 2f. Which cells carry a histogram

`Total saved(duplicates)` does, and it is the only cell that does. Being cumulative, its saved
half on the LAST data row is exactly the published census; the `=` row repeats that half alone.

The duplicate half matters because a leg can find a class and write none: in `K18Aut3-All`,
`V4 fx2,0,0 F4` finds one and saves none, and the duplicate's `|Aut|` is the only class
information that leg contributes.

Saved and Duplicates were separate columns, beside a third called `Results`, when this spec was
first written. Merging the two and dropping the third removed a column reading `0` on most rows
and one that was always the sum of the others, and put the two numbers a reader compares next to each
other instead of at opposite ends of a 139-column line.

---

## 3. Column set A -- the leg-per-row cases (`K18Aut3-All`, `K14Aut2-All`, `K16Aut3-All`, `K20Aut4-All`)

Unit = one searched cycle type ("leg"). Many subsets per run.

    Symmetry  Type  Elapsed  Leg nodes (rate)  Total saved(duplicates)

Rendered, from a real `K18Aut3-All` run:

    Factorization of K18 P1F |Aut| > 2

    --------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Leg nodes (rate)       | Total saved(duplicates)
      order 4  | 4⁴ 1²          |    8.0s | 1,821,756 (228/ms)     | 131(0) |Aut|={4:96 8:22 16:12 272:1}
      order 4  | 4⁴ 2           |   89.9s | 3,733,367 (46/ms)      | 179(0) |Aut|={4:144 8:22 16:12 272:1}
      order 5  | 5² 1⁸          | skipped
      order 7  | 7² 1⁴          |   89.9s | 3,085 (-)              | 179(0) |Aut|={4:144 8:22 16:12 272:1}
      order 17 | 17 1           |   89.9s | 17 (-)                 | 180(1) |Aut|={4:144 8:22 16:12 17:1 272:1(1)}
      V4       | fx2,2,2 F3     |   90.0s | 7 (-)                  | 180(1) |Aut|={4:144 8:22 16:12 17:1 272:1(1)}
      V4       | fx2,0,0 F4     |  103.5s | 829,241 (61/ms)        | 180(2) |Aut|={4:144(1) 8:22 16:12 17:1 272:1(1)}
      order 3  | 3² 1¹²         | skipped
    ~ so far   |                |    6min | 67,000,000 (223/ms)    | 363(2) |Aut|={3:183 4:144(1) 8:22 16:12 17:1 272:1(1)}
      order 3  | 3⁶             |   64min | 829,826,761 (222/ms)   | 531(2) |Aut|={3:351 4:144(1) 8:22 16:12 17:1 272:1(1)}

    --------------------------------------------------------------------------------------
      Symmetry | Type           | Elapsed | Total nodes (average)  | Saved
    = TOTAL    |                |   64min | 836,214,247 (218/ms)   | 531 |Aut|={3:351 4:144 8:22 16:12 17:1 272:1}
    --------------------------------------------------------------------------------------

Notes:

- HOW MUCH OF THAT MOCK IS REAL: the per-leg node counts and saved counts are the figures from
  the run of 2026-08-29. `Elapsed` is shown cumulative here, which that log did not record per
  leg, so those figures and the duplicate halves are RECONSTRUCTED -- the old log did not report
  them at all, which is why this spec exists. Do not treat any of it as expected values.
- `N` is NOT a column. It is 18 on every row of every such run, and it is in the title and in
  the header block.
- `Skipped` is NOT a column. A skipped type is one row ending at `skipped` in the `Elapsed`
  position, and a column that would be blank on every other row buys nothing. Why a type is skipped stays in the test's `ReadMe.md`, per
  `printout_spec.md` section 10.
- The `[SUBSET]` line is DROPPED here. Its two facts, order/group and cycle type, ARE the
  `Symmetry` and `Type` columns; printing them again above each row would restore the
  interleaving this spec exists to remove. `printout_spec.md` section 1 still governs the
  WORDING of those two cells.
- There is NO percentage column. Decided 2026-08-30: against a per-leg row a run-level
  expectation is not a fact about the leg, and a completion figure raises more questions than
  it answers in a case that finishes inside an hour. The `EXPECTED_RESULTS` parameter
  considered at draft stage is not implemented, here or anywhere.
- The `=` row's `Saved` histogram is the run's census -- 531 for a full `K18Aut3-All`,
  matching `AllResults\K18_P1F_aut_gt1.txt`.

---

## 4. Column set B -- the block-driven cases (`K18-t8-a0`, `K18-t9-a0`, `K18-t9-a4`)

Unit = one block. Exactly ONE subset per run, so the subset is the title, not a column.

    Block  Elapsed  Block nodes (rate)  Total saved(duplicates)  Done%

Rendered, from a real `K18-t8-a0` run:

    Factorization of K18 P1F |Aut| = 2, symmetry type 2⁸ 1², branch a=0
    Blocks 0-57751 selected for processing (REP_F3START=0 REP_F3STOP=57751)

    ------------------------------------------------------------------------------------------
      Block  | Elapsed | Block nodes (rate)     | Total saved(duplicates)    | Done%
      0      |   64.0s | 25,768,960 (403/ms)    | 0(0)                       |  0.00
      1      |  142.4s | 31,591,424 (403/ms)    | 3(0) |Aut|={2:3}           |  0.00
      2      |  213.2s | 28,526,592 (403/ms)    | 3(0) |Aut|={2:3}           |  0.01
      3      |  292.2s | 31,818,752 (403/ms)    | 9(0) |Aut|={2:9}           |  0.01
      ...

    ------------------------------------------------------------------------------------------
      Block  | Elapsed | Total nodes (average)  | Saved                      | Done%
    = TOTAL  |  717min | 11,643,608,576 (271/ms)| 860 |Aut|={2:860}          |  1.03
    ------------------------------------------------------------------------------------------

    [F3COMPLETE] canonical blocks in range: 448
    [F3COMPLETE] duplicates by |Aut| > 2: aut{4:9 8:2}

Notes:

- HOW MUCH OF THAT MOCK IS REAL: block numbers, node counts and the saved
  running totals come from `K18-t8-a0_0-57751.log`. `Elapsed` is reconstructed (block lines
  carried no time), and so are the `=` row and the two footnotes.
- The range caption is `printout_spec.md` section 2, UNCHANGED, and it sits between the title
  and the first column-name row.
- `Block` is the bare `c` value, per `printout_spec.md` section 6.
- Non-canonical blocks produce NO row, as today, so block numbers have gaps. A row per
  skipped block would add tens of thousands of empty rows to a census log.
- `Done%` = raw blocks WALKED / raw blocks in the requested range, `%.2f`. Walked, not
  entered: a non-canonical block is skipped without a row, but it has still been got past, so
  it counts. RAW, not canonical: the canonical count is not known until the run ends, and the
  raw range size is the only denominator fixed before the work starts, so the figure can never
  walk backwards. In the mock, block 595 is the 596th raw position of 57,752, hence 1.03.
  This keeps the reasoning of `printout_spec.md` section 11 -- which already specified `%.2f`
  -- and moves the field off the `[rep]` line into a column present on every row rather than
  only the periodic ones.
- `Done%` exists ONLY here. Column set A has no percentage.
- The two `[F3COMPLETE] DONE:` lines of `printout_spec.md` section 4 are REPLACED by the `=`
  row, which carries `saved` and `duplicates`. The two facts with no column survive as
  footnote lines under the table: the canonical-block count for the range, and the
  `|Aut| > 2` duplicate histogram of `printout_spec.md` section 9 (omitted when empty).

---

## 5. Closing lines -- kept, one word lighter

    P1F: 531 result(s) written to result.txt (Total time=64min)
    End of job

Kept, and NOT folded into the `=` row. They name the output file, and `End of job` is the
sentinel that says the run was not interrupted -- a log stopping short of it was killed.

`done,` was dropped from the first of them: the line already states the outcome, and the
sentinel below it is what says the run finished.
`Total time` there is the whole process; `Elapsed` on the `=` row is the table's own span,
and the two differ by startup and teardown.

---

## 6. What changes in the code

Indicative, for the implementation plan; the plan is authoritative.

- `source/p1f.cpp:163` -- banner rewritten (section 1): add the .exe-mtime build stamp and
  the `REP_*` echo, drop `results ->`.
- `source/k18a2.cpp:38` and the k14 / k16 / k20 siblings -- the `Init:` line is deleted,
  taking `GIT_VERSION` and `__DATE__` with it. Sibling rule: the same edit in all four.
- `source/k18a2rep.cpp:1071` -- the `[rep]` line becomes a `~` row; its counters change to
  class-level meaning (section 0), so `g_emits` / `g_dupCovers` stop feeding it and
  `g_autHist` gives way to the written-class histogram.
- `source/k18a2rep.cpp:4054-4060` -- the per-type line becomes a blank row.
- `source/k18a2rep.cpp:3363, 3398` -- the per-block and DONE lines become blank rows, the `=`
  row, and two footnotes.
- `g_crossDupAut` (`:952`) already exists but, unlike `g_savedAut` (`:953`, cleared at
  `:3014`), is never cleared per leg. The `Duplicates` histogram needs it per unit.
- One table writer -- a single function owning the title, the column-name row and its
  50-row repeat, widths, alignment and the marker -- used by every row of both column sets.
  Two column sets, one writer; the sibling rule forbids an N-specific path.

---

## 7. Not in this spec

- The RESULT file format. Untouched, and that is what `run_all.bat` still judges: every
  change here was accepted on 6 passed, 0 failed over the RESULT files, never on the log.
- `regression\*` was declared out of scope here, as in `printout_spec.md` section 8a, and
  that did NOT survive contact. Two things forced edits:
  - `K18-t9-a4-owner-1\check.pl` greps the LOG -- it asserts that each rejection is reported.
    Its assertions are unchanged in substance, but their regexes moved to the table twice: once
    for the ` | ` separators and the merged column, once when `Results` went and the merged
    column turned cumulative. Its four cases run ONE block each, so cumulative and per-block are
    the same number there; a multi-block case added later must expect running totals.
  - Every `run.bat` that captures output, in `regression\` as much as `runs\`, needed
    `[Console]::OutputEncoding` set to UTF-8 for the superscripts (STATUS block, above).
- Cover-level counters (`g_emits`, `g_dupCovers`). They stop being reported. If a
  cover-per-class ratio is later wanted as a search-efficiency diagnostic, it is a NEW
  optional column, never a reinterpretation of an existing one.
