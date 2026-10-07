# Theorems the search relies on

The engine skips cells that a theorem proves empty, and the case ReadMes refer to these results
by the names below. None of them is ours: all are due to E. C. Ihrig, *Symmetry groups related to
the construction of perfect one factorizations of K<sub>2n</sub>*, J. Combin. Theory Ser. B 40
(1986) 121–151, and several were generalized by Duncan and Ihrig, Rocky Mountain J. Math. 30
(2000) 529–553.

| Name used here | Statement | Ihrig 1986 |
|---|---|---|
| involution fixed-point lemma | an involutory automorphism fixes 0 or 2 vertices | Lemma 3.5 (from Thm 3.3) |
| order-2 parity theorem | a fixed-point-free involution needs 2n ≡ 2 (mod 4) | Cor 3.9 |
| order-4 parity theorem | an automorphism of order divisible by 4 needs 2n ≡ 2 (mod 4) | immediate from Lemma 3.5 and Cor 3.9 |
| Klein-four theorem | for 2n ≡ 0 (mod 4), no Klein four-group of automorphisms | Thm 6.2(b) |
| odd-prime parity theorem | an automorphism of odd prime order p with an odd number of p-cycles needs p \| 2n−1 | Thm 3.3 + Cor 3.4(b) |
| dichotomy theorem | a fixed-point-free involution (n odd) fixes 1 factor, containing all n of its transposition edges, or n factors, each containing exactly one | Thm 3.11 + 3.13(b) |
| two-fixed-points theorem | an involution with 2 fixed points fixes exactly one factor: its transposition edges plus the edge joining the fixed points | Thm 3.10 + 3.13(a) |

`runs/K20/K20Aut4-NoSkip` re-derives the K20 result by search instead of relying on these.
