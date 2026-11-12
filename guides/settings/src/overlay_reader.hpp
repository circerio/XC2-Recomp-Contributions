// Serving changed copies of a game's files without changing the files on disk. Reference code for
// guides/settings/README.md, section 3.
//
// Many games read their data out of one large archive: an index gives each file's offset and size, and the game reads
// that range. To change one file without writing to the archive:
//   1. build the file's new bytes in memory, from the player's own copy of the original;
//   2. serve a changed copy of the index (small), in which that file's entry points at a range past the archive's end;
//   3. answer reads of that range from memory (an Overlay), so the archive reads as longer than it is.
// The archive and the index on disk stay as they were, and no game data has to be shipped.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace settings_guide {

// Reads `len` bytes at `offset` of the file on disk into `out`; returns how many it read (fewer at its end).
using BaseRead = std::function<size_t(uint64_t offset, char* out, size_t len)>;

// Byte ranges laid over one file. A range may start at or past the file's end; the file then reads as longer, with zeros
// in any gap between its real end and the range.
class Overlay {
public:
    // Adds `bytes` at `offset`. False, and nothing added, when the range is empty or overlaps one already there.
    bool add(uint64_t offset, std::string bytes) {
        if (bytes.empty()) return false;
        const uint64_t end = offset + bytes.size();
        auto next = ranges_.lower_bound(offset);
        if (next != ranges_.end() && next->first < end) return false;
        if (next != ranges_.begin()) {
            auto prev = std::prev(next);
            if (prev->first + prev->second.size() > offset) return false;
        }
        ranges_.emplace(offset, std::move(bytes));
        return true;
    }

    // The file's size as the game should see it: the size on disk, or the end of the last range past it.
    uint64_t size(uint64_t base_size) const {
        if (ranges_.empty()) return base_size;
        const auto& last = *ranges_.rbegin();
        return std::max(base_size, last.first + last.second.size());
    }

    // Reads [offset, offset + len) as the game should see it. Returns the number of bytes read: fewer than `len` only
    // at the end of size(base_size).
    size_t read(uint64_t base_size, const BaseRead& base_read, uint64_t offset, char* out, size_t len) const {
        const uint64_t total = size(base_size);
        if (offset >= total) return 0;
        len = static_cast<size_t>(std::min<uint64_t>(len, total - offset));
        // The file on disk first, zeros past its end; then the ranges over both.
        size_t from_base = 0;
        if (offset < base_size) {
            const size_t want = static_cast<size_t>(std::min<uint64_t>(len, base_size - offset));
            from_base = base_read(offset, out, want);
            if (from_base < want) return from_base;  // the disk read failed short: report what is there
        }
        std::memset(out + from_base, 0, len - from_base);
        const uint64_t end = offset + len;
        for (auto it = first_touching(offset); it != ranges_.end() && it->first < end; ++it) {
            const uint64_t a = std::max(offset, it->first);
            const uint64_t b = std::min(end, it->first + it->second.size());
            if (a < b) std::memcpy(out + (a - offset), it->second.data() + (a - it->first), static_cast<size_t>(b - a));
        }
        return len;
    }

private:
    std::map<uint64_t, std::string> ranges_;  // offset -> bytes; never overlapping

    std::map<uint64_t, std::string>::const_iterator first_touching(uint64_t offset) const {
        auto it = ranges_.upper_bound(offset);
        if (it != ranges_.begin()) {
            auto prev = std::prev(it);
            if (prev->first + prev->second.size() > offset) return prev;
        }
        return it;
    }
};

// One file in an example archive index. Real formats differ; what matters is that the entry can say "stored as is".
struct IndexEntry {
    uint64_t offset = 0;
    uint64_t size = 0;      // bytes in the archive
    bool stored = true;     // false: compressed in the archive (the game unpacks it); true: read as is
};

// Replaces file `i` of `index` with `bytes`: they go past everything placed so far (`archive_end`, aligned up to
// `align`), the entry points there and says "stored as is", and the overlay serves them. Pick an entry form the game
// already reads: in the game we worked on, many files were stored uncompressed already, so a replaced file is stored
// that way too and needs no compressor. False when `i` is not in the index.
inline bool replace_file(std::vector<IndexEntry>& index, size_t i, std::string bytes, Overlay& overlay,
                         uint64_t& archive_end, uint64_t align = 16) {
    if (i >= index.size() || bytes.empty() || align == 0) return false;
    const uint64_t at = (archive_end + align - 1) / align * align;
    const uint64_t n = bytes.size();
    if (!overlay.add(at, std::move(bytes))) return false;
    index[i] = IndexEntry{at, n, true};
    archive_end = at + n;
    return true;
}

}  // namespace settings_guide
