// =============================================================================
// k18a2rep.cpp — automorphism-REPRESENTATIVE method for classifying K18 Perfect
// 1-Factorizations (P1Fs) by a given automorphism order.
//
// Why this exists: the cyclic K18A2 search cannot finish small automorphism
// orders (L=4, L=2), AND it structurally undercounts even orders that have
// FIXED-POINT-FREE cycle types (e.g. order-4 4^4 2^1 = 16+2 = 18), because its
// generator forces vertex 0 to be a fixed point of the automorphism. This module
// is unseeded and complete: for ONE representative permutation alpha per cycle
// TYPE of the target order, it finds ALL alpha-invariant P1Fs by an exact cover
// of the 153 edges with alpha-invariant factor-orbits, then deduplicates across
// types with a labeling-independent canonical key.
//
// Validated (standalone) against known counts: K16 order-5 -> 6, order-7 -> 5,
// order-15 -> 1; K18 order-17 -> 2 (|Aut| 17,272). Isomorph rejection is by
// ORDERLY GENERATION: at every cover level the candidate factor-orbits covering
// the lowest uncovered edge are deduplicated by the current centralizer
// stabilizer G, and each chosen orbit O recurses with its sub-stabilizer
// Stab_G(O). Sound for class counting because any g in G = Stab(partial cover)
// is a class-preserving bijection between sibling subtrees; combined with the
// shrinking stabilizer chain this collapses the dense high-centralizer types
// (e.g. order-4 4^4 2^1) that the previous naive lex-leader prune could not
// terminate. Types whose centralizer exceeds the enumeration cap (sparse, huge
// S_k on fixed points) fall back to plain recursion + the global canonical key.
//
// Parallelism: within each cycle type, the independent first-factor-orbit
// branches are fanned out across `kThreads` worker threads, each with its own
// search state; the per-type/global dedup is merged after the workers join.
//
// Integration rules honored here:
//   * The ONLY entry point is the private member K18A2::runRepresentativeMethod,
//     called internally from K18A2::runExhaustiveSearch. Everything else lives in
//     the anonymous namespace below, so nothing new has external linkage.
//   * Worker count comes from K18A2::kThreads.
//   * Every print is gated by K18A2::m_bPrint (forwarded as g_bprint).
// =============================================================================

#include "k18a2.h"
#include "cgtEngine.h"   // shared computational-group-theory engine (BSGS, edgeStabGens, setwiseStab)
#include "logTable.h"   // the run log IS a table -- one writer for all four engines
#include <cstdint>
#include <intrin.h>   // _BitScanForward (genM bitmask candidate iteration)
#include <random>     // std::mt19937 (REP_ESTIMATE Knuth tree-size probes)
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <array>
#include <deque>
#include <set>
#include <map>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>          // std::shared_ptr (REP_PRECALC: the seed's orbit list shared by its subtree)
#include <unordered_set>   // orbit-key dedup while building that list

std::string printWithPowers(const std::string& str);   // (allSupports.h) render "2^9" -> "2⁹" when g_useColors is on

namespace {  // everything here is file-local (no external linkage)

// ---- problem-size constants (single knob: N) --------------------------------
constexpr int N      = 18;             // players / graph vertices (K18)
constexpr int NM     = N - 1;          // 17 = factors in a 1-factorization
constexpr int NHALF  = N / 2;          // 9  = edges in one perfect matching (factor)
constexpr int NEDGES = N * (N - 1) / 2;// 153 = edges of K18
constexpr int INF    = 1 << 20;        // "no edge / not covered" sentinel (> any edge id)
constexpr uint32_t FULLMASK = (1u << N) - 1;   // bit v = vertex v (uint32 fits every sibling N)

typedef std::array<uint8_t, N> Match;  // a factor in adjacency form: m[u] = partner of u
typedef std::array<uint8_t, N> Perm;   // a vertex permutation: p[u] = image of u

// ---- edge-id tables (filled once by initEdges) ------------------------------
int     eidT[N][N];          // eidT[u][v] = canonical id (0..152) of edge {u,v}; symmetric
uint8_t edgeU[NEDGES];       // edgeU[e] = lower  endpoint of edge id e
uint8_t edgeV[NEDGES];       // edgeV[e] = higher endpoint of edge id e

void initEdges() {
    int c = 0;               // running edge id, assigned in (u<v) lexicographic order
    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++) {
            eidT[i][j] = eidT[j][i] = c;   // both orientations map to the same id
            edgeU[c] = (uint8_t)i;
            edgeV[c] = (uint8_t)j;
            c++;
        }
}

// two factors are "perfect" iff their union is a single Hamiltonian N-cycle.
// Walk the alternating a/b cycle from vertex 0; the cycle through 0 has even length 2L with
// L in 1..NHALF, so a SHORT cycle can only close at hops 1..NHALF-1. Surviving all NHALF-1
// checks forces L=NHALF (the whole N-cycle) => the last hop is guaranteed back to 0, so the
// final hop AND the final compare are unnecessary: NHALF-1 checks decide it (8 for K18, 7 K16).
inline bool is_perfect(const Match& a, const Match& b) {
    int u = 0;
    for (int i = 0; i < NHALF - 1; i++) {
        u = a[u]; u = b[u];
        if (u == 0) return false;   // closed early => more than one cycle
    }
    return true;                    // survived NHALF-1 checks => single Hamiltonian N-cycle
}

// [DIAG] cycle-length partition (sorted, descending) of the 2-regular union of two EDGE-DISJOINT
// perfect matchings a,b -- the alternating a/b cycles. A single {N} entry means a U b is one
// Hamiltonian cycle (a,b "perfect", is_perfect==true); >1 entry is the non-Hamiltonicity
// obstruction. Optionally returns the vertices of the shortest cycle (the tightest clash).
inline std::vector<int> unionCycleParts(const Match& a, const Match& b, std::vector<int>* shortCyc = nullptr) {
    bool seen[N]; for (int i = 0; i < N; i++) seen[i] = false;
    std::vector<int> parts, best;
    for (int s = 0; s < N; s++) {
        if (seen[s]) continue;
        std::vector<int> cyc; int u = s; bool useA = true;
        do { seen[u] = true; cyc.push_back(u); u = useA ? a[u] : b[u]; useA = !useA; } while (u != s);
        parts.push_back((int)cyc.size());
        if (best.empty() || cyc.size() < best.size()) best = cyc;
    }
    std::sort(parts.begin(), parts.end(), std::greater<int>());
    if (shortCyc) *shortCyc = best;
    return parts;
}

// image of a factor M under permutation alpha: (alpha M)[alpha u] = alpha(M[u])
inline Match applyAlpha(const Match& m, const uint8_t* al) {
    Match r;                 // result factor (adjacency form)
    for (int u = 0; u < N; u++) r[al[u]] = al[m[u]];
    return r;
}

// permutation composition (a o b)(x) = a(b(x)), and inverse. Group elements act on a
// factor via applyAlpha; the actions compose as
//   applyAlpha(applyAlpha(m, b), a) == applyAlpha(m, composePerm(a, b)).
inline Perm composePerm(const Perm& a, const Perm& b) { Perm r; for (int x = 0; x < N; x++) r[x] = a[b[x]]; return r; }
inline Perm invPerm(const Perm& a) { Perm r; for (int x = 0; x < N; x++) r[a[x]] = (uint8_t)x; return r; }

// [CANONV] (sibling of k14a2rep) fast C(sigma0)-minimal-image machinery = the PL-invariant canonical
// content key for a partial cover (used by REP_PATHCANON starter enumeration and the a.b.c coordinate).
// buildRhoStd sends the fpf involution sigma0 to standard (0 1)(2 3)...; canonV_fast finds the
// minimal vertex-order image over C(sigma0)=C2 wr S_{N/2} by backtracking the wreath (permute pairs +
// within-pair flip) with prefix pruning -- using ONLY sigma0's pair structure, NO Calpha enumeration, so
// it runs on the over-cap coverGen path (K18 t9 = 2^9, |C|=185M). Validated on K14 vs a brute 645k oracle.
inline Perm buildRhoStd(const uint8_t* alpha) {
    // sigma0 = alpha may have FIXED POINTS (type 2^m 1^{N-2m}, e.g. t8 = 2^8 1^2): map its m 2-cycles to
    // standard pairs {2k,2k+1} (k by ascending min-vertex), then its fixed points to the trailing slots
    // 2m..N-1. So C(sigma0) = C2 wr S_m x S_{N-2m}. (fpf t9: N-2m=0, identical to before.)
    Perm rho; bool seen[N] = { false }; int k = 0;
    for (int x = 0; x < N; x++) { if (seen[x] || alpha[x] == x) continue; int y = alpha[x]; seen[x] = seen[y] = true; rho[x] = (uint8_t)(2 * k); rho[y] = (uint8_t)(2 * k + 1); k++; }
    int base = 2 * k;                                   // fixed points follow the 2k paired vertices
    for (int x = 0; x < N; x++) if (!seen[x]) { rho[x] = (uint8_t)base++; seen[x] = true; }
    return rho;
}
inline int serVPrefix(const int* e2f, const uint8_t* inv, int L, uint8_t* out) {
    int color[NM]; for (int i = 0; i < NM; i++) color[i] = -1; int nx = 0, idx = 0;
    for (int w = 1; w < L; w++) for (int u = 0; u < w; u++) {
        int se = eidT[inv[u]][inv[w]]; int f = e2f[se];
        if (f < 0) out[idx++] = 0xFF; else { if (color[f] < 0) color[f] = nx++; out[idx++] = (uint8_t)color[f]; }
    }
    return idx;
}
inline void canonV_fast(const std::vector<Match>& chosen, const uint8_t* alpha, uint8_t* best, uint8_t* bestInv = nullptr) {
    Perm rho = buildRhoStd(alpha);
    int NF = (int)chosen.size();
    int m = 0; for (int x = 0; x < N; x++) if (alpha[x] != x && alpha[x] > x) m++;   // # sigma0 pairs
    int f = N - 2 * m;                                 // # fixed points (standard slots 2m..N-1)
    std::vector<Match> Pstd(NF);
    for (int i = 0; i < NF; i++) Pstd[i] = applyAlpha(chosen[i], rho.data());
    int e2f[NEDGES]; for (int e = 0; e < NEDGES; e++) e2f[e] = -1;
    for (int fi = 0; fi < NF; fi++) { const Match& F = Pstd[fi]; for (int u = 0; u < N; u++) { int v = F[u]; if (u < v) e2f[eidT[u][v]] = fi; } }
    uint8_t inv[N];
    for (int i = 0; i < N; i++) inv[i] = (uint8_t)i;
    serVPrefix(e2f, inv, N, best);
    if (bestInv) memcpy(bestInv, inv, N);                // winning vertex order (identity so far)
    bool usedP[NHALF + 1]; for (int i = 0; i < m; i++) usedP[i] = false;
    bool usedF[N]; for (int i = 0; i < f; i++) usedF[i] = false;
    std::function<void(int)> recF = [&](int j) {        // S_f over the fixed points, after all pairs placed
        if (j == f) { uint8_t full[NEDGES]; serVPrefix(e2f, inv, N, full); if (memcmp(full, best, NEDGES) < 0) { memcpy(best, full, NEDGES); if (bestInv) memcpy(bestInv, inv, N); } return; }
        for (int s = 0; s < f; s++) {
            if (usedF[s]) continue;
            inv[2 * m + j] = (uint8_t)(2 * m + s);       // target fixed slot 2m+j <- source fixed 2m+s
            uint8_t pre[NEDGES]; int pl = serVPrefix(e2f, inv, 2 * m + j + 1, pre);
            if (memcmp(pre, best, pl) <= 0) { usedF[s] = true; recF(j + 1); usedF[s] = false; }
        }
    };
    std::function<void(int)> rec = [&](int k) {          // C2 wr S_m over the pairs
        if (k == m) { recF(0); return; }
        for (int s = 0; s < m; s++) {
            if (usedP[s]) continue;
            for (int fl = 0; fl < 2; fl++) {
                inv[2 * k]     = (uint8_t)(fl ? 2 * s + 1 : 2 * s);
                inv[2 * k + 1] = (uint8_t)(fl ? 2 * s     : 2 * s + 1);
                uint8_t pre[NEDGES]; int pl = serVPrefix(e2f, inv, 2 * (k + 1), pre);
                if (memcmp(pre, best, pl) <= 0) { usedP[s] = true; rec(k + 1); usedP[s] = false; }
            }
        }
    };
    rec(0);
}

// Canonical serialization of the alpha-orbit of factor m (the set {m, alpha m, ...}),
// as the sorted concatenation of the orbit's factors. Used to (a) group first-orbit
// tasks into C(alpha)-orbits and (b) test whether a centralizer element fixes an orbit.
// Note g commutes with alpha, so g(orbit of m) == orbit of g(m); hence applying g to m
// and re-taking the orbit gives g's image of the whole orbit.
// gEl = the FULL list of non-identity elements of the symmetry group G (cyclic types:
// alpha^1..alpha^{order-1}; V4: {a, b, ab}), so {m} U {g m} is the whole G-orbit.
std::string orbitKey(const Match& m, const std::vector<Perm>& gEl) {
    std::vector<Match> orb;  // the G-orbit of m
    orb.reserve(gEl.size() + 1);
    orb.push_back(m);
    for (const auto& g : gEl) orb.push_back(applyAlpha(m, g.data()));
    std::sort(orb.begin(), orb.end());
    orb.erase(std::unique(orb.begin(), orb.end()), orb.end());
    std::string s; s.reserve(orb.size() * N);
    for (auto& F : orb) for (int u = 0; u < N; u++) s.push_back((char)F[u]);
    return s;
}

// [DIAG] faithful numeric id of a perfect matching = its rank in genM's lowest-vertex-ascending order
// (0 .. (N-1)!!-1). Two distinct matchings get distinct ranks. Used by COMPLETEPREFIX to print the
// start/last c VALUE (min/max rank of the {0,3}-factor's canonical orbit) alongside the distinct count,
// so start..last vs count reveals how sparsely a super-block skips across the c-space.
inline long long matchRank(const Match& F) {
    static const long long DF[] = { 1,1,3,15,105,945,10395,135135,2027025,34459425 };  // (2k-1)!! for k=0..9
    bool used[N] = { false }; long long rank = 0;
    for (int u = 0; u < N; u++) {
        if (used[u]) continue;
        used[u] = true; int v = F[u]; int before = 0, total = 0;
        for (int w = u + 1; w < N; w++) if (!used[w]) { total++; if (w < v) before++; }
        rank += (long long)before * DF[(total - 1) / 2];
        used[v] = true;
    }
    return rank;
}
// rank of the canonical (min over the G-orbit) representative -- matches orbitKey's dedup exactly.
inline long long cRankCanon(const Match& F, const std::vector<Perm>& gEl) {
    Match best = F;
    for (const auto& g : gEl) { Match img = applyAlpha(F, g.data()); if (img < best) best = img; }
    return matchRank(best);
}

// convert a factor from adjacency form to the project's "src" pair-list form
void adj_to_src(const uint8_t* adj, unsigned char* src) {
    bool visited[N] = { false };   // visited[u] = vertex u already emitted into src
    src[0] = 0; src[1] = adj[0];
    visited[0] = true; visited[adj[0]] = true;
    int idx = 2;                   // write cursor into src
    for (int u = 1; u < N; u++) {
        if (!visited[u]) {
            src[idx] = (uint8_t)u; src[idx + 1] = adj[u];
            visited[u] = true; visited[adj[u]] = true;
            idx += 2;
        }
    }
}

// [LEGACY CANON] The project canonizer pins the first two rows to the pair init() builds:
//   R1 = (0,1)(2,3)...(N-2,N-1)   and   R2 = (0,2)(1,4)(3,6)...(N-5,N-2)(N-3,N-1)
// R1 u R2 is a single N-cycle, so relabeling a frame along ITS OWN cycle onto the walk of THAT
// cycle -- p[c[t]] = legacyWalk()[t] instead of t -- lands the frame pair exactly on R1, R2. Row
// sorting then puts them first and second by itself: R1's partner of 0 is 1 and R2's is 2, smaller
// than any other row's. Unlike the serialization, this DOES move the chosen representative: the
// minimum is now taken in the legacy labeling. If R1 u R2 is not one cycle the walk falls back to
// the identity, i.e. to the cycle labeling used before.
// [RACE FIX 2026-09-10] The table used to be filled under a plain `static bool built` that was set
// TRUE before the fill began, so a second worker reaching its first canonization while the first
// was still filling could read a half-built labeling and canonize against it -- a wrong key, once,
// nondeterministically. A function-local static with an initializer is built exactly once under the
// language's own guard, and every other thread blocks until it is complete.
const uint8_t* legacyWalk() {
    static const std::array<uint8_t, N> L = [] {
        std::array<uint8_t, N> T;
        for (int u = 0; u < N; u++) T[u] = (uint8_t)u;             // fallback: the cycle labeling
        uint8_t r1[N], r2[N];
        for (int u = 0; u < N; u += 2) { r1[u] = (uint8_t)(u + 1); r1[u + 1] = (uint8_t)u; }
        for (int u = 0; u < N; u++) r2[u] = 0xFF;
        r2[0] = 2; r2[2] = 0; r2[N - 3] = (uint8_t)(N - 1); r2[N - 1] = (uint8_t)(N - 3);
        for (int j = 1; j + 3 <= N - 2; j += 2) { r2[j] = (uint8_t)(j + 3); r2[j + 3] = (uint8_t)j; }
        uint8_t w[N]; bool seen[N] = { false }; int pos = 0, cur = 0; bool ok = true;
        for (int t = 0; t < N / 2 && ok; t++) {                    // walk R1, R2, R1, R2, ...
            if (seen[cur] || r1[cur] == 0xFF) { ok = false; break; } seen[cur] = true; w[pos++] = (uint8_t)cur; cur = r1[cur];
            if (seen[cur] || r2[cur] == 0xFF) { ok = false; break; } seen[cur] = true; w[pos++] = (uint8_t)cur; cur = r2[cur];
        }
        if (ok && pos == N && cur == 0) memcpy(T.data(), w, N);    // one N-cycle -> use the legacy walk
        return T;
    }();
    return L.data();
}

// ---- centralizer C(alpha) = { g : g*alpha == alpha*g } ----------------------
// Structure: g maps each alpha-cycle onto an alpha-cycle of the SAME length, with
// a rotation. |C(alpha)| = prod over cycle-lengths l of ( l^{m_l} * m_l! ). If
// that exceeds `cap` we do NOT enumerate (return false): such types have a huge
// centralizer but their search rejects almost instantly, so breaking is
// unnecessary. Returns true iff `out` was filled with all of C(alpha).
bool buildCentralizer(const uint8_t* alpha, std::vector<Perm>& out, long long cap) {
    std::vector<std::vector<int>> cyc;   // cyc[k] = vertices of cycle k, in alpha order
    bool vis[N] = { false };             // vis[u] = vertex u already assigned to a cycle
    for (int i = 0; i < N; i++) {
        if (vis[i]) continue;
        std::vector<int> c;              // the cycle currently being traced
        int x = i;
        do { vis[x] = true; c.push_back(x); x = alpha[x]; } while (x != i);
        cyc.push_back(c);
    }
    std::vector<int> lens;               // lens[k] = length of cycle k
    for (auto& c : cyc) lens.push_back((int)c.size());

    // size check against the cap (avoid building a multi-million-element group)
    {
        std::vector<int> uq;             // distinct cycle lengths present
        for (int L : lens) if (std::find(uq.begin(), uq.end(), L) == uq.end()) uq.push_back(L);
        long long sz = 1;                // running |C(alpha)| product
        for (int L : uq) {
            int m = 0; for (int x : lens) if (x == L) m++;   // m = #cycles of length L
            for (int i = 0; i < m; i++)  { sz *= L; if (sz > cap) { out.clear(); return false; } } // l^m
            for (int i = 2; i <= m; i++) { sz *= i; if (sz > cap) { out.clear(); return false; } } // m!
        }
    }

    std::vector<int> uniqLens;           // distinct cycle lengths (enumeration classes)
    for (int L : lens) if (std::find(uniqLens.begin(), uniqLens.end(), L) == uniqLens.end()) uniqLens.push_back(L);
    out.clear();
    Perm g; for (int i = 0; i < N; i++) g[i] = (uint8_t)i;   // permutation being built (start = identity)

    std::function<void(int)> recLen = [&](int li) {          // recurse over length classes
        if (li == (int)uniqLens.size()) { out.push_back(g); return; }
        int L = uniqLens[li];
        std::vector<int> idx;            // indices of the cycles having this length L
        for (int k = 0; k < (int)cyc.size(); k++) if (lens[k] == L) idx.push_back(k);
        int m = (int)idx.size();         // number of length-L cycles
        std::vector<int> perm(m);        // perm[s] = which length-L cycle source s maps to
        for (int i = 0; i < m; i++) perm[i] = i;
        std::vector<int> off(m, 0);      // off[s] = rotation offset applied to source cycle s

        std::function<void(int)> recOff = [&](int t) {       // choose offsets per source cycle
            if (t == m) {
                for (int s = 0; s < m; s++) {
                    const std::vector<int>& src = cyc[idx[s]];
                    const std::vector<int>& tgt = cyc[idx[perm[s]]];
                    for (int j = 0; j < L; j++) g[src[j]] = (uint8_t)tgt[(j + off[s]) % L];
                }
                recLen(li + 1);
                return;
            }
            for (int o = 0; o < L; o++) { off[t] = o; recOff(t + 1); }
        };
        std::function<void(int)> recPerm = [&](int t) {      // choose the cycle->cycle bijection
            if (t == m) { recOff(0); return; }
            for (int v = t; v < m; v++) { std::swap(perm[t], perm[v]); recPerm(t + 1); std::swap(perm[t], perm[v]); }
        };
        recPerm(0);
    };
    recLen(0);
    return true;
}

// A small GENERATING SET of C(alpha) (always cheap to build, even when C(alpha) itself is far
// too large to enumerate). Used to walk C(alpha)-orbits of tasks for the over-cap types whose
// centralizer is dominated by the S_k symmetry on alpha's fixed points (k! is astronomical but
// trivially generated). For each cycle-length class l with m cycles, the wreath C_l wr S_m is
// generated by: rotate one cycle (if l>1), swap two cycles, and cycle all m cycles.
void buildGenerators(const uint8_t* alpha, std::vector<Perm>& gens) {
    gens.clear();
    std::vector<std::vector<int>> cyc;   // cyc[k] = vertices of cycle k, in alpha order
    bool vis[N] = { false };
    for (int i = 0; i < N; i++) {
        if (vis[i]) continue;
        std::vector<int> c; int x = i;
        do { vis[x] = true; c.push_back(x); x = alpha[x]; } while (x != i);
        cyc.push_back(c);
    }
    std::vector<int> lens; for (auto& c : cyc) lens.push_back((int)c.size());
    std::vector<int> uniqLens;
    for (int L : lens) if (std::find(uniqLens.begin(), uniqLens.end(), L) == uniqLens.end()) uniqLens.push_back(L);
    auto identityPerm = []() { Perm g; for (int i = 0; i < N; i++) g[i] = (uint8_t)i; return g; };
    for (int L : uniqLens) {
        std::vector<int> idx;            // cycle indices of this length
        for (int k = 0; k < (int)cyc.size(); k++) if (lens[k] == L) idx.push_back(k);
        int m = (int)idx.size();
        if (L > 1) {                     // rotate cycle idx[0] by one step (base C_l generator)
            Perm g = identityPerm(); const std::vector<int>& K = cyc[idx[0]];
            for (int j = 0; j < L; j++) g[K[j]] = (uint8_t)K[(j + 1) % L];
            gens.push_back(g);
        }
        if (m > 1) {                     // swap cycles idx[0],idx[1] aligned (transposition in S_m)
            Perm g = identityPerm(); const std::vector<int>& A = cyc[idx[0]]; const std::vector<int>& B = cyc[idx[1]];
            for (int j = 0; j < L; j++) { g[A[j]] = (uint8_t)B[j]; g[B[j]] = (uint8_t)A[j]; }
            gens.push_back(g);
            if (m > 2) {                 // cycle all m cycles (m-cycle in S_m): idx[s] -> idx[s+1]
                Perm h = identityPerm();
                for (int s = 0; s < m; s++) { const std::vector<int>& P = cyc[idx[s]]; const std::vector<int>& Q = cyc[idx[(s + 1) % m]]; for (int j = 0; j < L; j++) h[P[j]] = (uint8_t)Q[j]; }
                gens.push_back(h);
            }
        }
    }
}


// Partition a set of candidate factor-orbits into orbits under the group G = <gens>, used
// for the OVER-CAP types whose centralizer C(alpha) cannot be enumerated (so the group is
// only available as a small generating set). Each candidate is a factor-orbit given by a
// representative matching reps[i], its serialization keys[i], and the lookup keyIdx (orbit
// key -> index). G acts on a factor-orbit O via applyAlpha (every g in C(alpha) commutes
// with alpha, hence maps alpha-orbits to alpha-orbits); the image orbit is identified by
// orbitKey. For each G-orbit we output one representative index and a generating set of (a
// subgroup of) its setwise stabilizer Stab_G(O), obtained as the Schreier generators along
// a candidate-restricted BFS tree.
//
// Soundness for class counting: every Schreier generator lies in Stab_G(O) (it maps O to
// itself), so the recursion is always handed a genuine subgroup of the true stabilizer;
// candidate marking only ever collapses two orbits that are genuine G-images of one another;
// and the global canonKey is the final dedup. The BFS is restricted to the candidate set, so
// the stabilizer it recovers may be an under-approximation of Stab_G(O) -- that can only cost
// extra search work, never a missed class.
void schreierDedup(const std::vector<Match>& reps, const std::vector<std::string>& keys,
                   const std::unordered_map<std::string, int>& keyIdx,
                   const std::vector<Perm>& gens, const std::vector<Perm>& gEl,
                   std::vector<int>& outRepIdx, std::vector<std::vector<Perm>>& outStab) {
    (void)keys;                                          // (kept for signature symmetry/readability)
    int n = (int)reps.size();
    Perm idp; for (int x = 0; x < N; x++) idp[x] = (uint8_t)x;   // identity permutation
    std::vector<char> marked(n, 0);                      // marked[j]: j already assigned to some output orbit
    for (int i = 0; i < n; i++) {
        if (marked[i]) continue;
        std::vector<Perm> transv(n);     // transv[j] maps orbit_i -> orbit_j (valid where visited[j])
        std::vector<char> visited(n, 0);
        transv[i] = idp; visited[i] = 1; marked[i] = 1;
        std::vector<int> q; q.push_back(i);              // BFS queue of candidate indices in orbit_i's class
        std::set<Perm> stabSet;                          // distinct nontrivial Schreier gens of Stab_G(orbit_i)
        for (size_t qh = 0; qh < q.size(); qh++) {
            int x = q[qh];
            for (const auto& g : gens) {
                std::string ik = orbitKey(applyAlpha(reps[x], g.data()), gEl);
                auto it = keyIdx.find(ik);
                if (it == keyIdx.end()) continue;        // g moves this orbit out of the candidate set
                int t = it->second;
                Perm gx = composePerm(g, transv[x]);     // maps orbit_i -> orbit_t
                if (!visited[t]) { visited[t] = 1; transv[t] = gx; marked[t] = 1; q.push_back(t); }
                else { Perm s = composePerm(invPerm(transv[t]), gx); if (s != idp) stabSet.insert(s); }  // in Stab_G(orbit_i)
            }
        }
        outRepIdx.push_back(i);
        outStab.emplace_back(cgt::reduceGens<N>(std::vector<Perm>(stabSet.begin(), stabSet.end())));  // bound generator growth
    }
}

// ---- read-only data shared by all worker threads for one cycle type ---------
struct RepShared {
    uint8_t alpha[N];                 // representative automorphism (vertex perm)
    int order = 4;                    // target automorphism order
    std::vector<Perm> gElems;         // ALL non-identity elements of the symmetry group G
                                      // (cyclic: alpha^1..alpha^{order-1}; V4: {a, b, ab});
                                      // orbit building and image pruning iterate this list.
    // genM group-image freeness precompute (built once per type from gElems, which is static
    // per type). For each candidate edge (u,v), u<v, genM must verify no non-fixing group image
    // (g[u],g[v]) is already covered; that image list is fixed, so precompute it as CSR keyed by
    // edge id e = u*N+v. genM then tests coveredBits[au] & (1u<<av) over the packed list -- no
    // g[] lookups, no fix-edge branch per candidate (kills the ~7% group-image loop). Each
    // entry packs (au<<8)|av. Same source for every sibling N (sibling-code rule).
    std::vector<uint32_t> imgEdgeOff;   // size N*N+1: CSR offsets, slot e = u*N+v
    std::vector<uint16_t> imgEdge;      // packed (au<<8)|av for non-fixing images of edge e
    void buildImgEdges() {
        const int NN = N * N;
        std::vector<uint32_t> cnt(NN, 0);
        for (int u = 0; u < N; u++)
            for (int v = u + 1; v < N; v++) {
                int c = 0;
                for (const auto& g : gElems) {
                    int au = g[u], av = g[v];
                    if ((au == u && av == v) || (au == v && av == u)) continue;   // element fixes {u,v}
                    c++;
                }
                cnt[u * N + v] = c;
            }
        imgEdgeOff.assign(NN + 1, 0);
        for (int e = 0; e < NN; e++) imgEdgeOff[e + 1] = imgEdgeOff[e] + cnt[e];
        imgEdge.assign(imgEdgeOff[NN], 0);
        for (int u = 0; u < N; u++)
            for (int v = u + 1; v < N; v++) {
                uint32_t p = imgEdgeOff[u * N + v];
                for (const auto& g : gElems) {
                    int au = g[u], av = g[v];
                    if ((au == u && av == v) || (au == v && av == u)) continue;
                    imgEdge[p++] = (uint16_t)((au << 8) | av);
                }
            }
    }
    std::vector<Perm> Calpha;         // all elements of C(alpha) (empty if over the cap)
    std::vector<Perm> Cinv;           // Cinv[i] = inverse permutation of Calpha[i]
    std::vector<int>  rootActive;     // {0,1,...,|Calpha|-1}: the root active-gamma set
    std::vector<Perm> rootCgens;      // generators of C(alpha) (over-cap path)
    cgt::BSGS<N> rootBSGS;            // BSGS of C(alpha), built once per type; reused by setwiseStab each node
    // V4 shard filters (host params v4Row*/v4*), SEARCH-FRAME semantics: the factor
    // pairing 0-with-v4Row[i] must pair 1-with-v4Val[i]. Compacted (first nV4 slots).
    // WARNING: the predicate is NOT C(alpha)-invariant, so a filtered run is a SHARD
    // of the labeled search, not a clean class partition -- shard-union completeness
    // must be validated empirically (see run20a2f.bat header) before census use.
    uint8_t v4Row[4] = { 0, 0, 0, 0 };
    uint8_t v4Val[4] = { 0, 0, 0, 0 };
    int nV4 = 0;
};

// One parallel work item: a representative first-factor-orbit (the orbit of the factor
// containing edge (0,1)) plus the centralizer subgroup that fixes that orbit. The subtree
// for this orbit only needs `stab` for symmetry-breaking (Stab is far smaller than C(alpha),
// which is what makes the dense types fast).
struct Task {
    Match m0;                   // the factor containing edge (0,1) (defines the first orbit)
    std::vector<int> stab;      // ENUMERABLE path: indices into Calpha of the gammas fixing this orbit
    std::vector<Perm> stabGens; // OVER-CAP path: a generating set of Stab_{C(alpha)}(this orbit)
};

// ---- shared progress/print state (one classification run) -------------------
std::atomic<long long> g_nodes{ 0 };  // total exact-cover nodes across all workers
std::atomic<long long> g_emits{ 0 };  // total complete P1F covers produced (results); new = results - rejected
// [FANOUT] REP_FANOUT=1: per-depth tally of the tree shape -- at each expanded node, indexed by the
// number of committed factors: nodes expanded, rows the generator emitted, distinct orbits among
// them. Printed with every progress row. Diagnostic only; off by default and then costs nothing.
bool g_fanOn = false;
std::atomic<long long> g_fanNodes[NM + 2], g_fanCands[NM + 2], g_fanReps[NM + 2];
inline void fanTally(int nchosen, size_t cands, size_t reps) {
    if (!g_fanOn) return;
    g_fanNodes[nchosen].fetch_add(1, std::memory_order_relaxed);
    g_fanCands[nchosen].fetch_add((long long)cands, std::memory_order_relaxed);
    g_fanReps[nchosen].fetch_add((long long)reps, std::memory_order_relaxed);
}
inline void fanPrint() {
    if (!g_fanOn) return;
    printf("[FANOUT] factors committed: nodes expanded / rows generated / distinct orbits / orbits per node\n");
    for (int d = 0; d <= NM; d++) {
        long long n = g_fanNodes[d].load(); if (!n) continue;
        long long c = g_fanCands[d].load(), r = g_fanReps[d].load();
        printf("[FANOUT] %2d: %lld / %lld / %lld / %.2f\n", d, n, c, r, (double)r / (double)n);
    }
    fflush(stdout);
}
std::atomic<int> g_maxDepth{ 0 };     // diagnostic (observational only): deepest partial cover reached in the current block; printed by F3COMPLETE/NAVIGATE, never read by the search
// [DIAG] REP_F17DUMP: sample the "forced 17th factor" infertility obstruction (TBD1 "check color").
// At a coverGen node with NM-1 factors committed the 9 uncovered edges form the FORCED last factor
// F17; an infertile a=4 block dies because F17 is non-Hamiltonian ("not perfect") with some earlier
// factor. This counts forced-17th nodes reached and, once per 60s, prints one node's obstruction
// signature (clashing factor(s), the cycle partition of F17 U F_k, the shortest clash cycle) plus a
// running histogram of the partition shape -- to see whether the obstruction is UNIFORM/local.
bool g_f17dump = false;
std::atomic<long long> g_f17Total{ 0 };             // forced-17th (size NM-1) coverGen nodes reached
std::atomic<long long> g_f17NextSec{ 60 };          // next 60s print boundary (claimed via CAS)
std::mutex g_f17_mtx;                               // guards g_f17Hist + the sampled printf
std::map<std::string, long long> g_f17Hist;         // sampled obstruction partition (e.g. "9+9") -> count
std::map<int, long long> g_f17ClashHist;            // sampled: # earlier factors F17 clashes with -> count
long long g_f17Sub = 16;                            // REP_F17SUB: accumulate the histogram every g_f17Sub-th forced-17th node
std::atomic<long long> g_maxKids{ 0 };              // [DIAG] max children materialized by one splitNode call (completeBlockF3 memory probe)
std::atomic<long long> g_maxNQ{ 0 };               // [DIAG] max completeBlockF3 work-queue size reached (memory probe)
std::atomic<long long> g_monNextSec{ 5 };          // [DIAG] next 5s boundary for the kill-surviving F17 monitor dump
// [DIAG] REP_PATSTAT: tally the sigma0 stay/swap orbit-pattern (S=sigma0-fixed factor, W=sigma0-paired) of
// completed P1Fs and of forced-17th (level NM-1) partials, in commit order. Answers "which of the a=4 patterns
// does the search actually reach / realize". Optional mid-level strided sample via REP_PATLEVEL/REP_PATSUB.
bool g_patStat = false;
// [FILTER] REP_SWAPCAP: a=4 has exactly 4 sigma0-swap-pairs (9 stay + 4 pairs, forced by the dichotomy).
// Any partial that commits a 5th pair (>8 sigma0-non-symmetric factors) cannot complete to a=4 -> prune.
// SOUND for a=4 blocks only (F1 = 1-diagonal). Measured ~49% of deep nodes violate this (PATSTAT L13).
bool g_swapCap = false;
// [FILTER] REP_PATONLY=<S/W string>: restrict the search to ONE full sigma0 stay/swap pattern. At each node prune
// unless the committed pattern patSeq() is a prefix of the target. Measures one pattern's slice + whether it completes.
std::string g_patOnly;                                       // REP_PATTERN=<S/W string>: neighbour-0 target (empty = calculate from rows 2,3)
bool g_patApply = false;                                     // REP_PATAPPLY: enable the neighbour-0 pattern filter (default off)
bool g_patPair = false;                                      // REP_PATPAIR: one-per-pair filter -- within each sigma0 position-pair {2k,2k+1} the two committed factors must differ (one S, one W). Complete |Aut|=2 filter, no fixed pattern.
// REP_ROW4=S|W: force the sigma0 type of ROW 4 (the factor covering {0,4}); unset = any, the normal search.
// Row 4 is the first row whose type is free in a block whose sigma0F3 lands at position >= 6 (in the q3=5
// stratum one-per-pair already forces it to S). Exists to test whether a W at row 4 can ever complete.
char g_row4 = 0;
bool g_sGen = false;                                         // REP_SGEN: enumerate a FORCED-S row on the sigma0 quotient (genS) instead of filtering genM's output. Requires REP_TYPEMASK.
bool g_typeMask = false;                                     // REP_TYPEMASK: apply the one-per-pair rule at GENERATION time (mask the partner of vertex 1 in genM) instead of rejecting the built orbit in patPairOK. Same classes, fewer nodes.
std::string g_curBlock;    // "4.0.288" while that block is being completed (F3COMPLETE); empty otherwise.
                           // Written by the single-threaded block loop before it calls completeBlockF3,
                           // read by the workers inside that call -- never concurrent.

// ===========================================================================================
// REP_OWNER -- block ownership, the stateless replacement for a global "already found" set.
//
// A class is reachable from MANY blocks of a column (about 60 of them for a=4), so a sweep
// re-finds it over and over. Deciding "is this new?" by asking a set of everything found so
// far makes a run depend on every earlier run: that is what a preload baseline is for, and it
// is why two separately-run block ranges can each record the same class.
//
// Ownership removes the question. Every class has a canonical home in a column -- the MINIMUM
// block index over all of its column labelings -- and that value is a property of the class
// and the column, not of the labeling the search happened to find. So every block computes the
// same owner, exactly one block matches, and a range [lo,hi) writes exactly the classes owned
// in it: disjoint by construction, no baseline, no coordination.
//
//   owner(R) = min { c : block c holds R and c is a first-occurrence (canonical) block }
//
// The identity relabeling is always in the enumeration below (X = PFX[0] is sigma0-fixed with
// the column's diagonal count, Y = PFX[1], pi = id, and the anchor factor is this block's own
// orbit representative), so owner <= the current block ALWAYS. That is asserted, not assumed:
// if it ever fails the owner function is wrong and the run is silently losing classes, so the
// run aborts instead.
//
// The canonical restriction is provably redundant -- the driver keeps the FIRST occurrence of
// each canonical key, i.e. the lowest raw c in its key class, and a duplicate starter holds
// exactly the same classes as its twin, so the raw minimum is already canonical. It is kept
// anyway: it costs one byte per block and one branch, and it makes correctness independent of
// that ordering detail instead of resting on it.
//
// Ported from an offline Perl block-map oracle, which this must agree with.
// ===========================================================================================
// Ownership is the DEFAULT: every k18 REP_F3COMPLETE run dedups with no variable set at all.
// Opt-in made silence mean "record every re-find", which is the answer nobody wants -- a range
// that re-found a class owned by another block wrote it anyway, and the run read like a
// discovery. Silence now means "write only what this range owns". REP_OWNER=0 turns it back
// off, which is what a test that must see the raw re-find sets
// (regression\K18-t9-a4-block1777-1).
bool g_ownerMode = false;        // active: reject any cover this block does not own
bool g_ownerVerbose = false;     // REP_OWNERALL: one [OWNER] line per rejected cover

struct OwnerColumn {             // column context: fixed before the block loop, read-only to the workers
    std::vector<Match> pfx;      // the committed prefix (sigma0 orbit + F2 orbit), constant across blocks
    const uint8_t* sig0 = nullptr;                                  // the column's involution = sh.alpha
    int d1 = 0;                                                     // diagonal count of pfx[0] wrt sig0 (9 for a=0, 1 for a=4)
    int u0 = -1, v0 = -1;                                           // the level-3 anchor edge
    const std::unordered_map<std::string, int>* ckey = nullptr;     // orbitKey(F3) -> raw block c
    const std::vector<Perm>* gEl = nullptr;                         // symmetry group elements, for orbitKey
    std::vector<char> isCanon;                                      // isCanon[c]: was raw block c a first occurrence
    int a0 = 0;                                                     // the column's a, so rejects can print a.0.c
    int nfix = 0;                                                   // fixed points of sig0 (0 for t9, 2 for t8): tau must
                                                                    // have the same count, since pi tau pi^-1 == sig0 and
                                                                    // on N points that one number fixes the cycle type
};
OwnerColumn g_ownerCol;
long long g_ownerBlockC = -1;             // raw c of the block being completed (set by the block loop)
std::set<std::string> g_ownerBlockSeen;   // canonKeys already seen IN THIS BLOCK; cleared at block start
long long g_ownerSaved = 0, g_ownerRejForeign = 0, g_ownerRejInBlock = 0, g_ownerRejAut = 0;   // per-block counters

// [AUT2ONLY] The block column IS the order-2 census: its subject is the classes whose FULL
// automorphism group has order 2. A cover with |Aut| > 2 reaches this search through its order-2
// subgroup, but it is defined by -- and written by -- another case (the |Aut|>2 catalog), so this
// run must not write it as well. Same concept as the owner filter, so it is reported the same way:
// as a duplicate. Gated on REP_F3COMPLETE, NOT on order == 2 -- K14Aut3-All-1 also sweeps order 2
// and takes 16 of its 21 classes from those legs, nearly all with |Aut| > 2. There the |Aut|>2
// covers ARE the result. It is the CASE that is the census, not the order.
bool g_aut2Only = false;                  // REP_F3COMPLETE: keep only |Aut| == 2
std::map<int, long long> g_autRejHist;    // |Aut| -> distinct classes rejected by it (g_harvest_mtx)

// Every involution tau in Aut(cover) with exactly `nfix` fixed points. Colour each vertex pair by
// the factor covering it, then backtrack over tau[u] -- either u pairs with some w, or u is one of
// the fixed points -- propagating the induced factor permutation and rejecting the moment two edges
// demand contradictory factor images.
//
// `nfix` is not a filter applied afterwards, it is the column's own: pi tau pi^-1 == sig0 forces
// tau and sig0 to share a cycle type, and on N points an involution's cycle type is exactly its
// number of fixed points. nfix = 0 is t9 and takes the pairing branch only, so it enumerates what
// this function did before it learned about fixed points; nfix = 2 is t8.
static void ownerAllTaus(const std::vector<Match>& fac, int nfix, std::vector<Perm>& out) {
    out.clear();
    static thread_local int color[N][N];
    for (int u = 0; u < N; u++) for (int v = 0; v < N; v++) color[u][v] = -1;
    for (int fi = 0; fi < (int)fac.size(); fi++) { const Match& f = fac[fi]; for (int u = 0; u < N; u++) color[u][f[u]] = fi; }

    const int nf = (int)fac.size();
    std::vector<int> tau(N, -1), pi(nf, -1), pii(nf, -1);
    std::vector<std::pair<int, int>> added; added.reserve(2 * N);

    auto tryPair = [&](int u, int w) -> bool {          // extend the factor map for tau[u] = w
        for (int x = 0; x < N; x++) {
            if (tau[x] == -1 || x == u || x == w) continue;
            const int c1 = color[u][x], c2 = color[w][tau[x]];
            if (pi[c1] == -1 && pii[c2] == -1) { pi[c1] = c2; pii[c2] = c1; added.emplace_back(c1, c2); }
            else if (pi[c1] != c2 || pii[c2] != c1) return false;
        }
        const int c1 = color[u][w];                     // the edge {u,w} itself must map to itself
        if (pi[c1] == -1 && pii[c1] == -1) { pi[c1] = c1; pii[c1] = c1; added.emplace_back(c1, c1); }
        else if (pi[c1] != c1) return false;
        return true;
    };
    auto undoTo = [&](size_t mark) { while (added.size() > mark) { pi[added.back().first] = -1; pii[added.back().second] = -1; added.pop_back(); } };

    auto tryFix = [&](int u) -> bool {                  // extend the factor map for tau[u] = u
        for (int x = 0; x < N; x++) {                   // (no self-edge clause: there is no edge {u,u})
            if (tau[x] == -1 || x == u) continue;
            const int c1 = color[u][x], c2 = color[u][tau[x]];
            if (pi[c1] == -1 && pii[c2] == -1) { pi[c1] = c2; pii[c2] = c1; added.emplace_back(c1, c2); }
            else if (pi[c1] != c2 || pii[c2] != c1) return false;
        }
        return true;
    };

    int nFixed = 0;
    std::function<void(int)> solve = [&](int u) {
        if (u == N) { if (nFixed != nfix) return; Perm p; for (int t = 0; t < N; t++) p[t] = (uint8_t)tau[t]; out.push_back(p); return; }
        if (tau[u] != -1) { solve(u + 1); return; }
        if (nFixed < nfix) {                            // u is a fixed point of tau
            tau[u] = u; nFixed++;
            const size_t mark = added.size();
            if (tryFix(u)) solve(u + 1);
            undoTo(mark);
            nFixed--; tau[u] = -1;
        }
        for (int w = u + 1; w < N; w++) {               // u pairs with w
            if (tau[w] != -1) continue;
            tau[u] = w; tau[w] = u;
            const size_t mark = added.size();
            if (tryPair(u, w)) solve(u + 1);
            undoTo(mark);
            tau[u] = -1; tau[w] = -1;
        }
    };
    solve(0);
}

// The cycles of the 2-regular graph X u Y, each walked from its start along an X edge, so
// position 0 -> 1 is an X step, 1 -> 2 a Y step, and so on.
static void ownerAltCycles(const Match& X, const Match& Y, std::vector<std::vector<int>>& cyc) {
    cyc.clear();
    bool seen[N] = { false };
    for (int s = 0; s < N; s++) {
        if (seen[s]) continue;
        std::vector<int> w; w.push_back(s); seen[s] = true;
        int cur = s; bool useX = true;
        for (;;) {
            const int nx = useX ? X[cur] : Y[cur];
            if (nx == s) break;
            w.push_back(nx); seen[nx] = true; cur = nx; useX = !useX;
        }
        cyc.push_back(std::move(w));
    }
}

// Every relabeling pi carrying X u Y onto A u B with X -> A and Y -> B: match the alternating
// cycles by length, and for each matching walk the target cycle from every EVEN offset in both
// directions (even only, and the reversed walk starting at the X-partner, so position 0 still
// leaves along an A edge and the X/A alternation survives).
static void ownerIsos(const Match& X, const Match& Y, const Match& A, const Match& B, std::vector<Perm>& out) {
    out.clear();
    std::vector<std::vector<int>> sc, tc;
    ownerAltCycles(X, Y, sc);
    ownerAltCycles(A, B, tc);
    if (sc.size() != tc.size()) return;

    std::vector<char> used(tc.size(), 0);
    std::vector<int> p(N, -1);
    std::function<void(size_t)> rec = [&](size_t si) {
        if (si == sc.size()) { Perm q; for (int t = 0; t < N; t++) q[t] = (uint8_t)p[t]; out.push_back(q); return; }
        const std::vector<int>& S = sc[si];
        const int L = (int)S.size();
        for (size_t ti = 0; ti < tc.size(); ti++) {
            if (used[ti] || (int)tc[ti].size() != L) continue;
            const std::vector<int>& T = tc[ti];
            used[ti] = 1;
            for (int rot = 0; rot < L / 2; rot++) {
                for (int dir = 0; dir < 2; dir++) {
                    for (int k = 0; k < L; k++) {
                        const int tk = dir ? (((2 * rot + 1 - k) % L) + L) % L : (2 * rot + k) % L;
                        p[S[k]] = T[tk];
                    }
                    rec(si + 1);
                    for (int k = 0; k < L; k++) p[S[k]] = -1;
                }
            }
            used[ti] = 0;
        }
    };
    rec(0);
}

// The owner of `cover`: the smallest canonical block of this column holding this class, over
// every labeling of it the column can reach. Returns -1 if none was found, which cannot happen
// for a cover the search actually produced -- emit() treats it as fatal.
static long long ownerOf(const std::vector<Match>& cover) {
    const OwnerColumn& col = g_ownerCol;
    const long long INF = (long long)1 << 62;
    long long best = INF;

    std::vector<Perm> taus; ownerAllTaus(cover, col.nfix, taus);
    const Match& A = col.pfx[0];
    const Match& B = col.pfx[1];
    std::set<std::string> seenPi;                 // the same pi can arise from several (X,Y) pairs
    std::vector<Perm> isos;

    for (const Perm& tau : taus) {
        for (size_t xi = 0; xi < cover.size(); xi++) {
            const Match& X = cover[xi];
            Match img;                            // X must be tau-invariant ...
            for (int u = 0; u < N; u++) img[tau[u]] = tau[X[u]];
            if (!(img == X)) continue;
            int d = 0;                            // ... and carry the column's diagonal count
            for (int u = 0; u < N; u++) if (X[u] == tau[u]) d++;
            if (d / 2 != col.d1) continue;

            for (size_t yi = 0; yi < cover.size(); yi++) {
                if (yi == xi) continue;
                ownerIsos(X, cover[yi], A, B, isos);
                for (const Perm& p : isos) {
                    bool ok = true;               // pi tau pi^-1 == sigma0
                    for (int u = 0; u < N && ok; u++) if (p[tau[u]] != col.sig0[p[u]]) ok = false;
                    if (!ok) continue;
                    if (!seenPi.insert(std::string((const char*)p.data(), N)).second) continue;

                    std::vector<Match> im; im.reserve(cover.size());
                    std::set<Match> imSet;
                    for (const Match& F : cover) { Match g = applyAlpha(F, p.data()); imSet.insert(g); im.push_back(g); }
                    bool havePfx = true;          // the image must actually sit in this column
                    for (const Match& F : col.pfx) if (!imSet.count(F)) { havePfx = false; break; }
                    if (!havePfx) continue;

                    const Match* F3 = nullptr;    // the image factor covering the level-3 anchor names the block
                    for (const Match& F : im) if (F[col.u0] == col.v0) { F3 = &F; break; }
                    if (!F3) continue;
                    auto it = col.ckey->find(orbitKey(*F3, *col.gEl));
                    if (it == col.ckey->end()) continue;
                    const long long cc = (long long)it->second;
                    if (cc < 0 || cc >= (long long)col.isCanon.size() || !col.isCanon[cc]) continue;
                    if (cc < best) best = cc;
                }
            }
        }
    }
    return best == INF ? -1 : best;
}

std::mutex g_pat_mtx;
std::map<std::string, std::pair<long long, int>> g_patEmit;   // completion pattern -> (count, |Aut|*2)
// [DIAG] REP_PDUMP: the ROW-PAIRING INVOLUTION p of each completion, in the SEARCH frame.
// Row x is the factor covering (0,x); its sigma0 image is the row covering (0, alpha[F_x[alpha[0]]]),
// so p[x] = alpha[F_x[alpha[0]]]. p is an involution whose FIXED POINTS are the sigma0-fixed (S)
// rows -- 1 for a=0, 9 for a=4 -- and whose transpositions pair the W rows. It carries strictly more
// than the S/W string (that string is just the indicator p[x]==x, and is constant in a=0).
// Recorded per COMPLETION, not per class, so re-finds count too: the point is how many of the
// a-priori-possible pairings are actually realized.
// [DIAG] REP_PROBE1=L: at nodes with L committed factors, test the pairing invariant
//   U == sigma0(R),  R = rows not yet committed (vertices with no factor covering (0,x)),
//   U = vertices still unmatched to vertex 1.
// Every remaining row x pairs with some z and then carries (1, sigma0(z)); summed over the pairing
// that demands exactly sigma0(R), whichever pairing is chosen -- so this is pairing-INDEPENDENT and
// costs one set compare instead of (k-1)!! enumerations. A node failing it cannot complete.
// Records only; never prunes. Answers "would a look-ahead here reject anything the search does not
// already avoid?" -- if violations are 0, the closure ban already implies it and there is nothing to win.
int g_probe1 = 0;
std::atomic<long long> g_probeSeen{ 0 }, g_probeBad{ 0 };
bool g_pDump = false;
std::mutex g_pdump_mtx;
std::map<std::string, long long> g_pSeen;     // p (as "x:p[x],..") -> completions
std::map<std::string, long long> g_patF17;                    // forced-17th (16-factor) pattern -> count
std::map<std::string, long long> g_patMid;                    // REP_PATLEVEL pattern -> count (strided)
int g_patLevel = -1;                                          // REP_PATLEVEL=L: also sample pattern at L committed factors
long long g_patSub = 256;                                     // REP_PATSUB: mid-level sample stride
std::atomic<long long> g_patMidSeen{ 0 };
std::vector<long long> g_cref;                      // REP_CMAP: sorted reference c-ranks -> maps a c-rank to its 0..N index
inline long long cIdx(long long r) { return g_cref.empty() ? r : (long long)(std::lower_bound(g_cref.begin(), g_cref.end(), r) - g_cref.begin()); }
bool g_lvlStats = false;                            // REP_LEVELSTATS: per-level node-count histogram (branching/success profile)
// [DIAG] REP_DIAGPROBE=<minDepth> (+ REP_DIAGSUB=<stride>, default 1000): look-ahead probe on the
// UNCOVERED DIAGONALS. In the a=4 stratum every sigma0-fixed factor carries exactly one diagonal, so
// each still-uncovered diagonal needs at least one legal S row on the leftover graph; if any has none,
// the node is dead however deep the search would otherwise go before finding out. This only MEASURES:
// per depth it counts nodes probed and nodes where some diagonal has no candidate. A prune is worth
// building only if the hits land well above the depth the search dies at anyway (15-16 of 17).
int g_diagProbe = 0;                                // 0 = off; else probe nodes with >= this many committed factors
int g_diagSub = 1000;                               // probe every Nth qualifying node (the probe is not cheap)
// [FILTER] REP_DIAGPRUNE=<lo>[:<hi>]: the same look-ahead used as a PRUNE, at depths lo..hi (hi
// defaults to NM). SOUND for the a=4 stratum only: there every sigma0-fixed factor carries exactly one
// diagonal, so each uncovered diagonal needs a legal S row and a node without one cannot complete.
// Needs a fixed-point-free sigma0 (the probe walks sigma0 mirror pairs), like REP_TYPEMASK/REP_SGEN.
int g_diagPruneLo = 0, g_diagPruneHi = NM;
std::atomic<long long> g_diagPruned{ 0 };           // nodes killed by it
std::atomic<long long> g_diagSeen{ 0 };
std::mutex g_diag_mtx;
long long g_diagProbes[NM + 2] = { 0 };             // depth -> nodes probed
long long g_diagHits[NM + 2] = { 0 };               // depth -> nodes with an unrealizable diagonal
std::atomic<long long> g_lvlReached[NM + 2];        // nodes reached at each level (chosen.size()); branching[m]=reached[m+1]/reached[m]
// [DIAG] REP_GENSTAT: generation waste. genM builds a COMPLETE candidate matching, then
// buildAndValidateOrbit decides whether it can join `chosen` (orbit-internal is_perfect, and the
// F-vs-chosen check when the closure prune is off). made[] counts candidates built at a level,
// kept[] those that survive. made/kept is the work spent enumerating factors that were already
// doomed -- i.e. how much a cheaper up-front constraint could save at that level.
bool g_genStat = false;
std::atomic<long long> g_genMade[NM + 2];
std::atomic<long long> g_genKept[NM + 2];
int g_ovlLevel = 0;                                 // REP_CANONOVL=L: TREE-OVERLAP measure. In completeBlockF3, at each partial of
                                                    // size>=L, canonKey it (order-independent S18 class), CAP (do not expand), and
                                                    // accumulate ACROSS blocks -> total/distinct per size = the cross-block redundancy
                                                    // McKay canonical-augmentation would remove. Diagnostic; k18-only.
std::mutex g_ovlMtx;
std::unordered_map<int, long long> g_ovlTot;        // size -> total partials visited (across all blocks)
std::unordered_map<int, std::set<std::string>> g_ovlSet;  // size -> distinct canonKeys
std::atomic<long long> g_ovlNonSym{ 0 };            // # partials recorded that contained a NON-sigma0-symmetric factor
                                                    // (decides whether the fast quotient labeler applies -- 0 => all sigma0-symmetric)
bool g_ovlKey = false;                              // REP_CANONKEY=1: also canonKey each partial (~1ms) for overlap. Default OFF = fast sigma0-symmetry+total probe only.
static inline bool partialAllSym(const std::vector<Match>& F) {   // every factor sigma0-symmetric: F[u^1]==F[u]^1 ?
    for (const auto& f : F) for (int u = 0; u < N; u++) if (f[u ^ 1] != (uint8_t)(f[u] ^ 1)) return false;
    return true;
}

// [PRECALC] REP_PRECALC=1 -- PROTOTYPE. Enumerable
// block-threading pool only, trivial-stabilizer nodes only (REP_PRUNELEVEL=1 makes every node
// below a seed trivial). The sigma-orbits admissible below a seed are enumerated
// ONCE into a list; every node below inherits its parent's index list filtered by the orbit
// the parent committed, and its candidates are the entries through its anchor edge -- no genM.
//   rule 1: an ORBIT is one entry, base row (least member) first, the images after it;
//   rule 2: the sigma-fixed factor is an entry of size 1.
//
// [TUNE 2026-09-07, k20; MIRRORED 2026-09-10] The 2026-09-07 pass was k20-only by instruction.
// It is mirrored here now because the k18 t9 a=4 thread-scaling test located its loss in this
// walk: two 1-thread processes scaled 1.67x and the plain path 1.62x, but one process with two
// threads only 1.35x. The per-node make_shared, the per-child copies of the partial cover and
// the per-child copies of the seed's shared_ptr -- one refcount cache line every thread hits --
// are exactly the things that show between THREADS and not between PROCESSES. The k20 profile
// that motivated the rewrite (block 0, 8 threads, 632 s wall / 4,407 s CPU) put it at:
//     orbCompat 44.7%   is_perfect (+ its array::operator[]) 9.1%   malloc 9.9%
//     splitNodeL self 9.1%   queue mutex 6.0%   commitOrbit+rollbackTo 4.2%
//     shared_ptr refcounts 3.6%   the per-child `chosen` copy 1.9%   genM 0.2%
bool g_precalc = false;
struct OrbEnt {
    Match rows[3];                     // sorted members: rows[0] is the base row
    uint8_t nrows = 0;                 // 3, or 1 for the sigma-fixed factor
};
// The edge bitmaps live OUTSIDE OrbEnt, in their own 32-byte-aligned plane. The filter loop
// reads nothing but bitmaps; with them inside OrbEnt it dragged ~61 bytes of rows through cache
// to reach 24 bytes of bitmap, and at depth 6 it streams a 1.19M-entry list. One aligned 32-byte
// record per entry is a single AVX2 load that never straddles a cache line, and the three ANDs
// plus OR collapse to vpand + vptest. w[3] is always 0 and only pads the record to 32 bytes.
struct alignas(32) OrbBits { uint64_t w[4]; };
struct OrbList {
    std::vector<OrbEnt>  ent;         // the rows -- touched only when a child is really committed
    std::vector<OrbBits> bits;        // bits[i] is ent[i]'s edge set; the two stay index-parallel
};
typedef std::vector<uint32_t> IdxList; // indices into OrbList::ent
// A node's admissible list. `idx` is what the node needs to reach ent[] when it finally commits a
// child; `bits` is the SAME entries' bitmaps copied out CONTIGUOUSLY, and it is the reason this
// struct exists.
//
// [TUNE 2026-09-07, third pass] The filter used to walk an index list and load bits[j] for
// scattered j -- one dependent load per entry into a 38 MB plane. The 60 s profile of the running
// case charged 28.1% to bitsDisjoint, which is a handful of instructions: it was waiting on those
// loads, not doing the AND. Copying each survivor's 32-byte record into the child's own buffer
// costs one store per survivor and turns every deeper scan into a LINEAR read of a small block --
// <= 34 entries (~1 KB, L1-resident) below depth 12, ~4.1k entries at depth 9, which is where most
// bitmap tests happen. The lists shrink ~60x per level, so the copy is paid on far fewer entries
// than the scans it makes contiguous.
struct NodeList {
    IdxList               idx;
    std::vector<OrbBits> bits;                 // bits[k] belongs to idx[k]
    void clear() { idx.clear(); bits.clear(); }
    size_t size() const { return idx.size(); }
};
// A queue item carries the seed's list, its PARENT's admissible list and the entry the parent
// committed to reach it. The node computes its own list lazily when it is expanded (parent list
// filtered by that entry), so lists exist only for nodes whose children are pending -- with the
// LIFO queue that is about one path's worth -- and a seed's 233k children do not each get a
// materialized list at the seed (the first cut did that: hours of one thread and tens of GB).
struct OrbTask {
    std::shared_ptr<OrbList> root;             // the seed's list (null until the seed builds it)
    std::shared_ptr<const NodeList> plist;      // the parent's admissible entries (null at the seed = all)
    uint32_t committed = 0xFFFFFFFFu;           // entry committed by the parent to make this node
};
// Edge-disjointness of two entries, the first half of the admissibility test and the single
// hottest operation in the run. One 32-byte load each, one AND, one zero test.
inline bool bitsDisjoint(const OrbBits& a, const OrbBits& b) {
    const __m256i x = _mm256_and_si256(_mm256_load_si256((const __m256i*)a.w),
                                       _mm256_load_si256((const __m256i*)b.w));
    return _mm256_testz_si256(x, x) != 0;
}
// j stays admissible after committing c iff its edges are disjoint from c's and its base row is
// Hamiltonian with every row of c: every other pair (sigma^a F_j, sigma^b F_c) is the sigma^a-image
// of (F_j, sigma^{b-a} F_c), and a fixed factor is its own image, so those checks cover all pairs.
// Keeping the bitmap plane and the rows apart is the point: the common case returns on the
// bitmaps alone and never touches ent[] at all.
//
// [TUNE 2026-09-07, second pass] The COMMITTED entry is loop-invariant -- one c for a whole filter
// pass over the parent's list -- so it arrives already resolved rather than re-derived per entry
// (that cost showed as orbCompat self time and vector<OrbEnt>::operator[] 3.4%, now gone).
// Third pass: the caller holds the candidate's bitmap CONTIGUOUSLY and passes it in, so this half
// of the test no longer touches the 38 MB plane at all.
inline bool rowsHamiltonian(const Match& jrow, const OrbEnt& ce) {
    for (int r = 0; r < ce.nrows; r++) if (!is_perfect(jrow, ce.rows[r])) return false;
    return true;
}
std::mutex g_print_mtx;               // serializes the periodic progress line
std::chrono::steady_clock::time_point g_t0;         // run start time
std::chrono::steady_clock::time_point g_last_print; // time of last progress line
bool g_bprint = false;                // mirror of K18A2::m_bPrint (gate for ALL prints)
long long g_last_print_nodes = 0;     // g_nodes at the previous progress line (for the rate)
int  g_order = 0, g_typeIdx = 0, g_numTypes = 0;    // current order, current cycle type 1/N

// The two identity cells of a column-set-A row: what printSubset() used to print as
// [SUBSET].  Wording is unchanged; only the
// destination changed -- these are table cells now.
static void subsetCells(int order, bool isV4, bool isE9, bool isS3, const char* tstr,
                        std::string& symmetry, std::string& type)
{
    char t[80]; snprintf(t, sizeof(t), "%s", tstr);
    { size_t L = strlen(t); while (L && t[L - 1] == ' ') t[--L] = '\0'; }
    if (isV4 || isS3 || isE9) {
        const char* g = isV4 ? "V4" : isS3 ? "S3" : "E9";
        symmetry = g;
        // tstr already opens with the group token in the k18 shapes -- do not repeat it.
        type = (!strncmp(t, g, strlen(g))) ? std::string(t + strlen(g) + (t[strlen(g)] == ' ' ? 1 : 0))
                                           : std::string(t);
        return;
    }
    char s[32]; snprintf(s, sizeof(s), "order %d", order);
    symmetry = s;
    type = t;
}

// ---- the run's ONE table --------------------------------------------
// Column set A.  Widths are minimums; Saved is last
// and so is never padded, which is why its histogram may run as wide as it needs to.
static const LogCol kColsA[] = {
    { "Symmetry",     8, false },
    { "Type",        14, false },
    { "Elapsed",      7, true  },
    { "Leg nodes (rate)", 22, false },
    { "Total saved(duplicates)", 0, false },
};
// Same count, same widths, different words: the = row does not carry what the data rows carry.
static const LogCol kTotalsA[] = {
    { "Symmetry",     8, false },
    { "Type",        14, false },
    { "Elapsed",      7, true  },
    { "Total nodes (average)", 22, false },
    { "Saved",        0, false },
};

// The table must OUTLIVE the type loop -- it spans every REP_ORDERS token (one
// runRepresentativeMethod call per token, one table per RUN) and is reached from the
// periodic progress callback, which is a free function.  So: file scope, not a local.
static LogTable* g_tbl = nullptr;

// Run totals for the ~ and = rows.  The = row is the SUM of the rows printed above it, so
// every one of these is accumulated at the point its per-unit value is computed -- never
// re-derived from a separate clock or counter, which is how the old log came to print two
// different numbers for one quantity.
static std::map<int, int> g_runSavedAut;    // |Aut| -> classes written by the RUN
static std::map<int, int> g_runDupAut;      // |Aut| -> duplicates rejected by the RUN
static long long g_runFound  = 0;           // distinct classes found by the RUN   (Results)
static long long g_runSaved  = 0;           // classes written by the RUN          (Saved)
static long long g_runNodes  = 0;           // cover nodes entered by the RUN
static double    g_runSec    = 0.0;         // summed per-unit elapsed, NOT wall time
static std::chrono::steady_clock::time_point g_legT0;   // when the unit now open started
static long long g_legNodes0 = 0;           // g_nodes when it opened -- the ~ row needs the open unit too
static long long g_runDups   = 0;           // duplicates rejected by COMPLETED units (the open one is live-added)
static long long g_legDup0   = 0;           // g_crossDup when the open unit started
// Column set B.  Only the block driver builds this table,
// and the block driver is k18 only; the flag exists in all four so the ~ row is one piece of code.
static long long g_doneUnits = 0, g_doneTotal = 0;   // Done% = raw units walked / raw units in range; the block driver alone sets them
static bool g_tblIsB = false;
static const LogCol kColsB[] = {
    { "Block",        6, false },
    { "Elapsed",      7, true  },
    { "Block nodes (rate)", 22, false },
    { "Total saved(duplicates)", 26, false },
    { "Done%",        5, true  },
};
static const LogCol kTotalsB[] = {
    { "Block",        6, false },
    { "Elapsed",      7, true  },
    { "Total nodes (average)", 22, false },
    { "Saved",       26, false },
    { "Done%",        5, true  },
};


char g_typeStr[80] = "";              // current cycle type string (e.g. "2 2 2 2 2 2 2 2 2")
std::atomic<long long> g_reps_total{ 0 };   // representative/queue tasks for the current type (64-bit:
std::atomic<long long> g_reps_done{ 0 };    //   the 2026-07 K20 type-5 run reached 2.129e9, ~1% below INT_MAX)

// [PROGRESS] How far through the work the run is, as a percentage on the periodic [rep] line.
// ONLY the block driver publishes it, because only its denominator is both fixed before the work
// starts and evenly spent: it is a range of blocks, it is known up front, and blocks are broadly
// comparable in cost.
//
// The other fan-outs were tried and are not printed:
//   * the two work-queue paths (over-cap, block threading) count tasks CREATED against tasks
//     COMPLETED, and children are pushed as the tree opens, so the denominator GROWS and the
//     percentage can fall back late in a long run;
//   * the ENUMERABLE path does have a fixed denominator, reps.size(), but the work behind a
//     representative is wildly uneven. MEASURED 2026-08-28 on the order-3 3^6 leg of
//     K18Aut3-All: still reading 0% after 25 minutes of a 64-minute leg that was by then 35% of
//     the way through its nodes. A number that says 0 for the first half hour is worse than no
//     number, so that source was removed rather than shipped.
std::atomic<long long> g_blkDone{ 0 };      // blocks finished in the requested range (block driver)
std::atomic<long long> g_blkTotal{ 0 };     // blocks in the requested range, 0 = no block driver

// V4 mode sentinel: runRepresentativeMethod(order == kOrderV4) classifies P1Fs invariant
// under a KLEIN FOUR-GROUP {id, a, b, ab} (host maps the REP_ORDER[S] token "V4" to this).
constexpr int kOrderV4 = -4;
// E9/S3 mode sentinels: two-generator group sweeps analogous to V4 (host tokens "E9"/"S3").
// E9 = C3 x C3 (elementary abelian of order 9); S3 = the symmetric group on 3 letters
// (order 6, non-abelian). Together with the cyclic order-6/9 sweeps these close the
// "group has order divisible by 6 or 9 but no element of that order" gap.
constexpr int kOrderE9 = -9;
constexpr int kOrderS3 = -6;
inline const char* ordName(int o) { static thread_local char b[16]; if (o == kOrderV4) return "V4"; if (o == kOrderE9) return "E9"; if (o == kOrderS3) return "S3"; snprintf(b, sizeof(b), "%d", o); return b; }

// ---- harvest mode (per-order class-count target) ----------------------------
// When g_target>0, the run STOPS as soon as g_harvest holds g_target distinct classes,
// skipping the (intractable) proof-of-exhaustion tail. g_harvest is the labeling-
// independent distinct set across ALL workers (each emit() inserts under g_harvest_mtx),
// so the count is exact even mid-run. Result is COMPLETE only if g_target == the true
// count (e.g. a known/published number); otherwise it is an explicit LOWER BOUND.
int g_target = 0;                     // 0 = run to completion; >0 = stop after this many distinct classes
int g_printRawLeft = 0;               // [DIAG] REP_PRINTRAW=n: dump the search-frame cover of the
                                      //   first n new classes (the frame the V4 filters apply to)
std::atomic<bool> g_stop{ false };    // set once g_harvest reaches g_target; all workers/recursion bail
std::mutex g_harvest_mtx;             // guards g_harvest (locked only on a genuine new class -> rare)
std::map<std::string, long long> g_harvest; // class key -> #detections this order (capture-recapture input; 64-bit for week-scale harvests)
// CROSS-LEG DEDUP. g_harvest above is cleared at the top of every runRepresentativeMethod call,
// i.e. once per REP_ORDERS token, so it only dedups WITHIN one order. A class whose automorphism
// group matches two legs reached the result file once per leg: |Aut|=272 = 16*17 was written by
// both the order-4 and the order-17 leg, and the |Aut|=16 class P47 by both order 4 and V4. The
// same defect cost K14 14 of 35 records, K20 13 of 243 and K16 1 of 31. g_emittedAll is never
// cleared, so a run writes each class exactly once; the later leg counts it as a duplicate.
std::set<std::string> g_emittedAll;         // class keys already written to RESULT this RUN (g_harvest_mtx)
std::atomic<long long> g_crossDup{ 0 };     // classes re-found by a later leg and not written again
std::map<int, int> g_crossDupAut;           // |Aut| -> number of those duplicates (g_harvest_mtx)
std::map<int, int> g_savedAut;              // |Aut| -> classes WRITTEN by the current type (g_harvest_mtx)
std::atomic<int> g_f1{ 0 }, g_f2{ 0 };// #classes seen exactly once / exactly twice (Chao1 terms)
std::atomic<int> g_banked{ 0 };       // exact |g_harvest| mirror (lock-free read for the [rep] line)
std::atomic<long long> g_blockCovers{ 0 };  // raw complete-covers found in the CURRENT level-L block (estimate log)
std::atomic<long long> g_dupCovers{ 0 };    // rejected covers = re-detections (cdet>1) of an already-found class; total covers = classes + rejected
bool g_noOrbChk = false;                     // REP_NOORBCHK: skip the orbit-internal is_perfect (PERF PROBE only -- see buildAndValidateOrbit)
bool g_noClosurePrune = false;              // REP_NOPRUNE=1 -> skip genM's closure-ban early prune (A/B; leaf is_perfect still validates)
int g_pruneMaxFactors = 0;                  // REP_PRUNELEVEL: once >= this many factors are committed, SKIP the per-node setwiseStab/schreierDedup prune (explore all orbits, rely on the global canonKey dedup). 0 = disabled (prune at every depth, default). Trades more duplicate work for skipping the expensive BSGS setwise-stabilizer walk where it rarely pays.
FILE* g_estFile = nullptr;            // REP_ESTFILE: append "<blockIdx> <covers> <newDistinct>" per FULL block; null = off
// REP_LOCATE: LOCATE MODE. Given target factorization file(s), report the depth-shardLevel block index i
// whose committed cover is a SUBSET of a target's factor set, then STOP. Skips runBlock => fast (walks only
// the block-index DFS). Requires the SAME REP_PRUNELEVEL/REP_LEVEL as the run whose block numbers you want.
struct LocTarget { int nfac; uint32_t edgeFac[N][N]; };  // edgeFac[a][b] = bitmask of factor indices containing edge {a,b}
std::vector<LocTarget> g_locTargets;
static inline std::string facKey18(const Match& m) { return std::string((const char*)m.data(), (size_t)N); }

// LOCATE MATCH (up to C(sigma0)): is there phi in the centralizer of sigma0=(0 1)(2 3)..(16 17) mapping every
// cover factor onto a WHOLE target factor? phi commutes with sigma0 => phi(2k+1)=sigma0(phi(2k)), so phi is
// fixed by its EVEN-vertex images. Backtrack over even source vertices. Candidate-mask prune: cand[c] = set of
// target factors still consistent with cover factor c's mapped edges; prune the instant any cand[c] empties.
static bool locBt(int ev, const std::vector<Match>& cover, const LocTarget& T, int* phi, bool* usedT, const uint32_t* cand) {
    if (ev == N) return true;                 // full phi with every cand[c] != 0 => each cover factor == a target factor
    size_t nc = cover.size();
    for (int t = 0; t < N; t++) {
        if (usedT[t]) continue; int tp = t ^ 1; if (usedT[tp]) continue;   // sigma0 block {t, t^1}
        phi[ev] = t; phi[ev + 1] = tp;
        uint32_t ncand[16]; bool ok = true;
        for (size_t c = 0; c < nc; c++) {
            uint32_t m = cand[c]; const Match& f = cover[c];
            int w0 = f[ev];     int p0 = (w0 < ev) ? phi[w0] : (w0 == ev ? t : (w0 == ev + 1 ? tp : -1)); if (p0 >= 0) m &= T.edgeFac[t][p0];
            int w1 = f[ev + 1]; int p1 = (w1 < ev) ? phi[w1] : (w1 == ev ? t : (w1 == ev + 1 ? tp : -1)); if (p1 >= 0) m &= T.edgeFac[tp][p1];
            ncand[c] = m; if (!m) { ok = false; break; }
        }
        if (ok) {
            usedT[t] = true; usedT[tp] = true;
            if (locBt(ev + 2, cover, T, phi, usedT, ncand)) { usedT[t] = false; usedT[tp] = false; return true; }
            usedT[t] = false; usedT[tp] = false;
        }
    }
    return false;
}
static bool coverMatchesTarget(const std::vector<Match>& cover, const LocTarget& T) {
    if (cover.size() > 16) return false;
    int phi[N]; bool usedT[N]; for (int i = 0; i < N; i++) usedT[i] = false;
    uint32_t cand[16]; uint32_t full = (T.nfac >= 32) ? 0xFFFFFFFFu : ((1u << T.nfac) - 1u);
    for (size_t c = 0; c < cover.size(); c++) cand[c] = full;
    return locBt(0, cover, T, phi, usedT, cand);
}
// Same, but on success copy the matching centralizer element phi (maps cover factors onto target
// factors). Used by path->coordinate to rotate the target into the search frame at level 1.
static bool coverMatchesTargetPhi(const std::vector<Match>& cover, const LocTarget& T, int* phiOut) {
    if (cover.size() > 16) return false;
    int phi[N]; bool usedT[N]; for (int i = 0; i < N; i++) usedT[i] = false;
    uint32_t cand[16]; uint32_t full = (T.nfac >= 32) ? 0xFFFFFFFFu : ((1u << T.nfac) - 1u);
    for (size_t c = 0; c < cover.size(); c++) cand[c] = full;
    if (locBt(0, cover, T, phi, usedT, cand)) { for (int i = 0; i < N; i++) phiOut[i] = phi[i]; return true; }
    return false;
}
std::map<int, int> g_autHist;         // |Aut| -> #distinct classes so far (live distribution; guarded by g_harvest_mtx)
std::function<void(const unsigned char*, int)> g_sendResult;  // forwards each globally-new class + its |Aut| to the
                                      //   NORMAL result pipeline the moment it is found (set per run)

// One periodic progress line (every 300s) shared by cover()/coverGen(): shows the current
// order + cycle type, how many of the type's representative subtrees are done (the best
// completion-trend signal), node throughput since the last line, and classes found so far.
// [DIAG] REP_F17DUMP: kill-surviving monitor. Every ~5s, overwrite NewTests/f17_mon.txt with the live
// forced-17th tally, max depth, splitNode child-batch/queue peaks (memory probe), and the sampled
// obstruction + clash histograms. Because it is written to a file each tick, a watchdog kill (OOM cap)
// still leaves the latest snapshot on disk -- unlike the 60s-gated stdout print which is lost on kill.
inline void f17MonitorTick() {
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
    long long due = g_monNextSec.load(std::memory_order_relaxed);
    if (el < due) return;
    long long nxt = ((long long)(el / 5.0) + 1) * 5;
    if (!g_monNextSec.compare_exchange_strong(due, nxt, std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(g_f17_mtx);
    std::string hist; for (auto& kv : g_f17Hist) { char t[48]; snprintf(t, sizeof(t), "%s%s:%lld", hist.empty() ? "" : "  ", kv.first.c_str(), kv.second); hist += t; }
    std::string ch;   for (auto& kv : g_f17ClashHist) { char t[48]; snprintf(t, sizeof(t), "%s%d:%lld", ch.empty() ? "" : " ", kv.first, kv.second); ch += t; }
    FILE* f = fopen("f17_mon.txt", "w");
    if (f) {
        fprintf(f, "t=%.0fs maxlvl=%d/%d forced17(sampled 1/%lld)=%lld nodes=%lld maxKids=%lld maxNQ=%lld\n",
                el, g_maxDepth.load(), NM, g_f17Sub, g_f17Total.load(), g_nodes.load(), g_maxKids.load(), g_maxNQ.load());
        fprintf(f, "obstruction histogram: %s\n", hist.c_str());
        fprintf(f, "clash-count histogram: %s\n", ch.c_str());
        fclose(f);
    }
}

inline void progressTick(long long& nodes_flush, std::atomic<long long>& g_nodesRef) {
    if ((nodes_flush & 0x3FF) != 0) return;   // flush every 1024 nodes/worker (the 300s gate paces the actual print)
    g_nodesRef.fetch_add(nodes_flush, std::memory_order_relaxed);
    nodes_flush = 0;
    if (g_f17dump) f17MonitorTick();          // [DIAG] periodic kill-surviving F17 monitor snapshot to disk
    if (!g_bprint) return;
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(g_print_mtx);
    double sinceLast = std::chrono::duration<double>(now - g_last_print).count();
    if (sinceLast < 300.0) return;
    g_last_print = now;
    long long cur = g_nodesRef.load();
    g_last_print_nodes = cur;
    // The periodic row, marker '~': one leg, still open, reported mid-search. Column set B does
    // NOT get one -- there a block row lands every minute or so and already carries the same
    // cumulative figures, so this would duplicate the row above it. Column set A does, because
    // one leg can run an hour (K18Aut3-All's order-3 3^6 is 62 minutes) and nothing else prints.
    // The cover-level counters this line used to carry (g_emits, g_dupCovers) are gone: they
    // answered a different question with the same words as the closing line.
    if (!g_tbl || g_tblIsB) return;
    const double elapsed = g_runSec + std::chrono::duration<double>(now - g_legT0).count();
    long long saved; std::map<int, int> savedHist, dupHist;
    { std::lock_guard<std::mutex> lk(g_harvest_mtx);
      saved = (long long)g_emittedAll.size();   // written this RUN; never cleared, so it needs no accumulator
      savedHist = g_runSavedAut; dupHist = g_runDupAut; }
    const long long dups = g_runDups + (g_crossDup.load(std::memory_order_relaxed) - g_legDup0);
    std::vector<std::string> cells;
    cells.push_back("so far");
    cells.push_back("");
    cells.push_back(fmtElapsed(elapsed));                       // cumulative, as on a data row
    cells.push_back(fmtNodes(cur - g_legNodes0, elapsed - g_runSec));   // this leg's own, as on a data row
    cells.push_back(fmtSavedDup(saved, dups, savedHist, dupHist));
    g_tbl->row('~', cells);
    fanPrint();
}

// ---- per-thread search worker ----------------------------------------------
// [INFO] the relabeling of the frame that won canonKey's minimum, i.e. how the canonical matrix was
// labeled. thread_local rather than a RepWorker member on purpose: adding members to that object has
// measurably perturbed its layout and cost before. Written only when a new minimum is found.
static thread_local std::array<uint8_t, N> t_canonP;
struct RepWorker {
    const RepShared* sh = nullptr;    // shared read-only data for the current type
    std::set<long long>* cSet = nullptr;    // COMPLETEPREFIX: distinct-c sink (canonical c-ranks; set by that path only)
    std::mutex* cMtx = nullptr;             // guards *cSet

    std::vector<Match> chosen;        // factors committed so far on this search path
    std::vector<std::array<uint8_t, 2 * NHALF>> facEdges; // facEdges[f] = edge ids of factor f
    std::vector<int> facMin;          // facMin[f] = smallest edge id in factor f ("MEC")
    bool covered[N][N];               // covered[u][v] = edge {u,v} used by a chosen factor
    uint32_t coveredBits[N];          // same info as SYMMETRIC bit rows: bit v of coveredBits[u]
                                      //   (kept in sync in clearState/commitOrbit/rollbackTo;
                                      //   genM consumes these, the bool array serves the rest)
    int  edgeFactor[NEDGES];          // edgeFactor[e] = chosen factor index covering edge e, or -1
    uint8_t path_end[NM][N];          // path_end[k][x] = endpoint of the alternating path through x
                                      //   in (chosen factor k) U (partial matching); Hamiltonian prune
    uint8_t sPE[NM][N];               // the same structure for sExists (REP_DIAGPRUNE's matching search),
                                      //   kept apart so the prune never disturbs a genM enumeration in flight
    Match    dgWit[NM + 2][NHALF];    // [WITNESS] dgWit[m][d/2] = the matching that proved diagonal {d,d^1}
                                      //   realizable at a node of m rows; a child re-checks it before searching
    uint32_t dgWitValid[NM + 2] = { 0 };   //   bit d/2 set when dgWit[m][d/2] holds one; commitRows clears level m
    int lastAut = 0;                  // |Aut|*2 of the P1F most recently canonicalized

    std::set<std::string>      canon;     // distinct canonical keys found by THIS worker
    std::map<std::string, int> autOf;     // canonical key -> 2*|Aut| (this worker)
    std::vector<Match>* collectInto = nullptr;  // non-null => collect first-orbit tasks, don't recurse
    std::vector<Match> orbScratch;    // reusable scratch for genM's leaf buildAndValidateOrbit (avoids per-leaf alloc)
    // splitNode reusable buffers: cleared (not reconstructed) per node so the ultra-wide a=4 candidate
    // lists don't churn the heap. splitNode is non-recursive per worker, so per-worker members are safe.
    std::vector<Match> snCands, snReps;               // candidate matchings / orbit reps
    std::vector<std::string> snKeys;                  // orbitKey strings (kept for schreierDedup)
    std::unordered_map<std::string, int> snKeyIdx;    // orbitKey -> rep index (clear() retains bucket array)
    std::vector<int> snRepIdx;                        // selected rep indices

    long long nodes = 0;              // local node counter (flushed to g_nodes periodically)
    long long nodes_flush = 0;        // local nodes not yet added to g_nodes

    void clearState() {               // reset search state for a fresh task
        chosen.clear(); facEdges.clear(); facMin.clear();
        memset(covered, 0, sizeof(covered));
        memset(coveredBits, 0, sizeof(coveredBits));
        for (int e = 0; e < NEDGES; e++) edgeFactor[e] = -1;
    }

    // Cover-anchor selection: V4-FILTERED rows first (so each filter binds at a FIXED
    // level right under the root on EVERY branch -- K16-style sharding; without this
    // the binding depth floats per branch and the pruning leaks down the tree), then
    // the lowest uncovered edge. Sound: the exact-cover anchor order is a free
    // deterministic choice (orderly-generation argument is anchor-agnostic; coverGen
    // restricts its stabilizer to anchor-fixing generators whatever the anchor is).
    // With no filters this is byte-identical to the plain lowest-uncovered rule.
    inline void pickAnchor(int& u0, int& v0) {
        u0 = -1; v0 = -1;
        for (int i = 0; i < sh->nV4; i++) {
            const int r = sh->v4Row[i];
            if (!covered[0][r]) { u0 = 0; v0 = r; return; }
        }
        for (int u = 0; u < N && u0 < 0; u++) for (int v = u + 1; v < N; v++) if (!covered[u][v]) { u0 = u; v0 = v; break; }
    }

    // every edge of a G-orbit member must be free, so the image of any edge we place
    // under EVERY group element must currently be uncovered (cheap, sound early reject)
    inline bool imgUncovered(int u, int v) {
        for (const auto& g : sh->gElems) {
            int au = g[u], av = g[v];
            int x = au < av ? au : av, y = au < av ? av : au;
            if (x == u && y == v) continue;       // this element fixes the edge
            if (covered[x][y]) return false;
        }
        return true;
    }

    // build the G-orbit {M} U {g M : g in gElems} and validate it: every member must be
    // perfect with all chosen factors and the members must be mutually perfect.
    bool buildAndValidateOrbit(const Match& M, std::vector<Match>& orbit) {
        orbit.clear(); orbit.push_back(M);
        for (const auto& g : sh->gElems) orbit.push_back(applyAlpha(M, g.data()));
        std::sort(orbit.begin(), orbit.end());
        orbit.erase(std::unique(orbit.begin(), orbit.end()), orbit.end());
        // V4 shard filters: every committed factor must satisfy F[0]==row <=> F[1]==val
        // (a factor pairing 1-with-val while pairing 0-with-another-row would steal edge
        // {1,val} from the row's factor, so it can never be part of a shard solution).
        // Checked on every orbit member: the members pair 0 with different partners.
        for (int i = 0; i < sh->nV4; i++) {
            const uint8_t r = sh->v4Row[i], v = sh->v4Val[i];
            for (const auto& F : orbit)
                if ((F[0] == r) != (F[1] == v)) return false;
        }
        // F-vs-chosen is REDUNDANT when the closure prune is on: genM's per-edge closure ban already
        // guarantees every generated factor is Hamiltonian with every chosen factor (that is exactly
        // what makes the leaf check "still validates" only under REP_NOPRUNE). So only re-check here
        // when the prune is OFF. The orbit-INTERNAL check (F vs sigma0 F) is NOT covered by genM (sigma0 F
        // is not in chosen), so it is always needed.
        if (g_noClosurePrune) for (const auto& F : orbit) for (const auto& C : chosen) if (!is_perfect(F, C)) return false;
        // [PERF PROBE] REP_NOORBCHK: skip the orbit-INTERNAL is_perfect (F vs sigma0 F). genM's closure
        // ban cannot cover it (sigma0 F is not in `chosen`), so it is nominally always needed -- but
        // REP_GENSTAT measured 0 rejections here across ~28M candidates in a t9 a=0 block, i.e. it never
        // fires. This flag exists to MEASURE what it costs; it is NOT sound to enable for a census until
        // the redundancy is proven, since a miss would admit an invalid factor pair.
        if (!g_noOrbChk)
            for (size_t x = 0; x < orbit.size(); x++) for (size_t y = x + 1; y < orbit.size(); y++) if (!is_perfect(orbit[x], orbit[y])) return false;
        return true;
    }

    void commitOrbit(const std::vector<Match>& orbit) {   // append the orbit's factors
        if (cSet)   // COMPLETEPREFIX only (member ptrs set by that path): the factor covering {0,3} (F[0]==3) is the level-3 coordinate c; record its orbit
            for (const auto& F : orbit) if (F[0] == 3) { long long cr = cRankCanon(F, sh->gElems); std::lock_guard<std::mutex> lk(*cMtx); cSet->insert(cr); }
        commitRows(orbit.data(), (int)orbit.size());
    }

    // [TUNE 2026-09-07] the same commit, from a plain row pointer. REP_PRECALC already holds an
    // orbit's rows contiguously in its OrbEnt, so the local recursion commits straight from there
    // instead of first copying them into a std::vector to satisfy commitOrbit's signature.
    void commitRows(const Match* rows, int n) {           // append n factors
        { int cs = (int)chosen.size() + n; int pv = g_maxDepth.load(std::memory_order_relaxed);   // diagnostic: track deepest partial cover (BFS + coverGen + list paths)
          while (cs > pv && !g_maxDepth.compare_exchange_weak(pv, cs, std::memory_order_relaxed)) {} }
        for (int i = 0; i < n; i++) {
            const Match& F = rows[i];
            int fidx = (int)chosen.size();
            chosen.push_back(F);
            dgWitValid[chosen.size()] = 0;   // [WITNESS] a new node of this size: whatever an earlier node of the same size proved is not its own
            std::array<uint8_t, 2 * NHALF> el;   // this factor's edge ids
            int ec = 0;               // edge count written into el
            int mn = INF;             // min edge id of this factor
            for (int u = 0; u < N; u++) { int v = F[u]; if (u < v) { covered[u][v] = true; coveredBits[u] |= 1u << v; coveredBits[v] |= 1u << u; int e = eidT[u][v]; el[ec++] = (uint8_t)e; edgeFactor[e] = fidx; if (e < mn) mn = e; } }
            facEdges.push_back(el); facMin.push_back(mn);
        }
    }

    void rollbackTo(size_t base) {    // undo factors added after index `base`
        for (size_t k = base; k < chosen.size(); k++) {
            const Match& F = chosen[k];
            for (int u = 0; u < N; u++) { int v = F[u]; if (u < v) { covered[u][v] = false; coveredBits[u] &= ~(1u << v); coveredBits[v] &= ~(1u << u); edgeFactor[eidT[u][v]] = -1; } }
        }
        chosen.resize(base); facEdges.resize(base); facMin.resize(base);
    }

    // labeling-independent canonical form: lexicographically-smallest serialization
    // over all (ordered factor pair, direction, anchor) Hamiltonian-cycle frames.
    // Side effect: lastAut = number of frames achieving the minimum = 2*|Aut|.
    std::string canonKey(const std::vector<Match>& f) {
        std::string best;             // current lexicographically smallest serialization
        std::string bestAdj;          // the SAME winner in adjacency form -- that is the key everything downstream decodes
        bool first = true;            // have we seen any valid frame yet?
        int bestCount = 0;            // how many frames tie for `best`
        for (int i = 0; i < NM; i++) for (int j = 0; j < NM; j++) {
            if (i == j) continue;
            const Match& A = f[i]; const Match& B = f[j];   // the factor pair forming the basis cycle
            for (int dir = 0; dir < 2; dir++) {
                const Match& X = dir ? B : A; const Match& Y = dir ? A : B;  // traversal order
                for (int a = 0; a < N; a++) {               // anchor (cycle start vertex)
                    uint8_t c[N];      // the Hamiltonian cycle as a vertex sequence
                    bool seen[N]; memset(seen, 0, sizeof(seen));  // seen[v] during cycle build
                    int pos = 0, cur = a; bool ok = true;
                    for (int t = 0; t < NHALF; t++) {
                        if (seen[cur]) { ok = false; break; } seen[cur] = true; c[pos++] = (uint8_t)cur; cur = X[cur];
                        if (seen[cur]) { ok = false; break; } seen[cur] = true; c[pos++] = (uint8_t)cur; cur = Y[cur];
                    }
                    if (!ok || cur != a || pos != N) continue;   // pair didn't form a single N-cycle here
                    const uint8_t* L = legacyWalk();                                // [LEGACY CANON] target labeling
                    uint8_t p[N]; for (int t = 0; t < N; t++) p[c[t]] = L[t];   // relabel cycle -> R1, R2 frame
                    std::vector<Match> rel(NM);   // the relabeled factor set
                    for (int k = 0; k < NM; k++) { Match g; for (int u = 0; u < N; u++) g[p[u]] = p[f[k][u]]; rel[k] = g; }
                    std::sort(rel.begin(), rel.end());        // canonicalize factor order
                    // [LEGACY CANON] compare the rows as `src` PAIRS -- the project canonizer's form -- and
                    // not as adjacency vectors: both lead with the partner of 0 but diverge after it, so the
                    // two pick different representatives of the same class (MD/canonization_switch_spec.md).
                    std::string s; s.reserve(NM * N);         // serialized relabeled P1F, as src pairs
                    for (auto& g : rel) { unsigned char sc[N]; adj_to_src(g.data(), sc); s.append((const char*)sc, N); }
                    if (first || s < best) { best = s; first = false; bestCount = 1; bestAdj.clear(); for (auto& g2 : rel) for (int u2 = 0; u2 < N; u2++) bestAdj.push_back((char)g2[u2]); memcpy(t_canonP.data(), p, N); }   // [INFO] keep the winning relabeling
                    else if (s == best) bestCount++;
                }
            }
        }
        lastAut = bestCount;
        return bestAdj;   // adjacency form of the src-minimal frame
    }


    // [DIAG] REP_PATSTAT: sigma0 stay/swap pattern of the committed factors in commit order
    // (S = sigma0-fixed/stay factor, W = sigma0-paired/swap factor).
    std::string patSeq() const {
        std::string s; s.reserve(chosen.size());
        for (const auto& F : chosen) s += (applyAlpha(F, sh->alpha) == F) ? 'S' : 'W';
        return s;
    }

    // [INFO] the cover's stay/swap pattern in ITS OWN (pre-canonization) labeling, indexed by
    // neighbour-0: position j-1 is the type of the row whose 0-neighbour is j. Same convention as
    // the pattern filter, so a logged pattern can be fed straight back as REP_PATTERN.
    std::string patN0() const {
        std::string s; s.reserve(N - 1);
        for (int j = 1; j < N; j++) s += patRowType(j);
        return s;
    }

    // aut-type ('S'/'W') of the committed row whose 0-neighbour == j; '?' if not yet committed.
    char patRowType(int j) const {
        for (const auto& F : chosen) if (F[0] == j) return (applyAlpha(F, sh->alpha) == F) ? 'S' : 'W';
        return '?';
    }

    // [PATFILTER] NEIGHBOUR-0 pattern filter: each committed factor's aut-type must match the target at
    // position F[0]-1 (0's neighbour, 1..N-1). S = sigma0-fixed, W = sigma0-paired.
    //  * SET mode (g_patOnly non-empty): target = g_patOnly string.
    //  * CALCULATE (empty): row1=S; rows {2,3} give one S/one W (one-per-pair), and that {2,3} pair is
    //    DUPLICATED across every pair (even pos -> row2 type, odd -> row3 type). In the a=4 block r2=S,r3=W
    //    is guaranteed (F2 stay, F3 forced swap), so r2==r3 is an internal error there.
    bool patOK() const {
        char r2 = 'S', r3 = 'W';
        if (g_patOnly.empty()) {                               // calculate mode
            r2 = patRowType(2); r3 = patRowType(3);
            if (r2 == '?' || r3 == '?') return true;           // rows 2,3 not both committed yet
            if (r2 == r3) {                                    // a=4 block guarantees r2=S,r3=W -> a genuine bug if here
                printf("[PATFILTER] INTERNAL ERROR: row2 and row3 same type (%c) -- one-per-pair violated. Starter:\n  ", r2);
                for (int fi = 0; fi < 3 && fi < (int)chosen.size(); fi++) { for (int u = 0; u < N; u++) if (u < chosen[fi][u]) printf("%d-%d ", u, (int)chosen[fi][u]); printf("| "); }
                printf("\n"); fflush(stdout); exit(1);
            }
            static std::atomic<bool> printed{ false }; bool exp = false;   // [DIAG] print the calculated target once
            if (printed.compare_exchange_strong(exp, true)) {
                std::string tgt(1, 'S'); for (int k = 2; k < N; k++) tgt += (k % 2 == 0) ? r2 : r3;
                printf("[PATFILTER] calculated target (r2=%c r3=%c): %s\n", r2, r3, tgt.c_str()); fflush(stdout);
            }
        }
        for (const auto& F : chosen) {
            int k = F[0];
            char expected;
            if (!g_patOnly.empty()) { if (k < 1 || k >(int)g_patOnly.size()) return false; expected = g_patOnly[k - 1]; }
            else expected = (k == 1) ? 'S' : ((k % 2 == 0) ? r2 : r3);   // calculate: dup {2,3} across pairs
            bool isS = (applyAlpha(F, sh->alpha) == F);
            if (expected != (isS ? 'S' : 'W')) return false;
        }
        return true;
    }

    // [PATFILTER] REP_PATPAIR one-per-pair: within each sigma0 position-pair {2k,2k+1} (partner-of-0 vertices),
    // the two committed factors must have DIFFERENT aut-type (one S, one W). Sound+complete |Aut|=2 filter
    // (both-stays => 4-cycle => |Aut|>2), independent of any fixed pattern. Position 1 (partner 0<->1) is the
    // lone sigma0-fixed factor and has no pair partner, so it is exempt.
    bool patPairOK() const {
        for (const auto& F : chosen) {
            int k = F[0];
            if (k < 2) continue;                               // position 1 has no pair partner
            char tp = patRowType(k ^ 1);                       // type of the pair-partner factor (partner-of-0 = k^1), '?' if not yet committed
            if (tp == '?') continue;
            char t = (applyAlpha(F, sh->alpha) == F) ? 'S' : 'W';
            if (t == tp) return false;                         // both members of the pair same type -> reject
        }
        return true;
    }

    void emit() {
        g_blockCovers.fetch_add(1, std::memory_order_relaxed);   // raw complete-cover count for the per-block estimate log
        g_emits.fetch_add(1, std::memory_order_relaxed);         // total results = every complete cover (new + rejected re-detections)
        static const bool dumpFirst = std::getenv("REP_DUMPFIRST") != nullptr;   // [PERF] hoisted: emit() is per complete cover
        if (dumpFirst) {                             // [VALIDATION] dump the FIRST complete cover (its sigma0-labeling), then stop
            static std::atomic<int> dumped{ 0 };
            if (dumped.fetch_add(1) == 0) {
                FILE* df = fopen("dump_cover.txt", "w");
                if (df) { for (const Match& f : chosen) { for (int u = 0; u < N; u++) if (u < f[u]) fprintf(df, "%d %d ", u, (int)f[u]); fprintf(df, "\n"); } fclose(df); }
                printf("[DUMPFIRST] first cover dumped (%d factors) -> dump_cover.txt\n", (int)chosen.size()); fflush(stdout);
                g_stop.store(true);
            }
        }
        std::string key = canonKey(chosen);          // labeling-independent class key (sets lastAut = 2*|Aut|)

        // [REP_OWNER] Two rejections, in this order, before anything is recorded:
        //
        //   1. the SAME-BLOCK repeat. This block already yielded this class -- e.g. block
        //      4.0.17607 reaches class #2 by two different covers. Both have the same owner,
        //      so ownership cannot separate them; only the canonized key can. Testing it
        //      first also saves the owner computation on every repeat.
        //   2. the FOREIGN class: genuinely present in this block, but another block owns it.
        //      This is the one that used to need a global set of everything ever found.
        //
        // Both return before g_harvest, so `saved` counts exactly what this run writes.
        if (g_ownerMode) {
            bool repeat;
            { std::lock_guard<std::mutex> lk(g_harvest_mtx); repeat = !g_ownerBlockSeen.insert(key).second; }
            if (repeat) {
                g_dupCovers.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lk(g_harvest_mtx);
                g_ownerRejInBlock++;
                if (g_ownerVerbose) { printf("[OWNER] block %s: class already found in this block -- rejected (in-block)\n", g_curBlock.c_str()); fflush(stdout); }
                return;
            }
            const long long own = ownerOf(chosen);
            if (own < 0 || own > g_ownerBlockC) {
                // The identity relabeling is always in the enumeration, so the owner can never
                // exceed the block that found the cover. If it does, ownerOf is wrong and every
                // block would reject this class -- losing it silently. Stop instead.
                printf("\n[OWNER] FATAL self-check: block %s found a cover whose owner is %lld.\n"
                       "        The owner can never exceed the finding block. ownerOf() is wrong and\n"
                       "        classes would be silently lost -- stopping the run.\n", g_curBlock.c_str(), own);
                fflush(stdout);
                g_stop.store(true);
                return;
            }
            if (own != g_ownerBlockC) {
                g_dupCovers.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lk(g_harvest_mtx);
                g_ownerRejForeign++;
                if (g_ownerVerbose) { printf("[OWNER] block %s: owned by %lld -- rejected (foreign)\n", g_curBlock.c_str(), own); fflush(stdout); }
                return;
            }
        }

        // [AUT2ONLY] Last reject, and deliberately AFTER the owner filter: a class with |Aut| > 2
        // is present in many blocks, but only its owner reaches here, so the histogram below counts
        // each class exactly ONCE and is an independent re-derivation of the |Aut|>2 census.
        // (With REP_OWNER=0 there is no owner filter and the histogram counts detections instead.)
        if (g_aut2Only && lastAut != 4) {          // lastAut = 2*|Aut|; keep |Aut| == 2 only
            g_dupCovers.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lk(g_harvest_mtx);
            g_ownerRejAut++;
            g_autRejHist[lastAut / 2]++;
            if (g_ownerVerbose) { printf("[OWNER] block %s: |Aut|=%d -- rejected (belongs to the |Aut|>2 case)\n", g_curBlock.c_str(), lastAut / 2); fflush(stdout); }
            return;
        }
        if (g_ownerMode) { std::lock_guard<std::mutex> lk(g_harvest_mtx); g_ownerSaved++; }

        if (g_patStat) { std::string p = patSeq(); std::lock_guard<std::mutex> lk(g_pat_mtx); auto& e = g_patEmit[p]; e.first++; e.second = lastAut; }   // [DIAG] REP_PATSTAT: completion pattern + |Aut|
        if (g_pDump) {   // [DIAG] REP_PDUMP: row-pairing involution of this completion, search frame
            const uint8_t* al = sh->alpha; const int z = al[0];   // sigma0 partner of vertex 0
            int pv[N]; for (int i = 0; i < N; i++) pv[i] = -1;
            for (const Match& F : chosen) pv[F[0]] = al[F[z]];
            std::string s; char t[16];
            for (int x = 1; x < N; x++) { if (pv[x] < 0) continue; snprintf(t, sizeof(t), "%d:%d,", x, pv[x]); s += t; }
            std::lock_guard<std::mutex> lk(g_pdump_mtx); g_pSeen[s]++;
        }
        bool localNew = canon.insert(key).second;    // first time THIS worker sees the class
        if (localNew) autOf[key] = lastAut;
        {
            std::lock_guard<std::mutex> lk(g_harvest_mtx);   // exact cross-worker tracking + capture-recapture
            // EVERY leaf detection counts (also re-finds): the f1/f2 spectrum feeds the
            // Chao1 richness estimate printed in the [rep] line.
            long long cdet = ++g_harvest[key];
            if (cdet > 1) {
                g_dupCovers.fetch_add(1, std::memory_order_relaxed);   // re-detection of a known class = rejected duplicate
            }
            if (cdet == 1) {
                g_f1.fetch_add(1, std::memory_order_relaxed); g_banked.store((int)g_harvest.size(), std::memory_order_relaxed);
                g_autHist[lastAut / 2]++;   // live |Aut| distribution (lastAut = 2*|Aut|); under g_harvest_mtx
                static const bool printEach = std::getenv("REP_PRINTEACH") != nullptr;   // per-result console line (restored)
                if (printEach) { printf("[emit] new class #%d  |Aut|=%d\n", (int)g_harvest.size(), lastAut / 2); fflush(stdout); }
                // send the NEW class to the normal result pipeline IMMEDIATELY. The mutex
                // serializes the host callback (it never runs concurrently), so long runs
                // deliver results as they are found instead of at order end.
                // Written once per RUN, not once per leg: g_emittedAll is never cleared (g_harvest is, per
                // REP_ORDERS token), so a class matched by two legs is written by the first and counted a
                // duplicate by the rest.  results = saved + duplicates.
                const bool globallyNew = g_emittedAll.insert(key).second;
                if (!globallyNew) {
                    g_crossDup.fetch_add(1, std::memory_order_relaxed);
                    g_crossDupAut[lastAut / 2]++;
                    g_runDupAut[lastAut / 2]++;   // the RUN total the ~ and = rows report
                }
                else { g_savedAut[lastAut / 2]++;   // |Aut| histogram of what THIS type actually wrote
                       g_runSavedAut[lastAut / 2]++; }   // ... and the RUN total the ~ and = rows report
                if (globallyNew && g_sendResult) {
                    unsigned char src[NM * N];
                    for (int k = 0; k < NM; k++) { uint8_t adj[N]; for (int u = 0; u < N; u++) adj[u] = (uint8_t)key[k * N + u]; adj_to_src(adj, src + k * N); }
                    g_sendResult(src, lastAut / 2);
                }
                if (g_printRawLeft > 0) {   // [DIAG] REP_PRINTRAW: the alpha-frame cover as the
                    g_printRawLeft--;       // search built it (rows sorted by partner of vertex 0)
                    std::vector<Match> fs(chosen.begin(), chosen.end());
                    std::sort(fs.begin(), fs.end(), [](const Match& a, const Match& b) { return a[0] < b[0]; });
                    printf("\n[K18-REP] RAW search-frame cover, class #%zu (v4Row = partner of 0, v4 = partner of 1):\n", g_harvest.size());
                    for (const auto& F : fs) {
                        unsigned char s[N]; adj_to_src(F.data(), s);
                        printf("  \"");
                        for (int i = 0; i < N; i++) printf(" %2d", s[i]);
                        printf(" \"\n");
                    }
                    fflush(stdout);
                }
            }
            else if (cdet == 2) { g_f1.fetch_sub(1, std::memory_order_relaxed); g_f2.fetch_add(1, std::memory_order_relaxed); }
            else if (cdet == 3) { g_f2.fetch_sub(1, std::memory_order_relaxed); }
            if (g_target > 0 && (int)g_harvest.size() >= g_target) g_stop.store(true, std::memory_order_relaxed);  // target hit -> bail everywhere
        }
    }

    void processM(const Match& M, const std::vector<int>& active) {
        std::vector<Match> orbit;     // the alpha-orbit {M, alphaM, ...} (a block of factors)
        if (!buildAndValidateOrbit(M, orbit)) return;
        size_t base = chosen.size();  // roll-back point
        commitOrbit(orbit);
        cover(active);                // recurse on the rest of the cover
        rollbackTo(base);
    }

    // [SGEN] Dedicated generator for a row whose type is already FORCED to S (its sigma0 pair partner is
    // committed as W). A sigma0-fixed factor lives on the quotient: every edge {u,v} comes with its mirror
    // {u^1,v^1}, and a diagonal {u,u^1} is its own mirror -- so walking the quotient emits ONLY sigma0-fixed
    // matchings (~50 per block) instead of the ~1,400 that genM builds and then discards at
    // is_perfect(F, sigma0 F). Measured waste at a forced-S row (block 483 row 4): 1,434 generated, 52 valid.
    // The free-vertex set stays sigma0-closed (edges are placed in mirror pairs and the seed is closed), so
    // the lowest free vertex u always has its mirror u^1 = u+1 free too. genM's closure prune is NOT applied
    // here, so this checks F against every chosen factor itself: buildAndValidateOrbit skips that test while
    // the prune is on (it relies on genM having guaranteed it).
    void genS(Match& M, uint32_t usedMask) {
        uint32_t freeMask = ~usedMask & FULLMASK;
        if (!freeMask) {
            for (const auto& C : chosen) if (!is_perfect(M, C)) return;
            if (buildAndValidateOrbit(M, orbScratch)) collectInto->push_back(M);
            return;
        }
        unsigned long u; _BitScanForward(&u, freeMask);
        const uint32_t um = (uint32_t)u ^ 1;                       // mirror of u (free by the closure invariant)
        uint32_t cand = freeMask & ~coveredBits[u] & ~(1u << u);
        while (cand) {
            unsigned long v; _BitScanForward(&v, cand); cand &= cand - 1;
            const uint32_t vm = (uint32_t)v ^ 1;
            if ((uint32_t)v == um) {                               // diagonal: its own mirror, one edge
                M[u] = (uint8_t)v; M[v] = (uint8_t)u;
                genS(M, usedMask | (1u << u) | (1u << v));
            }
            else {                                                  // ordinary edge: place it with its mirror
                if (!(freeMask & (1u << vm))) continue;             // mirror partner already matched
                if (coveredBits[um] & (1u << vm)) continue;         // mirror edge already covered
                M[u] = (uint8_t)v;  M[v] = (uint8_t)u;
                M[um] = (uint8_t)vm; M[vm] = (uint8_t)um;
                genS(M, usedMask | (1u << u) | (1u << v) | (1u << um) | (1u << vm));
            }
        }
    }

    // [DIAGPROBE] Does ANY sigma0-fixed factor exist on the leftover graph that contains the seeded
    // diagonal and is Hamiltonian with every committed row? Same quotient walk as genS, but it returns
    // on the FIRST one instead of collecting -- cheap while candidates are plentiful, expensive exactly
    // when it is about to report none. a=4 rule: a fixed factor carries exactly one diagonal, so no
    // second diagonal may be placed (that is the `~(1u << um)` term).
    //
    // [SPRUNE 2026-09-10] Hamiltonicity is now enforced INCREMENTALLY, with genM's closure ban, instead
    // of at the leaf. The VTune profile of the a=4 fast path after the list walk was fixed put this
    // function and its leaf is_perfect loop at ~68% of all CPU: a dead diagonal cost a complete
    // exhaustive failed search, because nothing was rejected before a whole matching existed. Now sPE
    // holds, per committed row, the far end of the alternating path through each free vertex, and an
    // edge (x,y) is refused when y is the far end of x's path in ANY row -- that edge would close a
    // cycle shorter than N with that row. The one exception is the last edge of the matching, whose
    // closure IS the Hamiltonian N-cycle. A completed matching therefore has every row Hamiltonian by
    // construction and the leaf test is gone.
    //
    // Same answer as before: a branch this refuses can never complete to a matching Hamiltonian with
    // that row, so no witness is lost; and every witness it accepts passed the same condition the old
    // leaf loop tested. The census tree is untouched (this decides a node's fate, not its children).
    // sPE is genM's path_end, kept as its own array so the prune cannot disturb an enumeration in flight.
    bool sExists(Match& M, uint32_t usedMask, int edges) {      // edges = matching edges placed so far
        uint32_t freeMask = ~usedMask & FULLMASK;
        if (!freeMask) return true;                              // every edge passed the ban => Hamiltonian with every row
        const int nchosen = (int)chosen.size();
        // [MRV] Branch on the free sigma0 pair with the FEWEST admissible partners, not the lowest one:
        // a dead diagonal then fails within a few nodes instead of after the lowest vertex's partners
        // are all exhausted, and an empty partner set ends the search on the spot. The partner set of
        // u is: free, edge {u,v} uncovered, v not u's own pair, v not the far end of u's path in any
        // committed row (that edge would close a short cycle -- (u,v) is never the last edge, its
        // mirror follows it), and the mirror edge {u^1,v^1} uncovered. Free vertices come in whole
        // pairs (edges are placed with their mirrors), so one member per pair is enough to scan.
        uint32_t cand = 0; unsigned u = 0; int bestCnt = 1 << 30;
        for (uint32_t fm = freeMask; fm; ) {
            unsigned long x; _BitScanForward(&x, fm); fm &= fm - 1;
            if (x & 1) continue;
            uint32_t hit = 0;
            for (int k = 0; k < nchosen; k++) hit |= 1u << sPE[k][x];
            const uint32_t cb = coveredBits[x ^ 1];
            const uint32_t mir = ((cb & 0x55555555u) << 1) | ((cb >> 1) & 0x55555555u);   // bit v set iff {x^1, v^1} is covered
            const uint32_t c = freeMask & ~coveredBits[x] & ~(1u << x) & ~(1u << (x ^ 1)) & ~hit & ~mir;
            const int cnt = (int)__popcnt(c);
            if (cnt < bestCnt) { bestCnt = cnt; u = (unsigned)x; cand = c; if (!cnt) return false; }
        }
        const uint32_t um = u ^ 1;
        const bool lastPair = (edges + 2 == NHALF);              // the mirror edge completes the matching
        while (cand) {
            unsigned long v; _BitScanForward(&v, cand); cand &= cand - 1;
            const uint32_t vm = (uint32_t)v ^ 1;
            if (!(freeMask & (1u << vm))) continue;              // mirror partner already matched
            if (coveredBits[um] & (1u << vm)) continue;          // mirror edge already covered
            // place (u,v): join u's and v's paths in every row, then ban the mirror edge on the joined paths
            for (int k = 0; k < nchosen; k++) { const uint8_t eu = sPE[k][u], ev = sPE[k][v]; sPE[k][eu] = ev; sPE[k][ev] = eu; }
            bool ok = true;
            if (!lastPair) for (int k = 0; k < nchosen; k++) if (sPE[k][um] == vm) { ok = false; break; }
            if (ok) {
                if (!lastPair) for (int k = 0; k < nchosen; k++) { const uint8_t eu = sPE[k][um], ev = sPE[k][vm]; sPE[k][eu] = ev; sPE[k][ev] = eu; }
                M[u] = (uint8_t)v;  M[v] = (uint8_t)u;
                M[um] = (uint8_t)vm; M[vm] = (uint8_t)um;
                const bool found = sExists(M, usedMask | (1u << u) | (1u << v) | (1u << um) | (1u << vm), edges + 2);
                if (!lastPair) for (int k = 0; k < nchosen; k++) { const uint8_t eu = sPE[k][um], ev = sPE[k][vm]; sPE[k][eu] = (uint8_t)um; sPE[k][ev] = (uint8_t)vm; }
                if (found) { for (int k = 0; k < nchosen; k++) { const uint8_t eu = sPE[k][u], ev = sPE[k][v]; sPE[k][eu] = (uint8_t)u; sPE[k][ev] = (uint8_t)v; } return true; }   // sPE restored on every exit
            }
            for (int k = 0; k < nchosen; k++) { const uint8_t eu = sPE[k][u], ev = sPE[k][v]; sPE[k][eu] = (uint8_t)u; sPE[k][ev] = (uint8_t)v; }
        }
        return false;
    }
    // Search for one diagonal. deadDiagonal has already laid the committed rows' paths into sPE; this
    // places the diagonal edge on them, searches, and puts sPE back. A found matching is kept as the
    // node's WITNESS for that diagonal (see deadDiagonal).
    bool diagRealizable(int d) {                                 // d = even endpoint of the diagonal {d, d^1}
        const int dm = d ^ 1;
        const int nchosen = (int)chosen.size();
        for (int k = 0; k < nchosen; k++) if (chosen[k][d] == dm) return false;   // a row already holds the diagonal: a 2-cycle with it
        for (int k = 0; k < nchosen; k++) { const uint8_t a = chosen[k][d], b = chosen[k][dm]; sPE[k][a] = b; sPE[k][b] = a; }   // the diagonal edge placed
        Match M; for (int i = 0; i < N; i++) M[i] = 0xFF;
        M[d] = (uint8_t)dm; M[dm] = (uint8_t)d;
        const bool live = sExists(M, (1u << d) | (1u << dm), 1);
        for (int k = 0; k < nchosen; k++) { const uint8_t a = chosen[k][d], b = chosen[k][dm]; sPE[k][a] = (uint8_t)d; sPE[k][b] = (uint8_t)dm; }   // and taken back
        if (live) { dgWit[nchosen][d >> 1] = M; dgWitValid[nchosen] |= 1u << (d >> 1); }   // [WITNESS] kept for the children
        return live;
    }
    // [WITNESS 2026-09-10] Does the parent's witness for a diagonal still prove it here? It was found
    // against the parent's covered edges and rows, so only the rows committed since (from..m-1) and the
    // edges they covered need testing: the diagonal, the sigma0 symmetry and the one-diagonal rule are
    // properties of the matching itself. A witness that holds IS a realization, the same object the
    // search would have produced, so the answer is unchanged -- only the search is skipped.
    bool witnessHolds(const Match& W, int from) const {
        for (int u = 0; u < N; u++) if ((coveredBits[u] >> W[u]) & 1u) return false;   // an edge of W is covered now
        const int m = (int)chosen.size();
        for (int k = from; k < m; k++) if (!is_perfect(W, chosen[k])) return false;
        return true;
    }
    // Is some uncovered diagonal unrealizable at this node? Used both by the measurement (diagProbe)
    // and by the prune (REP_DIAGPRUNE), so the two can never disagree.
    //
    // parentSize = the row count of the node whose witnesses may be reused (-1: none). The measured
    // cost split (blocks 1777-1780, 8 threads): 141M diagRealizable calls for 64M census nodes, 60% of
    // them live, each search only ~10-14 sExists nodes -- so the price is the NUMBER of searches and
    // their setup, not their depth. A live diagonal at the parent usually stays live at the child with
    // the SAME matching, and checking that costs one edge scan plus is_perfect against the new rows.
    // The diagonals whose witness broke are the ones worth searching, and they are searched first.
    bool deadDiagonal(int parentSize) {
        const int m = (int)chosen.size();
        uint32_t todo = 0;
        for (int d = 0; d < N; d += 2) {
            if (coveredBits[d] & (1u << (d ^ 1))) continue;      // covered: nothing to realize
            if (parentSize >= 0 && ((dgWitValid[parentSize] >> (d >> 1)) & 1u)) {
                const Match& W = dgWit[parentSize][d >> 1];
                if (witnessHolds(W, parentSize)) { dgWit[m][d >> 1] = W; dgWitValid[m] |= 1u << (d >> 1); continue; }
            }
            todo |= 1u << (d >> 1);
        }
        if (!todo) return false;
        for (int k = 0; k < m; k++) for (int x = 0; x < N; x++) sPE[k][x] = chosen[k][x];   // the rows' paths, once per node
        while (todo) {
            unsigned long i; _BitScanForward(&i, todo); todo &= todo - 1;
            if (!diagRealizable(2 * (int)i)) return true;
        }
        return false;
    }
    void diagProbe() {                                           // records only -- never prunes
        int depth = (int)chosen.size(); if (depth > NM + 1) return;
        bool dead = deadDiagonal(-1);
        std::lock_guard<std::mutex> lk(g_diag_mtx);
        g_diagProbes[depth]++; if (dead) g_diagHits[depth]++;
    }
    // [FILTER] REP_DIAGPRUNE: true when this node is provably unable to complete.
    bool diagPruned(int parentSize = -1) {
        const int depth = (int)chosen.size();
        if (depth < g_diagPruneLo || depth > g_diagPruneHi) return false;
        if (!deadDiagonal(parentSize)) return false;
        g_diagPruned.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // [SGEN] Row dispatch: a row whose sigma0 pair partner is already committed as W is FORCED to S, so it is
    // enumerated on the quotient by genS (only sigma0-fixed matchings) instead of filtering genM output. An S
    // factor also holds the mirror {1, v0^1} of the anchor edge {0,v0}: seeding both keeps the free-vertex set
    // sigma0-closed, which genS relies on. Falls back to genM whenever the shard filters bind vertex 1 or the
    // anchor is not at vertex 0.
    void collectRow(Match& M, uint32_t usedM, int u0, int v0, std::vector<Match>& cands) {
        // [ROW4] REP_ROW4=W pins row 4 to a swap factor, so the S-only generator must not serve that row.
        const bool useS = g_sGen && g_typeMask && sh->nV4 == 0 && u0 == 0 && v0 != 1 && patRowType(v0 ^ 1) == 'W'
                          && !(g_row4 == 'W' && v0 == 4);
        collectInto = &cands;
        if (useS) {
            const uint32_t a = 1, b = (uint32_t)v0 ^ 1;
            if (!(coveredBits[a] & (1u << b))) {          // the mirror edge must still be free
                M[a] = (uint8_t)b; M[b] = (uint8_t)a;
                genS(M, usedM | (1u << a) | (1u << b));
                M[a] = 0xFF; M[b] = 0xFF;
            }
        }
        else genM(M, usedM, 1);
        collectInto = nullptr;
    }

    // generate every perfect matching of the still-uncovered graph that contains
    // the forced first edge, pairing the lowest unused vertex with each legal
    // partner; prune partials that cannot stay Hamiltonian with a chosen factor.
    // BITMASK FORM (2026-07-05; from the K20 type-6 CPU profile where genM+imgUncovered
    // were 82% of the run). Semantics identical to the byte-array version, same candidate
    // order (ascending v), so the emitted matchings and all downstream dedup/keys match.
    // usedMask is passed BY VALUE so nothing needs undoing; the candidate partners of u
    // are computed as ONE uint32 mask, then iterated with tzcnt. The path-closure ban is
    // precomputed once per level (path_end[k][u] is invariant across the v-loop: every
    // recursive call restores it before returning). Group-image freeness tests the
    // SYMMETRIC coveredBits rows. Same source for every sibling N (sibling-code rule).
    void genM(Match& M, uint32_t usedMask, int edge_count) {
        uint32_t freeMask = ~usedMask & FULLMASK;
        if (!freeMask) {                           // matching complete -> record the orbit member
            const int gsLvl = g_genStat ? (int)chosen.size() : 0;   // [DIAG] REP_GENSTAT: level this candidate was built at
            if (g_genStat && gsLvl < NM + 2) g_genMade[gsLvl].fetch_add(1, std::memory_order_relaxed);
            if (buildAndValidateOrbit(M, orbScratch)) {   // reusable member buffer (no per-leaf alloc)
                if (g_genStat && gsLvl < NM + 2) g_genKept[gsLvl].fetch_add(1, std::memory_order_relaxed);
                collectInto->push_back(M);
            }
            return;
        }
        unsigned long u; _BitScanForward(&u, freeMask);    // lowest still-unmatched vertex
        int nchosen = (int)chosen.size();
        uint8_t epu[NM];                           // epu[k]=path_end[k][u]: INVARIANT across the v-loop below
        for (int k = 0; k < nchosen; k++) epu[k] = path_end[k][u];   // (u fixed; closure ban keeps path_end[k][u] unwritten)
        uint32_t cand = freeMask & ~coveredBits[u] & (FULLMASK << (u + 1));  // free, unused edge, v > u
        if (edge_count < NHALF - 1 && !g_noClosurePrune) {   // Hamiltonian-with-chosen pre-check (closure ban; REP_NOPRUNE disables)
            uint32_t hit = 0;
            for (int k = 0; k < nchosen; k++) hit |= 1u << epu[k];
            cand &= ~hit;
        }
        if (u == 1) {   // V4 shard filters bind the partner of 1 once the factor's partner
                        // of 0 (M[0], the cover anchor) is known; prune before recursing
            for (int i = 0; i < sh->nV4; i++)
                cand = (M[0] == sh->v4Row[i]) ? (cand & (1u << sh->v4Val[i]))
                                              : (cand & ~(1u << sh->v4Val[i]));
        }
        // [TYPEMASK] The sigma0 TYPE of this factor and the POSITION of its orbit partner are both decided by
        // the partner of vertex 1, so the (proven) one-per-pair rule becomes a mask here instead of a rejection
        // after the orbit is built. With kpos = M[0] = this factor's row (partner of 0):
        //   F(1) == kpos^1            <=> sigma0-FIXED (S), the factor is its own orbit;
        //   F(1) == y otherwise       => W, and the image factor occupies row q = y^1.
        // One-per-pair (each pair {p,p^1} holds one S and one W) then rejects, before generating them:
        // an S when the pair partner is already S; a W when the pair partner is already W; and a W whose image
        // would land on a taken row, or in a pair that already holds its W (= the pair-connection restriction).
        // [ROW4] REP_ROW4=S|W: pin the type of row 4 only. The type is decided by the partner of vertex 1:
        // F(1) == kpos^1 is the sigma0-fixed (S) choice, anything else is W. Independent of REP_TYPEMASK.
        if (u == 1 && g_row4 && M[0] == 4) {
            if (g_row4 == 'S') cand &= (1u << (4 ^ 1));
            else               cand &= ~(1u << (4 ^ 1));
        }
        if (u == 1 && g_typeMask) {
            const int kpos = M[0];
            const char sib = patRowType(kpos ^ 1);
            uint32_t allow = 0, c = cand;
            while (c) {
                unsigned long y; _BitScanForward(&y, c); c &= c - 1;
                if ((int)y == (kpos ^ 1)) {                     // S candidate
                    if (sib != 'S') allow |= 1u << y;
                }
                else {                                          // W candidate; image factor lands on row q
                    const int q = (int)y ^ 1;
                    if (sib == 'W') continue;
                    if (patRowType(q) != '?') continue;
                    if (patRowType(q ^ 1) == 'W') continue;
                    allow |= 1u << y;
                }
            }
            cand &= allow;
        }
        // [TYPEMASK] LEMMA: a factor holding a diagonal {u,u^1} is sigma0-FIXED -- its image holds that same
        // edge, and two distinct factors cannot share one. So once vertex 1 has settled this factor as W
        // (M[1] != M[0]^1), NO diagonal may appear anywhere in it. Ban it per vertex, instead of generating
        // the whole matching and only failing is_perfect(F, sigma0 F) in buildAndValidateOrbit at the leaf.
        if (g_typeMask && u >= 2 && M[1] != (M[0] ^ 1)) cand &= ~(1u << (u ^ 1));
        const uint32_t nextUsed = usedMask | (1u << u);
        const uint32_t uRow = (uint32_t)u * N;                 // edge id base: e = u*N + v (v > u here)
        const uint16_t* imgAll = sh->imgEdge.data();           // precomputed non-fixing group images of each edge
        const uint32_t* imgOff = sh->imgEdgeOff.data();        // CSR offsets (RepShared::buildImgEdges)
        while (cand) {
            unsigned long v; _BitScanForward(&v, cand); cand &= cand - 1;
            bool imgOk = true;                     // group-images of {u,v} must be free (precomputed list)
            for (uint32_t p = imgOff[uRow + v], pe = imgOff[uRow + v + 1]; p < pe; p++) {
                uint16_t ie = imgAll[p];
                if (coveredBits[ie >> 8] & (1u << (ie & 0xFF))) { imgOk = false; break; }
            }
            if (!imgOk) continue;
            uint8_t epv[NM];   // epu[] hoisted above (invariant); only epv[k]=path_end[k][v] varies per candidate
            // The path_end update and undo (the 46% of the run in the VTune line profile): one row
            // pointer stepping by N instead of k*N address arithmetic per factor, and unrolled by 3
            // (nchosen is a multiple of 3 until the sigma-fixed factor is in). Per factor the
            // statement order is unchanged -- row[a] = b then row[b] = a, and u then v on undo --
            // so the a == b case of the closing edge behaves exactly as before.
            {
                uint8_t* row = &path_end[0][0];
                int k = 0;
                for (; k + 3 <= nchosen; k += 3, row += 3 * N) {
                    const uint8_t a0 = epu[k], a1 = epu[k + 1], a2 = epu[k + 2];
                    const uint8_t b0 = row[v], b1 = row[N + v], b2 = row[2 * N + v];
                    epv[k] = b0; epv[k + 1] = b1; epv[k + 2] = b2;
                    row[a0] = b0; row[b0] = a0;
                    row[N + a1] = b1; row[N + b1] = a1;
                    row[2 * N + a2] = b2; row[2 * N + b2] = a2;
                }
                for (; k < nchosen; k++, row += N) { const uint8_t a = epu[k], b = row[v]; epv[k] = b; row[a] = b; row[b] = a; }
            }
            M[u] = (uint8_t)v; M[v] = (uint8_t)u;
            genM(M, nextUsed | (1u << v), edge_count + 1);
            {   // undo
                uint8_t* row = &path_end[0][0];
                const uint8_t uu = (uint8_t)u, vv = (uint8_t)v;
                int k = 0;
                for (; k + 3 <= nchosen; k += 3, row += 3 * N) {
                    row[epu[k]] = uu; row[epv[k]] = vv;
                    row[N + epu[k + 1]] = uu; row[N + epv[k + 1]] = vv;
                    row[2 * N + epu[k + 2]] = uu; row[2 * N + epv[k + 2]] = vv;
                }
                for (; k < nchosen; k++, row += N) { row[epu[k]] = uu; row[epv[k]] = vv; }
            }
        }
    }

    // [ESTIMATE MODE] one Knuth random descent: build the SAME deduped child list that
    // cover() would explore, multiply the running weight by the child count, recurse
    // into ONE uniformly-random child. Every cover() call = 1 node, so a leaf/dead-end
    // returns 1; a child whose orbit fails validation contributes 0 (cover() never
    // recurses into it). Unbiased estimator of the subtree cover-node count; variance
    // is high on skewed trees, so run many probes and read it as order-of-magnitude.
    double probeCover(const std::vector<int>& active, std::mt19937& rng) {
        int u0, v0;                   // cover anchor (filtered rows first, then lowest uncovered)
        pickAnchor(u0, v0);
        if (u0 < 0) return 1.0;
        if (!imgUncovered(u0, v0)) return 1.0;
        int nchosen = (int)chosen.size();
        for (int k = 0; k < nchosen; k++) for (int x = 0; x < N; x++) path_end[k][x] = chosen[k][x];
        Match M; for (int i = 0; i < N; i++) M[i] = 0xFF;
        uint32_t usedM = (1u << u0) | (1u << v0);
        M[u0] = (uint8_t)v0; M[v0] = (uint8_t)u0;
        for (int k = 0; k < nchosen; k++) { uint8_t eu = path_end[k][u0], ev = path_end[k][v0]; path_end[k][eu] = ev; path_end[k][ev] = eu; }
        std::vector<Match> cands;
        collectInto = &cands; genM(M, usedM, 1); collectInto = nullptr;
        if (cands.empty()) return 1.0;
        std::unordered_map<std::string, int> keyIdx;
        std::vector<std::string> keys;
        std::vector<Match> creps;
        for (auto& C : cands) {
            std::string k = orbitKey(C, sh->gElems);
            if (keyIdx.emplace(k, (int)creps.size()).second) { keys.push_back(std::move(k)); creps.push_back(C); }
        }
        std::vector<int> kidIdx; std::vector<std::vector<int>> kidStab;
        if (active.empty()) {
            for (size_t i = 0; i < creps.size(); i++) { kidIdx.push_back((int)i); kidStab.emplace_back(); }
        } else {
            std::vector<char> marked(creps.size(), 0);
            for (size_t i = 0; i < creps.size(); i++) {
                if (marked[i]) continue;
                std::vector<int> stab;
                for (int gi : active) {
                    std::string img = orbitKey(applyAlpha(creps[i], sh->Calpha[gi].data()), sh->gElems);
                    if (img == keys[i]) { stab.push_back(gi); continue; }
                    auto it = keyIdx.find(img);
                    if (it != keyIdx.end()) marked[it->second] = 1;
                }
                kidIdx.push_back((int)i); kidStab.push_back(std::move(stab));
            }
        }
        int kcount = (int)kidIdx.size();
        std::uniform_int_distribution<int> pickk(0, kcount - 1);
        int c = pickk(rng);
        std::vector<Match> orbit;
        if (!buildAndValidateOrbit(creps[kidIdx[c]], orbit)) return 1.0;   // picked child contributes 0 nodes
        size_t base = chosen.size();
        commitOrbit(orbit);
        double sub = probeCover(kidStab[c], rng);
        rollbackTo(base);
        return 1.0 + (double)kcount * sub;
    }

    // [ESTIMATE MODE] over-cap sibling of probeCover: build the SAME deduped child list
    // that coverGen() would explore (exact setwiseStab of the partial cover, then
    // anchor-edge-stabilizer dedup), multiply the running weight by the child count,
    // recurse into ONE uniformly-random child. Unbiased for the coverGen tree size.
    double probeCoverGen(std::mt19937& rng) {
        int u0, v0;                   // cover anchor (filtered rows first, then lowest uncovered)
        pickAnchor(u0, v0);
        if (u0 < 0) return 1.0;
        if (!imgUncovered(u0, v0)) return 1.0;
        int nchosen = (int)chosen.size();
        for (int k = 0; k < nchosen; k++) for (int x = 0; x < N; x++) path_end[k][x] = chosen[k][x];
        Match M; for (int i = 0; i < N; i++) M[i] = 0xFF;
        uint32_t usedM = (1u << u0) | (1u << v0);
        M[u0] = (uint8_t)v0; M[v0] = (uint8_t)u0;
        for (int k = 0; k < nchosen; k++) { uint8_t eu = path_end[k][u0], ev = path_end[k][v0]; path_end[k][eu] = ev; path_end[k][ev] = eu; }
        std::vector<Match> cands;
        collectInto = &cands; genM(M, usedM, 1); collectInto = nullptr;
        if (cands.empty()) return 1.0;
        std::unordered_map<std::string, int> keyIdx; std::vector<std::string> keys; std::vector<Match> reps;
        for (auto& C : cands) { std::string k = orbitKey(C, sh->gElems); if (keyIdx.emplace(k, (int)reps.size()).second) { keys.push_back(std::move(k)); reps.push_back(C); } }
        std::vector<Perm> Gp = cgt::setwiseStab<N>(sh->rootBSGS, chosen, covered);
        std::vector<int> repIdx;
        if (Gp.empty()) { for (size_t i = 0; i < reps.size(); i++) repIdx.push_back((int)i); }
        else { std::vector<Perm> hgens = cgt::edgeStabGens<N>(Gp, u0, v0); std::vector<std::vector<Perm>> stabs; schreierDedup(reps, keys, keyIdx, hgens, sh->gElems, repIdx, stabs); }
        int kcount = (int)repIdx.size();
        std::uniform_int_distribution<int> pickk(0, kcount - 1);
        std::vector<Match> orbit;
        if (!buildAndValidateOrbit(reps[repIdx[pickk(rng)]], orbit)) return 1.0;   // picked child contributes 0 nodes
        size_t base = chosen.size();
        commitOrbit(orbit);
        double sub = probeCoverGen(rng);
        rollbackTo(base);
        return 1.0 + (double)kcount * sub;
    }

    // [FAST SEARCH] randomized BACKTRACKING DFS from the committed root, bounded by `budget` nodes.
    // At each cover step it tries the valid factor-orbits in RANDOM order (backtracking on dead-ends)
    // and emit()s every complete cover reached; it stops when `budget` nodes are spent (the caller then
    // restarts from a fresh random root). The budget escapes barren subtrees; backtracking + random
    // order reach complete covers a single no-backtrack dive never would. No setwiseStab -- the global
    // canonKey dedups classes at emit. `budget` is shared by reference across the recursion.
    void diveGen(std::mt19937& rng, long long& budget, std::atomic<bool>* halt = nullptr) {
        if (budget <= 0 || g_stop.load(std::memory_order_relaxed) || (halt && halt->load(std::memory_order_relaxed))) return;
        nodes++; nodes_flush++; budget--; progressTick(nodes_flush, g_nodes);
        int u0, v0; pickAnchor(u0, v0);
        if (u0 < 0) { if ((int)chosen.size() == NM) emit(); return; }     // complete cover -> emit, backtrack
        if (!imgUncovered(u0, v0)) return;                                // dead-end
        int nchosen = (int)chosen.size();
        for (int k = 0; k < nchosen; k++) for (int x = 0; x < N; x++) path_end[k][x] = chosen[k][x];
        Match M; for (int i = 0; i < N; i++) M[i] = 0xFF;
        uint32_t usedM = (1u << u0) | (1u << v0);
        M[u0] = (uint8_t)v0; M[v0] = (uint8_t)u0;
        for (int k = 0; k < nchosen; k++) { uint8_t eu = path_end[k][u0], ev = path_end[k][v0]; path_end[k][eu] = ev; path_end[k][ev] = eu; }
        std::vector<Match> cands;
        collectInto = &cands; genM(M, usedM, 1); collectInto = nullptr;
        if (cands.empty()) { fanTally(nchosen, 0, 0); return; }                                        // dead-end
        std::unordered_map<std::string, int> keyIdx; std::vector<Match> creps;
        for (auto& C : cands) { std::string k = orbitKey(C, sh->gElems); if (keyIdx.emplace(k, (int)creps.size()).second) creps.push_back(C); }
        std::shuffle(creps.begin(), creps.end(), rng);                    // random candidate order
        for (auto& c : creps) {
            if (budget <= 0 || g_stop.load(std::memory_order_relaxed) || (halt && halt->load(std::memory_order_relaxed))) return;
            std::vector<Match> orbit;
            if (!buildAndValidateOrbit(c, orbit)) continue;
            size_t base = chosen.size();
            commitOrbit(orbit);
            diveGen(rng, budget);
            rollbackTo(base);
        }
    }

    // Orderly-generation cover step. `active` = the current stabilizer subgroup
    // G <= C(alpha) (as indices into sh->Calpha) that fixes the chosen factor set.
    // We collect every candidate factor-orbit covering the lowest uncovered edge,
    // keep ONE representative per G-orbit of candidates, and recurse each chosen
    // orbit O with its sub-stabilizer Stab_G(O). Sound for class counting: any
    // g in G maps {solutions extending P+O1} bijectively onto {solutions extending
    // P+O2} whenever g*O1 = O2, so exploring one representative finds every class
    // the others would (the global canonical key dedups any residual repeats).
    void cover(const std::vector<int>& active) {
        // local node counter; periodically flush to the shared atomic and (if enabled)
        // print one progress line per 300s. No ETA, no percentage — elapsed time only.
        nodes++; nodes_flush++;
        progressTick(nodes_flush, g_nodes);
        if (g_stop.load(std::memory_order_relaxed)) return;   // harvest target reached -> unwind the search

        int u0, v0;                   // cover anchor (filtered rows first, then lowest uncovered)
        pickAnchor(u0, v0);
        if (u0 < 0) { if ((int)chosen.size() == NM) emit(); return; }   // all edges covered -> a P1F
        if (!imgUncovered(u0, v0)) return;            // no alpha-orbit can cover this edge

        // 1. collect every candidate matching whose orbit covers the forced anchor edge.
        int nchosen = (int)chosen.size();
        for (int k = 0; k < nchosen; k++) for (int x = 0; x < N; x++) path_end[k][x] = chosen[k][x];  // reset path_end
        Match M; for (int i = 0; i < N; i++) M[i] = 0xFF;     // matching being built (0xFF = unset)
        uint32_t usedM = (1u << u0) | (1u << v0);             // vertices already matched (anchor edge forced)
        M[u0] = (uint8_t)v0; M[v0] = (uint8_t)u0;
        for (int k = 0; k < nchosen; k++) { uint8_t eu = path_end[k][u0], ev = path_end[k][v0]; path_end[k][eu] = ev; path_end[k][ev] = eu; }
        std::vector<Match> cands;
        collectRow(M, usedM, u0, v0, cands);
        if (cands.empty()) { fanTally(nchosen, 0, 0); return; }

        // 2. collapse to one member per distinct alpha-orbit (genM may emit several
        //    matchings that lie in the same orbit), keyed by the orbit serialization.
        std::unordered_map<std::string, int> keyIdx;   // orbit key -> representative index
        std::vector<std::string> keys;                 // keys[i] = orbit key of reps[i]
        std::vector<Match> reps;                        // one matching per distinct orbit
        for (auto& C : cands) {
            std::string k = orbitKey(C, sh->gElems);
            if (keyIdx.emplace(k, (int)reps.size()).second) { keys.push_back(std::move(k)); reps.push_back(C); }
        }

        // 3a. no usable symmetry (G empty: over-cap/sparse types) -> explore every orbit;
        //     the global canonical key removes any labeling duplicates downstream.
        if (active.empty()) {
            for (auto& R : reps) processM(R, active);
            return;
        }

        // 3b. dedup candidate orbits by G and recurse each representative with Stab_G(O).
        std::vector<char> marked(reps.size(), 0);       // marked[j] = orbit j is in an already-handled G-orbit
        for (size_t i = 0; i < reps.size(); i++) {
            if (marked[i]) continue;
            std::vector<int> stab;                      // gammas in G fixing this orbit = Stab_G(O)
            for (int gi : active) {
                std::string img = orbitKey(applyAlpha(reps[i], sh->Calpha[gi].data()), sh->gElems);
                if (img == keys[i]) { stab.push_back(gi); continue; }   // gamma fixes the orbit
                auto it = keyIdx.find(img);             // gamma maps it onto another candidate -> same G-orbit
                if (it != keyIdx.end()) marked[it->second] = 1;
            }
            processM(reps[i], stab);                     // commit orbit, recurse with the sub-stabilizer
        }
    }

    // Cover step for OVER-CAP types, where C(alpha) is too large to enumerate (sh->Calpha
    // empty). The candidate-collection is identical to cover(); the symmetry reduction
    // recomputes the EXACT Stab_C(P) of the current partial cover from scratch each node
    // (setwiseStab over C(alpha)'s generators), then dedups candidates by the stabilizer of
    // the anchor edge within it. Computing Stab_C(P) exactly per node (rather than carrying an
    // under-approximating generator chain) is what makes the dense types -- order-2 2^9 in
    // particular -- terminate: the dedup is full strength at every depth. Sound for counting
    // (dedup is by a genuine subgroup; global canonKey is the final net).
    // [DIAG] REP_F17DUMP: at a node with NM-1 committed factors, the uncovered edges are the FORCED
    // last factor F17. Count it, and once per 60s print the obstruction -- which earlier factor(s) F17
    // is non-Hamiltonian with, the cycle partition of F17 U F_k, the shortest clash cycle -- plus the
    // running histogram of the partition shape (to reveal whether the obstruction is uniform/local).
    void f17Sample() {
        long long tot = g_f17Total.fetch_add(1, std::memory_order_relaxed) + 1;
        bool doHist = (tot % g_f17Sub) == 0;                    // subsample the histogram densely (read-only, tree-neutral)
        double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
        bool doPrint = false;                                   // detailed print is still throttled to once / 60s
        long long due = g_f17NextSec.load(std::memory_order_relaxed);
        if (el >= due) { long long nxt = ((long long)(el / 60.0) + 1) * 60; doPrint = g_f17NextSec.compare_exchange_strong(due, nxt, std::memory_order_relaxed); }
        if (!doHist && !doPrint) return;                        // nothing to record or print at this node
        Match f17; bool okF = true;                             // forced last factor = the uncovered edges (1 per vertex)
        for (int u = 0; u < N; u++) { f17[u] = 0xFF; for (int v = 0; v < N; v++) if (v != u && !(coveredBits[u] & (1u << v))) { f17[u] = (uint8_t)v; break; } if (f17[u] == 0xFF) okF = false; }   // coveredBits is the SYMMETRIC map (covered[][] is upper-triangular only)
        if (!okF) return;                                       // not a clean forced matching (should not happen at NM-1)
        int bestK = -1, nclash = 0; std::vector<int> bestParts, bestShort;
        for (int k = 0; k < (int)chosen.size(); k++) {          // find the tightest (shortest-cycle) non-Hamiltonian clash
            std::vector<int> sc; std::vector<int> parts = unionCycleParts(f17, chosen[k], &sc);
            if (parts.size() > 1) { nclash++; if (bestK < 0 || sc.size() < bestShort.size()) { bestK = k; bestParts = parts; bestShort = sc; } }
        }
        std::string pk;                                         // partition key, e.g. "9+9"
        for (size_t i = 0; i < bestParts.size(); i++) pk += (i ? "+" : "") + std::to_string(bestParts[i]);
        if (bestK < 0) pk = "NOCLASH";
        std::lock_guard<std::mutex> lk(g_f17_mtx);
        if (doHist) { g_f17Hist[pk]++; g_f17ClashHist[nclash]++; }
        if (!doPrint || !g_bprint) return;
        auto edges = [](const Match& m) { std::string s; for (int u = 0; u < N; u++) { int v = m[u]; if (u < v) { char t[16]; snprintf(t, sizeof(t), "{%d,%d}", u, v); s += t; } } return s; };
        std::string sc; for (size_t i = 0; i < bestShort.size(); i++) { char t[8]; snprintf(t, sizeof(t), "%s%d", i ? " " : "", bestShort[i]); sc += t; }
        std::string hist; for (auto& kv : g_f17Hist) { char t[48]; snprintf(t, sizeof(t), "%s%s:%lld", hist.empty() ? "" : "  ", kv.first.c_str(), kv.second); hist += t; }
        std::string ch; for (auto& kv : g_f17ClashHist) { char t[48]; snprintf(t, sizeof(t), "%s%d:%lld", ch.empty() ? "" : " ", kv.first, kv.second); ch += t; }
        printf("[F17] t=%.0fs forced-17th nodes=%lld (sampled 1/%lld) | clashes=%d tightest=F#%d part=%s shortcyc(len%zu)=[%s]\n",
               el, tot, g_f17Sub, nclash, bestK, pk.c_str(), bestShort.size(), sc.c_str());
        printf("      F17 = %s\n", edges(f17).c_str());
        if (bestK >= 0) printf("      F#%d = %s\n", bestK, edges(chosen[bestK]).c_str());
        printf("      obstruction histogram (sampled): %s\n", hist.c_str());
        printf("      clash-count histogram (sampled): %s\n", ch.c_str());
        fflush(stdout);
    }

    void coverGen(bool parentTrivial) {
        nodes++; nodes_flush++;
        progressTick(nodes_flush, g_nodes);
        if (g_stop.load(std::memory_order_relaxed)) return;   // harvest target reached -> unwind the search
        { int cs = (int)chosen.size(); int pv = g_maxDepth.load(std::memory_order_relaxed);   // diagnostic: track deepest partial factorization
          while (cs > pv && !g_maxDepth.compare_exchange_weak(pv, cs, std::memory_order_relaxed)) {} }
        if (g_f17dump && (int)chosen.size() == NM - 1) f17Sample();   // [DIAG] REP_F17DUMP: forced-17th obstruction sampler
        if (g_lvlStats) { int m = (int)chosen.size(); if (m < NM + 2) g_lvlReached[m].fetch_add(1, std::memory_order_relaxed); }   // [DIAG] REP_LEVELSTATS
        if (g_probe1 > 0 && (int)chosen.size() == g_probe1) {   // [DIAG] REP_PROBE1: U == sigma0(R) ?
            bool row[N] = { false }, one[N] = { false };        // row[x]: (0,x) committed;  one[v]: (1,v) committed
            for (const Match& F : chosen) { row[F[0]] = true; one[F[1]] = true; }
            bool bad = false;
            for (int x = 1; x < N && !bad; x++) {               // R = { x : !row[x] } ; U = { v : !one[v] }
                if (row[x]) continue;                           // x committed -> sigma0(x) must be matched to 1
                if (one[sh->alpha[x]]) bad = true;              // required partner already used up
            }
            g_probeSeen.fetch_add(1, std::memory_order_relaxed);
            if (bad) g_probeBad.fetch_add(1, std::memory_order_relaxed);
        }
        if (g_diagProbe > 0 && (int)chosen.size() >= g_diagProbe
            && (g_diagSeen.fetch_add(1, std::memory_order_relaxed) % g_diagSub) == 0) diagProbe();   // [DIAG] REP_DIAGPROBE: unrealizable-diagonal look-ahead (measure only)
        if (g_diagPruneLo > 0 && diagPruned()) return;   // [FILTER] REP_DIAGPRUNE: an uncovered diagonal has no legal S row -> this node cannot complete
        if (g_patStat) { int m = (int)chosen.size();   // [DIAG] REP_PATSTAT: forced-17th (near-complete) + optional mid-level pattern tally
            if (m == NM - 1) { std::string p = patSeq(); std::lock_guard<std::mutex> lk(g_pat_mtx); g_patF17[p]++; }
            else if (g_patLevel > 0 && m == g_patLevel && (g_patMidSeen.fetch_add(1, std::memory_order_relaxed) % g_patSub) == 0) { std::string p = patSeq(); std::lock_guard<std::mutex> lk(g_pat_mtx); g_patMid[p]++; } }
        if (g_swapCap) { int w = 0; for (const auto& F : chosen) if (applyAlpha(F, sh->alpha) != F) w++; if (w > 8) return; }   // [FILTER] REP_SWAPCAP: >4 sigma0-swap-pairs cannot complete to a=4 (dichotomy) -> prune (a=4 blocks only)
        if (g_patApply && !patOK()) return;   // [PATFILTER] neighbour-0 reject (set target or calculated)
        if (g_patPair && !patPairOK()) return;   // [PATFILTER] REP_PATPAIR one-per-pair reject (complete |Aut|=2 filter, no fixed pattern)
        if (g_ovlLevel > 0 && (int)chosen.size() >= g_ovlLevel) {   // [DIAG] REP_CANONOVL: cap here too (inline coverGen fallback path)
            if (!partialAllSym(chosen)) g_ovlNonSym.fetch_add(1, std::memory_order_relaxed);
            // [FIX] canonKey() indexes f[0..NM-1] unconditionally, so calling it on a PARTIAL (size
            // < NM) reads past the end of the vector -- REP_CANONKEY segfaulted here. canonV_fast is
            // the partial-safe canonizer: it is what block canonization already uses on the 5-factor
            // starters, and it yields the same order-independent key.
            int sz = (int)chosen.size(); std::string ck;
            if (g_ovlKey) { uint8_t k4[NEDGES]; canonV_fast(chosen, sh->alpha, k4); ck.assign((const char*)k4, NEDGES); }
            { std::lock_guard<std::mutex> lk(g_ovlMtx); g_ovlTot[sz]++; if (g_ovlKey) g_ovlSet[sz].insert(std::move(ck)); }
            return;
        }

        int u0, v0;                   // cover anchor (filtered rows first, then lowest uncovered)
        pickAnchor(u0, v0);
        if (u0 < 0) { if ((int)chosen.size() == NM) emit(); return; }
        if (!imgUncovered(u0, v0)) return;

        // 1. collect every candidate matching whose orbit covers the forced anchor edge
        int nchosen = (int)chosen.size();
        for (int k = 0; k < nchosen; k++) for (int x = 0; x < N; x++) path_end[k][x] = chosen[k][x];
        Match M; for (int i = 0; i < N; i++) M[i] = 0xFF;
        uint32_t usedM = (1u << u0) | (1u << v0);
        M[u0] = (uint8_t)v0; M[v0] = (uint8_t)u0;
        for (int k = 0; k < nchosen; k++) { uint8_t eu = path_end[k][u0], ev = path_end[k][v0]; path_end[k][eu] = ev; path_end[k][ev] = eu; }
        std::vector<Match> cands;
        collectRow(M, usedM, u0, v0, cands);
        if (cands.empty()) { fanTally(nchosen, 0, 0); return; }

        // 2. collapse to one member per distinct alpha-orbit
        std::unordered_map<std::string, int> keyIdx;
        std::vector<std::string> keys;
        std::vector<Match> reps;
        for (auto& C : cands) {
            std::string k = orbitKey(C, sh->gElems);
            if (keyIdx.emplace(k, (int)reps.size()).second) { keys.push_back(std::move(k)); reps.push_back(C); }
        }
        fanTally(nchosen, cands.size(), reps.size());

        // 3. exact stabilizer of the current partial cover within C(alpha)
        // 3-. TRIVIAL SHORT-CIRCUIT: a node up this path already had a trivial Stab_C(P);
        //     every descendant's stabilizer is a subgroup of that, hence also trivial, so
        //     setwiseStab here would return {} after a full (pruned) walk of C(alpha)'s BSGS.
        //     Skip it and explore every orbit -- identical to what the Gp.empty() branch does,
        //     and the global canonKey is still the final dedup. Node-count- and class-neutral.
        if (parentTrivial) { for (auto& R : reps) processMGen(R, true); return; }
        // REP_PRUNELEVEL: past this factor-depth, force-skip the setwiseStab prune (explore all orbits,
        // mark the subtree trivial so descendants skip too); global canonKey remains the exact dedup.
        if (g_pruneMaxFactors > 0 && nchosen >= g_pruneMaxFactors) { for (auto& R : reps) processMGen(R, true); return; }

        std::vector<Perm> Gp = cgt::setwiseStab<N>(sh->rootBSGS, chosen, covered);

        // 3a. trivial stabilizer -> no usable symmetry anywhere below, explore every orbit
        //     (and mark the whole subtree trivial so descendants skip setwiseStab entirely)
        if (Gp.empty()) { for (auto& R : reps) processMGen(R, true); return; }

        // 3b. dedup candidates by the stabilizer of the anchor edge within Stab_C(P). Its
        //     generators fix (u0,v0), so the candidate-restricted BFS stays inside the
        //     candidate set and recovers the full Stab_C(P)(edge)-orbit dedup at this node.
        std::vector<Perm> hgens = cgt::edgeStabGens<N>(Gp, u0, v0);
        std::vector<int> repIdx;
        std::vector<std::vector<Perm>> stabs;            // (Schreier stabs unused: Stab is recomputed per node)
        schreierDedup(reps, keys, keyIdx, hgens, sh->gElems, repIdx, stabs);
        for (size_t r = 0; r < repIdx.size(); r++) processMGen(reps[repIdx[r]], false);
    }

    void processMGen(const Match& M, bool childTrivial) { // over-cap sibling of processM
        std::vector<Match> orbit;
        if (!buildAndValidateOrbit(M, orbit)) return;
        size_t base = chosen.size();
        commitOrbit(orbit);
        // [PERF] HOIST: processMGen runs per node, and getenv() on Windows walks and upcases the whole
        // environment block every call. Profiling the t9 a=0 census (2026-08-19) showed ~4.3% of samples
        // in RtlCreateUnicodeString / RtlUpcaseUnicodeString / RtlQueryFeatureConfiguration from this one
        // line. Read it once, as line ~1147 already does for REP_PRINTEACH.
        static const bool vtest = std::getenv("REP_VTEST") != nullptr;
        if (vtest && !chosen.empty()) {   // canonV_fast must be INVARIANT under C(sigma0) gens
            static std::atomic<int> vb{ 40 };
            if (vb.fetch_sub(1) > 0) {
                uint8_t base[NEDGES]; canonV_fast(chosen, sh->alpha, base);
                bool ok = true; int ng = 0;
                for (const auto& g : sh->rootCgens) {
                    std::vector<Match> img(chosen.size());
                    for (size_t i = 0; i < chosen.size(); i++) img[i] = applyAlpha(chosen[i], g.data());
                    uint8_t v[NEDGES]; canonV_fast(img, sh->alpha, v);
                    ng++; if (memcmp(v, base, NEDGES) != 0) { ok = false; break; }
                }
                static std::atomic<int> vn{ 0 }, vf{ 0 }; int c = ++vn; if (!ok) ++vf;
                fprintf(stderr, "[vtest-k18] partial #%d (%d fac, %d gens): %s [%d checks, %d FAIL]\n", c, (int)chosen.size(), ng, ok ? "PASS" : "FAIL", (int)vn, (int)vf); fflush(stderr);
            }
        }
        coverGen(childTrivial);
        rollbackTo(base);
    }

    // One-level expansion of the current partial cover (state already committed): collect the
    // DEDUPED child partial covers (each = current chosen + a child orbit) into outChildren
    // instead of recursing; complete covers are emitted here. Mirrors coverGen's dedup so the
    // children are exactly the subtrees coverGen would explore. Used to build a fine task
    // frontier so the dense over-cap types (few root reps) can use all worker threads.
    void splitNode(bool parentTrivial, std::vector<std::vector<Match>>& outChildren, std::vector<char>& outTrivial) {
        nodes++; nodes_flush++;
        progressTick(nodes_flush, g_nodes);
        if (g_stop.load(std::memory_order_relaxed)) return;   // harvest target reached -> unwind the search
        if (g_f17dump && (int)chosen.size() == NM - 1) f17Sample();   // [DIAG] REP_F17DUMP: forced-17th sampler (BFS/work-queue path)
        if (g_lvlStats) { int m = (int)chosen.size(); if (m < NM + 2) g_lvlReached[m].fetch_add(1, std::memory_order_relaxed); }   // [DIAG] REP_LEVELSTATS
        if (g_probe1 > 0 && (int)chosen.size() == g_probe1) {   // [DIAG] REP_PROBE1: U == sigma0(R) ?
            bool row[N] = { false }, one[N] = { false };        // row[x]: (0,x) committed;  one[v]: (1,v) committed
            for (const Match& F : chosen) { row[F[0]] = true; one[F[1]] = true; }
            bool bad = false;
            for (int x = 1; x < N && !bad; x++) {               // R = { x : !row[x] } ; U = { v : !one[v] }
                if (row[x]) continue;                           // x committed -> sigma0(x) must be matched to 1
                if (one[sh->alpha[x]]) bad = true;              // required partner already used up
            }
            g_probeSeen.fetch_add(1, std::memory_order_relaxed);
            if (bad) g_probeBad.fetch_add(1, std::memory_order_relaxed);
        }
        if (g_diagProbe > 0 && (int)chosen.size() >= g_diagProbe
            && (g_diagSeen.fetch_add(1, std::memory_order_relaxed) % g_diagSub) == 0) diagProbe();   // [DIAG] REP_DIAGPROBE (work-queue path -- where F3COMPLETE actually spends its nodes)
        if (g_diagPruneLo > 0 && diagPruned()) return;   // [FILTER] REP_DIAGPRUNE (work-queue path)
        if (g_patStat) { int m = (int)chosen.size();   // [DIAG] REP_PATSTAT: forced-17th (near-complete) + optional mid-level pattern tally
            if (m == NM - 1) { std::string p = patSeq(); std::lock_guard<std::mutex> lk(g_pat_mtx); g_patF17[p]++; }
            else if (g_patLevel > 0 && m == g_patLevel && (g_patMidSeen.fetch_add(1, std::memory_order_relaxed) % g_patSub) == 0) { std::string p = patSeq(); std::lock_guard<std::mutex> lk(g_pat_mtx); g_patMid[p]++; } }
        if (g_swapCap) { int w = 0; for (const auto& F : chosen) if (applyAlpha(F, sh->alpha) != F) w++; if (w > 8) return; }   // [FILTER] REP_SWAPCAP: >4 sigma0-swap-pairs cannot complete to a=4 (dichotomy) -> prune (a=4 blocks only)
        if (g_patApply && !patOK()) return;   // [PATFILTER] neighbour-0 reject (set target or calculated)
        if (g_patPair && !patPairOK()) return;   // [PATFILTER] REP_PATPAIR one-per-pair reject (complete |Aut|=2 filter, no fixed pattern)
        int u0, v0;                   // cover anchor (filtered rows first, then lowest uncovered)
        pickAnchor(u0, v0);
        if (u0 < 0) { if ((int)chosen.size() == NM) emit(); return; }     // leaf -> emit, no children
        if (!imgUncovered(u0, v0)) return;                                // dead end
        int nchosen = (int)chosen.size();
        for (int k = 0; k < nchosen; k++) for (int x = 0; x < N; x++) path_end[k][x] = chosen[k][x];
        Match M; for (int i = 0; i < N; i++) M[i] = 0xFF;
        uint32_t usedM = (1u << u0) | (1u << v0);
        M[u0] = (uint8_t)v0; M[v0] = (uint8_t)u0;
        for (int k = 0; k < nchosen; k++) { uint8_t eu = path_end[k][u0], ev = path_end[k][v0]; path_end[k][eu] = ev; path_end[k][ev] = eu; }
        std::vector<Match>& cands = snCands; cands.clear();                 // reused buffers -> no per-node heap churn
        collectRow(M, usedM, u0, v0, cands);
        if (cands.empty()) { fanTally(nchosen, 0, 0); return; }
        std::unordered_map<std::string, int>& keyIdx = snKeyIdx; keyIdx.clear();
        std::vector<std::string>& keys = snKeys; keys.clear();
        std::vector<Match>& reps = snReps; reps.clear();
        for (auto& C : cands) { std::string k = orbitKey(C, sh->gElems); if (keyIdx.emplace(k, (int)reps.size()).second) { keys.push_back(std::move(k)); reps.push_back(C); } }
        fanTally(nchosen, cands.size(), reps.size());
        std::vector<int>& repIdx = snRepIdx; repIdx.clear();
        bool childTrivial;
        if (parentTrivial || (g_pruneMaxFactors > 0 && nchosen >= g_pruneMaxFactors)) {   // trivial up-path OR REP_PRUNELEVEL past depth -> skip setwiseStab, explore all, stay trivial
            for (size_t i = 0; i < reps.size(); i++) repIdx.push_back((int)i); childTrivial = true;
        } else {
            std::vector<Perm> Gp = cgt::setwiseStab<N>(sh->rootBSGS, chosen, covered);
            if (Gp.empty()) { for (size_t i = 0; i < reps.size(); i++) repIdx.push_back((int)i); childTrivial = true; }
            else { std::vector<Perm> hgens = cgt::edgeStabGens<N>(Gp, u0, v0); std::vector<std::vector<Perm>> stabs; schreierDedup(reps, keys, keyIdx, hgens, sh->gElems, repIdx, stabs); childTrivial = false; }
        }
        for (int ri : repIdx) {
            std::vector<Match> orbit;
            if (!buildAndValidateOrbit(reps[ri], orbit)) continue;
            std::vector<Match> child = chosen; for (auto& F : orbit) child.push_back(F);
            outChildren.push_back(std::move(child));
            outTrivial.push_back(childTrivial ? (char)1 : (char)0);
        }
    }

    // [PATHCOORDCANON helper] Build the raw orbitKey-deduped child reps at the current anchor (same
    // list/order splitNode & PATHCOORD use -> PL-invariant), AND expose the C(sigma0) node-stabilizer
    // generators hgens (edge-stab of the setwise stabilizer of the committed cover). The caller can
    // then minimize the child index over the hgens-orbit = the canonized (min-over-C(sigma0)) a.b.c
    // component. Commits nothing. Returns false on leaf/dead-end.
    bool childReps(std::vector<Match>& reps, std::vector<std::string>& keys,
                   std::unordered_map<std::string, int>& keyIdx, std::vector<Perm>& hgens,
                   int& u0, int& v0) {
        pickAnchor(u0, v0);
        if (u0 < 0) return false;
        if (!imgUncovered(u0, v0)) return false;
        int nchosen = (int)chosen.size();
        for (int k = 0; k < nchosen; k++) for (int x = 0; x < N; x++) path_end[k][x] = chosen[k][x];
        Match M; for (int i = 0; i < N; i++) M[i] = 0xFF;
        uint32_t usedM = (1u << u0) | (1u << v0);
        M[u0] = (uint8_t)v0; M[v0] = (uint8_t)u0;
        for (int k = 0; k < nchosen; k++) { uint8_t eu = path_end[k][u0], ev = path_end[k][v0]; path_end[k][eu] = ev; path_end[k][ev] = eu; }
        std::vector<Match> cands; collectInto = &cands; genM(M, usedM, 1); collectInto = nullptr;
        if (cands.empty()) return false;
        for (auto& C : cands) { std::string k = orbitKey(C, sh->gElems); if (keyIdx.emplace(k, (int)reps.size()).second) { keys.push_back(std::move(k)); reps.push_back(C); } }
        std::vector<Perm> Gp = cgt::setwiseStab<N>(sh->rootBSGS, chosen, covered);
        hgens = Gp.empty() ? std::vector<Perm>() : cgt::edgeStabGens<N>(Gp, u0, v0);
        return true;
    }


    // collect the valid first-factor-orbits (parallel tasks) into `tasks`, by running
    // the root-level matching generation once. Each task is the matching M0 that
    // contains the lowest edge (0,1); its orbit is the first block of the cover.
    // [PRECALC] the seed's list: every orbit admissible against the current `chosen` -- for each
    // uncovered edge the rows through it (genM under the closure ban, images free), grouped by
    // orbit key, each orbit stored once with its sorted members and its edge bitmap.
    std::shared_ptr<OrbList> buildOrbList() {
        if (sh->gElems.size() > 2 || sh->nV4 > 0) { fprintf(stderr, "REP_PRECALC: prototype supports orbits of size <= 3 and no V4 filters\n"); exit(3); }
        auto L = std::make_shared<OrbList>();
        std::unordered_set<std::string> seen;
        const int nchosen = (int)chosen.size();
        std::vector<Match> cands, orbit;
        for (int u0 = 0; u0 < N; u0++) for (int v0 = u0 + 1; v0 < N; v0++) {
            if (covered[u0][v0] || !imgUncovered(u0, v0)) continue;
            for (int k = 0; k < nchosen; k++) for (int x = 0; x < N; x++) path_end[k][x] = chosen[k][x];
            Match M; for (int i = 0; i < N; i++) M[i] = 0xFF;
            uint32_t usedM = (1u << u0) | (1u << v0);
            M[u0] = (uint8_t)v0; M[v0] = (uint8_t)u0;
            for (int k = 0; k < nchosen; k++) { uint8_t eu = path_end[k][u0], ev = path_end[k][v0]; path_end[k][eu] = ev; path_end[k][ev] = eu; }
            cands.clear(); collectInto = &cands; genM(M, usedM, 1); collectInto = nullptr;
            for (auto& C : cands) {
                std::string key = orbitKey(C, sh->gElems);
                if (!seen.insert(std::move(key)).second) continue;
                if (!buildAndValidateOrbit(C, orbit)) continue;      // sorted, deduped members: [0] is the least
                OrbEnt e; e.nrows = (uint8_t)orbit.size();
                for (int r = 0; r < e.nrows; r++) e.rows[r] = orbit[r];
                OrbBits b; b.w[0] = b.w[1] = b.w[2] = b.w[3] = 0;
                for (int r = 0; r < e.nrows; r++) for (int u = 0; u < N; u++) { int v = e.rows[r][u]; if (u < v) { int id = eidT[u][v]; b.w[id >> 6] |= 1ull << (id & 63); } }
                L->ent.push_back(e); L->bits.push_back(b);          // index-parallel, always pushed together
            }
        }
        return L;
    }

    // [PRECALC] one level of the cover from a list: candidates = admissible entries through the
    // anchor edge; each child = chosen + the entry's rows, with the parent's list filtered by it.
    // [TYPEMASK] The genM hook at u==1, restated as a predicate on a finished entry so the list
    // walk can apply it too. genM masks vertex 1's partner while the matching is being built; here
    // the matching already exists, so the same rule becomes a test on its base row.
    //
    // It CANNOT be baked into the list at build time: patRowType() reads `chosen`, so the verdict
    // changes as factors are committed. The list is built once at the block prefix and already
    // carries the mask AS OF THAT PREFIX -- entries that go bad deeper were never re-checked, and
    // that is what this restores.
    //
    // The companion u>=2 rule (a W factor may hold no diagonal) needs no restating: it depends on
    // the entry alone, so genM already enforced it when the list was built.
    bool typeMaskOK(const OrbEnt& ce) const {
        const Match& F = ce.rows[0];                 // the base row -- the factor genM would have built
        const int kpos = F[0];
        const char sib = patRowType(kpos ^ 1);
        if (F[1] == (kpos ^ 1)) return sib != 'S';   // S candidate
        const int q = F[1] ^ 1;                      // W candidate; the image factor lands on row q
        if (sib == 'W') return false;
        if (patRowType(q) != '?') return false;
        if (patRowType(q ^ 1) == 'W') return false;
        return true;
    }

    void splitNodeL(const OrbTask& t, std::vector<std::vector<Match>>& outChildren, std::vector<char>& outTrivial, std::vector<OrbTask>& outTrip) {
        nodes++; nodes_flush++;
        progressTick(nodes_flush, g_nodes);
        if (g_stop.load(std::memory_order_relaxed)) return;
        // [FILTER] REP_DIAGPRUNE, as coverGen and splitNode apply it. The test is a property of
        // the partial cover -- an uncovered diagonal with no legal S row means this node cannot
        // complete -- so it does not care how the children were produced, and the list path was
        // simply missing the call rather than being unable to make it.
        if (g_diagPruneLo > 0 && diagPruned()) return;
        int u0, v0; pickAnchor(u0, v0);
        if (u0 < 0) { if ((int)chosen.size() == NM) emit(); return; }
        if (!imgUncovered(u0, v0)) return;
        const OrbList& L = *t.root;
        const int eA = eidT[u0][v0];
        const int aW = eA >> 6; const uint64_t aB = 1ull << (eA & 63);
        // ONE pass, not two: build this node's admissible list (the parent's, filtered by the
        // entry the parent committed) and pick the candidates through the anchor edge as we go.
        // Appending to *mine while children already hold the shared_ptr is safe -- they reference
        // the vector object, not its buffer, and are not read until this returns.
        const OrbBits* const B = L.bits.data();     // planes hoisted: no vector::operator[] per entry
        const OrbEnt*  const E = L.ent.data();
        auto mine = std::make_shared<NodeList>();
        std::vector<uint32_t> cand;
        if (!t.plist) {                              // the seed: every entry is admissible
            const uint32_t M = (uint32_t)L.ent.size();
            mine->idx.resize(M); mine->bits.resize(M);
            for (uint32_t i = 0; i < M; i++) { mine->idx[i] = i; mine->bits[i] = B[i]; if (B[i].w[aW] & aB) cand.push_back(i); }
        }
        else {
            const OrbBits& cb = B[t.committed];     // the committed entry is invariant over the pass
            const OrbEnt&  ce = E[t.committed];
            const size_t n = t.plist->idx.size();
            const uint32_t*  pi = t.plist->idx.data();
            const OrbBits*  pb = t.plist->bits.data();
            mine->idx.reserve(n / 16 + 4); mine->bits.reserve(n / 16 + 4);
            for (size_t k = 0; k < n; k++) {
                const uint32_t j = pi[k];
                if (j == t.committed || !bitsDisjoint(pb[k], cb)) continue;    // contiguous read
                if (!rowsHamiltonian(E[j].rows[0], ce)) continue;
                mine->idx.push_back(j); mine->bits.push_back(pb[k]);
                if (pb[k].w[aW] & aB) cand.push_back(j);
            }
        }
        size_t ncand = 0;
        for (uint32_t c : cand) {
            const OrbEnt& ce = E[c];
            if (g_typeMask && !typeMaskOK(ce)) continue;   // [TYPEMASK] see typeMaskOK
            ncand++;
            std::vector<Match> child = chosen; for (int r = 0; r < ce.nrows; r++) child.push_back(ce.rows[r]);
            OrbTask ct; ct.root = t.root; ct.plist = mine; ct.committed = c;
            outChildren.push_back(std::move(child)); outTrivial.push_back((char)1); outTrip.push_back(std::move(ct));
        }
        fanTally((int)chosen.size(), ncand, ncand);
    }

    // [PRECALC] local recursion (used when the shared queue is full), same tree as splitNodeL.
    //
    // [TUNE 2026-09-07] This is where the nodes are: the pool expands ONE level per queue item and
    // drains whole subtrees here once the queue is at QCAP, so nearly every node in the run is a
    // coverLocal call. The old version went through splitNodeL and therefore paid, per node, a
    // make_shared<IdxList> (malloc 9.9% + shared_ptr refcounts 3.6% in the profile) and TWO copies
    // of the whole partial cover -- splitNodeL built `chosen` + the entry's rows into a child
    // vector, and coverL then sliced the 1-3 rows back out of it -- to recover rows that were
    // already sitting in ce.rows. None of that is needed on a depth-first path: the parent's list
    // stays alive on the stack for the whole subtree, so it can live in a per-thread buffer indexed
    // by recursion depth, and the child can be committed straight from the entry.
    //
    // The tree is UNCHANGED: same nodes++ per node, same anchor, same candidate order, same emit.
    std::vector<NodeList> orbBuf;     // one reusable admissible-list per recursion depth

    void coverL(const OrbTask& t) {   // entry from the pool: the parent's list is shared, ours is not
        if (orbBuf.size() < (size_t)NM + 2) orbBuf.resize((size_t)NM + 2);
        coverLocal(*t.root, t.plist.get(), t.committed, 0);
    }

    void coverLocal(const OrbList& L, const NodeList* plist, uint32_t committed, int depth) {
        nodes++; nodes_flush++;
        progressTick(nodes_flush, g_nodes);
        if (g_stop.load(std::memory_order_relaxed)) return;
        if (g_diagPruneLo > 0 && diagPruned(plist ? (int)chosen.size() - (int)L.ent[committed].nrows : -1)) return;   // [FILTER] REP_DIAGPRUNE, as splitNodeL; the parent's witnesses are reused
        int u0, v0; pickAnchor(u0, v0);
        if (u0 < 0) { if ((int)chosen.size() == NM) emit(); return; }
        if (!imgUncovered(u0, v0)) return;
        const int eA = eidT[u0][v0];
        const int aW = eA >> 6; const uint64_t aB = 1ull << (eA & 63);
        // this depth's buffer; the recursion writes depth+1, so `mine` stays valid across the loop
        const OrbBits* const B = L.bits.data();     // planes hoisted: no vector::operator[] per entry
        const OrbEnt*  const E = L.ent.data();
        NodeList& mine = orbBuf[depth];
        mine.clear();
        if (!plist) {
            const uint32_t M = (uint32_t)L.ent.size();
            mine.idx.resize(M); mine.bits.resize(M);
            for (uint32_t i = 0; i < M; i++) { mine.idx[i] = i; mine.bits[i] = B[i]; }
        }
        else {
            const OrbBits& cb = B[committed];       // the committed entry is invariant over the pass
            const OrbEnt&  pe = E[committed];
            const size_t n = plist->idx.size();
            const uint32_t* const pi = plist->idx.data();
            const OrbBits* const pb = plist->bits.data();   // CONTIGUOUS: the whole point of NodeList
            for (size_t k = 0; k < n; k++) {
                const uint32_t j = pi[k];
                if (j == committed || !bitsDisjoint(pb[k], cb)) continue;
                if (!rowsHamiltonian(E[j].rows[0], pe)) continue;
                mine.idx.push_back(j); mine.bits.push_back(pb[k]);
            }
        }
        size_t ncand = 0;
        const size_t mn = mine.idx.size();
        for (size_t k = 0; k < mn; k++) {
            if (!(mine.bits[k].w[aW] & aB)) continue;   // anchor test, also from the contiguous copy
            const uint32_t c = mine.idx[k];
            const OrbEnt& ce = E[c];
            if (g_typeMask && !typeMaskOK(ce)) continue;   // [TYPEMASK] see typeMaskOK
            ncand++;
            const size_t base = chosen.size();
            commitRows(ce.rows, ce.nrows);
            coverLocal(L, &mine, c, depth + 1);
            rollbackTo(base);
        }
        fanTally((int)chosen.size(), ncand, ncand);
    }

    void collectTasks(std::vector<Match>& tasks) {
        clearState();
        // anchor edge is (0,1) on the empty board
        Match M; for (int i = 0; i < N; i++) M[i] = 0xFF;
        uint32_t usedM = (1u << 0) | (1u << 1);
        M[0] = 1; M[1] = 0;
        collectInto = &tasks;
        genM(M, usedM, 1);
        collectInto = nullptr;
    }

    // run one task: commit its first-orbit, then exact-cover the rest. Dispatch on whether
    // C(alpha) was enumerated: enumerable types use the index-based cover() with the exact
    // stabilizer; over-cap types use the generator-based coverGen() with Schreier stabilizers.
    void runTask(const Task& tk) {
        clearState();
        std::vector<Match> orbit;
        if (!buildAndValidateOrbit(tk.m0, orbit)) return;   // safety (tasks are pre-validated)
        commitOrbit(orbit);
        if (!sh->Calpha.empty()) cover(tk.stab);
        else coverGen(false);
    }
};

// ---- cycle-type enumeration -------------------------------------------------
int gcd_(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }
int lcm_(int a, int b) { return a / gcd_(a, b) * b; }
bool isOddPrime_(int p) { if (p < 3 || (p & 1) == 0) return false; for (int d = 3; d * d <= p; d += 2) if (p % d == 0) return false; return true; }

void enumTypes(int order, std::vector<std::vector<int>>& out) {
    std::vector<int> divs;            // divisors of order that are >= 2 (cycle lengths > 1)
    for (int d = 2; d <= order; d++) if (order % d == 0) divs.push_back(d);
    std::vector<int> cur;             // parts chosen so far (the partial type)
    std::function<void(int, int)> rec = [&](int idx, int rem) {
        if (idx == (int)divs.size()) {
            int L = 1; for (int p : cur) L = lcm_(L, p);     // order of this type
            if (L == order) { std::vector<int> t = cur; for (int i = 0; i < rem; i++) t.push_back(1); out.push_back(t); }
            return;
        }
        int d = divs[idx]; int saved = (int)cur.size();
        for (int cnt = 0; cnt * d <= rem; cnt++) { rec(idx + 1, rem - cnt * d); cur.push_back(d); }
        cur.resize(saved);
    };
    rec(0, N);
}

// ---- V4 (Klein four-group) machinery -----------------------------------------------
// Vertex shapes per MD/klein_four_theorem.md: each involution of a P1F fixes 0 or 2
// vertices (<=2-fixed lemma), so the vertex set splits into f global-fixed points,
// s_i pairs fixed by exactly involution i, and F free (regular) 4-orbits with
// f + 2(s1+s2+s3) + 4F = N and fix_i = f + 2 s_i in {0, 2}. Legs are canonicalized
// s1 >= s2 >= s3 (Aut(V4) = S3 relabels the involutions). For N == 0 (mod 4) every
// arithmetic shape contains a fixed-point-free involution (Klein-four theorem: the
// all-fix-2 shapes do not exist there) and is skipped citing the order-2 parity theorem.
struct V4Shape { int f, s1, s2, s3, F; };
void enumShapesV4(std::vector<V4Shape>& out) {
    out.clear();
    for (int f = 0; f <= 2; f += 2)
        for (int s1 = (f == 2 ? 0 : 1); s1 >= 0; s1--)       // fix_i = f + 2 s_i <= 2
            for (int s2 = s1; s2 >= 0; s2--)
                for (int s3 = s2; s3 >= 0; s3--) {
                    int rem = N - f - 2 * (s1 + s2 + s3);
                    if (rem < 0 || (rem % 4) != 0) continue;
                    out.push_back(V4Shape{ f, s1, s2, s3, rem / 4 });
                }
}
// explicit commuting involutions (a, b) realizing a shape; vertex layout:
// [4F free blocks][2*s1 a-fixed pairs][2*s2 b-fixed pairs][2*s3 ab-fixed pairs][f fixed].
// Inside a free block indices act by XOR: a: j^1, b: j^2 (V4 = (Z2)^2 regular action).
void buildV4Perms(const V4Shape& sp, Perm& a, Perm& b) {
    for (int i = 0; i < N; i++) { a[i] = (uint8_t)i; b[i] = (uint8_t)i; }
    int base = 0;
    for (int k = 0; k < sp.F; k++, base += 4)
        for (int j = 0; j < 4; j++) { a[base + j] = (uint8_t)(base + (j ^ 1)); b[base + j] = (uint8_t)(base + (j ^ 2)); }
    for (int k = 0; k < sp.s1; k++, base += 2) { b[base] = (uint8_t)(base + 1); b[base + 1] = (uint8_t)base; }
    for (int k = 0; k < sp.s2; k++, base += 2) { a[base] = (uint8_t)(base + 1); a[base + 1] = (uint8_t)base; }
    for (int k = 0; k < sp.s3; k++, base += 2) { a[base] = (uint8_t)(base + 1); a[base + 1] = (uint8_t)base;
                                                 b[base] = (uint8_t)(base + 1); b[base + 1] = (uint8_t)base; }
}
// All of C(V4) = { g : ga = ag, gb = bg }: g permutes same-stabilizer blocks, with the
// within-block index map j -> j XOR t (t in the block's translation group): free blocks
// form V4 wr S_F (4 translations each), leg-i pairs form C2 wr S_{s_i}, global fixed S_f.
// |C| = 4^F F! * prod_i 2^{s_i} s_i! * f!  -- tiny for every shape at these N, so the V4
// path always takes the ENUMERABLE branch (the cyclic-only CGT fallback is never needed).
bool buildCentralizerV4(const V4Shape& sp, std::vector<Perm>& out, long long cap) {
    struct Cls { std::vector<int> starts; int bs; int nch; };   // same-type blocks: start vertices, block size, #translations
    std::vector<Cls> cls;
    int base = 0;
    { Cls c; c.bs = 4; c.nch = 4; for (int k = 0; k < sp.F; k++, base += 4) c.starts.push_back(base); if (!c.starts.empty()) cls.push_back(c); }
    int ss[3] = { sp.s1, sp.s2, sp.s3 };
    for (int i = 0; i < 3; i++) { Cls c; c.bs = 2; c.nch = 2; for (int k = 0; k < ss[i]; k++, base += 2) c.starts.push_back(base); if (!c.starts.empty()) cls.push_back(c); }
    { Cls c; c.bs = 1; c.nch = 1; for (int k = 0; k < sp.f; k++, base += 1) c.starts.push_back(base); if (!c.starts.empty()) cls.push_back(c); }
    long long sz = 1;                    // size check against the cap
    for (auto& c : cls) {
        int m = (int)c.starts.size();
        for (int i = 0; i < m; i++)  { sz *= c.nch; if (sz > cap) { out.clear(); return false; } }
        for (int i = 2; i <= m; i++) { sz *= i;     if (sz > cap) { out.clear(); return false; } }
    }
    out.clear();
    Perm g; for (int i = 0; i < N; i++) g[i] = (uint8_t)i;
    std::function<void(size_t)> recCls = [&](size_t ci) {
        if (ci == cls.size()) { out.push_back(g); return; }
        Cls& c = cls[ci]; int m = (int)c.starts.size();
        std::vector<int> perm(m); for (int i = 0; i < m; i++) perm[i] = i;
        std::vector<int> off(m, 0);
        std::function<void(int)> recOff = [&](int t) {
            if (t == m) {
                for (int s = 0; s < m; s++) { int src = c.starts[s], tgt = c.starts[perm[s]];
                    for (int j = 0; j < c.bs; j++) g[src + j] = (uint8_t)(tgt + (j ^ off[s])); }
                recCls(ci + 1);
                return;
            }
            for (int o = 0; o < c.nch; o++) { off[t] = o; recOff(t + 1); }
        };
        std::function<void(int)> recPerm = [&](int t) {
            if (t == m) { recOff(0); return; }
            for (int v = t; v < m; v++) { std::swap(perm[t], perm[v]); recPerm(t + 1); std::swap(perm[t], perm[v]); }
        };
        recPerm(0);
    };
    recCls(0);
    return true;
}

// ---- E9 (C3 x C3) and S3 two-generator machinery ------------------------------------
// Both modes classify P1Fs invariant under a NON-CYCLIC group given by two generators,
// exactly as the V4 mode does for the Klein four-group: sh.gElems carries all
// non-identity elements (the search core is group-generic), and the centralizer of the
// embedding is built from the block structure of the group action.
//
// A subgroup embedding into S_N is determined up to conjugacy by the multiset of its
// transitive constituents. E9 = C3xC3 = <a, b>: constituents are regular 9-orbits,
// 3-orbits whose kernel is one of the FOUR order-3 subgroups H1..H4 (Aut(C3xC3) =
// GL(2,3) permutes them as S4, so the counts are canonicalized k1 >= k2 >= k3 >= k4),
// and fixed points: f + 3(k1+k2+k3+k4) + 9R = N. S3 = <r, s | r^3 = s^2 = 1,
// srs = r^-1>: constituents are regular 6-orbits, 3-orbits (cosets of a C2), 2-orbits
// (cosets of the C3), and fixed points: f + 2d + 3t + 6R = N. Non-faithful shapes are
// not emitted (they prescribe a smaller group, covered by other sweeps).
//
// Theorem-backed skips (REP_NOSKIP searches anyway; a skip can never lose classes):
//   E9: each subgroup H_i supplies an order-3 automorphism with (K - k_i) + 3R
//       3-cycles; an odd count with (N-1) % 3 != 0 is empty by the odd-prime parity
//       theorem.
//   S3: every involution fixes f + t vertices (f global + one per 3-orbit): > 2 is
//       empty by the involution fixed-point lemma, == 0 with N == 0 (mod 4) by the
//       order-2 parity theorem; the order-3 elements have t + 2R 3-cycles, an odd
//       count with (N-1) % 3 != 0 is empty by the odd-prime parity theorem.
struct BlockCls {                 // one class of interchangeable same-action blocks
    std::vector<int> starts;      // start vertex of each block
    int bs = 1;                   // block size
    std::vector<std::vector<uint8_t>> tr;  // within-block intertwiners (index maps); tr[0] = identity
};
struct GrpShape {
    char name[64];                // human-readable shape string
    std::vector<Perm> gElems;     // ALL non-identity elements of the group
    std::vector<BlockCls> cls;    // centralizer block structure
    const char* skipWhy = nullptr;  // non-null: empty by this theorem (skip unless REP_NOSKIP)
};

// BFS closure of a generating set; fills `out` with all NON-identity elements.
void groupClosure(const std::vector<Perm>& gens, std::vector<Perm>& out, size_t maxSize) {
    Perm id; for (int i = 0; i < N; i++) id[i] = (uint8_t)i;
    std::set<Perm> elems; elems.insert(id);
    std::vector<Perm> q(1, id);
    for (size_t h = 0; h < q.size(); h++)
        for (const auto& g : gens) {
            Perm x = composePerm(g, q[h]);
            if (elems.insert(x).second) { q.push_back(x); if (elems.size() > maxSize) { out.clear(); return; } }
        }
    out.clear();
    for (const auto& e : elems) if (e != id) out.push_back(e);
}

// Full centralizer of a block-structured group action: within each class, permute the
// blocks (S_m) and apply a per-block intertwiner (c.tr). Same recursion as
// buildCentralizerV4, with an explicit intertwiner TABLE instead of XOR translations.
bool buildCentralizerBlocks(const std::vector<BlockCls>& cls, std::vector<Perm>& out, long long cap) {
    long long sz = 1;                    // size check against the cap
    for (const auto& c : cls) {
        int m = (int)c.starts.size(), nch = (int)c.tr.size();
        for (int i = 0; i < m; i++)  { sz *= nch; if (sz > cap) { out.clear(); return false; } }
        for (int i = 2; i <= m; i++) { sz *= i;   if (sz > cap) { out.clear(); return false; } }
    }
    out.clear();
    Perm g; for (int i = 0; i < N; i++) g[i] = (uint8_t)i;
    std::function<void(size_t)> recCls = [&](size_t ci) {
        if (ci == cls.size()) { out.push_back(g); return; }
        const BlockCls& c = cls[ci]; int m = (int)c.starts.size();
        std::vector<int> perm(m); for (int i = 0; i < m; i++) perm[i] = i;
        std::vector<int> off(m, 0);
        std::function<void(int)> recOff = [&](int t) {
            if (t == m) {
                for (int s = 0; s < m; s++) { int src = c.starts[s], tgt = c.starts[perm[s]];
                    for (int j = 0; j < c.bs; j++) g[src + j] = (uint8_t)(tgt + c.tr[off[s]][j]); }
                recCls(ci + 1);
                return;
            }
            for (int o = 0; o < (int)c.tr.size(); o++) { off[t] = o; recOff(t + 1); }
        };
        std::function<void(int)> recPerm = [&](int t) {
            if (t == m) { recOff(0); return; }
            for (int v = t; v < m; v++) { std::swap(perm[t], perm[v]); recPerm(t + 1); std::swap(perm[t], perm[v]); }
        };
        recPerm(0);
    };
    recCls(0);
    return true;
}

// Small generating set of the same block centralizer (for shapes whose full centralizer
// exceeds the cap, e.g. many fixed points): per class, the nontrivial intertwiners of
// block 0, a swap of blocks 0,1, and a cycle of all m blocks -- the standard wreath
// generators, exactly parallel to the cyclic buildGenerators.
void buildGeneratorsBlocks(const std::vector<BlockCls>& cls, std::vector<Perm>& gens) {
    gens.clear();
    auto identityPerm = []() { Perm g; for (int i = 0; i < N; i++) g[i] = (uint8_t)i; return g; };
    for (const auto& c : cls) {
        int m = (int)c.starts.size();
        if (m == 0) continue;
        for (int o = 1; o < (int)c.tr.size(); o++) {
            Perm g = identityPerm();
            for (int j = 0; j < c.bs; j++) g[c.starts[0] + j] = (uint8_t)(c.starts[0] + c.tr[o][j]);
            gens.push_back(g);
        }
        if (m > 1) {
            Perm g = identityPerm();
            for (int j = 0; j < c.bs; j++) { g[c.starts[0] + j] = (uint8_t)(c.starts[1] + j); g[c.starts[1] + j] = (uint8_t)(c.starts[0] + j); }
            gens.push_back(g);
            if (m > 2) {
                Perm h = identityPerm();
                for (int s = 0; s < m; s++) for (int j = 0; j < c.bs; j++) h[c.starts[s] + j] = (uint8_t)(c.starts[(s + 1) % m] + j);
                gens.push_back(h);
            }
        }
    }
}

// E9 shapes. Vertex layout: [9R regular][3k1 kernel H1][3k2 H2][3k3 H3][3k4 H4][f fixed].
// Regular block index j = 3x + y: a adds 1 to x, b adds 1 to y. On a 3-orbit with
// kernel H_i the generators act as rotations by (actA[i], actB[i]); the kernels are
// H1 = <a>, H2 = <b>, H3 = <ab>, H4 = <ab^2> (each pair (actA, actB) has the stated
// kernel: a^i b^j acts as i*actA + j*actB mod 3). Centralizer: 9^R R! * prod 3^ki ki! * f!.
void enumShapesE9(std::vector<GrpShape>& out) {
    out.clear();
    static const int actA[4] = { 0, 1, 1, 1 }, actB[4] = { 1, 0, 2, 1 };
    for (int R = 0; 9 * R <= N; R++)
        for (int k1 = 0; 9 * R + 3 * k1 <= N; k1++)
            for (int k2 = 0; k2 <= k1 && 9 * R + 3 * (k1 + k2) <= N; k2++)
                for (int k3 = 0; k3 <= k2 && 9 * R + 3 * (k1 + k2 + k3) <= N; k3++)
                    for (int k4 = 0; k4 <= k3 && 9 * R + 3 * (k1 + k2 + k3 + k4) <= N; k4++) {
                        const int kk[4] = { k1, k2, k3, k4 };
                        const int K = k1 + k2 + k3 + k4, f = N - 9 * R - 3 * K;
                        const int nz = (k1 > 0) + (k2 > 0) + (k3 > 0) + (k4 > 0);
                        if (R == 0 && nz < 2) continue;              // not faithful (some element acts trivially)
                        GrpShape gs;
                        Perm a, b; for (int i = 0; i < N; i++) { a[i] = (uint8_t)i; b[i] = (uint8_t)i; }
                        int base = 0;
                        std::vector<BlockCls> cls;
                        { BlockCls c; c.bs = 9;
                          for (int q = 0; q < R; q++, base += 9) {
                              c.starts.push_back(base);
                              for (int x = 0; x < 3; x++) for (int y = 0; y < 3; y++) {
                                  a[base + 3 * x + y] = (uint8_t)(base + 3 * ((x + 1) % 3) + y);
                                  b[base + 3 * x + y] = (uint8_t)(base + 3 * x + (y + 1) % 3);
                              }
                          }
                          for (int u = 0; u < 3; u++) for (int v = 0; v < 3; v++) {   // 9 translations (abelian: right = left)
                              std::vector<uint8_t> t(9);
                              for (int x = 0; x < 3; x++) for (int y = 0; y < 3; y++) t[3 * x + y] = (uint8_t)(3 * ((x + u) % 3) + (y + v) % 3);
                              c.tr.push_back(std::move(t));
                          }
                          if (!c.starts.empty()) cls.push_back(std::move(c)); }
                        for (int i = 0; i < 4; i++) {                // 3-orbit classes, one per kernel (never mixed)
                            BlockCls c; c.bs = 3;
                            for (int q = 0; q < kk[i]; q++, base += 3) {
                                c.starts.push_back(base);
                                for (int j = 0; j < 3; j++) { a[base + j] = (uint8_t)(base + (j + actA[i]) % 3); b[base + j] = (uint8_t)(base + (j + actB[i]) % 3); }
                            }
                            for (int o = 0; o < 3; o++) { std::vector<uint8_t> t(3); for (int j = 0; j < 3; j++) t[j] = (uint8_t)((j + o) % 3); c.tr.push_back(std::move(t)); }
                            if (!c.starts.empty()) cls.push_back(std::move(c));
                        }
                        { BlockCls c; c.bs = 1; c.tr.push_back({ 0 });
                          for (int i = 0; i < f; i++, base++) c.starts.push_back(base);
                          if (!c.starts.empty()) cls.push_back(std::move(c)); }
                        groupClosure({ a, b }, gs.gElems, 16);       // the 8 non-identity elements
                        if ((NM % 3) != 0)
                            for (int i = 0; i < 4; i++)
                                if (((K - kk[i]) + 3 * R) & 1) { gs.skipWhy = "odd number of 3-cycles in a subgroup element (odd-prime parity theorem)"; break; }
                        gs.cls = std::move(cls);
                        snprintf(gs.name, sizeof(gs.name), "E9 k%d,%d,%d,%d R%d f%d", k1, k2, k3, k4, R, f);
                        out.push_back(std::move(gs));
                    }
}

// S3 shapes. Vertex layout: [6R regular][3t C2-cosets][2d C3-cosets][f fixed].
// Regular block index j = 3e + x encodes the element r^x s^e under LEFT multiplication
// (r: x+1; s: e flips, x negates). 3-orbit (cosets r^x C2): r rotates, s negates x
// (fixing exactly one point, as every involution must). 2-orbit (cosets of C3): r
// trivial, s swaps. Centralizer: per regular block the 6 RIGHT multiplications
// (g -> g*h commutes with all left multiplications); per 2-orbit a C2; per 3-orbit
// only the identity (N_S3(C2) = C2); giving 6^R R! * 2^d d! * t! * f!.
void enumShapesS3(std::vector<GrpShape>& out) {
    out.clear();
    for (int R = 0; 6 * R <= N; R++)
        for (int t = 0; 6 * R + 3 * t <= N; t++)
            for (int d = 0; 6 * R + 3 * t + 2 * d <= N; d++) {
                const int f = N - 6 * R - 3 * t - 2 * d;
                if (t + R == 0) continue;                    // r would act trivially (not a faithful S3)
                GrpShape gs;
                Perm r, s; for (int i = 0; i < N; i++) { r[i] = (uint8_t)i; s[i] = (uint8_t)i; }
                int base = 0;
                std::vector<BlockCls> cls;
                { BlockCls c; c.bs = 6;
                  for (int q = 0; q < R; q++, base += 6) {
                      c.starts.push_back(base);
                      for (int e = 0; e < 2; e++) for (int x = 0; x < 3; x++) {
                          r[base + 3 * e + x] = (uint8_t)(base + 3 * e + (x + 1) % 3);
                          s[base + 3 * e + x] = (uint8_t)(base + 3 * (1 - e) + (3 - x) % 3);
                      }
                  }
                  for (int eh = 0; eh < 2; eh++) for (int xh = 0; xh < 3; xh++) {   // right mult by r^xh s^eh
                      std::vector<uint8_t> t6(6);
                      for (int e = 0; e < 2; e++) for (int x = 0; x < 3; x++)
                          t6[3 * e + x] = (uint8_t)(3 * (e ^ eh) + (x + (e ? 3 - xh : xh)) % 3);
                      c.tr.push_back(std::move(t6));
                  }
                  if (!c.starts.empty()) cls.push_back(std::move(c)); }
                { BlockCls c; c.bs = 3;
                  for (int q = 0; q < t; q++, base += 3) {
                      c.starts.push_back(base);
                      for (int x = 0; x < 3; x++) { r[base + x] = (uint8_t)(base + (x + 1) % 3); s[base + x] = (uint8_t)(base + (3 - x) % 3); }
                  }
                  c.tr.push_back({ 0, 1, 2 });
                  if (!c.starts.empty()) cls.push_back(std::move(c)); }
                { BlockCls c; c.bs = 2;
                  for (int q = 0; q < d; q++, base += 2) {
                      c.starts.push_back(base);
                      s[base] = (uint8_t)(base + 1); s[base + 1] = (uint8_t)base;
                  }
                  c.tr.push_back({ 0, 1 }); c.tr.push_back({ 1, 0 });
                  if (!c.starts.empty()) cls.push_back(std::move(c)); }
                { BlockCls c; c.bs = 1; c.tr.push_back({ 0 });
                  for (int i = 0; i < f; i++, base++) c.starts.push_back(base);
                  if (!c.starts.empty()) cls.push_back(std::move(c)); }
                groupClosure({ r, s }, gs.gElems, 12);       // the 5 non-identity elements
                const int fixInv = f + t;                    // fixed points of EVERY involution
                if (fixInv > 2) gs.skipWhy = "involutions fix > 2 vertices (involution fixed-point lemma)";
                else if (fixInv == 0 && (N % 4) == 0) gs.skipWhy = "fixed-point-free involutions with N = 0 mod 4 (order-2 parity theorem)";
                else if ((NM % 3) != 0 && ((t + 2 * R) & 1)) gs.skipWhy = "odd number of 3-cycles in the order-3 element (odd-prime parity theorem)";
                gs.cls = std::move(cls);
                snprintf(gs.name, sizeof(gs.name), "S3 t%d d%d R%d f%d", t, d, R, f);
                out.push_back(std::move(gs));
            }
}

}  // anonymous namespace

// =============================================================================
// Private K18A2 entry point (declared in k18a2.h, called only from
// runExhaustiveSearch). Enumerates all K18 P1Fs with an automorphism of `order`,
// deduplicates them, forwards each distinct class to resultCallback, and prints a
// summary (only when m_bPrint). Worker count = kThreads.
// =============================================================================
// The table's LAST row.  It is emitted here, not at the end of
// runRepresentativeMethod, because that function runs once per REP_ORDERS token while the table
// spans the whole run -- printing a TOTAL per token would be six totals and no total.
// Every figure is the per-unit accumulation of the rows above, so the = row is exactly their sum;
// saved is cross-checked against g_emittedAll, which counts the same classes a different way.
void K18A2::finishRepTable() {
    if (!g_tbl) return;
    std::vector<std::string> cells;
    cells.push_back("TOTAL");
    cells.push_back("");
    cells.push_back(fmtElapsed(g_runSec));
    cells.push_back(fmtNodes(g_runNodes, g_runSec));
    cells.push_back(fmtCount(g_runSaved, g_runSavedAut));
    g_tbl->total(cells);
    delete g_tbl; g_tbl = nullptr;
}

void K18A2::runRepresentativeMethod(int order, int target) {
    initEdges();
    long long symCap = K18_REP_SYMCAP;                                    // overridable at runtime via env REP_SYMCAP
    if (const char* env = std::getenv("REP_SYMCAP")) symCap = atoll(env);   // e.g. force a type over-cap for oracle/estimate runs
    int nThreads = (kThreads > 0) ? kThreads : 1;   // worker count from the standard KThreads knob

    // [TEST HOOKS] REP_NOSKIP=1 disables the odd-prime parity-theorem type skip (used by the
    // theorem-VERIFICATION runs in run20a2Tests.bat, which search theorem-covered types and
    // must find 0). REP_ONLYTYPES=comma list of 1-based cycle-type indices restricts the run
    // to those types (all others reported as skipped) -- lets a verification run search e.g.
    // order-3 types 1 and 3 without touching the intractable type 5.
    int estProbes = 0;                              // [ESTIMATE MODE] REP_ESTIMATE=P: P Knuth probes per rep, NO search
    if (const char* e = std::getenv("REP_ESTIMATE")) estProbes = atoi(e);
    g_printRawLeft = 0;                             // [DIAG] REP_PRINTRAW=n: dump first n raw covers
    if (const char* e = std::getenv("REP_PRINTRAW")) g_printRawLeft = atoi(e);
    // [SHARD/MEASURE] REP_LEVEL=L: on an over-cap type, enumerate the search tree level by level
    // (L = orbit-blocks committed), print the node count at each level 1..L, then process ONLY the
    // REP_RANGE=start:end slice of the level-L nodes (default: none -> just count/print). Level-L
    // nodes are a complete, disjoint, deterministic partition -> split ranges across machines; the
    // count guides the shard granularity. No disk / no checkpoint (position == the range you run).
    int shardLevel = 0;
    if (const char* e = std::getenv("REP_LEVEL")) shardLevel = atoi(e);
    g_pruneMaxFactors = 0;   // REP_PRUNELEVEL: skip per-node setwiseStab prune once >= N factors committed (0 = prune everywhere)
    if (const char* e = std::getenv("REP_PRUNELEVEL")) g_pruneMaxFactors = atoi(e);
    if (std::getenv("REP_NOPRUNE")) g_noClosurePrune = true;   // A/B: disable genM closure-ban prune (measures its node-cut value)
    g_f17dump = std::getenv("REP_F17DUMP") != nullptr;         // [DIAG] sample the forced-17th-factor infertility obstruction
    if (const char* e = std::getenv("REP_F17SUB")) { g_f17Sub = atoll(e); if (g_f17Sub < 1) g_f17Sub = 1; }   // histogram subsample stride
    g_lvlStats = std::getenv("REP_LEVELSTATS") != nullptr;   // [DIAG] per-level node-count / branching profile
    if (g_lvlStats) for (int i = 0; i < NM + 2; i++) g_lvlReached[i].store(0);
    g_genStat = std::getenv("REP_GENSTAT") != nullptr;       // [DIAG] genM candidates built vs kept, per level
    g_pDump = std::getenv("REP_PDUMP") != nullptr;           // [DIAG] row-pairing involution p per completion
    g_probe1 = std::getenv("REP_PROBE1") ? atoi(std::getenv("REP_PROBE1")) : 0;   // [DIAG] pairing-invariant probe level
    g_noOrbChk = std::getenv("REP_NOORBCHK") != nullptr;     // [PERF PROBE] measure the orbit-internal check cost
    g_probeSeen.store(0); g_probeBad.store(0);
    if (g_pDump) { std::lock_guard<std::mutex> lk(g_pdump_mtx); g_pSeen.clear(); }
    if (g_genStat) for (int i = 0; i < NM + 2; i++) { g_genMade[i].store(0); g_genKept[i].store(0); }
    g_swapCap = std::getenv("REP_SWAPCAP") != nullptr;       // [FILTER] a=4 >4-swap-pair prune (sound for a=4 blocks; ~49% deep-node cut measured)
    // [PATFILTER] REP_PATTERN = neighbour-0 S/W target (empty => calculate from rows 2,3); legacy REP_PATONLY still read.
    // REP_PATAPPLY enables the filter. Canonization is NOT forced off -- the neighbour-0 index is canon-compatible
    // (K14: filter + canon ON is complete AND ~14x faster than baseline, ~75x faster than canon OFF).
    if (const char* e = std::getenv("REP_PATTERN")) g_patOnly = e;
    else if (const char* e2 = std::getenv("REP_PATONLY")) g_patOnly = e2;
    else g_patOnly.clear();
    // [PATFILTER] configurable mode via REP_PATAPPLY value (CASE-INSENSITIVE):
    //   REP_PATAPPLY=oneperpair -> one-per-pair filter (each sigma0 pair one S + one W)
    //   REP_PATAPPLY=calculate  -> calculate the alternating target from rows 2,3 (default)
    //   REP_PATAPPLY=1/empty/other + REP_PATTERN=<S/W> -> fixed-pattern filter
    // REP_PATPAIR= (any value) still selects one-per-pair standalone.
    g_patApply = false;
    g_patPair  = std::getenv("REP_PATPAIR") != nullptr;
    if (const char* pa = std::getenv("REP_PATAPPLY")) {
        std::string m(pa); for (char& ch : m) if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';   // lowercase (case-insensitive match)
        if (m == "oneperpair") g_patPair = true;      // select one-per-pair by name
        else g_patApply = true;                       // "calculate"/"1"/empty/other -> calculate or REP_PATTERN fixed string
    }
    g_typeMask = std::getenv("REP_TYPEMASK") != nullptr;     // [TYPEMASK] one-per-pair + pair-connection prune inside genM (see the u==1 hook)
    if (const char* r4 = std::getenv("REP_ROW4")) { char c = r4[0]; if (c >= 'a' && c <= 'z') c = c - 'a' + 'A'; if (c == 'S' || c == 'W') g_row4 = c; }   // [ROW4] pin row 4 type
    if (g_row4 && m_bPrint) { printf("[K18-REP] REP_ROW4=%c : row 4 (the factor covering {0,4}) is restricted to %s factors\n", g_row4, g_row4 == 'S' ? "sigma0-FIXED (stay)" : "sigma0-PAIRED (swap)"); fflush(stdout); }
    if (const char* dp = std::getenv("REP_DIAGPROBE")) g_diagProbe = atoi(dp);          // [DIAG] look-ahead probe from this depth up
    if (const char* ds = std::getenv("REP_DIAGSUB")) { int v = atoi(ds); if (v > 0) g_diagSub = v; }
    if (g_diagProbe > 0 && m_bPrint) { printf("[K18-REP] REP_DIAGPROBE=%d (every %dth node): measuring how often an uncovered diagonal has NO legal S row -- records only, never prunes\n", g_diagProbe, g_diagSub); fflush(stdout); }
    if (const char* dq = std::getenv("REP_DIAGPRUNE")) {   // [FILTER] lo[:hi]
        g_diagPruneLo = atoi(dq); const char* c = strchr(dq, ':'); if (c) g_diagPruneHi = atoi(c + 1);
    }
    {   // [OWNER] ON by default wherever there is a block column to own against; REP_OWNER=0 opts out.
        const char* oe = std::getenv("REP_OWNER");
        const bool off = oe && oe[0] == '0' && oe[1] == 0;    // REP_OWNER=0 -- the one value that disables
        g_ownerMode = !off && std::getenv("REP_F3COMPLETE") != nullptr;
    }
    g_ownerVerbose = std::getenv("REP_OWNERALL") != nullptr; // [OWNER] name the owner of every rejected cover
    // [AUT2ONLY] Same gate as the owner default, and NOT disabled by REP_OWNER=0: the case is the
    // order-2 census whether or not blocks dedup among themselves. See the note at g_aut2Only.
    g_aut2Only = std::getenv("REP_F3COMPLETE") != nullptr;
    // (An EXPLICIT REP_OWNER without REP_F3COMPLETE, or on any N but 18, is refused by
    //  checkOwnerEnv() in P1F-Census.cpp -- in the host, so it exits 1 and never creates RESULT.
    //  The default reaches neither case: it is conditioned on REP_F3COMPLETE, which is k18-only.)
    g_sGen = std::getenv("REP_SGEN") != nullptr;             // [SGEN] forced-S rows enumerate on the quotient
    g_patStat = std::getenv("REP_PATSTAT") != nullptr;       // [DIAG] sigma0 stay/swap pattern tally (completions + forced-17th + optional mid-level)
    if (const char* e = std::getenv("REP_PATLEVEL")) g_patLevel = atoi(e);   // mid-level pattern sample depth
    if (const char* e = std::getenv("REP_PATSUB")) { g_patSub = atoll(e); if (g_patSub < 1) g_patSub = 1; }   // mid-level sample stride
    if (g_patStat) { std::lock_guard<std::mutex> lk(g_pat_mtx); g_patEmit.clear(); g_patF17.clear(); g_patMid.clear(); g_patMidSeen.store(0); }
    g_ovlLevel = std::getenv("REP_CANONOVL") ? atoi(std::getenv("REP_CANONOVL")) : 0;   // [DIAG] tree-overlap probe (global; F3COMPLETE + COMPLETEPREFIX)
    g_ovlKey = std::getenv("REP_CANONKEY") != nullptr;   // add canonKey (overlap); default off = fast symmetry+total only
    if (g_ovlLevel > 0 && m_bPrint) { printf("[CANONOVL] probe ON: cap partials at size>=%d, sigma0-symmetry check%s, accumulate across blocks\n", g_ovlLevel, g_ovlKey ? " + canonKey overlap" : " (no canonKey -- fast)"); fflush(stdout); }
    g_cref.clear();   // REP_CMAP: load reference c-ranks so startC/lastC print on the 0..N (~0..60k) scale
    if (const char* e = std::getenv("REP_CMAP")) { FILE* f = fopen(e, "r"); if (f) { long long r; while (fscanf(f, "%lld", &r) == 1) g_cref.push_back(r); fclose(f); } std::sort(g_cref.begin(), g_cref.end());
        if (m_bPrint) { printf("[CMAP] loaded %zu reference c-ranks from %s -> startC/lastC now on 0..%zu scale\n", g_cref.size(), e, g_cref.size()); fflush(stdout); } }
    // REP_LOCATE=file1[,file2,...]: LOCATE MODE -- load target factorization(s), each a text file of N-1 lines,
    // one factor per line as N ints (N/2 pairs). Report the block index whose committed cover is a subset of a target.
    g_locTargets.clear();
    if (const char* e = std::getenv("REP_LOCATE")) {
        std::string spec(e); size_t pp = 0;
        while (pp < spec.size()) {
            size_t cc = spec.find(',', pp);
            std::string path = spec.substr(pp, cc == std::string::npos ? std::string::npos : cc - pp);
            pp = (cc == std::string::npos) ? spec.size() : cc + 1;
            LocTarget T; T.nfac = 0; for (int a = 0; a < N; a++) for (int b = 0; b < N; b++) T.edgeFac[a][b] = 0;
            FILE* fp = fopen(path.c_str(), "r"); if (!fp) { printf("[LOCATE] cannot open %s\n", path.c_str()); continue; }
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                int vals[64], nv = 0; char* q = line;
                while (*q && nv < 64) { while (*q && !(*q >= '0' && *q <= '9')) q++; if (!*q) break; vals[nv++] = atoi(q); while (*q >= '0' && *q <= '9') q++; }
                if (nv != N) continue;
                int fi = T.nfac++;                               // factor index (bit position)
                for (int k = 0; k + 1 < nv; k += 2) { int a = vals[k], b = vals[k + 1]; T.edgeFac[a][b] |= (1u << fi); T.edgeFac[b][a] |= (1u << fi); }
            }
            fclose(fp);
            printf("[LOCATE] loaded target from %s : %d factors\n", path.c_str(), T.nfac);
            if (T.nfac > 0) g_locTargets.push_back(std::move(T));
        }
    }
    // REP_RANGE=start:end                     -> process that block range on the work-queue (unset -> COUNT the level).
    // REP_RANGE=start:end:step(timeout,results) -> CHUNKED DENSITY SWEEP: walk [start,end) in `step`-block chunks;
    //   in each chunk process blocks from the chunk start, advancing to the next chunk the instant EITHER cap is hit
    //   -- `timeout` seconds OR `results` new classes -- checked MID-BLOCK (a block halts early). Either cap may be
    //   0 (disabled). Prints a per-chunk "+classes ... TIMED-OUT|RESULT-CAP|range-done" line. e.g. 0:18000000:1000000(3600,5).
    bool rangeMode = false;
    size_t rangeStart = 0, rangeEnd = 0;
    long long chunkStep = 0; double chunkTimeout = 0; long long chunkResultCap = 0; long long chunkMaxBlocks = 0;   // (timeout,results,maxblocks) per chunk
    if (const char* e = std::getenv("REP_RANGE")) {
        rangeMode = true;
        const char* c1 = strchr(e, ':');
        long a = atol(e), b = c1 ? atol(c1 + 1) : -1;
        rangeStart = (a > 0) ? (size_t)a : 0;
        rangeEnd   = (b >= 0) ? (size_t)b : (size_t)-1;   // "start:" or "start" -> to end
        if (c1) { const char* c2 = strchr(c1 + 1, ':'); if (c2) { chunkStep = atoll(c2 + 1); const char* op = strchr(c2 + 1, '('); if (op) { chunkTimeout = atof(op + 1); const char* cm = strchr(op, ','); if (cm) { chunkResultCap = atoll(cm + 1); const char* cm2 = strchr(cm + 1, ','); if (cm2) chunkMaxBlocks = atoll(cm2 + 1); } } } }
        if (chunkStep < 0) chunkStep = 0;
    }
    // [FAST SEARCH] REP_DIVE=B (OPTIONAL, off by default): on an over-cap type, replace the exhaustive
    // tree walk with randomized BACKTRACKING DFS restarts across all threads. Each restart begins at a
    // uniform-random root and explores in random candidate order (backtracking on dead-ends, no
    // setwiseStab) for up to B nodes, emitting every complete cover it reaches (global canonKey dedups);
    // then it restarts from a fresh random root. Runs until a harvest target (REP_ORDER=order:target) or
    // it is killed. B (the per-restart node budget) trades depth-of-exploration vs restart diversity.
    long long diveBudget = 0;
    if (const char* e = std::getenv("REP_DIVE")) diveBudget = atoll(e);
    const bool diveMode = (diveBudget > 0);
    const bool noSkip = (std::getenv("REP_NOSKIP") != nullptr);
    // [RAW SHARD] REP_RAWSHARD: on the shard/level/range modes, seed the tree traversal as "trivial"
    // so splitNode SKIPS the per-node setwiseStab + schreierDedup (the expensive order-|C| group
    // backtrack). Branches become raw potential candidates (alpha-orbit reps, symmetry NOT reduced):
    // a superset ordered index-stably by genM, so seeking a mid-range shard start is far cheaper (no
    // group backtrack per skipped node). More branches + some redundant symmetric subtrees, but the
    // distinct-class result is unchanged -- the global canonical dedup (gcanon/g_harvest) collapses the
    // duplicates, and a superset of branches can never miss a class. Use for fast sharding/harvest, not
    // for a minimal-work complete census (where the symmetry reduction saves redundant work).
    const bool rawShard = (std::getenv("REP_RAWSHARD") != nullptr);
    g_fanOn = (std::getenv("REP_FANOUT") != nullptr);   // [FANOUT] per-depth tree-shape tally, printed with the progress rows
    g_precalc = (std::getenv("REP_PRECALC") != nullptr); // [PRECALC] prototype: list-filtered cover below each seed (block-threading pool)
    if (g_precalc && m_bPrint) xprintf("[K18-REP] REP_PRECALC: PROTOTYPE -- precalculated orbit lists below each seed; trivial-stabilizer nodes only (use REP_PRUNELEVEL=1)\n");
    std::set<int> onlyTypes;
    if (const char* env = std::getenv("REP_ONLYTYPES")) {
        for (const char* p = env; *p; ) { int v = atoi(p); if (v > 0) onlyTypes.insert(v); while (*p && *p != ',') p++; while (*p == ',') p++; }
    }

    g_bprint = m_bPrint;
    g_nodes.store(0); g_emits.store(0); g_last_print_nodes = 0;
    g_target = target; g_stop.store(false); g_harvest.clear(); g_banked.store(0); g_f1.store(0); g_f2.store(0); g_autHist.clear(); g_dupCovers.store(0);   // harvest mode (target>0 -> stop after `target` classes)
    // Live result emission: every globally-new class goes straight to the normal result
    // pipeline (mutex-serialized in emit()); a long or killed run keeps everything sent.
    g_sendResult = [this](const unsigned char* s, int aut) { if (resultCallback) resultCallback(cbClass, s, aut, 1, 2); };   // |Aut| rides the callback's free r4 slot
    g_t0 = std::chrono::steady_clock::now();
    g_f17Total.store(0); g_f17NextSec.store(60); g_maxKids.store(0); g_maxNQ.store(0); g_monNextSec.store(5); { std::lock_guard<std::mutex> lk(g_f17_mtx); g_f17Hist.clear(); g_f17ClashHist.clear(); }   // [DIAG] REP_F17DUMP reset
    g_last_print = g_t0;
    g_order = order;

    // [DISABLED] REP_RANGE -- the level-3 chunked sweep. Superseded on k18 by the block driver
    // and not worth reasoning about twice, so it is refused rather than left as a second way to
    // sweep the same space:
    //   * it walks the search tree WITHOUT block canonicalization, so it re-searches starters that
    //     canonize together -- t8 a=0 is 57,752 raw blocks against 25,530 canonical ones;
    //   * it has no end of its own. REP_RANGE=1:1000000 stops on the COUNT, long past the fertile
     //    region, and the documented way to finish early was to watch the Chao1 est-total flatten
    //     -- a signal this project has been burned by repeatedly (a t8 per-block estimate of
    //     606/666 was overturned to thousands);
    //   * it prints no resume point, so an interrupted sweep cannot be continued exactly.
    // REP_F3COMPLETE + REP_F3A gives the same coverage with a canonical block list, a printed
    // resume value and a real DONE line. k14/k16/k20 keep REP_RANGE: they have no block driver.
    if (std::getenv("REP_RANGE")) {
        printf("[K18-REP] REP_RANGE is DISABLED for k18 -- the level-3 chunked sweep is superseded by the\n");
        printf("[K18-REP]   block driver, which canonizes the block list, ENDS, and prints a resume point.\n");
        printf("[K18-REP]   Use:  REP_F3COMPLETE=1 REP_F3COUNT=1 REP_F3A=<a> [REP_F3START=<c>] [REP_F3STOP=<c>]\n");
        printf("[K18-REP]   t9 a=0: 57,216 raw -> 8,713 canonical blocks.  t8 a=0: 57,752 -> 25,530.\n");
        printf("[K18-REP]   (k14/k16/k20 still accept REP_RANGE; they have no block driver.)\n");
        fflush(stdout);
        exit(1);
    }


    // [SERVICE] REP_CANONFILE=<file>: re-canonicalize a class file and write it to RESULT, then
    // stop. No search, no blocks. A canonical form is only canonical for ONE canonizer, and this
    // engine changed which one it emits on 2026-08-18 ("the project canonical form"),
    // so a class file written before that shares no matrix with one written after -- a stored
    // P-file of the t8 census matched the current catalog on 1 class in 9,622. Feeding it through
    // canonKey() rewrites every record in the labeling the engine emits today AND recovers the
    // real |Aut| that a P-file header never carried (it always printed 0), so the output is
    // exactly what a run of the matching case would have produced.
    if (const char* cf = std::getenv("REP_CANONFILE")) {
        FILE* fp = fopen(cf, "r");
        if (!fp) { printf("[K18-REP] REP_CANONFILE: cannot open %s -- stop\n", cf); fflush(stdout); exit(1); }
        RepWorker pw;                              // canonKey needs no shared state (sets pw.lastAut)
        std::vector<Match> facs; long long in = 0, out = 0;
        auto flush = [&]() {
            if (facs.size() != (size_t)NM) { facs.clear(); return; }
            in++;
            std::string key = pw.canonKey(facs);
            if (g_sendResult) {
                unsigned char src[NM * N];
                for (int k = 0; k < NM; k++) { uint8_t adj[N]; for (int u = 0; u < N; u++) adj[u] = (uint8_t)key[k * N + u]; adj_to_src(adj, src + k * N); }
                g_sendResult(src, pw.lastAut / 2); out++;
            }
            g_autHist[pw.lastAut / 2]++;
            facs.clear();
        };
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            int vals[64], nv = 0; char* q = line;
            while (*q && nv < 64) { while (*q && !(*q >= '0' && *q <= '9')) q++; if (!*q) break; vals[nv++] = atoi(q); while (*q >= '0' && *q <= '9') q++; }
            if (nv == N) {                          // a factor row: N ints = N/2 pairs
                Match m; for (int u = 0; u < N; u++) m[u] = 0xFF;
                for (int k = 0; k + 1 < nv; k += 2) { m[vals[k]] = (uint8_t)vals[k + 1]; m[vals[k + 1]] = (uint8_t)vals[k]; }
                facs.push_back(m);
                if (facs.size() == (size_t)NM) flush();
            }
        }
        flush(); fclose(fp);
        std::string h; for (auto& kv : g_autHist) { char t[24]; snprintf(t, sizeof(t), "%s%d:%d", h.empty() ? "" : " ", kv.first, kv.second); h += t; }
        printf("[K18-REP] REP_CANONFILE: %lld results read from %s, %lld written, aut{%s}\n", in, cf, out, h.c_str());
        printf("[K18-REP] REP_CANONFILE: records are in the canonical form THIS build emits -- re-run any\n");
        printf("[K18-REP]                comparison against files written by the same build.\n");
        fflush(stdout);
        return;
    }


    std::set<std::string>      gcanon;   // global distinct canonical keys (across all types)
    std::map<std::string, int> gautOf;   // global canonical key -> 2*|Aut|


    const bool isV4 = (order == kOrderV4);          // V4 mode: Klein four-group instead of <alpha>
    const bool isE9 = (order == kOrderE9);          // E9 mode: C3 x C3 two-generator sweep
    const bool isS3 = (order == kOrderS3);          // S3 mode: symmetric-group-on-3 two-generator sweep
    const bool isGrp = isV4 || isE9 || isS3;        // any non-cyclic (group-shape) mode
    std::vector<std::vector<int>> types;            // cyclic: all cycle types of this order
    std::vector<V4Shape> shapes;                    // V4: vertex shapes (see enumShapesV4)
    std::vector<GrpShape> gshapes;                  // E9/S3: fully-constructed group shapes
    if (isV4) enumShapesV4(shapes); else if (isE9) enumShapesE9(gshapes); else if (isS3) enumShapesS3(gshapes); else enumTypes(order, types);
    g_numTypes = (int)(isV4 ? shapes.size() : isGrp ? gshapes.size() : types.size());
    // The run's ONE table.  runRepresentativeMethod is called once
    // per REP_ORDERS token, so this is guarded: the title, and the first column-name row, are
    // printed by the FIRST token and every later token adds rows to the same table.  The block
    // driver builds its own table (column set B) and is excluded here.
    if (m_bPrint && !g_tbl && !std::getenv("REP_F3COMPLETE")) {
        static char title[160];
        snprintf(title, sizeof(title), "Factorization of K%d P1F |Aut| > 2", N);
        g_tbl = new LogTable(title, kColsA, kTotalsA, sizeof(kColsA) / sizeof(kColsA[0]));
    }
    int g_typeCounter = 0;                          // 1-based index of the type currently processed
    int g_typeSkipped = 0;                          // types skipped by a theorem or by REP_ONLYTYPES -- counted here, reported once at the end of the order
    if (m_bPrint) {
        if (noSkip) printf("[K18-REP] REP_NOSKIP: odd-prime parity-theorem skip DISABLED (theorem-verification mode)\n");
        if (g_patApply && !g_patOnly.empty()) printf("[K18-REP] REP_PATAPPLY pattern=%s: neighbour-0 filter ON; canonization per REP_PRUNELEVEL=%d; block canon %s\n", g_patOnly.c_str(), g_pruneMaxFactors, std::getenv("REP_F3RAW") ? "OFF (raw)" : "ON");
        if (g_patApply && g_patOnly.empty()) printf("[K18-REP] REP_PATAPPLY (calculate): target from rows 2,3 (dup {2,3} across pairs, row1=S); canonization per REP_PRUNELEVEL=%d; block canon %s\n", g_pruneMaxFactors, std::getenv("REP_F3RAW") ? "OFF (raw)" : "ON");
        if (estProbes > 0) printf("[K18-REP] REP_ESTIMATE=%d : Knuth tree-size probes per rep -- NO SEARCH, output is an ESTIMATE, not a result\n", estProbes);

        if (target > 0) printf("[K18-REP] HARVEST MODE: stop after %d distinct classes (COMPLETE only if %d == true count, else a LOWER BOUND)\n", target, target);
        if (rawShard) printf("[K18-REP] REP_RAWSHARD: branches = raw potential candidates (symmetry NOT reduced) -- LEVEL counts are a superset, mid-range seek is cheap; distinct-set correct via global dedup\n");
        {
            int nf = 0; for (int i = 0; i < 4; i++) if (m_v4Row[i] && m_v4Val[i]) nf++;
            if (nf) {
                printf("[K18-REP] V4 SHARD FILTERS (search frame):");
                for (int i = 0; i < 4; i++) if (m_v4Row[i] && m_v4Val[i]) printf("  0-%d factor pairs 1-%d;", m_v4Row[i], m_v4Val[i]);
                printf("  (SHARD run -- NOT a complete order)\n");
                // Consistency validation: each condition below makes the shard PROVABLY
                // EMPTY (the run would burn its full time and return 0 classes) -- say so.
                for (int i = 0; i < 4; i++) {
                    const int r = m_v4Row[i], v = m_v4Val[i];
                    if (!r && !v) continue;
                    if (!r || !v || r < 2 || r > N - 1 || v < 2 || v > N - 1)
                        printf("[K18-REP] *** V4 FILTER %d INVALID: row=%d val=%d (both must be set, in 2..%d) -- shard is EMPTY\n", i + 1, r, v, N - 1);
                    else if (r == v)
                        printf("[K18-REP] *** V4 FILTER %d INVALID: row == val == %d (no vertex partners both 0 and 1) -- shard is EMPTY\n", i + 1, r);
                    for (int j = i + 1; j < 4; j++) {
                        if (!m_v4Row[j] || !m_v4Val[j]) continue;
                        if (m_v4Row[j] == r && m_v4Val[j] != v)
                            printf("[K18-REP] *** V4 FILTERS %d,%d CONTRADICT: row %d demands val %d AND %d -- shard is EMPTY\n", i + 1, j + 1, r, v, m_v4Val[j]);
                        if (m_v4Val[j] == v && m_v4Row[j] != r)
                            printf("[K18-REP] *** V4 FILTERS %d,%d CONTRADICT: val %d demanded in rows %d AND %d (edge {1,%d} is in ONE factor) -- shard is EMPTY\n", i + 1, j + 1, v, r, m_v4Row[j], v);
                    }
                }
            }
        }
        if (g_printRawLeft > 0) printf("[K18-REP] REP_PRINTRAW=%d : raw search-frame cover of the first %d classes will be printed\n", g_printRawLeft, g_printRawLeft);
        fflush(stdout);
    }

    static const bool only_fpf = (getenv("REP_FPF") != nullptr);   // [DIAG] isolate fixed-point-free type(s)
    for (int typeIdx = 0; typeIdx < g_numTypes; typeIdx++) {
        if (g_stop.load(std::memory_order_relaxed)) break;   // harvest target already reached -> skip remaining types
        const std::vector<int>* type = isGrp ? nullptr : &types[typeIdx];
        if (!isGrp && only_fpf) { bool hasFix = false; for (int p : *type) if (p == 1) hasFix = true; if (hasFix) continue; }
        RepShared sh;                    // shared read-only data for this type
        sh.order = isV4 ? 4 : isE9 ? 9 : isS3 ? 6 : order;   // |G| (informational; the search iterates sh.gElems)
        for (int i = 0; i < 4; i++)      // compact the host's (row,val) shard-filter pairs
            if (m_v4Row[i] && m_v4Val[i]) { sh.v4Row[sh.nV4] = m_v4Row[i]; sh.v4Val[sh.nV4] = m_v4Val[i]; sh.nV4++; }
        char tstr[64]; int tp = 0;       // human-readable type/shape string
        if (isV4) {
            // Klein four-group for this shape: gElems = the three involutions {a, b, ab}
            const V4Shape& sp = shapes[typeIdx];
            Perm pa, pb; buildV4Perms(sp, pa, pb);
            for (int i = 0; i < N; i++) sh.alpha[i] = pa[i];   // (cyclic-only code paths never run in V4 mode)
            sh.gElems.clear();
            sh.gElems.push_back(pa); sh.gElems.push_back(pb); sh.gElems.push_back(composePerm(pa, pb));
            snprintf(tstr, sizeof(tstr), "V4 fx%d,%d,%d F%d", sp.f + 2 * sp.s1, sp.f + 2 * sp.s2, sp.f + 2 * sp.s3, sp.F);
        } else if (isE9 || isS3) {
            // Two-generator group for this shape: gElems = all non-identity elements
            const GrpShape& gsp = gshapes[typeIdx];
            sh.gElems = gsp.gElems;
            for (int i = 0; i < N; i++) sh.alpha[i] = sh.gElems.empty() ? (uint8_t)i : sh.gElems[0][i];   // (cyclic-only code paths never run in group modes)
            snprintf(tstr, sizeof(tstr), "%s", gsp.name);
        } else {
            for (int i = 0; i < N; i++) sh.alpha[i] = (uint8_t)i;
            int base = 0;                // first vertex of the next cycle block
            for (int p : *type) {
                if (p > 1) { for (int i = 0; i < p; i++) sh.alpha[base + i] = (uint8_t)(base + (i + 1) % p); }
                base += p;
            }
            // compact cycle type in power notation, e.g. "2^9", "2^8 1^2" (printWithPowers renders ^ as a superscript).
            // Built from the WHOLE type, fixed points included: a bare list of the p > 1 cycles silently drops them,
            // so "3 3 3 3 3" would be all a K16 reader saw of 3^5 1.
            std::map<int, int, std::greater<int>> cc; for (int p : *type) cc[p]++;
            for (auto& kv : cc)
                tp += (kv.second == 1) ? snprintf(tstr + tp, sizeof(tstr) - tp, "%s%d", tp ? " " : "", kv.first)
                                       : snprintf(tstr + tp, sizeof(tstr) - tp, "%s%d^%d", tp ? " " : "", kv.first, kv.second);
            Perm ap; for (int i = 0; i < N; i++) ap[i] = sh.alpha[i];
            Perm cur = ap;               // gElems = alpha^1 .. alpha^{order-1}
            sh.gElems.clear();
            for (int j = 1; j < order; j++) { sh.gElems.push_back(cur); cur = composePerm(ap, cur); }
        }
        sh.buildImgEdges();              // precompute genM's group-image freeness lists (gElems now final)
        { std::string r = printWithPowers(std::string(tstr)); snprintf(tstr, sizeof(tstr), "%s", r.c_str()); }   // ^N -> superscript (no-op if g_useColors off)
        // Pad tstr to a fixed DISPLAY width (14 code points, not bytes) so a byte-counted "%-14s"
        // cannot mis-align rows once printWithPowers has inserted multibyte superscripts. Trailing
        // spaces are stripped again before the string reaches a table cell.
        { int cols = 0; for (const unsigned char* p = (const unsigned char*)tstr; *p; ++p) if ((*p & 0xC0) != 0x80) cols++;
          size_t used = strlen(tstr);
          while (cols < 14 && used + 1 < sizeof(tstr)) { tstr[used++] = ' '; cols++; } tstr[used] = '\0'; }
        int tIdx = ++g_typeCounter;      // 1-based enumeration index of this type (stable, includes skipped types)

        // Odd-prime parity theorem (MD/odd_prime_parity_theorem.md): for odd PRIME order p
        // with (N-1) % p != 0, a cycle type with an ODD number of p-cycles admits no
        // invariant P1F -- factor-orbit counting (orbit sizes 1 or p) forces a fixed factor,
        // and a fixed factor must pair the fixed vertices among themselves, so N - p*t is
        // even and t must be even. Such types are EMPTY BY PROOF and are skipped (this is
        // what retires the intractable K20 order-3 type 3^5.1^5). Validated: brute force
        // K8/K12/K14 (scratchpad/order3_parity_check.cpp) + K20 full searches of orders
        // 11,13,17 and order-3 types 3^1/3^3 all returned 0. REP_NOSKIP=1 searches anyway.
        if (isV4) {
            // V4 shapes containing a FIXED-POINT-FREE involution are empty by the order-2
            // parity theorem when N == 0 (mod 4); combined with the Klein-four counting
            // theorem (no all-fix-2 shape exists for N == 0 mod 4) a V4 run on such N
            // skips every shape -> 0, echoing MD/klein_four_theorem.md.
            const V4Shape& sp = shapes[typeIdx];
            bool hasFpf = (sp.f + 2 * sp.s1 == 0) || (sp.f + 2 * sp.s2 == 0) || (sp.f + 2 * sp.s3 == 0);
            if (!noSkip && hasFpf && (N % 4 == 0)) {
                g_typeSkipped++;
                continue;
            }
        }
        if (isE9 || isS3) {
            const GrpShape& gsp = gshapes[typeIdx];
            if (sh.gElems.size() != (isE9 ? 8u : 5u)) {   // paranoia: generator closure must be the full group
                if (m_bPrint) { printf("[K18-REP]  type %-14s (type %d/%d) SKIPPED: INTERNAL closure size %zu != %u -- report this\n", tstr, tIdx, g_numTypes, sh.gElems.size(), isE9 ? 8u : 5u); fflush(stdout); }
                continue;
            }
            if (!noSkip && gsp.skipWhy) {
                g_typeSkipped++;
                continue;
            }
        }
        int tCyc = 0; bool pureType = true;              // tCyc = #p-cycles; pureType = only p-cycles and fixed points (cyclic mode)
        if (!isGrp) for (int p : *type) if (p > 1) { tCyc++; if (p != order) pureType = false; }
        if (!isGrp && !noSkip && pureType && isOddPrime_(order) && (NM % order) != 0 && (tCyc & 1)) {
            g_typeSkipped++;
            continue;
        }
        // POWER-REDUCTION skip (2026-07-06, THEOREM-BACKED ONLY -- skipping cannot lose
        // classes; REP_NOSKIP searches anyway): a sigma-invariant P1F is invariant under
        // every power of sigma, so for each prime p | order the power sigma^(order/p)
        // (an order-p automorphism) must itself satisfy the parity theorems. A part of
        // length L contributes gcd(L, order/p) cycles of length L/gcd to that power.
        // Checks: p = 2 -> involution fixes <= 2 vertices (fixed-point lemma), and not
        // fixed-point-free when N == 0 (mod 4) (order-2 parity theorem); p odd with
        // (N-1) % p != 0 -> even number of p-cycles (odd-prime parity theorem). Retires
        // the dense over-cap junk types of composite orders (e.g. K20 6^1.1^14, 9^1.1^11)
        // and the lemma-empty order-2 types with more than two fixed points.
        if (!isGrp && !noSkip) {
            const char* why = nullptr; int badp = 0;
            for (int p = 2; p <= order && !why; p++) {
                if (order % p != 0) continue;
                bool prime = true; for (int d = 2; d * d <= p; d++) if (p % d == 0) { prime = false; break; }
                if (!prime) continue;
                const int m = order / p;         // sigma^m has order exactly p
                int t = 0;                       // #p-cycles of sigma^m
                for (int L : *type) { int g = gcd_(L, m); if (L / g == p) t += g; }
                const int f = N - p * t;         // fixed points of sigma^m
                if (p == 2) {
                    if (f > 2) { why = "sigma^(k/2) fixes > 2 vertices (involution fixed-point lemma)"; badp = 2; }
                    else if (f == 0 && (N % 4) == 0) { why = "sigma^(k/2) fixed-point-free with N = 0 mod 4 (order-2 parity theorem)"; badp = 2; }
                } else if ((NM % p) != 0 && (t & 1)) { why = "odd number of p-cycles in sigma^(k/p) (odd-prime parity theorem)"; badp = p; }
            }
            if (why) {
                g_typeSkipped++;
                continue;
            }
        }
        if (!onlyTypes.empty() && !onlyTypes.count(tIdx)) {
            g_typeSkipped++;
            continue;
        }
        // [GUARD] Checked here -- on the type actually about to be SEARCHED, past every skip. The sigma0
        // stay/swap machinery assumes sigma0 is FIXED-POINT-FREE: the one-per-pair rule is a t9 theorem
        // (both-stays => 4-cycle), the vertex-1 mask reads v^1 as sigma0's partner of v, and genS places
        // edges in mirror pairs. On a type with fixed points (t8 = 2^8 1^2 fixes two vertices, whose slots
        // still look like a pair) every one of those is wrong and NOTHING downstream would complain -- the
        // census would just quietly lose classes. cmd SET persists for the window, so a flag left over from
        // an a=4 run is the realistic way this happens.
        { int nfix = 0; for (int i = 0; i < N; i++) if (sh.alpha[i] == (uint8_t)i) nfix++;
          const char* bad = !nfix ? nullptr
                          : g_typeMask ? "REP_TYPEMASK" : g_sGen ? "REP_SGEN" : g_row4 ? "REP_ROW4"
                          : g_patPair ? "REP_PATPAIR" : g_patApply ? "REP_PATAPPLY"
                          : g_diagPruneLo > 0 ? "REP_DIAGPRUNE" : nullptr;
          if (bad) { printf("[K18-REP] %s needs a fixed-point-free sigma0, but type %s (about to be searched) fixes %d vertices -- stop\n", bad, tstr, nfix); fflush(stdout); exit(1); }
          }   // [SUBSET] is gone: column set A puts its two facts in the Symmetry and Type columns, and the block driver puts them in the table title
        // [DIAGPRUNE] Sound only in the a=4 column: there every sigma0-fixed factor carries exactly ONE
        // diagonal (dichotomy + 9 S rows for 9 diagonals). In the a=0 column the single fixed factor is D,
        // which carries all nine, so the probe's one-diagonal rule would reject the very row that must exist.
        if (g_diagPruneLo > 0) {
            const int a0chk = std::getenv("REP_F3A") ? atoi(std::getenv("REP_F3A")) : 0;
            if (a0chk != 4) { printf("[K18-REP] REP_DIAGPRUNE is sound only for the a=4 column (REP_F3A=4), but REP_F3A=%d -- stop\n", a0chk); fflush(stdout); exit(1); }
        }
        // [SETUP TIMING] Everything from here to the worker fan-out runs BEFORE the first search
        // node, and printed nothing -- which is why an over-cap order-2 leg looks hung for its
        // first half hour (K20 2^9.1^2: ~37 min of silence, measured 2026-09-03). One line per
        // stage, with the size of what the stage produced, so the cost is attributable and the
        // question "is this worth caching" has an answer per stage.
        auto setupT0 = std::chrono::steady_clock::now();
        auto setupTick = [&](const char* stage, long long n) {
            if (!m_bPrint) return;
            auto now = std::chrono::steady_clock::now();
            const double s = std::chrono::duration<double>(now - setupT0).count();
            setupT0 = now;
            xprintf("[K18-REP]  type %-14s SETUP %-14s %8.1fs  n=%lld\n", tstr, stage, s, n);
            fflush(stdout);
        };
        if (isV4) buildCentralizerV4(shapes[typeIdx], sh.Calpha, symCap);   // V4 |C| is tiny -> always enumerable
        else if (isE9 || isS3) buildCentralizerBlocks(gshapes[typeIdx].cls, sh.Calpha, symCap);   // block centralizer; over cap -> generator path below
        else buildCentralizer(sh.alpha, sh.Calpha, symCap);   // empty => run this type without symmetry-breaking
        if ((isE9 || isS3) && !sh.Calpha.empty()) {   // paranoia: every symmetry element must commute with the whole group
            size_t step = sh.Calpha.size() > 100000 ? sh.Calpha.size() / 100000 + 1 : 1;
            bool okC = true;
            for (size_t gi = 0; gi < sh.Calpha.size() && okC; gi += step)
                for (const auto& e : sh.gElems) if (composePerm(sh.Calpha[gi], e) != composePerm(e, sh.Calpha[gi])) { okC = false; break; }
            if (!okC) { if (m_bPrint) { printf("[K18-REP]  type %-14s SKIPPED: INTERNAL centralizer element does not commute -- report this\n", tstr); fflush(stdout); } continue; }
        }
        sh.Cinv.resize(sh.Calpha.size());
        for (size_t i = 0; i < sh.Calpha.size(); i++) for (int x = 0; x < N; x++) sh.Cinv[i][sh.Calpha[i][x]] = (uint8_t)x;
        sh.rootActive.resize(sh.Calpha.size());
        for (size_t i = 0; i < sh.Calpha.size(); i++) sh.rootActive[i] = (int)i;
        setupTick("centralizer", (long long)sh.Calpha.size());   // n = |C(alpha)| enumerated, 0 = over-cap

        // 1. collect the raw first-orbit tasks (single-threaded root enumeration)
        std::vector<Match> raw;
        { RepWorker collector; collector.sh = &sh; collector.collectTasks(raw); }
        setupTick("collectTasks", (long long)raw.size());

        // 2. reduce tasks by C(alpha): keep ONE representative per C(alpha)-orbit. Each orbit is
        //    handled once, so the cost is O(#reps * <group walk>), not O(#rawTasks * |C|).
        // First collapse raw tasks to one matching per distinct alpha-orbit (collectTasks may emit
        // several matchings from the same orbit), keyed by the alpha-orbit serialization.
        std::unordered_map<std::string, int> orbIdx;   // alpha-orbit key -> unique-task index
        std::vector<Match> uniq;                        // one matching per distinct alpha-orbit
        std::vector<std::string> uniqKey;               // its alpha-orbit key
        for (auto& m : raw) {
            std::string k = orbitKey(m, sh.gElems);
            if (orbIdx.emplace(k, (int)uniq.size()).second) { uniq.push_back(m); uniqKey.push_back(std::move(k)); }
        }
        setupTick("orbit-collapse", (long long)uniq.size());

        std::vector<Task> reps;
        std::vector<char> seen(uniq.size(), 0);         // seen[j] = unique-task j already covered
        if (!sh.Calpha.empty()) {
            // C(alpha) enumerated (dense types): mark each C(alpha)-orbit by applying every gamma,
            // and record the orbit's STABILIZER so its (large, productive) subtree is symmetry-broken.
            for (size_t i = 0; i < uniq.size(); i++) {
                if (seen[i]) continue;
                seen[i] = 1;
                std::vector<int> stab;                  // gammas fixing this orbit (setwise)
                for (int gi = 0; gi < (int)sh.Calpha.size(); gi++) {
                    std::string img = orbitKey(applyAlpha(uniq[i], sh.Calpha[gi].data()), sh.gElems);
                    if (img == uniqKey[i]) stab.push_back(gi);     // gamma fixes the orbit
                    auto it = orbIdx.find(img);
                    if (it != orbIdx.end()) seen[it->second] = 1;  // mark this C(alpha)-orbit member
                }
                reps.push_back(Task{ uniq[i], std::move(stab) });
            }
            setupTick("C-orbit reps", (long long)reps.size());
            // [BLOCK THREADING] With REP_RANGE the slice is run through the shared work queue
            // rather than one thread per rep, so a SINGLE block can use every thread instead of
            // one core (cover() has no fan-out; splitNode does). splitNode needs the root BSGS,
            // which otherwise only the over-cap setup builds -- so build it here, by exactly the
            // calls that setup uses. Also built for REP_LEVEL > 1, whose level-L frontier is
            // expanded by splitNode on this path too.
            // Without either, none of this is built and nothing changes.
            // (k18 refuses REP_RANGE at startup; the REP_LEVEL > 1 count is reachable. Kept identical anyway.)
            if (rangeMode || shardLevel > 1) {
                if (isE9 || isS3) {
                    buildGeneratorsBlocks(gshapes[typeIdx].cls, sh.rootCgens);   // block-wreath generators
                    bool okG = true;   // paranoia: generators must commute with the whole group
                    for (const auto& g : sh.rootCgens) { for (const auto& e : sh.gElems) if (composePerm(g, e) != composePerm(e, g)) { okG = false; break; } if (!okG) break; }
                    if (!okG) { if (m_bPrint) { printf("[K18-REP]  type %-14s SKIPPED: INTERNAL centralizer generator does not commute -- report this\n", tstr); fflush(stdout); } continue; }
                } else
                buildGenerators(sh.alpha, sh.rootCgens);
                sh.rootBSGS = cgt::buildBSGS<N>(sh.rootCgens);
                setupTick("BSGS+edgeStab", (long long)sh.rootCgens.size());
            }
        } else {
            if (isV4) {   // cannot happen (V4 |C| << cap); the CGT fallback below is cyclic-only
                if (m_bPrint) { printf("[K18-REP]  type %-14s SKIPPED: V4 centralizer over cap (unsupported path)\n", tstr); fflush(stdout); }
                continue;
            }
            // C(alpha) too large to enumerate (sparse types, AND every order-2 type, whose fixed
            // points alone make |C| huge): represent the group by a GENERATING SET and, for each
            // first-orbit task, compute Schreier generators of its setwise stabilizer in C(alpha)
            // via a candidate-restricted BFS. These flow into coverGen so the over-cap subtrees get
            // the same per-level orbit dedup the enumerable types get from explicit elements.
            if (isE9 || isS3) {
                buildGeneratorsBlocks(gshapes[typeIdx].cls, sh.rootCgens);   // block-wreath generators (e.g. fixed-point-heavy shapes)
                bool okG = true;   // paranoia: generators must commute with the whole group
                for (const auto& g : sh.rootCgens) { for (const auto& e : sh.gElems) if (composePerm(g, e) != composePerm(e, g)) { okG = false; break; } if (!okG) break; }
                if (!okG) { if (m_bPrint) { printf("[K18-REP]  type %-14s SKIPPED: INTERNAL centralizer generator does not commute -- report this\n", tstr); fflush(stdout); } continue; }
            } else
            buildGenerators(sh.alpha, sh.rootCgens);              // seed for exact Stab_C(P) in coverGen
            sh.rootBSGS = cgt::buildBSGS<N>(sh.rootCgens);        // built once per type, reused per node
            if (const char* rcf = std::getenv("REP_REPSCHECK")) {   // [DEBUG] read dumped rep covers; verify canonV_fast is C(sigma0)-invariant on each
                FILE* fp = fopen(rcf, "r");
                if (!fp) { printf("[REPSCHECK] cannot open %s\n", rcf); fflush(stdout); return; }
                std::vector<std::vector<Match>> covers; std::vector<Match> cur; char ln[1024];
                while (fgets(ln, sizeof(ln), fp)) {
                    if (ln[0] == '#') { if (!cur.empty()) { covers.push_back(cur); cur.clear(); } continue; }
                    int vals[64], nv = 0; char* q = ln;
                    while (*q && nv < 64) { while (*q && !(*q >= '0' && *q <= '9')) q++; if (!*q) break; vals[nv++] = atoi(q); while (*q >= '0' && *q <= '9') q++; }
                    if (nv != N) continue;
                    Match F; for (int i = 0; i < N; i++) F[i] = (uint8_t)i;
                    for (int k = 0; k + 1 < nv; k += 2) { F[vals[k]] = (uint8_t)vals[k + 1]; F[vals[k + 1]] = (uint8_t)vals[k]; }
                    cur.push_back(F);
                }
                if (!cur.empty()) covers.push_back(cur);
                fclose(fp);
                std::set<std::string> keys; long long checked = 0, fail = 0;
                for (auto& cov : covers) {
                    uint8_t k0[NEDGES]; canonV_fast(cov, sh.alpha, k0);
                    keys.insert(std::string((const char*)k0, NEDGES));
                    for (const auto& g : sh.rootCgens) {
                        std::vector<Match> img(cov.size());
                        for (size_t i = 0; i < cov.size(); i++) img[i] = applyAlpha(cov[i], g.data());
                        uint8_t kg[NEDGES]; canonV_fast(img, sh.alpha, kg);
                        checked++; if (memcmp(kg, k0, NEDGES) != 0) fail++;
                    }
                }
                printf("[REPSCHECK] %zu covers | distinct canonV_fast keys=%zu (should == #covers) | invariance checks=%lld FAIL=%lld (should be 0)\n",
                       covers.size(), keys.size(), checked, fail); fflush(stdout);
                return;
            }
            std::vector<Perm> hgens = cgt::edgeStabGens<N>(sh.rootCgens, 0, 1);  // collectTasks forces the anchor edge (0,1)
            setupTick("BSGS+edgeStab", (long long)sh.rootCgens.size());
            std::vector<int> repIdx; std::vector<std::vector<Perm>> stabs;
            schreierDedup(uniq, uniqKey, orbIdx, hgens, sh.gElems, repIdx, stabs);
            setupTick("schreierDedup", (long long)repIdx.size());
            for (size_t r = 0; r < repIdx.size(); r++)
                reps.push_back(Task{ uniq[repIdx[r]], {}, std::move(stabs[r]) });
        }

        // 3. fan the representative tasks out across kThreads workers; each subtree breaks
        //    symmetry with only its orbit's stabilizer.
        size_t before = gcanon.size();   // classes known before this type
        const long long dupBefore = g_crossDup.load(std::memory_order_relaxed);   // duplicates before this type
        { std::lock_guard<std::mutex> lk(g_harvest_mtx); g_savedAut.clear(); g_crossDupAut.clear(); }   // per-leg |Aut| histograms: what this leg wrote, and what it rejected
        auto ts = std::chrono::steady_clock::now();
        g_legT0 = ts; g_legNodes0 = g_nodes.load(std::memory_order_relaxed); g_legDup0 = g_crossDup.load(std::memory_order_relaxed);   // the ~ row counts the open leg too
        // (no per-type start line: a type prints one line, when it finishes)
        // [PATHCOORD] REP_PATHCOORD=<file>: compute the target's hierarchical block coordinate a.b.c
        // DETERMINISTICALLY (no scan, no backtracking). LEVEL 1: find the seed rep a and the C(sigma0)
        // element phi mapping rep[a] onto the target's first orbit (coverMatchesTargetPhi); h=phi^-1 rotates
        // the target into the search frame so its first orbit == rep[a]. LEVELS 2+: the anchor edge is FORCED
        // (lowest uncovered), so the rotated target's factor covering it is the exact orbit -- find its index
        // in the raw reps child list by orbitKey (linear in #children, NOT the old O(children*match) scan +
        // backtrack that blew up for low-symmetry targets). PL-invariant (raw reps order). Then skip cover.
        if (const char* pcf = std::getenv("REP_PATHCOORD")) {
            LocTarget T; T.nfac = 0; for (int a = 0; a < N; a++) for (int b = 0; b < N; b++) T.edgeFac[a][b] = 0;
            std::vector<Match> tfac;                     // target factors (matchings)
            FILE* fp = fopen(pcf, "r");
            if (!fp) { if (m_bPrint) { printf("[PATHCOORD] cannot open %s\n", pcf); fflush(stdout); } continue; }
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                int vals[64], nv = 0; char* q = line;
                while (*q && nv < 64) { while (*q && !(*q >= '0' && *q <= '9')) q++; if (!*q) break; vals[nv++] = atoi(q); while (*q >= '0' && *q <= '9') q++; }
                if (nv != N) continue;
                int fi = T.nfac++;
                Match F; for (int u = 0; u < N; u++) F[u] = 0xFF;
                for (int k = 0; k + 1 < nv; k += 2) { int a = vals[k], b = vals[k + 1]; T.edgeFac[a][b] |= (1u << fi); T.edgeFac[b][a] |= (1u << fi); F[a] = (uint8_t)b; F[b] = (uint8_t)a; }
                tfac.push_back(F);
            }
            fclose(fp);
            const bool pcdiag = std::getenv("REP_PATHCOORDDIAG") != nullptr;
            // REP_PATHCOORDCANON: emit the CANONIZED a.b.c = the minimum a.b.c over the C(sigma0) orbit
            // (labeling-independent class identity), instead of the raw labeling-specific coordinate.
            // Levels 2+ minimize the child index over the node-stabilizer orbit (greedy lex-min via the
            // schreier transporter). Level 1 (seed rep a) is already C(sigma0)-canonical.
            const bool pccanon = std::getenv("REP_PATHCOORDCANON") != nullptr;
            // GUARD: the target MUST be in a sigma0-invariant labeling (sigma0 an automorphism = it permutes
            // the factors). Raw canonical-frame output is NOT -- relabel it first (see P47_std via buildRhoStd).
            { int s0 = 0; for (const Match& t : tfac) { Match ts = applyAlpha(t, sh.alpha); for (const Match& t2 : tfac) if (ts == t2) { s0++; break; } }
              if (s0 != (int)tfac.size()) { if (m_bPrint) { printf("[PATHCOORD] target %s is NOT sigma0-invariant (%d/%zu factors) -- relabel to the sigma0 frame first (buildRhoStd). Skipping.\n", pcf, s0, tfac.size()); fflush(stdout); } continue; } }
            if (m_bPrint) { printf("[PATHCOORD] target %s: %d factors (sigma0-invariant) -> deterministic descent to depth %d ...\n", pcf, T.nfac, shardLevel); fflush(stdout); }
            if (pccanon) {
                // CANONIZED coordinate: replace the target labeling with its UNIQUE C(sigma0)-canonical
                // labeling (canonV_fast minimal image) BEFORE descending. All sigma0-labelings of the class
                // map to the same canonical labeling, so the resulting a.b.c is a labeling-INDEPENDENT class
                // identity. Reuses the raw descent below (no per-level stabilizer needed).
                uint8_t cser[NEDGES]; uint8_t cinv[N];
                canonV_fast(tfac, sh.alpha, cser, cinv);
                Perm rho = buildRhoStd(sh.alpha);
                Perm bi; for (int x = 0; x < N; x++) bi[x] = cinv[x];
                Perm Cp = composePerm(invPerm(bi), rho);   // canonical relabel = invPerm(inv) o rho
                for (auto& F : tfac) F = applyAlpha(F, Cp.data());
                for (int a = 0; a < N; a++) for (int b = 0; b < N; b++) T.edgeFac[a][b] = 0;
                for (int fi = 0; fi < (int)tfac.size(); fi++) { const Match& F = tfac[fi]; for (int u = 0; u < N; u++) { int v = F[u]; if (u < v) { T.edgeFac[u][v] |= (1u << fi); T.edgeFac[v][u] |= (1u << fi); } } }
                if (m_bPrint) { printf("[PATHCOORD] canonicalized target via canonV_fast (labeling-independent)\n"); fflush(stdout); }
            }
            RepWorker dw; dw.sh = &sh; dw.clearState();
            std::vector<int> coord; bool ok = true;
            std::vector<Match> tf;                        // target rotated into the search frame (first orbit == rep[a])
            {   // LEVEL 1: seed rep a + rotation h=phi^-1
                int a = -1, phi[N];
                for (size_t j = 0; j < reps.size(); j++) {
                    std::vector<Match> orbit;
                    if (!dw.buildAndValidateOrbit(reps[j].m0, orbit)) continue;
                    if (coverMatchesTargetPhi(orbit, T, phi)) { a = (int)j; break; }
                }
                if (a < 0) { if (m_bPrint) { printf("[PATHCOORD] NOT FOUND (no seed rep matches the target's first orbit)\n"); fflush(stdout); } continue; }
                Perm phiP; for (int i = 0; i < N; i++) phiP[i] = (uint8_t)phi[i];
                if (pcdiag) { int good = 0; for (int i = 0; i < N; i++) if (phiP[sh.alpha[i]] == sh.alpha[phiP[i]]) good++; printf("[PATHCOORD-DIAG] phi in C(sigma0)? %d/%d vertices commute\n", good, N); fflush(stdout); }
                Perm h = invPerm(phiP);
                tf.resize(tfac.size());
                for (size_t i = 0; i < tfac.size(); i++) tf[i] = applyAlpha(tfac[i], h.data());
                std::vector<Match> orb; dw.buildAndValidateOrbit(reps[a].m0, orb); dw.commitOrbit(orb);
                coord.push_back(a);
                if (pcdiag) {
                    int inTf = 0; for (const Match& c : dw.chosen) { for (const Match& t : tf) if (c == t) { inTf++; break; } }
                    int s0inv = 0; for (const Match& t : tf) { Match ts = applyAlpha(t, sh.alpha); for (const Match& t2 : tf) if (ts == t2) { s0inv++; break; } }
                    int repInTf = 0; for (const Match& r : orb) { for (const Match& t : tf) if (r == t) { repInTf++; break; } }
                    printf("[PATHCOORD-DIAG] level 1: seed rep a=%d, committed=%zu in-tf=%d | tf sigma0-invariant factors=%d/%zu | rep[a] orbit in-tf=%d/%zu\n", a, dw.chosen.size(), inTf, s0inv, tf.size(), repInTf, orb.size()); fflush(stdout);
                }
            }
            for (int L = 1; L < shardLevel && ok; L++) {   // LEVELS 2..shardLevel: anchor-forced exact lookup
                int u0, v0; dw.pickAnchor(u0, v0);
                if (u0 < 0) { ok = false; if (pcdiag) { printf("[PATHCOORD-DIAG] level %d: FAIL no anchor (u0<0)\n", L + 1); fflush(stdout); } break; }
                int fi = -1; for (size_t i = 0; i < tf.size(); i++) if (tf[i][u0] == (uint8_t)v0) { fi = (int)i; break; }
                if (fi < 0) { ok = false; if (pcdiag) { printf("[PATHCOORD-DIAG] level %d: FAIL no target factor covers anchor {%d,%d}\n", L + 1, u0, v0); fflush(stdout); } break; }
                std::string targKey = orbitKey(tf[fi], sh.gElems);
                std::vector<std::vector<Match>> kids; std::vector<char> kt; size_t base = dw.chosen.size();
                dw.splitNode(true, kids, kt);
                int idx = -1;
                for (size_t j = 0; j < kids.size(); j++) if (orbitKey(kids[j][base], sh.gElems) == targKey) { idx = (int)j; break; }
                if (idx < 0) { ok = false;
                    if (pcdiag) {
                        int inTf = 0; for (const Match& c : dw.chosen) { for (const Match& t : tf) if (c == t) { inTf++; break; } }
                        int hamOk = 0; for (const Match& c : dw.chosen) if (is_perfect(tf[fi], c)) hamOk++;
                        printf("[PATHCOORD-DIAG] level %d: FAIL orbit(anchor {%d,%d}, tf-factor %d) not among %zu children | committed=%zu, of which in-tf=%d, ham-compat-with-tf[fi]=%d\n",
                               L + 1, u0, v0, fi, kids.size(), dw.chosen.size(), inTf, hamOk); fflush(stdout);
                    }
                    break; }
                std::vector<Match> newOrbit(kids[idx].begin() + base, kids[idx].end());
                dw.commitOrbit(newOrbit); coord.push_back(idx);
                if (pcdiag) { int inTf = 0; for (const Match& c : dw.chosen) { for (const Match& t : tf) if (c == t) { inTf++; break; } } bool tfIn = false; for (const Match& t : tf) if (t == tf[fi]) { tfIn = true; break; } printf("[PATHCOORD-DIAG] level %d: anchor {%d,%d}, %zu children, idx=%d | committed=%zu in-tf=%d (targ tf[fi] valid=%d)\n", L + 1, u0, v0, kids.size(), idx, dw.chosen.size(), inTf, (int)tfIn); fflush(stdout); }
            }
            if (ok && (int)coord.size() == shardLevel) {
                std::string s; for (size_t i = 0; i < coord.size(); i++) { if (i) s += "."; s += std::to_string(coord[i]); }
                if (m_bPrint) { printf("[PATHCOORD]%s FOUND a.b.c = %s\n", pccanon ? " CANON" : "", s.c_str()); fflush(stdout); }
            } else if (m_bPrint) { printf("[PATHCOORD] NOT FOUND / inconsistent at level %zu\n", coord.size()); fflush(stdout); }
            continue;
        }
        // [CANONCOUNT] REP_CANONCOUNT=<file>: read sigma0-invariant covers (factor rows, blank line between
        // covers), run canonV_fast on each, report #covers and #distinct canonical keys. Used to verify the
        // 3-row t9 starter file collapses to the same canonV_fast count as the (0,0)-column enumeration.
        if (const char* ccf = std::getenv("REP_CANONCOUNT")) {
            FILE* fp = fopen(ccf, "r");
            if (!fp) { if (m_bPrint) { printf("[CANONCOUNT] cannot open %s\n", ccf); fflush(stdout); } continue; }
            std::set<std::string> keys; std::vector<Match> cur; long long ncov = 0; char line[1024];
            auto flush = [&]() { if (!cur.empty()) { uint8_t key[NEDGES]; canonV_fast(cur, sh.alpha, key); keys.insert(std::string((const char*)key, NEDGES)); ncov++; cur.clear(); } };
            while (fgets(line, sizeof(line), fp)) {
                int vals[64], nv = 0; char* q = line;
                while (*q && nv < 64) { while (*q && !(*q >= '0' && *q <= '9')) q++; if (!*q) break; vals[nv++] = atoi(q); while (*q >= '0' && *q <= '9') q++; }
                if (nv == N) { Match F; for (int u = 0; u < N; u++) F[u] = 0xFF; for (int k = 0; k + 1 < nv; k += 2) { int a = vals[k], b = vals[k + 1]; F[a] = (uint8_t)b; F[b] = (uint8_t)a; } cur.push_back(F); }
                else flush();
            }
            flush(); fclose(fp);
            if (m_bPrint) { printf("[CANONCOUNT] %s: %lld covers -> %zu distinct canonV_fast keys\n", ccf, ncov, keys.size()); fflush(stdout); }
            continue;
        }
        // [F2COUNT] REP_F2COUNT: fix a=a0 (F1 only), enumerate the level-2 F2 orbits at anchor {0,2}, and
        // count the DISTINCT canonV_fast (F1,F2) prefixes. This is the "is b=0 WLOG?" test: if the canonical
        // count is 1, every second factor is C(sigma0)-equivalent (a=0-style transversal, pin b=0 loses
        // nothing); if >1, the a=a0 sweep must cover each distinct b. Mirrors the F3COUNT loop one level up.
        if (std::getenv("REP_F2COUNT")) {
            RepWorker dw; dw.sh = &sh; dw.clearState();
            if (reps.empty()) { if (m_bPrint) { printf("[F2COUNT] no level-1 reps\n"); fflush(stdout); } continue; }
            const int a0 = std::getenv("REP_F3A") ? atoi(std::getenv("REP_F3A")) : 0;
            if (a0 < 0 || a0 >= (int)reps.size()) { if (m_bPrint) { printf("[F2COUNT] REP_F3A=%d out of range (reps=%zu)\n", a0, reps.size()); fflush(stdout); } continue; }
            { std::vector<Match> orb; if (!dw.buildAndValidateOrbit(reps[a0].m0, orb)) { if (m_bPrint) { printf("[F2COUNT] a=%d build fail\n", a0); fflush(stdout); } continue; } dw.commitOrbit(orb); }   // commit F1 = a=a0
            std::vector<Match> creps; std::vector<std::string> ckeys; std::unordered_map<std::string, int> ckeyIdx; std::vector<Perm> hgens; int u0 = -1, v0 = -1;
            if (!dw.childReps(creps, ckeys, ckeyIdx, hgens, u0, v0)) { if (m_bPrint) { printf("[F2COUNT] a=%d no F2 anchor\n", a0); fflush(stdout); } continue; }
            const long long f2max = std::getenv("REP_F2MAX") ? atoll(std::getenv("REP_F2MAX")) : -1;   // optional cap (probe)
            // REP_F2FIXED: restrict to sigma0-FIXED second factors (orbit size 1). The coverage test:
            // if the sigma0-fixed completions of F1 form ONE canonV_fast class, then every a=4 class's 8
            // non-F1 sigma0-fixed factors lie in b=0's orbit => b=0 captures every a=4 class => the
            // b=0-per-column census is COMPLETE for a=4.
            const bool f2fixed = std::getenv("REP_F2FIXED") != nullptr;
            std::set<std::string> canonKeys; size_t base2 = dw.chosen.size(); size_t seen = 0, nfixed = 0;
            for (size_t i = 0; i < creps.size(); i++) {
                if (f2max >= 0 && (long long)i >= f2max) break;
                std::vector<Match> orb; if (!dw.buildAndValidateOrbit(creps[i], orb)) continue;
                if (f2fixed && orb.size() > 1) continue;   // skip sigma0-paired F2, keep only sigma0-fixed (orbit size 1)
                if (orb.size() == 1) nfixed++;
                dw.commitOrbit(orb);
                uint8_t key[NEDGES]; canonV_fast(dw.chosen, sh.alpha, key, nullptr);
                canonKeys.insert(std::string((const char*)key, NEDGES));
                dw.rollbackTo(base2); seen++;
                if (m_bPrint && i && (i % 20000 == 0)) { printf("[F2COUNT] a=%d %zu/%zu, distinct (F1,F2) canon so far=%zu (fixed-F2 seen=%zu)\n", a0, i, creps.size(), canonKeys.size(), nfixed); fflush(stdout); }
            }
            if (m_bPrint) { printf("[F2COUNT]%s a=%d F2 anchor {%d,%d}: raw F2 orbits=%zu (processed %zu, sigma0-fixed=%zu), CANONICAL (F1,F2) prefixes (canonV_fast distinct)=%zu => %s\n",
                f2fixed ? " [FIXED-ONLY]" : "", a0, u0, v0, creps.size(), seen, nfixed, canonKeys.size(),
                canonKeys.size() == 1 ? (f2fixed ? "single sigma0-fixed orbit => b=0 COVERS a=4 (census complete)" : "b=0 is WLOG (single orbit)")
                                      : (f2fixed ? "MULTIPLE sigma0-fixed orbits => coverage NOT established" : "b=0 NOT WLOG (multiple b)")); fflush(stdout); }
            continue;
        }

        // [F3COUNT] REP_F3COUNT: fix a=0 (sigma0) and b=0 (canonical Hamiltonian F2), then count the level-3
        // F3 factors: RAW (orbitKey-deduped) and CANONICAL (further deduped by the C(sigma0) setwise-stabilizer
        // of the fixed (sigma0,F2) prefix = the residual symmetry). The canonical count = # distinct level-3
        // blocks under (0,0) = the real 1-D search size for the t9 aut=2 census.
        if (std::getenv("REP_F3COUNT")) {
            RepWorker dw; dw.sh = &sh; dw.clearState();
            if (reps.empty()) { if (m_bPrint) { printf("[F3COUNT] no level-1 reps\n"); fflush(stdout); } continue; }
            // REP_F3A=<n>: which level-1 seed rep is the "a" column (0..reps-1; a=0 = sigma0). Default 0.
            // Run once per a (0..4) to sweep the whole a.0.c space and test
            // whether any a>=1 column yields a class absent from a=0 (it should surface P47 at a=4).
            const int a0 = std::getenv("REP_F3A") ? atoi(std::getenv("REP_F3A")) : 0;
            if (a0 < 0 || a0 >= (int)reps.size()) { if (m_bPrint) { printf("[F3COUNT] REP_F3A=%d out of range (reps=%zu)\n", a0, reps.size()); fflush(stdout); } continue; }
            { std::vector<Match> orb; if (!dw.buildAndValidateOrbit(reps[a0].m0, orb)) { if (m_bPrint) { printf("[F3COUNT] a=%d build fail\n", a0); fflush(stdout); } continue; } dw.commitOrbit(orb); }   // a=a0
            { std::vector<std::vector<Match>> kk; std::vector<char> kt; size_t bs = dw.chosen.size(); dw.splitNode(true, kk, kt);
              if (kk.empty()) { if (m_bPrint) { printf("[F3COUNT] a=%d b=0 no children\n", a0); fflush(stdout); } continue; }
              std::vector<Match> no(kk[0].begin() + bs, kk[0].end()); dw.commitOrbit(no); }   // b=0
            std::vector<Match> creps; std::vector<std::string> ckeys; std::unordered_map<std::string, int> ckeyIdx; std::vector<Perm> hgens; int u0 = -1, v0 = -1;
            if (!dw.childReps(creps, ckeys, ckeyIdx, hgens, u0, v0)) { if (m_bPrint) { printf("[F3COUNT] no F3 anchor\n"); fflush(stdout); } continue; }
            // canonize each F3 block (sigma0 + F2 orbit + F3 orbit) with canonV_fast -- the FULL C(sigma0)
            // minimal image (free to permute all committed factors) -- and count DISTINCT canonical starters.
            std::unordered_map<std::string, std::pair<long long,long long>> canonKeys;   // ks -> {first-occurrence RAW c, that block's result count (-1 = not completed this run segment)}
            std::set<long long> cranks; const char* cdumpf = std::getenv("REP_CDUMP");   // [DIAG] dump distinct canonical c-ranks (the a=4 c-space reference for the 0..N mapping)
            FILE* lst = nullptr; if (const char* lf = std::getenv("REP_F3LIST")) lst = fopen(lf, "w");   // "to process" list: first-occurrence blocks
            FILE* dmp = nullptr; if (const char* df = std::getenv("REP_F3DUMP")) dmp = fopen(df, "w");   // dump each first-occurrence block's 3-factor starter (edge pairs), one record per line
            // [WITNESS] REP_WITNESS: transporter/injectivity check on the canonization -- the fatal-direction test.
            // For every block canonV_fast returns the key AND bestInv (the winning C(sigma0) vertex order g1). We
            // reconstruct the canonical factor set C = (invg1 . rho) . block, self-check that C serializes back to
            // the key, verify g1 is a genuine C(sigma0) element (preserves sigma0's pair structure), and -- on a key
            // COLLISION -- assert C matches the stored representative's C byte-for-byte. A collision with mismatched
            // C = two inequivalent prefixes sharing a key = an OVER-MERGE (serVPrefix not faithful) = the one failure
            // mode that would make the 727 reduction undercount. All-OK => every collision has an explicit C(sigma0)
            // transporter (invg1_rep)^-1 . invg1_i, i.e. the dedup only merges genuinely equivalent blocks.
            const bool witness = std::getenv("REP_WITNESS") != nullptr;
            std::map<std::string, std::vector<Match>> wCanon; long long wOK = 0, wFAIL = 0, wSelf = 0, wGrp = 0;
            Perm rhoW = buildRhoStd(sh.alpha);
            int mPairs = 0; for (int x = 0; x < N; x++) if (sh.alpha[x] != x && sh.alpha[x] > x) mPairs++;
            // [F3COMPLETE] REP_F3COMPLETE: the integrated proof driver. Sweep ALL 0.0.c blocks, canonize each, and
            // COMPLETE only the first-occurrence (canonical) blocks -- the 8713 -- skipping every duplicate with a
            // count. Each completed block reports the classes it holds: if the whole sweep ends
            // with distinct==727 (0 new, aut still {2:727}) the a=0,b=0 column yields no class beyond the 727.
            // Shardable/resumable by RAW block c (0..57215) -- the "c" printed as "block a.0.c" and listed in
            // toprocess_t9.txt. Completes raw c in [F3START, F3STOP]; the sweep still canonizes the FULL
            // 0..57215 (~1min) to identify first-occurrences, so duplicates of already-done blocks are skipped
            // (never re-completed). RESUME: set REP_F3START = (last completed c printed) + 1.
            const bool doComplete = std::getenv("REP_F3COMPLETE") != nullptr;
            if (const char* ov = std::getenv("REP_CANONOVL")) { g_ovlLevel = atoi(ov); if (m_bPrint && g_ovlLevel > 0) { printf("[CANONOVL] tree-overlap measure ON: cap at level %d, canonKey each partial, accumulate across blocks\n", g_ovlLevel); fflush(stdout); } }
            const long long f3start = std::getenv("REP_F3START") ? atoll(std::getenv("REP_F3START")) : 0;   // first RAW block c to complete
            // The range is stated by its ENDS -- the two numbers the log prints, and the same numbers a
            // resume value is read off. REP_F3STOP is the LAST raw block c, INCLUSIVE, not a count.
            // Everything below still works in a count, so the count is derived here and nowhere else.
            // (The old REP_F3MAX is refused by checkRemovedEnv() in P1F-Census.cpp: reading a count as a stop
            // would silently run the wrong range.)
            const long long f3stop  = std::getenv("REP_F3STOP")  ? atoll(std::getenv("REP_F3STOP"))  : -1;  // last RAW block c, inclusive (unset = to the end of the column)
            const long long f3max   = f3stop < 0 ? -1 : (f3stop < f3start ? 0 : f3stop - f3start + 1);      // # RAW blocks from F3START; a stop below the start asks for nothing
            long long canonIdx = 0, completed = 0;
            long long ownTotSaved = 0, ownTotForeign = 0, ownTotInBlock = 0, ownTotAut = 0;   // [OWNER] run totals for the DONE line
            long long skipRejTotal = 0, skipUnkTotal = 0;   // grand totals across all skip runs: duplicate results the skips stand for, and skips whose twin's count is unknown
            long long skipStart = -1, skipEnd = -1, skipRun = 0, skipRejSum = 0, skipUnk = 0, skipOrigC = -1;   // current run of consecutive skipped (duplicate-starter) blocks
            const long long blocksForC = std::getenv("REP_BLOCKSFOR") ? atoll(std::getenv("REP_BLOCKSFOR")) : -1;   // [DIAG] list every RAW c sharing this block's level-3 canonical key (the raw blocks equivalent to it)
            std::map<std::string, std::vector<long long>> blkByKey; std::string blkTargetKey;
            auto completeBlockF3 = [&](const std::vector<Match>& pc) -> long long {   // complete one prefix on the nThreads work-queue (mirror of NAVIGATE completeBlock)
                long long e0 = g_emits.load(); g_maxDepth.store(0, std::memory_order_relaxed);   // diagnostic: reset per-block max depth
                std::deque<std::pair<std::vector<Match>, char>> nq; std::mutex nqmtx; std::atomic<int> nact{ 0 };
                const size_t QCAP = 200000; nq.push_back({ pc, (char)0 });
                std::vector<RepWorker> nw((size_t)nThreads); std::vector<std::thread> npool;
                if (g_precalc) {
                    // [PRECALC] Same idea as the K20 order-3 triple lists, at order 2 where an
                    // orbit is {F, sigmaF} -- or one factor when sigma fixes it. Build the list of
                    // orbits admissible at this block's prefix once, then walk the block by
                    // FILTERING it instead of regenerating candidates with genM at every node.
                    // The list code is orbit-size generic already: nrows comes from the orbit, so
                    // the same builder yields doubles here and triples on K20. Confirmed against
                    // the closed form at K14 order 2, where it builds exactly 2^6*6!/2 = 23,040.
                    //
                    // Branches are taken in sequence, as on the k20 plain path: no queue, no cap,
                    // no work handed back.
                    //
                    // NOTE the prototype covers trivial-stabilizer nodes only, so it needs
                    // REP_PRUNELEVEL=1 -- which t8 and t9 already run with, so the per-node dedup
                    // this path skips is off either way and both walk the same tree. Without that
                    // flag the trees DIVERGE and the list path is far slower (measured at K14
                    // order 2: the plain path loses dedup and blows up identically, so it is the
                    // dedup and not the list).
                    RepWorker& sw = nw[0]; sw.sh = &sh;
                    sw.clearState(); sw.commitOrbit(pc);
                    OrbTask seedOrb;
                    auto tL = std::chrono::steady_clock::now();
                    seedOrb.root = sw.buildOrbList();
                    std::vector<std::vector<Match>> front; std::vector<char> frontTriv; std::vector<OrbTask> frontOrb;
                    sw.splitNodeL(seedOrb, front, frontTriv, frontOrb);
                    if (m_bPrint) { std::lock_guard<std::mutex> lk(g_print_mtx);
                        xprintf("[K18-REP]  PRECALC: block list %zu orbits -> %zu branches (%.1fs)\n",
                               seedOrb.root->ent.size(), front.size(),
                               std::chrono::duration<double>(std::chrono::steady_clock::now() - tL).count());
                        fflush(stdout); }
                    std::atomic<size_t> bidx{ 0 };
                    for (int t = 0; t < nThreads; t++) {
                        nw[t].sh = &sh;
                        npool.emplace_back([&, t]() {
                            RepWorker& w = nw[t];
                            for (;;) {
                                if (g_stop.load(std::memory_order_relaxed)) break;
                                const size_t i = bidx.fetch_add(1, std::memory_order_relaxed);
                                if (i >= front.size()) break;
                                w.clearState(); w.commitOrbit(front[i]);
                                w.coverL(frontOrb[i]);
                            }
                        });
                    }
                    for (auto& th : npool) th.join();
                    return g_emits.load() - e0;
                }
                for (int t = 0; t < nThreads; t++) { nw[t].sh = &sh; npool.emplace_back([&, t]() { RepWorker& w = nw[t];
                    for (;;) {
                        if (g_stop.load(std::memory_order_relaxed)) break;
                        std::pair<std::vector<Match>, char> task; bool have = false;
                        { std::unique_lock<std::mutex> lk(nqmtx); if (!nq.empty()) { task = std::move(nq.back()); nq.pop_back(); nact.fetch_add(1); have = true; } else if (nact.load() == 0) break; }
                        if (!have) { std::this_thread::yield(); continue; }
                        w.clearState(); w.commitOrbit(task.first);
                        if (g_ovlLevel > 0 && (int)w.chosen.size() >= g_ovlLevel) {   // [DIAG] REP_CANONOVL: record this partial (order-independent canonKey), CAP -- do not expand deeper
                            if (!partialAllSym(w.chosen)) g_ovlNonSym.fetch_add(1, std::memory_order_relaxed);
                            // [FIX] partial-safe canonizer -- canonKey() indexes f[0..NM-1] and so overruns a
                            // PARTIAL, which is what made REP_CANONKEY segfault. canonV_fast handles partials
                            // (block canonization already uses it on the 5-factor starters).
                            int sz = (int)w.chosen.size(); std::string ck;
                            if (g_ovlKey) { uint8_t k4[NEDGES]; canonV_fast(w.chosen, sh.alpha, k4); ck.assign((const char*)k4, NEDGES); }
                            { std::lock_guard<std::mutex> lk(g_ovlMtx); g_ovlTot[sz]++; if (g_ovlKey) g_ovlSet[sz].insert(std::move(ck)); }
                            nact.fetch_sub(1); continue;
                        }
                        std::vector<std::vector<Match>> kk; std::vector<char> ktk; w.splitNode(task.second != 0, kk, ktk);
                        if (g_f17dump) { long long ks = (long long)kk.size(), om = g_maxKids.load(std::memory_order_relaxed); while (ks > om && !g_maxKids.compare_exchange_weak(om, ks, std::memory_order_relaxed)) {} }   // [DIAG] child-batch peak
                        bool pushed = false;
                        { std::unique_lock<std::mutex> lk(nqmtx); if (nq.size() < QCAP) { for (size_t j = 0; j < kk.size(); j++) nq.push_back({ std::move(kk[j]), ktk[j] }); pushed = true; }
                          if (g_f17dump) { long long qs = (long long)nq.size(), om = g_maxNQ.load(std::memory_order_relaxed); while (qs > om && !g_maxNQ.compare_exchange_weak(om, qs, std::memory_order_relaxed)) {} } }   // [DIAG] queue peak
                        if (!pushed) for (size_t j = 0; j < kk.size(); j++) { w.clearState(); w.commitOrbit(kk[j]); w.coverGen(ktk[j] != 0); }
                        nact.fetch_sub(1);
                    } }); }
                for (auto& th : npool) th.join();
                return g_emits.load() - e0;
            };
            auto flushSkips = [&]() {                       // emit the pending run of consecutive skipped (duplicate-starter) blocks, with the # of duplicate results they stand for
                if (skipRun <= 0) return;
                skipRejTotal += skipRejSum; skipUnkTotal += skipUnk;
                // (duplicate-starter blocks are counted, not printed -- they are never searched; the DONE line carries the total)
                skipRun = 0; skipStart = skipEnd = -1; skipRejSum = 0; skipUnk = 0; skipOrigC = -1;
            };
            size_t base3 = dw.chosen.size();               // sigma0 + F2 orbit already committed

            // [OWNER] Freeze the column. Everything ownerOf() needs is already computed here:
            // the prefix that every block of this column shares, the anchor childReps() found,
            // and ckeyIdx, which is the orbitKey -> raw c map the loop itself indexes blocks by.
            // isCanon is filled as the loop decides isNew below; it is only ever read for c
            // values at or below the current one, which the loop has already passed.
            if (g_ownerMode) {
                g_ownerCol.pfx.assign(dw.chosen.begin(), dw.chosen.begin() + base3);
                g_ownerCol.sig0 = sh.alpha;
                g_ownerCol.u0 = u0; g_ownerCol.v0 = v0;
                g_ownerCol.ckey = &ckeyIdx;
                g_ownerCol.gEl = &sh.gElems;
                g_ownerCol.isCanon.assign(creps.size(), 0);
                g_ownerCol.a0 = a0;
                int nfix = 0, d1 = 0;
                for (int u = 0; u < N; u++) { if (sh.alpha[u] == u) nfix++; if (g_ownerCol.pfx[0][u] == sh.alpha[u]) d1++; }
                g_ownerCol.d1 = d1 / 2;
                g_ownerCol.nfix = nfix;   // 0 = t9, 2 = t8; ownerAllTaus enumerates tau with the same count
            }
            if (doComplete) {   // [PROGRESS] the requested range, clipped to the column: a denominator fixed before the first block
                const long long lastC = (long long)creps.size() - 1;
                const long long a = f3start > lastC ? lastC : f3start;
                long long b = (f3max < 0 || f3start + f3max > lastC + 1) ? lastC : f3start + f3max - 1;
                if (b < a) b = a;
                g_blkTotal.store(b - a + 1); g_blkDone.store(0);
                // The range this run processes, by its ends, followed by the two variables that set it.
                // The left half is what will actually be walked -- clipped to the column -- and the right
                // half is what was ASKED for, so a stop beyond the last block shows up as a difference
                // between them instead of passing unnoticed. Printed for every block-driven run, owner
                // filter or not: it is the denominator every later per-block line is read against.
                // [TABLE] Column set B.  Exactly ONE subset per run here, so
                // the subset is the TITLE, not a column: it carries what [SUBSET] carried, and the range
                // caption above, unchanged wording, sits under it, so
                // both are handed to LogTable as one two-line title.  Done%'s denominator is the raw
                // range, fixed right here before the first block, which is why the figure can never
                // walk backwards.
                g_doneTotal = b - a + 1; g_doneUnits = 0;
                if (m_bPrint && !g_tbl) {
                    char ty[80]; snprintf(ty, sizeof(ty), "%s", tstr);
                    { size_t L = strlen(ty); while (L && ty[L - 1] == ' ') ty[--L] = '\0'; }
                    static char titleB[320];
                    snprintf(titleB, sizeof(titleB),
                             "Factorization of K%d P1F |Aut| = 2, symmetry type %s, branch a=%d\n"
                             "Blocks %lld-%lld selected for processing (REP_F3START=%lld REP_F3STOP=%lld)",
                             N, ty, a0, a, b, f3start, f3stop < 0 ? lastC : f3stop);
                    g_tblIsB = true;
                    g_tbl = new LogTable(titleB, kColsB, kTotalsB, sizeof(kColsB) / sizeof(kColsB[0]));
                }
            }
            for (size_t i = 0; i < creps.size(); i++) {
                std::vector<Match> orb; if (!dw.buildAndValidateOrbit(creps[i], orb)) continue;
                dw.commitOrbit(orb);
                if (cdumpf) for (const auto& F : orb) if (F[0] == 3) { cranks.insert(cRankCanon(F, sh.gElems)); break; }   // [DIAG] this block's c = the {0,3}-factor
                uint8_t key[NEDGES]; uint8_t g1[N];
                canonV_fast(dw.chosen, sh.alpha, key, witness ? g1 : nullptr);
                std::string ks((const char*)key, NEDGES);
                if (blocksForC >= 0) { blkByKey[ks].push_back((long long)i); if ((long long)i == blocksForC) blkTargetKey = ks; }   // [DIAG] REP_BLOCKSFOR: group raw c by level-3 canonical key
                if (witness) {
                    Perm invg1; for (int t = 0; t < N; t++) invg1[g1[t]] = (uint8_t)t;
                    bool grpOK = true;                     // g1 must map every standard sigma0-pair {2k,2k+1} to a standard pair
                    for (int kk = 0; kk < mPairs && grpOK; kk++) { int a = g1[2 * kk], b = g1[2 * kk + 1]; if ((a >> 1) != (b >> 1) || ((a ^ b) != 1)) grpOK = false; }
                    std::vector<Match> C; C.reserve(dw.chosen.size());   // canonical factor set = block pushed to std frame then relabeled by the winning order
                    for (const Match& f : dw.chosen) { Match Ps = applyAlpha(f, rhoW.data()); C.push_back(applyAlpha(Ps, invg1.data())); }
                    int e2fC[NEDGES]; for (int e = 0; e < NEDGES; e++) e2fC[e] = -1;
                    for (int fi = 0; fi < (int)C.size(); fi++) { const Match& F = C[fi]; for (int u = 0; u < N; u++) { int v = F[u]; if (u < v) e2fC[eidT[u][v]] = fi; } }
                    uint8_t idv[N]; for (int t = 0; t < N; t++) idv[t] = (uint8_t)t; uint8_t ser[NEDGES]; serVPrefix(e2fC, idv, N, ser);
                    bool selfOK = (memcmp(ser, key, NEDGES) == 0);   // does the reconstructed canonical set serialize back to the key?
                    if (!grpOK) wGrp++;
                    if (!selfOK) wSelf++;
                    std::sort(C.begin(), C.end());         // order-independent set comparison
                    auto it = wCanon.find(ks);
                    if (it == wCanon.end()) wCanon.emplace(ks, std::move(C));
                    else if (selfOK && grpOK && it->second == C) wOK++;
                    else { wFAIL++; if (wFAIL <= 12) { printf("[WITNESS] FAIL block %zu: key collision but selfOK=%d grpOK=%d canonMatch=%d\n", i, (int)selfOK, (int)grpOK, (int)(it->second == C)); fflush(stdout); } }
                }
                auto insIt = canonKeys.emplace(ks, std::make_pair((long long)i, (long long)-1));   // remember the FIRST c that owns this canonical starter
                bool isNew = insIt.second;
                if (std::getenv("REP_F3RAW")) isNew = true;   // [OPTIONAL] REP_F3RAW: disable 4.0.c block canonization (all 65,040 raw blocks). Default: canon ON (59,872) -- the neighbour-0 filter is canon-compatible, so keep the block dedup.
                if (g_ownerMode) g_ownerCol.isCanon[i] = isNew ? 1 : 0;   // [OWNER] the sweep only completes these, so only these may own a class
                if (isNew && lst) fprintf(lst, "%d.0.%zu\n", a0, i);   // record the RAW first-occurrence (canonical) block = an entry of the a.0.c list
                if (isNew && dmp) {   // dump this first-occurrence block's committed starter: one line of 9 edge-pairs per factor, then a blank separator
                    // [FIX] chosen[0..2] is the sigma0 + F2 prefix, IDENTICAL for every block -- dumping it wrote
                    // 25,530 copies of the same record. The starter is the WHOLE committed set: the prefix plus
                    // this block's own orbit (5 factors for t8, where F2/F3 are swaps of orbit size 2). Emitting
                    // all of chosen also makes the record usable as a REP_LOCATE target, which needs the full cover.
                    for (const Match& F : dw.chosen) { for (int u = 0; u < N; u++) if (u < F[u]) fprintf(dmp, "%d %d ", u, (int)F[u]); fprintf(dmp, "\n"); }
                    fprintf(dmp, "\n");
                }
                if (doComplete) {
                    if (isNew) {                               // on the list (first occurrence) -> complete it
                        flushSkips();                          // close any pending run of skipped duplicates first (one report)
                        long long ci = canonIdx++;             // canonical (completed-order) index, informational only
                        if ((long long)i >= f3start && (f3max < 0 || (long long)i < f3start + f3max)) {   // complete RAW block c=i
                            { char bb[32]; snprintf(bb, sizeof(bb), "%zu", i); g_curBlock = bb; }   // [INFO] label every class this block yields
                            // [OWNER] block-local state. g_ownerBlockSeen is the ONLY thing the filter
                            // remembers, it never crosses a block boundary, and that is what keeps two
                            // block ranges independent of each other.
                            if (g_ownerMode) { g_ownerBlockC = (long long)i; g_ownerBlockSeen.clear(); g_ownerSaved = g_ownerRejForeign = g_ownerRejInBlock = 0; }
                            g_ownerRejAut = 0;   // [AUT2ONLY] per-block, and not conditioned on the owner filter
                            // Elapsed and Total saved(duplicates) on the row below are CUMULATIVE:
                            // reading a census log while it runs, the number wanted is the range's
                            // total so far, and this block's own is one subtraction from the row
                            // above. Nodes and its rate stay this BLOCK's own -- which is what the
                            // column name says -- because they measure how fast this block searched.
                            long long savedBefore;
                            { std::lock_guard<std::mutex> lk(g_harvest_mtx); savedBefore = (long long)g_emittedAll.size(); }
                            g_legT0 = std::chrono::steady_clock::now(); g_legNodes0 = g_nodes.load(); g_legDup0 = g_crossDup.load();
                            long long n0blk = g_nodes.load(); long long em = completeBlockF3(dw.chosen); long long nblk = g_nodes.load() - n0blk;
                            g_curBlock.clear(); completed++; canonKeys[ks].second = em;   // record this canonical block's result count so its future duplicate-starters know how many results they stand for
                            const double blkSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_legT0).count();
                            long long blkSaved; std::map<int, int> savedHist, dupHist;
                            { std::lock_guard<std::mutex> lk(g_harvest_mtx);
                              blkSaved = (long long)g_emittedAll.size() - savedBefore;
                              savedHist = g_runSavedAut; dupHist = g_runDupAut; }
                            // Duplicates = every class this block found and did NOT write: the owner
                            // filter's three rejections, plus the classes a later block re-found.
                            const long long blkDups = (g_ownerMode ? g_ownerRejForeign + g_ownerRejInBlock : 0)
                                                    + g_ownerRejAut + (g_crossDup.load() - g_legDup0);
                            g_runFound += blkSaved + blkDups; g_runSaved += blkSaved; g_runDups += blkDups;
                            g_runNodes += nblk; g_runSec += blkSec;
                            if (m_bPrint && g_tbl) {
                                std::vector<std::string> cells;
                                char bc[24]; snprintf(bc, sizeof(bc), "%zu", i);
                                cells.push_back(bc);
                                cells.push_back(fmtElapsed(g_runSec));
                                cells.push_back(fmtNodes(nblk, blkSec));
                                cells.push_back(fmtSavedDup(g_runSaved, g_runDups, savedHist, dupHist));
                                cells.push_back(fmtPct((long long)i - f3start + 1, g_doneTotal));   // this block is walked: it IS the row being printed
                                g_tbl->row(' ', cells);
                            }
                            if (g_ownerMode) { ownTotSaved += g_ownerSaved; ownTotForeign += g_ownerRejForeign; ownTotInBlock += g_ownerRejInBlock; g_ownerBlockC = -1; }
                            ownTotAut += g_ownerRejAut;
                        }
                    } else if ((long long)i >= f3start) {      // duplicate-starter IN RANGE -> accumulate into the consecutive run
                        if (skipStart < 0) skipStart = (long long)i;   // (skips below REP_F3START are suppressed on resume)
                        skipEnd = (long long)i; skipRun++;
                        const auto& tw = canonKeys[ks];        // its already-seen canonical twin: {first-occurrence c, that twin's result count}
                        skipOrigC = tw.first;
                        if (tw.second >= 0) skipRejSum += tw.second;   // twin searched this run -> we know EXACTLY how many duplicate results this block would produce
                        else skipUnk++;                        // twin completed in a prior run segment -> count unknown this run
                    }
                }
                dw.rollbackTo(base3);
                if (doComplete && (long long)i >= f3start) { g_blkDone.store((long long)i - f3start + 1, std::memory_order_relaxed);
                                                             g_doneUnits = (long long)i - f3start + 1; }   // [PROGRESS] blocks WALKED, completed and skipped alike -- the Done% numerator
                if (doComplete && f3max >= 0 && (long long)i + 1 >= f3start + f3max) break;   // reached raw-c end of this shard
                if (m_bPrint && !doComplete && i && (i % 10000 == 0)) { printf("[F3COUNT] %zu/%zu, distinct canonical starters so far=%zu\n", i, creps.size(), canonKeys.size()); fflush(stdout); }
            }
            if (doComplete) flushSkips();                      // flush the final pending skip run
            if (lst) fclose(lst);
            if (dmp) fclose(dmp);
            if (witness && m_bPrint) { printf("[WITNESS] blocks=%zu | collisions matched(OK)=%lld  OVER-MERGE(FAIL)=%lld | self-check fails=%lld | C(sigma0)-membership fails=%lld | distinct canon=%zu\n", creps.size(), wOK, wFAIL, wSelf, wGrp, wCanon.size()); fflush(stdout); }
            if (doComplete && m_bPrint) {
                // The range the run was ASKED for, clipped to the column, and how many blocks in it
                // were canonical -- i.e. actually completed. The rest are relabelings of an earlier
                // block and are skipped. No "last block" is reported: it would be the last COMPLETED
                // one, which stalls short of the true end whenever the range closes on skipped blocks.
                // The two [F3COMPLETE] DONE: lines are REPLACED by the = row, which already carries
                // saved and duplicates in the same columns every block row used.
                // Only the facts with no column survive, as footnotes under the table: how many blocks in
                // the range were canonical -- the rest are relabelings of an earlier block and are skipped
                // -- and the |Aut| > 2 rejection histogram. No "last block" is reported: it would be the
                // last COMPLETED one, which stalls short of the true end whenever the range closes on
                // skipped blocks.
                if (g_tbl) {
                    std::vector<std::string> cells;
                    cells.push_back("TOTAL");
                    cells.push_back(fmtElapsed(g_runSec));
                    cells.push_back(fmtNodes(g_runNodes, g_runSec));
                    cells.push_back(fmtCount(g_runSaved, g_runSavedAut));
                    cells.push_back(fmtPct(g_doneUnits, g_doneTotal));
                    g_tbl->total(cells);
                    delete g_tbl; g_tbl = nullptr; g_tblIsB = false;
                }
                printf("\n[F3COMPLETE] canonical blocks in range: %lld\n", completed); fflush(stdout);
                // [AUT2ONLY] The symmetry groups behind the rejected covers. Under the owner filter
                // each class is counted once, by the block that owns it, so a full-column run prints
                // the |Aut|>2 census of this branch -- derived here by a completely different route
                // than the case that publishes it.
                if (g_aut2Only) {
                    std::lock_guard<std::mutex> lk(g_harvest_mtx);
                    if (!g_autRejHist.empty()) {
                        std::string h;
                        for (auto& kv : g_autRejHist) { char t[32]; snprintf(t, sizeof(t), "%s%d:%lld", h.empty() ? "" : " ", kv.first, kv.second); h += t; }
                        printf("[F3COMPLETE] duplicates by |Aut| > 2: aut{%s}\n", h.c_str()); fflush(stdout);
                    }
                }
            }
            if (m_bPrint && !doComplete) { printf("[F3COUNT] a=%d b=0, F3 anchor {%d,%d}: raw F3 blocks=%zu, CANONICAL starters (canonV_fast distinct)=%zu%s\n", a0, u0, v0, creps.size(), canonKeys.size(), lst ? " -> wrote to-process list" : ""); fflush(stdout); }
            if (blocksForC >= 0 && m_bPrint) {   // [DIAG] REP_BLOCKSFOR: report the raw blocks canonically equivalent to the target (all yield its class)
                auto it = blkByKey.find(blkTargetKey);
                if (blkTargetKey.empty() || it == blkByKey.end()) printf("[BLOCKSFOR] target raw block %lld NOT reached in the sweep (run full F3COUNT with no REP_F3STOP, no REP_PATONLY)\n", blocksForC);
                else { printf("[BLOCKSFOR] raw block %lld's level-3 canonical class = %zu raw block(s) (all canonize to the same starter, all complete to the same result):\n", blocksForC, it->second.size());
                    std::string s; for (long long c : it->second) { char t[16]; snprintf(t, sizeof(t), "%lld ", c); s += t; }
                    printf("[BLOCKSFOR]   c = %s\n", s.c_str()); }
                fflush(stdout);
            }
            if (cdumpf) { FILE* cf = fopen(cdumpf, "w"); if (cf) { for (long long r : cranks) fprintf(cf, "%lld\n", r); fclose(cf); }
                if (m_bPrint) { printf("[CDUMP] distinct canonical c-ranks=%zu -> %s | rank range [%lld, %lld]\n", cranks.size(), cdumpf, cranks.empty() ? 0 : *cranks.begin(), cranks.empty() ? 0 : *cranks.rbegin()); fflush(stdout); } }
            if (g_ovlLevel > 0 && m_bPrint) {   // [DIAG] REP_CANONOVL report: cross-block tree overlap = total partials / distinct canonKeys, per size
                std::lock_guard<std::mutex> lk(g_ovlMtx);
                printf("[CANONOVL] cross-block tree-overlap at size>=%d (across %lld completed blocks):\n", g_ovlLevel, completed);
                std::vector<int> szs; for (auto& kv : g_ovlTot) szs.push_back(kv.first); std::sort(szs.begin(), szs.end());
                for (int sz : szs) { long long tot = g_ovlTot[sz]; long long dist = (long long)g_ovlSet[sz].size();
                    printf("    size %2d: total=%lld  distinct=%lld  overlap=%.2fx\n", sz, tot, dist, dist ? (double)tot / dist : 0.0); }
                fflush(stdout);
            }
            continue;
        }
        // [NAVIGATE] REP_NAVIGATE=a.b.c = START block. REP_NAVSTEP=Da.Db.Dc = per-step coordinate increment
        // (default 0.0.1 = next block). REP_NAVSTEPS=N = number of step positions (default 1). At each of the
        // N positions the coordinate is navigated by index (PL-independent) and a CHUNK of consecutive blocks
        // is COMPLETED, advancing to the next step when: REP_NAVMAXBLK blocks done (default 1) OR REP_NAVRES
        // new results OR (each block) REP_NAVCAP seconds. Then coord += step. Out-of-range positions are
        // reported SKIPPED and the walk continues. Completion runs on the KThreads work-queue (PL-gated by
        // REP_PRUNELEVEL). REP_NAVEST=<probes> estimates instead.
        if (const char* nav = std::getenv("REP_NAVIGATE")) {
            auto parseCoord = [](const char* s, std::vector<int>& out) { out.clear(); const char* p = s; while (*p) { out.push_back(atoi(p)); while (*p && *p != '.') p++; while (*p == '.') p++; } };
            std::vector<int> coord; parseCoord(nav, coord);
            std::vector<int> step; if (const char* sv = std::getenv("REP_NAVSTEP")) parseCoord(sv, step);
            if (step.empty()) { step.assign(coord.size(), 0); if (!coord.empty()) step.back() = 1; }   // default 0.0.1
            while (step.size() < coord.size()) step.insert(step.begin(), 0);                            // pad to coord width
            long long nsteps = std::getenv("REP_NAVSTEPS") ? atoll(std::getenv("REP_NAVSTEPS")) : 1;
            long long maxblk = std::getenv("REP_NAVMAXBLK") ? atoll(std::getenv("REP_NAVMAXBLK")) : 1;   // blocks per position
            double navCap = std::getenv("REP_NAVCAP") ? atof(std::getenv("REP_NAVCAP")) : 0.0;           // per-block time cap (s)
            long long resCap = std::getenv("REP_NAVRES") ? atoll(std::getenv("REP_NAVRES")) : 0;         // new results per position
            int navEst = std::getenv("REP_NAVEST") ? atoi(std::getenv("REP_NAVEST")) : 0;
            g_typeIdx = tIdx; snprintf(g_typeStr, sizeof(g_typeStr), "%s", tstr);
            { size_t L = strlen(g_typeStr); while (L && g_typeStr[L - 1] == ' ') g_typeStr[--L] = '\0'; }   // drop tstr's display-pad
            { std::string ss; for (size_t i = 0; i < step.size(); i++) { if (i) ss += "."; ss += std::to_string(step[i]); }
              if (m_bPrint) { printf("[NAVIGATE] start %s, step %s, steps %lld | per-pos: maxblk=%lld navcap=%.0fs res=%lld%s\n", nav, ss.c_str(), nsteps, maxblk, navCap, resCap, navEst > 0 ? " [EST]" : ""); fflush(stdout); } }
            // complete ONE block (full prefix cover pc) on the KThreads work-queue; per-block cap navCap; returns covers emitted.
            auto completeBlock = [&](const std::vector<Match>& pc) -> long long {
                long long e0 = g_emits.load(); g_maxDepth.store(0, std::memory_order_relaxed);   // diagnostic: reset per-block max depth
                std::deque<std::pair<std::vector<Match>, char>> nq; std::mutex nqmtx; std::atomic<int> nact{ 0 };
                const size_t QCAP = 200000; nq.push_back({ pc, (char)0 });
                auto t0 = std::chrono::steady_clock::now();
                std::vector<RepWorker> nw((size_t)nThreads); std::vector<std::thread> npool;
                for (int t = 0; t < nThreads; t++) { nw[t].sh = &sh; npool.emplace_back([&, t]() { RepWorker& w = nw[t];
                    for (;;) {
                        if (g_stop.load(std::memory_order_relaxed)) break;
                        if (navCap > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > navCap) break;
                        std::pair<std::vector<Match>, char> task; bool have = false;
                        { std::unique_lock<std::mutex> lk(nqmtx); if (!nq.empty()) { task = std::move(nq.back()); nq.pop_back(); nact.fetch_add(1); have = true; } else if (nact.load() == 0) break; }
                        if (!have) { std::this_thread::yield(); continue; }
                        w.clearState(); w.commitOrbit(task.first);
                        std::vector<std::vector<Match>> kk; std::vector<char> ktk; w.splitNode(task.second != 0, kk, ktk);
                        bool pushed = false;
                        { std::unique_lock<std::mutex> lk(nqmtx); if (nq.size() < QCAP) { for (size_t i = 0; i < kk.size(); i++) nq.push_back({ std::move(kk[i]), ktk[i] }); pushed = true; } }
                        if (!pushed) for (size_t i = 0; i < kk.size(); i++) { w.clearState(); w.commitOrbit(kk[i]); w.coverGen(ktk[i] != 0); }
                        nact.fetch_sub(1);
                    } }); }
                for (auto& th : npool) th.join();
                return g_emits.load() - e0;
            };
            std::vector<int> cur = coord;
            for (long long s = 0; s < nsteps && !g_stop.load(std::memory_order_relaxed); s++) {
                std::string cs; for (size_t i = 0; i < cur.size(); i++) { if (i) cs += "."; cs += std::to_string(cur[i]); }
                std::string pfx; for (size_t i = 0; i + 1 < cur.size(); i++) { if (i) pfx += "."; pfx += std::to_string(cur[i]); }
                RepWorker dw; dw.sh = &sh; dw.clearState(); bool ok = true;   // navigate cur's prefix (all but last)
                for (size_t L = 0; L + 1 < cur.size() && ok; L++) {
                    size_t bs = dw.chosen.size();
                    if (L == 0) { if (cur[0] < 0 || cur[0] >= (int)reps.size()) { ok = false; break; } std::vector<Match> orb; if (!dw.buildAndValidateOrbit(reps[cur[0]].m0, orb)) { ok = false; break; } dw.commitOrbit(orb); }
                    else { std::vector<std::vector<Match>> kk; std::vector<char> kt; dw.splitNode(true, kk, kt); if (cur[L] < 0 || cur[L] >= (int)kk.size()) { ok = false; break; } std::vector<Match> no(kk[cur[L]].begin() + bs, kk[cur[L]].end()); dw.commitOrbit(no); }
                }
                std::vector<std::vector<Match>> kids; std::vector<char> kt; size_t base = 0;
                if (ok) { base = dw.chosen.size(); dw.splitNode(true, kids, kt); int c = cur.back(); if (c < 0 || c >= (int)kids.size()) ok = false; }
                if (!ok) { if (m_bPrint) { printf("[NAVIGATE] block %s SKIPPED (out of range)\n", cs.c_str()); fflush(stdout); } for (size_t i = 0; i < cur.size(); i++) cur[i] += step[i]; continue; }
                long long chRes = 0, nb = 0;               // chunk: consecutive blocks from cur.back() until maxblk / resCap
                for (int cc = cur.back(); cc < (int)kids.size() && !g_stop.load(std::memory_order_relaxed); cc++) {
                    if (maxblk > 0 && nb >= maxblk) break;
                    if (resCap > 0 && chRes >= resCap) break;
                    std::vector<Match> no(kids[cc].begin() + base, kids[cc].end()); dw.commitOrbit(no);
                    std::string bs2 = pfx.empty() ? std::to_string(cc) : (pfx + "." + std::to_string(cc));
                    if (std::getenv("REP_NAVDUMP") && m_bPrint) {   // dump the block's committed prefix factors (edge pairs)
                        printf("[NAVIGATE-DUMP] block %s prefix (%zu factors):\n", bs2.c_str(), dw.chosen.size());
                        for (const auto& F : dw.chosen) { for (int u = 0; u < N; u++) if (u < (int)F[u]) printf("%d %d  ", u, (int)F[u]); printf("\n"); }
                        fflush(stdout);
                    }
                    if (navEst > 0) {
                        std::vector<Match> pc = dw.chosen; RepWorker pw; pw.sh = &sh; std::mt19937 rng((unsigned)(1234567u + (unsigned)cc));
                        double tot = 0, mx = 0; for (int i = 0; i < navEst; i++) { pw.clearState(); pw.commitOrbit(pc); double e = pw.probeCoverGen(rng); tot += e; if (e > mx) mx = e; }
                        if (m_bPrint) { printf("[NAVIGATE-EST] block %s: ~%.3g PL=0 cover-nodes (mean %d probes; max %.3g)\n", bs2.c_str(), tot / navEst, navEst, mx); fflush(stdout); }
                    } else {
                        if (m_bPrint) { printf("[NAVIGATE] block %s: completing (%d threads, PL=%s)...\n", bs2.c_str(), nThreads, g_pruneMaxFactors > 0 ? "1" : "0"); fflush(stdout); }
                        long long em = completeBlock(dw.chosen); chRes += em;
                        if (m_bPrint) { std::string h; { std::lock_guard<std::mutex> lk(g_harvest_mtx); for (auto& kv : g_autHist) { char t[24]; snprintf(t, sizeof(t), "%s%d:%d", h.empty() ? "" : " ", kv.first, kv.second); h += t; } }
                            printf("[NAVIGATE] block %s done: covers emitted=%lld, maxlvl=%d/%d, cumulative aut{%s}\n", bs2.c_str(), em, g_maxDepth.load(), NM, h.c_str()); fflush(stdout); }
                    }
                    dw.rollbackTo(base); nb++;
                }
                for (size_t i = 0; i < cur.size(); i++) cur[i] += step[i];   // STEP the coordinate
            }
            continue;
        }
        // [COMPLETEPREFIX] REP_COMPLETEPREFIX=<file>: complete EXPLICIT sigma0-frame prefixes (not an a.b.c).
        // The file holds one-or-more records; each record = a "|Aut| header" line followed by factor rows
        // (each row = 9 space-separated edge pairs = one matching). The record's factors are committed as the
        // prefix and completed on the KThreads work-queue (mirror of NAVIGATE completeBlock).
        // Used to drive the re-framed a=4 (k=9) starter list. REP_CPCAP=<sec> per-record cap.
        if (const char* cpf = std::getenv("REP_COMPLETEPREFIX")) {
            g_typeIdx = tIdx; snprintf(g_typeStr, sizeof(g_typeStr), "%s", tstr);
            { size_t L = strlen(g_typeStr); while (L && g_typeStr[L - 1] == ' ') g_typeStr[--L] = '\0'; }
            double cpCap = std::getenv("REP_CPCAP") ? atof(std::getenv("REP_CPCAP")) : 0.0;
            int nrec = 0;   // current starter index (declared before completeBlock so its monitor thread can name it)
            std::set<long long> cset; std::mutex cmtx; size_t lastDistinctC = 0; long long lastStartC = 0, lastLastC = 0;   // distinct c-ranks of the current starter -- LOCAL to this branch, no globals
            auto completeBlock = [&](const std::vector<Match>& pc) -> long long {
                long long e0 = g_emits.load(); g_maxDepth.store(0, std::memory_order_relaxed);
                { std::lock_guard<std::mutex> lk(cmtx); cset.clear(); }   // reset distinct-c for this starter
                std::deque<std::pair<std::vector<Match>, char>> nq; std::mutex nqmtx; std::atomic<int> nact{ 0 };
                const size_t QCAP = 200000; nq.push_back({ pc, (char)0 });
                auto t0 = std::chrono::steady_clock::now();
                std::atomic<bool> mdone{ false };   // periodic distinct-c reporter for THIS starter (local monitor thread; no globals)
                std::thread mon([&]() {
                    auto m0 = std::chrono::steady_clock::now(); auto last = m0;
                    while (!mdone.load(std::memory_order_relaxed)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration<double>(now - last).count() < 300.0) continue;
                        last = now; size_t n; long long cs = 0, ce = 0; { std::lock_guard<std::mutex> lk(cmtx); n = cset.size(); if (n) { cs = *cset.begin(); ce = *cset.rbegin(); } }
                        if (m_bPrint) { printf("    [rep] COMPLETEPREFIX rec %d elapsed=%.0fs startC=%lld lastC=%lld distinctC=%zu results=%lld\n", nrec, std::chrono::duration<double>(now - m0).count(), cIdx(cs), cIdx(ce), n, g_emits.load() - e0); fflush(stdout); }
                    }
                });
                std::vector<RepWorker> nw((size_t)nThreads); std::vector<std::thread> npool;
                for (int t = 0; t < nThreads; t++) { nw[t].sh = &sh; nw[t].cSet = &cset; nw[t].cMtx = &cmtx; npool.emplace_back([&, t]() { RepWorker& w = nw[t];
                    for (;;) {
                        if (g_stop.load(std::memory_order_relaxed)) break;
                        if (cpCap > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > cpCap) break;
                        std::pair<std::vector<Match>, char> task; bool have = false;
                        { std::unique_lock<std::mutex> lk(nqmtx); if (!nq.empty()) { task = std::move(nq.back()); nq.pop_back(); nact.fetch_add(1); have = true; } else if (nact.load() == 0) break; }
                        if (!have) { std::this_thread::yield(); continue; }
                        w.clearState(); w.commitOrbit(task.first);
                        if (g_ovlLevel > 0 && (int)w.chosen.size() >= g_ovlLevel) {   // [DIAG] REP_CANONOVL (COMPLETEPREFIX path): record partial, cap (no expand)
                            if (!partialAllSym(w.chosen)) g_ovlNonSym.fetch_add(1, std::memory_order_relaxed);
                            // [FIX] partial-safe canonizer -- canonKey() indexes f[0..NM-1] and so overruns a
                            // PARTIAL, which is what made REP_CANONKEY segfault. canonV_fast handles partials
                            // (block canonization already uses it on the 5-factor starters).
                            int sz = (int)w.chosen.size(); std::string ck;
                            if (g_ovlKey) { uint8_t k4[NEDGES]; canonV_fast(w.chosen, sh.alpha, k4); ck.assign((const char*)k4, NEDGES); }
                            { std::lock_guard<std::mutex> lk(g_ovlMtx); g_ovlTot[sz]++; if (g_ovlKey) g_ovlSet[sz].insert(std::move(ck)); }
                            nact.fetch_sub(1); continue;
                        }
                        std::vector<std::vector<Match>> kk; std::vector<char> ktk; w.splitNode(task.second != 0, kk, ktk);
                        bool pushed = false;
                        { std::unique_lock<std::mutex> lk(nqmtx); if (nq.size() < QCAP) { for (size_t i = 0; i < kk.size(); i++) nq.push_back({ std::move(kk[i]), ktk[i] }); pushed = true; } }
                        if (!pushed) for (size_t i = 0; i < kk.size(); i++) { w.clearState(); w.commitOrbit(kk[i]); w.coverGen(ktk[i] != 0); }
                        nact.fetch_sub(1);
                    } }); }
                for (auto& th : npool) th.join();
                mdone.store(true, std::memory_order_relaxed); mon.join();
                { std::lock_guard<std::mutex> lk(cmtx); lastDistinctC = cset.size(); if (lastDistinctC) { lastStartC = *cset.begin(); lastLastC = *cset.rbegin(); } }
                return g_emits.load() - e0;
            };
            FILE* fp = fopen(cpf, "r");
            if (!fp) { if (m_bPrint) { printf("[COMPLETEPREFIX] cannot open %s\n", cpf); fflush(stdout); } continue; }
            char line[1024]; std::vector<Match> pref;
            auto flushRec = [&]() {
                if (pref.empty()) return; nrec++;
                long long em = completeBlock(pref);
                long long dist = (long long)g_harvest.size();
                std::string h; { std::lock_guard<std::mutex> lk(g_harvest_mtx); for (auto& kv : g_autHist) { char t[24]; snprintf(t, sizeof(t), "%s%d:%d", h.empty() ? "" : " ", kv.first, kv.second); h += t; } }
                if (m_bPrint) { printf("[COMPLETEPREFIX] rec %d (%zu factors): results=%lld  maxlvl=%d/%d  startC=%lld lastC=%lld distinctC=%zu  saved=%lld  aut{%s}\n", nrec, pref.size(), em, g_maxDepth.load(), NM, cIdx(lastStartC), cIdx(lastLastC), lastDistinctC, dist, h.c_str()); fflush(stdout); }
                pref.clear();
            };
            while (fgets(line, sizeof(line), fp)) {
                int vals[64], nv = 0; char* q = line;
                while (*q && nv < 64) { while (*q && !(*q >= '0' && *q <= '9')) q++; if (!*q) break; vals[nv++] = atoi(q); while (*q >= '0' && *q <= '9') q++; }
                if (nv == N) { Match F; for (int u = 0; u < N; u++) F[u] = 0xFF; for (int k = 0; k + 1 < nv; k += 2) { F[vals[k]] = (uint8_t)vals[k + 1]; F[vals[k + 1]] = (uint8_t)vals[k]; } pref.push_back(F); }
                else if (strstr(line, "Aut")) { flushRec(); }   // header line = start of next record
            }
            flushRec();
            fclose(fp);
            if (m_bPrint) { printf("[COMPLETEPREFIX] DONE: %d prefixes completed, distinct saved=%zu\n", nrec, g_harvest.size()); fflush(stdout); }
            if (g_ovlLevel > 0 && m_bPrint) {   // [DIAG] REP_CANONOVL report: cross-prefix tree overlap + sigma0-symmetry of completion factors
                std::lock_guard<std::mutex> lk(g_ovlMtx);
                printf("[CANONOVL] cross-prefix overlap at size>=%d (across %d prefixes) | partials with a NON-sigma0-symmetric factor = %lld:\n", g_ovlLevel, nrec, g_ovlNonSym.load());
                std::vector<int> szs; for (auto& kv : g_ovlTot) szs.push_back(kv.first); std::sort(szs.begin(), szs.end());
                for (int sz : szs) { long long tot = g_ovlTot[sz]; long long dist = (long long)g_ovlSet[sz].size();
                    printf("    size %2d: total=%lld  distinct=%lld  overlap=%.2fx\n", sz, tot, dist, dist ? (double)tot / dist : 0.0); }
                fflush(stdout);
            }
            continue;
        }
        // [ESTIMATE MODE] Knuth probes instead of the search: per representative run
        // estProbes random descents and report mean estimated subtree node counts.
        // ETA for a real run = (est TOTAL) / (measured nodes/s of a real run).
        if (estProbes > 0) {
            if (sh.Calpha.empty() && m_bPrint) { printf("[K18-REP]  type %-14s ESTIMATE: over-cap type -- probing the coverGen tree (exact per-node stabilizer dedup)\n", tstr); fflush(stdout); }
            if (reps.empty()) {   // root-collapsed type: exhausted during collection -> 0 nodes, 0 classes
                if (m_bPrint) { printf("[K18-REP]  type %-14s ESTIMATE: reps 0 -> est cover-nodes TOTAL ~ 0 (type exhausts at the root)\n", tstr); fflush(stdout); }
                continue;
            }
            // Parallelize over (rep, probe) PAIRS: with only a handful of root reps a
            // per-rep split leaves every thread but one idle once the small reps finish
            // (the dense over-cap types have 4-5 reps, one of them dominant).
            std::vector<double> estVal((size_t)reps.size() * estProbes, 0.0);
            std::atomic<size_t> nextT{ 0 };
            std::vector<std::thread> epool;
            for (int t = 0; t < nThreads; t++) epool.emplace_back([&]() {
                RepWorker w; w.sh = &sh;
                for (;;) {
                    size_t idx = nextT.fetch_add(1);
                    if (idx >= estVal.size()) break;
                    size_t r = idx / estProbes; int p = (int)(idx % estProbes);
                    std::vector<Match> orbit;
                    w.clearState();
                    if (!w.buildAndValidateOrbit(reps[r].m0, orbit)) continue;   // rep contributes 0
                    w.commitOrbit(orbit);
                    std::mt19937 rng((unsigned)(1000003u * (unsigned)r + 7919u * (unsigned)p + 12345u));
                    estVal[idx] = sh.Calpha.empty() ? w.probeCoverGen(rng) : w.probeCover(reps[r].stab, rng);
                }
            });
            for (auto& th : epool) th.join();
            std::vector<double> estMean(reps.size(), 0.0), estMax(reps.size(), 0.0);
            for (size_t r = 0; r < reps.size(); r++) {
                double s = 0, mx = 0;
                for (int p = 0; p < estProbes; p++) { double e = estVal[r * estProbes + p]; s += e; if (e > mx) mx = e; }
                estMean[r] = s / estProbes; estMax[r] = mx;
            }
            double total = 0, worstMean = 0; size_t worstR = 0;
            for (size_t r = 0; r < reps.size(); r++) { total += estMean[r]; if (estMean[r] > worstMean) { worstMean = estMean[r]; worstR = r; } }
            if (m_bPrint) {
                printf("[K18-REP]  type %-14s ESTIMATE: reps %zu, probes/rep %d -> est cover-nodes TOTAL ~ %.3g (largest rep #%zu ~ %.3g mean, %.3g max-probe)\n",
                       tstr, reps.size(), estProbes, total, worstR, worstMean, estMax[worstR]);
                printf("[K18-REP]  type %-14s ESTIMATE: ETA = above TOTAL / (nodes-per-second of a real run on the same machine/threads)\n", tstr);
                // PER-REP, because on the enumerable path a rep IS a block (REP_RANGE=r:r+1),
                // and a census is scheduled from the block-size DISTRIBUTION, not the total:
                // it says how uneven the work is, which blocks are cheap enough to finish
                // first, and whether any are near-empty. max-probe next to the mean is the
                // spread WITHIN a rep -- if it towers over the mean, raise the probe count
                // before believing that row.
                const double meanAll = reps.empty() ? 0.0 : total / (double)reps.size();
                printf("[K18-REP]  type %-14s ESTIMATE per block: idx  est-nodes  xMean  max-probe\n", tstr);
                for (size_t r = 0; r < reps.size(); r++)
                    printf("[K18-REP]    block %3zu  %10.3g  %6.2fx  %10.3g\n",
                           r, estMean[r], meanAll > 0 ? estMean[r] / meanAll : 0.0, estMax[r]);
                fflush(stdout);
            }
            continue;
        }
        // publish current-type progress state for the periodic [rep] line
        g_typeIdx = tIdx; g_reps_done.store(0); g_reps_total.store(0);
        snprintf(g_typeStr, sizeof(g_typeStr), "%s", tstr);
        { size_t L = strlen(g_typeStr); while (L && g_typeStr[L - 1] == ' ') g_typeStr[--L] = '\0'; }   // drop tstr's display-pad
        std::vector<RepWorker> workers((size_t)nThreads);
        std::vector<std::thread> pool;
        // Worker-shared state. MUST outlive the threads (joined after the if/else below), so it is
        // declared here in the same scope as `pool`/the join -- NOT inside the branches, where it
        // would be destroyed before the threads referencing it run.
        std::deque<std::pair<std::vector<Match>, char>> queue;  // OVER-CAP: work queue of (partial cover, stab-trivial flag)
        std::mutex qmtx;                                  // OVER-CAP: guards `queue`
        std::atomic<int> active{ 0 };                     // OVER-CAP: tasks currently being expanded
        std::atomic<size_t> next{ 0 };                    // ENUMERABLE: next representative-task index
        std::vector<std::vector<Match>> shardFront;       // SHARD: level-L nodes (full partial cover each)
        std::vector<char> shardTriv;                      // SHARD: per-node stab-trivial flag
        size_t shardHi = 0;                               // SHARD: number of branches to process (0 = count only)
        std::atomic<long long> divesDone{ 0 };            // FAST SEARCH: random restarts performed
        // SAMPLE (REP_SAMPLE=stride[:cap], deep-shard RANGE on the enumerable path): seed every
        // stride-th branch of the slice, run each to completion or until it has cost `cap` nodes,
        // print per-branch node counts, and scale by the stride for an estimate of the slice.
        long long sampleStride = 0, sampleCap = 0;
        if (const char* es = std::getenv("REP_SAMPLE")) { sampleStride = atoll(es); if (const char* c = strchr(es, ':')) sampleCap = atoll(c + 1); }
        std::vector<size_t> sampleIdx;                    // SAMPLE: global level-L index of each sampled branch
        std::vector<long long> brNodes;                   // SAMPLE: nodes charged to each sampled branch (under qmtx)
        std::vector<int> brOpen;                          // SAMPLE: tasks outstanding per sampled branch (under qmtx)
        std::vector<char> brCapped;                       // SAMPLE: branch abandoned at the cap (under qmtx)
        std::deque<int> qBranch;                          // SAMPLE: lockstep with `queue` in the enumerable pool -- branch of each item (-1 = untracked)
        std::deque<OrbTask> qOrb;                       // PRECALC: lockstep with `queue` in the enumerable pool -- the item's list (root null until the seed builds it)
        if (sh.Calpha.empty() && diveMode && shardLevel == 0) {   // pure fast search (no sweep); with REP_LEVEL the sweep's runBlock dives per block
            // [FAST SEARCH] parallel random dives: up to diveTarget dives across all threads, each ->
            // emit if it reaches a complete cover; the global canonKey dedups. Stops at diveTarget dives
            // or when a harvest target sets g_stop. No .restart needed -- a re-run just keeps diving.
            if (reps.empty()) { if (m_bPrint) { printf("[K18-REP]  type %-14s FAST SEARCH: no root reps -> 0\n", tstr); fflush(stdout); } continue; }
            if (m_bPrint) { printf("[K18-REP]  type %-14s FAST SEARCH (backtracking dives, budget %lld nodes/restart) x %d threads -> run until target/kill...\n", tstr, diveBudget, nThreads); fflush(stdout); }
            g_reps_total.store(0); g_reps_done.store(0);
            for (int t = 0; t < nThreads; t++) {
                workers[t].sh = &sh;
                pool.emplace_back([&, t]() {
                    RepWorker& w = workers[t];
                    std::random_device rd; std::mt19937 rng(rd() ^ ((unsigned)(t + 1) * 0x9E3779B9u));   // per-thread, per-run entropy
                    std::uniform_int_distribution<int> pickRep(0, (int)reps.size() - 1);
                    for (;;) {
                        if (g_stop.load(std::memory_order_relaxed)) break;
                        divesDone.fetch_add(1);                          // one random restart
                        w.clearState();
                        std::vector<Match> orbit;
                        if (w.buildAndValidateOrbit(reps[pickRep(rng)].m0, orbit)) {
                            w.commitOrbit(orbit);
                            long long budget = diveBudget;
                            w.diveGen(rng, budget);                      // random backtracking DFS, up to diveBudget nodes
                        }
                        g_reps_done.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
        } else if (sh.Calpha.empty() && shardLevel > 0) {
            // [SHARD MODE] Partition the over-cap tree at depth `shardLevel` (orbit-blocks committed)
            // into deterministic, index-stable subtree roots ("branches"), ordered depth-first by
            // (parent index, child order).
            //  * range mode (REP_RANGE set): build ONLY the branches in [start,end) via a lazy in-order
            //    DFS that stops at `end` -- the first block starts almost immediately, no whole-level
            //    enumeration or storage. Then process them (each -> full coverGen subtree).
            //  * count mode (no REP_RANGE): enumerate the WHOLE level in parallel and print the branch
            //    count at each depth (to size the shards); process nothing.
            auto tS = std::chrono::steady_clock::now();
            auto elapsed = [&]{ return std::chrono::duration<double>(std::chrono::steady_clock::now() - tS).count(); };
            const size_t QCAP = 200000;                   // queue cap (~60MB); over it, drain subtrees locally
            if (rangeMode && chunkStep > 0) {
                // ===== CHUNKED DENSITY SWEEP: REP_RANGE=start:end:step(timeout,results) =====
                // ONE continuous DFS enumerates the branches of [rangeStart,rangeEnd); it processes the FRONT
                // blocks of each `chunkStep`-block chunk (each via the threaded work-queue) and advances to the
                // next chunk the instant a per-chunk cap is hit -- `chunkTimeout` seconds OR `chunkResultCap` new
                // classes -- checked MID-BLOCK (the block halts early), then skips the rest of the chunk and
                // resumes at the next boundary. A per-chunk density line reports +classes and the stop reason.
                // Memory-safe: one block in flight, work-queue <= QCAP; skipped branches are only enumerated.
                // per-chunk state -- declared BEFORE runBlock so its worker threads can check the caps
                // MID-BLOCK (stop the block the instant a cap is hit, then advance the chunk).
                std::chrono::steady_clock::time_point ct0;
                long long banked0 = 0, results0 = 0, rej0 = 0;   // per-band baselines: saved / total covers / rejected
                std::atomic<bool> chunkHalt{ false };          // set the moment a cap is hit -> stops the block early
                const char* chunkWhy = "range-done";
                auto runBlock = [&](const std::vector<Match>& cover, char triv) {   // process one block's subtree on all threads
                    if (diveMode) {   // FAST: explore this block's subtree with parallel backtracking dives from its cover
                        std::vector<std::thread> dp;
                        for (int t = 0; t < nThreads; t++) {
                            workers[t].sh = &sh;
                            dp.emplace_back([&, t]() {
                                RepWorker& w = workers[t];
                                std::random_device rd; std::mt19937 rng(rd() ^ ((unsigned)(t + 1) * 0x9E3779B9u));
                                long long budget = diveBudget;                 // per-thread node budget for this block
                                w.clearState(); w.commitOrbit(cover); w.diveGen(rng, budget, &chunkHalt);
                            });
                        }
                        for (auto& th : dp) th.join();
                        return;
                    }
                    { std::lock_guard<std::mutex> lk(qmtx); queue.clear(); queue.push_back({ cover, triv }); }
                    active.store(0);
                    std::vector<std::thread> bp;
                    for (int t = 0; t < nThreads; t++) {
                        workers[t].sh = &sh;
                        bp.emplace_back([&, t]() {
                            RepWorker& w = workers[t];
                            for (;;) {
                                if (g_stop.load(std::memory_order_relaxed) || chunkHalt.load(std::memory_order_relaxed)) break;
                                std::pair<std::vector<Match>, char> task; bool have = false;
                                { std::unique_lock<std::mutex> lk(qmtx); if (!queue.empty()) { task = std::move(queue.back()); queue.pop_back(); active.fetch_add(1); have = true; } else if (active.load() == 0) break; }
                                if (!have) { std::this_thread::yield(); continue; }
                                w.clearState(); w.commitOrbit(task.first);
                                std::vector<std::vector<Match>> kids; std::vector<char> kt; w.splitNode(task.second != 0, kids, kt);
                                bool pushed = false;
                                { std::unique_lock<std::mutex> lk(qmtx); if (queue.size() < QCAP) { for (size_t i = 0; i < kids.size(); i++) queue.push_back({ std::move(kids[i]), kt[i] }); pushed = true; } }
                                if (!pushed) for (size_t i = 0; i < kids.size(); i++) { w.clearState(); w.commitOrbit(kids[i]); w.coverGen(kt[i] != 0); }
                                g_reps_done.fetch_add(1, std::memory_order_relaxed);
                                active.fetch_sub(1);
                                // (no mid-block cut: each block runs to its END; caps are checked AFTER each full block)
                            }
                        });
                    }
                    for (auto& th : bp) th.join();
                };
                long long visited = 0, curChunk = -1, chunkBlocks = 0;
                bool doneAll = false;
                auto closeChunk = [&]() {
                    if (curChunk < 0) return;
                    long long cs = (long long)rangeStart + curChunk * chunkStep, ce = cs + chunkStep;
                    if (ce > (long long)rangeEnd) ce = (long long)rangeEnd;
                    double csec = std::chrono::duration<double>(std::chrono::steady_clock::now() - ct0).count();
                    if (m_bPrint) { printf("[K18-REP]  type %-14s BLOCKS [%lld, %lld): +%lld new, %lld results, %lld duplicates, %lld blocks, %.0fs %s\n", tstr, cs, ce, (long long)g_banked.load() - banked0, g_emits.load() - results0, g_dupCovers.load() - rej0, chunkBlocks, csec, chunkWhy); fflush(stdout); }
                };
                RepWorker dw; dw.sh = &sh;                     // single worker for the enumeration DFS
                std::function<void(const std::vector<Match>&, bool, int)> dfs =
                  [&](const std::vector<Match>& cover, bool triv, int depth) {
                    if (doneAll) return;
                    if (depth == shardLevel) {                 // a level-L branch (index = visited)
                        long long i = visited++;
                        if (i < (long long)rangeStart) return;
                        if (i >= (long long)rangeEnd) { doneAll = true; return; }
                        if (!g_locTargets.empty()) {           // LOCATE MODE: match cover vs target(s) up to C(sigma0); skip runBlock
                            for (size_t s = 0; s < g_locTargets.size(); s++) {
                                if (coverMatchesTarget(cover, g_locTargets[s])) {
                                    printf("[LOCATE] target #%zu FOUND at block i=%lld  (cover=%zu factors)\n", s + 1, i, cover.size()); fflush(stdout);
                                    doneAll = true; return;
                                }
                            }
                            if ((i % 200000) == 0) { printf("[LOCATE] scanned to block %lld ...\n", i); fflush(stdout); }
                            return;                            // not a match -> keep enumerating, skip runBlock
                        }
                        long long ch = (i - (long long)rangeStart) / chunkStep;
                        if (ch != curChunk) { closeChunk(); curChunk = ch; ct0 = std::chrono::steady_clock::now(); banked0 = (long long)g_banked.load(); results0 = g_emits.load(); rej0 = g_dupCovers.load(); chunkBlocks = 0; chunkHalt.store(false); chunkWhy = "range-done"; }
                        if (!chunkHalt.load(std::memory_order_relaxed)) {   // each systematic block runs to its END (no mid-block cut)
                            long long estB0 = g_banked.load(); g_blockCovers.store(0);   // per-block estimate counters
                            runBlock(cover, triv ? 1 : 0);
                            chunkBlocks++;
                            if (g_estFile && !diveMode)   // FULLY-scanned block: log "<blockIndex> <rawCovers> <newDistinct>"
                                { fprintf(g_estFile, "%lld %lld %lld\n", i, g_blockCovers.load(), (long long)g_banked.load() - estB0); fflush(g_estFile); }
                            // ADVANCE (step): the chunk SCANS full blocks (each runs to its END -- no mid-block cut),
                            // checking caps AFTER each completed block: advance on chunkTimeout sec / chunkResultCap new
                            // classes / chunkMaxBlocks blocks; else continue to the NEXT block. A chunk that scans ALL
                            // its blocks closes "range-done" => a +0 range-done chunk is genuinely barren (fully scanned).
                            if (chunkTimeout > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - ct0).count() >= chunkTimeout) { chunkWhy = "TIMED-OUT"; chunkHalt.store(true); }
                            else if (chunkResultCap > 0 && (long long)g_banked.load() - banked0 >= chunkResultCap) { chunkWhy = "RESULT-CAP"; chunkHalt.store(true); }
                            else if (chunkMaxBlocks > 0 && chunkBlocks >= chunkMaxBlocks) { chunkWhy = "BLOCK-CAP"; chunkHalt.store(true); }
                        }
                        return;                                // capped chunk: keep enumerating, do not process
                    }
                    dw.clearState(); dw.commitOrbit(cover);
                    std::vector<std::vector<Match>> kids; std::vector<char> kt; dw.splitNode(triv, kids, kt);
                    for (size_t k = 0; k < kids.size() && !doneAll; k++) dfs(kids[k], kt[k] != 0, depth + 1);
                };
                if (m_bPrint) { printf("[K18-REP]  type %-14s BLOCK SWEEP [%zu, %zu) step %lld, advance-when: %.0fs | %lld new | %lld blocks (0=off), engine: %s%s -> run...\n", tstr, rangeStart, rangeEnd, chunkStep, chunkTimeout, chunkResultCap, chunkMaxBlocks, diveMode ? "fast dives" : "systematic", g_target > 0 ? ", LOOP to target" : ""); fflush(stdout); }
                // ONE pass over the range; if a harvest target is set (REP_ORDER=order:N) LOOP the range,
                // re-diving with fresh randomness each pass (global canonKey dedups), until the target is
                // banked or the run is killed. No target -> a single pass (density map).
                if (!diveMode && std::getenv("REP_ESTFILE")) {   // per-block estimate log (fresh per run; step in the name)
                    char epath[256]; snprintf(epath, sizeof(epath), "k%d_%d_step%lld_estimate.txt", N, tIdx, chunkStep);
                    g_estFile = fopen(epath, "w");
                    if (m_bPrint) { printf("[K18-REP]  type %-14s ESTIMATE LOG -> %s (%s)\n", tstr, epath, g_estFile ? "fresh" : "OPEN FAILED"); fflush(stdout); }
                }
                int sweepPass = 0;
                do {
                    if (m_bPrint && ++sweepPass > 1) { printf("[K18-REP]  type %-14s SWEEP PASS %d (cum %zu distinct) -> re-dive the range...\n", tstr, sweepPass, g_harvest.size()); fflush(stdout); }
                    visited = 0; curChunk = -1; doneAll = false; chunkBlocks = 0; chunkHalt.store(false); chunkWhy = "range-done";
                    { RepWorker seed; seed.sh = &sh; for (auto& tk : reps) { if (doneAll) break; std::vector<Match> orb; if (seed.buildAndValidateOrbit(tk.m0, orb)) dfs(orb, rawShard, 1); } }   // rawShard -> trivial descent (skip per-node group dedup)
                    closeChunk();                              // final chunk of this pass
                } while (g_target > 0 && !g_stop.load(std::memory_order_relaxed));
                if (g_estFile) { fclose(g_estFile); g_estFile = nullptr; }   // close the per-block estimate log
                shardHi = 0;                                   // processing done here -> the post work-queue is a no-op
            } else if (rangeMode) {
                long long visited = 0;                        // running index of level-L branches (DFS order)
                bool doneRange = false;
                RepWorker dw; dw.sh = &sh;                     // one reusable worker (re-committed at every node)
                // [PATHCANON streaming] canonicalize each block's path inline (no storage) so the range can be
                // huge: dedup canonV_fast keys into `pcSet`, print the running distinct count every 100k blocks.
                const bool pcStream = std::getenv("REP_PATHCANON") != nullptr;
                std::map<std::string, long long> pcRep; long long pcSeen = 0;   // canonical starter -> FIRST (representative) block index
                std::set<long long> classBlocks;              // REP_CLASSBLOCKS: log's +class block indices to validate
                long long cbRep = 0, cbDup = 0;               // class-blocks that ARE / are NOT the first occurrence of their starter
                if (const char* cbf = std::getenv("REP_CLASSBLOCKS")) { FILE* f = fopen(cbf, "r"); if (f) { long long b; while (fscanf(f, "%lld", &b) == 1) classBlocks.insert(b); fclose(f); } }
                // REP_DUMPREPS=<file>: dump the actual 3-orbit COVER of every representative (first-occurrence) box, so the
                // completion pass can seed each directly WITHOUT re-streaming/re-walking the tree. Format = the DUMPFIRST
                // format ("u v u v" per factor line), '#' on its own line separating one representative's cover from the next.
                FILE* repFile = nullptr; long long repDumped = 0;
                if (const char* rf = std::getenv("REP_DUMPREPS")) { repFile = fopen(rf, "w"); printf("[PATHCANON] REP_DUMPREPS: representative covers -> %s (%s)\n", rf, repFile ? "open" : "OPEN FAILED"); fflush(stdout); }
                std::function<void(const std::vector<Match>&, bool, int)> dfs =
                  [&](const std::vector<Match>& cover, bool triv, int depth) {
                    if (doneRange) return;
                    if (depth == shardLevel) {                 // a level-L branch
                        long long i = visited++;
                        if (i >= (long long)rangeStart && i < (long long)rangeEnd) {
                            if (pcStream) {
                                uint8_t mn[NEDGES]; canonV_fast(cover, sh.alpha, mn);
                                std::string k((const char*)mn, NEDGES);
                                bool firstOcc = (pcRep.find(k) == pcRep.end());
                                if (classBlocks.count(i)) {      // validate: is this +class block the first occurrence of its starter?
                                    if (firstOcc) cbRep++;
                                    else { cbDup++; if (cbDup <= 12) { printf("[PATHCANON] class-block %lld is a DUP: starter first appeared at block %lld\n", i, pcRep[k]); fflush(stdout); } }
                                }
                                if (firstOcc) {
                                    pcRep[k] = i;                // first occurrence = representative block
                                    if (repFile) { for (const Match& f : cover) { for (int u = 0; u < N; u++) if (u < f[u]) fprintf(repFile, "%d %d ", u, (int)f[u]); fprintf(repFile, "\n"); } fprintf(repFile, "#\n"); repDumped++; }
                                }
                                pcSeen++;
                                if ((pcSeen % 100000) == 0) { printf("[PATHCANON] seen %lld blocks -> %zu distinct starters (results) | reps saved=%lld (%.1fs)\n", pcSeen, pcRep.size(), repDumped, elapsed()); if (repFile) fflush(repFile); fflush(stdout); }
                            } else { shardFront.push_back(cover); shardTriv.push_back(triv ? 1 : 0); }
                        }
                        if (i + 1 >= (long long)rangeEnd) doneRange = true;   // reached end -> stop the whole DFS
                        return;
                    }
                    dw.clearState(); dw.commitOrbit(cover);
                    std::vector<std::vector<Match>> kids; std::vector<char> kt; dw.splitNode(triv, kids, kt);
                    for (size_t k = 0; k < kids.size() && !doneRange; k++) dfs(kids[k], kt[k] != 0, depth + 1);
                };
                { RepWorker seed; seed.sh = &sh; for (auto& tk : reps) { if (doneRange) break; std::vector<Match> orb; if (seed.buildAndValidateOrbit(tk.m0, orb)) dfs(orb, rawShard, 1); } }   // rawShard -> trivial descent (skip per-node group dedup)
                if (pcStream) {
                    long long mx = 0, le2850 = 0, le4299 = 0, gt4299 = 0;
                    for (auto& pr : pcRep) { long long b = pr.second; if (b > mx) mx = b; if (b <= 2850) le2850++; if (b <= 4299) le4299++; else gt4299++; }
                    printf("[PATHCANON] FINAL blocks [%zu,%zu) = %lld: distinct starters=%zu | max-rep-block=%lld | rep<=2850=%lld  rep<=4299(swept)=%lld  rep>4299(UNSWEPT)=%lld\n",
                           rangeStart, rangeEnd, pcSeen, pcRep.size(), mx, le2850, le4299, gt4299); fflush(stdout);
                    if (!classBlocks.empty()) printf("[PATHCANON] CLASS-BLOCK CHECK: %zu log +class blocks | first-occurrence(representative)=%lld | DUPLICATE(inconsistent)=%lld\n", classBlocks.size(), cbRep, cbDup); fflush(stdout);
                    if (repFile) { fclose(repFile); printf("[PATHCANON] REP_DUMPREPS: saved %lld representative covers (= distinct starters) to the seed file\n", repDumped); fflush(stdout); }
                    // dump the UNSWEPT representative blocks (rep > 4299) -- these need completion to prove no new class
                    FILE* uf = fopen("pathcanon_unswept.txt", "w");
                    if (uf) { for (auto& pr : pcRep) if (pr.second > 4299) fprintf(uf, "%lld\n", pr.second); fclose(uf); }
                    shardFront.clear(); shardHi = 0;
                }
                shardHi = shardFront.size();                   // shardFront holds exactly the block [start,end)
                if (m_bPrint) { xprintf("[K18-REP]  type %-14s SHARD level %d: built range [%zu, %zu) = %zu branches (%.1fs) -> run (work-queue, all threads)...\n", tstr, shardLevel, rangeStart, rangeEnd, shardFront.size(), elapsed()); fflush(stdout); }
                if (m_bPrint && rangeEnd > rangeStart) { std::string ts(tstr); while (!ts.empty() && ts.back() == ' ') ts.pop_back(); printf("  blocks %zu-%zu, type %s\n", rangeStart, rangeEnd - 1, ts.c_str()); fflush(stdout); }
                // [PATHCANON] canonicalize each block's PATH with canonV_fast: report how many DISTINCT
                // C(sigma0)-canonical level-L prefixes the range holds, and how many blocks are SELF-canonical
                // (their path IS the C(sigma0)-minimum = the canonical representative). Then skip completion.
                if (std::getenv("REP_PATHCANON")) {
                    Perm rho = buildRhoStd(sh.alpha);
                    std::set<std::string> distinct; long long selfc = 0;
                    for (size_t bi = 0; bi < shardFront.size(); bi++) {
                        const std::vector<Match>& cov = shardFront[bi]; int NF = (int)cov.size();
                        int e2f[NEDGES]; for (int e = 0; e < NEDGES; e++) e2f[e] = -1;
                        for (int f = 0; f < NF; f++) { Match F = applyAlpha(cov[f], rho.data()); for (int u = 0; u < N; u++) { int v = F[u]; if (u < v) e2f[eidT[u][v]] = f; } }
                        uint8_t own[NEDGES], inv[N]; for (int i = 0; i < N; i++) inv[i] = (uint8_t)i; serVPrefix(e2f, inv, N, own);
                        uint8_t mn[NEDGES]; canonV_fast(cov, sh.alpha, mn);
                        if (memcmp(own, mn, NEDGES) == 0) selfc++;
                        distinct.insert(std::string((const char*)mn, NEDGES));
                    }
                    printf("[PATHCANON] blocks [%zu,%zu) = %zu paths: distinct C(sigma0)-canonical=%zu, self-canonical blocks=%lld\n",
                           rangeStart, rangeEnd, shardFront.size(), distinct.size(), selfc); fflush(stdout);
                    shardFront.clear(); shardTriv.clear(); shardHi = 0;   // skip the work-queue completion
                }
            } else {
                // COUNT the whole level: parallel level-synchronous build, print per-level branch count.
                { RepWorker seed; seed.sh = &sh; for (auto& tk : reps) { std::vector<Match> orb; if (seed.buildAndValidateOrbit(tk.m0, orb)) { shardFront.push_back(std::move(orb)); shardTriv.push_back(rawShard ? 1 : 0); } } }   // rawShard -> trivial count (skip per-node group dedup)
                if (m_bPrint) { xprintf("[K18-REP]  type %-14s LEVEL 1 branches %zu (%.1fs)\n", tstr, shardFront.size(), elapsed()); fflush(stdout); }
                for (int lvl = 2; lvl <= shardLevel && !shardFront.empty(); lvl++) {
                    if (lvl == shardLevel) {
                        // FINAL level: COUNT children per parent and SUM -- do NOT store the level, so a
                        // multi-million level-L count cannot blow memory. Parallel over the parents.
                        std::atomic<long long> cnt{ 0 };
                        std::atomic<size_t> pi{ 0 };
                        std::vector<std::thread> bp;
                        int nb = (shardFront.size() >= 2) ? nThreads : 1;
                        for (int t = 0; t < nb; t++) bp.emplace_back([&]() {
                            RepWorker w; w.sh = &sh;
                            for (;;) { size_t i = pi.fetch_add(1); if (i >= shardFront.size()) break; std::vector<std::vector<Match>> kids; std::vector<char> kt; w.clearState(); w.commitOrbit(shardFront[i]); w.splitNode(shardTriv[i] != 0, kids, kt); cnt.fetch_add((long long)kids.size(), std::memory_order_relaxed); }
                        });
                        for (auto& th : bp) th.join();
                        if (m_bPrint) { xprintf("[K18-REP]  type %-14s LEVEL %d branches %lld (%.1fs, counted -- not stored)\n", tstr, lvl, cnt.load(), elapsed()); fflush(stdout); }
                        break;
                    }
                    // intermediate level: build and store (parents for the next level)
                    std::vector<std::vector<std::vector<Match>>> pf(shardFront.size());   // per-parent children (deterministic regroup)
                    std::vector<std::vector<char>> pt(shardFront.size());
                    bool par = (shardFront.size() >= 2);
                    if (par) {
                        std::atomic<size_t> pi{ 0 };
                        std::vector<std::thread> bp;
                        for (int t = 0; t < nThreads; t++) bp.emplace_back([&]() {
                            RepWorker w; w.sh = &sh;
                            for (;;) { size_t i = pi.fetch_add(1); if (i >= shardFront.size()) break; w.clearState(); w.commitOrbit(shardFront[i]); w.splitNode(shardTriv[i] != 0, pf[i], pt[i]); }
                        });
                        for (auto& th : bp) th.join();
                    } else {
                        RepWorker col; col.sh = &sh;
                        for (size_t i = 0; i < shardFront.size(); i++) { col.clearState(); col.commitOrbit(shardFront[i]); col.splitNode(shardTriv[i] != 0, pf[i], pt[i]); }
                    }
                    std::vector<std::vector<Match>> nf; std::vector<char> nt;
                    for (size_t i = 0; i < pf.size(); i++) for (size_t k = 0; k < pf[i].size(); k++) { nf.push_back(std::move(pf[i][k])); nt.push_back(pt[i][k]); }
                    shardFront.swap(nf); shardTriv.swap(nt);
                    if (m_bPrint) { xprintf("[K18-REP]  type %-14s LEVEL %d branches %zu (%.1fs%s)\n", tstr, lvl, shardFront.size(), elapsed(), par ? ", parallel" : ""); fflush(stdout); }
                }
                shardHi = 0;                                  // count only -> process nothing (the LEVEL lines are the counts)
            }
            // BLOCK THREADING: seed the SHARED WORK QUEUE with this block's branches and run the
            // SAME load-balanced work-queue as the full over-cap path -- so a single block's subtree
            // is expanded across ALL threads (via splitNode fan-out), not stuck on one core.
            // (In chunked-sweep mode shardHi==0, so this loop + pool below are a no-op.)
            for (size_t i = 0; i < shardHi; i++) queue.push_back({ std::move(shardFront[i]), shardTriv[i] });
            g_reps_total.store((long long)queue.size());
            g_reps_done.store(0);
            for (int t = 0; t < nThreads; t++) {
                workers[t].sh = &sh;
                pool.emplace_back([&, t]() {
                    RepWorker& w = workers[t];
                    for (;;) {
                        if (g_stop.load(std::memory_order_relaxed)) break;   // harvest target reached
                        std::pair<std::vector<Match>, char> task; bool have = false;
                        {
                            std::unique_lock<std::mutex> lk(qmtx);
                            if (!queue.empty()) { task = std::move(queue.back()); queue.pop_back(); active.fetch_add(1); have = true; }
                            else if (active.load() == 0) break;     // no work and nobody producing -> done
                        }
                        if (!have) { std::this_thread::yield(); continue; }
                        w.clearState(); w.commitOrbit(task.first);
                        std::vector<std::vector<Match>> kids; std::vector<char> kidsTriv; w.splitNode(task.second != 0, kids, kidsTriv);
                        bool pushed = false;
                        {
                            std::unique_lock<std::mutex> lk(qmtx);
                            if (queue.size() < QCAP) { for (size_t i = 0; i < kids.size(); i++) queue.push_back({ std::move(kids[i]), kidsTriv[i] }); pushed = true; }
                        }
                        if (pushed) g_reps_total.fetch_add((long long)kids.size(), std::memory_order_relaxed);
                        else for (size_t i = 0; i < kids.size(); i++) { w.clearState(); w.commitOrbit(kids[i]); w.coverGen(kidsTriv[i] != 0); }  // queue full -> drain locally (bounds memory)
                        g_reps_done.fetch_add(1, std::memory_order_relaxed);
                        active.fetch_sub(1);
                    }
                });
            }
        } else if (sh.Calpha.empty()) {
            // OVER-CAP: a few root reps (order-2 2^9 has 5) would idle most cores with a per-rep
            // fan-out. Use a SHARED WORK QUEUE of partial covers: each worker pops one, expands
            // it ONE deduped level (splitNode -> children + leaf emits), and pushes the children
            // back. This spreads the per-node stabilizer work (shallow AND deep) across all
            // threads with load balancing. LIFO (push/pop back) keeps it DFS-like so the queue
            // stays bounded. g_reps_total/done track tasks-created vs tasks-completed.
            const size_t QCAP = 200000;                   // queue cap (~60MB); over it, workers drain subtrees locally
            { RepWorker seed; seed.sh = &sh; for (auto& tk : reps) { std::vector<Match> orb; if (seed.buildAndValidateOrbit(tk.m0, orb)) queue.push_back({ std::move(orb), (char)0 }); } }  // root tasks: not yet trivial
            g_reps_total.store((long long)queue.size());
            for (int t = 0; t < nThreads; t++) {
                workers[t].sh = &sh;
                pool.emplace_back([&, t]() {
                    RepWorker& w = workers[t];
                    for (;;) {
                        if (g_stop.load(std::memory_order_relaxed)) break;   // harvest target reached
                        std::pair<std::vector<Match>, char> task; bool have = false;
                        {
                            std::unique_lock<std::mutex> lk(qmtx);
                            if (!queue.empty()) { task = std::move(queue.back()); queue.pop_back(); active.fetch_add(1); have = true; }
                            else if (active.load() == 0) break;     // no work and nobody producing -> done
                        }
                        if (!have) { std::this_thread::yield(); continue; }
                        w.clearState(); w.commitOrbit(task.first);
                        std::vector<std::vector<Match>> kids; std::vector<char> kidsTriv; w.splitNode(task.second != 0, kids, kidsTriv);
                        bool pushed = false;
                        {
                            std::unique_lock<std::mutex> lk(qmtx);
                            if (queue.size() < QCAP) { for (size_t i = 0; i < kids.size(); i++) queue.push_back({ std::move(kids[i]), kidsTriv[i] }); pushed = true; }
                        }
                        if (pushed) g_reps_total.fetch_add((long long)kids.size(), std::memory_order_relaxed);
                        else for (size_t i = 0; i < kids.size(); i++) { w.clearState(); w.commitOrbit(kids[i]); w.coverGen(kidsTriv[i] != 0); }  // queue full -> drain this subtree locally (bounds memory)
                        g_reps_done.fetch_add(1, std::memory_order_relaxed);
                        active.fetch_sub(1);
                    }
                });
            }
        } else {
            // ENUMERABLE: many root reps already -> simple per-rep fan-out via an atomic index.
            //
            // [SHARD] REP_LEVEL / REP_RANGE APPLY HERE TOO. They used to be read only inside the
            // over-cap branches above, which this path never enters, so on a type whose C(alpha)
            // was small enough to enumerate -- K20 3^6.1^2, |C| = 1,049,760 -- a COUNT request was
            // silently ignored and ran as an unbounded sweep instead.
            // (On k18 REP_RANGE exits at startup -- the block driver supersedes it -- so only the
            // REP_LEVEL count below is reachable here. The code is kept identical across the four
            // engines on purpose.)
            //
            // The root reps ARE the level-1 partition of this path: collectTasks forces the anchor
            // edge and the C(alpha)-orbit collapse above keeps one representative per orbit, so they
            // are complete, disjoint and index-stable. A slice therefore covers exactly what the
            // full run covers over that slice -- no rework, no gap -- and every subtree is searched
            // exactly as it would be with no REP_RANGE at all.
            //
            // Deeper levels (REP_LEVEL > 1): the level-L frontier is built by splitNode, which
            // needs sh.rootBSGS; setup builds it on this path for REP_LEVEL > 1 (the same calls
            // block threading uses), so the two over-cap shard modes apply here too, with the
            // root reps as level 1 -- COUNT (no REP_RANGE: expand level by level, print the
            // counts, search nothing) and RANGE (lazy in-order DFS to depth L keeping only the
            // branches in [start,end), then the block-threading pool below). Level-L indices
            // are DFS order over (rep index, child order), so slices compose exactly, as on the
            // over-cap path.
            size_t lo = 0, hi = reps.size();
            const bool deepShard = (shardLevel > 1);
            if (shardLevel > 0 && m_bPrint) {
                xprintf("[K18-REP]  type %-14s LEVEL 1 branches %zu (root reps -- the partition on the enumerable path; the SETUP lines above are where the time went)\n", tstr, reps.size());
                fflush(stdout);
            }
            if (deepShard) {
                auto tS = std::chrono::steady_clock::now();
                auto elapsed = [&]{ return std::chrono::duration<double>(std::chrono::steady_clock::now() - tS).count(); };
                lo = hi = 0;                                  // the root reps are not run as units here
                if (rangeMode) {
                    long long visited = 0;                    // running index of level-L branches (DFS order)
                    bool doneRange = false;
                    RepWorker dw; dw.sh = &sh;                // one reusable worker (re-committed at every node)
                    std::function<void(const std::vector<Match>&, bool, int)> dfs =
                      [&](const std::vector<Match>& cover, bool triv, int depth) {
                        if (doneRange) return;
                        if (depth == shardLevel) {            // a level-L branch
                            long long i = visited++;
                            if (i >= (long long)rangeStart && i < (long long)rangeEnd) { shardFront.push_back(cover); shardTriv.push_back(triv ? 1 : 0); }
                            if (i + 1 >= (long long)rangeEnd) doneRange = true;   // reached end -> stop the whole DFS
                            return;
                        }
                        dw.clearState(); dw.commitOrbit(cover);
                        std::vector<std::vector<Match>> kids; std::vector<char> kt; dw.splitNode(triv, kids, kt);
                        for (size_t k = 0; k < kids.size() && !doneRange; k++) dfs(kids[k], kt[k] != 0, depth + 1);
                    };
                    { RepWorker seed; seed.sh = &sh; for (auto& tk : reps) { if (doneRange) break; std::vector<Match> orb; if (seed.buildAndValidateOrbit(tk.m0, orb)) dfs(orb, rawShard, 1); } }   // rawShard -> trivial descent (skip per-node group dedup)
                    shardHi = shardFront.size();              // shardFront holds exactly the block [start,end)
                    if (chunkStep > 0 && m_bPrint) printf("[K18-REP]  type %-14s REP_RANGE chunked form ignored here -- the slice runs PLAINLY, every branch to completion\n", tstr);
                    if (m_bPrint) { xprintf("[K18-REP]  type %-14s SHARD level %d: built range [%zu, %zu) = %zu branches (%.1fs) -> run (work-queue, all threads)...\n", tstr, shardLevel, rangeStart, rangeEnd, shardFront.size(), elapsed()); fflush(stdout); }
                    if (m_bPrint && rangeEnd > rangeStart) { std::string ts(tstr); while (!ts.empty() && ts.back() == ' ') ts.pop_back(); printf("  blocks %zu-%zu, type %s\n", rangeStart, rangeEnd - 1, ts.c_str()); fflush(stdout); }
                } else {
                    // COUNT the whole level: parallel level-synchronous build, print per-level branch count.
                    { RepWorker seed; seed.sh = &sh; for (auto& tk : reps) { std::vector<Match> orb; if (seed.buildAndValidateOrbit(tk.m0, orb)) { shardFront.push_back(std::move(orb)); shardTriv.push_back(rawShard ? 1 : 0); } } }   // rawShard -> trivial count (skip per-node group dedup)
                    for (int lvl = 2; lvl <= shardLevel && !shardFront.empty(); lvl++) {
                        if (lvl == shardLevel) {
                            // FINAL level: COUNT children per parent and SUM -- do NOT store the level, so a
                            // multi-million level-L count cannot blow memory. Parallel over the parents.
                            std::atomic<long long> cnt{ 0 };
                            std::atomic<size_t> pi{ 0 };
                            std::vector<std::thread> bp;
                            int nb = (shardFront.size() >= 2) ? nThreads : 1;
                            for (int t = 0; t < nb; t++) bp.emplace_back([&]() {
                                RepWorker w; w.sh = &sh;
                                for (;;) { size_t i = pi.fetch_add(1); if (i >= shardFront.size()) break; std::vector<std::vector<Match>> kids; std::vector<char> kt; w.clearState(); w.commitOrbit(shardFront[i]); w.splitNode(shardTriv[i] != 0, kids, kt); cnt.fetch_add((long long)kids.size(), std::memory_order_relaxed); }
                            });
                            for (auto& th : bp) th.join();
                            if (m_bPrint) { xprintf("[K18-REP]  type %-14s LEVEL %d branches %lld (%.1fs, counted -- not stored)\n", tstr, lvl, cnt.load(), elapsed()); fflush(stdout); }
                            break;
                        }
                        // intermediate level: build and store (parents for the next level)
                        std::vector<std::vector<std::vector<Match>>> pf(shardFront.size());   // per-parent children (deterministic regroup)
                        std::vector<std::vector<char>> pt(shardFront.size());
                        bool par = (shardFront.size() >= 2);
                        if (par) {
                            std::atomic<size_t> pi{ 0 };
                            std::vector<std::thread> bp;
                            for (int t = 0; t < nThreads; t++) bp.emplace_back([&]() {
                                RepWorker w; w.sh = &sh;
                                for (;;) { size_t i = pi.fetch_add(1); if (i >= shardFront.size()) break; w.clearState(); w.commitOrbit(shardFront[i]); w.splitNode(shardTriv[i] != 0, pf[i], pt[i]); }
                            });
                            for (auto& th : bp) th.join();
                        } else {
                            RepWorker col; col.sh = &sh;
                            for (size_t i = 0; i < shardFront.size(); i++) { col.clearState(); col.commitOrbit(shardFront[i]); col.splitNode(shardTriv[i] != 0, pf[i], pt[i]); }
                        }
                        std::vector<std::vector<Match>> nf; std::vector<char> nt;
                        for (size_t i = 0; i < pf.size(); i++) for (size_t k = 0; k < pf[i].size(); k++) { nf.push_back(std::move(pf[i][k])); nt.push_back(pt[i][k]); }
                        shardFront.swap(nf); shardTriv.swap(nt);
                        if (m_bPrint) { xprintf("[K18-REP]  type %-14s LEVEL %d branches %zu (%.1fs%s)\n", tstr, lvl, shardFront.size(), elapsed(), par ? ", parallel" : ""); fflush(stdout); }
                    }
                    shardHi = 0;                              // count only -> process nothing (the LEVEL lines are the counts)
                    if (m_bPrint) { printf("[K18-REP]  type %-14s COUNT only (REP_LEVEL set, REP_RANGE unset) -- nothing searched\n", tstr); fflush(stdout); }
                }
            } else if (rangeMode) {
                if (chunkStep > 0 && m_bPrint) printf("[K18-REP]  type %-14s REP_RANGE chunked form ignored here -- the slice runs PLAINLY, every branch to completion\n", tstr);
                lo = (rangeStart < hi) ? rangeStart : hi;
                if (rangeEnd < hi) hi = rangeEnd;
                if (hi < lo) hi = lo;
                if (m_bPrint) { xprintf("[K18-REP]  type %-14s SHARD range [%zu, %zu) of %zu root reps -> run...\n", tstr, lo, hi, reps.size()); fflush(stdout); }
                if (m_bPrint && hi > lo) { std::string ts(tstr); while (!ts.empty() && ts.back() == ' ') ts.pop_back(); printf("  blocks %zu-%zu of 0-%zu, type %s\n", lo, hi - 1, reps.size() - 1, ts.c_str()); fflush(stdout); }
            } else if (shardLevel > 0) {
                hi = lo;                                  // COUNT mode: the LEVEL line is the answer; search nothing
                if (m_bPrint) { printf("[K18-REP]  type %-14s COUNT only (REP_LEVEL set, REP_RANGE unset) -- nothing searched\n", tstr); fflush(stdout); }
            }
            next.store(lo);
            g_reps_total.store((long long)(hi - lo));
            if (rangeMode && deepShard && shardHi > 0) {
                // [BLOCK THREADING] the K18 arrangement. Per-rep fan-out gives a slice of k reps
                // exactly k busy threads -- ONE, for a single block -- so instead seed the shared
                // work queue with this slice's root orbits and let every worker pop a partial
                // cover, expand it ONE deduped level (splitNode), and push the children back.
                // Load-balanced, LIFO so it stays DFS-like and the queue stays bounded.
                //
                // NOTE ON NODE COUNTS: this route breaks symmetry with the root BSGS
                // (setwiseStab + Schreier generators) where the per-rep route uses the explicit
                // centralizer elements, so it walks a slightly different tree -- same classes,
                // more nodes (K14 order 3: 15,173 against 10,975). A block's node count under
                // this route is therefore not comparable with one measured single-threaded.
                const size_t QCAP = 200000;                   // queue cap (~60MB); over it, workers drain subtrees locally
                const bool sampling = deepShard && sampleStride > 0;
                if (deepShard) {
                    for (size_t i = 0; i < shardHi; i++) {    // the level-L slice built above
                        if (sampling && (i % (size_t)sampleStride) != 0) continue;   // SAMPLE: keep one branch, skip stride-1
                        int b = -1;
                        if (sampling) { b = (int)sampleIdx.size(); sampleIdx.push_back(rangeStart + i); brNodes.push_back(0); brOpen.push_back(1); brCapped.push_back(0); }
                        queue.push_back({ std::move(shardFront[i]), shardTriv[i] }); qBranch.push_back(b); qOrb.emplace_back();
                    }
                } else { RepWorker seed; seed.sh = &sh; for (size_t i = lo; i < hi; i++) { std::vector<Match> orb; if (seed.buildAndValidateOrbit(reps[i].m0, orb)) { queue.push_back({ std::move(orb), (char)0 }); qBranch.push_back(-1); qOrb.emplace_back(); } } }
                g_reps_total.store((long long)queue.size());
                if (m_bPrint) { printf("[K18-REP]  type %-14s BLOCK THREADING: %zu block(s) seeded -> shared work queue, all %d threads\n", tstr, queue.size(), nThreads); fflush(stdout); }
                if (sampling && m_bPrint) { printf("[K18-REP]  type %-14s SAMPLE: every %lld-th branch of [%zu, %zu) = %zu branches, cap %lld nodes each (0 = none)\n", tstr, sampleStride, rangeStart, rangeStart + shardHi, sampleIdx.size(), sampleCap); fflush(stdout); }
                auto tPool = std::chrono::steady_clock::now();
                auto reportBranch = [&](int b) {              // SAMPLE: called under qmtx when a sampled branch has no task left
                    if (!m_bPrint) return;
                    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - tPool).count();
                    printf("[K18-REP]  type %-14s SAMPLE branch %zu: %lld nodes %s (%.0fs)\n", tstr, sampleIdx[b], brNodes[b], brCapped[b] ? "CAPPED" : "finished", sec); fflush(stdout);
                };
                for (int t = 0; t < nThreads; t++) {
                    workers[t].sh = &sh;
                    pool.emplace_back([&, t]() {
                        RepWorker& w = workers[t];
                        for (;;) {
                            if (g_stop.load(std::memory_order_relaxed)) break;   // harvest target reached
                            std::pair<std::vector<Match>, char> task; int tb = -1; bool have = false; OrbTask orb;
                            {
                                std::unique_lock<std::mutex> lk(qmtx);
                                if (!queue.empty()) { task = std::move(queue.back()); queue.pop_back(); tb = qBranch.back(); qBranch.pop_back(); orb = std::move(qOrb.back()); qOrb.pop_back(); active.fetch_add(1); have = true; }
                                else if (active.load() == 0) break;     // no work and nobody producing -> done
                            }
                            if (!have) { std::this_thread::yield(); continue; }
                            if (tb >= 0) { std::unique_lock<std::mutex> lk(qmtx); if (brCapped[tb]) { if (--brOpen[tb] == 0) reportBranch(tb); lk.unlock(); active.fetch_sub(1); continue; } }   // SAMPLE: a capped branch's leftovers are dropped
                            const long long n0 = w.nodes;
                            w.clearState(); w.commitOrbit(task.first);
                            std::vector<std::vector<Match>> kids; std::vector<char> kidsTriv; std::vector<OrbTask> kidsOrb;
                            if (g_precalc) {
                                if (!orb.root) {             // a seed: build its list once; every entry is admissible at the seed
                                    auto tL = std::chrono::steady_clock::now();
                                    orb.root = w.buildOrbList();
                                    if (m_bPrint) { std::lock_guard<std::mutex> lk(g_print_mtx); xprintf("[K18-REP]  type %-14s PRECALC: seed list %zu orbits (%.1fs)\n", tstr, orb.root->ent.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - tL).count()); fflush(stdout); }
                                }
                                w.splitNodeL(orb, kids, kidsTriv, kidsOrb);
                            } else w.splitNode(task.second != 0, kids, kidsTriv);
                            bool pushed = false;
                            {
                                std::unique_lock<std::mutex> lk(qmtx);
                                if (queue.size() < QCAP) { for (size_t i = 0; i < kids.size(); i++) { queue.push_back({ std::move(kids[i]), kidsTriv[i] }); qBranch.push_back(tb); if (g_precalc) qOrb.push_back(std::move(kidsOrb[i])); else qOrb.emplace_back(); } pushed = true; }
                            }
                            if (pushed) g_reps_total.fetch_add((long long)kids.size(), std::memory_order_relaxed);
                            else for (size_t i = 0; i < kids.size(); i++) { w.clearState(); w.commitOrbit(kids[i]); if (g_precalc) w.coverL(kidsOrb[i]); else w.coverGen(kidsTriv[i] != 0); }  // queue full -> drain locally (bounds memory)
                            if (tb >= 0) {                    // SAMPLE: charge this task's nodes to its branch; cap; report when the branch is exhausted
                                std::unique_lock<std::mutex> lk(qmtx);
                                brNodes[tb] += w.nodes - n0;
                                brOpen[tb] += (pushed ? (int)kids.size() : 0) - 1;
                                if (sampleCap > 0 && brNodes[tb] > sampleCap) brCapped[tb] = 1;
                                if (brOpen[tb] == 0) reportBranch(tb);
                            }
                            g_reps_done.fetch_add(1, std::memory_order_relaxed);
                            active.fetch_sub(1);
                        }
                    });
                }
            } else if (rangeMode && !deepShard && hi > lo) {
                // [SEQUENTIAL BRANCHES] One block at a time; inside a block every thread takes the
                // next unclaimed branch and walks it whole. No shared queue, no cap, nothing handed
                // back: a branch is claimed with one atomic increment and that is the only sharing.
                //
                // This replaces the work queue on the plain path because the queue could not do
                // both of its jobs at once. At its cap a worker had to drain a subtree alone with no
                // way to publish any of it, which is the thread starvation measured on block 0 --
                // 28 of 30 threads spinning for 15 hours. Raised above the cap, every single node
                // round-tripped through the queue mutex instead: 533 nodes/ms against 1,954. No cap
                // value avoids both, so the cap goes.
                //
                // What makes a fixed list safe here is that branches are near-uniform. On block 0
                // they fall in two adjacent powers of two, mean about 250k nodes (~1 s), largest
                // seen 386,661. So the list balances itself, and the only idle time is threads
                // running out of branches at the very end -- bounded by one branch, about a second,
                // against a block of roughly two hours.
                //
                // Blocks run ONE AT A TIME, never pooled together: a block's orbit list is its own
                // (~1.2M orbits, ~50 s to build), so holding several at once would multiply that
                // memory for no gain. It also means the tail of one block cannot be filled with
                // another block's work -- run blocks as separate processes if that matters.
                for (size_t bi = lo; bi < hi && !g_stop.load(std::memory_order_relaxed); bi++) {
                    RepWorker& seedW = workers[0];
                    seedW.sh = &sh;
                    std::vector<Match> orb;
                    if (!seedW.buildAndValidateOrbit(reps[bi].m0, orb)) continue;
                    seedW.clearState(); seedW.commitOrbit(orb);
                    // The branch list. Each entry owns its descriptor outright -- two shared_ptrs
                    // (the block's list, the parent's admissible set) and the index committed to
                    // reach it -- so reusing worker 0 below cannot invalidate any of them.
                    std::vector<std::vector<Match>> front; std::vector<char> frontTriv; std::vector<OrbTask> frontOrb;
                    OrbTask seedOrb;
                    if (g_precalc) {
                        auto tL = std::chrono::steady_clock::now();
                        seedOrb.root = seedW.buildOrbList();
                        if (m_bPrint) { xprintf("[K18-REP]  type %-14s PRECALC: seed list %zu orbits (%.1fs)\n", tstr, seedOrb.root->ent.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - tL).count()); fflush(stdout); }
                        seedW.splitNodeL(seedOrb, front, frontTriv, frontOrb);
                    } else seedW.splitNode(false, front, frontTriv);
                    g_reps_total.store((long long)front.size());
                    if (m_bPrint) { xprintf("[K18-REP]  type %-14s BLOCK %zu: %zu branches -> %d threads, next branch in sequence\n", tstr, bi, front.size(), nThreads); fflush(stdout); }
                    std::atomic<size_t> bidx{ 0 };
                    std::vector<std::thread> bpool;
                    for (int t = 0; t < nThreads; t++) {
                        workers[t].sh = &sh;
                        bpool.emplace_back([&, t]() {
                            RepWorker& w = workers[t];
                            for (;;) {
                                if (g_stop.load(std::memory_order_relaxed)) break;   // harvest target reached
                                const size_t i = bidx.fetch_add(1, std::memory_order_relaxed);
                                if (i >= front.size()) break;
                                w.clearState(); w.commitOrbit(front[i]);
                                if (g_precalc) w.coverL(frontOrb[i]); else w.coverGen(frontTriv[i] != 0);
                                g_reps_done.fetch_add(1, std::memory_order_relaxed);
                            }
                        });
                    }
                    for (auto& th : bpool) th.join();
                }
            } else {
                for (int t = 0; t < nThreads; t++) {
                    workers[t].sh = &sh;
                    pool.emplace_back([&, t, hi]() {
                        RepWorker& w = workers[t];
                        for (;;) { if (g_stop.load(std::memory_order_relaxed)) break; size_t i = next.fetch_add(1); if (i >= hi) break; w.runTask(reps[i]); g_reps_done.fetch_add(1, std::memory_order_relaxed); }
                    });
                }
            }
        }
        for (auto& th : pool) th.join();
        if (sampleStride > 0 && !sampleIdx.empty() && m_bPrint) {   // SAMPLE summary: the stride-scaled estimate of the slice
            long long fin = 0, capped = 0, sumFin = 0, sumAll = 0, mx = 0;
            for (size_t b = 0; b < sampleIdx.size(); b++) { sumAll += brNodes[b]; if (brNodes[b] > mx) mx = brNodes[b]; if (brCapped[b]) capped++; else { fin++; sumFin += brNodes[b]; } }
            printf("[K18-REP]  type %-14s SAMPLE summary: %zu branches, %lld finished, %lld capped; nodes finished %lld (mean %.0f, max %lld), all %lld\n",
                   tstr, sampleIdx.size(), fin, capped, sumFin, fin ? (double)sumFin / (double)fin : 0.0, mx, sumAll);
            printf("[K18-REP]  type %-14s SAMPLE estimate of the slice: stride %lld x %lld = %.3e nodes%s\n",
                   tstr, sampleStride, sumAll, (double)sampleStride * (double)sumAll, capped ? " (a LOWER BOUND: capped branches counted at the cap)" : "");
            fflush(stdout);
        }

        // 4. merge each worker's distinct classes into the global dedup
        for (auto& w : workers)
            for (auto& kv : w.autOf)
                if (gcanon.insert(kv.first).second) gautOf[kv.first] = kv.second;

        if (m_bPrint && diveMode && sh.Calpha.empty()) {
            printf("[K18-REP]  type %-14s FAST SEARCH: %lld random restarts, %zu new distinct classes (cum %zu)\n",
                   tstr, divesDone.load(), gcanon.size() - before, gcanon.size());
            fflush(stdout);
        }
        if (m_bPrint) {
            double tsec = std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
            long long typeNodes = 0; for (auto& w : workers) typeNodes += w.nodes;   // exact per-type cover-node count
            // The leg's own numbers, at CLASS level: results = saved + duplicates.  `cum` is gone --
            // it counted within one REP_ORDERS token while reading as run-wide.  The histograms cover
            // what was WRITTEN and what was REJECTED, so the rows' Saved histograms sum to the census.
            const long long dups  = g_crossDup.load(std::memory_order_relaxed) - dupBefore;
            const long long found = (long long)(gcanon.size() - before);
            // One ROW per leg.  The blank marker says the numbers are
            // this leg's own; the run totals it feeds are what the ~ and = rows report.  A leg that
            // was never entered is one row with `skipped` in Elapsed and the rest blank.
            g_runFound += found; g_runSaved += found - dups; g_runDups += dups; g_runNodes += typeNodes; g_runSec += tsec;
            if (g_tbl) {
                // Elapsed and Total saved(duplicates) are CUMULATIVE -- watching a live log, the
                // number wanted is the run's total so far, and this leg's own is one subtraction
                // from the row above. Nodes and its rate stay this leg's own, which is what the
                // column name says: they measure how fast THIS subset searched, not the run.
                std::string sym, typ;
                subsetCells(order, isV4, isE9, isS3, tstr, sym, typ);
                std::map<int, int> savedHist, dupHist;
                { std::lock_guard<std::mutex> lk(g_harvest_mtx); savedHist = g_runSavedAut; dupHist = g_runDupAut; }
                std::vector<std::string> cells;
                cells.push_back(sym);
                cells.push_back(typ);
                if (!found && !typeNodes) {
                    cells.push_back("skipped");
                } else {
                    cells.push_back(fmtElapsed(g_runSec));
                    cells.push_back(fmtNodes(typeNodes, tsec));
                    cells.push_back(fmtSavedDup(g_runSaved, g_runDups, savedHist, dupHist));
                }
                g_tbl->row(' ', cells);
            }
        }
    }

    g_sendResult = nullptr;
    // (End-of-run batch emission removed: every distinct class was already sent to the
    // result pipeline the moment it was first found -- exact global dedup in emit().)

    if (m_bPrint) {
        if (g_diagProbe > 0) {   // [DIAG] REP_DIAGPROBE: where the look-ahead would have fired
            printf("[DIAGPROBE] depth: nodes probed -> nodes with an unrealizable diagonal\n");
            for (int d = 0; d <= NM + 1; d++) if (g_diagProbes[d])
                printf("           %2d: %lld -> %lld (%.1f%%)\n", d, g_diagProbes[d], g_diagHits[d], 100.0 * g_diagHits[d] / g_diagProbes[d]);
            fflush(stdout);
        }
        bool harvested = (g_target > 0 && g_stop.load(std::memory_order_relaxed));
        // g_harvest, not gcanon: gcanon is merged from the TYPE loop's worker pool only, so the
        // F3COMPLETE block driver (its own worker vector, destroyed per block) never reached it and
        // this line printed 0. It used to print the preload count, which hid the gap.
        const size_t distinctAll = g_harvest.size() > gcanon.size() ? g_harvest.size() : gcanon.size();
        // No end-of-order summary line: what this run enumerated is stated by the table's title, the
        // running tallies are its ~ rows, and the totals are its = row and the closing "P1F-Census: done"
        // line. Harvest mode still speaks up, because
        // "stopped at a target" is not the same outcome as "finished".
        if (harvested)
            printf("[K18-REP] HARVESTED %zu classes (target %d reached -- NOT proven complete)\n", distinctAll, g_target);
        if (g_f17dump) {   // [DIAG] REP_F17DUMP final tally: total forced-17th nodes + the full sampled obstruction histogram
            std::lock_guard<std::mutex> lk(g_f17_mtx);
            std::string hist; for (auto& kv : g_f17Hist) { char t[48]; snprintf(t, sizeof(t), "%s%s:%lld", hist.empty() ? "" : "  ", kv.first.c_str(), kv.second); hist += t; }
            std::string ch; for (auto& kv : g_f17ClashHist) { char t[48]; snprintf(t, sizeof(t), "%s%d:%lld", ch.empty() ? "" : " ", kv.first, kv.second); ch += t; }
            printf("[F17] FINAL: forced-17th nodes reached=%lld (histogram sampled 1/%lld) | obstruction: %s | clash-count: %s\n", g_f17Total.load(), g_f17Sub, hist.c_str(), ch.c_str());
        }
        if (g_lvlStats) {   // [DIAG] REP_LEVELSTATS: per-level node counts + branching (reached[m+1]/reached[m]) = success/expansion rate per level
            printf("[LVL] per-level node counts (level = #committed factors) + branching to next level:\n");
            for (int m = 0; m < NM + 1; m++) { long long r = g_lvlReached[m].load(); if (r == 0) continue;
                long long rn = g_lvlReached[m + 1].load(); double br = r ? (double)rn / (double)r : 0.0;
                printf("[LVL]   level %2d: reached=%-14lld branching=%.3f%s\n", m, r, br, (m == NM - 1) ? "   <- forced last factor" : ""); }
        }
        if (g_probe1 > 0) {   // [DIAG] REP_PROBE1: how often the pairing invariant already fails
            long long s = g_probeSeen.load(), b = g_probeBad.load();
            printf("[PROBE1] level %d: nodes=%lld  violating U==sigma0(R): %lld  (%.2f%%)  -- what a look-ahead here could reject\n",
                   g_probe1, s, b, s ? 100.0 * (double)b / (double)s : 0.0);
            fflush(stdout);
        }
        if (g_pDump) {   // [DIAG] REP_PDUMP: realized row-pairing involutions
            std::lock_guard<std::mutex> lk(g_pdump_mtx);
            long long tot = 0; for (auto& kv : g_pSeen) tot += kv.second;
            printf("[PDUMP] realized row-pairing involutions: %zu distinct over %lld completions\n", g_pSeen.size(), tot);
            for (auto& kv : g_pSeen) {
                // render as fixed points + transposition pairs, which is how the stratum reads it
                std::string fx, tr; int seen[N] = { 0 };
                int pv[N]; for (int i = 0; i < N; i++) pv[i] = -1;
                { int x = 0, v = 0; const char* c = kv.first.c_str();
                  while (sscanf(c, "%d:%d,", &x, &v) == 2) { if (x > 0 && x < N) pv[x] = v; c = strchr(c, ','); if (!c) break; c++; } }
                char t[16];
                for (int x = 1; x < N; x++) { if (pv[x] < 0) continue;
                    if (pv[x] == x) { snprintf(t, sizeof(t), "%d ", x); fx += t; }
                    else if (!seen[x]) { seen[x] = seen[pv[x]] = 1; snprintf(t, sizeof(t), "(%d %d)", x, pv[x]); tr += t; } }
                printf("[PDUMP]   x%-6lld fixed{ %s}  %s\n", kv.second, fx.c_str(), tr.c_str());
            }
            fflush(stdout);
        }
        if (g_genStat) {   // [DIAG] REP_GENSTAT: how many candidate factors genM built vs kept, per level
            printf("[GEN] genM candidates built vs kept (level = #committed factors when the candidate was built):\n");
            long long tm = 0, tk = 0;
            for (int m = 0; m < NM + 2; m++) {
                long long made = g_genMade[m].load(), kept = g_genKept[m].load();
                if (made == 0) continue;
                tm += made; tk += kept;
                printf("[GEN]   level %2d: built=%-14lld kept=%-12lld waste=%.1f%%  (built/kept=%.2fx)\n",
                       m, made, kept, made ? 100.0 * (double)(made - kept) / (double)made : 0.0,
                       kept ? (double)made / (double)kept : 0.0);
            }
            printf("[GEN]   TOTAL  : built=%lld kept=%lld waste=%.1f%%  -- this is the ceiling on what any\n", tm, tk, tm ? 100.0 * (double)(tm - tk) / (double)tm : 0.0);
            printf("[GEN]            up-front constraint could remove from generation (it cannot save the kept ones).\n");
        }
        if (g_patStat) {   // [DIAG] REP_PATSTAT: sigma0 stay/swap pattern occurrence (S=stay, W=swap; commit order)
            std::lock_guard<std::mutex> lk(g_pat_mtx);
            printf("[PATSTAT] COMPLETION patterns (full 17-factor P1Fs emitted): %zu distinct\n", g_patEmit.size());
            for (auto& kv : g_patEmit) printf("[PATSTAT]   %s  count=%lld  |Aut|=%d\n", kv.first.c_str(), kv.second.first, kv.second.second / 2);
            long long f17tot = 0; for (auto& kv : g_patF17) f17tot += kv.second;
            printf("[PATSTAT] FORCED-17th patterns (16-factor near-complete partials reached): %zu distinct, %lld total\n", g_patF17.size(), f17tot);
            for (auto& kv : g_patF17) printf("[PATSTAT]   %s  count=%lld\n", kv.first.c_str(), kv.second);
            if (g_patLevel > 0) { long long mtot = 0; for (auto& kv : g_patMid) mtot += kv.second;
                printf("[PATSTAT] MID-LEVEL(%d) patterns (sampled 1/%lld): %zu distinct, %lld sampled\n", g_patLevel, g_patSub, g_patMid.size(), mtot);
                for (auto& kv : g_patMid) printf("[PATSTAT]   %s  count=%lld\n", kv.first.c_str(), kv.second); }
        }
        fflush(stdout);
    }
}
