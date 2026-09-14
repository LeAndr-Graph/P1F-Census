# K20Aut4-NoSkip -- the theorems, re-derived by search

`K20Aut4-All` does not search orders 4, 11, 13, 17, nor order-3 types `3¹1¹⁷` and `3³1¹¹`. It
**skips** them, citing the order-4 parity theorem and the odd-prime parity theorem. This case
searches them anyway and asserts the answer is zero, turning those proofs into something the
suite re-checks on every build.

It runs in **two phases**, both writing into one log. A correct run gives **two** `= TOTAL` rows,
each reading 0 classes, and leaves both result files empty:

    = TOTAL    |                |    0.0s | 362 (-)       | 0     <- phase A
    = TOTAL    |                |   36.5s |  62 (0/ms)    | 0     <- phase B

Anything else means a theorem is being misapplied. Note which of the two failures that would be:
either the proof is wrong, or the **predicate implementing it** skips a type it should not. The
predicate is by far the likelier, and nothing tested it before this case existed.

## The two phases, and why it is not one

`REP_ONLYTYPES` applies its indices within *every* order, and order 3 needs it -- type 5 is the
intractable `3⁵1⁵` and type 6 is `3⁶1²`, the open `|Aut| = 3` cell, and neither may be searched.
Order 4 has 25 types and needs all of them, so it cannot share a run with that restriction.
Orders 11, 13 and 17 have exactly **one** type each, which is index 1, so they ride along with
order 3 for free.

    phase A   REP_ORDERS=3,11,13,17   REP_ONLYTYPES=1,3
    phase B   REP_ORDERS=4            (REP_ONLYTYPES cleared)

Phase B appends to the log. **Do not widen `REP_ONLYTYPES` beyond `1,3`.**

## Why it earns its place: the skips buy almost nothing

Measured 2026-09-03 on this build:

| what | time | nodes | classes |
|---|---:|---:|---:|
| orders 11, 13, 17 searched | 0.0s | 362 | 0 |
| order 4, all 25 types searched | 32.6s | 62 | 0 |
| order-3 types `3¹1¹⁷`, `3³1¹¹` searched | ~3.5s | 0 | 0 |
| orders 6+9 **with every skip disabled** | 93.8s | 1,653,362 | unchanged |
| orders 6+9 normally | 31.2s | 1,653,307 | -- |

The last two rows are the striking pair: disabling every skip across orders 6 and 9 adds
**55 nodes**. The retired composite types die at the root, so the extra minute is per-type setup,
not search.

Searching everything the skips retire costs roughly **two minutes** across the whole `|Aut| > 3`
case. That is the cost of not having to take four theorems on trust.

## The one skip that is load-bearing

Order-3 type `3⁵1⁵`: about `4.9e12` cover-nodes, and the 32-core grind never finished it. That is
the type the odd-prime parity theorem was written to retire, and it stays retired. Nothing here
touches it.

So the honest scope of "the classification depends on parity theorems" is narrower than it looks:
after this case, it depends on them for **one type**.

## Reading the log: two kinds of "skipped"

A type row reading `skipped` means one of two different things, and the `SETUP` ticks are what
tell them apart:

    no SETUP ticks at all        -> retired by a theorem, before any work
    SETUP collectTasks  n=0      -> SEARCHED, and empty at the root

Under `REP_NOSKIP` every row should be the second kind. For example:

    [K20-REP]  type 4² 1¹²   SETUP collectTasks   2.4s  n=0
      order 4  | 4² 1¹²      | skipped

That is a searched, genuinely empty type -- not a skip. Before the ticks existed the two were
indistinguishable in the log, which is exactly how a mis-skipped type could have hidden.

## No Compare step

A run that writes no records has nothing to look up in a catalog. The check is **both** `= TOTAL`
rows reading 0, and `result_a.txt` / `result_b.txt` both empty.

## Expected time

**About 37 seconds** at 10 threads, measured 2026-09-03. Phase A is instant (0.0s, 362 nodes --
orders 11/13/17 plus the two order-3 types). Phase B is 36.5s across order 4's 25 types, and 62
nodes: almost all of that time is per-type setup, not search.

## Changing the thread count

`run.bat` line `SET "RUNARGS=20 10"`. First number is N and must stay 20; second is the thread
count, shipped at 10.
