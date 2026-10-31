// Atomicity analysis of a P1F catalog file (Gill-Wanless framework, arXiv:1905.07535).
//
// Same mathematics as atomic_latin.cpp, but it reads a KnA2 AllResults catalog
// (`#N |Aut| = X` header followed by the factor rows) instead of one result file
// per class, and it takes the size from the data rather than from a #define, so
// the same source serves K14, K16, K18 and K20 with no size-specific path.
//
// For a P1F F of K_{2n}: build U(F) = symmetric unipotent LS of order 2n with
// U[i][j] = factor colour of edge ij. Then I(F,j) = order 2n-1: diagonal
// (i,i) <- U[i][j], delete row/col j.  I(F,j) is symmetric idempotent.
//   * F perfect  <=>  I(F,j) symbol-Hamiltonian for every j   (VALIDATION: must hold)
//   * I(F,j) atomic <=> row-Hamiltonian (symbol-Ham guaranteed; col-Ham=row-Ham by symmetry)
//
// Usage: atomcat.exe <catalog file>        DUMP=1 also prints each atomic square.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
using namespace std;

static int NV = 0;   // vertices, taken from the first data row
static int NF = 0;   // factors = order of I(F,j) = NV - 1

static vector<int> color;                                    // NV x NV, factor 1..NF
static inline int& C(int u, int v) { return color[u * NV + v]; }

// single-cycle test for row-Hamiltonicity of an order-NF square A (symbols 0..NF-1)
static bool rowHamiltonian(const vector<int>& A) {
    vector<int> pos1(NF);
    for (int i1 = 0; i1 < NF; i1++) {
        for (int c = 0; c < NF; c++) pos1[A[i1 * NF + c]] = c;
        for (int i2 = i1 + 1; i2 < NF; i2++) {
            // rho(s) = A[i2][pos1[s]]; must be a single NF-cycle
            int x = 0, cnt = 0;
            do { x = A[i2 * NF + pos1[x]]; cnt++; } while (x != 0 && cnt <= NF);
            if (cnt != NF) return false;
        }
    }
    return true;
}

// pull every integer out of a quoted data row; returns how many were found
static int parseRow(const char* q, vector<int>& vals) {
    vals.clear();
    const char* s = q;
    while (*s) {
        while (*s && !(*s >= '0' && *s <= '9')) s++;
        if (!*s) break;
        char* e;
        vals.push_back((int)strtol(s, &e, 10));
        s = e;
    }
    return (int)vals.size();
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: atomcat <catalog file>\n"); return 2; }
    FILE* f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }

    struct AtomRec { int rec, autOrder; string js; };
    vector<AtomRec> atomicList;
    int nRec = 0, nAtomicRec = 0, nValidateFail = 0;
    long nSymbolChecks = 0, nSymbolFail = 0;

    char line[8192];
    int  rec = -1, autOrder = -1, factorsRead = 0;
    bool inRecord = false;
    vector<int> vals;

    // one class is complete once NF rows have been read; then analyse and reset
    auto analyse = [&]() {
        nRec++;
        int atomicSpecies = 0;
        string js;
        bool anyValidateFail = false;
        vector<int> rem(NF), A(NF * NF), B(NF * NF);
        for (int j = 0; j < NV; j++) {
            int m = 0;
            for (int v = 0; v < NV; v++) if (v != j) rem[m++] = v;
            for (int a = 0; a < NF; a++)
                for (int b = 0; b < NF; b++) {
                    int ra = rem[a], rb = rem[b];
                    int col = (a == b) ? C(ra, j) : C(ra, rb);
                    A[a * NF + b] = col - 1;          // remap 1..NF -> 0..NF-1
                }
            // VALIDATION: symbol-Hamiltonian = row-Ham of the (symbol<->row) conjugate
            for (int r = 0; r < NF; r++)
                for (int c = 0; c < NF; c++) B[A[r * NF + c] * NF + c] = r;
            nSymbolChecks++;
            if (!rowHamiltonian(B)) { nSymbolFail++; anyValidateFail = true; }
            // ATOMIC test: row-Hamiltonian
            if (rowHamiltonian(A)) {
                atomicSpecies++;
                js += (js.empty() ? "" : ",") + to_string(j);
                if (getenv("DUMP")) {
                    printf("# ATOMIC SQUARE  #%d  j=%d  |Aut|=%d\n", rec, j, autOrder);
                    for (int a = 0; a < NF; a++) {
                        for (int b = 0; b < NF; b++) printf("%3d", A[a * NF + b] + 1);
                        printf("\n");
                    }
                }
            }
        }
        if (anyValidateFail) nValidateFail++;
        if (atomicSpecies > 0) { nAtomicRec++; atomicList.push_back({rec, autOrder, js}); }
    };

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') {                        // record header: #N |Aut| = X
            rec = atoi(line + 1);
            const char* p = strstr(line, "|Aut| =");
            autOrder = p ? atoi(p + 7) : -1;
            inRecord = true; factorsRead = 0;
            continue;
        }
        const char* q = strchr(line, '"');
        if (!q || !inRecord) continue;
        int n = parseRow(q + 1, vals);
        if (NV == 0) {                               // first data row fixes the size
            if (n < 4 || (n & 1)) { fprintf(stderr, "bad first row\n"); return 2; }
            NV = n; NF = NV - 1;
            color.assign(NV * NV, -1);
            printf("size taken from the data: %d vertices, %d factors\n", NV, NF);
        }
        if (n != NV) continue;                       // not a data row
        if (factorsRead == 0) color.assign(NV * NV, -1);
        int fidx = factorsRead + 1;                  // colours 1..NF
        for (int k = 0; k < NV / 2; k++) {
            int a = vals[2 * k], b = vals[2 * k + 1];
            if (a < 0 || a >= NV || b < 0 || b >= NV) { fprintf(stderr, "bad vertex in #%d\n", rec); return 2; }
            C(a, b) = fidx; C(b, a) = fidx;
        }
        if (++factorsRead == NF) { analyse(); inRecord = false; }
    }
    fclose(f);

    printf("=== atomicity analysis: %s ===\n", argv[1]);
    printf("classes processed            : %d\n", nRec);
    printf("symbol-Ham checks (F,j)      : %ld\n", nSymbolChecks);
    printf("  VALIDATION failures        : %ld   (perfect => must be 0)\n", nSymbolFail);
    printf("classes w/ >=1 atomic species: %d\n", nAtomicRec);
    if (!atomicList.empty()) {
        printf("atomic classes (record, |Aut|, atomic j-vertices):\n");
        for (auto& r : atomicList)
            printf("   #%d  |Aut|=%d  j=[%s]\n", r.rec, r.autOrder, r.js.c_str());
    }
    return 0;
}
