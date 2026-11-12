// The rows of a PC settings page: their values, the cursor, greyed rows and the caption that says why. Reference code
// for guides/settings/README.md, section 6. Nothing here knows about any game: the port draws what these functions
// decide into the game's own panel.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "settings_file.hpp"

namespace settings_guide {

// When a change takes effect. The caption says so when it is not at once.
enum class Takes { at_once, next_frame, next_start };

struct Row {
    std::string key;                 // the settings file's key; "" for a row that only opens a page
    std::vector<std::string> words;  // the file's word for each value, in the order left / right step through them
    int def = 0;                     // the default, an index into words: the game's own behaviour unless decided
    Takes takes = Takes::at_once;
    bool opens_page = false;         // A opens a page of its own (no values; it shows a summary)
};

// What the row can do on this PC now. The port fills it in each time the row is drawn.
struct RowState {
    bool missing = false;        // not on this PC at all: no driver support, a file that is not there (shows "unavailable")
    bool held = false;           // another row's choice holds it (an upscaler anti-aliases itself): value kept, greyed
    bool waits_for_start = false;  // the value chosen is not the one running; it takes effect at the next start
    bool kept_for_later = false;   // set now, used once another row turns its feature on (HDR levels with HDR off)
};

// The value shown for a row: the one in use when the port can tell (a test switch or a hotkey may have changed it since
// the file was read), else the file's, else the default. -1 is a word the row does not list: shown as "custom" and kept
// until the player picks another value.
inline int shown_choice(const Row& r, const std::string& file_word, const std::string& in_use_word = "") {
    const std::string& w = !in_use_word.empty() ? in_use_word : file_word;
    if (w.empty()) return r.def;
    for (size_t i = 0; i < r.words.size(); i++)
        if (r.words[i] == w) return static_cast<int>(i);
    return -1;
}

// Left (dir -1) or right (+1) from `cur`. Lists wrap around; values this PC cannot do (`skipped`) are stepped past;
// from "custom" (-1), right goes to the first value and left to the last. Returns `cur` when there is nowhere to go: a
// row with one value, or all others skipped. The port then plays the game's own "limit" sound.
inline int step(const Row& r, int cur, int dir, const std::function<bool(int)>& skipped) {
    const int n = static_cast<int>(r.words.size());
    if (n == 0) return cur;
    const int by = dir > 0 ? 1 : n - 1;
    int next = cur < 0 ? (dir > 0 ? 0 : n - 1) : (cur + by) % n;
    for (int tries = 0; tries < n && skipped(next); tries++) next = (next + by) % n;
    return skipped(next) ? cur : next;
}

// Whether the row shows its left / right arrows: only when there is something to step through.
inline bool shows_arrows(const Row& r, const RowState& s) {
    return !r.opens_page && !s.missing && !s.held && r.words.size() > 1;
}

// Whether the row is drawn greyed. A greyed row can still be selected, so that its caption can say why.
inline bool greyed(const Row& r, const RowState& s, bool page_has_rows = true) {
    if (r.opens_page) return !page_has_rows;
    return r.key.empty() || s.missing || s.held;
}

// Whether the cursor stops on the row. A row with no key and no page is a placeholder: the cursor skips it.
inline bool selectable(const Row& r, bool page_has_rows = true) {
    return r.opens_page ? page_has_rows : !r.key.empty();
}

// Up (dir -1) or down (+1) to the next row the cursor stops on, wrapping around as the game's lists do. Returns `cur`
// when no other row can be selected.
inline int move_cursor(int rows, int cur, int dir, const std::function<bool(int)>& can_select) {
    if (rows <= 0) return cur;
    const int by = dir > 0 ? 1 : rows - 1;
    int next = cur;
    for (int tries = 0; tries < rows; tries++) {
        next = (next + by) % rows;
        if (can_select(next)) return next;
    }
    return cur;
}

// Which caption the row shows under the panel. The first that applies wins: what stops the player before what only
// informs them.
enum class Caption { normal, unavailable, held, at_next_start, kept_for_later };

inline Caption caption_for(const RowState& s) {
    if (s.missing) return Caption::unavailable;
    if (s.held) return Caption::held;
    if (s.waits_for_start) return Caption::at_next_start;
    if (s.kept_for_later) return Caption::kept_for_later;
    return Caption::normal;
}

// Puts the value to work first and saves it after; when it cannot be put to work (`apply` returns false), nothing is
// saved and the caller keeps the old value. Rows that take effect at the next start apply nothing now.
inline bool choose(const Row& r, int choice, SettingsFile& file, const std::function<bool(const std::string&)>& apply) {
    if (r.key.empty() || choice < 0 || choice >= static_cast<int>(r.words.size())) return false;
    const std::string& w = r.words[static_cast<size_t>(choice)];
    if (r.takes != Takes::next_start && !apply(w)) return false;
    file.set(r.key, w);
    return true;
}

// "Restore the defaults?" answered yes, on one page: every row of the page back to its default, put to work and saved.
// Returns how many rows changed.
inline int restore_defaults(const std::vector<Row>& page, SettingsFile& file,
                            const std::function<bool(const Row&, const std::string&)>& apply) {
    int changed = 0;
    for (const Row& r : page) {
        if (r.key.empty() || r.words.empty()) continue;
        const std::string& w = r.words[static_cast<size_t>(r.def)];
        if (file.get(r.key, w) == w) continue;
        if (choose(r, r.def, file, [&](const std::string& v) { return apply(r, v); })) changed++;
    }
    return changed;
}

}  // namespace settings_guide
