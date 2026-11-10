// k18u1f: exhaustive search for uniform 1-factorizations of K_N of one cycle type.
//
//   k18u1f N type [rows] [threads]      e.g.  k18u1f 18 4.14
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
// of the fixed pair: the row through one chosen edge at vertex 0 is tried once per orbit. Its
// Each isomorphism class found goes to K<N>_U1F_<type>.txt with its |Aut|. It also keeps the partials
// with the most rows the search reached, up to `hold` classes, in K<N>_U1F_deepest_<type>.txt. The
// search abandons a branch once any row is out of candidates, so that size is not the maximum.
//
//   k18u1f N type max [threads]
//
// finds the largest number of rows any partial factorization of the type reaches, and writes one
// record per isomorphism class at that size, with |Aut|, to K<N>_U1F_max_<type>.txt. Orbits of
// candidates are numbered, and the root of a job only combines with candidates of its own or a
// later orbit: a symmetry takes any partial to one whose smallest orbit is met at the root.

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
#include <thread>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
static inline int popcnt64(uint64_t x) { return (int)__popcnt64(x); }
static inline int ctz64(uint64_t x) { unsigned long i; _BitScanForward64(&i, x); return (int)i; }
#else
static inline int popcnt64(uint64_t x) { return __builtin_popcountll(x); }
static inline int ctz64(uint64_t x) { return __builtin_ctzll(x); }
#endif

using Match = std::array<int8_t, 32>;   // Match[v] = partner of v

static int N;
static uint32_t allVerts;
static uint32_t typeLens;               // bit c/2 set when cycle length c occurs in the type
static uint64_t typeKey;                // one 4-bit count per cycle length
static Match R1, R2;

// True when the union of a and b has exactly the required cycle type. A shared edge is a cycle
// of length 2, which no type contains, so edge-disjointness needs no separate test.
static bool compat(const Match& a, const Match& b) {
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
static int rowsWanted;
static bool maxMode = false, collecting = false;
static std::atomic<bool> stopPass{false};
static std::atomic<int> running{0};
static std::atomic<size_t> nextJob{0}, jobsDone{0};
static std::atomic<uint64_t> totalFound{0}, totalNodes{0};

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

class Worker {
    int W = 0;                                  // words per bit set
    std::vector<const Match*> cand;             // bit -> matching
    std::vector<uint64_t> adj;                  // W words per bit
    std::vector<uint64_t> alive;                // W words per level
    std::vector<std::array<Range, 32>> act;     // active rows per level
    std::vector<const Match*> chosen;
    const Match* root = nullptr;
    uint64_t found = 0, nodes = 0;

    void collectAll(int L, int nAct) {
        std::vector<const Match*> rows = { &R1, &R2, root };
        rows.insert(rows.end(), chosen.begin(), chosen.end());
        rows.push_back(nullptr);
        for (int j = 0; j < nAct; j++) {
            const Range& r = act[L][j];
            for (int w = r.first; w < r.first + r.words; w++)
                for (uint64_t bits = alive[(size_t)L * W + w]; bits; bits &= bits - 1) {
                    rows.back() = cand[64 * w + ctz64(bits)];
                    addPartial(rows);
                    found++;
                }
        }
    }

    // act[L][0..nAct) are rows with at least one candidate; nAct >= need >= 1.
    void dfs(int L, int nAct, int need) {
        nodes++;
        if (need == 1) {
            if (collecting) { collectAll(L, nAct); return; }
            for (int j = 0; j < nAct; j++) found += act[L][j].count;
            if (maxMode) stopPass = true;
            return;
        }
        int best = 0;
        for (int j = 1; j < nAct; j++)
            if (act[L][j].count < act[L][best].count) best = j;
        const Range branch = act[L][best];
        const uint64_t* cur = &alive[(size_t)L * W];
        uint64_t* nxt = &alive[(size_t)(L + 1) * W];

        for (int w = branch.first; w < branch.first + branch.words; w++) {
            for (uint64_t bits = cur[w]; bits; bits &= bits - 1) {
                if (stopPass.load(std::memory_order_relaxed)) return;
                const int v = 64 * w + ctz64(bits);
                if (trackDeep) noteDeep(root, chosen, cand[v]);
                const uint64_t* a = &adj[(size_t)v * W];
                int n = 0, empties = 0;
                const int allowed = nAct - need;
                for (int j = 0; j < nAct && empties <= allowed; j++) {
                    if (j == best) continue;
                    Range r = act[L][j];
                    int c = 0;
                    for (int u = r.first; u < r.first + r.words; u++) {
                        nxt[u] = cur[u] & a[u];
                        c += popcnt64(nxt[u]);
                    }
                    if (c) { r.count = c; act[L + 1][n++] = r; }
                    else empties++;
                }
                if (empties > allowed) continue;
                chosen.push_back(cand[v]);
                dfs(L + 1, n, need - 1);
                chosen.pop_back();
            }
        }

        if (nAct - 1 >= need) {                 // sets with no row from this branch
            int n = 0;
            for (int j = 0; j < nAct; j++) {
                if (j == best) continue;
                const Range& r = act[L][j];
                std::copy(cur + r.first, cur + r.first + r.words, nxt + r.first);
                act[L + 1][n++] = r;
            }
            dfs(L + 1, n, need);
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

        W = words;
        cand.assign((size_t)W * 64, nullptr);
        adj.assign((size_t)W * 64 * W, 0);
        alive.assign((size_t)W * (N + 2), 0);
        if (act.size() < (size_t)N + 2) act.resize((size_t)N + 2);

        for (size_t i = 0; i < local.size(); i++)
            for (size_t k = 0; k < local[i].size(); k++) {
                const int b = 64 * ranges[i].first + (int)k;
                cand[b] = local[i][k];
                alive[b / 64] |= 1ULL << (b % 64);
            }
        for (size_t i = 0; i < local.size(); i++)
            for (size_t k = i + 1; k < local.size(); k++)
                for (size_t x = 0; x < local[i].size(); x++)
                    for (size_t y = 0; y < local[k].size(); y++)
                        if (compat(*local[i][x], *local[k][y])) {
                            const int bx = 64 * ranges[i].first + (int)x;
                            const int by = 64 * ranges[k].first + (int)y;
                            adj[(size_t)bx * W + by / 64] |= 1ULL << (by % 64);
                            adj[(size_t)by * W + bx / 64] |= 1ULL << (bx % 64);
                        }

        for (int j = 0; j < nonEmpty; j++) act[0][j] = ranges[j];
        dfs(0, nonEmpty, need);
    }

public:
    void run() {
        for (;;) {
            const size_t i = nextJob.fetch_add(1);
            if (i >= jobs.size() || stopPass.load()) return;
            found = nodes = 0;
            runJob(jobs[i]);
            totalFound += found;
            totalNodes += nodes;
            jobsDone++;
        }
    }
};

// Run all jobs for the current rowsWanted on `threads` threads, with a progress line each minute.
static void runPass(int threads) {
    nextJob = 0;
    jobsDone = 0;
    totalFound = 0;
    totalNodes = 0;
    stopPass = false;
    running = threads;
    std::vector<std::thread> pool;
    for (int i = 0; i < threads; i++)
        pool.emplace_back([] { auto w = std::make_unique<Worker>(); w->run(); running--; });
    const auto t0 = std::chrono::steady_clock::now();
    double lastPrint = 0;
    while (running.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (s - lastPrint >= 60) {
            lastPrint = s;
            printf("  %.0f s: %zu of %zu jobs, found %llu\n", s, jobsDone.load(), jobs.size(),
                (unsigned long long)totalFound.load());
            fflush(stdout);
        }
    }
    for (auto& t : pool) t.join();
}

// k18u1f merge out.txt in1.txt in2.txt ... : union of class files of one type and size, each class
// once, sorted as a single run would write it. Classes are canonical, so equal classes have equal rows.
static int mergeFiles(int argc, char** argv) {
    std::set<std::vector<Match>> have;
    std::vector<ClassRecord> all;
    std::string type;
    int rows = -1;
    for (int a = 3; a < argc; a++) {
        FILE* f = fopen(argv[a], "r");
        if (!f) { printf("cannot read %s\n", argv[a]); return 3; }
        char t[64];
        size_t n;
        int k;
        if (fscanf(f, "=== type %63s %zu class(es) with %d rows\n", t, &n, &k) != 3) {
            printf("%s: not a class file\n", argv[a]); return 3;
        }
        std::string ts(t);
        if (!ts.empty() && ts.back() == ':') ts.pop_back();
        if (rows < 0) { type = ts; rows = k; }
        if (ts != type || k != rows) { printf("%s: type or rows differ from %s\n", argv[a], argv[3]); return 3; }
        for (size_t i = 0; i < n; i++) {
            size_t idx;
            unsigned long long aut;
            if (fscanf(f, " #%zu |Aut| = %llu", &idx, &aut) != 2) { printf("%s: bad record\n", argv[a]); return 3; }
            ClassRecord c{ {}, aut };
            for (int r = 0; r < k; r++) {
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
            if (have.insert(c.rows).second) all.push_back(c);
        }
        fclose(f);
    }
    if (rows < 0) { printf("no input files\n"); return 2; }
    if (!writeClasses(all, argv[2], type.c_str(), rows)) return 3;
    printf("RESULT merged %d files: type %s, %d rows, %zu classes, in %s\n", argc - 3, type.c_str(), rows,
        all.size(), argv[2]);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "merge") {
        if (argc < 4) { printf("usage: k18u1f merge out.txt in1.txt [in2.txt ...]\n"); return 2; }
        return mergeFiles(argc, argv);
    }
    if (argc < 3) {
        printf("usage: k18u1f N type [rows|max] [threads] [hold]        e.g. k18u1f 18 4.14\n"
               "       k18u1f N type max threads K first last          one shard of the K-row max pass\n"
               "       k18u1f merge out.txt in1.txt [in2.txt ...]\n");
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
    rowsWanted = argc > 3 && !maxMode ? atoi(argv[3]) : N - 1;
    const int threads = argc > 4 ? atoi(argv[4]) : 8;
    if (argc > 5) holdLimit = (size_t)atoll(argv[5]);
    if (!ok || sum != N || rowsWanted < 3 || rowsWanted > N - 1 || threads < 1) {
        printf("bad arguments: N even 4..32, type = even parts >= 4 summing to N, 3 <= rows <= N-1\n");
        return 2;
    }

    allVerts = N == 32 ? 0xFFFFFFFFu : (1u << N) - 1;
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
    std::sort(parts.begin(), parts.end(),
        [](const std::vector<Match>& a, const std::vector<Match>& b) { return a.size() < b.size(); });

    size_t candidates = 0;
    for (const auto& p : parts) candidates += p.size();
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
        for (const auto& g : pairSymmetries(cycles))
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

    if (maxMode && argc > 7) {
        // One shard: every class with exactly K rows whose root is a job in [first, last).
        // The shards of all jobs together give all K-row classes; merge them afterwards.
        const int K = atoi(argv[5]);
        const size_t total = jobs.size();
        const size_t first = std::min((size_t)atoll(argv[6]), total);
        const size_t last = std::min((size_t)atoll(argv[7]), total);
        if (K < 3 || K > N - 1 || first >= last) { printf("bad shard: K 3..N-1, first < last\n"); return 2; }
        jobs = std::vector<Job>(jobs.begin() + first, jobs.begin() + last);
        rowsWanted = K;
        collecting = true;
        runPass(threads);
        const std::string name = "K" + std::to_string(N) + "_U1F_max_" + argv[2] + "_k" + std::to_string(K) +
            "_" + std::to_string(first) + "-" + std::to_string(last) + ".txt";
        if (!writeClasses(results.classes, name, argv[2], K)) return 3;
        printf("RESULT K%d type %s, %d rows, jobs %zu-%zu of %zu: %zu classes, %.1f s, in %s\n", N, argv[2], K,
            first, last, total, results.classes.size(), seconds(), name.c_str());
        return 0;
    }

    if (maxMode) {
        int largest = 2;
        for (int k = 3; k <= N - 1; k++) {
            rowsWanted = k;
            runPass(threads);
            const bool exists = totalFound.load() > 0;
            printf("  %d rows: %s (%.1f s)\n", k, exists ? "exist" : "none", seconds());
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
        const std::string name = "K" + std::to_string(N) + "_U1F_max_" + argv[2] + ".txt";
        if (!writeClasses(results.classes, name, argv[2], largest)) return 3;
        printf("RESULT K%d type %s: largest %d rows, %zu classes, %llu labelled copies, %.1f s, in %s\n",
            N, argv[2], largest, results.classes.size(), (unsigned long long)copies, seconds(), name.c_str());
        return 0;
    }

    collecting = trackDeep = rowsWanted == N - 1;
    runPass(threads);
    if (collecting) {
        const std::string tail = std::string("_") + argv[2] + ".txt";
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
    printf("RESULT K%d type %s, %d rows: found %llu (nodes %llu, %.1f s)\n", N, argv[2], rowsWanted,
        (unsigned long long)totalFound.load(), (unsigned long long)totalNodes.load(), seconds());
    return 0;
}
