// p1f_check -- validate a KnA2 RESULT file: every record a P1F, |Aut| as recorded, no two records of one class.
//
// Written 2026-09-10 to certify box results produced by a build that predates the legacyWalk() race fix
// (KnA2 16fdcf8): the race could only ever write a record from a WRONG canonical key -- an invalid matrix,
// or a class recorded twice under two labelings. Both show here; nothing else about such a run is suspect.
//
// Checks per record:  17 rows, each a perfect matching; every pair of rows Hamiltonian (a P1F);
//                     |Aut| recomputed by the frame method and compared with the header;
//                     a canonical form (lexicographically least relabeling over all Hamiltonian frames,
//                     rows sorted) -- two records with the same form are the same class.
//
// Usage: p1f_check <result.txt> [--quiet]
// Exit 0 = clean, 1 = a fault was found, 2 = usage / unreadable.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
using namespace std;

static int N = 0, NM = 0;
typedef vector<int> Match;
struct Rec { int id; int aut; vector<Match> fac; };

static bool readCat(const char* path, vector<Rec>& out, int& badRows) {
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); return false; }
    char line[65536];
    int rec = -1, aut = 0; vector<Match> fac;
    auto flush = [&]() { if (rec >= 0 && !fac.empty()) { if ((int)fac.size() != NM) badRows++; Rec r; r.id = rec; r.aut = aut; r.fac = fac; out.push_back(r); } rec = -1; fac.clear(); };
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') { flush(); rec = atoi(line + 1); aut = 0; const char* p = strstr(line, "="); if (p) aut = atoi(p + 1); continue; }
        if (!strchr(line, '"') || rec < 0) continue;
        vector<int> v;
        for (char* p = line; *p; ) { if (*p >= '0' && *p <= '9') v.push_back((int)strtol(p, &p, 10)); else p++; }
        if (v.empty()) continue;
        if (N == 0) { N = (int)v.size(); NM = N - 1; }
        if ((int)v.size() != N) { badRows++; continue; }
        Match m(N, -1);
        for (int i = 0; i < N; i += 2) { if (v[i] < 0 || v[i] >= N || v[i+1] < 0 || v[i+1] >= N) { badRows++; m.clear(); break; } m[v[i]] = v[i+1]; m[v[i+1]] = v[i]; }
        if (m.empty()) continue;
        fac.push_back(m);
    }
    flush();
    fclose(f);
    return true;
}

static bool walk(const Match& A, const Match& B, int start, vector<int>& seq) {
    static vector<char> seen; seen.assign(N, 0);
    int cur = start, pos = 0;
    for (int t = 0; t < N / 2; t++) {
        if (seen[cur]) return false; seen[cur] = 1; seq[pos++] = cur; cur = A[cur];
        if (seen[cur]) return false; seen[cur] = 1; seq[pos++] = cur; cur = B[cur];
    }
    return cur == start;
}

// P1F validity: every row a perfect matching, rows edge-disjoint, every pair Hamiltonian
static const char* validP1F(const vector<Match>& X) {
    if ((int)X.size() != NM) return "row count";
    vector<vector<char> > used(N, vector<char>(N, 0));
    for (int k = 0; k < NM; k++) {
        for (int u = 0; u < N; u++) { const int v = X[k][u]; if (v < 0 || v >= N || v == u || X[k][v] != u) return "not a perfect matching"; }
        for (int u = 0; u < N; u++) { const int v = X[k][u]; if (u < v) { if (used[u][v]) return "repeated edge"; used[u][v] = 1; } }
    }
    vector<int> seq(N);
    for (int a = 0; a < NM; a++) for (int b = a + 1; b < NM; b++) if (!walk(X[a], X[b], 0, seq)) return "a row pair is not Hamiltonian";
    return nullptr;
}

// frame method: relabel so that the walk of (row i, row j) from a becomes 0,1,2,...; the canonical form is
// the least (rows sorted, compared lexicographically) over all Hamiltonian frames; |Aut| = how many frames
// reproduce the record's own frame image (equivalently, the number of frames attaining the minimum / ... we
// count the automorphisms directly: maps that carry every row onto a row).
static void relabelSorted(const vector<Match>& X, const vector<int>& phi, vector<Match>& out) {
    out.assign(NM, Match(N));
    for (int k = 0; k < NM; k++) for (int u = 0; u < N; u++) out[k][phi[u]] = phi[X[k][u]];
    sort(out.begin(), out.end());
}
static void canonAndAut(const vector<Match>& X, vector<Match>& best, int& aut) {
    vector<vector<int> > edgeFac(N, vector<int>(N, -1));
    for (int k = 0; k < NM; k++) for (int u = 0; u < N; u++) edgeFac[u][X[k][u]] = k;
    vector<int> base(N), seq(N), phi(N), inv(N);
    aut = 0; bool first = true; vector<Match> cur;
    if (!walk(X[0], X[1], 0, base)) { best.clear(); return; }
    for (int i = 0; i < NM; i++) for (int j = 0; j < NM; j++) {
        if (i == j) continue;
        for (int a = 0; a < N; a++) {
            if (!walk(X[i], X[j], a, seq)) continue;
            // phi maps the frame (i,j,a) onto the labeling 0..N-1 in walk order
            for (int t = 0; t < N; t++) phi[seq[t]] = t;
            relabelSorted(X, phi, cur);
            if (first || cur < best) { best = cur; first = false; }
            // automorphism test: the map base-walk -> this walk carries rows onto rows
            for (int t = 0; t < N; t++) inv[base[t]] = seq[t];
            bool ok = true;
            for (int k = 0; k < NM && ok; k++) {
                const int c = edgeFac[inv[0]][inv[X[k][0]]];
                if (c < 0) { ok = false; break; }
                for (int u = 1; u < N; u++) if (X[c][inv[u]] != inv[X[k][u]]) { ok = false; break; }
            }
            if (ok) aut++;
        }
    }
}

static string keyOf(const vector<Match>& F) {
    string s; s.reserve(NM * N);
    for (int k = 0; k < NM; k++) for (int u = 0; u < N; u++) s.push_back((char)('0' + F[k][u]));
    return s;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: p1f_check <result.txt> [--quiet]\n"); return 2; }
    const bool quiet = (argc > 2 && !strcmp(argv[2], "--quiet"));
    vector<Rec> R; int badRows = 0;
    if (!readCat(argv[1], R, badRows)) return 2;
    printf("%s: K%d, %d records read, %d malformed row(s)\n", argv[1], N, (int)R.size(), badRows);
    long long invalid = 0, autMismatch = 0;
    map<string, vector<int> > byKey; map<int, long long> autHist;
    for (size_t r = 0; r < R.size(); r++) {
        const char* why = validP1F(R[r].fac);
        if (why) { invalid++; if (!quiet) printf("  record #%d: INVALID -- %s\n", R[r].id, why); continue; }
        vector<Match> best; int aut = 0;
        canonAndAut(R[r].fac, best, aut);
        autHist[aut]++;
        if (aut != R[r].aut) { autMismatch++; if (!quiet) printf("  record #%d: |Aut| recorded %d, recomputed %d\n", R[r].id, R[r].aut, aut); }
        byKey[keyOf(best)].push_back(R[r].id);
        if (!quiet && (r + 1) % 5000 == 0) { printf("  ... %zu of %zu\n", r + 1, R.size()); fflush(stdout); }
    }
    long long dupClasses = 0, dupRecords = 0;
    for (auto& kv : byKey) if (kv.second.size() > 1) {
        dupClasses++; dupRecords += (long long)kv.second.size() - 1;
        if (!quiet) { printf("  DUPLICATE class: records"); for (int id : kv.second) printf(" #%d", id); printf("\n"); }
    }
    printf("valid P1Fs %lld, invalid %lld, |Aut| mismatches %lld, distinct classes %zu, classes recorded more than once %lld (%lld extra records)\n",
           (long long)R.size() - invalid, invalid, autMismatch, byKey.size(), dupClasses, dupRecords);
    printf("recomputed |Aut| histogram:"); for (auto& kv : autHist) printf(" %d:%lld", kv.first, kv.second); printf("\n");
    const bool fault = invalid || autMismatch || dupClasses || badRows;
    printf("%s\n", fault ? "FAULT" : "CLEAN");
    return fault ? 1 : 0;
}
