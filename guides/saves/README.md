# Save protection and crash reports

**Status:** sections 2 to 4 describe what we did and tested in our own port. All of it is in our main line, and the save
journal is on by default.

A console gives a game save data with a commit: what the game writes counts only once it commits. A PC port that writes
the game's saves straight to files loses that, so a crash halfway through a save can cost the player everything. This guide
covers putting the commit back with a journal, keeping backups, and writing crash reports that are useful and safe to send.
It is meant for other people porting a game the same way. You do not need any private code or game data to follow it: the
reference code in [`src/`](src) and its tests build on their own.

**Tested on one machine only:** Windows 11. The crash reports in section 4 are Windows only.
**Not tested: Linux, macOS, saves on a network or cloud folder.**

Each result below says how we know it:

- **[seen]** a person looked at the game
- **[measured]** logged or checked by a program
- **[read]** taken from a specification or vendor document
- **[inferred]** our reasoning, not proven

## 1. The short version

1. Do not count on the game to notice a broken save. Ours loaded a save torn halfway through a write without complaint, into
   an empty world. [measured, section 2]
2. Keep a journal. Before the first change to a save file since the last commit, copy its committed bytes aside and put the
   copy on the disk. At the commit, put the written files on the disk and drop the journal. At the next start, a journal
   left behind means the last run ended in between, so put those files back. [measured, section 2]
3. Back up each run's saves before its first write, and prune the backups by a rule that never touches a folder it did
   not make. [section 3]
4. Write crash reports from data gathered at start-up. The crash path only formats into a buffer it was given. [section 4]
5. Before asking players to send a report, tell them what is in it: no save files, but the dump holds part of the program's
   memory, and folder names can include their user name. [section 4]
6. Tell the player from a separate small process, and never in scripted or test runs. [section 4]

## 2. A journal for the commit

The reference code is [`src/save_journal.hpp`](src/save_journal.hpp).

| When | What the journal does |
|---|---|
| Before the first write, create or delete of a file since the last commit | Copy the file's committed bytes into the journal (or note that it did not exist), add its name to a list, and flush both to the disk before the game's write starts |
| The game commits | Flush the files the game wrote, then delete the journal |
| The save data is mounted, before the game reads anything | If a journal is there, the last run ended between a write and its commit: each listed file goes back to its committed bytes, or away if it did not exist then; what was there is moved aside, not deleted |

- **Flush, not only write.** A copy that is still in the OS's cache when the power goes is no copy (`flush_to_disk`).
- **On Linux, flush the folder too.** After creating, renaming or deleting a file, fsync the folder it is in: the fsync(2)
  manual says the file's own fsync does not promise that its entry in the folder has reached the disk. [read] `flush_dir`
  does this on POSIX systems. On Windows it does nothing; we have not found or tested an equivalent there.
- **Remove the journal for good.** If its removal never reaches the disk, a crash right after a commit brings the journal
  back at the next start, and the commit is rolled back. So the folder is flushed after the journal is removed too.
  [inferred]
- **Keep the journal on the same volume as the saves.** POSIX requires a rename to be atomic, and a rename between file
  systems fails instead (EXDEV). [read] Putting files back must be a rename, not a copy, so check this once at start-up
  (`same_volume`). On Windows the reference code compares drive names only, which misses a folder mounted as another
  volume.
- **Recovery can be interrupted and run again.** A file already put back has no copy left in the journal and is skipped.
- **Keep the journal next to the save folder, not inside it.** Then the save folder only ever holds the game's own files,
  and anything that copies or checks it sees nothing new. Between commits the journal folder is empty.
- **Make it testable.** We added a test switch that ends the process halfway through the first large save write.

What happened in our port, with a copy of a real save: [measured]

- **Without the journal,** the switch left a save file whose second half was zeros. The game loaded it without any error, and
  the player was left in an empty world, with only the interface and a clock.
- **With the journal,** the next start found the journal, put the committed bytes back, and moved the torn file aside. The game
  loaded normally, at the same moment as the save it came from.
- **A normal save** left nothing behind after the commit, and loading it again changed nothing.

Our game writes each save file whole and commits straight after, so the journal holds one file at a time. [measured] The cost is
one extra copy and the flushes, on each save only. [measured]

## 3. Backups

The reference code is [`src/backup_retention.hpp`](src/backup_retention.hpp).

- **Before a run's first write** to a save file, copy it into a folder named after the run's start,
  `yyyymmdd-hhmmss`. A backup then holds what the saves were before that run touched them.
- **Keep the newest 10 runs, and the newest of each earlier day for 30 days.** Prune only folders whose names have exactly
  that form, so nothing a player put there is deleted (`backups_to_delete`).
- **Restoring on request.** Before the first mount, replace the save files with a chosen backup, moving the replaced ones
  aside. Restoring the latest backup undoes the last run that saved.

## 4. Crash reports

The reference code is [`src/crash_report.hpp`](src/crash_report.hpp).

**What the report starts with:**

- What stopped and where: the exception's name and code, the module and offset, and for an access violation, the address and
  whether it was a read or a write.
- When, how long the game had been running, and how many frames it had shown.
- What to send and what it contains: this file and the log and dump next to it. They do not include the player's save
  files. But the dump holds part of the program's memory at the moment of the crash, and the files contain folder names
  from the player's computer, which can include their user name.
  - Our dump asks Windows for the stacks of all threads, the memory that data on those stacks points to, thread state, the
    OS's per-process and per-thread information, and the recently unloaded modules. [read] That memory can hold anything
    the program was using at the time, such as folder names or the game's text. [inferred]
- This run: OS version, CPU and memory; the executable and its build time; the game's build, GPU and driver; installed
  add-ons, mods in use, and the run's settings, one per line.

Next to it: a copy of the whole run's log and a minidump, kept and pruned together with the report.

**Writing it from inside a crash.** The thread that crashed may have no stack left, or may hold the heap's or the loader's
lock. So gather everything at start-up into fixed buffers (`RunContext`) and create the thread that writes the report in
advance. The crash path only formats into a buffer it was given: no allocation, no new threads, no libraries loaded.
[inferred]

The reference code formats with snprintf to stay readable. POSIX does not list snprintf among the async-signal-safe
functions [read], and a C runtime may take a lock inside it, for its locale for example. [inferred] A real crash handler is
better off with a small formatter of its own that only copies text and writes digits. [inferred]

**Telling the player.** The crashed process starts the executable again with an argument that names the report. That second
process is small and healthy, and shows a message in the OS's language; its "yes" opens the folder with the report
selected. Runs with scripted input, automated tests and runs that record their own markers never show it, so test
runs are not interrupted. A switch can force it on or off, and a test mode writes the message to a file instead
(`tell_player`).

What we checked: [measured]

- A crash forced at a set frame produced the report, the log, the dump and, in the test mode, the message file. The report
  started with the cause, address, time, frames and run details as above.
- A fatal error raised by the program itself took the same path, and a run with scripted input showed no message.
- Testing found two gaps, both fixed: a long settings line was cut off, so we now write one setting per line with a longer
  limit, ending in "..." when cut; and with settings reading turned off, the settings section disappeared, so it now says
  they were not read. Test your report with the settings and mods a real player might have.

## 5. What this guide does not cover

- **The game's own save format.** Nothing here reads or checks it.
- **Linux and macOS crash handling.** Section 4 is Windows only.
- **Saves on a network or cloud folder.** A flush there may not mean what it means on a local disk.
- **The POSIX code paths.** `flush_to_disk`, `flush_dir` and `same_volume` follow the manuals, but we have not built or run
  them: our only test machine runs Windows.
- **Folder flushes on Windows.** Not done, and not tested.
- **Mods that change saves.** Not looked at.

## 6. Build and run the tests

Any C++17 compiler works; there are no dependencies. The test writes into a temporary folder and removes it afterwards.

```sh
cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

or by hand:

```sh
g++ -std=c++17 -Isrc tests/test_saves.cpp -o test_saves && ./test_saves
```

## Sources

The code here was written for this guide and is under the MIT licence of this repository. It contains no third-party code.
