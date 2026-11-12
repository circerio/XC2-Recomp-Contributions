// Keyboard rebinding for a settings page: swaps, reserved keys, restoring one page, the file, and waiting for a key.
// Reference code for guides/settings/README.md, section 9.
//
// Rules:
//   - Actions that are active at the same time form a set; within a set no key ever belongs to two actions. A group
//     that is active alone (the menu's keys, say) is a set of its own and may reuse keys of the others.
//   - Giving an action a key another action of its set has: that action gets the first one's old key (a swap). When the
//     old key is not one it may have, or there was none, it gets its own default; when that is taken too, it gets no key
//     and is "conflicted" until its default is free again.
//   - Reserved keys (the port's own: fullscreen, the OS's keys) are refused.
//   - Restoring one page's defaults also moves any other action off the keys that page takes back.
//   - The file holds only what the player changed; all defaults means no file.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace settings_guide {

struct Action {
    std::string id;           // the file's name for it
    int group = 0;            // the page it is on
    int set = 0;              // actions active at the same time share a set
    std::string default_key;  // "" for none
};

struct Bindings {
    std::vector<std::string> key;  // per action; "" for none
    std::vector<bool> conflicted;  // no key only because its default is another action's: not the player's choice
};

class KeyMap {
public:
    KeyMap(std::vector<Action> actions, std::set<std::string> reserved)
        : actions_(std::move(actions)), reserved_(std::move(reserved)) {}

    int count() const { return static_cast<int>(actions_.size()); }
    const Action& action(int i) const { return actions_[static_cast<size_t>(i)]; }
    int find(const std::string& id) const {
        for (int i = 0; i < count(); i++)
            if (actions_[static_cast<size_t>(i)].id == id) return i;
        return -1;
    }

    Bindings defaults() const {
        Bindings b;
        for (const Action& a : actions_) b.key.push_back(a.default_key);
        b.conflicted.assign(actions_.size(), false);
        return b;
    }

    // Whether `action` may have `key`: not a reserved key, unless it is the action's own default.
    bool allowed(int action, const std::string& key) const {
        return !key.empty() && (reserved_.count(key) == 0 || key == actions_[static_cast<size_t>(action)].default_key);
    }

    // Gives `action` the key `key` (check allowed() first). Returns the other action that had it, or -1.
    int bind(Bindings& b, int action, const std::string& key) const {
        const size_t a = static_cast<size_t>(action);
        const std::string old = b.conflicted[a] ? std::string() : b.key[a];
        const int other = holder(b, actions_[a].set, key, action);
        b.key[a] = key;
        b.conflicted[a] = false;
        if (other >= 0) {
            const size_t o = static_cast<size_t>(other);
            std::string next = !old.empty() && allowed(other, old) ? old : actions_[o].default_key;
            if (!next.empty() && holder(b, actions_[o].set, next, other) >= 0) next.clear();
            b.key[o] = next;
            b.conflicted[o] = next.empty();
        }
        give_back_defaults(b);
        return other;
    }

    // Every action of `group` back to its default key. An action of another group that had one of those keys gets its
    // own default, or no key (conflicted) while that is taken, so that no key is ever two actions' in a set. Returns the
    // number of other actions so moved.
    int reset_group(Bindings& b, int group) const {
        for (size_t i = 0; i < actions_.size(); i++)
            if (actions_[i].group == group) {
                b.key[i] = actions_[i].default_key;
                b.conflicted[i] = false;
            }
        int moved = 0;
        for (size_t i = 0; i < actions_.size(); i++) {
            if (actions_[i].group == group || b.key[i].empty()) continue;
            const int other = holder(b, actions_[i].set, b.key[i], static_cast<int>(i));
            if (other < 0 || actions_[static_cast<size_t>(other)].group != group) continue;
            std::string next = actions_[i].default_key;
            if (!next.empty() && holder(b, actions_[i].set, next, static_cast<int>(i)) >= 0) next.clear();
            b.key[i] = next;
            b.conflicted[i] = next.empty();
            moved++;
        }
        give_back_defaults(b);
        return moved;
    }

    bool all_default(const Bindings& b) const {
        for (size_t i = 0; i < actions_.size(); i++)
            if (b.key[i] != actions_[i].default_key) return false;
        return true;
    }

    // The file: one "id=key" line per action whose key is not its default; a conflicted action is not written (it is
    // found again on reading). "" when everything is the default: then the caller deletes the file.
    std::string to_text(const Bindings& b) const {
        std::string out;
        for (size_t i = 0; i < actions_.size(); i++)
            if (!b.conflicted[i] && b.key[i] != actions_[i].default_key) out += actions_[i].id + "=" + b.key[i] + "\n";
        return out;
    }

    // Read back over the defaults. Unknown actions, keys not allowed and a key given twice in one set are skipped, each
    // with a line in `problems`. An action the text does not set, whose default the text gives to another action of its
    // set, gets no key (conflicted): the player's choice wins.
    Bindings from_text(const std::string& text, std::vector<std::string>* problems) const {
        Bindings b = defaults();
        std::vector<bool> set_by_text(actions_.size(), false);
        std::istringstream in(text);
        for (std::string ln; std::getline(in, ln);) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (ln.empty() || ln[0] == '#') continue;
            const size_t eq = ln.find('=');
            const int a = eq == std::string::npos ? -1 : find(ln.substr(0, eq));
            const std::string key = eq == std::string::npos ? std::string() : ln.substr(eq + 1);
            if (a < 0 || (!key.empty() && !allowed(a, key))) {
                if (problems) problems->push_back("skipped: " + ln);
                continue;
            }
            bool twice = false;
            for (size_t j = 0; j < actions_.size(); j++)
                twice = twice || (set_by_text[j] && !key.empty() && b.key[j] == key && actions_[j].set == action(a).set);
            if (twice) {
                if (problems) problems->push_back("key given twice: " + ln);
                continue;
            }
            b.key[static_cast<size_t>(a)] = key;
            set_by_text[static_cast<size_t>(a)] = true;
        }
        for (size_t i = 0; i < actions_.size(); i++) {
            if (set_by_text[i] || b.key[i].empty()) continue;
            for (size_t j = 0; j < actions_.size(); j++)
                if (j != i && set_by_text[j] && actions_[j].set == actions_[i].set && b.key[j] == b.key[i]) {
                    b.key[i].clear();
                    b.conflicted[i] = true;
                    break;
                }
        }
        return b;
    }

private:
    std::vector<Action> actions_;
    std::set<std::string> reserved_;

    int holder(const Bindings& b, int set, const std::string& key, int except) const {
        if (key.empty()) return -1;
        for (size_t i = 0; i < actions_.size(); i++)
            if (static_cast<int>(i) != except && actions_[i].set == set && b.key[i] == key) return static_cast<int>(i);
        return -1;
    }

    // A conflicted action whose default is free again gets it back.
    void give_back_defaults(Bindings& b) const {
        for (size_t i = 0; i < actions_.size(); i++)
            if (b.conflicted[i] && holder(b, actions_[i].set, actions_[i].default_key, static_cast<int>(i)) < 0) {
                b.key[i] = actions_[i].default_key;
                b.conflicted[i] = false;
            }
    }
};

// Waiting for the key the player presses for an action. While it waits, and after the answer until every key is up,
// the keyboard is held back from the game: otherwise the key that answered also reaches the game, as a press of the
// action it now belongs to.
class KeyWait {
public:
    enum class Result { not_waiting, answered, refused, cancelled };

    explicit KeyWait(double timeout_seconds = 5.0) : timeout_(timeout_seconds) {}

    void start(double now) {
        waiting_ = true;
        holding_ = true;
        started_ = now;
        answer_.clear();
    }
    bool waiting() const { return waiting_; }
    const std::string& answer() const { return answer_; }

    // A key went down while waiting. Esc cancels; a reserved key is refused and the wait goes on (the caption says
    // that key cannot be used); any other key is the answer.
    Result on_key(const std::string& key, bool reserved) {
        if (!waiting_) return Result::not_waiting;
        if (key == "Esc") {
            waiting_ = false;
            return Result::cancelled;
        }
        if (reserved) return Result::refused;
        waiting_ = false;
        answer_ = key;
        return Result::answered;
    }

    // Each frame while waiting: after the timeout the wait ends with nothing changed (true once, when it does).
    bool timed_out(double now) {
        if (!waiting_ || now - started_ < timeout_) return false;
        waiting_ = false;
        return true;
    }

    // Each frame: whether the game gets the keyboard. `any_key_down`: whether any key is down now.
    bool game_gets_keys(bool any_key_down) {
        if (waiting_) return false;
        if (holding_ && !any_key_down) holding_ = false;
        return !holding_;
    }

private:
    double timeout_;
    bool waiting_ = false, holding_ = false;
    double started_ = 0;
    std::string answer_;
};

}  // namespace settings_guide
