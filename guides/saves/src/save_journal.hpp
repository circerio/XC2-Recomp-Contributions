// Save data that survives a crash between a write and its commit, for a port that writes the game's saves to plain files.
// Reference code for guides/saves/README.md. Standard library only, plus the OS calls that flush to the disk. The POSIX
// branches follow the manuals but have not been built or run by us: our only test machine runs Windows.
//
// On the console a write to save data counts only when the game commits it; a crash before that leaves the last committed
// files. Writing straight into files loses that: a crash halfway through a write leaves a torn file. The journal keeps the
// committed copy of each file the game touches until the commit:
//
//   before the first write, create or delete of a file since the last commit:  journal.before_change(name)
//   when the game commits:                                                     journal.commit(written_names)
//   when the save data is mounted, before the game reads anything:             journal.recover()
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace saves_guide {

namespace fs = std::filesystem;

// Make sure what was written to a file has reached the disk, not only the OS's cache.
inline bool flush_to_disk(const fs::path& p) {
#ifdef _WIN32
    int fd = -1;
    if (_wsopen_s(&fd, p.c_str(), _O_RDWR | _O_BINARY, _SH_DENYNO, 0) != 0) return false;
    const bool ok = _commit(fd) == 0;
    _close(fd);
#else
    const int fd = ::open(p.c_str(), O_RDWR);
    if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
#endif
    return ok;
}

// Make sure a folder's entries (files created, renamed or deleted in it) have reached the disk. On Linux an fsync of the
// file alone does not promise that (fsync(2)). On Windows this does nothing beyond checking the folder exists: we have
// not found or tested an equivalent there.
inline bool flush_dir(const fs::path& p) {
#ifdef _WIN32
    return fs::is_directory(p);
#else
    const int fd = ::open(p.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
#endif
}

// A rename is atomic only within one file system, so the journal must be on the same volume as the saves. Check it once
// at start-up. On Windows this compares drive names only, which misses a folder mounted as another volume.
inline bool same_volume(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
    auto drive = [](const fs::path& p) {
        std::wstring d = fs::absolute(p).root_name().wstring();
        for (wchar_t& c : d) c = towlower(c);
        return d;
    };
    return drive(a) == drive(b);
#else
    struct stat sa, sb;
    return ::stat(a.c_str(), &sa) == 0 && ::stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev;
#endif
}

class SaveJournal {
public:
    // save: the folder the game's save files are in. journal: a folder next to it, not inside it, so that the save folder
    // only ever holds the game's own files. broken: where replaced files are kept instead of being deleted.
    SaveJournal(fs::path save, fs::path journal, fs::path broken)
        : save_(std::move(save)), journal_(std::move(journal)), broken_(std::move(broken)) {}

    // Before the first change to `name` since the last commit: keep its committed bytes (or note that it did not exist),
    // and put both on the disk before the game's write starts.
    bool before_change(const std::string& name) {
        if (touched_.count(name)) return true;
        fs::create_directories(journal_ / "files");
        const fs::path src = save_ / name, copy = journal_ / "files" / name;
        const bool existed = fs::exists(src);
        if (existed) {
            fs::copy_file(src, copy, fs::copy_options::overwrite_existing);
            if (!flush_to_disk(copy)) return false;
        }
        {
            std::ofstream list(journal_ / "pending.txt", std::ios::app | std::ios::binary);
            list << name << '\t' << (existed ? 1 : 0) << '\n';
        }
        if (!flush_to_disk(journal_ / "pending.txt")) return false;
        // The new entries too: the copy, the list, and the journal folder itself.
        if (!flush_dir(journal_ / "files") || !flush_dir(journal_) || !flush_dir(journal_.parent_path())) return false;
        touched_.insert(name);
        return true;
    }

    // The game committed: put its written files on the disk, then drop the journal.
    bool commit(const std::vector<std::string>& written) {
        for (const std::string& name : written)
            if (fs::exists(save_ / name) && !flush_to_disk(save_ / name)) return false;
        std::error_code ec;
        fs::remove_all(journal_, ec);
        touched_.clear();
        // If the journal's removal did not reach the disk, a crash now would roll this commit back at the next start.
        return !ec && flush_dir(journal_.parent_path());
    }

    // At mount: a journal left behind means the last run ended between a write and its commit. Each listed file goes
    // back to its committed bytes (or away, if it did not exist then); what was there is moved to `broken_/<stamp>`.
    // Safe to run again if interrupted: a file already put back has no copy left, and is skipped. Returns the names.
    std::vector<std::string> recover(const std::string& stamp) {
        std::vector<std::string> restored;
        std::ifstream list(journal_ / "pending.txt", std::ios::binary);
        if (!list) return restored;
        std::string line;
        while (std::getline(list, line)) {
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            const std::string name = line.substr(0, tab);
            const bool existed = line.compare(tab + 1, std::string::npos, "1") == 0;
            const fs::path copy = journal_ / "files" / name, dst = save_ / name;
            if (existed && !fs::exists(copy)) continue;  // already put back
            if (fs::exists(dst)) {
                fs::create_directories(broken_ / stamp);
                fs::rename(dst, broken_ / stamp / name);
            }
            if (existed) fs::rename(copy, dst);
            restored.push_back(name);
        }
        list.close();
        flush_dir(save_);
        if (fs::exists(broken_ / stamp)) flush_dir(broken_ / stamp);
        std::error_code ec;
        fs::remove_all(journal_, ec);
        flush_dir(journal_.parent_path());
        return restored;
    }

private:
    fs::path save_, journal_, broken_;
    std::set<std::string> touched_;
};

}  // namespace saves_guide
