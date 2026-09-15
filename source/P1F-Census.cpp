// P1F-Census -- standalone driver for the |Aut|>1 representative-method engines.
//
// The engines (k14a2rep.cpp, k16A2rep.cpp, k18a2rep.cpp, k20a2rep.cpp) sit behind a
// deliberately small interface. Everything they need is: a FactorParams, a thread count, a fixed-row
// buffer that init() copies (the rep search never reads it back -- there are no `fixedRows` references
// in any of the five rep sources), and a result callback. All run configuration is read by the engines
// themselves from REP_* environment variables, exactly as before.
//
//   P1F-Census.exe [N] [kThreads]      N = 14 | 16 | 18 | 20; defaults N=18, kThreads=10
//   RESULT=<path>                the run's ONE output file -- required, never appended to
//
// A P1F-Census run has exactly one result output, in one format, whatever the mode and whatever N.
// REP_PRELOAD, REP_INFO and KNA2_RESULT are GONE: the first read a
// baseline of previously-found classes, the other two were second outputs in internal formats. So
// are REP_F3CAP, REP_F3L4 and REP_CANDCAP, which each let a block return less than it holds and so
// break the one promise ownership makes. A public user of this tool sees classes and nothing else
// -- no block coordinates, no stay/swap patterns, no legacy P-file layout. Everything that needs
// those lives in tools/.

#include "k14a2.h"
#include "k16A2.h"
#include "k18a2.h"
#include "k20a2.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <mutex>
#include <string>
#include <io.h>
#include <windows.h>
#include <sys/stat.h>   // _stat -- the .exe's mtime is the build stamp
#include <ctime>

// ---- fixed rows -------------------------------------------------------------------------------
// The host used to load these from a start-matrix file. The rep method is unseeded (every engine's
// addRow is a hard error), so they are built here: row 0 = (0,1)(2,3)...(N-2,N-1), row 1 = init()'s
// second row (0,2)(1,4)(3,6)...(N-3,N-1), row 2 = a copy of row 0 where NFIXED is 3. Rows are in
// `src` pair layout, which is what init() expects.
static void buildFixedRows(int np, int nFixed, unsigned char* out) {
    unsigned char r0[32], r1[32];
    for (int i = 0; i < np; i += 2) { r0[i] = (unsigned char)i; r0[i + 1] = (unsigned char)(i + 1); }
    unsigned char mate[32];
    for (int u = 0; u < np; u++) mate[u] = 0xFF;
    mate[0] = 2; mate[2] = 0; mate[np - 3] = (unsigned char)(np - 1); mate[np - 1] = (unsigned char)(np - 3);
    for (int j = 1; j + 3 <= np - 2; j += 2) { mate[j] = (unsigned char)(j + 3); mate[j + 3] = (unsigned char)j; }
    bool used[32] = { false };
    int idx = 0;
    for (int u = 0; u < np; u++) {
        if (used[u]) continue;
        r1[idx++] = (unsigned char)u; r1[idx++] = mate[u];
        used[u] = true; used[mate[u]] = true;
    }
    for (int s = 0; s < nFixed; s++) memcpy(out + s * np, (s == 1) ? r1 : r0, np);
}

// ---- result file ------------------------------------------------------------------------------
// One record per distinct class: a "#<seq> |Aut| = <aut>" header followed by NM rows of N/2 pairs.
//
//   #1 |Aut| = 2
//    "   0  1   2  3   4  5   6  7   8  9  10 11  12 13  14 15  16 17 "
//    ...
//
// `aut` is the real automorphism order, delivered through the callback's r4 slot -- the engines set
// it from canonKey()'s lastAut. The old P-file header printed 0
// because |Aut| never reached this callback at all.
//
// `seq` counts write order in THIS file only. It is not a class identifier: records land in thread
// discovery order, and every file restarts at 1. Naming a class is a tools/ job.
static FILE*      g_resFp = nullptr;
static std::mutex g_resMtx;
static int        g_resCount = 0;
static int        g_resNP = 0, g_resNM = 0;

static bool saveResult(void* /*cbClass*/, const unsigned char* results, int aut, int /*r5*/, int mode) {
    // mode 1 is cnvCheckNew's "is this canonical?" probe (REP_CANONDIAG in k16A2rep.cpp), never a
    // record -- the old writer ignored `mode` and emitted a spurious class for every probe.
    if (!g_resFp || mode != 2) return true;
    std::lock_guard<std::mutex> lk(g_resMtx);
    fprintf(g_resFp, "#%d |Aut| = %d\n", ++g_resCount, aut);
    for (int k = 0; k < g_resNM; k++) {
        fputs(" \"", g_resFp);
        for (int i = 0; i < g_resNP; i++) fprintf(g_resFp, "%s%2d", (i % 2) ? " " : "  ", results[k * g_resNP + i]);
        fputs(" \"\n", g_resFp);
    }
    fflush(g_resFp);   // per record: a run killed after 30 hours must leave a valid, complete prefix
    return true;
}

// ---- environment contract ---------------------------------------------------------------------
// Stop on a variable that no longer exists rather than ignoring it. Someone who sets REP_PRELOAD
// means to resume from a baseline; silently running without one would produce a file that looks
// like a census and is not.
//
// The last three are gone for the same reason turned inside out: each one makes a block report
// LESS than the block holds, and ownership then reads that partial answer as final. A class whose
// owner is the truncated block is written by nobody, and the run still ends "this range owns what
// it wrote". Diagnostics that quietly cost completeness have no place in a tool whose one output
// is a census.
static bool checkRemovedEnv() {
    static const char* const removed[][2] = {
        { "REP_PRELOAD",  "class baselines are gone -- ownership dedups statelessly" },
        { "REP_INFO",     "replaced by RESULT" },
        { "KNA2_RESULT",  "replaced by RESULT" },
        { "REP_F3CAP",    "per-block time cap -- a truncated block can miss the cover it OWNS, and no other block writes it: silent class loss" },
        { "REP_F3L4",     "level-4 block sharding -- both shards of a block believe they are block c, so both write its owned class" },
        { "REP_CANDCAP",  "biased genM candidate subset -- never a census, same silent-loss mode as a time cap" },
        { "REP_F3MAX",    "block ranges are given by their ENDS now: use REP_F3STOP=<last raw block c, inclusive>. It is refused rather than read, because a count read as a stop -- or a stop read as a count -- runs a different range and still reports success" },
        { "REP_TRIPLES",  "renamed REP_PRECALC. The mechanism was never triples-specific -- nrows comes from the orbit, so the same builder yields TRIPLES at order 3 and DOUBLES at order 2, which is what the k18 t8/t9 legs use. Refused rather than aliased: a run whose log says TRIPLES while building doubles misreports what it did" },
    };
    bool bad = false;
    for (const auto& r : removed)
        if (std::getenv(r[0])) { printf("P1F-Census: %s is no longer supported in P1F-Census -- %s\n", r[0], r[1]); bad = true; }
    if (bad) printf("P1F-Census: unset the variable(s) above and re-run -- stop\n");
    return !bad;
}

// Ownership is defined against a BLOCK COLUMN: the owner of a class is the minimum block index
// over all of its column labelings. Only k18 has blocks -- the a.b.c coordinate, childReps, and
// the REP_F3COMPLETE driver that walks them. Elsewhere there is no column and nothing to own.
//
// The engine turns the filter ON BY DEFAULT, but only under REP_F3COMPLETE, which is k18-only:
// so the default never reaches this check, and what is validated here is always an explicit
// request. Ignoring an explicit one is the DANGEROUS option, not the safe one. The whole point of
// ownership is that separately-run ranges write disjoint class sets and so concatenate without a
// dedup; someone who set REP_OWNER to shard a k14 or k16 sweep and watched it run would believe
// exactly that, and their `cat` would over-count. So refuse -- here in the host, before RESULT is
// created: one check covering all five engines, with no way for the siblings to drift apart on it.
static bool checkOwnerEnv(int np) {
    const char* owner = std::getenv("REP_OWNER");
    if (owner && owner[0] == '0' && owner[1] == 0) owner = nullptr;   // REP_OWNER=0 asks for LESS, never for a column
    const char* ownerAll = std::getenv("REP_OWNERALL");
    if (!owner && !ownerAll) return true;
    const char* name = owner ? "REP_OWNER" : "REP_OWNERALL";
    if (np != 18) {
        printf("P1F-Census: %s is k18-only, and N=%d was requested.\n"
               "      Ownership is the minimum BLOCK index over a class's column labelings, and only\n"
               "      k18 has blocks (the a.b.c coordinate and the REP_F3COMPLETE driver). With no\n"
               "      column there is nothing to own, so this run would NOT be shard-disjoint however\n"
               "      much it looked like it -- stop\n", name, np);
        return false;
    }
    if (!std::getenv("REP_F3COMPLETE")) {
        printf("P1F-Census: %s needs REP_F3COMPLETE.\n"
               "      Ownership is defined against a block column; without the block driver there is\n"
               "      no column, no block index and nothing to own -- stop\n", name);
        return false;
    }
    return true;
}

// The build stamp is the running executable's OWN modification time. __DATE__/__TIME__ is the
// compile time of one translation unit, and goes stale the moment that unit is not recompiled
// while the binary is -- which is the failure it was there to prevent.
static const char* buildStamp() {
    static char s[32] = "";
    if (*s) return s;
    char exe[MAX_PATH] = "";
    struct _stat st;
    if (GetModuleFileNameA(NULL, exe, MAX_PATH) && _stat(exe, &st) == 0) {
        struct tm tmv;
        if (localtime_s(&tmv, &st.st_mtime) == 0)
            strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", &tmv);
    }
    if (!*s) snprintf(s, sizeof(s), "unknown");
    return s;
}

// Wall-clock time the process started. The table's Elapsed column is relative, so two logs cannot
// be laid over each other without knowing where each one begins -- which is exactly what a
// staggered set of runs (start one every half hour) needs in order to be compared at equal machine
// load. Read once, on the first call, before any work starts.
static const char* startStamp() {
    static char s[32] = "";
    if (*s) return s;
    const time_t t = time(NULL);
    struct tm tmv;
    if (localtime_s(&tmv, &t) == 0)
        strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", &tmv);
    if (!*s) snprintf(s, sizeof(s), "unknown");
    return s;
}

// The machine. A rate is meaningless without one: logs here are compared against each other
// constantly -- 8 threads against 30, this laptop against the 32-core box -- and a log that does
// not name its host can only be placed by guesswork. One 10-thread K18 log cost exactly that on
// 2026-09-09, read alongside runs from another machine before anyone noticed it said nothing about
// where it came from.
static const char* hostName() {
    static char s[64] = "";
    if (*s) return s;
    DWORD n = (DWORD)sizeof(s);
    if (!GetComputerNameA(s, &n)) snprintf(s, sizeof(s), "unknown");
    return s;
}

// Every REP_* variable actually set, on one line. A run whose log does not state its own knobs
// cannot be reproduced from the log, and env_reset.bat exists because a leftover knob
// silently changing a run is a real failure mode here.
//
// REP_RANGE is deliberately absent: its end is EXCLUSIVE, so echoing it next to a bat that
// asks for BStart/BLast inclusive invites the reader to mis-read one as the other. The engine
// prints the scope instead, as "blocks A-B of 0-X", with both ends inclusive like every other
// range in this repository.
static void printEnv() {
    static const char* const names[] = {
        "REP_ORDERS", "REP_ORDER", "REP_LEVEL", "REP_PRECALC",
        "REP_F3COMPLETE", "REP_F3A", "REP_F3START", "REP_F3STOP", "REP_F3LIST",
        "REP_OWNER", "REP_OWNERALL", "REP_PRUNELEVEL", "REP_TYPEMASK", "REP_SGEN",
        "REP_DIAGPRUNE", "REP_PATAPPLY", "REP_PATTERN", "REP_INFO", "REP_WITNESS",
        "REP_LOCATE", "REP_COMPLETEPREFIX", "REP_PATHCOORDCANON", "REP_ESTIMATE",
        "REP_DIAGPROBE", "REP_F17DUMP", "REP_CANONFILE",
    };
    std::string line;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char* v = std::getenv(names[i]);
        if (!v || !*v) continue;
        if (!line.empty()) line += "  ";
        line += names[i]; line += "="; line += v;
    }
    if (!line.empty()) printf("P1F-Census: Requested %s\n", line.c_str());
}

int main(int argc, const char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);   // unbuffered: the engines' progress lines appear as they are printed
    // UTF-8 on the console, set here in main(). The engines print
    // cycle types with superscripts -- "3⁶ 1²", "2⁹" -- unconditionally since g_useColors was
    // removed (see the note below), and those are multi-byte UTF-8. Without this the console stays
    // on the machine's OEM codepage and every superscript arrives as mojibake. The runs\ bats never
    // showed it because they pipe stdout through a PowerShell UTF-8 StreamWriter into the .log, so
    // only someone running P1F-Census.exe straight from a prompt saw the damage. This affects the CONSOLE
    // only: the bytes written to a redirected file are the same either way, so no existing log,
    // result file or regression compare changes.
    SetConsoleOutputCP(CP_UTF8);
    // BELOW NORMAL priority, set before any work starts. A census run saturates every thread it is
    // given for hours or days, and at NORMAL it makes the machine it runs on unusable -- a 10-thread
    // K20 order-2 run drove this one to a near-crash (2026-09-03). Below normal costs the run almost
    // nothing when the box is otherwise idle (it still gets every free core) and yields immediately
    // to anything the user is doing. Set REP_PRIORITY=normal to opt out.
    {
        const char* pr = std::getenv("REP_PRIORITY");
        const bool normalPri = (pr && (!strcmp(pr, "normal") || !strcmp(pr, "NORMAL")));
        if (!normalPri) {
            if (SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS))
                printf("P1F-Census: process priority BELOW NORMAL (REP_PRIORITY=normal to opt out)\n");
            else
                printf("P1F-Census: could not lower process priority (error %lu) -- running at the default\n", GetLastError());
        }
    }
    // g_useColors is gone (2026-08-30). It gated the type string's superscripts on
    // _isatty(_fileno(stdout)), and every runs\ case pipes stdout into its .log -- so the flag
    // was always false there and a log never showed one. See source/knSupport.cpp.
    const int np = (argc > 1) ? atoi(argv[1]) : 18;
    int kThreads = (argc > 2) ? atoi(argv[2]) : 10;
    if (kThreads < 1) kThreads = 1;
    if (!checkRemovedEnv()) return 1;
    if (!checkOwnerEnv(np)) return 1;
    const char* resPath = std::getenv("RESULT");
    if (!resPath || !*resPath) { printf("P1F-Census: RESULT=<path> is required -- it is the run's only output -- stop\n"); return 1; }
    // Refuse an existing file, before the engine does any work: a run is never resumed into its own
    // output, and a finished census must not be silently appended to or overwritten.
    if (_access(resPath, 0) == 0) { printf("P1F-Census: RESULT file %s already exists -- delete it or name another file -- stop\n", resPath); return 1; }
    g_resFp = fopen(resPath, "w");
    if (!g_resFp) { printf("P1F-Census: cannot create %s -- stop\n", resPath); return 1; }
    printf("P1F-Census: built %s, N=%d, kThreads=%d, RESULT saved to %s\n",
           buildStamp(), np, kThreads, resPath);
    printf("P1F-Census: started %s on %s\n", startStamp(), hostName());
    printEnv();
    const auto tStart = std::chrono::steady_clock::now();   // whole-run clock; reported once, on the closing line

    unsigned char fixed[3 * 32] = { 0 };
    KSolver* solver = nullptr;
    switch (np) {
    case 14: { const FactorParams fp(K14A2::NP, K14A2::NM, K14A2::NFIXED, K14A2::M_MAX, 0);
               g_resNP = K14A2::NP; g_resNM = K14A2::NM; buildFixedRows(K14A2::NP, K14A2::NFIXED, fixed);
               solver = new K14A2(fp, 0, kThreads, fixed, saveResult, nullptr, true); break; }
    case 16: { const FactorParams fp(K16A2::NP, K16A2::NM, K16A2::NFIXED, K16A2::M_MAX, 0);
               g_resNP = K16A2::NP; g_resNM = K16A2::NM; buildFixedRows(K16A2::NP, K16A2::NFIXED, fixed);
               solver = new K16A2(fp, 0, kThreads, fixed, saveResult, nullptr, true); break; }
    case 18: { const FactorParams fp(K18A2::NP, K18A2::NM, K18A2::NFIXED, K18A2::M_MAX, 0);
               g_resNP = K18A2::NP; g_resNM = K18A2::NM; buildFixedRows(K18A2::NP, K18A2::NFIXED, fixed);
               solver = new K18A2(fp, 0, kThreads, fixed, saveResult, nullptr, true); break; }
    case 20: { const FactorParams fp(K20A2::NP, K20A2::NM, K20A2::NFIXED, K20A2::M_MAX, 0);
               g_resNP = K20A2::NP; g_resNM = K20A2::NM; buildFixedRows(K20A2::NP, K20A2::NFIXED, fixed);
               solver = new K20A2(fp, 0, kThreads, fixed, saveResult, nullptr, true); break; }
    default: printf("P1F-Census: N=%d not supported (14, 16, 18, 20) -- stop\n", np); return 1;
    }
    solver->solve();
    delete solver;
    fclose(g_resFp);
    const double totMin = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count() / 60.0;
    printf("P1F-Census: %d result(s) written to %s (Total time=%.0fmin)\n", g_resCount, resPath, totMin);
    printf("End of job\n");   // last line of every completed run: a log that stops short of it was interrupted
    fflush(stdout);
    return 0;
}
