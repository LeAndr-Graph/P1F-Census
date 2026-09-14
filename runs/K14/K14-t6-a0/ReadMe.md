# K14-t6-a0

**Purpose: verify the search engine used by `runs\K18-t8-a0`.**

K18's t8 column is the block-driven census of the `2^8 1^2` involution -- the two-fixed-point
type -- at branch a=0. Its answer, 9,447 classes, is the engine's own output and has no external
check: a systematic fault reproduces itself on every re-run and stays invisible.

This case runs the same code path on K14, where the answer is already known by other means.
Type **t6 = 2^6 1^2**, the two-fixed-point type, branch **a=0**. `tN` is the number of 2-cycles,
so `REP_ONLYTYPES=6` here is the same selector as `REP_ONLYTYPES=8` in the K18 case.

**Expected: 2 classes**, both `|Aut| = 2`, both present in `AllResults\K14_P1F_aut_gt1.txt`.
196 canonical blocks out of 452 raw. Under a second.

The answer to check against comes from `runs\K14Aut2-All`, which finds K14's three `|Aut| = 2`
classes by the ordinary type loop, not by blocks. Two of the three are t6-invariant and must
appear here; the third is fixed-point-free and appears in `runs\K14-t7-a3`.
