// ============================================================================
// k14a2.cpp — minimal K14 host: construct, then dispatch solve() to the
// representative-method classifier (runRepresentativeMethod, in k14a2rep.cpp).
// No cyclic engine; the rep method is unseeded so the fixed starter rows are
// ignored. Mirrors the REP_ORDER[S] dispatch used by K16A2/K18A2/K20A2.
// ============================================================================
#include "k14a2.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <atomic>
#include <vector>

K14A2::K14A2(const FactorParams& factParam, int fixed3RowsIndex, int kThreads,
             const unsigned char* first3Rows, ResultCallback callback, void* cbClassPtr, bool bPrint)
    : KBase<Mask14_C>(factParam, bPrint) {
    this->fixed3RowsIndex = fixed3RowsIndex;
    this->kThreads = (kThreads > 256) ? 256 : kThreads;
    this->resultCallback = callback;
    this->cbClass = cbClassPtr;
    (void)first3Rows;   // rep method is unseeded; fixed rows are not used
    KBase::init();
}

bool K14A2::addRow(int /*iRow*/, const unsigned char* /*source*/) {
    printf("K14A2::addRow not supported (rep method is unseeded)\n");
    return false;
}

void K14A2::solve(int /*mode*/) {
    runExhaustiveSearch();
}

void K14A2::runExhaustiveSearch() {
#if K14_USE_REP_METHOD
    // Run the classifier ONCE per process (the job may invoke the solver per start
    // matrix, but this search is unseeded so it must run a single time).
    static std::atomic<bool> repDone{ false };
    bool expected = false;
    if (repDone.compare_exchange_strong(expected, true)) {
        // Orders to classify. Priority: env REP_ORDERS (comma list) > REP_ORDER
        // (single) > K14_REP_ORDER default. All emit into the SAME result pipeline,
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
                const char* q = p; while (*q && *q != ',' && *q != ':') q++;   // scan this token
                if (*q == ':') tgt = atoi(q + 1);                              // optional :target
                if (v > 1 || v == -4 || v == -9 || v == -6) orders.push_back({ v, tgt });
                while (*p && *p != ',') p++; while (*p == ',') p++;
            }
        } else if (const char* env1 = std::getenv("REP_ORDER")) { // single-order run (also "order:target")
            int v = atoi(env1), tgt = 0; const char* q = env1; while (*q && *q != ':') q++; if (*q == ':') tgt = atoi(q + 1);
            if ((*env1 == 'V' || *env1 == 'v') && env1[1] == '4') v = -4;      // token "V4" = Klein four-group sweep
            if ((*env1 == 'E' || *env1 == 'e') && env1[1] == '9') v = -9;      // token "E9" = C3xC3 two-generator sweep
            if ((*env1 == 'S' || *env1 == 's') && env1[1] == '3') v = -6;      // token "S3" = S3 two-generator sweep
            orders.push_back({ v, tgt });
        } else {
            orders.push_back({ K14_REP_ORDER, 0 });
        }
        for (auto& ot : orders)
            runRepresentativeMethod(ot.first, ot.second);   // private member; uses kThreads + m_bPrint
        // ONE table per run, so its = TOTAL row is emitted here -- after the LAST token, not inside
        // runRepresentativeMethod, which is entered once per token.  docs/log_table_spec.md 2a, 2c.
        finishRepTable();
    }
#endif
}
