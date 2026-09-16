# regression

Eight cases, about **4.5 minutes** in total. Build first -- `build.bat` in the repository root
puts `p1f.exe` in `x64\Release\`, which is where every case looks for it unless `P1F_EXE` says
otherwise.

Run the whole suite:

    regression\run_all.bat

It calls each case in turn and prints `SUMMARY: 8 passed, 0 failed`. Exit code 0 means all
passed, 1 means at least one did not.

Or run one case on its own -- go into its folder and call its bat:

    cd regression\K16Aut3-All-1
    run.bat

Every case does the same three things: it calls `env_reset.bat` first, so a `REP_*` left in your
shell cannot quietly change what the case searches; it deletes its old `result.txt` and runs
`p1f.exe` with a fixed set of variables, writing `<case>.log` beside the bat; and it compares the
new `result.txt` against the frozen `result_expected.txt`. The comparison is byte-exact first and
falls back to a sorted compare only if that differs -- two correct runs of a multi-leg case agree
on content but not on the order records happen to be written in.

Times below were measured on the development laptop, Release|x64. They vary with the machine;
what must not vary is the verdict.

## K14-t6-a0-1

Guards the search engine that the K18 t8 a=0 census uses: K14 type `2^6.1^2` (two fixed points),
branch a=0, one block column driven through the same block driver K18 drives. Being tiny, it
runs the whole path in a second and pins the node count as well as the classes.

Total time: **1 s**.
Expected result if the test passed OK:

    PASS K14-t6-a0-1  (2 class records, byte-identical)
    PASS K14-t6-a0-1  (nodes 44,047, as expected)

## K14-t7-a0-1

The same guard for the engine behind K18 t9 a=0 and t9 a=4: K14 type `2^7`, fixed-point-free,
branch a=0, one block column. The expected answer is zero classes, so this is the case that
catches an engine which has started inventing them.

Total time: **2 s**.
Expected result if the test passed OK:

    PASS K14-t7-a0-1  (0 class records, byte-identical)
    PASS K14-t7-a0-1  (nodes 13,656, as expected)

## K14Aut3-All-1

The structural validator: K14 with orders {2,3,7,13}, the primes dividing any K14 `|Aut|`, whose
union is the full `|Aut| > 1` classification. K14 is small enough to COMPLETE order 2 -- the leg
that is intractable at K16, K18 and K20 -- and like K18 it has the fixed-point-free even types
`2^7` and `4^3.2^1` that K16 lacks.

Total time: **15 s**.
Expected result if the test passed OK:

    PASS K14Aut3-All-1  (21 class records, byte-identical)

The 21 classes are `{2:3, 3:5, 4:1, 6:5, 12:5, 84:1, 156:1}`; the order-2 leg alone finds 16 of
them and is proven complete.

## K16Aut3-All-1

The literature-matched oracle: K16 with orders {3,5,7} returns the 30 classes with `|Aut| > 2`,
a published number this engine must reproduce exactly. Order 2 is out of scope here, since
`|Aut| = 2` is not `> 2` and type `2^k` is over the centralizer cap.

Total time: **4 s**.
Expected result if the test passed OK:

    PASS K16Aut3-All-1  (30 class records; record ORDER differs between runs, content identical)

The 30 classes are `{3:19, 5:5, 7:4, 14:1, 15:1}`. The class with `|Aut| = 15` is found by both
the order-3 and the order-5 leg, so 31 records are emitted for 30 distinct classes.

## K18-t9-a4-block1777-1

The fast end-to-end check of the whole t9 a=4 fast path -- TYPEMASK, SGEN, DIAGPRUNE and
oneperpair -- on a single block searched from scratch. There is no baseline to lean on: the block
is asked what it yields, and `seed6.txt` sits beside it as an independent oracle showing the class
it re-finds is the one block 4.0.133 owns.

Total time: **12 s**.
Expected result if the test passed OK:

    PASS K18-t9-a4-block1777-1  (1 class records, byte-identical)

## K18-t9-a4-owner-1

The owner filter (`REP_OWNER`) end to end, over four blocks chosen to cover every path it has: a
block that owns its class, a block holding a class another block owns, the one known a=4 class
with `|Aut| > 2`, and a block that yields the same class twice with the same owner. The filter may
only drop results, never prune, so the node counts are checked to be exactly what the same blocks
cost without it.

Total time: **78 s**.
Expected result if the test passed OK:

    PASS  owner17607.log nodes=44961792 (unchanged by the filters)
    PASS  owner1777.log nodes=18239488 (unchanged by the filters)
    PASS  owner2237.log nodes=10771456 (unchanged by the filters)
    K18-t9-a4-owner-1: PASSED

## K20Aut4-All-1

The complete K20 `|Aut| > 3` census, run quickly: the order set {19, V4, E9, S3, 6, 9} is every
leg that produces classes plus the two instant theorem-echo sweeps. Orders 5 and 7 are left out
because they are expected-0 sweeps that would cost 81% of the wall time, and order 3 because its
type 6 cannot finish and runs as a nondeterministic harvest.

Total time: **163 s** -- the longest case in the suite.
Expected result if the test passed OK:

    PASS K20Aut4-All-1  (230 class records; record ORDER differs between runs, content identical)

The 230 classes are `{6:168, 9:46, 18:9, 19:3, 57:1, 171:2, 342:1}`, emitted as 243 records
because a class found by two legs is recorded by each.

## owner-guards-1

Checks that `REP_OWNER` is REFUSED wherever it cannot mean anything -- on k14, k16 and k20, which
have no block column, and on k18 without the block driver. Ignoring the variable is the dangerous
option there, because someone sharding a sweep with it would believe the outputs are disjoint when
they are ordinary duplicate-prone results, so the run stops with exit code 1 before `RESULT` is
created.

Total time: **1 s** -- the guards fire before any search starts.
Expected result if the test passed OK: seven `PASS` lines followed by

    owner-guards-1: PASSED

the last two of which check that the refusal messages say why -- that blocks are k18-only, and
that k18 needs `REP_F3COMPLETE`.
