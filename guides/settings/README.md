# PC settings in the game's own Options screen

**Status:** sections 2 to 10 describe what we did and tested in our own port. All of it is in our main line.

A PC port needs settings the console game never had: display mode, resolution, vertical sync, HDR, keys. They can go in
a launcher, in an overlay, or in the game's own Options screen. We chose the last: extra tabs that look and work like the
game's, with the same controller and keyboard navigation, the game's fonts and languages, and nothing to start before the
game. This guide covers adding those pages without shipping or changing any game file, keeping the PC settings
out of the game's save data, and making each row tell the player what it can do. It is meant for other people porting a
game the same way. You do not need any private code or game data to follow it: the reference code in [`src/`](src) and its
tests build on their own and know nothing about any particular game.

**Tested on one machine only:** Windows 11. Section 10 lists the input we tested with.
**Not tested: Linux, macOS.** The reference code uses only the C++17 standard library, but we have built and run it on
Windows only.

Each result below says how we know it:

- **[seen]** a person looked at the game
- **[measured]** logged or checked by a program
- **[read]** taken from a specification or vendor document
- **[inferred]** our reasoning, not proven

## 1. The short version

1. Add pages by changing the game's menu data, not by drawing a menu of your own. Build the changed files at start-up from
   the player's own copies, keep them in memory, and serve them through the file layer. The files on disk stay as they
   are, and nothing from the game is shipped. [section 3]
2. Hook only functions the game reaches through a table, check that nothing calls them directly, and leave the screen as
   the game made it when anything does not match. [section 4]
3. Keep the PC settings in a file of the port's own. The game's save data never sees them, so the game never saves
   because of them. [section 5]
4. Start every new option at the game's own behaviour, and give automated runs two switches: the original screen, and
   default settings. [section 5]
5. Make each row say what it can do on this PC: grey it with a reason, skip values that cannot work here, and say when a
   change takes effect. [section 6]
6. Reuse the game's own panels, dialogs, sounds and colours wherever they fit. [section 7]
7. Write the text in every language the game ships, within the panel's limits, and check every string by program.
   [section 8]
8. When rebinding keys, swap on a conflict, refuse the port's own keys, and hold the keyboard back from the game until
   the key that answered is let go. [section 9]

## 2. Why the game's own screen

- Controller and keyboard navigation are already there, with the game's cursor, sounds and help line.
- The text goes into the game's own text tables, so it is drawn with the game's fonts, in the language the player
  chose for the game.
- A change can take effect while the game runs, and the player sees it behind the menu.
- There is no second program, and no window on top of a fullscreen game.

The cost is the game's limits: a fixed number of rows per panel, list values instead of free sliders, and a fixed length
for each text. We show numbers as steps of a list, such as "1000 nit" or "x1.5".

Our pages, as they are in our main line:

| Tab | Rows |
|---|---|
| Display (in place of the game's camera tab) | Display mode, Output resolution, Refresh rate (shows the desktop's), Vertical sync, Variable refresh rate, Frame rate cap, HDR, Peak brightness, Paper white, Highlight compression (shown only where it changes something) |
| Sound, Screen, Game | The game's own, unchanged |
| Graphics (new) | Super resolution, Super resolution quality, Render resolution, Dynamic resolution, Anti-aliasing, Frame generation and latency ▶, Texture quality, Anisotropic filtering, Depth of field, Motion blur |
| Controls (new) | Mouse camera, Mouse sensitivity, Number keys in battle, Keyboard and mouse keys ▶, Controller buttons ▶, Controller layout, Camera ▶ (the game's own camera panel, section 7) |

▶: the row opens a page of its own. The table shows where the rows are, not how far each feature behind them has been
tested. Features with a guide of their own describe that there.

## 3. Adding pages without shipping game files

The reference code is [`src/overlay_reader.hpp`](src/overlay_reader.hpp).

In the game we worked on, the Options screen is built from three kinds of data, and many games split it the same way:

- a UI object file: the tabs, a panel per tab, the slots in each panel and the kind of widget in each slot;
- a layout file: where everything goes;
- data tables: each row's name, caption and limits, and the text in each language.

The game reads all of it from one large archive, through an index that gives each file's offset and size. So at
start-up, before the game opens the archive, the port:

1. **Reads the original files** from the player's own archive, unpacks them, and makes changed copies in memory:
   - the UI file: new tabs copied from an existing one, and a new panel copied from the panel with the most slots, its
     sliders replaced by list values;
   - the text tables of every language: the new names, values and captions as new rows, every existing row unchanged;
   - the other tables the game consults for the new panel (see the first lesson below).
2. **Serves them.** A changed copy of the index points each replaced file at a range past the archive's end, marked as
   stored uncompressed. Reads of that range are answered from memory, so the archive reads as longer than it is. The
   archive and the index on disk never change. Pick an entry form the game already reads: many files in ours were stored
   uncompressed anyway, so the port needs no compressor.
3. **Is checked offline.** We build the patching code on its own, run it against the player's files, and compare its
   output with a second, independent reader: every index entry not replaced is byte-identical, every original UI object
   and table row is unchanged, and the new rows are as intended. [measured]

What surprised us: [measured]

- **Copying an object is not enough when the game keeps a registry.** Our first panel opened with every text empty. The
  game creates only the text objects listed in a separate table. We found it by logging every object lookup: panels and
  images were found, every text was not. The new text objects needed rows in that table too.
- **A copied panel copies its input rules.** The game's option panels change values only with the D-pad's left and
  right. Our port maps the keyboard's arrow keys to the left stick, so the arrow keys did nothing. The new panel got an
  input definition of its own, a copy with the stick added. The game's own panels kept theirs.
- **Find out how the game decides what a tab opens.** Ours picks the panel by the tab's position, and a new position
  would have opened an existing panel [inferred from the game's code]. So the hook opens our panel for our tabs itself,
  and passes every other tab to the game.
- **Unpack only what you change.** Our first version unpacked every table of a large data file and made start-up
  noticeably slower. Unpacking only the tables it changes fixed that.

New tab icons follow the same rule. The game's icon sheet had empty frames. At start-up the port draws our own symbols
into them, compresses only those blocks again, and serves the sheet like the other files. The offline check finds only
those frames changed [measured]. Our own symbols are all we ship.

## 4. Hooks that really run

In a statically recompiled game, a hook that replaces a function's entry in a table catches only the calls that go
through that table. A direct call, or a tail call the recompiler turned into one, goes straight to the recompiled function
and never reaches the hook. So:

- Hook the screen's entry points that the game reaches through virtual-method tables: its set-up, its tear-down, and
  the event handlers of the tab bar and of the panels.
- Search the recompiled code for direct calls and tail calls to every hooked function, and for small functions that
  adjust `this` and jump into it. We found none, so every call goes through the table. [measured] Our build now runs
  this search every time, and fails when a hooked function has a direct caller.
- Give each hook a log line that only it writes, and count those lines against a scripted run's inputs: one set-up per
  opening of the screen, one value change per key press that changes a value. [measured]
- Before installing, check that the game is the version you studied, that every function the pages use is found, that
  the screen's tables hold the functions you hook, and that no page has more rows than the panel has slots. If anything
  does not check out, add no pages and replace no files: the player gets the game's own screen, the saved settings still
  apply, and the log says why.

## 5. A settings file of the port's own

The reference code is [`src/settings_file.hpp`](src/settings_file.hpp).

- **Not in the game's save data.** When the menu closes, our game compares its working copy of the options with the ones
  in use, and saves only if they differ [inferred from the game's code]. The PC pages never touch either copy. Leaving the
  screen after changing only PC settings writes nothing. [measured, section 10]
- **Not in unused bytes either.** Spare bytes in the game's own option data are tempting. But when the player changes any
  of the game's options, the game writes those bytes into its save too. [inferred]
- **One `key=value` per line, written at once.** Each change goes to a temporary file that is then renamed over the old
  one. Comments and keys this version does not know are kept as they are, so an older and a newer build can share the
  file. A key appears only once the player sets it.
- **A word keeps its meaning.** When exclusive fullscreen came, existing settings files already said `fullscreen` for the
  borderless window. So `fullscreen` kept that meaning, and the new mode got a new word. The menu shows names; the file
  keeps its words.
- **A value the row does not list** shows as "custom", and stays until the player picks another one.
- **Show what runs.** A test switch, or a key such as F11 for fullscreen, can change a setting after the file was read.
  Our rows ask the runtime what it uses wherever it can tell.
- **Two test switches.** One leaves the Options screen exactly as the game made it: no pages, no file replaced. The other
  runs with every setting at its default and never reads or writes the file. Our regression runs use the second, so a
  player's choices on the test machine never change a test.
- **Defaults are what ran before.** Every new option started at what the game, or the port, did before the option
  existed. So the regression's reference pictures stayed valid with the settings code in. Where we later chose on purpose
  a default that changes the picture, the regression got reference pictures for that default too, and compares both.
  [measured]

## 6. Rows that say what they can do

The reference code is [`src/option_rows.hpp`](src/option_rows.hpp).

Each row has the file's key, its values in the order left and right step through them, a default, and when a change
takes effect: at once, from the next frame, or at the next start. Each time the row is drawn, the port works out what it
can do on this PC now:

| State | What the player sees | Our examples |
|---|---|---|
| Not on this PC | Greyed, the value reads "unavailable", the caption says why | An upscaler whose libraries are missing; HDR while the display or Windows has it off (the caption says to turn it on and restart the game) |
| Held by another row | Greyed, the value kept, the caption says what holds it | Anti-aliasing while an upscaler is chosen, which anti-aliases by itself |
| A value that cannot work here | Left and right step past it | Exclusive fullscreen where the driver does not offer it |
| Takes effect at the next start | The caption says so while the value chosen is not the one running | Anti-aliasing |
| Used later | Can be set now; the caption says it is kept and takes effect once its feature is on | Peak brightness and paper white while HDR is off |
| Nothing to step through | No arrows; left and right play the game's own "limit" sound | The refresh rate row, which shows the desktop's rate |

- The cursor skips rows that are only placeholders. A greyed row with a reason can still be selected, so the player can
  read the reason.
- When several states apply, the caption shows the first, in the table's order.
- Lists wrap around, as the game's own lists do.
- Choosing a value puts it to work first and saves it after. If the runtime cannot do it, nothing is saved and the row
  keeps its old value.

What testing taught us: [measured]

- **Work out the state from the value just chosen,** with the rule the runtime uses, not from a flag the runtime updates
  later. One of our hints ("the device you chose is not connected") showed one step late: the flag changed only at the
  next device read, after the menu had drawn the row again.
- **Hide a value field instead of writing an empty text into it.** Our UI kept the previous page's word when it was given
  an empty one.
- **Some rows have no values:** a row that opens a page, a key binding's row. Code that applied every row's value at
  start-up read the first value of an empty list, and a smoke test crashed at start-up. The reference code's tests
  include such a row.

## 7. Reuse the game's own pieces

- **Restoring the defaults** uses the game's own confirmation dialog and message. Yes restores the page in view only.
  [measured] A setting whose default follows another one is cleared rather than set: dynamic resolution's default
  depends on the render resolution, so it keeps following it.
- **The game's camera options** moved from their tab to a row of our controls page. The row opens the game's own camera
  panel, with the game's text, sliders and save path, and B there comes back to our page. An option changed this way
  wrote a save byte-identical to the same change from the game's original tab. [measured]
- **Sub-pages** reuse a pattern the game already has: one of its panels opens another (custom difficulty, from the game
  options), with the same button. Our sub-pages fill the same panel with other rows, so they need no new UI objects.
- **Sounds and colours** come from the game: the cursor and "limit" sounds of its own lists, and the grey its other
  menus use for text that cannot be chosen.

## 8. Text in every language the game ships

- Every new row has text in every language the game ships. A language without our rows would show them empty.
  [inferred]
- Where the game already has a word for something, use the game's word.
- The panel limits the length of row names, values and the two-line caption. Our offline check (section 3) measures
  every string in every language, and fails on any that does not fit. [measured]
- When the first page went in, we looked at it in Japanese and Traditional Chinese, on screenshots of scripted runs. The
  Simplified Chinese and Korean text was checked in the tables only. [measured]

## 9. Rebinding keys

The reference code is [`src/key_bindings.hpp`](src/key_bindings.hpp).

- **Sets.** Actions active at the same time form a set, and within a set no key is ever two actions'. The menu's keys are
  a set of their own and may reuse keys used in play.
- **Swaps.** Giving an action a key another action has swaps their keys. If the old key is not one the other action may
  have, the other gets its own default. If that is taken too, it gets no key and shows "conflict" until its default is
  free again, and then gets it back by itself.
- **Refused keys.** The port's own keys (fullscreen) and the OS's (Alt, the Windows key) are refused, and the caption says
  that key cannot be used.
- **Cancelling.** Esc cancels the wait, and so does a timeout, with nothing changed.
- **Restoring one page** must also move other actions off the keys that page takes back. Ours first did not, and one key
  ended up on two actions. [measured]
- **Hold the keyboard back** from the game while waiting for a key, and after the answer until every key is up. Our
  scripted input once skipped that hold, and the key that answered reached the game as a press of its new action.
  [measured]
- **The file** holds only what the player changed, and is deleted when everything is back to its default. When the file
  gives an action another action's default key, the player's choice wins and the other action has no key.

## 10. What we checked

All [measured] unless marked otherwise:

- **Offline:** the patching code against the player's files, compared by an independent reader (section 3).
- **Input:** the pages opened from the tab bar and worked with scripted controller input, which takes the same path
  through the port as a real controller's, and with real keyboard events, in a window and in fullscreen. Greyed rows were
  skipped, and restoring the defaults went through the game's dialog.
- **Vertical sync took effect from the next frame shown.** The OS's present statistics (PresentMon) showed a sync interval of 1 with
  standard vertical sync, and in fullscreen, tearing allowed with it off. In a window, the Windows compositor syncs
  anyway, so "off" and "low latency" look the same there.
- **Settings read at start-up apply from the first frame:** with vertical sync off in the file, the first swapchain was
  already made without it.
- **The game's saves stayed untouched.** At the end of every run that used only the PC pages, the game's save files were
  byte-identical to the start, including runs that changed values, restored defaults and left the screen. The one run
  whose save changed had changed one of the game's own options, and the game saved as it always does.
- **The camera panel path:** the save was byte-identical to the original path's (section 7).
- **At 3840x2160** the pages have the same layout as at 1280x720.

## 11. What this guide does not cover

- **The mouse in the game's own menus:** pointing, clicking and scrolling. A separate piece of work.
- **Other versions of the game.** The pages are made for the version we studied. The check before the hooks (section 4)
  keeps the game's own screen anywhere else.
- **A setting that leaves the picture unusable** before the player can reach the menu. Deleting the settings file brings
  back the defaults; we have nothing more.
- **Linux and macOS.** The reference code was built and run on Windows only.

## 12. Build and run the tests

Any C++17 compiler works; there are no dependencies. The test writes into a temporary folder and removes it afterwards.

```sh
cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

or by hand:

```sh
g++ -std=c++17 -Isrc tests/test_settings_menu.cpp -o test_settings_menu && ./test_settings_menu
```

## Sources

The code here was written for this guide and is under the MIT licence of this repository. It contains no third-party code.
