# Keyboard, mouse and button prompts that follow the context

**Status:** sections 2 to 8 describe what is in our main line. Rebinding keys and buttons is not covered here.

A console game reads one controller and draws that controller's buttons in its prompts. This guide describes how our
port tells where the player is (field, battle, dialogue, menu), what keys and mouse buttons do there, how the mouse
turns the camera, when the pointer shows, and how the prompts follow the device used last. It is for people porting a
console game to PC by static recompilation, who cannot change the game's own code. You need no private code or game data
to follow it. Clicking in menus is a separate piece of work: here we only say when the pointer shows and hides.

**Tested on one machine only:** Windows 11, an NVIDIA GPU, a controller read through XInput.
**Not tested: AMD, Intel, Linux, macOS.** The input code described here exists on Windows only.

## What has not been verified

- **Most checks used scripted input,** which follows the same rules as real input but enters after the code that reads
  the devices. Section 9 lists what one person tried with real ones.
- **The fourth number key.** A person cast the first three skills with it in a battle; the fourth was not yet open in
  that save.
- **Side mouse buttons, left and right Shift or Ctrl as separate keys, keys that press two buttons.** Unit tests only.
- **Choices in a dialogue.** By our rule they count as a menu [inferred], but no run reached one. Clicking them is not
  in our main line.
- **How the mouse camera feels.** Its direction, its stopping and its switch were found right; its speed was not judged.
- **The PlayStation layout on a real controller, and the automatic choice of the layout.**
- **Keyboards other than US ones.** Keys are Windows virtual keys, named as on a US keyboard.
- **The pointer itself.** The hardware draws it, so screenshots do not show it, and automated runs had their window in
  the background: they checked the rule's decisions only.
- **Other versions of the game.** The context is read in the one version we studied; elsewhere Esc and the pointer keep
  simpler rules. The prompts ran in one other version, on one route.
- **Prompts in movies, or drawn into pictures we did not search.**

Each result below says how we know it:

- **[seen]** a person looked at the game
- **[measured]** logged or checked by a program
- **[read]** taken from a document, here another project's notes
- **[inferred]** our reasoning, not proven

## 1. The short version

1. Merge keyboard, mouse and real controller into the one controller the game reads. [section 2]
2. Ask the game every frame where the player is. [section 3]
3. Give each context its own keys. A key belongs to the context it was pressed in until it is let go. Where a wrong
   press can cost something, press nothing. [section 4]
4. Turn the camera by an angle, through the game's own update. [section 5]
5. Let the left mouse button confirm only while the game waits for it. [section 6]
6. Show the pointer only where the mouse can click. [section 7]
7. Draw the prompts from the same rules that map the input. [section 8]

## 2. One controller, three devices

- **Everything becomes the controller the game reads.** The keyboard's map gives buttons and stick directions, added to
  the real controller's. Buttons add up; a stick the keys push replaces the controller's. Both work at the same time.
- **No device setting.** We removed ours after a person pointed out that a player could choose a device they do not have
  and be stuck.
- **Two direction keys at once** put the stick on its circle at 45 degrees. Ours first pushed it into the corner of the
  square, which no stick reports, and the character walked straight ahead. On the circle it walks diagonally, at the
  same speed [measured], and a person found it right. [seen]
- **Read keys only while the window is in front, and keep quick taps.** The window's key-down message notes each press,
  and the map sees it as held through the first frame that reads it.
- **Turn the input method off for the game's window.** A Chinese or Japanese one took letter keys, so taps were lost,
  and its hints appeared over the game in recordings of test runs.

## 3. Knowing the context

At the start of each game frame the port reads the game's state and calls a few of its functions:

| Context | How it is told |
|---|---|
| Menu | From the game's list of UI input layers, below |
| Battle | The game's own answer to "is a battle on" |
| Cutscene | The game's own answer to "is an event playing", with no menu open |
| Field | The player's camera was updated in the last quarter second, with none of the above |
| A window that waits | The input layer of the speech balloon, item window, tutorial window or cutscene pause |
| None of these | Scripted animations, for example |

Our game's UI keeps its input layers sorted by priority, with a floor below which no layer gets input. A menu is open
while a layer that takes input and is shown

- has something to select, except a low-priority layer during a battle (the battle's own display), or
- is at the menus' priority while the floor is raised.

While the floor is raised and no layer takes input (screens changing, loading), the answer stays as it was. With the
floor down it holds for a quarter second. We found the rule by logging every layer on many screens, and compared its
answers with those logs in new runs. [measured]

- **The game calls its title screen an event.** "An event is playing" alone does not mean a cutscene.
- **Tutorial windows blink.** Their layer is off for a frame now and then. The quarter second covers it. [measured]
- **A window that only waits for a button is not a menu.** Ours first counted as one by its priority, and the pointer
  showed during play. Now a layer with nothing to select is no menu while the floor is down. [measured]
- **Know when you cannot know.** One function says whether menus can be told in this version of the game.

## 4. What each input does

Our defaults, for the inputs whose meaning changes:

| Input | Field | Battle | Menu |
|---|---|---|---|
| Space | Jump | Nothing (below) | Confirm |
| Enter | The main menu's button | The same button, another meaning | Confirm |
| Esc | Opens the main menu | Nothing | Back |
| 1 to 4 | Nothing | The battle skills | Nothing |
| Tab, Shift+Tab | Nothing | Nothing | Next and previous page |
| Mouse movement | Turns the camera | Turns the camera | Moves the pointer, where it shows |
| Wheel | Camera distance | Nothing | Scrolls long lists |

- **The menu goes before battle, and battle before the field.** In a menu, the menu's keys are its own, whatever else
  they are bound to. In battle, the number keys do only their battle action.
- **A key belongs to what it was pressed for, until it is let go.** Otherwise the Esc that closes the last menu becomes
  the field's Esc and opens it again. The rule covers all of the menu's keys, and the Shift of a Shift+Tab. [measured,
  unit tests]
- **Esc depends on the context at the moment of the press.** It pauses a cutscene. It presses nothing in a battle, where
  the main menu's button is a battle command, or where no context holds, such as a timed button challenge or an
  animation. With scripted keys, Esc opened and closed menus without reopening one however long it was held, paused and
  resumed cutscenes, and answered "no" in a save question; a battle and a challenge went on as usual. [measured]
- **A button with two meanings.** The jump button is also a battle skill. While the number keys cast skills, the jump
  key presses nothing in battle; with that option off it presses its button, so the button always has a key. The rule
  follows the action, not Space. [measured, unit tests]
- **Always a way out.** Esc and the right mouse button go back in menus.
- **Conflicts.** The same key may act in different contexts; the prompt shows where it cannot be pressed (section 8).

## 5. The mouse camera

1. **Raw input, only when needed.** It is registered while the mouse may turn the camera. Relative movement is added up
   only while the pointer is held in the window.
2. **Taken once per camera update, as an angle:** 0.08 degrees a count, times a sensitivity from 0.25 to 4.
3. **Added to the game's own two angles, just before the game's camera update.** If the camera follows the player, it is
   freed first, as the stick frees it. The game wraps and limits the angles. Its options for inverting the axes apply.
4. **Handed on after the update,** as the game does for the stick.
5. **Nothing while the stick would be ignored:** camera paused, just reset, no camera target.
6. **Dropped, not saved up.** Movement nobody took for a quarter second is dropped, so the end of a cutscene does not
   spin the camera.

An angle needs no time step. At three frame rate limits the same scripted counts turned the camera by the same angles:
1125 counts by 90.00 degrees, 250 by 20.00. With the option off, nothing turned. In a battle, 400 counts turned it by 32
degrees every time. [measured]

What we tried or decided against:

- **Both directions were reversed at first.** We found it by turning with the mouse and the stick in one run, and
  comparing the camera's position with the player's. [measured]
- **A simulated stick.** Not tried. The notes of Monado, another porting project, say it cannot be tuned well: the dead
  zone eats small movements, and the turning limit makes fast ones queue up. [read]
- **The camera's distance through the game's own buttons.** The game uses a held combination and the stick, but pressing
  the combination resets the whole camera at once. [measured] So the wheel writes the distance, inside the game's own
  limits, outside menus, cutscenes and battles (with the number keys on). The direction turned with the mouse stays.
  [measured]

## 6. Mouse buttons in dialogue and cutscenes

While a talk, an item window or a tutorial window waits, the left mouse button is the confirm button.

- **Look at enough layers.** About 36 input layers are on in the field. We first read the top six, and the speech
  balloon's was further down. [measured]
- **Pick the right sign.** The balloon's text object reads as hidden even while a talk is up. Its input layer is on only
  during a talk, so that is the sign. [measured]
- **Try it by hand.** A person found that the left button did not advance a tutorial window, though it already worked on
  dialogue. With those windows added, they said it was right. [seen]

In a cutscene's pause the right button goes on and the left one skips. The game skips at once, without asking, so the
left button was off at first. A person asked for it, and later found it right. [seen]

## 7. When the pointer shows

| Where | The pointer |
|---|---|
| Another window is in front, or the game is minimised | Shown and free |
| In play, with the mouse camera on | Hidden, and held inside the window |
| A screen the mouse can click, keyboard or mouse used last | Shown |
| The same screen, controller used last | Hidden, until the mouse moves a little or a key is pressed |
| A screen where the game's own pointer follows the mouse | Hidden: the game draws its own |
| Anywhere else | Hidden |

- **The rule changed more than once.** First it was hidden only during play: a person saw it in tutorial windows, which
  counted as a menu, and in a scripted animation, which counted as neither. Now it shows only where the mouse can click,
  and logs of scripted runs agree. [measured] We found no record of the person looking at those places again.
- **Esc does not release it.** It once did, but every step back would have shown it. Alt+Tab does, by our code; we found
  no record of a test.
- **Apply the hold again now and then,** as other programs can lift it: every half second while the mouse moves.
- **Log every change with its reason:** window in front, minimised, hidden by a rule, held for the camera.

## 8. The device used last, and the prompts

**Which device.** A key or mouse button the map uses, or a controller's button or stick, makes that device the last one
used. Mouse movement counts from a few pixels in one read, so a knock on the desk does not switch. The prompts change
once the new device has stayed for a third of a second, both ways in a scripted run. [measured]

**Replacing the pictures.** Our game draws its button pictures from three shared textures: for the screens, for text,
and for timed challenges. We find them by a hash of their contents at first use, and the renderer stands our own
pictures in for them: no game code is hooked and no game file changes. A worker thread draws the cells, with no font
file and no third-party art. The last few pictures are kept, so switching back draws nothing. In our test pictures at
1280x720 a five-letter word could not be read in a prompt, so Space, Shift, Enter and Tab are symbols. At a larger
output the pictures inside text are redrawn at its scale.

**One rule for input and prompts.** What each picture shows is asked from the input rules themselves:

- Keyboard: the key bound to that button's action. In a menu, A and B show the menu's confirm and back keys. In battle,
  three skill buttons show the number keys. In a cutscene's pause, A and B show the mouse buttons.
- "?" when another action holds the action's default key, or, in a menu, when the menu takes its key.
- Controller: the physical button that does it now, found by running the input's own rule backwards.

**Layouts.** Nintendo, Xbox and PlayStation: the positions stay and the names change, with letters and simple shapes, no
logos. The layout follows the connected controller's maker, read from Windows' device list, or the player's choice. In
menus, dialogue, item and tutorial windows and a cutscene's pause, the Xbox and PlayStation layouts confirm with the
bottom button and go back with the right one, so "back" does not skip a scene. The field keeps the game's positions.
Switching the layout in the settings changed the prompts in a scripted run. [measured] At 1280x720, with a controller in
the game's own layout, menu frames of an earlier build were the same, pixel for pixel, with the replacement on and off.
[measured]

**Known gaps.** The control diagrams, whole pictures with a controller drawn in, still show a controller; we left them
for later. The first time a texture is used, the game's own picture can show for a few frames, until ours is drawn.
[measured]

## 9. How we checked it

- **Unit tests** of the rules: the default map against a written-out copy of the previous one for every key and every
  pair of keys, ownership at press time, Esc in and out of menus, the wheel, the mouse in dialogue. [measured]
- **Scripted runs** through these contexts. A trap there: a first version took a key as let go when it had not been seen
  for a moment, so one stall with the key held counted as a new press. Count the game's reads, not time. [measured]
- **One person, on several builds,** tried lists of things and said for each whether it was right. [seen]
  - First the keyboard prompts, as pictures: the style is fine.
  - Then, right: the title screen with the mouse, the left button in dialogue, the left skip in a pause, the right
    button for back, the arrow keys as the D-pad with WASD for moving, the middle button to reset the camera, the wheel
    for distance, Esc in the field, changing the menu's confirm key and back, the Xbox layout's prompts and menu
    buttons. Not right at first, then fixed and right: walking diagonally, and the pointer staying while a controller
    is used.
  - Later, right: the shapes of the shoulder buttons, the D-pad's pictures and the key cap's shape.
  - Later still, right: the mouse camera turns the way the mouse moves and stops with it, does nothing while switched off,
    and works again when switched on; the keys 1 to 3 cast their skills in a battle, with the mouse camera still turning;
    and a controller and the keyboard and mouse used at once, where both act together, the prompts change to the device
    used last, and the pointer shows with the mouse and hides with the controller.
- **Not in the records:** the "?" cap (it did not come up in their test) and the PlayStation layout.

## 10. What this guide does not cover

Pointing, clicking and scrolling in the game's menus; rebinding keys and buttons; PlayStation and Nintendo controllers
without XInput; controller vibration; and devices that report absolute positions, such as a remote desktop's mouse,
which do not turn the camera. [inferred]

## Sources

Written for this guide, from our own work and tests; no third-party code. Turning the camera by an angle instead of a
simulated stick is also described in Monado's notes.
