// The save journal on real files in a temporary folder, the backup retention rule, and the crash report's head.
// SPDX-License-Identifier: MIT
#include "backup_retention.hpp"
#include "crash_report.hpp"
#include "save_journal.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace saves_guide;

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

int main() {
    const fs::path root = fs::temp_directory_path() / "saves_guide_test";
    fs::remove_all(root);
    const fs::path save = root / "save", journal = root / "save_journal", broken = root / "save_broken";
    fs::create_directories(save);

    {  // A normal save: the journal holds the committed copy until the commit, then nothing is left.
        write_file(save / "game.sav", "committed-1");
        SaveJournal j(save, journal, broken);
        CHECK(j.before_change("game.sav"));
        CHECK(read_file(journal / "files" / "game.sav") == "committed-1");
        write_file(save / "game.sav", "committed-2");
        CHECK(j.commit({"game.sav"}));
        CHECK(!fs::exists(journal));
        CHECK(read_file(save / "game.sav") == "committed-2");
        // Nothing to recover at the next mount.
        CHECK(SaveJournal(save, journal, broken).recover("t0").empty());
    }
    {  // A crash halfway through a write: at the next mount the file goes back, and the torn one is kept aside.
        SaveJournal j(save, journal, broken);
        CHECK(j.before_change("game.sav"));
        write_file(save / "game.sav", std::string("commit") + std::string(5, '\0'));  // torn: the rest never written
        // ... the process ends here, without a commit.
        const auto restored = SaveJournal(save, journal, broken).recover("t1");
        CHECK(restored.size() == 1 && restored[0] == "game.sav");
        CHECK(read_file(save / "game.sav") == "committed-2");
        CHECK(read_file(broken / "t1" / "game.sav").size() == 11);
        CHECK(!fs::exists(journal));
    }
    {  // A file the game created and never committed goes away again; one it deleted comes back.
        write_file(save / "old.sav", "keep me");
        SaveJournal j(save, journal, broken);
        CHECK(j.before_change("new.sav"));
        write_file(save / "new.sav", "half");
        CHECK(j.before_change("old.sav"));
        fs::remove(save / "old.sav");
        SaveJournal(save, journal, broken).recover("t2");
        CHECK(!fs::exists(save / "new.sav"));
        CHECK(read_file(save / "old.sav") == "keep me");
        CHECK(fs::exists(broken / "t2" / "new.sav"));
    }
    {  // Recovery stopped halfway can run again: a file already put back is skipped, the rest still go back.
        write_file(save / "a.sav", "A1");
        write_file(save / "b.sav", "B1");
        SaveJournal j(save, journal, broken);
        CHECK(j.before_change("a.sav") && j.before_change("b.sav"));
        write_file(save / "a.sav", "torn");
        write_file(save / "b.sav", "torn");
        fs::create_directories(broken / "t3");
        fs::rename(save / "a.sav", broken / "t3" / "a.sav");        // the first file had been put back ...
        fs::rename(journal / "files" / "a.sav", save / "a.sav");    // ... when the recovery itself was cut off
        const auto restored = SaveJournal(save, journal, broken).recover("t4");
        CHECK(restored.size() == 1 && restored[0] == "b.sav");
        CHECK(read_file(save / "a.sav") == "A1" && read_file(save / "b.sav") == "B1");
    }
    {  // Folder flushes, and the journal on the same volume as the saves (a rename is atomic only within one).
        CHECK(flush_dir(save));
        CHECK(!flush_dir(root / "missing"));
        CHECK(same_volume(save, root));
    }
    fs::remove_all(root);

    {  // Backups: the newest 10, and the newest of each earlier day for 30 days; other names are never touched.
        std::vector<std::string> names = {"my notes", "20261001-090000"};
        for (int h = 0; h < 12; h++) {
            char n[16];
            std::snprintf(n, sizeof n, "20261110-%02d0000", h);
            names.push_back(n);
        }
        names.push_back("20261105-080000");
        names.push_back("20261105-200000");
        const auto del = backups_to_delete(names, "20261011");
        // 20261110-00 and -01 are past the newest 10, and their day already kept its newest folder.
        // 20261105-08 is not that day's newest; 20261001 is older than 30 days.
        CHECK(del.size() == 4);
        CHECK(std::find(del.begin(), del.end(), "20261110-000000") != del.end());
        CHECK(std::find(del.begin(), del.end(), "20261110-010000") != del.end());
        CHECK(std::find(del.begin(), del.end(), "20261105-080000") != del.end());
        CHECK(std::find(del.begin(), del.end(), "20261001-090000") != del.end());
        CHECK(std::find(del.begin(), del.end(), "my notes") == del.end());
        CHECK(!is_backup_name("20261110-12000") && !is_backup_name("2026111O-120000"));
    }
    {  // The report's head, into a fixed buffer.
        RunContext s;
        RunContext::put(s.machine, sizeof s.machine, "Windows 11, 8 cores, 32 GB");
        RunContext::put(s.content, sizeof s.content, std::string(2000, 'x'));
        CHECK(std::strlen(s.content) == sizeof s.content - 1 && std::strcmp(s.content + sizeof s.content - 4, "...") == 0);
        CrashInfo c;
        c.code = 0xC0000005u, c.module = "game.exe", c.offset = 0x1234, c.has_address = true, c.write = true;
        c.when = "2026-11-12 10:00", c.seconds_played = 87, c.frames = 2599;
        char out[4096];
        const size_t n = format_report_head(out, sizeof out, c, s);
        const std::string head(out, n);
        CHECK(head.rfind("stopped: access violation (exception c0000005) at game.exe+1234, writing address 0", 0) == 0);
        CHECK(head.find("after 1:27 played, 2599 frames shown") != std::string::npos);
        CHECK(head.find("[This run]\nWindows 11") != std::string::npos);
        // What the player is told the files hold: no save files, but part of the program's memory and folder names.
        CHECK(head.find("They do not include your\nsave files.") != std::string::npos);
        CHECK(head.find("part of the program's memory") != std::string::npos);
        char small[64];
        CHECK(format_report_head(small, sizeof small, c, s) == sizeof small - 1);  // cut, never past the buffer
    }
    {  // Telling the player.
        CHECK(tell_player({}));
        CHECK(!tell_player({true, false, false, -1}));
        CHECK(!tell_player({false, true, false, -1}));
        CHECK(!tell_player({false, false, true, -1}));
        CHECK(tell_player({true, false, false, 1}));
        CHECK(!tell_player({false, false, false, 0}));
    }

    if (failures == 0) std::printf("all save and crash report checks passed\n");
    return failures == 0 ? 0 : 1;
}
