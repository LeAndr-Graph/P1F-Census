#pragma once
// =============================================================================
// include/logTable.h -- the ONE writer for every table in a P1F-Census run log.
//
// Shared by all four engines and P1F-Census.cpp, so the
// sibling rule holds by construction: one copy of the formatting, not four.
//
// It knows nothing about P1Fs, blocks or automorphisms -- it turns counts into
// aligned text.  Callers hand it cells already formatted by the fmt* helpers.
// =============================================================================
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>

// ExtPrint -- how much of the run's own bookkeeping reaches the log.
//
//   0 (default)  the census only: the header, one row per block, the totals.
//   >= 1         adds the engine's trace lines -- SETUP timings, PRECALC list
//                sizes, SHARD/LEVEL branch counts, the per-block branch split.
//
// Read once, on the first call, so a long run never pays for it and a variable
// changed mid-run cannot make the top and bottom of one log disagree.
inline int extPrint() {
    static const int level = []() {
        const char* e = std::getenv("ExtPrint");
        return (e && *e) ? std::atoi(e) : 0;
    }();
    return level;
}

// printf for a trace line: prints only at ExtPrint >= 1. Warnings, errors and
// anything a reader needs in order to trust the result keep plain printf --
// what is gated here is volume, never meaning.
inline int xprintf(const char* fmt, ...) {
    if (extPrint() < 1) return 0;
    va_list ap;
    va_start(ap, fmt);
    const int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

// A column.  `width` is a MINIMUM: an over-wide cell pushes the rest of its row
// right rather than being truncated, because a truncated count is a wrong count.
// The 50-row header repeat re-anchors the eye after any such row.
struct LogCol { const char* name; int width; bool right; };

// How WIDE a cell is on screen, which is not how many bytes it holds. The type string carries
// UTF-8 superscripts -- `2⁹` is 4 bytes and 2 columns -- so padding by size() would leave every
// row with a superscript short by the number of continuation bytes in it, and the columns to its
// right would step left. Count lead bytes: a UTF-8 continuation byte is 10xxxxxx.
inline int dispWidth(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size(); i++)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) n++;
    return n;
}

class LogTable {
public:
    // Prints a blank line, the title, a blank line, then the ruled column-name row.
    //
    // TWO name arrays, same count and same widths, different words. The data rows and the
    // = TOTAL row do not carry the same quantities -- `Total saved(duplicates)` becomes
    // `Saved`, `Leg nodes (rate)` becomes `Total nodes (average)` -- so each gets its own
    // header rather than one header that would be wrong above one of them.
    LogTable(const char* title, const LogCol* cols, const LogCol* totalCols, int ncols)
        : m_cols(cols), m_totalCols(totalCols), m_n(ncols), m_rows(0) {
        printf("\n%s\n\n", title);
        header();
        fflush(stdout);
    }
    // One data row.  marker: ' ' = a completed unit, '~' = a running total.
    void row(char marker, const std::vector<std::string>& cells) {
        if (m_rows && (m_rows % 50) == 0) { printf("\n"); header(); }
        printf("%s\n", line(marker, cells).c_str());
        ++m_rows;
        fflush(stdout);
    }
    // The final total.  Column names are reprinted immediately above it, so the one row a
    // reader scrolls to the bottom for is always labeled, and a rule closes the table.
    void total(const std::vector<std::string>& cells) {
        printf("\n");
        header(m_totalCols);
        printf("%s\n", line('=', cells).c_str());
        rule();
        fflush(stdout);
    }
private:
    // A rule as wide as the column-name row, above the names and below the last row, so the
    // table has a visible top and bottom rather than running into the prose around it.
    void rule() { printf("%s\n", std::string(m_width, '-').c_str()); }
    void header() { header(m_cols); }
    void header(const LogCol* cols) {
        std::vector<std::string> h;
        h.reserve(m_n);
        for (int i = 0; i < m_n; i++) h.push_back(std::string(cols[i].name));
        const std::string names = line(' ', h);
        const size_t w = (size_t)dispWidth(names);   // the rule is drawn in columns, like the row
        if (w > m_width) m_width = w;
        rule();
        printf("%s\n", names.c_str());
    }
    std::string line(char marker, const std::vector<std::string>& cells) {
        // Render only as far as the last cell that has anything in it: a `skipped` row supplies
        // three cells, and trailing empties would otherwise print as bare column separators.
        int last = 0;
        for (int i = 0; i < m_n; i++)
            if (i < (int)cells.size() && !cells[i].empty()) last = i;
        std::string out(1, marker);
        for (int i = 0; i <= last; i++) {
            std::string s = (i < (int)cells.size()) ? cells[i] : std::string();
            const int pad = m_cols[i].width - dispWidth(s);   // columns, not bytes -- see dispWidth
            out += (i == 0) ? " " : " | ";   // separators go BETWEEN columns, never at either end
            if (m_cols[i].right && pad > 0) out += std::string(pad, ' ');
            out += s;
            if (!m_cols[i].right && pad > 0 && i != last) out += std::string(pad, ' ');
        }
        while (!out.empty() && out[out.size() - 1] == ' ') out.erase(out.size() - 1);
        return out;
    }
    const LogCol* m_cols;
    const LogCol* m_totalCols;
    int m_n;
    long long m_rows;
    size_t m_width = 0;
};

// ---- value formatters -------------------------------------------------------
// One rule per quantity, so a leg row and a block row are comparable at a glance.

// Below 600 s, seconds with one decimal; at or above, whole minutes.
inline std::string fmtElapsed(double sec) {
    char b[32];
    if (sec < 600.0) snprintf(b, sizeof(b), "%.1fs", sec);
    else             snprintf(b, sizeof(b), "%.0fmin", sec / 60.0);
    return std::string(b);
}

// Digits in groups of three.  The separators are for the reader; nothing in the
// repository parses the log (compare_to_catalog.pl reads the RESULT file), which
// is why it is safe.
inline std::string fmtGrouped(long long v) {
    char b[32];
    snprintf(b, sizeof(b), "%lld", v);
    std::string d(b), out;
    const size_t neg = (!d.empty() && d[0] == '-') ? 1 : 0;
    for (size_t i = 0; i < d.size(); i++) {
        if (i > neg && ((d.size() - i) % 3) == 0) out += ',';
        out += d[i];
    }
    return out;
}

// Count plus the rate over THIS row's own elapsed time, so rate = Nodes/Elapsed
// holds on every row and the column can be checked by eye.  On a ~ or = row both
// are cumulative, so the rate there is the average, not the last interval's.
inline std::string fmtNodes(long long nodes, double sec) {
    std::string s = fmtGrouped(nodes);
    char b[24];
    if (sec < 0.05) snprintf(b, sizeof(b), " (-)");   // elapsed rounds to 0.0s
    else            snprintf(b, sizeof(b), " (%.0f/ms)", (double)nodes / (sec * 1000.0));
    return s + b;
}

// "4:96 8:22 16:12", ascending in k.  Bare -- the caller supplies the brackets, because
// the table cells want |Aut|={...} and the footnotes of section 4 want aut{...}.
inline std::string fmtHist(const std::map<int, int>& hist) {
    std::string s;
    for (std::map<int, int>::const_iterator it = hist.begin(); it != hist.end(); ++it) {
        if (!s.empty()) s += ' ';
        char t[24];
        snprintf(t, sizeof(t), "%d:%d", it->first, it->second);
        s += t;
    }
    return s;
}

// A count, with its |Aut| histogram appended when there is one.
inline std::string fmtCount(long long n, const std::map<int, int>& hist) {
    char b[32];
    snprintf(b, sizeof(b), "%lld", n);
    std::string s(b);
    if (n > 0 && !hist.empty()) { s += " |Aut|={"; s += fmtHist(hist); s += '}'; }
    return s;
}

inline std::string fmtCount(long long n) { return fmtCount(n, std::map<int, int>()); }

// The Saved(Duplicates) cell: one column for the two halves of Results, because they are read
// against each other and never apart -- Results = Saved + Duplicates on every row.
//
//   5(12) |Aut|={3:5 6:(5) 12:(5) 84:(1) 156:(1)}
//
// The counts lead, saved first and duplicates in parentheses. The histogram merges the two the
// same way, ascending in k: a k that was saved prints bare, a k that was rejected prints in
// parentheses, and a k that was both prints "k:saved(dups)". Both counts always print, 0
// included, so the identity can be checked on a row without inferring anything.
inline std::string fmtSavedDup(long long saved, long long dups,
                               const std::map<int, int>& savedHist,
                               const std::map<int, int>& dupHist) {
    char b[48];
    snprintf(b, sizeof(b), "%lld(%lld)", saved, dups);
    std::string s(b);
    if (savedHist.empty() && dupHist.empty()) return s;
    std::map<int, int>::const_iterator a = savedHist.begin(), d = dupHist.begin();
    std::string h;
    while (a != savedHist.end() || d != dupHist.end()) {
        int k;
        if      (a == savedHist.end()) k = d->first;
        else if (d == dupHist.end())   k = a->first;
        else                           k = (a->first < d->first) ? a->first : d->first;
        const bool hasA = (a != savedHist.end() && a->first == k);
        const bool hasD = (d != dupHist.end()   && d->first == k);
        char t[48];
        if      (hasA && hasD) snprintf(t, sizeof(t), "%d:%d(%d)", k, a->second, d->second);
        else if (hasA)         snprintf(t, sizeof(t), "%d:%d", k, a->second);
        else                   snprintf(t, sizeof(t), "%d:(%d)", k, d->second);
        if (!h.empty()) h += ' ';
        h += t;
        if (hasA) ++a;
        if (hasD) ++d;
    }
    return s + " |Aut|={" + h + "}";
}

inline std::string fmtPct(long long done, long long total) {
    char b[16];
    snprintf(b, sizeof(b), "%.2f", total > 0 ? 100.0 * (double)done / (double)total : 0.0);
    return std::string(b);
}
