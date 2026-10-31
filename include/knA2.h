#ifndef KNA2_H
#define KNA2_H
// ============================================================================
// knA2.h — the ONE host for the representative-method classifier, templated on N.
//
// Replaces the per-N hosts k14a2.h / k16A2.h / k18a2.h / k20a2.h (docs/K22_spec.md).
// KnA2<N> is the KSolver shell the dispatcher constructs: solve() ->
// runExhaustiveSearch() -> runRepresentativeMethod(order) for each REP_ORDERS token.
// The two engine members are defined per N in source/knA2_<N>.cpp, which defines
// KNA2_N and includes source/knA2rep.inl -- one translation unit per N, so N is a
// compile-time constant everywhere in the engine.
//
// Supported N: every even N from 14 to 32 (KNA2_FOR_EACH_N below). Adding one is a
// new knA2_<N>.cpp plus one entry here and in p1f.cpp's dispatcher.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <malloc.h>
#include <atomic>
#include <vector>
#include <utility>
#include "kBase.h"

#define KNA2_REP_ORDER      4          // default automorphism order to classify
#define KNA2_REP_SYMCAP     2000000LL  // enumerate C(alpha) only when |C(alpha)| <= this

#define KNA2_FOR_EACH_N(X) X(14) X(16) X(18) X(20) X(22) X(24) X(26) X(28) X(30) X(32)

// [GENGRP] Generic small-group legs (source/knA2rep.inl enumShapesGen): REP_ORDERS tokens D8, Q8,
// C4xC2, C2xC2xC2, A4, D10 (GAP StructureDescription names; D8 = dihedral of order 8) map to the
// sentinel codes -101 .. -106 (KNA2_GEN_BASE - index); 0 = not one.
#define KNA2_GEN_BASE (-101)
inline int knA2GenGroupCode(const char* p) {
    static const char* const names[] = { "D8", "Q8", "C4xC2", "C2xC2xC2", "A4", "D10" };
    for (int i = 0; i < 6; i++) {
        const char* n = names[i]; int k = 0;
        while (n[k] && p[k] && (p[k] | 32) == (n[k] | 32)) k++;
        if (!n[k] && (!p[k] || p[k] == ',' || p[k] == ':')) return KNA2_GEN_BASE - i;
    }
    return 0;
}

template<int NN> struct MaskN { uint64_t m[(NN * (NN - 1) / 2 + 63) / 64] = {}; };  // unused by rep

template<int NN>
class KnA2 : public KBase<MaskN<NN>> {
    static_assert(NN % 2 == 0 && NN >= 8 && NN <= 32, "KnA2: N must be even, 8..32");
public:
    // N-derived constants, used by the dispatcher's FactorParams.
    static constexpr int NP     = NN;          // players / vertices
    static constexpr int NM     = NP - 1;      // factors in a 1-factorization
    static constexpr int NFIXED = 3;           // fixed starter rows (unused by rep)
    static constexpr int M_MAX  = 131072;      // pool cap (unused by rep; sizes nothing it touches)

    // Guard custom aligned operator new/delete from MemoryLeak.h's Debug `#define new DBG_NEW`.
#pragma push_macro("new")
#undef new
    void* operator new(size_t size) {
        void* p = _aligned_malloc(size, 64);
        if (!p) throw std::bad_alloc();
        return p;
    }
    void operator delete(void* p) {
        _aligned_free(p);
    }
#if defined(_DEBUG) && defined(_MSC_VER)
    void* operator new(size_t size, int blockType, const char* filename, int linenumber) {
        void* p = _aligned_malloc_dbg(size, 64, filename, linenumber);
        if (!p) throw std::bad_alloc();
        return p;
    }
    void operator delete(void* p, int blockType, const char* filename, int linenumber) {
        _aligned_free(p);
    }
#endif
#pragma pop_macro("new")

    KnA2(const FactorParams& factParam, int fixed3RowsIndex, int kThreads,
         const unsigned char* first3Rows, ResultCallback callback,
         void* cbClassPtr = NULL, bool bPrint = true)
        : KBase<MaskN<NN>>(factParam, bPrint) {
        this->fixed3RowsIndex = fixed3RowsIndex;
        this->kThreads = (kThreads > 256) ? 256 : kThreads;
        this->resultCallback = callback;
        this->cbClass = cbClassPtr;
        (void)first3Rows;   // rep method is unseeded; fixed rows are not used
        KBase<MaskN<NN>>::init();
    }

    void solve(int /*mode*/ = 0) override { runExhaustiveSearch(); }

    bool addRow(int /*iRow*/, const unsigned char* /*source*/) override {
        printf("KnA2::addRow not supported (rep method is unseeded)\n");
        return false;
    }

private:
    void runExhaustiveSearch() {
        // Run the classifier ONCE per process (the job may invoke the solver per start
        // matrix, but this search is unseeded so it must run a single time).
        static std::atomic<bool> repDone{ false };
        bool expected = false;
        if (!repDone.compare_exchange_strong(expected, true)) return;
        // Orders to classify. Priority: env REP_ORDERS (comma list) > REP_ORDER
        // (single) > KNA2_REP_ORDER default. All emit into the SAME result pipeline,
        // which canonicalizes the union -> one combined, deduplicated classification.
        // Per-order HARVEST TARGET via "order:target" (e.g. "2:16,3,7,13"): that order
        // STOPS after `target` distinct classes (skips the proof-of-exhaustion tail) --
        // complete only if target == true count, else a lower bound. No ':' = run to end.
        std::vector<std::pair<int,int>> orders;   // (order, target); target 0 = full run
        if (const char* envs = std::getenv("REP_ORDERS")) {       // full job: comma list
            const char* p = envs;
            while (*p) {
                int v = atoi(p), tgt = 0;
                if ((*p == 'V' || *p == 'v') && p[1] == '4') v = -4;           // token "V4" = Klein four-group sweep
                if ((*p == 'E' || *p == 'e') && p[1] == '9') v = -9;           // token "E9" = C3xC3 two-generator sweep
                if ((*p == 'S' || *p == 's') && p[1] == '3') v = -6;           // token "S3" = S3 two-generator sweep
                if (int gc = knA2GenGroupCode(p)) v = gc;                       // tokens D8, Q8, C4xC2, C2xC2xC2, A4, D10 = generic group legs
                const char* q = p; while (*q && *q != ',' && *q != ':') q++;   // scan this token
                if (*q == ':') tgt = atoi(q + 1);                              // optional :target
                if (v > 1 || v == -4 || v == -9 || v == -6 || (v <= KNA2_GEN_BASE && v > KNA2_GEN_BASE - 6)) orders.push_back({ v, tgt });
                while (*p && *p != ',') p++; while (*p == ',') p++;
            }
        } else if (const char* env1 = std::getenv("REP_ORDER")) { // single-order run (also "order:target")
            int v = atoi(env1), tgt = 0; const char* q = env1; while (*q && *q != ':') q++; if (*q == ':') tgt = atoi(q + 1);
            if ((*env1 == 'V' || *env1 == 'v') && env1[1] == '4') v = -4;      // token "V4" = Klein four-group sweep
            if ((*env1 == 'E' || *env1 == 'e') && env1[1] == '9') v = -9;      // token "E9" = C3xC3 two-generator sweep
            if ((*env1 == 'S' || *env1 == 's') && env1[1] == '3') v = -6;      // token "S3" = S3 two-generator sweep
            if (int gc = knA2GenGroupCode(env1)) v = gc;                  // tokens D8, Q8, C4xC2, C2xC2xC2, A4, D10
            orders.push_back({ v, tgt });
        } else {
            orders.push_back({ KNA2_REP_ORDER, 0 });
        }
        for (auto& ot : orders)
            runRepresentativeMethod(ot.first, ot.second);   // private member; uses kThreads + m_bPrint
        // ONE table per run, so its = TOTAL row is emitted here -- after the LAST token, not inside
        // runRepresentativeMethod, which is entered once per token.
        finishRepTable();
    }
    void runRepresentativeMethod(int order, int target = 0);   // per N, in knA2_<N>.cpp; target>0 = harvest mode
    void finishRepTable();                                    // the log table's = TOTAL row; called once, after the last REP_ORDERS token

    int kThreads = 1;
    ResultCallback resultCallback = NULL;
    void* cbClass = NULL;
};

// The engine members exist only as explicit specializations, one pair per N, each defined in its
// own knA2_<N>.cpp. Declaring them here is what lets every other translation unit call them.
#define KNA2_DECLARE_N(n) \
    template<> void KnA2<n>::runRepresentativeMethod(int order, int target); \
    template<> void KnA2<n>::finishRepTable();
KNA2_FOR_EACH_N(KNA2_DECLARE_N)
#undef KNA2_DECLARE_N

#endif // KNA2_H
