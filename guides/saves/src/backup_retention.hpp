// Which run backups of the save data to keep. Reference code for guides/saves/README.md.
//
// Before the first write of a run, the save files are copied into a folder named after the run's start,
// "yyyymmdd-hhmmss". Kept: the newest 10 folders, and the newest folder of each earlier day for 30 days. Only folders
// with that exact form of name are ever considered, so nothing a player put there is deleted.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace saves_guide {

inline bool is_backup_name(const std::string& s) {
    if (s.size() != 15 || s[8] != '-') return false;
    for (size_t i = 0; i < s.size(); i++)
        if (i != 8 && !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// The folders to delete. `oldest_day_kept` is "yyyymmdd" for today minus 30 days; the caller works it out from its clock.
inline std::vector<std::string> backups_to_delete(std::vector<std::string> names, const std::string& oldest_day_kept,
                                                  size_t keep_newest = 10) {
    names.erase(std::remove_if(names.begin(), names.end(), [](const std::string& n) { return !is_backup_name(n); }),
                names.end());
    std::sort(names.begin(), names.end(), std::greater<>());  // the names sort by time; newest first
    std::vector<std::string> remove;
    std::string last_day_kept;
    for (size_t i = 0; i < names.size(); i++) {
        const std::string day = names[i].substr(0, 8);
        const bool newest = i < keep_newest;
        const bool day_newest = day != last_day_kept && day >= oldest_day_kept;
        if (newest || day_newest) last_day_kept = day;
        else remove.push_back(names[i]);
    }
    return remove;
}

}  // namespace saves_guide
