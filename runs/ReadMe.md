# runs -- one folder per computation

Each case folder holds a `run.bat` and a `ReadMe.md` with what it searches, what it should
find and how long it takes. This file defines the notation those ReadMe files and the logs
share. Notation used by one graph only is in that graph's folder (`K18/ReadMe.md` for the
`t8`, `t9` and `a` of the K18 order-2 cases).

Each `run.bat` sets N and the thread count in `RUNARGS` and starts `p1f.exe [N] [kThreads]`
(N any even number 14..32); everything else is environment variables, all documented in
[`docs/env_variables.txt`](../docs/env_variables.txt).

## Case names

`KnAut<k>` searches the classes with an automorphism of order `k` (`K22Aut7`: K22, order 7).
`KnAut<k>-All` runs every leg of a whole `|Aut| > k-1` classification in one go.
`KnAut<G>` with a group name (`K22AutS3`, `K22AutV4`, `K22AutGroups`) searches by a group
that no single element generates.

## Types

An automorphism is a relabeling of the N players; its **type** lists the cycle lengths with
their multiplicities. `4⁴ 1²` is four 4-cycles and two fixed players; `25 1` is one 25-cycle
and one fixed player (exponent 1 omitted). Fixed players are always written, so `3⁵ 1` and
`3⁵` are different statements. The order is the lcm of the cycle lengths.

## Group names

| Name | Group | Order |
|---|---|---|
| `Ck`, or just `k` | cyclic group, one element of order k (the "order k" legs) | k |
| `V4` = C2xC2 | Klein four-group: three commuting involutions | 4 |
| `S3` | symmetric group on 3 letters (= D6) | 6 |
| `D8` | dihedral group of the square | 8 |
| `Q8` | quaternion group {±1, ±i, ±j, ±k} | 8 |
| `C4xC2` | direct product of C4 and C2 | 8 |
| `C2xC2xC2` | elementary abelian group of order 8 | 8 |
| `E9` = C3xC3 | elementary abelian group of order 9 | 9 |
| `D10` | dihedral group of the pentagon | 10 |
| `A4` | alternating group on 4 letters | 12 |
| `AGL(1,p)` | affine maps x -> ax + b mod p (catalogue remarks) | p(p-1) |

`Dm` is the dihedral group of order m (not of the m-gon).

## Shapes

A group leg is swept one **shape** at a time: how the group's orbits on the N players
split by stabilizer. The log prints the shape in the second column.

- `V4 fx<a>,<b>,<c> F<n>` -- the three involutions fix a, b, c players; n free orbits of 4.
- `S3 t<t> d<d> R<R> f<f>` -- R regular orbits (6 players), t orbits of 3 (stabilizer C2),
  d orbits of 2 (stabilizer C3), f fixed players; 6R + 3t + 2d + f = N.
- `E9 k<k1>,<k2>,<k3>,<k4> R<R> f<f>` -- R regular orbits (9), k_i orbits of 3 fixed by
  the i-th of the four C3 subgroups, f fixed players.
- The generic groups (D8, Q8, C4xC2, C2xC2xC2, A4, D10) print `<name>` and one token per
  orbit kind, `<size><letter>^<count>`: the orbit size, a letter telling apart orbits of the
  same size with different stabilizers, and how many such orbits (`D8 8^2 4a 1^2`).

A shape marked `skipped` is empty by a theorem in [`docs/theorems.md`](../docs/theorems.md)
(the fixed-point lemma `f <= 2`, the parity theorems, the Klein-four theorem).
