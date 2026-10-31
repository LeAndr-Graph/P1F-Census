# K18 -- notation of the order-2 cases

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
