// The first lines of a crash report, written from inside a crash. Reference code for guides/saves/README.md.
//
// The thread that crashed may have no stack left, or may hold the heap's or the loader's lock. So everything the report
// needs is gathered at start-up into fixed buffers (RunContext), and the crash path only formats into a buffer it was
// given: no allocation, no new threads, no library loads. Write the result with the OS's plain file calls.
//
// This code uses snprintf to stay readable. POSIX does not list snprintf as async-signal-safe, and a C runtime may take
// a lock inside it (for its locale, for example). A real crash handler is better off with a small formatter of its own
// that only copies text and writes digits.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace saves_guide {

// Filled once at start-up. Each field is plain text; anything longer than its buffer is cut and ends in "...".
struct RunContext {
    char machine[256] = "";   // OS version, CPU, memory
    char program[256] = "";   // executable, its build time, the data folder
    char game[256] = "";      // game and its build, GPU and driver
    char content[1024] = "";  // installed add-ons, mods in use, the run's settings (one per line)

    static void put(char* dst, size_t cap, const std::string& text) {
        if (text.size() < cap) {
            std::memcpy(dst, text.c_str(), text.size() + 1);
            return;
        }
        std::memcpy(dst, text.c_str(), cap - 4);
        std::memcpy(dst + cap - 4, "...", 4);
    }
};

// What the crash handler knows when it runs.
struct CrashInfo {
    uint32_t code = 0;            // the exception code (Windows), or a signal number
    const char* module = "";      // the module the faulting address is in
    uint64_t offset = 0;          // the faulting address's offset in it
    bool has_address = false;     // an access violation: what it tried to touch
    bool write = false;
    uint64_t address = 0;
    const char* when = "";        // the time, formatted at start-up and refreshed by a timer, not in the crash path
    uint64_t seconds_played = 0;
    uint64_t frames = 0;
};

inline const char* exception_name(uint32_t code) {
    switch (code) {
        case 0xC0000005u: return "access violation";
        case 0xC00000FDu: return "stack overflow";
        case 0xC0000094u: return "integer divide by zero";
        case 0xC000001Du: return "illegal instruction";
        case 0x80000003u: return "breakpoint";
        default: return "unhandled exception";
    }
}

// Formats the report's head into out (capacity cap). Returns the length written, cut to fit.
inline size_t format_report_head(char* out, size_t cap, const CrashInfo& c, const RunContext& s) {
    if (!cap) return 0;
    size_t n = 0;  // never more than cap - 1, so out + n always has room for the terminating zero
    const auto put = [&](int written) {
        if (written > 0) n = std::min(cap - 1, n + size_t(written));
    };
    put(std::snprintf(out, cap, "stopped: %s (exception %08x) at %s+%llx", exception_name(c.code), c.code, c.module,
                      static_cast<unsigned long long>(c.offset)));
    if (c.has_address)
        put(std::snprintf(out + n, cap - n, ", %s address %llx", c.write ? "writing" : "reading",
                          static_cast<unsigned long long>(c.address)));
    put(std::snprintf(out + n, cap - n, "\nwhen: %s, after %llu:%02llu played, %llu frames shown\n", c.when,
                      static_cast<unsigned long long>(c.seconds_played / 60),
                      static_cast<unsigned long long>(c.seconds_played % 60), static_cast<unsigned long long>(c.frames)));
    put(std::snprintf(out + n, cap - n, "%s",
                      "Please send this file together with the .log and .dmp of the same name. They do not include your\n"
                      "save files. The .dmp holds part of the program's memory at the moment of the crash, and the files\n"
                      "contain folder names from this computer, which may include your user name.\n\n"));
    put(std::snprintf(out + n, cap - n, "[This run]\n%s\n%s\n%s\n%s\n", s.machine, s.program, s.game, s.content));
    return n;
}

// Whether to tell the player. Never for scripted input, automated tests, or a run that records its own markers.
struct NoticeFacts {
    bool scripted_input = false;
    bool automated_test = false;
    bool recording_run = false;
    int forced = -1;  // -1 decide, 0 never, 1 always
};

inline bool tell_player(const NoticeFacts& f) {
    if (f.forced >= 0) return f.forced == 1;
    return !f.scripted_input && !f.automated_test && !f.recording_run;
}

}  // namespace saves_guide
