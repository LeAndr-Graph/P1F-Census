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
// "found" is then not a count of factorizations; only zero or non-zero means anything.

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

struct Job { const Match* root; int firstPart; };

static std::vector<std::vector<Match>> parts;   // sorted by size
static std::vector<Job> jobs;
static int rowsWanted;
static std::atomic<size_t> nextJob{0}, jobsDone{0};
static std::atomic<uint64_t> totalFound{0}, totalNodes{0};
static std::mutex printLock;
static bool printed = false;

static void printSolution(const std::vector<const Match*>& rows) {
    std::lock_guard<std::mutex> g(printLock);
    if (printed) return;
    printed = true;
    printf("First result found:\n");
    for (const Match* m : rows) {
        for (int v = 0; v < N; v++)
            if ((*m)[v] > v) printf(" %2d %2d ", v, (int)(*m)[v]);
        printf("\n");
    }
    fflush(stdout);
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

    void record(int L, int nAct) {
        std::vector<const Match*> rows = { &R1, &R2, root };
        rows.insert(rows.end(), chosen.begin(), chosen.end());
        for (int j = 0; j < nAct; j++) {
            const Range& r = act[L][j];
            for (int w = r.first; w < r.first + r.words; w++)
                if (alive[(size_t)L * W + w]) {
                    rows.push_back(cand[64 * w + ctz64(alive[(size_t)L * W + w])]);
                    printSolution(rows);
                    return;
                }
        }
    }

    // act[L][0..nAct) are rows with at least one candidate; nAct >= need >= 1.
    void dfs(int L, int nAct, int need) {
        nodes++;
        if (need == 1) {
            for (int j = 0; j < nAct; j++) found += act[L][j].count;
            if (!printed) record(L, nAct);
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
                const int v = 64 * w + ctz64(bits);
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
        if (need == 0) { found++; return; }

        std::vector<std::vector<const Match*>> local;
        int words = 0, nonEmpty = 0;
        std::vector<Range> ranges;
        for (size_t p = job.firstPart + 1; p < parts.size(); p++) {
            std::vector<const Match*> keep;
            for (const Match& m : parts[p])
                if (compat(*root, m)) keep.push_back(&m);
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
            if (i >= jobs.size()) return;
            found = nodes = 0;
            runJob(jobs[i]);
            totalFound += found;
            totalNodes += nodes;
            jobsDone++;
        }
    }
};

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: k18u1f N type [rows] [threads]   e.g. k18u1f 18 4.14\n");
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
    rowsWanted = argc > 3 ? atoi(argv[3]) : N - 1;
    const int threads = argc > 4 ? atoi(argv[4]) : 8;
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
    if (rowsWanted == N - 1) {
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
        printf("%zu orbits of candidates under the fixed pair's symmetries\n", orbits);
    }
    else {
        for (int i = 0; i < (int)parts.size() && (int)parts.size() - i >= need; i++)
            for (const Match& m : parts[i]) jobs.push_back({ &m, i });
    }
    printf("K%d type %s, %d rows: %zu candidates, %zu root jobs, %d threads\n",
        N, argv[2], rowsWanted, candidates, jobs.size(), threads);
    fflush(stdout);

    std::vector<std::thread> pool;
    for (int i = 0; i < threads; i++)
        pool.emplace_back([] { auto w = std::make_unique<Worker>(); w->run(); });

    double lastPrint = 0;
    while (jobsDone.load() < jobs.size()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (seconds() - lastPrint >= 60) {
            lastPrint = seconds();
            const size_t d = jobsDone.load();
            printf("  %.0f s: %zu of %zu jobs, found %llu\n", lastPrint, d, jobs.size(),
                (unsigned long long)totalFound.load());
            fflush(stdout);
        }
    }
    for (auto& t : pool) t.join();

    printf("RESULT K%d type %s, %d rows: found %llu (nodes %llu, %.1f s)\n", N, argv[2], rowsWanted,
        (unsigned long long)totalFound.load(), (unsigned long long)totalNodes.load(), seconds());
    return 0;
}
