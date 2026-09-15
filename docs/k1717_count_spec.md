# Spec -- how many P1Fs of K17,17 the K18 catalogue yields, and which are atomic

STATUS: DONE 2026-09-06, same day. Prompted by Ian Wanless's reply to Andrei of that day:
"it would be good to count how many of K17,17 you have just found, and which of those produce
atomic Latin squares. You get these things essentially for free."

**Result: the 10,710 classes yield 104,088 pairwise non-isomorphic P1Fs of K17,17.**
Tool `tools/k1717_count.pl` (Perl, not C++: it lives with the other catalogue helpers);
results in `AllResults/K1717_from_K18/` -- `K1717_from_K18.txt` in the catalogue's own shape
(title, Total, histogram by |Aut|, then `#k |Aut| = a  orbits = o  roots = r ...` per record in
catalogue order, so that (record, root) names each K17,17 P1F once) plus a ReadMe; 4 min 42 s
for the whole catalogue.

| \|Aut\| | classes | orbits per class | sum |
|---|---|---|---|
| 2 | 10,179 | 10 (two-fixed, 9,447 classes) or 9 (fixed-point-free, 732) | 101,058 |
| 3 | 351 | 6 | 2,106 |
| 4 | 144 | 5 (48) or 6 (96) | 816 |
| 8 | 22 | 3 (13) or 4 (9) | 75 |
| 16 | 12 | 2 (7) or 3 (5) | 29 |
| 17 | 1 | 2 | 2 |
| 272 | 1 | 2 | 2 |
| **total** | **10,710** | | **104,088** |

Controls, all passed: K10 -> 1 orbit (|Aut| = 40, vertex-transitive), the Wanless-Ihrig
"only one from K10"; #9323 -> 2 orbits and #9527 -> 3, the main-class counts Bryant-Maenhaut-
Wanless get from the same two P1Fs; the |Aut| = 2 arithmetic 9,447 x 10 + 732 x 9 = 101,058;
every record re-verified as a P1F and every computed |Aut| equal to its header. K14 gives 94
(21 classes), K16 gives 686 (89 classes, all 59 |Aut| = 2 classes two-fixed, as the order-2
parity theorem requires). The atomic correction of section 5 is zero, so 104,088 is exact.
The paragraph for the paper went into P1F_K18_v23.tex, Section 5.6.

The rest of this file is the spec as written before the run.

Every mathematical statement below is taken from Wanless & Ihrig, *Symmetries that Latin
squares inherit from 1-factorizations*, J. Combin. Des. 13 (2005) 157-172 (open PDF at
users.monash.edu.au/~iwanless/papers/inheritsymJCD.pdf), and was read from that paper, not
recalled.

## 1. What is being counted

**The K-construction.** Take a P1F F of K18 and a root vertex v. Delete v; on the 17 remaining
vertices define L(u,w) = the factor containing edge uw, indexed by its v-neighbour, and
L(u,u) = u. L is a symmetric idempotent Latin square of order 17, equivalently a 1-factorization
of K17,17 (rows and columns are the two sides, symbols are the factors). This is exactly the
I(F,j) folding that `Tt4/tools/atomic_latin_catalog.cpp` already performs; the root is the deleted
vertex.

**Theorem 1 (the "if" half is Laufer, Ars Combin. 9 (1980) 43-46).** L(F,v) is a perfect
1-factorization of K17,17 if and only if F is perfect. So every catalogue class gives a P1F of
K17,17 at every one of its 18 roots.

**Theorem 16.** L(F,v) and L(F,w) lie in the same main class if and only if some automorphism
of F maps v to w. And the main class of L determines the isomorphism class of F (the converse is
false). So distinct classes never collide, and within one class the distinct main classes are
in bijection with the orbits of Aut(F) on the 18 vertices.

**From main classes to bipartite P1Fs.** Two 1-factorizations of K_{n,n} are isomorphic when one
is carried to the other by relabelling the two sides, swapping the sides, and relabelling the
factors: on squares, isotopy plus transposition, i.e. two of the six conjugates. A main class
therefore holds up to three isomorphism classes of bipartite 1-factorizations, one per
transpose-pair of conjugates -- but a conjugate is a *perfect* 1-factorization only if it is
symbol-Hamiltonian, which for the other two pairs means L is row-Hamiltonian, i.e. **atomic**.
For a non-atomic L the count is one per main class. For an atomic L it is up to three, and
exactly one if all six conjugates are isotopic to L.

**The formula.** With the atomic exceptions handled in section 5,

    #(P1Fs of K17,17 from the catalogue) = SUM over the 10,710 classes F of |V / Aut(F)|,

the number of orbits of Aut(F) on the 18 vertices.

## 2. What the number means, and what it does not

- It is exact for what it counts: the K-construction images of the classes with |Aut| > 1.
- It is a lower bound on the K-construction images of all P1Fs of K18: every |Aut| = 1 class,
  of which nothing is known, would add 18 more.
- It says nothing about P1Fs of K17,17 that are not K-construction images. Wanless & Ihrig note
  that K9,9 has 37 and only one comes from K10, so those are expected to be the majority.
- Expected size: the 9,447 two-fixed |Aut| = 2 classes give 10 orbits each, the 732
  fixed-point-free ones 9 each, and the 531 classes with |Aut| > 2 fewer; about 1.0e5 in all.
  By contrast the constructions in the literature give a handful per prime.

## 3. The tool

One program, `tools/k1717_count.cpp` in Tt4 (size-generic like its siblings, order read from the
data), reading the KnA2 catalogue format directly (`AllResults/K18_P1F_aut_gt1.txt`: a `#k |Aut| = a`
header and 17 quoted factor rows per record).

Per record:
1. Read F. Verify it is a P1F (every pair of factors a single 18-cycle) -- the same check the
   atomic tool makes; a failure stops the run.
2. Compute Aut(F) as an explicit list of vertex permutations. |Aut| <= 272, so enumerate: the
   image of the factor set must be the factor set, which the existing `p1f_overlap --aut-only`
   search already does in seconds per catalogue; reuse that routine. Check the list size against
   the `|Aut|` in the header -- a mismatch stops the run.
3. Compute the orbits of that group on the 18 vertices; record the orbit count and the orbit
   sizes.
4. (Optional, `--verify k`) for every k-th record, fold L(F,v) at one representative v per orbit
   and confirm with `paratopy_latin` that representatives of different orbits are NOT paratopic
   and that two roots in the same orbit ARE. This is Theorem 16 checked on our own data.

Output: one line per record (`#k |Aut| orbits sizes`), then a table by |Aut| stratum with the
class count, the orbit-count histogram, and the stratum sum, then the grand total.

## 4. Controls, before any number is quoted

- **K10.** The unique P1F of K10 has a vertex-transitive automorphism group: 1 orbit, so 1
  main class. Wanless & Ihrig: "37 non-isomorphic P1Fs of K9,9 and only one comes from a P1F of
  K10". The tool must print 1.
- **The two atomic K18 classes.** Catalogue #9323 (AGL(1,17), |Aut| = 272) has orbits {v_inf} and
  the other 17, so 2; #9527 (|Aut| = 16, cycle type 16.1.1) has 3. Bryant, Maenhaut & Wanless
  (JCTA 113 (2006)) obtain 2 and 3 main classes from exactly these two P1Fs by choosing roots,
  and our own nu computation partitioned the atomic roots along these orbits. Both numbers are
  already on record; the tool must reproduce them.
- **|Aut| = 2 arithmetic.** A fixed-point-free involution gives 9 orbits, a two-fixed one 10.
  The stratum sum must equal 9 x (t9 count) + 10 x (t8 count) with the t8/t9 split the census
  already knows (732 / 9,447).
- **K14 and K16 catalogues** run without error and with orbit counts consistent with the known
  |Aut| values; no external oracle is claimed for them.

## 5. The atomic half -- done

Run over all 10,710 classes at all 18 roots (2026-08-27, KnA2 AllResults): exactly two atomic
squares of order 17, #9323 at its infinity vertex (the classical AGL(1,17) square) and #9527 at
one root (Bryant-Maenhaut-Wanless F^B), both published. In both, all six conjugates are isotopic
(nu = 6 |Atop|: 27,744 = 6 x 4,624 and 96 = 6 x 16), so neither adds a second or third bipartite
class. Hence no correction to the formula of section 1 is needed for K18, and the answer to
Wanless's second question is: two, both known, none new. The K20 analogue is also on record:
four atomic classes, eight main classes, seven published.

## 6. Deliverable

- The results folder `AllResults/K1717_from_K18/`: the list in catalogue shape and a ReadMe.
- One sentence for the paper, in the atomic-squares section: "By the K-construction the
  catalogue yields N pairwise non-isomorphic perfect 1-factorizations of K17,17, one per
  Aut-orbit of roots (Wanless-Ihrig, Thm 16); exactly two of the resulting main classes are
  atomic, and both are known."
- Nothing in the engine changes.
