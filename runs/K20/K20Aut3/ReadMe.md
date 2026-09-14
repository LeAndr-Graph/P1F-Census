# K20Aut3 — the K20 order-3 sweep

Sweeps the K20 order-3 cell, symmetry type `3^6.1^2`, and writes every P1F class it finds to the
result file. The run ends by itself.

The cell is divided into **104 independent blocks**, numbered 0 to 103. Any set of them may be
run, in any order, on any machine, and the results concatenate.

---

## Running it

Edit two lines in `run.bat` — the interval of blocks to process, both **inclusive** and counted
from 0:

```bat
SET "BStart=0"
SET "BLast=0"
```

| you want | set |
|---|---|
| block 0 alone | `BStart=0`, `BLast=0` |
| blocks 5 and 6 | `BStart=5`, `BLast=6` |
| the whole cell | `BStart=0`, `BLast=103` |

Thread count is the second number of `RUNARGS` (`SET "RUNARGS=20 8"` → 8 threads). Nothing else
in the file needs changing; the log name, the result name and the engine's block range are all
derived from `BStart` and `BLast`.

Output for `BStart=5 BLast=6`:

```
K20Aut3_L1_b005-006.log      the console transcript
result_L1_b005-006.txt       the classes found
```

A run refuses to start if its log already exists, so a block cannot be swept twice by accident.

---

## Block size

Measured on **block 0**, 30 threads, 2026-09-08:

| | |
|---|---|
| wall | **17.1 hours** |
| nodes | 6.62 × 10¹⁰ |
| classes | **13,616** — `{3: 13611, 6: 5}` |

Blocks are uneven, and this is one sample of the 104.

⚠️ **That wall time came from the old scheduler and should not be planned against.** Most of it
was threads doing nothing. The rate fell from 9.47 to 0.63 million nodes per second over the run
while classes found per node stayed flat, so it was the same search at a fifteenth of the speed,
with 28 of the 30 threads spinning on an empty queue. The queue was replaced on 2026-09-09 by
sequential branch allocation. The same node count at the run's own peak rate is about **1.9
hours**, and no block has yet been run end to end under the new scheduler to say where between
the two it really lands. Measure one before scheduling the other 103.

---

## Reading the printout

After a couple of minutes of setup the run prints a progress row every five minutes:

```
~ so far   |                |   14min | 7,260,918,784 (8583/ms) | 1550(0) |Aut|={3:1550}
```

| field | meaning |
|---|---|
| `14min` | elapsed since the search began |
| `7,260,918,784` | search-tree nodes visited so far |
| `(8583/ms)` | nodes per millisecond, **averaged over the whole run so far** — not the current rate |
| `1550` | classes written to the result file so far |
| `(0)` | of those, how many were duplicates |
| `\|Aut\|={3:1550}` | the classes found so far by automorphism group order |

The final row replaces `~ so far` with the symmetry type, and a `= TOTAL` row closes the run:

```
  order 3  | 3⁶ 1²          | 1027min | 66,239,596,000 (1075/ms) | 13616(0) |Aut|={3:13611 6:5}
= TOTAL    |                | 1027min | 66,239,596,000 (1075/ms) | 13616 |Aut|={3:13611 6:5}
```

Two other lines are normal, not errors:

- `type 3² 1¹⁴ | skipped` and `type 3⁴ 1⁸ | skipped` — those types are empty, so only `3⁶ 1²` is
  searched.
- `P1F-Census: process priority BELOW NORMAL` — the run yields the machine the moment you need it.

Killing a run early keeps every class found up to that point.

---

## Checking the result

There is no automatic Compare step for this case: `AllResults` holds only classes with
`|Aut| > 3`, so a correct order-3 run would report a Compare Fault. Read the `= TOTAL` row and its
`|Aut|` histogram instead.

---

*Measurement history, profiling data and the reasoning behind the settings are kept outside this
repository, in `docs/k20aut3_details.md`.*
