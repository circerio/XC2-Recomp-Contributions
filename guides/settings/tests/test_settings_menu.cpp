// Tests for the settings guide's reference code: the overlay over an archive, the settings file, the rows and the
// rebinding rules.
// SPDX-License-Identifier: MIT
#include "key_bindings.hpp"
#include "option_rows.hpp"
#include "overlay_reader.hpp"
#include "settings_file.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace settings_guide;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);         \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void write_file(const fs::path& p, const std::string& bytes) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << bytes;
}

static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// The "disk": a string that is only ever read.
static BaseRead reader(const std::string& disk) {
    return [&disk](uint64_t offset, char* out, size_t len) -> size_t {
        if (offset >= disk.size()) return 0;
        const size_t n = static_cast<size_t>(std::min<uint64_t>(len, disk.size() - offset));
        std::memcpy(out, disk.data() + offset, n);
        return n;
    };
}

static std::string read_range(const Overlay& ov, const std::string& disk, uint64_t offset, size_t len) {
    std::string out(len, '?');
    out.resize(ov.read(disk.size(), reader(disk), offset, &out[0], len));
    return out;
}

static void test_overlay() {
    const std::string disk = "ABCDEFGHIJ";
    Overlay ov;
    CHECK(ov.add(4, "xy"));
    CHECK(ov.size(disk.size()) == 10);
    CHECK(read_range(ov, disk, 0, 10) == "ABCDxyGHIJ");
    CHECK(read_range(ov, disk, 5, 3) == "yGH");  // a read that starts inside a range
    CHECK(ov.add(16, "NEW"));                    // past the end: the file reads as longer, zeros in the gap
    CHECK(ov.size(disk.size()) == 19);
    CHECK(read_range(ov, disk, 8, 11) == std::string("IJ") + std::string(6, '\0') + "NEW");
    CHECK(read_range(ov, disk, 17, 10) == "EW");  // cut at the end
    CHECK(read_range(ov, disk, 19, 4).empty());
    CHECK(!ov.add(5, "zz"));   // overlaps "xy"
    CHECK(!ov.add(15, "zz"));  // overlaps "NEW"
    CHECK(!ov.add(30, ""));
    CHECK(disk == "ABCDEFGHIJ");
}

static void test_replace_file() {
    // An archive of three files; the second is compressed in the archive.
    const std::string disk = std::string("alpha") + "bravo" + "charlie";
    std::vector<IndexEntry> index = {{0, 5, true}, {5, 5, false}, {10, 7, true}};
    const std::vector<IndexEntry> before = index;
    Overlay ov;
    uint64_t end = disk.size();

    CHECK(replace_file(index, 1, "BRAVO, CHANGED", ov, end));
    CHECK(index[1].offset == 32 && index[1].size == 14 && index[1].stored);  // past the end, aligned to 16
    CHECK(end == 46 && ov.size(disk.size()) == 46);
    CHECK(read_range(ov, disk, index[1].offset, static_cast<size_t>(index[1].size)) == "BRAVO, CHANGED");
    // What an offline check does: every entry not replaced is the same and reads the same bytes.
    for (size_t i : {size_t(0), size_t(2)}) {
        CHECK(index[i].offset == before[i].offset && index[i].size == before[i].size && index[i].stored == before[i].stored);
        CHECK(read_range(ov, disk, index[i].offset, static_cast<size_t>(index[i].size)) ==
              disk.substr(static_cast<size_t>(before[i].offset), static_cast<size_t>(before[i].size)));
    }
    // A second file goes after the first.
    CHECK(replace_file(index, 2, "CHARLIE+", ov, end));
    CHECK(index[2].offset == 48 && end == 56);
    CHECK(read_range(ov, disk, 48, 8) == "CHARLIE+");
    CHECK(!replace_file(index, 3, "x", ov, end));  // no such file
    CHECK(disk == "alphabravocharlie");             // the archive itself never changed
}

static void test_settings_file(const fs::path& root) {
    const fs::path path = root / "pc_settings.txt";
    {  // No file yet: defaults, and nothing is written until the player sets something.
        SettingsFile s(path);
        CHECK(s.get("vsync", "low_latency") == "low_latency");
        CHECK(!fs::exists(path));
        CHECK(s.set("vsync", "off"));
        CHECK(read_file(path) == "# PC settings of this port (not the game's save data). key=value\nvsync=off\n");
    }
    {  // A file with a comment and a key this version does not know: both kept, in place.
        write_file(path, "# my notes\r\nvsync = off\r\nfuture_key=1\r\n");
        SettingsFile s(path);
        CHECK(s.get("vsync") == "off");
        CHECK(s.get("future_key") == "1");
        CHECK(!s.set("vsync", "off"));  // the same value: nothing written
        CHECK(read_file(path) == "# my notes\r\nvsync = off\r\nfuture_key=1\r\n");
        CHECK(s.set("vsync", "standard"));
        CHECK(s.set("frame_cap", "60"));
        CHECK(read_file(path) == "# my notes\nvsync=standard\nfuture_key=1\nframe_cap=60\n");
        CHECK(!fs::exists(root / "pc_settings.txt.tmp"));
    }
    {  // Read back.
        SettingsFile s(path);
        CHECK(s.get("vsync") == "standard" && s.get("frame_cap") == "60" && s.get("future_key") == "1");
    }
    {  // Defaults only: the player's file is neither read nor written.
        SettingsFile s(path, true);
        CHECK(s.get("vsync", "low_latency") == "low_latency");
        CHECK(s.set("vsync", "off"));
        CHECK(read_file(path) == "# my notes\nvsync=standard\nfuture_key=1\nframe_cap=60\n");
    }
}

static void test_rows(const fs::path& root) {
    // The file keeps an old word's meaning: "fullscreen" stayed the borderless window when exclusive fullscreen came.
    const Row mode{"display_mode", {"exclusive", "fullscreen", "window"}, 2};
    CHECK(shown_choice(mode, "") == 2);
    CHECK(shown_choice(mode, "fullscreen") == 1);
    CHECK(shown_choice(mode, "fullscreen", "window") == 2);  // what runs wins (a hotkey changed it)
    CHECK(shown_choice(mode, "tv") == -1);                   // custom: shown as such, kept

    const auto none = [](int) { return false; };
    const auto no_exclusive = [](int i) { return i == 0; };  // the driver offers no exclusive mode
    CHECK(step(mode, 2, +1, none) == 0);                     // lists wrap
    CHECK(step(mode, 2, +1, no_exclusive) == 1);
    CHECK(step(mode, 1, -1, no_exclusive) == 2);
    CHECK(step(mode, -1, +1, no_exclusive) == 1);            // from custom: right goes to the first it can
    CHECK(step(mode, -1, -1, none) == 2);                    // and left to the last
    CHECK(step(mode, 1, +1, [](int i) { return i != 1; }) == 1);  // nothing else: stays, the "limit" sound

    const Row cap{"frame_cap", {"30"}, 0, Takes::next_frame};
    CHECK(step(cap, 0, +1, none) == 0);
    CHECK(!shows_arrows(cap, {}));
    CHECK(shows_arrows(mode, {}));

    // A row that only opens a page has no values: nothing may read words[0] (a crash we had at start-up).
    const Row keys{"", {}, 0, Takes::at_once, true};
    CHECK(step(keys, 0, +1, none) == 0);
    CHECK(!shows_arrows(keys, {}));
    CHECK(selectable(keys, true) && !selectable(keys, false));
    CHECK(greyed(keys, {}, false) && !greyed(keys, {}, true));

    // Greyed rows: a placeholder is skipped by the cursor; a row held by another can be selected to read why.
    const Row placeholder{"", {}, 0};
    const Row aa{"anti_aliasing", {"taa", "smaa", "off"}, 0, Takes::next_start};
    RowState held;
    held.held = true;
    CHECK(greyed(aa, held) && selectable(aa) && !shows_arrows(aa, held));
    CHECK(greyed(placeholder, {}) && !selectable(placeholder));

    const std::vector<const Row*> page = {&mode, &placeholder, &cap, &aa};
    const auto can = [&](int i) { return selectable(*page[static_cast<size_t>(i)]); };
    CHECK(move_cursor(4, 0, +1, can) == 2);   // past the placeholder
    CHECK(move_cursor(4, 3, +1, can) == 0);   // wraps
    CHECK(move_cursor(4, 0, -1, can) == 3);
    CHECK(move_cursor(4, 0, +1, [](int i) { return i == 0; }) == 0);

    // The caption: what stops the player before what only informs them.
    RowState s;
    CHECK(caption_for(s) == Caption::normal);
    s.kept_for_later = true;
    CHECK(caption_for(s) == Caption::kept_for_later);
    s.waits_for_start = true;
    CHECK(caption_for(s) == Caption::at_next_start);
    s.held = true;
    CHECK(caption_for(s) == Caption::held);
    s.missing = true;
    CHECK(caption_for(s) == Caption::unavailable);

    // Choosing: put to work first, saved after; nothing saved when it cannot be put to work.
    const fs::path path = root / "rows.txt";
    SettingsFile file(path);
    int applied = 0;
    CHECK(!choose(mode, 0, file, [&](const std::string&) { applied++; return false; }));
    CHECK(applied == 1 && !file.has("display_mode") && !fs::exists(path));
    CHECK(choose(mode, 1, file, [&](const std::string& w) { applied++; return w == "fullscreen"; }));
    CHECK(applied == 2 && file.get("display_mode") == "fullscreen");
    CHECK(choose(aa, 1, file, [&](const std::string&) { applied++; return false; }));  // next start: saved, not applied
    CHECK(applied == 2 && file.get("anti_aliasing") == "smaa");
    CHECK(!choose(keys, 0, file, [&](const std::string&) { return true; }));

    // Restore this page's defaults: only rows away from their default change.
    std::vector<std::string> put;
    const int changed = restore_defaults({mode, cap, aa}, file, [&](const Row& r, const std::string& w) {
        put.push_back(r.key + "=" + w);
        return true;
    });
    CHECK(changed == 2);
    CHECK(put.size() == 1 && put[0] == "display_mode=window");  // anti-aliasing takes effect at the next start
    CHECK(file.get("display_mode") == "window" && file.get("anti_aliasing") == "taa" && !file.has("frame_cap"));
}

static void test_bindings() {
    // Sets: 0 = play (movement and buttons on together), 2 = the menu's keys (on alone).
    const KeyMap km({{"move_up", 1, 0, "W"},
                     {"move_down", 1, 0, "S"},
                     {"btn_x", 2, 0, "J"},
                     {"btn_y", 2, 0, "K"},
                     {"menu_up", 5, 2, "W"},
                     {"menu_confirm", 5, 2, "Space"},
                     {"menu_back", 5, 2, "Esc"}},
                    {"F11", "Alt", "Win", "Esc"});
    const int move_up = km.find("move_up"), btn_x = km.find("btn_x"), menu_up = km.find("menu_up"),
              confirm = km.find("menu_confirm"), back = km.find("menu_back");
    Bindings b = km.defaults();
    CHECK(km.all_default(b) && km.to_text(b).empty());  // all defaults: no file

    CHECK(!km.allowed(move_up, "F11"));
    CHECK(km.allowed(back, "Esc"));  // a reserved key that is the action's own default
    CHECK(!km.allowed(confirm, "Esc"));

    // A swap: J was btn_x's, so btn_x gets move_up's old W.
    CHECK(km.bind(b, move_up, "J") == btn_x);
    CHECK(b.key[static_cast<size_t>(move_up)] == "J" && b.key[static_cast<size_t>(btn_x)] == "W");
    CHECK(b.key[static_cast<size_t>(menu_up)] == "W");  // another set: no conflict
    CHECK(km.to_text(b) == "move_up=J\nbtn_x=W\n");

    // Read back the same.
    std::vector<std::string> problems;
    const Bindings again = km.from_text(km.to_text(b), &problems);
    CHECK(problems.empty() && again.key == b.key);

    // Restoring only the movement page: move_up takes W back, so btn_x is moved to its own J, now free.
    CHECK(km.reset_group(b, 1) == 1);
    CHECK(b.key[static_cast<size_t>(move_up)] == "W" && b.key[static_cast<size_t>(btn_x)] == "J");
    CHECK(km.all_default(b));

    // The old key is not one the other may have: it gets its own default, or none (conflicted) while that is taken.
    CHECK(km.bind(b, back, "Space") == confirm);
    CHECK(b.key[static_cast<size_t>(confirm)].empty() && b.conflicted[static_cast<size_t>(confirm)]);
    CHECK(km.to_text(b) == "menu_back=Space\n");  // a conflict is not the player's choice: not written
    CHECK(km.bind(b, back, "Enter") == -1);
    CHECK(b.key[static_cast<size_t>(confirm)] == "Space" && !b.conflicted[static_cast<size_t>(confirm)]);  // given back

    // A file that gives an action another's default: the player's choice wins, the other has no key.
    problems.clear();
    const Bindings f = km.from_text("move_up=J\nnot_an_action=Q\nbtn_y=F11\nmove_down=J\n", &problems);
    CHECK(f.key[static_cast<size_t>(move_up)] == "J");
    CHECK(f.key[static_cast<size_t>(btn_x)].empty() && f.conflicted[static_cast<size_t>(btn_x)]);
    CHECK(f.key[static_cast<size_t>(km.find("btn_y"))] == "K");      // F11 refused
    CHECK(f.key[static_cast<size_t>(km.find("move_down"))] == "S");  // J given twice
    CHECK(problems.size() == 3);
}

static void test_key_wait() {
    KeyWait w;
    w.start(10.0);
    CHECK(!w.game_gets_keys(false));
    CHECK(w.on_key("F11", true) == KeyWait::Result::refused && w.waiting());
    CHECK(w.on_key("J", false) == KeyWait::Result::answered && w.answer() == "J" && !w.waiting());
    CHECK(!w.game_gets_keys(true));  // J is still down: the game must not see it as a press
    CHECK(w.game_gets_keys(false));  // every key up
    CHECK(w.game_gets_keys(true));   // new presses go to the game again

    w.start(20.0);
    CHECK(!w.timed_out(24.9));
    CHECK(w.timed_out(25.0) && !w.waiting());
    CHECK(!w.timed_out(26.0));
    CHECK(w.game_gets_keys(false));

    w.start(30.0);
    CHECK(w.on_key("Esc", false) == KeyWait::Result::cancelled && !w.waiting() && w.answer().empty());
    CHECK(w.on_key("J", false) == KeyWait::Result::not_waiting);
}

int main() {
    const fs::path root = fs::temp_directory_path() / "settings_guide_test";
    fs::remove_all(root);
    fs::create_directories(root);
    test_overlay();
    test_replace_file();
    test_settings_file(root);
    test_rows(root);
    test_bindings();
    test_key_wait();
    fs::remove_all(root);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
