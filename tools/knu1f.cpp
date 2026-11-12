// knu1f (formerly k18u1f): exhaustive search for uniform 1-factorizations of K_N of one cycle type.
//
//   knu1f N type [rows] [threads]      e.g.  knu1f 18 4.14
//
// A factorization is uniform of a type when every pair of its factors has that type. Any two
// factors can be relabelled to one standard pair for the type, so rows 1 and 2 are fixed and
// nothing else is assumed: there is no canonicity test and no isomorph rejection.
//
// Every other row is identified by the partner of vertex 0 in it. The program lists, per
// partner, every matching compatible with rows 1 and 2. It then looks for `rows`-2 of them with
// distinct partners, pairwise compatible (same type). With rows < N-1 it counts partial
// factorizations: each set of rows containing the two fixed rows is counted once.
//
// A full search (rows = N-1) only has to decide whether anything exists, so it uses the symmetry
// of the fixed pair: the row through one chosen edge at vertex 0 is tried once per orbit.
// Each isomorphism class found goes to K<N>_U1F_<type>.txt with its |Aut|. It also keeps the partials
// with the most rows the search reached, up to `hold` classes, in K<N>_U1F_deepest_<type>.txt. The
// search abandons a branch once any row is out of candidates, so that size is not the maximum.
//
//   knu1f N type max [threads]
//
// finds the largest number of rows any partial factorization of the type reaches, and writes one
// record per isomorphism class at that size, with |Aut|, to K<N>_U1F_max_<type>.txt. Orbits of
// candidates are numbered, and the root of a job only combines with candidates of its own or a
// later orbit: a symmetry takes any partial to one whose smallest orbit is met at the root.
//
//   knu1f N type max threads K first last      one shard: the K-row pass for root jobs [first, last)
//   knu1f N type extend classes.txt [threads]  for each class, the ways to add one more row
//   knu1f N type continue threads f1 [f2 ...]  complete each class (e.g. U1F_SMALLKEEP starters,
//                                               .txt or .part) to N-1 rows; see runContinue
//   knu1f merge out.txt in1.txt [in2.txt ...]   union of shard files, as a single run writes it
//
// Every pass line and RESULT line ends with "rows 3..K: n3 ... nK", the number of branches that
// held 3, 4, ... K rows (nK = found). On Windows the program says "k task exit ok" at the end, as
// Tt4 does.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
// Spoken report at the end of a run, as in Tt4 (Windows speech, volume 30). Nothing elsewhere.
#define RPC_NO_WINDOWS_H
#define NOMINMAX
#include <sapi.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")
static void speak(const std::string& text) {
    if (FAILED(::CoInitialize(NULL))) return;
    ISpVoice* voice = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_PPV_ARGS(&voice)))) {
        std::wstring w(text.begin(), text.end());
        voice->SetVolume(30);
        voice->Speak(w.c_str(), 0, NULL);
        voice->Release();
    }
    ::CoUninitialize();
}
#else
static void speak(const std::string&) {}
#endif

#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
static inline int popcnt64(uint64_t x) { return (int)__popcnt64(x); }
static inline int ctz64(uint64_t x) { unsigned long i; _BitScanForward64(&i, x); return (int)i; }
#else
static inline int popcnt64(uint64_t x) { return __builtin_popcountll(x); }
static inline int ctz64(uint64_t x) { return __builtin_ctzll(x); }
#endif
// The bits of x at the positions set in m, packed to the low end (BMI2; build with AVX2/BMI2).
static inline uint64_t pext64(uint64_t x, uint64_t m) { return _pext_u64(x, m); }

using Match = std::array<int8_t, 32>;   // Match[v] = partner of v

static int N;
static uint32_t allVerts;
static uint32_t typeLens;               // bit c/2 set when cycle length c occurs in the type
static uint64_t typeKey;                // one 4-bit count per cycle length
static Match R1, R2;

// True when the union of a and b has exactly the required cycle type. A shared edge is a cycle
// of length 2, which no type contains, so edge-disjointness needs no separate test.
// Walk version: follows each cycle of the union.
static bool compatWalk(const Match& a, const Match& b) {
    uint64_t got = 0;
    uint32_t seen = 0;
    while (seen != allVerts) {
        int s = ctz64(~(uint64_t)seen), x = s, len = 0;
        do {
            seen |= (1u << x) | (1u << a[x]);
            x = b[a[x]];
            len += 2;
        } while (x != s);
        if (!((typeLens >> (len / 2)) & 1)) return false;
        got += 1ULL << (4 * (len / 2 - 1));
    }
    return got == typeKey;
}

// Power version, branch-free AVX2. p = b o a maps each cycle of length 2L of the union to two
// cycles of length L, and a shared edge to two fixed points. The cycle lengths of p, and so the
// type of the union, are fixed by the number of fixed points of p^d for every divisor d of the
// lcm of the L: fix(p^d) = sum of the cycle lengths 2L of the type with L | d. powDiv / powReq
// list those d (ascending) and the counts; filled in run() from the type.
static std::vector<int> powDiv, powReq;
static uint32_t laneMask;
static __m256i laneId;

// r[x] = t[i[x]] for 32 byte lanes, indices 0..31 (vpshufb works on 16-byte halves).
static inline __m256i lookup32(__m256i t, __m256i i) {
    const __m256i lo = _mm256_permute2x128_si256(t, t, 0x00), hi = _mm256_permute2x128_si256(t, t, 0x11);
    const __m256i sel = _mm256_slli_epi16(i, 3);             // bit 4 of each index -> bit 7
    return _mm256_blendv_epi8(_mm256_shuffle_epi8(lo, i), _mm256_shuffle_epi8(hi, i), sel);
}

static bool compatPow(const Match& a, const Match& b) {
    const __m256i A = _mm256_loadu_si256((const __m256i*)a.data());
    const __m256i B = _mm256_loadu_si256((const __m256i*)b.data());
    __m256i pw[9];                                          // pw[j] = p^(2^j)
    pw[0] = lookup32(B, A);
    int have = 1;
    for (size_t k = 0; k < powDiv.size(); k++) {
        const int d = powDiv[k];
        while ((1 << have) <= d) { pw[have] = lookup32(pw[have - 1], pw[have - 1]); have++; }
        __m256i r = _mm256_setzero_si256();
        bool first = true;
        for (int j = 0; (1 << j) <= d; j++)
            if ((d >> j) & 1) { r = first ? pw[j] : lookup32(r, pw[j]); first = false; }
        const uint32_t fix = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(r, laneId)) & laneMask;
        if (popcnt64(fix) != powReq[k]) return false;
    }
    return true;
}

// Env U1F_WALK uses the walk; U1F_CHECKPOW computes both and stops on any difference.
static const bool useWalk = getenv("U1F_WALK") != nullptr;
static const bool checkPow = getenv("U1F_CHECKPOW") != nullptr;
static std::atomic<uint64_t> powChecked{0};
static bool compat(const Match& a, const Match& b) {
    if (checkPow) {
        const bool w = compatWalk(a, b), p = compatPow(a, b);
        if (w != p) { printf("internal error: compatPow %d, compatWalk %d\n", (int)p, (int)w); exit(3); }
        powChecked.fetch_add(1, std::memory_order_relaxed);
        return w;
    }
    return useWalk ? compatWalk(a, b) : compatPow(a, b);
}

// All matchings containing edge 0-p, compatible with rows 1 and 2.
static void listMatchings(Match& m, uint32_t used, std::vector<Match>& out) {
    if (used == allVerts) {
        if (compat(m, R1) && compat(m, R2)) out.push_back(m);
        return;
    }
    int v = ctz64(~(uint64_t)used);
    for (int w = v + 1; w < N; w++) {
        if ((used >> w) & 1 || w == R1[v] || w == R2[v]) continue;
        m[v] = (int8_t)w;
        m[w] = (int8_t)v;
        listMatchings(m, used | (1u << v) | (1u << w), out);
    }
}

// Generators of the permutations that map the pair {R1, R2} to itself: on each cycle a rotation
// by two and a colour-preserving reflection, swaps of equal cycles, and the rotation by one on
// all cycles together, which exchanges R1 and R2.
static std::vector<std::array<int8_t, 32>> pairSymmetries(const std::vector<int>& cycles) {
    std::vector<std::array<int8_t, 32>> gens;
    std::array<int8_t, 32> id{};
    for (int i = 0; i < N; i++) id[i] = (int8_t)i;
    std::array<int8_t, 32> rot1 = id;
    int v = 0;
    for (size_t k = 0; k < cycles.size(); k++) {
        const int c = cycles[k];
        std::array<int8_t, 32> rot2 = id, refl = id, swap = id;
        for (int i = 0; i < c; i++) {
            rot1[v + i] = (int8_t)(v + (i + 1) % c);
            rot2[v + i] = (int8_t)(v + (i + 2) % c);
            refl[v + i] = (int8_t)(v + (1 - i + c) % c);
            if (k + 1 < cycles.size() && cycles[k + 1] == c) {
                swap[v + i] = (int8_t)(v + c + i);
                swap[v + c + i] = (int8_t)(v + i);
            }
        }
        gens.push_back(rot2);
        gens.push_back(refl);
        if (swap != id) gens.push_back(swap);
        v += c;
    }
    gens.push_back(rot1);
    return gens;
}

static Match applyPerm(const std::array<int8_t, 32>& g, const Match& m) {
    Match r{};
    for (int v = 0; v < N; v++) r[g[v]] = g[m[v]];
    return r;
}

struct Job { const Match* root; int firstPart; int orbit = 0; };

static std::vector<std::vector<Match>> parts;   // sorted by size
static std::vector<std::vector<int>> orbitOf;   // max mode: orbit number of each candidate
static std::vector<Job> jobs;
static size_t jobBase = 0;                      // a shard's first job, so progress shows global numbers
static int rowsWanted;

// What each thread is doing, for the progress lines: phase 0 idle, 1 building the job's matrix,
// 2 searching, in branch x of the n candidates of the first row chosen after the root; deep is
// the most rows any branch of the current job has held so far.
struct ThreadStatus {
    std::atomic<int> phase{0}, job{0}, x{0}, n{0}, deep{0};
    // The running job's branch counts per number of rows, published every 4096 nodes, so the
    // progress line can show live counts; zeroed when the job's counts move to depthNodes.
    std::array<std::atomic<uint64_t>, 33> depth{};
    std::atomic<long long> since{0};
};
static constexpr int kMaxThreads = 64;
// Per-thread lines under each progress line (job, branch x of n, deepest rows): env U1F_THREADLINES=1.
static const bool threadLines = getenv("U1F_THREADLINES") != nullptr;
static ThreadStatus threadStatus[kMaxThreads];
static bool maxMode = false, collecting = false;
// A subtree whose live candidates number at most localMax is searched on its own small matrix,
// which fits in cache. 4096 measured best on 4.14, 12 rows, jobs 1000-1008: 44.5 s, against 92 s
// without, 52 s at 16384 and much slower at 1024. Env U1F_LOCAL overrides it (0 = off).
static const int localMax = getenv("U1F_LOCAL") ? atoi(getenv("U1F_LOCAL")) : 4096;
// Edge-overlap test before compat when a job's matrix is built; env U1F_NOEDGE switches it off.
static const bool edgeFilter = getenv("U1F_NOEDGE") == nullptr;
// Diagnostic, env U1F_POOLSTAT=s: per job, print the size of the root's kept list and, for up to s
// candidates of its smallest group, the pool they would leave (kept candidates compatible with
// both), then skip the search. Measures whether a second fixed row (as rows 4/5 in k18p1f) pays.
static const int poolStat = getenv("U1F_POOLSTAT") ? atoi(getenv("U1F_POOLSTAT")) : 0;
// Diagnostic, env U1F_BUILDONLY: print each job's matrix build time and skip its search.
static const bool buildOnly = getenv("U1F_BUILDONLY") != nullptr;
// Subset search, env U1F_CYCLE4FIRST: keep only the candidates m with vertex 0 on a 4-cycle of
// R1 u m (types with a 4-cycle). Not a complete search: a factorization's rows need not all have
// that property. Orbits then use only the pair symmetries fixing vertex 0 and R1, and the output
// files are tagged "_c4first".
static const bool cycle4First = getenv("U1F_CYCLE4FIRST") != nullptr;
// Fixed-rows existence test, env U1F_FIXEDROWS with rows < N-1 (not max): only the rows-2 partner
// rows with the fewest candidates are kept, so each job's matrix holds only their candidates. Every
// full factorization has a row for every partner of vertex 0, so 0 found proves that none exists;
// a find proves nothing. Roots: every candidate of the smallest kept row (no symmetry used); the
// pass stops at the first find. Env U1F_JOBS="first last" runs only root jobs [first, last).
static const bool fixedRows = getenv("U1F_FIXEDROWS") != nullptr;
// Per-job existence test in a max shard (knu1f N type max threads K first last), env U1F_SMALLROWS:
// each job keeps only the K-3 rows with the fewest candidates kept for its root, so its matrix
// holds only those. A U1F rooted at the job has a row in each of them, so a job with no K-row
// partial there has no U1F; a job with one prints "HIT job j" (the full 19-row search must decide
// it). Each job stops at its first find; nothing is collected. Files are tagged "_small".
static const bool smallRows = getenv("U1F_SMALLROWS") != nullptr;
// With U1F_SMALLROWS, env U1F_SMALLKEEP: do not stop a job at its first find; keep every K-row
// partial found, one record per isomorphism class with |Aut|, in the "_small" file (starters for a
// second pass that extends them to N-1 rows).
static const bool smallKeep = smallRows && getenv("U1F_SMALLKEEP") != nullptr;
static std::atomic<size_t> jobsHit{0};
// Aligned AVX2 AND over the whole local matrix (W padded to 4 words). Measured no faster on 4.14,
// 12 rows, jobs 500-516 (233/249 s against 231/237 s): the matrix is in cache. Off; /D U1F_ALIGNED_AND=1 to try.
#ifndef U1F_ALIGNED_AND
#define U1F_ALIGNED_AND 0
#endif
static constexpr bool alignedAnd = U1F_ALIGNED_AND != 0;
static std::atomic<bool> stopPass{false};
static std::atomic<int> running{0};
static std::atomic<size_t> nextJob{0}, jobsDone{0};
static std::atomic<uint64_t> totalFound{0}, totalNodes{0};
static std::array<std::atomic<uint64_t>, 33> depthNodes;   // branches that reached d rows

// "rows 3..K: n3 n4 ... nK", the number of branches of this pass that held d rows; nK = found.
// With live = true (progress lines), the running jobs' published counts are added.
static std::string depthLine(bool live = false) {
    std::string s = "rows 3.." + std::to_string(rowsWanted) + ":";
    for (int d = 3; d <= rowsWanted; d++) {
        uint64_t n = depthNodes[d].load();
        if (live)
            for (const auto& t : threadStatus) n += t.depth[d].load(std::memory_order_relaxed);
        s += " " + std::to_string(n);
    }
    return s;
}

// Isomorphism classes of the results collected.
struct ClassRecord { std::vector<Match> rows; uint64_t aut; };
using Hash = std::pair<uint64_t, uint64_t>;
static std::vector<std::array<int8_t, 32>> pairGroup;   // permutations fixing R1 and R2
struct ClassStore { std::vector<ClassRecord> classes; std::set<Hash> seen; std::mutex lock; };
static ClassStore results, deep;
static bool trackDeep = false;
static size_t holdLimit = 1000;
static std::atomic<int> deepest{0};
static std::atomic<bool> deepFull{false};

static Hash sortAndHash(std::vector<Match>& rows) {
    std::sort(rows.begin(), rows.end());
    uint64_t a = 14695981039346656037ULL, b = 0x9E3779B97F4A7C15ULL;
    for (const Match& m : rows)
        for (int v = 0; v < N; v++) {
            a = (a ^ (uint8_t)m[v]) * 1099511628211ULL;
            b = (b + (uint8_t)m[v] + 1) * 0xC2B2AE3D27D4EB4FULL;
            b ^= b >> 29;
        }
    return { a, b };
}

// One permutation taking the ordered pair (A, B) to the standard pair (R1, R2).
static std::array<int8_t, 32> toStandard(const Match& A, const Match& B) {
    std::vector<std::vector<int>> found;
    uint32_t used = 0;
    for (int s = 0; s < N; s++) {
        if ((used >> s) & 1) continue;
        std::vector<int> c;
        for (int x = s;;) {
            c.push_back(x);
            c.push_back(A[x]);
            used |= (1u << x) | (1u << A[x]);
            x = B[A[x]];
            if (x == s) break;
        }
        found.push_back(c);
    }
    std::stable_sort(found.begin(), found.end(),
        [](const std::vector<int>& p, const std::vector<int>& q) { return p.size() < q.size(); });
    std::array<int8_t, 32> sigma{};
    int v = 0;
    for (const auto& c : found) {
        for (size_t i = 0; i < c.size(); i++) sigma[c[i]] = (int8_t)(v + i);
        v += (int)c.size();
    }
    return sigma;
}

// Record a partial unless its class is already known. Every labelled copy of a class that contains
// R1 and R2 is an image under one of the k(k-1)|G| maps tried here, so |Aut| = maps / distinct.
static void addClass(ClassStore& store, std::vector<Match>& P, const Hash& key) {
    if (store.seen.count(key)) return;

    std::set<Hash> images;
    std::vector<Match> best, Q(P.size());
    uint64_t maps = 0;
    for (size_t a = 0; a < P.size(); a++)
        for (size_t b = 0; b < P.size(); b++) {
            if (a == b) continue;
            const auto sigma = toStandard(P[a], P[b]);
            for (const auto& h : pairGroup) {
                std::array<int8_t, 32> perm{};
                for (int v = 0; v < N; v++) perm[v] = h[sigma[v]];
                for (size_t r = 0; r < P.size(); r++) Q[r] = applyPerm(perm, P[r]);
                images.insert(sortAndHash(Q));
                maps++;
                if (best.empty() || Q < best) best = Q;
            }
        }
    store.seen.insert(images.begin(), images.end());
    store.classes.push_back({ best, maps / images.size() });
}

static void addPartial(const std::vector<const Match*>& input) {
    std::vector<Match> P;
    for (const Match* m : input) P.push_back(*m);
    const Hash key = sortAndHash(P);
    std::lock_guard<std::mutex> g(results.lock);
    addClass(results, P, key);
}

// U1F_SMALLKEEP store: only the hash of each class's canonical form is kept (not of all its
// images), so memory stays small. Each new class is appended at once to <file>.part as one line
// "aut m0[0..N) m1[0..N) ...", and each finished job to <file>.done; a restarted shard reads both,
// skips the finished jobs and re-runs an interrupted one, whose classes are then already known.
static std::mutex smallLock;
static std::set<Hash> smallSeen;
static std::vector<ClassRecord> smallClasses;
static std::set<size_t> smallDoneJobs;
static FILE* smallPart = nullptr;
static FILE* smallDone = nullptr;

static void addSmall(const std::vector<const Match*>& input) {
    std::vector<Match> P;
    for (const Match* m : input) P.push_back(*m);
    // Canonical form: the least image over all maps taking two rows to (R1, R2), then the pair group.
    std::vector<Match> best, Q(P.size());
    for (size_t a = 0; a < P.size(); a++)
        for (size_t b = 0; b < P.size(); b++) {
            if (a == b) continue;
            const auto sigma = toStandard(P[a], P[b]);
            for (const auto& h : pairGroup) {
                std::array<int8_t, 32> perm{};
                for (int v = 0; v < N; v++) perm[v] = h[sigma[v]];
                for (size_t r = 0; r < P.size(); r++) Q[r] = applyPerm(perm, P[r]);
                std::sort(Q.begin(), Q.end());
                if (best.empty() || Q < best) best = Q;
            }
        }
    const Hash key = sortAndHash(best);
    {
        std::lock_guard<std::mutex> g(smallLock);
        if (!smallSeen.insert(key).second) return;
    }
    // New class: |Aut| = maps / distinct images.
    std::set<Hash> images;
    uint64_t maps = 0;
    for (size_t a = 0; a < best.size(); a++)
        for (size_t b = 0; b < best.size(); b++) {
            if (a == b) continue;
            const auto sigma = toStandard(best[a], best[b]);
            for (const auto& h : pairGroup) {
                std::array<int8_t, 32> perm{};
                for (int v = 0; v < N; v++) perm[v] = h[sigma[v]];
                for (size_t r = 0; r < best.size(); r++) Q[r] = applyPerm(perm, best[r]);
                images.insert(sortAndHash(Q));
                maps++;
            }
        }
    std::lock_guard<std::mutex> g(smallLock);
    smallClasses.push_back({ best, maps / images.size() });
    if (smallPart) {
        fprintf(smallPart, "%llu", (unsigned long long)(maps / images.size()));
        for (const Match& m : best)
            for (int v = 0; v < N; v++) fprintf(smallPart, " %d", (int)m[v]);
        fprintf(smallPart, "\n");
        fflush(smallPart);
    }
}

// Restore a shard's .part and .done files (if any) and open them for appending.
static void openSmall(const std::string& name, int rows) {
    bool needNewline = false;
    if (FILE* f = fopen((name + ".part").c_str(), "r")) {
        static char line[16384];
        while (fgets(line, sizeof line, f)) {
            const size_t len = strlen(line);
            needNewline = len == 0 || line[len - 1] != '\n';
            if (needNewline) break;             // a line cut by the stop: dropped, found again
            ClassRecord c{ {}, 0 };
            char* p = line;
            char* end;
            c.aut = strtoull(p, &end, 10);
            bool ok = end != p;
            for (int r = 0; r < rows && ok; r++) {
                Match m{};
                for (int v = 0; v < N && ok; v++) {
                    p = end;
                    const long x = strtol(p, &end, 10);
                    ok = end != p && x >= 0 && x < N;
                    m[v] = (int8_t)x;
                }
                c.rows.push_back(m);
            }
            if (ok && smallSeen.insert(sortAndHash(c.rows)).second) smallClasses.push_back(c);
        }
        fclose(f);
    }
    if (FILE* f = fopen((name + ".done").c_str(), "r")) {
        for (size_t j; fscanf(f, "%zu", &j) == 1;) smallDoneJobs.insert(j);
        fclose(f);
    }
    if (!smallClasses.empty() || !smallDoneJobs.empty())
        printf("resumed: %zu classes, %zu jobs done before\n", smallClasses.size(), smallDoneJobs.size());
    smallPart = fopen((name + ".part").c_str(), "a");
    if (smallPart && needNewline) fprintf(smallPart, "\n");
    smallDone = fopen((name + ".done").c_str(), "a");
}

// Keep the rows-so-far if they are at least as deep as anything seen, up to holdLimit classes.
static void noteDeep(const Match* root, const std::vector<const Match*>& chosen, const Match* last) {
    const int depth = 3 + (int)chosen.size() + (last ? 1 : 0);
    const int known = deepest.load(std::memory_order_relaxed);
    if (depth < known || (depth == known && deepFull.load(std::memory_order_relaxed))) return;

    std::vector<Match> P = { R1, R2, *root };
    for (const Match* m : chosen) P.push_back(*m);
    if (last) P.push_back(*last);
    const Hash key = sortAndHash(P);

    std::lock_guard<std::mutex> g(deep.lock);
    if (depth < deepest.load()) return;
    if (depth > deepest.load()) {
        deepest = depth;
        deep.classes.clear();
        deep.seen.clear();
        deepFull = false;
    }
    if (deep.classes.size() >= holdLimit) return;
    addClass(deep, P, key);
    if (deep.classes.size() >= holdLimit) deepFull = true;
}

static bool writeClasses(std::vector<ClassRecord>& classes, const std::string& name, const char* type, int rows) {
    std::sort(classes.begin(), classes.end(),
        [](const ClassRecord& a, const ClassRecord& b) { return a.rows < b.rows; });
    FILE* f = fopen(name.c_str(), "w");
    if (!f) { printf("cannot write %s\n", name.c_str()); return false; }
    fprintf(f, "=== type %s: %zu class(es) with %d rows\n", type, classes.size(), rows);
    for (size_t i = 0; i < classes.size(); i++) {
        fprintf(f, "#%zu |Aut| = %llu\n", i + 1, (unsigned long long)classes[i].aut);
        for (const Match& m : classes[i].rows) {
            for (int v = 0; v < N; v++)
                if (m[v] > v) fprintf(f, " %2d %2d", v, (int)m[v]);
            fprintf(f, "\n");
        }
    }
    fclose(f);
    return true;
}

// Word-aligned bit range of one row's candidates.
struct Range { int first, words, count; };

// 64-byte aligned storage for the bit sets, so that 4-word AVX2 loads never split a cache line.
template <class T> struct Aligned64 {
    using value_type = T;
    Aligned64() = default;
    template <class U> Aligned64(const Aligned64<U>&) {}
    T* allocate(size_t n) { return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t(64))); }
    void deallocate(T* p, size_t) { ::operator delete(p, std::align_val_t(64)); }
    template <class U> bool operator==(const Aligned64<U>&) const { return true; }
    template <class U> bool operator!=(const Aligned64<U>&) const { return false; }
};
using Bits = std::vector<uint64_t, Aligned64<uint64_t>>;

// The bit sets of one search: candidates, their compatibility and the live set per level.
struct Ctx {
    int W = 0;                                  // words per bit set
    std::vector<const Match*> cand;             // bit -> matching
    Bits adj;                                   // W words per bit
    Bits alive;                                 // W words per level
    std::vector<std::array<Range, 32>> act;     // active rows per level
};

// In-place transpose of a 64 x 64 bit tile, bit c of word r = entry (r, c).
static void transpose64(uint64_t a[64]) {
    uint64_t m = 0x00000000FFFFFFFFULL;
    for (int j = 32; j; j >>= 1, m ^= m << j)
        for (int k = 0; k < 64; k = ((k | j) + 1) & ~j) {
            const uint64_t t = ((a[k] >> j) ^ a[k | j]) & m;
            a[k | j] ^= t;
            a[k] ^= t << j;
        }
}

static inline void putBits(uint64_t* dst, int p, uint64_t bits, int k) {
    dst[p >> 6] |= bits << (p & 63);
    if ((p & 63) + k > 64) dst[(p >> 6) + 1] |= bits >> (64 - (p & 63));
}

class Worker {
    Ctx top;                                    // the job: all candidates compatible with the root
    Ctx local;                                  // one subtree, rebuilt on its live candidates only
    std::vector<int> global;                    // local bit -> bit in top
    std::vector<const Match*> chosen;
    const Match* root = nullptr;
    ThreadStatus* status = nullptr;             // progress of this thread's job, for printThreads
    uint64_t found = 0, nodes = 0;
    std::array<uint64_t, 33> atDepth{};
    bool jobStop = false;                       // U1F_SMALLROWS: this job has a find, stop it

    void collectAll(Ctx& c, int L, int nAct) {
        std::vector<const Match*> rows = { &R1, &R2, root };
        rows.insert(rows.end(), chosen.begin(), chosen.end());
        rows.push_back(nullptr);
        for (int j = 0; j < nAct; j++) {
            const Range& r = c.act[L][j];
            for (int w = r.first; w < r.first + r.words; w++)
                for (uint64_t bits = c.alive[(size_t)L * c.W + w]; bits; bits &= bits - 1) {
                    rows.back() = c.cand[64 * w + ctz64(bits)];
                    if (smallKeep) addSmall(rows);
                    else addPartial(rows);
                    found++;
                }
        }
    }

    // Copy level L of the job into `local`, keeping only the live candidates, each row packed into
    // its own words in the same order. The search of the subtree then reads a matrix of about
    // live x live bits, which fits in cache, instead of rows of the whole job.
    void compact(int L, int nAct) {
        const uint64_t* cur = &top.alive[(size_t)L * top.W];
        std::array<Range, 32> ranges;
        int words = 0;
        for (int j = 0; j < nAct; j++) {
            const int c = top.act[L][j].count, w = (c + 63) / 64;
            ranges[j] = { words, w, c };
            words += w;
        }
        if (alignedAnd) words = (words + 3) & ~3;        // whole 32-byte blocks per row
        local.W = words;
        local.cand.assign((size_t)words * 64, nullptr);
        local.adj.assign((size_t)words * 64 * words, 0);
        local.alive.assign((size_t)words * (N + 2), 0);
        if (local.act.size() < (size_t)N + 2) local.act.resize((size_t)N + 2);
        global.assign((size_t)words * 64, -1);
        for (int j = 0; j < nAct; j++) {
            const Range& r = top.act[L][j];
            int p = 64 * ranges[j].first;
            for (int u = r.first; u < r.first + r.words; u++)
                for (uint64_t bits = cur[u]; bits; bits &= bits - 1) {
                    global[p] = 64 * u + ctz64(bits);
                    local.cand[p] = top.cand[global[p]];
                    local.alive[p >> 6] |= 1ULL << (p & 63);
                    p++;
                }
            local.act[0][j] = ranges[j];
        }
        for (int i = 0; i < words * 64; i++) {
            if (global[i] < 0) continue;
            const uint64_t* a = &top.adj[(size_t)global[i] * top.W];
            uint64_t* dst = &local.adj[(size_t)i * words];
            for (int j = 0; j < nAct; j++) {
                const Range& r = top.act[L][j];
                int p = 64 * ranges[j].first;
                for (int u = r.first; u < r.first + r.words; u++) {
                    const uint64_t m = cur[u];
                    if (!m) continue;
                    const int k = popcnt64(m);
                    putBits(dst, p, pext64(a[u], m), k);
                    p += k;
                }
            }
        }
    }

    // c.act[L][0..nAct) are rows with at least one candidate; nAct >= need >= 1.
    void dfs(Ctx& c, int L, int nAct, int need) {
        if (jobStop) return;
        if (&c == &top && localMax > 0) {
            int live = 0;
            for (int j = 0; j < nAct && live <= localMax; j++) live += c.act[L][j].count;
            if (live <= localMax) { compact(L, nAct); dfs(local, 0, nAct, need); return; }
        }
        nodes++;
        if (status && (nodes & 4095) == 0) {    // live counts for the progress line
            for (int d = 3; d < rowsWanted; d++) status->depth[d].store(atDepth[d], std::memory_order_relaxed);
            status->depth[rowsWanted].store(found, std::memory_order_relaxed);
        }
        if (need == 1) {
            if (collecting) { collectAll(c, L, nAct); return; }
            for (int j = 0; j < nAct; j++) found += c.act[L][j].count;
            if (smallRows) jobStop = true;
            else if (maxMode || fixedRows) stopPass = true;
            return;
        }
        int best = 0;
        for (int j = 1; j < nAct; j++)
            if (c.act[L][j].count < c.act[L][best].count) best = j;
        const Range branch = c.act[L][best];
        const bool firstRow = chosen.empty() && status && status->n.load() == 0;
        if (firstRow) { status->x = 0; status->n = branch.count; }
        const int W = c.W;
        const uint64_t* cur = &c.alive[(size_t)L * W];
        uint64_t* nxt = &c.alive[(size_t)(L + 1) * W];
        const bool aligned = alignedAnd && &c == &local;

        for (int w = branch.first; w < branch.first + branch.words; w++) {
            for (uint64_t bits = cur[w]; bits; bits &= bits - 1) {
                if (jobStop || stopPass.load(std::memory_order_relaxed)) return;
                if (firstRow) status->x.fetch_add(1, std::memory_order_relaxed);
                const int v = 64 * w + ctz64(bits);
                if (trackDeep) noteDeep(root, chosen, c.cand[v]);
                const uint64_t* a = &c.adj[(size_t)v * W];
                int n = 0, empties = 0;
                const int allowed = nAct - need;
                // Local matrix: W is a multiple of 4 and every row of it is 32-byte aligned, so
                // the whole live set is ANDed with aligned AVX2 loads; the counts follow.
                if (aligned)
                    for (int u = 0; u < W; u += 4)
                        _mm256_store_si256((__m256i*)(nxt + u), _mm256_and_si256(
                            _mm256_load_si256((const __m256i*)(cur + u)), _mm256_load_si256((const __m256i*)(a + u))));
                for (int j = 0; j < nAct && empties <= allowed; j++) {
                    if (j == best) continue;
                    Range r = c.act[L][j];
                    int cnt = 0;
                    if (aligned)
                        for (int u = r.first; u < r.first + r.words; u++) cnt += popcnt64(nxt[u]);
                    else
                        for (int u = r.first; u < r.first + r.words; u++) {
                            nxt[u] = cur[u] & a[u];
                            cnt += popcnt64(nxt[u]);
                        }
                    if (cnt) { r.count = cnt; c.act[L + 1][n++] = r; }
                    else empties++;
                }
                if (empties > allowed) continue;
                chosen.push_back(c.cand[v]);
                atDepth[3 + chosen.size()]++;
                if (status && 3 + (int)chosen.size() > status->deep.load(std::memory_order_relaxed))
                    status->deep = 3 + (int)chosen.size();
                dfs(c, L + 1, n, need - 1);
                chosen.pop_back();
            }
        }

        if (nAct - 1 >= need) {                 // sets with no row from this branch
            int n = 0;
            for (int j = 0; j < nAct; j++) {
                if (j == best) continue;
                const Range& r = c.act[L][j];
                std::copy(cur + r.first, cur + r.first + r.words, nxt + r.first);
                c.act[L + 1][n++] = r;
            }
            dfs(c, L + 1, n, need);
        }
    }

    void runJob(const Job& job) {
        root = job.root;
        chosen.clear();
        const int need = rowsWanted - 3;
        if (trackDeep) noteDeep(root, chosen, nullptr);
        if (need == 0) {
            if (collecting) addPartial({ &R1, &R2, root });
            else if (maxMode) stopPass = true;
            found++;
            return;
        }

        std::vector<std::vector<const Match*>> local;
        int words = 0, nonEmpty = 0;
        std::vector<Range> ranges;
        for (size_t p = maxMode ? 0 : job.firstPart + 1; p < parts.size(); p++) {
            if ((int)p == job.firstPart) continue;
            std::vector<const Match*> keep;
            for (size_t i = 0; i < parts[p].size(); i++) {
                if (maxMode && orbitOf[p][i] < job.orbit) continue;
                if (compat(*root, parts[p][i])) keep.push_back(&parts[p][i]);
            }
            if (keep.empty()) continue;
            nonEmpty++;
            const int w = (int)(keep.size() + 63) / 64;
            ranges.push_back({ words, w, (int)keep.size() });
            words += w;
            local.push_back(std::move(keep));
        }
        if (nonEmpty < need) return;
        if (smallRows && maxMode && (int)local.size() > need) {
            std::vector<size_t> ord(local.size());
            std::iota(ord.begin(), ord.end(), 0);
            std::stable_sort(ord.begin(), ord.end(),
                [&](size_t a, size_t b) { return local[a].size() < local[b].size(); });
            std::vector<std::vector<const Match*>> keepRows;
            ranges.clear();
            words = 0;
            for (int j = 0; j < need; j++) {
                keepRows.push_back(std::move(local[ord[j]]));
                const int w = (int)(keepRows.back().size() + 63) / 64;
                ranges.push_back({ words, w, (int)keepRows.back().size() });
                words += w;
            }
            local = std::move(keepRows);
            nonEmpty = need;
        }

        if (poolStat > 0) {
            size_t K = 0, sg = 0;                    // sg: the smallest group ("small" is a Windows macro)
            for (size_t i = 0; i < local.size(); i++) {
                K += local[i].size();
                if (local[i].size() < local[sg].size()) sg = i;
            }
            const size_t g = local[sg].size(), step = std::max<size_t>(1, g / (size_t)poolStat);
            double sum = 0, sumSq = 0;
            size_t n = 0, minPool = SIZE_MAX, maxPool = 0;
            for (size_t x = 0; x < g; x += step) {
                size_t pool = 0;
                for (size_t i = 0; i < local.size(); i++)
                    if (i != sg)
                        for (const Match* m : local[i]) pool += compat(*local[sg][x], *m);
                sum += (double)pool;
                sumSq += (double)pool * pool;
                minPool = std::min(minPool, pool);
                maxPool = std::max(maxPool, pool);
                n++;
            }
            const double full = (double)K * K / 2, pair = (double)g * (sumSq / n) / 2 + (double)g * K;
            printf("POOLSTAT job orbit %d: kept %zu in %d groups, smallest group %zu; pool over %zu samples:"
                " avg %.0f min %zu max %zu (%.2f%% of kept); tests full matrix %.3g, pair scheme %.3g (x%.2f);"
                " memory full %.0f MB, pair bit sets %.0f MB\n",
                job.orbit, K, nonEmpty, g, n, sum / n, minPool, maxPool, 100.0 * sum / n / K, full, pair,
                pair / full, (double)K * K / 8 / 1048576, (double)g * K / 8 / 1048576);
            return;
        }

        const auto buildStart = std::chrono::steady_clock::now();
        top.W = words;
        const int W = words;
        top.cand.assign((size_t)W * 64, nullptr);
        top.adj.assign((size_t)W * 64 * W, 0);
        top.alive.assign((size_t)W * (N + 2), 0);
        if (top.act.size() < (size_t)N + 2) top.act.resize((size_t)N + 2);

        for (size_t i = 0; i < local.size(); i++)
            for (size_t k = 0; k < local[i].size(); k++) {
                const int b = 64 * ranges[i].first + (int)k;
                top.cand[b] = local[i][k];
                top.alive[b / 64] |= 1ULL << (b % 64);
            }
        // Edge sets as bit masks: two matchings with a common edge are never compatible, which
        // six ANDs decide before the cycle walk of compat.
        using Edges = std::array<uint64_t, 16>;          // bit v*N+w for edge v-w, v < w; N <= 32
        const int ew = (N * N + 63) / 64;
        std::vector<std::vector<Edges>> edges(local.size());
        for (size_t i = 0; i < local.size(); i++)
            for (const Match* m : local[i]) {
                Edges e{};
                for (int v = 0; v < N; v++)
                    if (m->at(v) > v) { const int b = v * N + m->at(v); e[b >> 6] |= 1ULL << (b & 63); }
                edges[i].push_back(e);
            }
        const auto disjoint = [ew](const Edges& a, const Edges& b) {
            uint64_t s = 0;
            for (int w = 0; w < ew; w++) s |= a[w] & b[w];
            return s == 0;
        };
        // The candidates of each group copied into one contiguous block, so the inner loop streams
        // through memory instead of following pointers into the global lists.
        std::vector<std::vector<Match>> flat(local.size());
        for (size_t i = 0; i < local.size(); i++)
            for (const Match* m : local[i]) flat[i].push_back(*m);
        const bool powOnly = !useWalk && !checkPow;      // the power test needs no edge prefilter
        for (size_t i = 0; i < local.size(); i++)
            for (size_t k = i + 1; k < local.size(); k++)
                for (size_t x = 0; x < flat[i].size(); x++) {
                    const Match& mx = flat[i][x];
                    uint64_t* row = &top.adj[(size_t)(64 * ranges[i].first + x) * W + ranges[k].first];
                    const size_t n = flat[k].size();
                    for (size_t y0 = 0; y0 < n; y0 += 64) {
                        uint64_t acc = 0;
                        const size_t y1 = std::min(n, y0 + 64);
                        if (powOnly)
                            for (size_t y = y0; y < y1; y++) acc |= (uint64_t)compatPow(mx, flat[k][y]) << (y - y0);
                        else
                            for (size_t y = y0; y < y1; y++)
                                if ((!edgeFilter || disjoint(edges[i][x], edges[k][y])) && compat(mx, flat[k][y]))
                                    acc |= 1ULL << (y - y0);
                        row[y0 / 64] = acc;
                    }
                }
        // Only the rows of the earlier group were written, in order. The mirror half is filled by
        // transposing 64 x 64 tiles (groups start on word boundaries), instead of one scattered
        // write per compatible pair, which in a matrix of GBs misses the cache every time.
        for (size_t i = 0; i < local.size(); i++)
            for (size_t k = i + 1; k < local.size(); k++)
                for (int rw = ranges[i].first; rw < ranges[i].first + ranges[i].words; rw++)
                    for (int cw = ranges[k].first; cw < ranges[k].first + ranges[k].words; cw++) {
                        uint64_t tile[64];
                        for (int t = 0; t < 64; t++) tile[t] = top.adj[(size_t)(64 * rw + t) * W + cw];
                        transpose64(tile);
                        for (int t = 0; t < 64; t++) top.adj[(size_t)(64 * cw + t) * W + rw] = tile[t];
                    }

        for (int j = 0; j < nonEmpty; j++) top.act[0][j] = ranges[j];
        if (status) status->phase = 2;
        if (buildOnly) {                        // diagnostic: time the matrix build, skip the search
            size_t K = 0;
            for (const auto& g : local) K += g.size();
            printf("BUILD job orbit %d: kept %zu, matrix %.0f MB, %.1f s\n", job.orbit, K,
                (double)top.adj.size() * 8 / 1048576,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart).count());
            return;
        }
        dfs(top, 0, nonEmpty, need);
    }

public:
    void run(int tid) {
        status = tid < kMaxThreads ? &threadStatus[tid] : nullptr;
        for (;;) {
            const size_t i = nextJob.fetch_add(1);
            if (i >= jobs.size() || stopPass.load()) { if (status) status->phase = 0; return; }
            if (smallKeep && smallDoneJobs.count(i + jobBase)) { jobsDone++; continue; }
            found = nodes = 0;
            jobStop = false;
            atDepth.fill(0);
            atDepth[3] = 1;
            if (status) {
                status->job = (int)(i + jobBase);
                status->x = status->n = 0;
                status->deep = 3;
                status->since = std::chrono::steady_clock::now().time_since_epoch().count();
                status->phase = 1;
            }
            runJob(jobs[i]);
            atDepth[rowsWanted] = found;
            totalFound += found;
            if (smallRows && found) {
                jobsHit++;
                printf("HIT job %zu\n", i + jobBase);
                fflush(stdout);
            }
            if (smallKeep && smallDone) {
                std::lock_guard<std::mutex> g(smallLock);
                fprintf(smallDone, "%zu\n", i + jobBase);
                fflush(smallDone);
            }
            totalNodes += nodes;
            if (status)                         // first unpublish, then add: never counted twice
                for (auto& d : status->depth) d.store(0, std::memory_order_relaxed);
            for (int d = 3; d <= rowsWanted; d++) depthNodes[d] += atDepth[d];
            jobsDone++;
        }
    }
};

// Run all jobs for the current rowsWanted on `threads` threads, with a progress line each minute,
// followed by one line per thread: its job, and how far it is (building the job's matrix, or the
// branch of the first chosen row it is in, x of n).
static void printThreads(int threads) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int t = 0; t < threads && t < kMaxThreads; t++) {
        const ThreadStatus& s = threadStatus[t];
        const int phase = s.phase.load();
        if (phase == 0) continue;
        const double sec = (double)(now - s.since.load()) * std::chrono::steady_clock::period::num /
            std::chrono::steady_clock::period::den;
        if (phase == 1)
            printf("    thread %d: job %d, building its matrix, %.0f s\n", t, s.job.load(), sec);
        else
            printf("    thread %d: job %d, branch %d of %d, deepest %d rows, %.0f s\n", t, s.job.load(), s.x.load(),
                s.n.load(), s.deep.load(), sec);
    }
}

static void runPass(int threads) {
    for (auto& s : threadStatus) {
        s.phase = 0;
        for (auto& d : s.depth) d.store(0, std::memory_order_relaxed);
    }
    nextJob = 0;
    jobsDone = 0;
    totalFound = 0;
    totalNodes = 0;
    for (auto& d : depthNodes) d = 0;
    stopPass = false;
    running = threads;
    std::vector<std::thread> pool;
    for (int i = 0; i < threads; i++)
        pool.emplace_back([i] {
            auto w = std::make_unique<Worker>();
            w->run(i);
            running--;
        });
    const auto t0 = std::chrono::steady_clock::now();
    double lastPrint = 0;
    while (running.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (s - lastPrint >= 60) {
            lastPrint = s;
            printf("  %.0f s: %zu of %zu jobs, found %llu; %s\n", s, jobsDone.load(), jobs.size(),
                (unsigned long long)totalFound.load(), depthLine(true).c_str());
            if (smallKeep) {
                std::lock_guard<std::mutex> g(smallLock);
                printf("    classes recorded: %zu\n", smallClasses.size());
            }
            if (threadLines) printThreads(threads);
            fflush(stdout);
        }
    }
    for (auto& t : pool) t.join();
}

// Read one class file written by writeClasses: its type, its number of rows and its records.
static bool readClassFile(const char* path, std::string& type, int& rows, std::vector<ClassRecord>& out) {
    FILE* f = fopen(path, "r");
    if (!f) { printf("cannot read %s\n", path); return false; }
    char t[64];
    size_t n;
    if (fscanf(f, "=== type %63s %zu class(es) with %d rows\n", t, &n, &rows) != 3) {
        printf("%s: not a class file\n", path); fclose(f); return false;
    }
    type = t;
    if (!type.empty() && type.back() == ':') type.pop_back();
    for (size_t i = 0; i < n; i++) {
        size_t idx;
        unsigned long long aut;
        if (fscanf(f, " #%zu |Aut| = %llu", &idx, &aut) != 2) { printf("%s: bad record\n", path); fclose(f); return false; }
        ClassRecord c{ {}, aut };
        for (int r = 0; r < rows; r++) {
            Match m{};
            int cnt = 0;
            for (;;) {
                int x, y;
                if (fscanf(f, "%d %d", &x, &y) != 2) break;
                m[x] = (int8_t)y; m[y] = (int8_t)x; cnt += 2;
                int ch = fgetc(f);
                while (ch == ' ') ch = fgetc(f);
                if (ch == '\n' || ch == EOF) break;
                ungetc(ch, f);
            }
            N = cnt;
            c.rows.push_back(m);
        }
        out.push_back(c);
    }
    fclose(f);
    return true;
}

// knu1f merge out.txt in1.txt in2.txt ... : union of class files of one type and size, each class
// once, sorted as a single run would write it. Classes are canonical, so equal classes have equal rows.
static int mergeFiles(int argc, char** argv) {
    std::set<std::vector<Match>> have;
    std::vector<ClassRecord> all;
    std::string type;
    int rows = -1;
    for (int a = 3; a < argc; a++) {
        std::string ts;
        int k;
        std::vector<ClassRecord> in;
        if (!readClassFile(argv[a], ts, k, in)) return 3;
        if (rows < 0) { type = ts; rows = k; }
        if (ts != type || k != rows) { printf("%s: type or rows differ from %s\n", argv[a], argv[3]); return 3; }
        for (const ClassRecord& c : in)
            if (have.insert(c.rows).second) all.push_back(c);
    }
    if (rows < 0) { printf("no input files\n"); return 2; }
    if (!writeClasses(all, argv[2], type.c_str(), rows)) return 3;
    printf("RESULT merged %d files: type %s, %d rows, %zu classes, in %s\n", argc - 3, type.c_str(), rows,
        all.size(), argv[2]);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Continuation: knu1f N type continue threads file1 [file2 ...]
//
// Second pass for starters, e.g. the K-row classes written by U1F_SMALLROWS + U1F_SMALLKEEP.
// Each class is relabelled so that its first two rows are the standard pair; its other rows are
// then fixed, and the missing N-1-R rows are searched among all candidates compatible with every
// row of the class: one candidate from each missing partner group of vertex 0, pairwise
// compatible (bit matrix, smallest group first, cut as soon as a group has no candidate left).
// Every uniform 1-factorisation found is recorded by class with |Aut| in K<N>_U1F_continued_<type>.txt.
// Per class the most rows reached is counted, so the output says how far the starters get.
// Input: class files as written by writeClasses, or the .part files of U1F_SMALLKEEP.
// The candidate lists must be the full ones: U1F_CYCLE4FIRST and U1F_FIXEDROWS are refused.

// Read classes from a writeClasses file or from a U1F_SMALLKEEP .part file (one class per line:
// aut, then R rows of N partners each). Returns false if the file cannot be read.
static bool readAnyClasses(const std::string& path, std::vector<ClassRecord>& out) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { printf("cannot read %s\n", path.c_str()); return false; }
    const int first = fgetc(f);
    fclose(f);
    if (first == '=') {
        std::string type;
        int rows;
        const int keepN = N;
        const bool ok = readClassFile(path.c_str(), type, rows, out);
        N = keepN;
        return ok;
    }
    f = fopen(path.c_str(), "r");
    static char line[16384];
    while (fgets(line, sizeof line, f)) {
        const size_t len = strlen(line);
        if (len == 0 || line[len - 1] != '\n') break;   // a cut last line
        std::vector<long> t;
        char* p = line;
        for (;;) {
            char* end;
            const long x = strtol(p, &end, 10);
            if (end == p) break;
            t.push_back(x);
            p = end;
        }
        if (t.size() < 1 + 3 * (size_t)N || (t.size() - 1) % N) continue;
        ClassRecord c{ {}, (uint64_t)t[0] };
        bool ok = true;
        for (size_t r = 0; r < (t.size() - 1) / N && ok; r++) {
            Match m{};
            for (int v = 0; v < N; v++) {
                const long x = t[1 + r * N + v];
                ok = ok && x >= 0 && x < N;
                m[v] = (int8_t)x;
            }
            c.rows.push_back(m);
        }
        if (ok) out.push_back(c);
    }
    fclose(f);
    return true;
}

// The permutations fixing R1 and R2 (all pair symmetries but the exchange), as in the max pass.
static void buildPairGroup(const std::vector<int>& cycles) {
    auto gens = pairSymmetries(cycles);
    gens.pop_back();
    std::array<int8_t, 32> identity{};
    for (int i = 0; i < N; i++) identity[i] = (int8_t)i;
    std::set<std::array<int8_t, 32>> group = { identity };
    pairGroup = { identity };
    for (size_t i = 0; i < pairGroup.size(); i++)
        for (const auto& g : gens) {
            std::array<int8_t, 32> c{};
            for (int v = 0; v < N; v++) c[v] = g[pairGroup[i][v]];
            if (group.insert(c).second) pairGroup.push_back(c);
        }
}

// Search the missing rows of one class. Returns the most rows reached (R if no row can be added).
struct ContSearch {
    std::vector<const Match*> cand;             // candidates compatible with every row of the class
    std::vector<std::vector<uint64_t>> groupMask;   // per missing partner: its candidates
    std::vector<uint64_t> adj;                  // cand x cand compatibility, W words per row
    int W = 0, base = 0, deepest = 0;
    uint64_t full = 0;
    std::vector<Match> rows;                    // R1, R2, the class rows, then the chosen ones

    void dfs(const std::vector<uint64_t>& live, int need) {
        if ((int)rows.size() > deepest) deepest = (int)rows.size();
        if (need == 0) {
            full++;
            std::vector<const Match*> P;
            for (const Match& m : rows) P.push_back(&m);
            addPartial(P);
            return;
        }
        int best = -1, bestCount = INT32_MAX;
        for (size_t g = 0; g < groupMask.size(); g++) {
            int cnt = 0;
            bool any = false;
            for (int w = 0; w < W; w++) {
                const uint64_t x = live[w] & groupMask[g][w];
                any = any || x;
                cnt += popcnt64(x);
            }
            if (!any) continue;                 // filled already, or dead (counted below)
            if (cnt < bestCount) { bestCount = cnt; best = (int)g; }
        }
        // Groups still to fill = need; a group with no live candidate that is not yet filled
        // ends the branch. Filled groups have no live candidates either, so count the live ones.
        int liveGroups = 0;
        for (size_t g = 0; g < groupMask.size(); g++)
            for (int w = 0; w < W; w++)
                if (live[w] & groupMask[g][w]) { liveGroups++; break; }
        if (liveGroups < need || best < 0) return;
        std::vector<uint64_t> next(W);
        for (int w = 0; w < W; w++)
            for (uint64_t bits = live[w] & groupMask[best][w]; bits; bits &= bits - 1) {
                const int c = 64 * w + ctz64(bits);
                const uint64_t* a = &adj[(size_t)c * W];
                for (int u = 0; u < W; u++) next[u] = live[u] & a[u];
                for (int u = 0; u < W; u++) next[u] &= ~groupMask[best][u];   // the group is filled
                rows.push_back(*cand[c]);
                dfs(next, need - 1);
                rows.pop_back();
            }
    }
};

static int runContinue(int argc, char** argv, int threads) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto secs = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    std::vector<ClassRecord> in;
    for (int a = 5; a < argc; a++) {
        const size_t before = in.size();
        if (!readAnyClasses(argv[a], in)) return 3;
        printf("%s: %zu classes\n", argv[a], in.size() - before);
    }
    {   // the same class may come from several shards
        std::set<Hash> seen;
        std::vector<ClassRecord> unique;
        for (auto& c : in) {
            std::vector<Match> r = c.rows;
            if (seen.insert(sortAndHash(r)).second) unique.push_back(c);
        }
        in.swap(unique);
    }
    std::vector<const Match*> all;
    for (const auto& p : parts)
        for (const Match& m : p) all.push_back(&m);
    printf("continue: %zu distinct classes, %zu candidates, %d threads\n", in.size(), all.size(), threads);
    fflush(stdout);

    std::vector<int> deepest(in.size(), 0);
    std::atomic<size_t> next{0}, done{0};
    std::atomic<uint64_t> fullTotal{0};
    std::atomic<int> running{threads};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++)
        pool.emplace_back([&] {
            for (size_t i; (i = next.fetch_add(1)) < in.size();) {
                const ClassRecord& cls = in[i];
                const int R = (int)cls.rows.size();
                const auto sigma = toStandard(cls.rows[0], cls.rows[1]);
                std::vector<Match> rest;
                for (int r = 2; r < R; r++) rest.push_back(applyPerm(sigma, cls.rows[r]));
                // Edges of the class rows, per vertex: a candidate using one is never compatible.
                std::array<uint32_t, 32> used{};
                for (const Match& m : rest)
                    for (int v = 0; v < N; v++) used[v] |= 1u << m[v];
                ContSearch s;
                s.rows = { R1, R2 };
                s.rows.insert(s.rows.end(), rest.begin(), rest.end());
                s.deepest = R;
                std::map<int, int> groupOf;     // partner of 0 -> group index
                std::vector<int> candGroup;
                for (const Match* c : all) {
                    bool good = true;
                    for (int v = 0; v < N && good; v++) good = !((used[v] >> (*c)[v]) & 1);
                    for (size_t r = 0; r < rest.size() && good; r++) good = compat(*c, rest[r]);
                    if (!good) continue;
                    const int p = (*c)[0];
                    if (!groupOf.count(p)) groupOf[p] = (int)groupOf.size();
                    s.cand.push_back(c);
                    candGroup.push_back(groupOf[p]);
                }
                const int need = N - 1 - R;
                if (need > 0 && (int)groupOf.size() >= need) {
                    const int K = (int)s.cand.size();
                    s.W = (K + 63) / 64;
                    s.groupMask.assign(groupOf.size(), std::vector<uint64_t>(s.W, 0));
                    std::vector<uint64_t> live(s.W, 0);
                    for (int c = 0; c < K; c++) {
                        s.groupMask[candGroup[c]][c / 64] |= 1ULL << (c % 64);
                        live[c / 64] |= 1ULL << (c % 64);
                    }
                    s.adj.assign((size_t)K * s.W, 0);
                    for (int x = 0; x < K; x++)
                        for (int y = x + 1; y < K; y++)
                            if (candGroup[x] != candGroup[y] && compat(*s.cand[x], *s.cand[y])) {
                                s.adj[(size_t)x * s.W + y / 64] |= 1ULL << (y % 64);
                                s.adj[(size_t)y * s.W + x / 64] |= 1ULL << (x % 64);
                            }
                    s.dfs(live, need);
                }
                else if (need == 0) {           // the class is already complete
                    std::vector<const Match*> P;
                    for (const Match& m : s.rows) P.push_back(&m);
                    addPartial(P);
                    s.full = 1;
                }
                deepest[i] = s.deepest;
                fullTotal += s.full;
                done++;
            }
            running--;
        });
    double lastPrint = 0;
    while (running.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const double s = secs();
        if (s - lastPrint >= 60) {
            lastPrint = s;
            printf("  %.0f s: %zu of %zu classes, %llu full found\n", s, done.load(), in.size(),
                (unsigned long long)fullTotal.load());
            fflush(stdout);
        }
    }
    for (auto& t : pool) t.join();

    std::map<int, size_t> hist;
    for (int d : deepest) hist[d]++;
    printf("most rows reached, per class:");
    for (const auto& e : hist) printf(" %d rows: %zu", e.first, e.second);
    printf("\n");
    const std::string name = "K" + std::to_string(N) + "_U1F_continued_" + argv[2] + ".txt";
    if (!writeClasses(results.classes, name, argv[2], N - 1)) return 3;
    printf("RESULT K%d type %s, continue %zu classes: %zu uniform 1-factorisation classes (%llu labelled), "
        "%.1f s, in %s\n", N, argv[2], in.size(), results.classes.size(),
        (unsigned long long)fullTotal.load(), secs(), name.c_str());
    return 0;
}

static int run(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "merge") {
        if (argc < 4) { printf("usage: knu1f merge out.txt in1.txt [in2.txt ...]\n"); return 2; }
        return mergeFiles(argc, argv);
    }
    if (argc < 3) {
        printf("usage: knu1f N type [rows|max] [threads] [hold]        e.g. knu1f 18 4.14\n"
               "       knu1f N type max threads K first last          one shard of the K-row max pass\n"
               "       knu1f N type extend classes.txt [threads]       ways to add one row to each class\n"
               "       knu1f N type continue threads f1 [f2 ...]       complete each class to N-1 rows\n"
               "       knu1f merge out.txt in1.txt [in2.txt ...]\n");
        return 2;
    }
    N = atoi(argv[1]);
    std::vector<int> cycles;
    std::stringstream ss(argv[2]);
    for (std::string s; std::getline(ss, s, '.');) cycles.push_back(atoi(s.c_str()));
    std::sort(cycles.begin(), cycles.end());
    int sum = 0;
    bool ok = N >= 4 && N <= 32 && N % 2 == 0;
    for (int c : cycles) { sum += c; ok = ok && c >= 4 && c % 2 == 0; }
    maxMode = argc > 3 && std::string(argv[3]) == "max";
    const bool extendMode = argc > 4 && std::string(argv[3]) == "extend";
    const bool continueMode = argc > 5 && std::string(argv[3]) == "continue";
    rowsWanted = argc > 3 && !maxMode && !extendMode && !continueMode ? atoi(argv[3]) : N - 1;
    const int threads = extendMode ? (argc > 5 ? atoi(argv[5]) : 8) : argc > 4 ? atoi(argv[4]) : 8;
    if (argc > 5 && !continueMode) holdLimit = (size_t)atoll(argv[5]);
    if (continueMode && (cycle4First || fixedRows || smallRows)) {
        printf("continue needs the full candidate lists: unset U1F_CYCLE4FIRST, U1F_FIXEDROWS, U1F_SMALLROWS\n");
        return 2;
    }
    if (!ok || sum != N || rowsWanted < 3 || rowsWanted > N - 1 || threads < 1) {
        printf("bad arguments: N even 4..32, type = even parts >= 4 summing to N, 3 <= rows <= N-1\n");
        return 2;
    }

    allVerts = N == 32 ? 0xFFFFFFFFu : (1u << N) - 1;
    {   // fixed-point counts of the powers of b o a that decide the type (see compatPow)
        laneMask = allVerts;
        alignas(32) int8_t id[32];
        for (int i = 0; i < 32; i++) id[i] = (int8_t)i;
        laneId = _mm256_load_si256((const __m256i*)id);
        int l = 1;
        for (int c : cycles) l = std::lcm(l, c / 2);
        powDiv.clear();
        powReq.clear();
        for (int d = 1; d <= l; d++)
            if (l % d == 0) {
                int req = 0;
                for (int c : cycles) if (d % (c / 2) == 0) req += c;
                powDiv.push_back(d);
                powReq.push_back(req);
            }
    }
    int v = 0;
    for (int c : cycles) {
        typeLens |= 1u << (c / 2);
        typeKey += 1ULL << (4 * (c / 2 - 1));
        for (int i = 0; i < c; i += 2) {
            const int a = v + i, b = v + i + 1, d = v + (i + 2) % c;
            R1[a] = (int8_t)b; R1[b] = (int8_t)a;
            R2[b] = (int8_t)d; R2[d] = (int8_t)b;
        }
        v += c;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto seconds = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };

    parts.resize(N - 3);
    {
        std::vector<int> partners;
        for (int p = 1; p < N; p++)
            if (p != R1[0] && p != R2[0]) partners.push_back(p);
        std::vector<std::thread> pool;
        for (size_t i = 0; i < partners.size(); i++)
            pool.emplace_back([&, i] {
                Match m{};
                m[0] = (int8_t)partners[i];
                m[partners[i]] = 0;
                listMatchings(m, 1u | (1u << partners[i]), parts[i]);
            });
        for (auto& t : pool) t.join();
    }
    const std::string tag = std::string(argv[2]) + (cycle4First ? "_c4first" : "");
    if (cycle4First) {
        if (cycles[0] != 4) { printf("U1F_CYCLE4FIRST needs a 4-cycle in the type\n"); return 2; }
        // 0 -m-> p -R1-> q -m-> R1(0) -R1-> 0 closes a 4-cycle exactly when m[q] = R1[0].
        for (auto& p : parts)
            p.erase(std::remove_if(p.begin(), p.end(),
                [](const Match& m) { return m[R1[m[0]]] != R1[0]; }), p.end());
        printf("U1F_CYCLE4FIRST: SUBSET search, only candidates with vertex 0 on a 4-cycle with row 1\n");
    }
    std::sort(parts.begin(), parts.end(),
        [](const std::vector<Match>& a, const std::vector<Match>& b) { return a.size() < b.size(); });
    if (fixedRows) {
        if (maxMode || rowsWanted >= N - 1) { printf("U1F_FIXEDROWS needs rows < N-1, not max\n"); return 2; }
        parts.resize(rowsWanted - 2);
        printf("U1F_FIXEDROWS: existence test on the %d rows with the fewest candidates, partners of 0:",
            rowsWanted - 2);
        for (const auto& p : parts) printf(" %d(%zu)", p.empty() ? -1 : p[0][0], p.size());
        printf("\n");
    }

    size_t candidates = 0;
    for (const auto& p : parts) candidates += p.size();

    if (continueMode) {
        buildPairGroup(cycles);
        return runContinue(argc, argv, threads);
    }

    if (extendMode) {
        // knu1f N type extend file.txt [threads]: for each class in the file, the number of
        // matchings compatible with all its rows, i.e. of ways to add one more row.
        std::string ftype;
        int k;
        std::vector<ClassRecord> in;
        if (!readClassFile(argv[4], ftype, k, in)) return 3;
        if (ftype != argv[2]) { printf("%s is type %s, not %s\n", argv[4], ftype.c_str(), argv[2]); return 2; }
        std::vector<const Match*> all;
        for (const auto& p : parts)
            for (const Match& m : p) all.push_back(&m);
        std::vector<uint64_t> ways(in.size(), 0);
        std::atomic<size_t> next{0};
        std::vector<std::thread> pool;
        for (int t = 0; t < threads; t++)
            pool.emplace_back([&] {
                for (size_t i; (i = next.fetch_add(1)) < in.size();) {
                    // Relabel so that the first two rows are R1 and R2; the candidates are then
                    // exactly the matchings compatible with those two.
                    const auto sigma = toStandard(in[i].rows[0], in[i].rows[1]);
                    std::vector<Match> rest;
                    for (int r = 2; r < k; r++) rest.push_back(applyPerm(sigma, in[i].rows[r]));
                    for (const Match* c : all) {
                        bool good = true;
                        for (const Match& m : rest)
                            if (!compat(*c, m)) { good = false; break; }
                        ways[i] += good;
                    }
                }
            });
        for (auto& t : pool) t.join();
        uint64_t total = 0;
        size_t extendable = 0;
        for (size_t i = 0; i < in.size(); i++) {
            if (ways[i]) printf("  class #%zu: %llu ways to add a row\n", i + 1, (unsigned long long)ways[i]);
            total += ways[i];
            extendable += ways[i] > 0;
        }
        printf("RESULT K%d type %s, %d rows, %s: %zu of %zu classes extend to %d rows (%llu ways), %.1f s\n",
            N, argv[2], k, argv[4], extendable, in.size(), k + 1, (unsigned long long)total, seconds());
        return 0;
    }

    const int need = rowsWanted - 2;            // rows still to place, the root among them
    if (maxMode || rowsWanted == N - 1) {
        // Orbits of all candidates under the symmetries of the fixed pair.
        std::map<Match, int> index;
        std::vector<int> uf(candidates);
        int id = 0;
        for (const auto& p : parts)
            for (const Match& m : p) index[m] = id++;
        std::iota(uf.begin(), uf.end(), 0);
        const auto find = [&](int x) {
            while (uf[x] != x) x = uf[x] = uf[uf[x]];
            return x;
        };
        auto orbitGens = pairSymmetries(cycles);
        if (cycle4First) {
            // The subset is kept only by the symmetries fixing R1, R2 and vertex 0: list the
            // group fixing R1 and R2 (all generators but the exchange) and keep those elements.
            auto g0 = orbitGens;
            g0.pop_back();
            std::array<int8_t, 32> e0{};
            for (int v = 0; v < N; v++) e0[v] = (int8_t)v;
            std::set<std::array<int8_t, 32>> seen = { e0 };
            std::vector<std::array<int8_t, 32>> all = { e0 };
            for (size_t i = 0; i < all.size(); i++)
                for (const auto& g : g0) {
                    std::array<int8_t, 32> c{};
                    for (int v = 0; v < N; v++) c[v] = g[all[i][v]];
                    if (seen.insert(c).second) all.push_back(c);
                }
            orbitGens.clear();
            for (const auto& h : all) if (h[0] == 0) orbitGens.push_back(h);
        }
        for (const auto& g : orbitGens)
            for (const auto& e : index) {
                const auto it = index.find(applyPerm(g, e.first));
                if (it == index.end()) {
                    printf("internal error: a pair symmetry left the candidate list\n");
                    return 3;
                }
                uf[find(e.second)] = find(it->second);
            }

        // Root on the row that meets the fewest orbits. Every factorization has a row there,
        // and a symmetry takes that row to the first member of its orbit in the same row.
        int bestPart = 0;
        size_t bestCount = SIZE_MAX, orbits = 0;
        for (int x = 0; x < (int)candidates; x++) orbits += find(x) == x;
        std::vector<int> mark(candidates, -1);
        if (maxMode) {
            // One job per orbit, rooted at the orbit's first member.
            std::vector<int> number(candidates, -1);
            int nextOrbit = 0;
            orbitOf.resize(parts.size());
            for (int p = 0; p < (int)parts.size(); p++) {
                orbitOf[p].resize(parts[p].size());
                for (size_t i = 0; i < parts[p].size(); i++) {
                    const int o = find(index[parts[p][i]]);
                    if (number[o] < 0) {
                        number[o] = nextOrbit++;
                        jobs.push_back({ &parts[p][i], p, number[o] });
                    }
                    orbitOf[p][i] = number[o];
                }
            }
        }
        else {
            for (int p = 0; p < (int)parts.size(); p++) {
                size_t n = 0;
                for (const Match& m : parts[p]) {
                    const int o = find(index[m]);
                    if (mark[o] != p) { mark[o] = p; n++; }
                }
                if (n < bestCount) { bestCount = n; bestPart = p; }
            }
            std::swap(parts[0], parts[bestPart]);
            std::vector<char> taken(candidates, 0);
            for (const Match& m : parts[0]) {
                const int o = find(index[m]);
                if (!taken[o]) { taken[o] = 1; jobs.push_back({ &m, 0 }); }
            }
        }
        printf("%zu orbits of candidates under the fixed pair's symmetries\n", orbits);

        // The group fixing R1 and R2: all generators except the last, which exchanges them.
        auto gens = pairSymmetries(cycles);
        gens.pop_back();
        std::array<int8_t, 32> identity{};
        for (int i = 0; i < N; i++) identity[i] = (int8_t)i;
        std::set<std::array<int8_t, 32>> group = { identity };
        pairGroup = { identity };
        for (size_t i = 0; i < pairGroup.size(); i++)
            for (const auto& g : gens) {
                std::array<int8_t, 32> c{};
                for (int v = 0; v < N; v++) c[v] = g[pairGroup[i][v]];
                if (group.insert(c).second) pairGroup.push_back(c);
            }
    }
    else {
        for (int i = 0; i < (int)parts.size() && (int)parts.size() - i >= need; i++)
            for (const Match& m : parts[i]) jobs.push_back({ &m, i });
    }
    printf("K%d type %s, %d rows: %zu candidates, %zu root jobs, %d threads\n",
        N, argv[2], rowsWanted, candidates, jobs.size(), threads);
    fflush(stdout);
    std::string jobRange;
    if (fixedRows && getenv("U1F_JOBS")) {
        size_t first = 0, last = 0;
        if (sscanf(getenv("U1F_JOBS"), "%zu %zu", &first, &last) != 2) { printf("U1F_JOBS=\"first last\"\n"); return 2; }
        const size_t total = jobs.size();
        first = std::min(first, total);
        last = std::min(last, total);
        if (first >= last) { printf("U1F_JOBS: first < last needed\n"); return 2; }
        jobs = std::vector<Job>(jobs.begin() + first, jobs.begin() + last);
        jobBase = first;
        jobRange = ", jobs " + std::to_string(first) + "-" + std::to_string(last) + " of " + std::to_string(total);
    }

    if (maxMode && argc > 7) {
        // One shard: every class with exactly K rows whose root is a job in [first, last).
        // The shards of all jobs together give all K-row classes; merge them afterwards.
        const int K = atoi(argv[5]);
        const size_t total = jobs.size();
        const size_t first = std::min((size_t)atoll(argv[6]), total);
        const size_t last = std::min((size_t)atoll(argv[7]), total);
        if (K < 3 || K > N - 1 || first >= last) { printf("bad shard: K 3..N-1, first < last\n"); return 2; }
        jobs = std::vector<Job>(jobs.begin() + first, jobs.begin() + last);
        jobBase = first;
        rowsWanted = K;
        collecting = !smallRows || smallKeep;
        const std::string smallName = "K" + std::to_string(N) + "_U1F_small_" + tag + "_k" + std::to_string(K) +
            "_" + std::to_string(first) + "-" + std::to_string(last) + ".txt";
        if (smallKeep) openSmall(smallName, K);
        runPass(threads);
        if (smallKeep) {
            if (smallPart) fclose(smallPart);
            if (smallDone) fclose(smallDone);
            smallPart = smallDone = nullptr;
            if (!writeClasses(smallClasses, smallName, argv[2], K)) return 3;
            printf("RESULT K%d type %s%s, %d smallest rows per job, jobs %zu-%zu of %zu: %zu jobs HIT (this run),"
                " %zu classes (%llu labelled finds this run), %.1f s, in %s; %s\n", N, argv[2],
                cycle4First ? " c4first" : "", K, first, last, total, jobsHit.load(), smallClasses.size(),
                (unsigned long long)totalFound.load(), seconds(), smallName.c_str(), depthLine().c_str());
            return 0;
        }
        if (smallRows) {
            printf("RESULT K%d type %s%s, %d smallest rows per job, jobs %zu-%zu of %zu: %zu jobs HIT, %zu have no U1F, %.1f s; %s\n",
                N, argv[2], cycle4First ? " c4first" : "", K, first, last, total, jobsHit.load(),
                (last - first) - jobsHit.load(), seconds(), depthLine().c_str());
            const std::string name = "K" + std::to_string(N) + "_U1F_small_" + tag + "_k" + std::to_string(K) +
                "_" + std::to_string(first) + "-" + std::to_string(last) + ".txt";
            if (FILE* f = fopen(name.c_str(), "w")) {
                fprintf(f, "K%d type %s%s, U1F_SMALLROWS K=%d, jobs %zu-%zu of %zu: %zu jobs HIT\n", N, argv[2],
                    cycle4First ? " c4first" : "", K, first, last, total, jobsHit.load());
                fclose(f);
            }
            return 0;
        }
        const std::string name = "K" + std::to_string(N) + "_U1F_max_" + tag + "_k" + std::to_string(K) +
            "_" + std::to_string(first) + "-" + std::to_string(last) + ".txt";
        if (!writeClasses(results.classes, name, argv[2], K)) return 3;
        printf("RESULT K%d type %s, %d rows, jobs %zu-%zu of %zu: %zu classes, %.1f s, in %s; %s\n", N, argv[2], K,
            first, last, total, results.classes.size(), seconds(), name.c_str(), depthLine().c_str());
        return 0;
    }

    if (maxMode) {
        int largest = 2;
        for (int k = 3; k <= N - 1; k++) {
            rowsWanted = k;
            runPass(threads);
            const bool exists = totalFound.load() > 0;
            printf("  %d rows: %s (%.1f s); %s\n", k, exists ? "exist" : "none", seconds(), depthLine().c_str());
            fflush(stdout);
            if (!exists) break;
            largest = k;
        }
        if (largest < 3) { printf("RESULT K%d type %s: no partial with 3 rows\n", N, argv[2]); return 0; }

        rowsWanted = largest;
        collecting = true;
        runPass(threads);

        uint64_t copies = 0;
        for (const ClassRecord& c : results.classes)
            copies += largest * (uint64_t)(largest - 1) * pairGroup.size() / c.aut;
        const std::string name = "K" + std::to_string(N) + "_U1F_max_" + tag + ".txt";
        if (!writeClasses(results.classes, name, argv[2], largest)) return 3;
        printf("RESULT K%d type %s: largest %d rows, %zu classes, %llu labelled copies, %.1f s, in %s\n",
            N, argv[2], largest, results.classes.size(), (unsigned long long)copies, seconds(), name.c_str());
        return 0;
    }

    collecting = trackDeep = rowsWanted == N - 1;
    runPass(threads);
    if (collecting) {
        const std::string tail = std::string("_") + tag + ".txt";
        const std::string prefix = "K" + std::to_string(N) + "_U1F";
        if (!results.classes.empty()) {
            const std::string name = prefix + tail;
            if (!writeClasses(results.classes, name, argv[2], rowsWanted)) return 3;
            printf("Classes written to %s, |Aut|:", name.c_str());
            for (const ClassRecord& c : results.classes) printf(" %llu", (unsigned long long)c.aut);
            printf("\n");
        }
        const std::string deepName = prefix + "_deepest" + tail;
        if (!writeClasses(deep.classes, deepName, argv[2], deepest.load())) return 3;
        printf("Deepest reached: %d rows, %s%zu classes held, in %s\n", deepest.load(),
            deepFull.load() ? "at least " : "", deep.classes.size(), deepName.c_str());
        printf("RESULT K%d type %s, %d rows: %zu classes (nodes %llu, %.1f s)\n", N, argv[2], rowsWanted,
            results.classes.size(), (unsigned long long)totalNodes.load(), seconds());
        return 0;
    }
    if (fixedRows) {
        printf("RESULT K%d type %s%s, %d fixed rows%s: %s (nodes %llu, %.1f s); %s\n", N, argv[2],
            cycle4First ? " c4first" : "", rowsWanted, jobRange.c_str(),
            totalFound.load() ? "a partial EXISTS (proves nothing)"
                : jobRange.empty() && !cycle4First ? "NONE, so no U1F of this type" : "none in this part",
            (unsigned long long)totalNodes.load(), seconds(), depthLine().c_str());
        return 0;
    }
    printf("RESULT K%d type %s, %d rows: found %llu (nodes %llu, %.1f s)\n", N, argv[2], rowsWanted,
        (unsigned long long)totalFound.load(), (unsigned long long)totalNodes.load(), seconds());
    return 0;
}

// End of job, spoken as in Tt4: OK or not. U1F_QUIET=1 turns it off (shards).
int main(int argc, char** argv) {
    const int code = run(argc, argv);
    const char* quiet = std::getenv("U1F_QUIET");
    if (!(quiet && *quiet && *quiet != '0'))
        speak(code ? "Oops. k task error" : "k task exit ok");
    return code;
}
