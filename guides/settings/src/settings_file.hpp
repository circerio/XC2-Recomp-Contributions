// The port's own settings file: one "key=value" per line. Reference code for guides/settings/README.md, section 5.
//
// The PC settings never go into the game's save data. This file is the runtime's own:
//   - each change is written at once, to a temporary file renamed over the old one;
//   - lines the player wrote, comments and keys this version does not know are kept as they are, in their order;
//   - a key is written only once the player sets it; setting the value it already has writes nothing;
//   - a test switch can make it "defaults only": nothing read, nothing written.
// How hard to push the file to the disk (flushes, the folder's entry) is the saves guide's subject; a settings file
// that is lost costs the player a few choices, not progress.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace settings_guide {

namespace fs = std::filesystem;

class SettingsFile {
public:
    // Reads `path` if it is there. `defaults_only`: never read or write it (for automated tests and regressions, so
    // they run with every setting at its default whatever a player chose).
    explicit SettingsFile(fs::path path, bool defaults_only = false) : path_(std::move(path)), off_(defaults_only) {
        if (off_) return;
        std::ifstream f(path_, std::ios::binary);
        if (!f) return;
        std::stringstream s;
        s << f.rdbuf();
        parse(s.str());
    }

    // The value of `key`, or `fallback` when the file does not set it.
    std::string get(const std::string& key, const std::string& fallback = "") const {
        auto it = values_.find(key);
        return it == values_.end() ? fallback : it->second;
    }
    bool has(const std::string& key) const { return values_.count(key) != 0; }

    // Sets `key` and writes the file. Returns false when nothing changed (the same value) or writing failed.
    bool set(const std::string& key, const std::string& value) {
        auto it = values_.find(key);
        if (it != values_.end() && it->second == value) return false;
        values_[key] = value;
        const std::string ln = key + "=" + value;
        if (auto l = line_of_.find(key); l != line_of_.end()) {
            lines_[l->second] = ln;  // replaced where it is
        } else {
            if (lines_.empty()) lines_.push_back("# PC settings of this port (not the game's save data). key=value");
            line_of_[key] = lines_.size();
            lines_.push_back(ln);
        }
        return save();
    }

    const std::vector<std::string>& lines() const { return lines_; }

    // Writes every line to "<path>.tmp", then renames it over the file: a reader sees the old file or the new one,
    // never half of one. std::filesystem::rename replaces an existing file as POSIX rename does.
    bool save() const {
        if (off_) return true;  // defaults only: never written, and that is not a failure
        std::error_code ec;
        if (path_.has_parent_path()) fs::create_directories(path_.parent_path(), ec);
        fs::path tmp = path_;
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            for (const std::string& ln : lines_) f << ln << '\n';
            f.flush();
            if (!f) return false;
        }
        fs::rename(tmp, path_, ec);
        return !ec;
    }

private:
    fs::path path_;
    bool off_ = false;
    std::vector<std::string> lines_;          // the file as read, settings lines replaced in place
    std::map<std::string, size_t> line_of_;   // key -> its line in lines_
    std::map<std::string, std::string> values_;

    static std::string trim(const std::string& s) {
        const size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }

    void parse(const std::string& text) {
        std::istringstream in(text);
        for (std::string ln; std::getline(in, ln);) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            const size_t eq = ln.find('=');
            const std::string key = eq == std::string::npos ? "" : trim(ln.substr(0, eq));
            if (!key.empty() && key[0] != '#') {
                values_[key] = trim(ln.substr(eq + 1));  // a key given twice: the last one counts
                line_of_[key] = lines_.size();
            }
            lines_.push_back(ln);
        }
    }
};

}  // namespace settings_guide
