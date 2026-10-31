# KnA2 -- run parameters

p1f.exe takes two positional arguments and reads everything else from environment variables,
which the engine parses itself.

    p1f.exe [N] [kThreads]         N = any even 14..32; default 18, 10 threads

There is ONE engine, knA2 (include/knA2.h, source/knA2rep.inl), compiled once per N; it replaced
the per-N k14/k16/k18/k20 engines on 2026-10-25. Every variable below is read at
every N; the ones that only make sense for an order-2 block sweep say so and refuse other use.

Every output file a run writes goes to the CURRENT DIRECTORY, i.e. the folder the bat was
started from. Nothing is written outside it.

A variable that is merely PRESENT enables its feature unless a value is documented below; the
engines mostly test presence, not content. Clearing one means removing it from the environment.
Note the shell difference: in cmd, SET REP_PATSTAT= UNSETS the variable, while in bash
export REP_PATSTAT= sets it to empty, which still counts as present and therefore ON.

## 0. Output files

```text
RESULT=<path>           The run's ONE output file. Required -- a run without it stops. One record
                        per distinct class:

                            #1 |Aut| = 2
                             "   0  1   2  3   4  5   6  7   8  9  10 11  12 13  14 15  16 17 "
                             ...

                        a "#<seq> |Aut| = <aut>" header followed by NM rows of N/2 pairs. `aut` is
                        the real automorphism order. `seq` counts write order in THIS file only:
                        records land in thread-discovery order and every file restarts at 1, so it
                        is NOT a class identifier -- two correct runs may order records differently
                        while holding exactly the same classes.

                        NEVER APPENDED TO. The file is created fresh, and if it already exists the
                        run reports an error and stops BEFORE doing any work: a run is never
                        resumed into its own output, and a finished census cannot be silently
                        extended or overwritten. Records are flushed as they are found, so a run
                        killed after hours leaves a valid, complete prefix.

                        All engines, all modes, all N.


REMOVED -- no engine reads these, so setting one has no effect on a run:

  REP_PRELOAD           Seeded the class set from a previous run's file so a restart reported only
                        NEW classes. Gone: a KnA2 run carries no baseline at all. Block sweeps
                        dedup statelessly instead (block ownership).

  REP_INFO              A second output carrying the block, the sigma0 stay/swap pattern and
                        "# dup block" attribution lines. Gone: those are internal coordinates,
                        not results. RESULT replaces it.

  KNA2_RESULT           The host's legacy P-file layout, whose header always printed
                        "|Aut(M)| = 0" because |Aut| never reached the result callback. Gone:
                        RESULT replaces it and carries the real |Aut|.

  REP_F3CAP             Per-block time cap for the block sweep, for cost profiling. Gone: a block
                        cut off early can miss the cover it OWNS, and no other block will write
                        that class -- silent loss, under a run that still claims the range owns
                        what it wrote.

  REP_F3L4              Split one block's level-4 work into shards and run one of them. Gone: both
                        shards believe they are block c, so both write its owned class.

  REP_CANDCAP           Cap genM candidates per node, bounding memory on ultra-wide a=4 nodes.
                        Gone: a biased subset, never a census, with the same silent-loss mode as
                        a time cap.

  REP_TRIPLES           Renamed REP_PRECALC (section 2). The mechanism was never triples-specific:
                        nrows comes from the orbit, so the same builder yields TRIPLES at order 3
                        and DOUBLES at order 2, which is what the K18 t8 and t9 legs use. Refused
                        rather than aliased -- a run whose log says TRIPLES while it is building
                        doubles misreports what it did.

  Anything that needs blocks, patterns or class identity is a services/ job, not engine output.

REP_PRIORITY=normal     Opt OUT of the below-normal process priority the engine sets at startup.
                        A census saturates every thread it is given for hours or days, and at
                        normal priority it makes the machine unusable -- a 10-thread K20 order-2
                        run drove this laptop to a near-crash (2026-09-03). Below normal costs
                        the run almost nothing on an otherwise idle box, since it still gets
                        every free core, and it yields at once to anything you are doing. The
                        first log line says which priority is in force. Anything other than
                        "normal" leaves the default (below normal) in place.
```

## 1. What gets searched

```text
REP_ORDERS=<list>       Comma-separated automorphism orders to classify in one pass, e.g.
                        "4,5,7,17,V4,3". Tokens: V4 = Klein four-group sweep, E9 = C3xC3
                        two-generator sweep, S3 = S3 two-generator sweep. "order:target" adds a
                        harvest target: that leg stops after `target` distinct classes (used for
                        the strata that cannot finish). Takes priority over REP_ORDER.

REP_ORDER=<order>       Single order, same tokens and same "order:target" syntax. Used when
                        REP_ORDERS is unset; with neither set, the engine's compiled-in default
                        order runs.

REP_ONLYTYPES=<list>    Comma-separated 1-based cycle-type indices; every other type of the
                        chosen order is skipped. The type list is printed at the start of each
                        order leg. A run restricted this way is NOT a complete order.

REP_NOSKIP=1            Search the cycle types the engine would skip by theorem (odd-prime
                        parity, involution fixed-point lemma, power reduction). Verification
                        mode: those types must return 0. A skip can never lose classes.

REP_FPF                 Diagnostic: isolate the fixed-point-free type(s) of the order.

REP_SYMCAP=<n>          Centralizer-enumeration cap (default 2,000,000). A type whose centralizer
                        exceeds it is over-cap and takes the level/shard path instead of direct
                        enumeration. Raise it to force the direct path on a bigger type.
```

## 2. Search control and speed

```text
REP_PRECALC             Precalculate, once per block, the list of orbits that are admissible at
                        that block's seed, then walk the block by filtering that list instead of
                        regenerating candidates at every node.

                        An orbit is whatever sigma makes it: a TRIPLE {F, sF, s^2 F} at order 3, a
                        DOUBLE {F, sF} at order 2, or a single sigma-fixed factor. nrows is taken
                        from the orbit, so one builder serves all of them. (It was called
                        REP_TRIPLES until that stopped being true.)

                        Measured:
                          K20 order 3, block 0                9.3x   (a ~2-hour block, not a day)
                          K18 t8 a=0, blocks 0-9, 10 thr      5.2x   530.1s -> 102.6s
                          K18 t9 a=0, blocks 0-9, 10 thr      5.6x   368.8s ->  65.5s
                          K18 t9 a=4, block 1777, 10 thr      1.3x    27.3s ->  20.8s
                        Same tree and same classes in every case. The a=4 gain is smaller because
                        its accelerators have already removed 95% of the tree; with them off the
                        same block is 7.7x. The list pays best where there is bulk to chew through.

                        The lists are built LAZILY per node: a node holds its parent's admissible
                        set and the entry it committed, and computes its own set only when it is
                        expanded. Do not change that. The first cut materialized a list per child
                        at the seed and cost hours of one thread and tens of GB.

                        The list belongs to ONE block, so blocks are never pooled together and one
                        block's threads cannot be lent work from another. Size and build cost vary
                        widely: K20 order 3 is ~1.2M orbits and ~50 s, K18 t8/t9 is 26-28k orbits
                        and 0.3 s. On K18 that build is single-threaded with the other threads
                        idle, about 3% of a block, and it is paid once per block.

                        PROTOTYPE, and it says so in the log. It supports orbits of size <= 3;
                        anything else exits. Use REP_PRUNELEVEL=1 with it.

REP_PRUNELEVEL=<L>      Past factor-depth L, skip the per-node setwiseStab/schreierDedup prune.
                        Classes stay exact -- the global canonKey dedup is what deduplicates --
                        and on over-cap sweep blocks the prune removes zero nodes while costing
                        most of the wall time (with L=1: t8 block ~3.4x faster, t9 ~29x).
                        (K20 order-3 block 0: identical tree, 1.49x.) On blocks whose root
                        stabilizer is large the flag can be
                        much SLOWER (K14 order-2 blocks 0:3: 4 s -> >240 s, same classes).

REP_TYPEMASK=1          Apply the one-per-pair rule at GENERATION time (mask the partner of
                        vertex 1 inside genM) instead of rejecting the built orbit afterwards.
                        Same classes, fewer nodes. Needs a fixed-point-free sigma0.

REP_SGEN=1              Enumerate a forced-S row on the sigma0 quotient (genS) instead of
                        filtering genM's output. Requires REP_TYPEMASK.

REP_DIAGPRUNE=<lo>[:<hi>]
                        Diagonal look-ahead used as a prune at depths lo..hi: a node provably
                        unable to complete is cut. The a=4 sweep uses 8:14. Needs a
                        fixed-point-free sigma0, and is refused outside the K18 a=4 column
                        (REP_F3A=4), the only place its one-diagonal rule is sound.

REP_PATAPPLY=<mode>     Neighbor-0 pattern filter (value is case-insensitive):
                          oneperpair -> within each sigma0 pair {2k,2k+1}, one S and one W
                          calculate  -> derive the alternating target from rows 2,3 (default)
                          1/empty/other, with REP_PATTERN=<S/W string> -> fixed-pattern filter
                        Canonization is NOT forced off; the neighbor-0 index is canon-compatible.

REP_PATTERN=<S/W>       The target stay/swap string for the fixed-pattern mode of REP_PATAPPLY,
                        indexed by neighbor-0 (position j-1 = the row whose 0-neighbor is j).
                        Empty means "calculate from rows 2,3".

REP_PATONLY=<S/W>       Legacy fixed-pattern form: restrict the search to ONE full sigma0
                        stay/swap pattern, pruning at every node.

REP_PATPAIR             Select the one-per-pair filter standalone (same rule as
                        REP_PATAPPLY=oneperpair). Sound, but not a clean |Aut|=2 filter: an
                        |Aut|>2 class can be one-per-pair too, so separate by |Aut| at emit.

REP_ROW=<r>:<j>[,<r>:<j>...]
                        Slice filter, any order, any N: a candidate orbit
                        holding the factor that covers {0,r} survives only if that factor pairs
                        vertex 1 with j (r, j in 2..N-1, r != j). The slices over all j of one r
                        together find every class of the unfiltered run; they can share classes
                        (merge with a dedup) and their node counts need not add up to the
                        unfiltered count. A second or third pin subdivides a slice further.

REP_NOROOTPRUNE         A/B switch: collect the first-orbit tasks with the old unpruned root walk
                        instead of the per-image pruned one. Same tasks, same order, much slower
                        from K22 on (the unpruned walk visits all (N-3)!! matchings through {0,1}).

REP_DIVE=<budget>       Fast-search mode on an over-cap type: replace the exhaustive walk with
                        backtracking dives of `budget` nodes per restart, each restarting from a
                        fresh random root. Runs until the harvest target or a kill. Not a census.
```

## 3. Level, range and shard modes

```text
REP_LEVEL=<L>           On an over-cap type, enumerate the search tree level by level down to
                        depth L and treat the level-L nodes as blocks.

                        ON AN UNDER-CAP (ENUMERABLE) TYPE -- one whose C(alpha) was small
                        enough to list, e.g. K20 3^6.1^2 with |C| = 1,049,760 -- only LEVEL 1
                        exists, and it is the set of root representatives: collectTasks forces
                        the anchor edge and the C(alpha)-orbit collapse keeps one rep per
                        orbit, so they are complete, disjoint and index-stable. Deeper levels
                        need the root BSGS that only the over-cap setup builds; REP_LEVEL > 1
                        prints "NOT AVAILABLE here" and falls back to level 1 rather than
                        counting something else. (Before 2026-09-05 both this variable and
                        REP_RANGE were SILENTLY IGNORED on that path, so a count request ran as
                        an unbounded sweep.)

REP_RANGE=<spec>        What to do with those level-L blocks:
                          start:end                        process that block range on the queue
                          start:end:step(timeout,results)  chunked density sweep: walk
                                                           [start,end) in `step`-block chunks,
                                                           each capped by seconds and by results
                                                           (0 = no cap)
                        Unset means COUNT the level and print the branch numbers instead.

                        For an ORDER-2 BLOCK SWEEP use the block driver (section 4) instead.
                        (The retired k18 engine refused REP_RANGE; knA2 accepts it at every N,
                        as k14/k16/k20 did, so this is now advice, not a refusal.) REP_RANGE walks
                        the tree WITHOUT block canonicalization, so it re-searches starters
                        that canonize together (t8 a=0: 57,752 raw blocks against 25,530
                        canonical); it has no end of its own, only the count in <spec>; and
                        it prints no resume point, so an interrupted sweep cannot be
                        continued exactly. The documented way to finish one early was to
                        watch the Chao1 est-total flatten -- a signal that has been
                        overturned before on this very stratum. Use
                        REP_F3COMPLETE + REP_F3A instead.

                        On an under-cap type the range is a slice of the root reps (see
                        REP_LEVEL) and the chunked form is ignored -- the slice runs plainly,
                        every branch to completion, and the log says so. Slices compose
                        exactly: verified at K14 order 3, where the full run is 10,975 nodes /
                        17 classes and the slices 0:3 and 3:8 are 4,148 + 6,827 = 10,975 nodes
                        whose class union is exactly those 17. Note there is no owner dedup
                        across processes, so a class reachable from two root reps is written by
                        both: the union of slice results needs a global dedup pass.

REP_RAWSHARD            Seed the tree traversal as "trivial" in the shard/level/range modes.

REP_BLOCKS=<i>,<j>,...    Walk ONLY these root blocks of each enumerable leg (indices; "first", "mid"
                        and "last" accepted), each on one thread, and print every block's nodes and
                        seconds plus the leg's block count. One setup for any selection: the quick
                        way to see how a leg's blocks vary before planning a sweep.
                        "i-j" is a range (e.g. "17-last"). Restart after a crash: every 5 min a
                        full walk's "~ so far" row ends with "Block restart #(%)" = k;
                        rerun with REP_BLOCKS=k-last,
                        plus REP_ONLYTYPES for that leg (REP_BLOCKS applies to
                        every leg of the run). Blocks above k already done are walked again.

REP_SPLIT=L[:a:b]       Cut every root block at level L (L orbits committed, root = 1), number the
                        level-L branches in DFS order and walk branches [a, b) -- each thread takes
                        the next branch in order and runs it with the normal search, so the tree
                        and node counts are an unsplit run's (plus the harvest above level L).
                        Prints the map "level L-1 branch p = level L branches x-y" (<= 2000
                        parents). The "~ so far" row's Block restart # is the lowest branch not
                        done: continue with L:<it>:b. b = 0 harvests only (prints the map).
                        Applies to every leg: pair it with REP_ONLYTYPES. Enumerable path only.

REP_COUNTROOT           Count each leg's root candidates without storing them, print the setup's
                        memory peak (task list x2 while joining, or list + lookup table), search
                        nothing. Answers "will this case fit in RAM here" before trying it.

REP_ROOTCAP=<K>[:raw]   Stop each leg's root walk after K candidates and search only their blocks:
                        a HARVEST of the early blocks for cases too big to set up whole (K26 orders
                        4, 5, 6), or with REP_BLOCKS a cheap per-block timing (K22 orders 4, 5).
                        The C(alpha) reduction still runs on the K candidates; ":raw" skips it
                        (every alpha-orbit its own block, no symmetry breaking). Every class it
                        writes is real; absence proves nothing.

REP_SAMPLE=<stride>[:<cap>]
                        Deep-shard RANGE on the enumerable path: seed every stride-th block, at
                        most cap of them. A sample, not a census.
```

## 4. Order-2 block sweeps (the a.b.c block coordinate)

```text
REP_F3COUNT             Fix a and b (canonical Hamiltonian F2), then count the level-3 blocks --
                        the c of the a.b.c coordinate. Prints raw and canonical starter counts.
REP_F3A=<n>             Which level-1 seed representative is the "a" column (0..reps-1; a=0 is
                        sigma0). Default 0. By the dichotomy theorem only a=0 and a=4 are fertile.
REP_F3COMPLETE          The integrated proof driver: sweep the a.0.c blocks, canonize each and
                        complete it. Every completed block reports the classes it holds.
                        Every block line ends with the REP_F3START value to resume from.
REP_F3START=<c>         First RAW block c to complete (the resume point). Default 0.
REP_F3STOP=<c>          Last RAW block c to complete, INCLUSIVE (unset = to the end). A range is
                        given by its two ends, never by a start and a count: REP_F3START=100
                        REP_F3STOP=199 is a hundred blocks, 100 through 199. The predecessor
                        REP_F3MAX took a COUNT from the same start, and it is now refused rather
                        than read (see REMOVED in section 0) -- a count read as a stop, or a stop
                        read as a count, runs a different range and still reports success.
REP_F3RAW               Disable block canonization: complete all 65,040 raw blocks instead of the
                        59,872 canonical ones. Default is canonization ON.
REP_F3LIST=<path>       Write the list of first-occurrence blocks (the "to process" list).
REP_F3DUMP=<path>       Dump each first-occurrence block's 3-factor starter, one record per line.

REP_OWNER=0             Owner filter OFF. The filter is ON BY DEFAULT under REP_F3COMPLETE, and 0
                        is the only value that disables it -- set it when you want every re-find
                        recorded, as RegressionTest\K18-t9-a4-block1777-1 does.

REP_OWNER               Owner filter (the default; naming it changes nothing). A class is reachable
                        from many blocks (about 60 for a=4), so a sweep re-finds it over and over.
                        Every class has one canonical home in a column -- the minimum block index
                        over all of its column labelings, a property of the class and the column,
                        not of the labeling the search happened to find -- and a block writes a
                        class only if it is that home.
                        Everything else is rejected and reported, never written.
                        Consequences: no baseline file is needed, any REP_F3START/REP_F3STOP
                        range is self-contained, and two ranges never write the same class, so
                        their outputs concatenate with no dedup.
                        Requires REP_F3COMPLETE, and an explicit REP_OWNER/REP_OWNERALL is accepted
                        at N=18 only (the host's check, unchanged from the k18 days): elsewhere it is
                        refused with exit 1, before RESULT is created, because a run that ignored the
                        variable would look shard-disjoint without being it.
                        Every involution type is served: the tau enumeration takes the column's
                        sigma0 fixed-point count, so t9 (2^9) and t8 (2^8.1^2) both work.
                        It drops results, it never prunes -- node counts are unchanged.
                        Per block the log adds "rejected=N (foreign=F, in-block=B)": foreign =
                        another block owns it; in-block = this block reached the same class by a
                        second cover, caught by the canonized class key.
REP_OWNERALL            With REP_OWNER: print one [OWNER] line per rejected cover, naming the
                        block that owns it. Verbose -- for validation runs, not a long sweep.

REP_BLOCKSFOR=<c>       List every raw c that shares this block's level-3 canonical form.
REP_CDUMP=<file>        Dump the distinct canonical c-ranks (the a=4 c-space reference).
```

## 5. Removed

The k18 navigation, coordinate and locating switches were retired with k18.

## 6. Estimates

```text
REP_ESTIMATE=<probes>   Estimate mode: run this many Knuth probes per representative and do NO
                        search. Gives a tree-size estimate for an intractable type.
REP_ESTFILE             Per-block estimate log, written as k<N>_<type>_step<step>_estimate.txt.

NOTE: the per-block estimate log is not trusted and no test sets it. The figure actually read
is the Chao1 estimate in the [rep] progress line -- "est-total~N (f1=.. f2=..)" -- and that is
printed whatever these variables are set to.
```

## 7. Diagnostics and validation

```text
REP_WITNESS             Transporter/injectivity check on the canonization (the fatal-direction
                        test): evidence that the canonization does not over-merge classes.
REP_VTEST               Check the fast backtracking minimal image against the brute
                        C(sigma0)-invariance oracle on real partial covers.
REP_FTEST               The same fast-vs-brute minimal-image check, second form.
REP_CANONTEST           Validate that the brute C(sigma0) smallest image is a class invariant.
REP_CANONOVL=<level>    Tree-overlap probe at that level (F3COMPLETE and COMPLETEPREFIX).
REP_BLOCKDUMP           Dump the F1/F2/F3 commit prefix of each distinct class as it is found.
REP_LEVELSTATS          Per-level node-count and branching profile.

REP_PATSTAT             Tally the sigma0 stay/swap pattern of completions (and of the forced last
                        factor), printed as [PATSTAT] lines with counts and |Aut|.
REP_PATBYCLASS          Collect neighbor-0-indexed patterns per canonical class.

REP_F17DUMP             Sample the "forced 17th factor" infertility obstruction; also refreshes a
                        live monitor file (f17_mon.txt) every ~5s.
REP_F17SUB=<stride>     Histogram subsample stride for F17DUMP.
REP_FANOUT=1            Per-depth tally of the tree shape (candidates and representatives per
                        expanded node), printed with the progress rows.

REP_VOICE=<any but 0>   Speak the outcome once at the end (Windows SAPI): "K <N> task exit
                        ok" after End of job, "Oops. K <N> task error" on any other exit; a crash
                        stays silent. Off by default; env_reset.bat does NOT clear it, so it can be
                        set once in a shell to hear every case bat finish.
```

## 8. Which engine reads what

```text
  One engine since 2026-10-25: knA2 reads every variable in this file at every N. Retired with the
  per-N engines (still in git history): REP_CANONDIAG REP_EVENTFILE REP_PRINTRAW
  REP_PRINTEACH REP_DIAGPROBE REP_DIAGSUB REP_PATLEVEL REP_PATSUB REP_ROW4 REP_SWAPCAP
  REP_NOPRUNE REP_F2COUNT REP_F2FIXED REP_F2MAX REP_NAV* REP_COMPLETEPREFIX REP_CPCAP
  REP_PATH* REP_LOCATE REP_CLASSBLOCKS REP_CMAP REP_CANONCOUNT REP_CANONKEY REP_CANONFILE
  REP_REPSCHECK REP_DUMPREPS REP_DUMPFIRST.
```

## 9. Worked settings

```text
The bats under runs and regression are the maintained examples. Each one first CALLs
env_reset.bat, which clears every variable listed above, and then sets only what its case
needs. The two most-used combinations:

  K18 |Aut| > 2 census
      REP_ORDERS=4,5,7,17,V4,3

  K18 |Aut| = 2, type 9, a = 4 stratum, fast flag set
      REP_ORDER=2  REP_ONLYTYPES=9  REP_LEVEL=3  REP_PRUNELEVEL=1
      REP_F3A=4  REP_F3COUNT=1  REP_F3COMPLETE=1  REP_F3START=<c>
      REP_PATAPPLY=oneperpair  REP_TYPEMASK=1  REP_SGEN=1  REP_DIAGPRUNE=8:14
```
