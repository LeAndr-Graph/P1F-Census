# K14-t7-a3

**Purpose: verify the search engine used by `runs\K18-t9-a0` and `runs\K18-t9-a4`.**

K18's t9 columns are the block-driven census of the `2^9` involution -- the fixed-point-free
type. Their answers, 727 and 5 classes, are the engine's own output and have no external check:
a systematic fault reproduces itself on every re-run and stays invisible.

This case runs the same code path on K14, where the answer is already known by other means.
Type **t7 = 2^7**, the fixed-point-free type, branch **a=3**. `tN` is the number of 2-cycles, so
`REP_ONLYTYPES=7` here is the same selector as `REP_ONLYTYPES=9` in the K18 cases.

**Expected: 1 class**, `|Aut| = 2`, present in `AllResults\K14_P1F_aut_gt1.txt`. 398 canonical
blocks. About a second.

a=3 is used because it is the only productive branch of t7 at K14: a=0, a=1 and a=2 yield
nothing. That does not mirror K18, where t9's a=0 carries 727 classes -- which branch is
productive is a property of the size, not of the type. The empty branches are covered by
`regression\K14-t7-a0-1`.

The answer to check against comes from `runs\K14Aut2-All`, which finds K14's three `|Aut| = 2`
classes by the ordinary type loop, not by blocks. This is the one whose only involution is
fixed-point-free; the other two appear in `runs\K14-t6-a0`.
