// =============================================================================
// k20a2rep.cpp — automorphism-REPRESENTATIVE method for classifying K20 Perfect
// 1-Factorizations (P1Fs) by a given automorphism order. N=20 sibling of
// k16A2rep.cpp / k18a2rep.cpp (the engine is N-generic; this file differs only
// in N and names). K20 has no cyclic engine at all -- this rep method is its
// only solver path.
//
// Method: unseeded and complete. For ONE representative permutation alpha per
// cycle TYPE of the target order, it finds ALL alpha-invariant P1Fs by an exact
// cover of the 190 edges of K20 with alpha-invariant factor-orbits, then
// deduplicates across types with a labeling-independent canonical key. Fixed-
// point-free cycle types (which cyclic generators structurally miss) are
// included, so the classification is complete.
//
// The engine is validated on the smaller cases by the same code: K16 |Aut|>2 =
// {3:19,5:5,7:4,14:1,15:1}=30 (Wanless catalogue), K18 order-4 = 179, order-17
// -> 2. Known K20 anchors: order-19 -> 7 classes {19:3,57:1,171:2,342:1}
// (342 = AGL(1,19)); the order-4 parity theorem (MD/order4_parity_theorem.md)
// says K20 admits NO automorphism of order divisible by 4, so order-4 must
// return 0. Isomorph rejection is by ORDERLY GENERATION: at every cover level
// the candidate factor-orbits covering the lowest uncovered edge are
// deduplicated by the current centralizer stabilizer G, and each chosen orbit O
// recurses with its sub-stabilizer Stab_G(O). Sound for class counting because
// any g in G = Stab(partial cover) is a class-preserving bijection between
// sibling subtrees; combined with the shrinking stabilizer chain this collapses
// the dense high-centralizer types that a naive lex-leader prune cannot
// terminate.
//
// Two symmetry-reduction paths, by whether C(alpha) fits the enumeration cap:
//   * ENUMERABLE (|C(alpha)| <= cap): C(alpha) is materialized; cover() carries
//     the stabilizer as indices into it and dedups candidates directly.
//   * OVER-CAP (sparse / huge S_k on fixed points / every order-2 type): C(alpha)
//     is held only as a generating set + BSGS (cgtEngine.h). coverGen() recomputes
//     the EXACT setwise stabilizer Stab_C(P) of the current partial cover per node
//     (full-strength dedup at every depth), and splitNode() feeds a shared
//     work-queue so the few root reps still saturate all worker threads.
//
// Parallelism: enumerable types fan their independent first-orbit reps across
// `kThreads` workers via an atomic index; over-cap types use a shared work-queue
// of partial covers (each worker pops one, expands it one deduped level, pushes
// the children back). The per-type/global dedup is merged after the workers join.
//
// Integration rules honored here:
//   * The ONLY entry point is the private member K20A2::runRepresentativeMethod,
//     called internally from K20A2::runExhaustiveSearch. Everything else lives in
//     the anonymous namespace below, so nothing new has external linkage.
//   * Worker count comes from K20A2::kThreads.
//   * Every print is gated by K20A2::m_bPrint (forwarded as g_bprint).
// =============================================================================

#include "k20a2.h"
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
constexpr int N      = 20;             // players / graph vertices (K20)
constexpr int NM     = N - 1;          // 19 = factors in a 1-factorization
constexpr int NHALF  = N / 2;          // 10 = edges in one perfect matching (factor)
constexpr int NEDGES = N * (N - 1) / 2;// 190 = edges of K20
constexpr int INF    = 1 << 20;        // "no edge / not covered" sentinel (> any edge id)
constexpr uint32_t FULLMASK = (1u << N) - 1;   // bit v = vertex v (uint32 fits every sibling N)

typedef std::array<uint8_t, N> Match;  // a factor in adjacency form: m[u] = partner of u
typedef std::array<uint8_t, N> Perm;   // a vertex permutation: p[u] = image of u

// ---- edge-id tables (filled once by initEdges) ------------------------------
int     eidT[N][N];          // eidT[u][v] = canonical id (0..189) of edge {u,v}; symmetric
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

// two factors are "perfect" iff their union is a single Hamiltonian N-cycle
inline bool is_perfect(const Match& a, const Match& b) {
    int u = 0;               // walk the alternating a/b cycle from vertex 0 (see k18a2rep note):
    for (int i = 0; i < NHALF - 1; i++) {   // a short cycle can only close at hops 1..NHALF-1;
        u = a[u]; u = b[u];                 // surviving all of them forces the last hop back to 0,
        if (u == 0) return false;           // so the final hop + compare are unnecessary
    }
    return true;             // survived NHALF-1 checks => single Hamiltonian N-cycle
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
std::atomic<long long> g_emits{ 0 };  // total distinct classes found so far (across workers)
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

// [PRECALC] REP_PRECALC=1 -- PROTOTYPE. Enumerable
// block-threading pool only, trivial-stabilizer nodes only (REP_PRUNELEVEL=1 makes every node
// below a seed trivial). The sigma-orbits admissible below a seed are enumerated
// ONCE into a list; every node below inherits its parent's index list filtered by the orbit
// the parent committed, and its candidates are the entries through its anchor edge -- no genM.
//   rule 1: an ORBIT is one entry, base row (least member) first, the images after it;
//   rule 2: the sigma-fixed factor is an entry of size 1.
//
// [TUNE 2026-09-07, k20 ONLY -- the sibling rule is deliberately suspended for this pass, on the
// user's instruction: k14/k16/k18 keep the plain prototype until a winner is mirrored on purpose.]
// A VTune hotspots profile of this exact case (block 0, 8 threads, 632 s wall / 4,407 s CPU;
// scratch/prof_k20_triples_r001) put the cost here, and NOT where the spec's section 6.4 guessed:
//     orbCompat 44.7%   is_perfect (+ its array::operator[]) 9.1%   malloc 9.9%
//     splitNodeL self 9.1%   queue mutex 6.0%   commitOrbit+rollbackTo 4.2%
//     shared_ptr refcounts 3.6%   the per-child `chosen` copy 1.9%   genM 0.2%
// genM at 0.2% is the point: the lists did their job and the filter is now the program.
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
bool g_bprint = false;                // mirror of K20A2::m_bPrint (gate for ALL prints)
bool g_noClosurePrune = false;        // sibling of k18a2rep REP_NOPRUNE (always off here => closure ban always on)
int g_pruneMaxFactors = 0;            // [PORT] REP_PRUNELEVEL: skip per-node setwiseStab once >= this many factors committed (0 = prune everywhere)
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
FILE* g_estFile = nullptr;            // REP_ESTFILE: append "<blockIdx> <covers> <newDistinct>" per FULL block; null = off
std::function<void(const unsigned char*, int)> g_sendResult;  // forwards each globally-new class + its |Aut| to the
                                      //   NORMAL result pipeline the moment it is found (set per run)

// One periodic progress line (every 300s) shared by cover()/coverGen(): shows the current
// order + cycle type, how many of the type's representative subtrees are done (the best
// completion-trend signal), node throughput since the last line, and classes found so far.
inline void progressTick(long long& nodes_flush, std::atomic<long long>& g_nodesRef) {
    if ((nodes_flush & 0x3FF) != 0) return;   // flush every 1024 nodes/worker (the 300s gate paces the actual print)
    g_nodesRef.fetch_add(nodes_flush, std::memory_order_relaxed);
    nodes_flush = 0;
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
struct RepWorker {
    const RepShared* sh = nullptr;    // shared read-only data for the current type

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
    int lastAut = 0;                  // |Aut|*2 of the P1F most recently canonicalized

    std::set<std::string>      canon;     // distinct canonical keys found by THIS worker
    std::map<std::string, int> autOf;     // canonical key -> 2*|Aut| (this worker)
    std::vector<Match>* collectInto = nullptr;  // non-null => collect first-orbit tasks, don't recurse
    std::vector<Match> orbScratch;    // reusable scratch for genM's leaf buildAndValidateOrbit (avoids per-leaf alloc)

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
        // F-vs-chosen redundant when the closure prune is on (genM already guarantees it); see k18a2rep.
        if (g_noClosurePrune) for (const auto& F : orbit) for (const auto& C : chosen) if (!is_perfect(F, C)) return false;
        for (size_t x = 0; x < orbit.size(); x++) for (size_t y = x + 1; y < orbit.size(); y++) if (!is_perfect(orbit[x], orbit[y])) return false;
        return true;
    }

    void commitOrbit(const std::vector<Match>& orbit) {   // append the orbit's factors
        commitRows(orbit.data(), (int)orbit.size());
    }

    // [TUNE 2026-09-07] the same commit, from a plain row pointer. REP_PRECALC already holds an
    // orbit's rows contiguously in its OrbEnt, so the local recursion commits straight from there
    // instead of first copying them into a std::vector to satisfy commitOrbit's signature.
    void commitRows(const Match* rows, int n) {           // append n factors
        for (int i = 0; i < n; i++) {
            const Match& F = rows[i];
            int fidx = (int)chosen.size();
            chosen.push_back(F);
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
                    if (first || s < best) { best = s; first = false; bestCount = 1; bestAdj.clear(); for (auto& g2 : rel) for (int u2 = 0; u2 < N; u2++) bestAdj.push_back((char)g2[u2]); }
                    else if (s == best) bestCount++;
                }
            }
        }
        lastAut = bestCount;
        return bestAdj;   // adjacency form of the src-minimal frame
    }

    void emit() {
        g_blockCovers.fetch_add(1, std::memory_order_relaxed);   // raw complete-cover count for the per-block estimate log
        std::string key = canonKey(chosen);          // labeling-independent class key
        bool localNew = canon.insert(key).second;    // first time THIS worker sees the class
        if (localNew) {
            autOf[key] = lastAut;
            g_emits.fetch_add(1, std::memory_order_relaxed);  // (approx; merged-dedup is exact)
        }
        {
            std::lock_guard<std::mutex> lk(g_harvest_mtx);   // exact cross-worker tracking + capture-recapture
            // EVERY leaf detection counts (also re-finds): the f1/f2 spectrum feeds the
            // Chao1 richness estimate printed in the [rep] line.
            long long cdet = ++g_harvest[key];
            if (cdet == 1) {
                g_f1.fetch_add(1, std::memory_order_relaxed); g_banked.store((int)g_harvest.size(), std::memory_order_relaxed);
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
                    printf("\n[K20-REP] RAW search-frame cover, class #%zu (v4Row = partner of 0, v4 = partner of 1):\n", g_harvest.size());
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
            if (buildAndValidateOrbit(M, orbScratch)) collectInto->push_back(M);   // reusable member buffer (no per-leaf alloc)
            return;
        }
        unsigned long u; _BitScanForward(&u, freeMask);    // lowest still-unmatched vertex
        int nchosen = (int)chosen.size();
        uint8_t epu[NM];                           // epu[k]=path_end[k][u]: INVARIANT across the v-loop below
        for (int k = 0; k < nchosen; k++) epu[k] = path_end[k][u];   // (u fixed; closure ban keeps path_end[k][u] unwritten)
        uint32_t cand = freeMask & ~coveredBits[u] & (FULLMASK << (u + 1));  // free, unused edge, v > u
        if (edge_count < NHALF - 1) {              // Hamiltonian-with-chosen pre-check (closure ban)
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
        collectInto = &cands; genM(M, usedM, 1); collectInto = nullptr;
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
        // [PORT] REP_PRUNELEVEL: past this factor-depth, skip the exact-stabilizer orbit dedup --
        //     explore every orbit with an empty stab so descendants stay exhaustive; the global
        //     canonKey remains the dedup, so classes are unchanged. Same code as k14a2rep.
        if (g_pruneMaxFactors > 0 && (int)chosen.size() >= g_pruneMaxFactors) {
            std::vector<int> none;
            for (auto& R : reps) processM(R, none);
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
    void coverGen(bool parentTrivial) {
        nodes++; nodes_flush++;
        progressTick(nodes_flush, g_nodes);
        if (g_stop.load(std::memory_order_relaxed)) return;   // harvest target reached -> unwind the search

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
        collectInto = &cands; genM(M, usedM, 1); collectInto = nullptr;
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
        if (g_pruneMaxFactors > 0 && nchosen >= g_pruneMaxFactors) { for (auto& R : reps) processMGen(R, true); return; }   // [PORT] REP_PRUNELEVEL: skip setwiseStab past depth, explore all orbits (global canonKey dedups)

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
        std::vector<Match> cands;
        collectInto = &cands; genM(M, usedM, 1); collectInto = nullptr;
        if (cands.empty()) { fanTally(nchosen, 0, 0); return; }
        std::unordered_map<std::string, int> keyIdx; std::vector<std::string> keys; std::vector<Match> reps;
        for (auto& C : cands) { std::string k = orbitKey(C, sh->gElems); if (keyIdx.emplace(k, (int)reps.size()).second) { keys.push_back(std::move(k)); reps.push_back(C); } }
        fanTally(nchosen, cands.size(), reps.size());
        std::vector<int> repIdx;
        bool childTrivial;
        if (parentTrivial || (g_pruneMaxFactors > 0 && nchosen >= g_pruneMaxFactors)) {   // [PORT] trivial up-path OR REP_PRUNELEVEL past depth -> skip setwiseStab, explore all, stay trivial (same as k14/k18)
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
    void splitNodeL(const OrbTask& t, std::vector<std::vector<Match>>& outChildren, std::vector<char>& outTrivial, std::vector<OrbTask>& outTrip) {
        nodes++; nodes_flush++;
        progressTick(nodes_flush, g_nodes);
        if (g_stop.load(std::memory_order_relaxed)) return;
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
        for (uint32_t c : cand) {
            const OrbEnt& ce = E[c];
            std::vector<Match> child = chosen; for (int r = 0; r < ce.nrows; r++) child.push_back(ce.rows[r]);
            OrbTask ct; ct.root = t.root; ct.plist = mine; ct.committed = c;
            outChildren.push_back(std::move(child)); outTrivial.push_back((char)1); outTrip.push_back(std::move(ct));
        }
        fanTally((int)chosen.size(), cand.size(), cand.size());
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
            ncand++;
            const OrbEnt& ce = E[c];
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
// Private K20A2 entry point (declared in k20a2.h, called only from
// runExhaustiveSearch). Enumerates all K20 P1Fs with an automorphism of `order`,
// deduplicates them, forwards each distinct class to resultCallback, and prints a
// summary (only when m_bPrint). Worker count = kThreads.
// =============================================================================
// The table's LAST row.  It is emitted here, not at the end of
// runRepresentativeMethod, because that function runs once per REP_ORDERS token while the table
// spans the whole run -- printing a TOTAL per token would be six totals and no total.
// Every figure is the per-unit accumulation of the rows above, so the = row is exactly their sum;
// saved is cross-checked against g_emittedAll, which counts the same classes a different way.
void K20A2::finishRepTable() {
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

void K20A2::runRepresentativeMethod(int order, int target) {
    initEdges();
    // Centralizer-enumeration cap, overridable at runtime via env REP_SYMCAP (e.g. raise it
    // to pull a borderline dense type like order-3 3^5.1^5 (|C|=3.5M) onto the fast path).
    long long symCap = K20_REP_SYMCAP;
    if (const char* env = std::getenv("REP_SYMCAP")) symCap = atoll(env);
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
    // [PORT from k18a2rep / k14a2rep, 2026-09-06] REP_PRUNELEVEL=N: once >= N factors are committed, skip the
    // per-node setwiseStab/schreierDedup (explore every orbit, subtree marked trivial); the global canonKey
    // stays the exact dedup so the class set is unchanged, only the node count grows. Ported to k20 ONLY,
    // on the user's instruction -- k16 deliberately left without it (a sibling-code-rule deviation, not drift).
    // 2026-09-07: REP_PRECALC was then mirrored into k16, and that path wants every node below the seed
    // trivial, so it cannot lean on this flag there. It did not matter -- on the K16 order-3 oracle both
    // sides walked the same 1,349,431-node tree anyway, that block root's stabilizer being already trivial.
    // The omission still costs nothing and still stands: do not port this flag to k16 without asking.
    g_pruneMaxFactors = 0;
    if (const char* e = std::getenv("REP_PRUNELEVEL")) g_pruneMaxFactors = atoi(e);
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
    if (g_precalc && m_bPrint) xprintf("[K20-REP] REP_PRECALC: PROTOTYPE -- precalculated orbit lists below each seed; trivial-stabilizer nodes only (use REP_PRUNELEVEL=1)\n");
    std::set<int> onlyTypes;
    if (const char* env = std::getenv("REP_ONLYTYPES")) {
        for (const char* p = env; *p; ) { int v = atoi(p); if (v > 0) onlyTypes.insert(v); while (*p && *p != ',') p++; while (*p == ',') p++; }
    }

    g_bprint = m_bPrint;
    g_nodes.store(0); g_emits.store(0); g_last_print_nodes = 0;
    g_target = target; g_stop.store(false); g_harvest.clear(); g_banked.store(0); g_f1.store(0); g_f2.store(0);   // harvest mode (target>0 -> stop after `target` classes)
    // Live result emission: every globally-new class goes straight to the normal result
    // pipeline (mutex-serialized in emit()); a long or killed run keeps everything sent.
    g_sendResult = [this](const unsigned char* s, int aut) { if (resultCallback) resultCallback(cbClass, s, aut, 1, 2); };   // |Aut| rides the callback's free r4 slot
    g_t0 = std::chrono::steady_clock::now();
    g_last_print = g_t0;
    g_order = order;

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
        if (noSkip) printf("[K20-REP] REP_NOSKIP: odd-prime parity-theorem skip DISABLED (theorem-verification mode)\n");
        if (estProbes > 0) printf("[K20-REP] REP_ESTIMATE=%d : Knuth tree-size probes per rep -- NO SEARCH, output is an ESTIMATE, not a result\n", estProbes);

        if (target > 0) printf("[K20-REP] HARVEST MODE: stop after %d distinct classes (COMPLETE only if %d == true count, else a LOWER BOUND)\n", target, target);
        if (rawShard) printf("[K20-REP] REP_RAWSHARD: branches = raw potential candidates (symmetry NOT reduced) -- LEVEL counts are a superset, mid-range seek is cheap; distinct-set correct via global dedup\n");
        {
            int nf = 0; for (int i = 0; i < 4; i++) if (m_v4Row[i] && m_v4Val[i]) nf++;
            if (nf) {
                printf("[K20-REP] V4 SHARD FILTERS (search frame):");
                for (int i = 0; i < 4; i++) if (m_v4Row[i] && m_v4Val[i]) printf("  0-%d factor pairs 1-%d;", m_v4Row[i], m_v4Val[i]);
                printf("  (SHARD run -- NOT a complete order)\n");
                // Consistency validation: each condition below makes the shard PROVABLY
                // EMPTY (the run would burn its full time and return 0 classes) -- say so.
                for (int i = 0; i < 4; i++) {
                    const int r = m_v4Row[i], v = m_v4Val[i];
                    if (!r && !v) continue;
                    if (!r || !v || r < 2 || r > N - 1 || v < 2 || v > N - 1)
                        printf("[K20-REP] *** V4 FILTER %d INVALID: row=%d val=%d (both must be set, in 2..%d) -- shard is EMPTY\n", i + 1, r, v, N - 1);
                    else if (r == v)
                        printf("[K20-REP] *** V4 FILTER %d INVALID: row == val == %d (no vertex partners both 0 and 1) -- shard is EMPTY\n", i + 1, r);
                    for (int j = i + 1; j < 4; j++) {
                        if (!m_v4Row[j] || !m_v4Val[j]) continue;
                        if (m_v4Row[j] == r && m_v4Val[j] != v)
                            printf("[K20-REP] *** V4 FILTERS %d,%d CONTRADICT: row %d demands val %d AND %d -- shard is EMPTY\n", i + 1, j + 1, r, v, m_v4Val[j]);
                        if (m_v4Val[j] == v && m_v4Row[j] != r)
                            printf("[K20-REP] *** V4 FILTERS %d,%d CONTRADICT: val %d demanded in rows %d AND %d (edge {1,%d} is in ONE factor) -- shard is EMPTY\n", i + 1, j + 1, v, r, m_v4Row[j], v);
                    }
                }
            }
        }
        if (g_printRawLeft > 0) printf("[K20-REP] REP_PRINTRAW=%d : raw search-frame cover of the first %d classes will be printed\n", g_printRawLeft, g_printRawLeft);
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
                if (m_bPrint) { printf("[K20-REP]  type %-14s (type %d/%d) SKIPPED: INTERNAL closure size %zu != %u -- report this\n", tstr, tIdx, g_numTypes, sh.gElems.size(), isE9 ? 8u : 5u); fflush(stdout); }
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
            xprintf("[K20-REP]  type %-14s SETUP %-14s %8.1fs  n=%lld\n", tstr, stage, s, n);
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
            if (!okC) { if (m_bPrint) { printf("[K20-REP]  type %-14s SKIPPED: INTERNAL centralizer element does not commute -- report this\n", tstr); fflush(stdout); } continue; }
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
            if (rangeMode || shardLevel > 1) {
                if (isE9 || isS3) {
                    buildGeneratorsBlocks(gshapes[typeIdx].cls, sh.rootCgens);   // block-wreath generators
                    bool okG = true;   // paranoia: generators must commute with the whole group
                    for (const auto& g : sh.rootCgens) { for (const auto& e : sh.gElems) if (composePerm(g, e) != composePerm(e, g)) { okG = false; break; } if (!okG) break; }
                    if (!okG) { if (m_bPrint) { printf("[K20-REP]  type %-14s SKIPPED: INTERNAL centralizer generator does not commute -- report this\n", tstr); fflush(stdout); } continue; }
                } else
                buildGenerators(sh.alpha, sh.rootCgens);
                sh.rootBSGS = cgt::buildBSGS<N>(sh.rootCgens);
                setupTick("BSGS+edgeStab", (long long)sh.rootCgens.size());
            }
        } else {
            if (isV4) {   // cannot happen (V4 |C| << cap); the CGT fallback below is cyclic-only
                if (m_bPrint) { printf("[K20-REP]  type %-14s SKIPPED: V4 centralizer over cap (unsupported path)\n", tstr); fflush(stdout); }
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
                if (!okG) { if (m_bPrint) { printf("[K20-REP]  type %-14s SKIPPED: INTERNAL centralizer generator does not commute -- report this\n", tstr); fflush(stdout); } continue; }
            } else
            buildGenerators(sh.alpha, sh.rootCgens);              // seed for exact Stab_C(P) in coverGen
            sh.rootBSGS = cgt::buildBSGS<N>(sh.rootCgens);        // built once per type, reused per node
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
        // [SUBSET] is gone: column set A puts its two facts in the Symmetry and Type columns, and the block driver puts them in the table title
        size_t before = gcanon.size();   // classes known before this type
        const long long dupBefore = g_crossDup.load(std::memory_order_relaxed);   // duplicates before this type
        { std::lock_guard<std::mutex> lk(g_harvest_mtx); g_savedAut.clear(); g_crossDupAut.clear(); }   // per-leg |Aut| histograms: what this leg wrote, and what it rejected
        auto ts = std::chrono::steady_clock::now();
        g_legT0 = ts; g_legNodes0 = g_nodes.load(std::memory_order_relaxed); g_legDup0 = g_crossDup.load(std::memory_order_relaxed);   // the ~ row counts the open leg too
        // (no per-type start line: a type prints one line, when it finishes)
        // [ESTIMATE MODE] Knuth probes instead of the search: per representative run
        // estProbes random descents and report mean estimated subtree node counts.
        // ETA for a real run = (est TOTAL) / (measured nodes/s of a real run).
        if (estProbes > 0) {
            if (sh.Calpha.empty()) {
                if (m_bPrint) { printf("[K20-REP]  type %-14s ESTIMATE SKIPPED: over-cap type (estimator supports the enumerable path only)\n", tstr); fflush(stdout); }
                continue;
            }
            if (reps.empty()) {   // root-collapsed type: exhausted during collection -> 0 nodes, 0 classes
                if (m_bPrint) { printf("[K20-REP]  type %-14s ESTIMATE: reps 0 -> est cover-nodes TOTAL ~ 0 (type exhausts at the root)\n", tstr); fflush(stdout); }
                continue;
            }
            std::vector<double> estMean(reps.size(), 0.0), estMax(reps.size(), 0.0);
            std::atomic<size_t> nextR{ 0 };
            std::vector<std::thread> epool;
            for (int t = 0; t < nThreads; t++) epool.emplace_back([&]() {
                RepWorker w; w.sh = &sh;
                for (;;) {
                    size_t r = nextR.fetch_add(1);
                    if (r >= reps.size()) break;
                    std::vector<Match> orbit;
                    w.clearState();
                    if (!w.buildAndValidateOrbit(reps[r].m0, orbit)) continue;   // rep contributes 0
                    double sum = 0, mx = 0;
                    for (int p = 0; p < estProbes; p++) {
                        w.clearState(); w.commitOrbit(orbit);
                        std::mt19937 rng((unsigned)(1000003u * (unsigned)r + 7919u * (unsigned)p + 12345u));
                        double e = w.probeCover(reps[r].stab, rng);
                        sum += e; if (e > mx) mx = e;
                    }
                    estMean[r] = sum / estProbes; estMax[r] = mx;
                }
            });
            for (auto& th : epool) th.join();
            double total = 0, worstMean = 0; size_t worstR = 0;
            for (size_t r = 0; r < reps.size(); r++) { total += estMean[r]; if (estMean[r] > worstMean) { worstMean = estMean[r]; worstR = r; } }
            if (m_bPrint) {
                printf("[K20-REP]  type %-14s ESTIMATE: reps %zu, probes/rep %d -> est cover-nodes TOTAL ~ %.3g (largest rep #%zu ~ %.3g mean, %.3g max-probe)\n",
                       tstr, reps.size(), estProbes, total, worstR, worstMean, estMax[worstR]);
                printf("[K20-REP]  type %-14s ESTIMATE: ETA = above TOTAL / (nodes-per-second of a real run on the same machine/threads)\n", tstr);
                // PER-REP, because on the enumerable path a rep IS a block (REP_RANGE=r:r+1),
                // and a census is scheduled from the block-size DISTRIBUTION, not the total:
                // it says how uneven the work is, which blocks are cheap enough to finish
                // first, and whether any are near-empty. max-probe next to the mean is the
                // spread WITHIN a rep -- if it towers over the mean, raise the probe count
                // before believing that row.
                const double meanAll = reps.empty() ? 0.0 : total / (double)reps.size();
                printf("[K20-REP]  type %-14s ESTIMATE per block: idx  est-nodes  xMean  max-probe\n", tstr);
                for (size_t r = 0; r < reps.size(); r++)
                    printf("[K20-REP]    block %3zu  %10.3g  %6.2fx  %10.3g\n",
                           r, estMean[r], meanAll > 0 ? estMean[r] / meanAll : 0.0, estMax[r]);
                fflush(stdout);
            }
            continue;
        }
        // publish current-type progress state for the periodic [rep] line
        g_typeIdx = tIdx; g_reps_done.store(0); g_reps_total.store(0);
        snprintf(g_typeStr, sizeof(g_typeStr), "%s", tstr);
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
            if (reps.empty()) { if (m_bPrint) { printf("[K20-REP]  type %-14s FAST SEARCH: no root reps -> 0\n", tstr); fflush(stdout); } continue; }
            if (m_bPrint) { printf("[K20-REP]  type %-14s FAST SEARCH (backtracking dives, budget %lld nodes/restart) x %d threads -> run until target/kill...\n", tstr, diveBudget, nThreads); fflush(stdout); }
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
                long long banked0 = 0;
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
                    if (m_bPrint) { printf("[K20-REP]  type %-14s CHUNK [%lld, %lld): +%lld classes, %lld blocks, %.0fs %s\n", tstr, cs, ce, (long long)g_banked.load() - banked0, chunkBlocks, csec, chunkWhy); fflush(stdout); }
                };
                RepWorker dw; dw.sh = &sh;                     // single worker for the enumeration DFS
                std::function<void(const std::vector<Match>&, bool, int)> dfs =
                  [&](const std::vector<Match>& cover, bool triv, int depth) {
                    if (doneAll) return;
                    if (depth == shardLevel) {                 // a level-L branch (index = visited)
                        long long i = visited++;
                        if (i < (long long)rangeStart) return;
                        if (i >= (long long)rangeEnd) { doneAll = true; return; }
                        long long ch = (i - (long long)rangeStart) / chunkStep;
                        if (ch != curChunk) { closeChunk(); curChunk = ch; ct0 = std::chrono::steady_clock::now(); banked0 = (long long)g_banked.load(); chunkBlocks = 0; chunkHalt.store(false); chunkWhy = "range-done"; }
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
                if (m_bPrint) { printf("[K20-REP]  type %-14s CHUNKED SWEEP [%zu, %zu) step %lld, caps: %.0fs/chunk, %lld results/chunk, %lld blocks/chunk, engine: %s%s -> run...\n", tstr, rangeStart, rangeEnd, chunkStep, chunkTimeout, chunkResultCap, chunkMaxBlocks, diveMode ? "fast dives" : "systematic", g_target > 0 ? ", LOOP to target" : ""); fflush(stdout); }
                // ONE pass over the range; if a harvest target is set (REP_ORDER=order:N) LOOP the range,
                // re-diving with fresh randomness each pass (global canonKey dedups), until the target is
                // banked or the run is killed. No target -> a single pass (density map).
                if (!diveMode && std::getenv("REP_ESTFILE")) {   // per-block estimate log (fresh per run; step in the name)
                    char epath[256]; snprintf(epath, sizeof(epath), "k%d_%d_step%lld_estimate.txt", N, tIdx, chunkStep);
                    g_estFile = fopen(epath, "w");
                    if (m_bPrint) { printf("[K20-REP]  type %-14s ESTIMATE LOG -> %s (%s)\n", tstr, epath, g_estFile ? "fresh" : "OPEN FAILED"); fflush(stdout); }
                }
                int sweepPass = 0;
                do {
                    if (m_bPrint && ++sweepPass > 1) { printf("[K20-REP]  type %-14s SWEEP PASS %d (cum %zu distinct) -> re-dive the range...\n", tstr, sweepPass, g_harvest.size()); fflush(stdout); }
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
                std::function<void(const std::vector<Match>&, bool, int)> dfs =
                  [&](const std::vector<Match>& cover, bool triv, int depth) {
                    if (doneRange) return;
                    if (depth == shardLevel) {                 // a level-L branch
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
                shardHi = shardFront.size();                   // shardFront holds exactly the block [start,end)
                if (m_bPrint) { xprintf("[K20-REP]  type %-14s SHARD level %d: built range [%zu, %zu) = %zu branches (%.1fs) -> run (work-queue, all threads)...\n", tstr, shardLevel, rangeStart, rangeEnd, shardFront.size(), elapsed()); fflush(stdout); }
                if (m_bPrint && rangeEnd > rangeStart) { std::string ts(tstr); while (!ts.empty() && ts.back() == ' ') ts.pop_back(); printf("  blocks %zu-%zu, type %s\n", rangeStart, rangeEnd - 1, ts.c_str()); fflush(stdout); }
            } else {
                // COUNT the whole level: parallel level-synchronous build, print per-level branch count.
                { RepWorker seed; seed.sh = &sh; for (auto& tk : reps) { std::vector<Match> orb; if (seed.buildAndValidateOrbit(tk.m0, orb)) { shardFront.push_back(std::move(orb)); shardTriv.push_back(rawShard ? 1 : 0); } } }   // rawShard -> trivial count (skip per-node group dedup)
                if (m_bPrint) { xprintf("[K20-REP]  type %-14s LEVEL 1 branches %zu (%.1fs)\n", tstr, shardFront.size(), elapsed()); fflush(stdout); }
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
                        if (m_bPrint) { xprintf("[K20-REP]  type %-14s LEVEL %d branches %lld (%.1fs, counted -- not stored)\n", tstr, lvl, cnt.load(), elapsed()); fflush(stdout); }
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
                    if (m_bPrint) { xprintf("[K20-REP]  type %-14s LEVEL %d branches %zu (%.1fs%s)\n", tstr, lvl, shardFront.size(), elapsed(), par ? ", parallel" : ""); fflush(stdout); }
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
                xprintf("[K20-REP]  type %-14s LEVEL 1 branches %zu (root reps -- the partition on the enumerable path; the SETUP lines above are where the time went)\n", tstr, reps.size());
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
                    if (chunkStep > 0 && m_bPrint) printf("[K20-REP]  type %-14s REP_RANGE chunked form ignored here -- the slice runs PLAINLY, every branch to completion\n", tstr);
                    if (m_bPrint) { xprintf("[K20-REP]  type %-14s SHARD level %d: built range [%zu, %zu) = %zu branches (%.1fs) -> run (work-queue, all threads)...\n", tstr, shardLevel, rangeStart, rangeEnd, shardFront.size(), elapsed()); fflush(stdout); }
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
                            if (m_bPrint) { xprintf("[K20-REP]  type %-14s LEVEL %d branches %lld (%.1fs, counted -- not stored)\n", tstr, lvl, cnt.load(), elapsed()); fflush(stdout); }
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
                        if (m_bPrint) { xprintf("[K20-REP]  type %-14s LEVEL %d branches %zu (%.1fs%s)\n", tstr, lvl, shardFront.size(), elapsed(), par ? ", parallel" : ""); fflush(stdout); }
                    }
                    shardHi = 0;                              // count only -> process nothing (the LEVEL lines are the counts)
                    if (m_bPrint) { printf("[K20-REP]  type %-14s COUNT only (REP_LEVEL set, REP_RANGE unset) -- nothing searched\n", tstr); fflush(stdout); }
                }
            } else if (rangeMode) {
                if (chunkStep > 0 && m_bPrint) printf("[K20-REP]  type %-14s REP_RANGE chunked form ignored here -- the slice runs PLAINLY, every branch to completion\n", tstr);
                lo = (rangeStart < hi) ? rangeStart : hi;
                if (rangeEnd < hi) hi = rangeEnd;
                if (hi < lo) hi = lo;
                if (m_bPrint) { xprintf("[K20-REP]  type %-14s SHARD range [%zu, %zu) of %zu root reps -> run...\n", tstr, lo, hi, reps.size()); fflush(stdout); }
                if (m_bPrint && hi > lo) { std::string ts(tstr); while (!ts.empty() && ts.back() == ' ') ts.pop_back(); printf("  blocks %zu-%zu of 0-%zu, type %s\n", lo, hi - 1, reps.size() - 1, ts.c_str()); fflush(stdout); }
            } else if (shardLevel > 0) {
                hi = lo;                                  // COUNT mode: the LEVEL line is the answer; search nothing
                if (m_bPrint) { printf("[K20-REP]  type %-14s COUNT only (REP_LEVEL set, REP_RANGE unset) -- nothing searched\n", tstr); fflush(stdout); }
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
                if (m_bPrint) { printf("[K20-REP]  type %-14s BLOCK THREADING: %zu block(s) seeded -> shared work queue, all %d threads\n", tstr, queue.size(), nThreads); fflush(stdout); }
                if (sampling && m_bPrint) { printf("[K20-REP]  type %-14s SAMPLE: every %lld-th branch of [%zu, %zu) = %zu branches, cap %lld nodes each (0 = none)\n", tstr, sampleStride, rangeStart, rangeStart + shardHi, sampleIdx.size(), sampleCap); fflush(stdout); }
                auto tPool = std::chrono::steady_clock::now();
                auto reportBranch = [&](int b) {              // SAMPLE: called under qmtx when a sampled branch has no task left
                    if (!m_bPrint) return;
                    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - tPool).count();
                    printf("[K20-REP]  type %-14s SAMPLE branch %zu: %lld nodes %s (%.0fs)\n", tstr, sampleIdx[b], brNodes[b], brCapped[b] ? "CAPPED" : "finished", sec); fflush(stdout);
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
                                    if (m_bPrint) { std::lock_guard<std::mutex> lk(g_print_mtx); xprintf("[K20-REP]  type %-14s PRECALC: seed list %zu orbits (%.1fs)\n", tstr, orb.root->ent.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - tL).count()); fflush(stdout); }
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
                        if (m_bPrint) { xprintf("[K20-REP]  type %-14s PRECALC: seed list %zu orbits (%.1fs)\n", tstr, seedOrb.root->ent.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - tL).count()); fflush(stdout); }
                        seedW.splitNodeL(seedOrb, front, frontTriv, frontOrb);
                    } else seedW.splitNode(false, front, frontTriv);
                    g_reps_total.store((long long)front.size());
                    if (m_bPrint) { xprintf("[K20-REP]  type %-14s BLOCK %zu: %zu branches -> %d threads, next branch in sequence\n", tstr, bi, front.size(), nThreads); fflush(stdout); }
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
            printf("[K20-REP]  type %-14s SAMPLE summary: %zu branches, %lld finished, %lld capped; nodes finished %lld (mean %.0f, max %lld), all %lld\n",
                   tstr, sampleIdx.size(), fin, capped, sumFin, fin ? (double)sumFin / (double)fin : 0.0, mx, sumAll);
            printf("[K20-REP]  type %-14s SAMPLE estimate of the slice: stride %lld x %lld = %.3e nodes%s\n",
                   tstr, sampleStride, sumAll, (double)sampleStride * (double)sumAll, capped ? " (a LOWER BOUND: capped branches counted at the cap)" : "");
            fflush(stdout);
        }

        // 4. merge each worker's distinct classes into the global dedup
        for (auto& w : workers)
            for (auto& kv : w.autOf)
                if (gcanon.insert(kv.first).second) gautOf[kv.first] = kv.second;

        if (m_bPrint && diveMode && sh.Calpha.empty()) {
            printf("[K20-REP]  type %-14s FAST SEARCH: %lld random restarts, %zu new distinct classes (cum %zu)\n",
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
        // No end-of-order summary line: what this run enumerated is stated by the table's title,
        // the per-leg rows carry each leg's own numbers, and the run totals are the = TOTAL row.
        // The order-level histogram and DISTINCT-CLASSES line said all three again, in a third
        // vocabulary: the table is the only content here.
        // Why a type was skipped stays in the case's ReadMe.md.
        bool harvested = (g_target > 0 && g_stop.load(std::memory_order_relaxed));
        if (harvested)   // "stopped at a target" is not the same outcome as "finished", so it still speaks up
            printf("[K20-REP] HARVESTED %zu classes (target %d reached -- NOT proven complete)\n", gcanon.size(), g_target);
        fflush(stdout);
    }
}
