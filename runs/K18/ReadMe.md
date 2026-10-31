# K18 cases

General notation (types, group names, shapes) is in [`../ReadMe.md`](../ReadMe.md).

The `|Aut| = 2` classes of K18 are found by three block-driven cases, named `K18-t<N>-a<a>`:

- **`tN`** is the number of 2-cycles of the involution: `t8` = type `2⁸ 1²` (two fixed
  players), `t9` = type `2⁹` (fixed-point-free). It is the value of `REP_ONLYTYPES`.
- **`a`** is the branch. An involution fixes exactly one round F0; with P the set of pairs it
  swaps, `|F0 ∩ P| = d = t - 2a`. For `t8` only a = 0 exists (F0 is the 8 swapped pairs plus
  the fixed pair). For `t9` the dichotomy theorem gives d = 9 or 1, so a = 0 or a = 4.

| Case | Type | Branch | Classes |
|---|---|---|---|
| `K18-t8-a0` | `2⁸ 1²` | a = 0 | 9,447 |
| `K18-t9-a0` | `2⁹` | a = 0 | 727 |
| `K18-t9-a4` | `2⁹` | a = 4 | 5 |
| `K18Aut3-All` | every order > 2 | -- | 531 |

Together: the 10,710 classes of K18 with `|Aut| > 1`. Each case ReadMe gives the details.
The K14 cases `K14-t6-a0` and `K14-t7-a3` use the same naming at K14 (`t6` = `2⁶ 1²`,
`t7` = `2⁷`) to check the engine against known answers.

## Restart

Cases `K18-t8-a0`, `K18-t9-a0`, `K18-t9-a4` use the order-2 block driver: every block
line ends with the `REP_F3START` value to resume from (`REP_F3STOP` unchanged); the case ReadMe has the details. `K18Aut3-All` (about 66 minutes) uses
the block walk:

**Block walk.** Every 5 minutes the log's `~ so far` row ends with `Block restart #(%)` = k, the
lowest root block not finished. Move the old log and result file (`RESULT` in `run.bat`) aside, add
`SET "REP_BLOCKS=k-last"` to `run.bat`, and run it; the new result plus the old one,
merged by `tools/make_catalog.pl` (which removes duplicates), is the full answer. Blocks above
k that were already done are walked again. `REP_BLOCKS` applies to every leg and type of the
run, so in a run with several legs also set `REP_ORDERS` and `REP_ONLYTYPES` to the
interrupted one, and run the legs the log never reached as their own run. A run shorter than
5 minutes prints no restart row: run it again from the start.
