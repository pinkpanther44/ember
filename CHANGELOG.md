# Changelog

All notable changes to Ember are listed here. Dates are the release dates.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [1.0.1] - 2026-09-23

### Fixed

- **Ember no longer closes itself when the audio input is set to none.**
  Changing the input device closes and reopens the audio device, and one block
  of audio could still arrive after the engine had been told the device had
  stopped — at which point it was working from a device that was no longer
  there. Nothing is passed on now between being told the device has stopped and
  being told a new one has started; that gap is filled with silence instead.
  This applies to any change of audio device, not only to choosing none

- **If Ember ever does close unexpectedly, it now leaves a note behind.** It
  goes to `Ember/crash` beside your settings, and it records where the fault
  happened rather than anything about your work — no project data, no file
  names. It is only written when the application stops the wrong way, and you
  can delete the folder at any time

### Changed

- **The Linux notes no longer say that FLAC can be exported.** Export is WAV,
  and MP3 on Windows; FLAC, AIFF and Ogg are read but never written

---

## [1.0.0] - 2026-09-22

### Added

- **Bars can be inserted and deleted from the arrange view**, from the
  right-click menu and from the menu on the track headers. A window asks how
  many, and everything after that point moves with them — notes, audio,
  automation, chord regions, markers, and tempo, time signature and key
  changes alike — so an arrangement can grow an extra verse without dragging
  every clip by hand. A clip that straddles the point is cut there, and the
  half after it moves. Deleting bars removes what is inside them

- **A MIDI track can be given a delay of its own**, up to half a second either
  way, from the inspector. Some instruments answer later than others, and a
  part that has to be played early to sound on time is a part that cannot be
  quantised. The delay applies while playing and while exporting, so what is
  heard is what is written out

- **The footer shows the bar and beat beside the time.** Until now the
  position was only in minutes and seconds, which says nothing about where in
  the music it is

- **Tempo can be tapped.** Clicking the word BPM in the footer in time with the
  music sets it, averaging the last few taps; it turns the highlight colour
  while it is listening and gives up two seconds after the last tap

- **Snapping is a button**, beside the tool buttons, and it is on to begin
  with. Turning it off is now one press rather than an entry at the top of the
  grid menu, and the grid the menu is showing is still there when it goes back
  on

- **Ctrl and the wheel over the ruler zooms the rows**, in the arrange view and
  in the editor, so tall tracks and a whole arrangement no longer need
  different windows

- **Alt and drag selects a range over clips and notes** without picking them
  up. Dragging in the arrange view has always moved whatever is under the
  pointer, so a range could only be drawn where there was nothing

- **Windows that were open are opened again.** A project remembers which
  instrument and insert windows were showing, and whether the editor had been
  popped out, and puts them back when it is opened. It is written down when the
  project is saved by hand, so opening a window does not mark it as unsaved

- **The mute and solo buttons of the tracks inside a folder light up when the
  folder's do.** A muted folder has always silenced what is inside it, but
  nothing on the tracks themselves said so. The borrowed light is fainter than
  a light from the track's own button, because the button still does what it
  always did and pressing it has to look like it will do something

### Changed

- **Playback runs on for about a bar after the last note or clip** instead of
  stopping the moment the music ends, so a release or a reverb tail is heard
  out rather than cut

- **Mute and solo cannot both be on.** Pressing solo on a muted track takes the
  mute off, and pressing mute on a soloed track takes the solo off. Both being
  lit needed a rule to be remembered about which of them won; now there is
  nothing to remember. Where a mute still meets a solo — a muted folder with a
  soloed track inside it — the track's own solo is what is heard

- **Notes are added to a selection with Ctrl rather than Shift**, which is what
  clips in the arrange view have always used, and Ctrl and drag now draws a
  further selection box without letting go of what is already selected

- **The stem export window says "Mono" above the column it belongs to**,
  instead of explaining both columns in a line of small print above the list

### Fixed

- **The mono checkboxes could be hidden behind the scrollbar.** The list works
  out its width before it knows whether it will scroll, and it was asking the
  viewport how wide it was while the scrollbar from the last time was still
  showing, so with enough tracks the rightmost column was cut in half

- **A cursor sitting on a bar line counted as the bar before it** when
  inserting or deleting bars. The playhead is truncated to whole samples, so it
  lands a hair short of the line it was put on; the comparison now allows for
  that, as the others do

- **The frame of a moved clip could stay behind after undoing.** The selected
  range was only being cleared when it was judged to be empty afterwards, which
  missed ranges that still had something else in them; it is now cleared
  whenever anything is undone or redone

- **Dragging on the velocity or automation lane scrolled the piano roll.**
  Which lane the drag had started on is now settled when the button goes down,
  rather than guessed at from what the drag is doing

- **Marker flags are readable against the arrange view's background.** Their
  text was one colour on the ruler and another below it

- **The editor no longer jumps away from the playhead** when a track is chosen
  that has no notes near it

---

## [0.9.0] - 2026-09-20

### Added

- **Right-clicking empty space offers the menu for adding a track** — to the
  right of the strips in the console, and below the track headers in the
  arrange view. The console had offered this since the menu was added, but only
  on the few pixels of its own background: the space beside the strips belongs
  to the component that holds them, and that component was swallowing the
  click. Left-clicking there still does nothing, so empty space stays empty

- **Dragging notes past the edge of the editor scrolls to follow the pointer**,
  for moving, copying and selecting alike, and keeps scrolling while the
  pointer is held outside. Until now a note had to be dropped, the view
  scrolled, and the note picked up again — and a selection box cannot be picked
  up again at all, so a range wider than the screen could not be drawn

- **Snapping has a second kind of stop for notes that do not sit on the grid.**
  Dragging one with snapping on could only put it on grid lines, so a note
  placed by hand could not be doubled at its own timing without turning
  snapping off and aiming by eye. The stops are now the grid and the dragged
  note's own offset carried forward in whole grid steps, whichever is nearer,
  so the note can equally be put back on the grid. A note that starts on the
  grid behaves exactly as it did, because the two kinds of stop then fall in
  the same places

### Changed

- **Write layers its notes over whatever is already there** instead of clearing
  the range first. Clearing made sense while pressing the same spot twice was
  the thing to protect against, but the button is used to build a part up — a
  counter melody over the chord, an inversion on top, the same position again
  at a different note length — and notes that disappear cannot be seen to have
  disappeared until it is played back. Notes that pile up can. Writing the
  whole progression from the right-click menu still replaces, as it always said
  it would

- **Selecting a track in the editor's list selects it everywhere else as well.**
  The other direction already worked, so the list would follow the arrange view
  but never lead it, and changing which track was being typed into left the
  inspector and the console showing the last one

- **Notes on other tracks are drawn a little more solidly.** The watermark was
  faint enough to satisfy "not mistakable for the notes being edited", which is
  only half of what it is for

- **The picture on the automation button is smaller**, so that it has the same
  margin as the mute and solo buttons beside it; the button is the size it was.
  Those two are letters and had always sat clear of their frames, while the
  picture filled its own to the edges, which made one of the three look larger
  than the others

### Fixed

- **The chord pad could write the chord before the one under the cursor.**
  Whether the cursor counted as being inside a chord region was decided with a
  plain greater-or-equal and less-than, so a cursor that had landed a hair
  short of a region's start was read as outside it and the answer fell back to
  the region before — the name beside the Write button showed that chord, and
  pressing it wrote that chord. Two things put the cursor a hair short, and
  both only ever undershoot: the playhead is truncated to whole samples, and
  the Write button advances by adding seconds while a region's start is
  converted from beats. Clicking elsewhere and back appeared to fix it because
  that path hands the chord pad the snapped time itself. Comparisons against a
  chord region now go through the same tolerance every other range test uses

---

## [0.8.0] - 2026-09-20

### Added

- **Track headers say what kind of track it is with a picture.** MIDI, audio,
  VCA and send tracks each have their own; folders borrow the one the browser
  uses, and a drum out track shows the MIDI picture, because the sound it
  carries comes from a MIDI track's instrument. The word in brackets after the
  name is gone, which gives that room back to the name. Chord tracks have no
  picture — there is only ever one of them

- **Pressing the MIDI picture opens and closes the instrument's window**, and
  it turns orange while that window is showing, so the instrument being edited
  can be found without hunting through windows. The folder picture collapses
  and expands the folder. The console strips carry the same pictures beside
  their level readout, and the MIDI one works there too

- **Clicking a console strip selects that track.** The arrange view and the
  inspector follow, since all three read the same selection. Only the strip's
  background and its name select it; the fader, the knobs, the buttons and the
  rack keep doing what they did, so reaching for the volume does not move the
  selection. The console also notices tracks being added and removed while it
  is open, where before a new strip only appeared after switching away and back

- **The rack in a console strip is labelled** — instrument, inserts, sends —
  with the button that bypasses every insert at the right of the insert
  heading, where the inspector has always had it. Until now the three "+"
  buttons simply followed one another with nothing to say where one kind ended
  and the next began

- **Pan knobs have an arc.** It grows from twelve o'clock, because the middle
  of the range is the middle of the knob, so a knob turned left and a knob
  turned right are told apart at a glance. The pointer alone gave the
  direction but not how far, which had to be read from the number below

### Changed

- **The inspector holds a console strip** rather than a second set of the same
  controls. It used to have its own fader, pan, meter and mute and solo
  buttons, and its own rack, which meant two of everything to keep in step by
  hand. The strip sits in two columns — the fader and its knobs on the left,
  inserts and sends on the right, with the write mode, VCA and instrument
  across the full width above them — and is pinned to the bottom of the panel,
  so it stays where it is when a MIDI track's extra input settings push
  everything else down. The panel's width no longer changes, and the handle for
  changing it is gone: a handle that moves nothing is worse than none

- **Fader and meter are half again as tall as their old minimum**, and the same
  height in every strip, master and VCA included. Those two carry neither pan
  nor mute and solo, so they used to end up some sixty pixels longer than the
  rest. The console panel now has a minimum height of its own, which follows
  from the meter not being allowed to shrink any further; at that minimum the
  rack disappears, which is the right thing to lose, since a rack can be
  scrolled and a meter cannot

- **The fader's height only changes when the divider above it is dragged.**
  Resizing the console gives the extra room to the rack and leaves the fader
  where it is. The divider also takes its starting point from the height on
  screen and will not remember a position that cannot be shown; before this,
  each drag added to a number that could no longer be reached, until the
  divider appeared to do nothing at all

- **The automation, mute and solo buttons in a track header are half again as
  large** and sit together as a group, in the console's order — mute, then
  solo. The two screens disagreed, and pressing the wrong one either silences
  the track or silences every other one. Record, input monitor and the
  inspector button stayed the size they were: what earns the extra room is
  being pressed often. The button that opens the inspector moved to the left of
  the track name, and on a folder it sits directly above the collapse triangle

- **Plugin latency in a console strip is shown just above the fader** and keeps
  its row whether or not there is anything to show, so a track with a plugin is
  no longer a few pixels shorter than one without. Master's name moved to the
  bottom of its strip, level with the track names beside it

---

## [0.7.0] - 2026-09-19

### Added

- **Orangutan Drums**, a sixteen-pad drum synthesizer. Every pad takes any of
  sixteen engines — kick, 808 sub, snare, clap, rim, tom, conga, closed and
  open hat, cymbal, cowbell, shaker, snap, zap, a noise sweep and a stick — and
  has its own tune, decay, tone, snap, level, pan and reverb send. Nothing is
  sampled: each engine is built from oscillators, noise and filters, so the
  knobs reach further than a sampled kit would. The closed hat chokes the open
  one, as on the machine this comes from, and it does so whichever pads the two
  are on, since the choke group belongs to the engine. Pads are MIDI notes 36
  to 51, laid out like an MPC with pad 1 at the bottom left. Ten factory kits
  come with it, and a master section with drive, glue compression, a reverb the
  pads send to, and a soft limiter

- **Any drum pad can leave on its own output**, to be picked up by a drum out
  track, instead of going into the main mix. A pad routed that way skips the
  master section entirely, so it is also not sent to the reverb. Pads whose
  output has not been made yet fall back to the main mix rather than going
  quiet — being audible in the wrong place is easier to understand than not
  being audible at all — and the screen says so while that is the case

- **Kakapo**, which tells you which scale you are playing in. It scores
  twenty-four candidates — twelve roots, major and natural minor — by counting
  the notes that fit and subtracting the ones that do not, so a chromatic run
  cannot fit everything at once, and shows one major and one minor with the
  notes each contains and how well it matches. Where the two are relative keys,
  C major and A minor for instance, the notes are identical and no score can
  separate them; the note played most decides which one it leans towards. The
  twelve pitch classes are drawn as bars, so the notes pulling the verdict can
  be seen rather than guessed. It listens to either the last few notes or the
  last few seconds, whichever you choose, and a Reset button clears what it is
  holding

- **Kakapo has a simple lead voice built in**, so a phrase can be checked by
  ear without routing MIDI to another track. It plays one note at a time, the
  last one pressed, and its sound is fixed: the only thing to set is its volume

### Changed

- **Knob values now read 0.0 to 10.0** in Racco Guitar, Java Rhino Bass and
  Orangutan Drums, showing how far round the knob is. The ranges behind them
  differ from knob to knob — nought to one here, nought to one and a half there
  — so the number by itself said little about the position. Where the number
  itself is the point it has been left alone: sustain stays in seconds, tune in
  semitones and pan reads zero in the middle

- **The right-hand edge of the control rows** in Racco Guitar, Java Rhino Bass
  and Kakapo now has the same margin as the left. The boxes and the controls
  beside them ran right up to the panel border while the left had a margin,
  which showed up as soon as the two sides were looked at together

---

## [0.6.0] - 2026-09-18

### Added

- **Triplets in the snap setting.** The list of divisions is unchanged; next to
  it there is now a **"3"** button that splits whichever note value you have
  chosen into three. Everything that snaps follows it — notes, clips, the
  chord lane, the loop, markers and the playhead — and the grid lines in the
  piano roll are drawn where the new division falls. The button is greyed out
  on "Bar" and "Free": a bar split into three no longer lands on the bar lines,
  and "Free" has no division to split. Choosing a different note value keeps
  the triplet on, so moving between 1/8 and 1/16 triplets takes one click

- **Chord flags can be selected several at a time.** Drag a box around them
  with the Select tool, or Ctrl-click to add and remove them one by one. Once
  selected they can be copied, cut and pasted at the playhead, deleted
  together in a single undo step, and **duplicated by holding Ctrl while you
  drag them** — the same way clips have always worked. Escape clears the
  selection. A copy dropped where a flag already stands replaces it, rather
  than leaving two flags in the same place

- **The view can follow the playhead.** A button beside the snap setting, in
  both the arrangement and the piano roll, turns it on; it starts on. Rather
  than scrolling continuously, the view turns the page: when the playhead
  reaches the edge it jumps forward so the playhead sits a tenth of the way in,
  and the next screenful then stays still while you read it. Rewinding and
  looping turn the page back the same way

- **Drum rows can be renamed in place**, by double-clicking the name, instead of
  through a separate window

- **Part names can be saved as presets and loaded again**, from the right-click
  menu on any drum row. A preset holds the note numbers, the part names and the
  choke groups — not which rows are muted, since that is what you are working on
  rather than what the kit is. Loading one renames the rows it covers and adds
  any it has that your map does not; it never removes a row, because a row that
  goes away takes its notes off the screen with it

### Changed

- **Chord flags are now picked up by the flag itself**, not by the whole band
  beneath it. The bands sit edge to edge along the whole song, so while they
  could be grabbed anywhere there was nowhere on the chord track left to start
  a selection box. Pressing the band now starts a box, exactly as it does on an
  empty part of any other track; double-clicking it still adds a chord there.
  Flags too narrow for their name to be drawn can still be grabbed by the band,
  since otherwise there would be nothing to take hold of

- **The chord pad writes to the track you have selected.** Choosing a MIDI
  track, or a clip on one, now switches the pad's destination to match, so the
  chords you play go where you were just working. The menu is still there and
  still works if you would rather pick without leaving the screen. Selecting an
  audio or chord track leaves the destination alone — chords cannot be written
  to either, and you are usually sitting on the chord track while you enter them

- **Java Rhino Bass has a new backdrop**, a pale one in place of the dark
  green. The knob names and values were hard to read against the old image even
  after they were given a dark outline to lift them; they are now drawn in a
  single dark colour with no outline, as they are in Racco Guitar. Nothing
  about the sound has changed


- **Chord regions can be given a length of their own**, by dragging their right
  edge. Until now the length was always "up to the next flag" and could not be
  set; the trade is that a chord can now stop before the next one begins, and
  the gap has no chord in it. Deleting a flag therefore leaves a gap where it
  was, instead of the chord before it stretching to fill the space. The left
  edge still moves the flag, since that is where the chord begins

- **A chord flag dropped on top of another replaces it**, rather than nothing
  happening. Flags can also be dragged past their neighbours now

- **The piano roll shows every key**, from C-1 to G9, rather than the five
  octaves that could be played. What you see when you open it is unchanged —
  C7 is still at the top — and the rest is there when you scroll to it

- **The drum editor shows all 128 rows too.** The drum map now decides which
  rows have names rather than which rows exist, so notes outside it are visible
  and editable in place. Rows without a name show the note name instead. The
  note that used to say how many notes were hidden is gone, because none are.
  A row joins the map when you name it, mute it or put it in a choke group

- **Every channel strip in the console is now cut to the same height.** The
  rack of inserts and sends took as much room as its contents wanted, so a
  track with four plugins had a shorter fader and meter than a track with one,
  and the meters no longer lined up to be read against each other. The rack is
  now one height shared by every strip, including the master; drag the line
  below it to change them all at once, and anything that does not fit scrolls
  as it did before. **The fader and meter are never squeezed below a readable
  size** — the rack gives way first, since the rack can scroll and a meter
  cannot

- **A project now keeps everything in a folder of its own.** Saving into the
  projects folder creates `<the project's name>/` and puts the project file
  inside it, along with `Backup` (the copy kept from the previous save), `Rec`
  (recordings), `Stems` and `Mixdown`. A project saved anywhere else treats the
  folder it is in as its own, so **nothing you have already saved moves or
  breaks**. Recording asks you to save first, so that the audio has a folder to
  go to. The preferences now name two folders instead of four: where projects
  go, and where templates go

### Fixed

- **Renaming a drum row no longer silences it.** Double-clicking the name
  toggled the row's mute as well, left over from when a single click on the
  name was what muted a row

- **Dragging the end of a note now resizes every note you have selected.**
  Moving them and changing their velocity already worked on the whole
  selection; the length was the one thing that still only applied to the note
  under the pointer. They stretch by the same amount rather than all becoming
  the same length, so a phrase of mixed note values keeps its shape, and the
  preview shows what will happen before you let go. Dragging the left-hand end
  works the same way

- **In the drum editor, clicking a note no longer picks the one before it.**
  Drum notes are drawn as a marker with no tail, but the area you could click
  was still as wide as the note was long — so a note left over from a coarser
  grid covered the markers after it, and the one underneath was the one that
  got selected. Nothing on screen showed the overlap. The clickable area is now
  the marker you can see, notes entered in the drum editor are no longer longer
  than a sixteenth, and where two notes do overlap the one drawn on top is the
  one you get

- **Copying a marker section no longer leaves out the first thing in it.** The
  chord at the start of the section, and the first note of the part written
  from it, were being treated as though they fell outside. Anything placed at
  the playhead sits on a whole sample rather than exactly on the beat — about
  one and a half millionths of a second early at 167bpm — and the test for
  "inside the section" was a millionth of a second tighter than that. Copy,
  cut, delete and move all share one test now, so they always act on the same
  notes

---

## [0.5.0] - 2026-09-17

### Added

- **Factory presets in every built-in plugin.** The synthesizer already had
  156 of them; now the EQ, the compressor, the delay, the reverb, the guitar
  and the bass each come with a set — between eight and twelve, grouped by
  what they are for. They sit in their own part of the preset menu, above
  anything you have saved yourself. Picking one sets everything back to its
  default first, so nothing from the preset before it is left behind

- **Plugins that have crashed twice now run in a separate process** instead of
  being refused. Until now, a plugin that had brought the application down
  twice was simply not loaded again. It is now opened in a sandbox: it plays,
  it saves with your project, its parameters can be automated, and instruments
  can be played through it. If it crashes there, only that process dies — the
  application keeps running and that plugin passes audio through untouched,
  with a message telling you what happened

- A plugin running in a sandbox **still opens its own window**. On Windows the
  window is placed inside Ember's, so it looks like any other plugin. On Linux
  it opens as a window of its own, beside a panel listing the controls. The
  window takes a moment to appear the first time, because it is being built in
  the other process; after that it opens at the size you left it. If the plugin
  crashes while you are looking at it, the window says so rather than going
  blank

### Changed

- **The tempo, key and time signature at the bottom of the window now follow
  the playhead.** If you change any of them partway through the song, the
  fields showed the value at the start regardless of where you were. They now
  show what is in force where the cursor is, and change as it passes each
  marker. Typing in them edits that same value — the one you are looking at —
  rather than the one at the start of the song, and the undo entry says which
  bar it changed. The key above the chord pad follows the playhead too; the
  pads themselves always did

- **Racco Guitar and Java Rhino Bass now play the same way twice.** Both drew
  their pick and slap noise from a source that started somewhere different
  every time the application ran, so the same project exported twice gave two
  files that were not identical. They still vary from note to note, and between
  the voices of a chord, but a render is now repeatable

- **Both instruments start a little quieter.** Played hard they were reaching
  full scale — sometimes just past it, depending on how that run's noise fell,
  which is why it went unnoticed. The guitar's output now starts at 0.70 rather
  than 0.80, and the bass at 0.30 rather than 0.35; the loudest notes land
  around 0.88 instead of just over 1.0. **Projects you have already saved are
  untouched** — this is only where each starts when you add a new one

- The knob labels and readouts in **Java Rhino Bass** are now white with a
  dark edge. On the speckled background of that instrument, dark text
  disappeared into the bright half of the picture

### Fixed

- **Ember started at 8 kHz on Linux.** With no audio device chosen yet, it
  opened whatever the system offered as its default, and on a PipeWire system
  that turned out to be 8 kHz with a buffer of 16 samples. Nothing appeared to
  be wrong — it played, and there was no error — but everything sounded dull,
  and the EQ drew dips in the treble that nobody had placed, which is how it
  was noticed. It now asks for 48 kHz and 512 samples, and falls back to the
  nearest your device can do. **If you have already chosen a device under
  Preferences → Audio, your choice is kept** — check the buffer size there if
  you set it up before this release

- A plugin in a sandbox that could not keep up with one block of audio **lost
  the notes in that block**. A missed note-off left the sound playing with no
  way to stop it. Notes now travel in a queue of their own rather than
  alongside the audio, so none are lost however far behind the plugin falls —
  they arrive a fraction of a beat late instead. Audio is still dropped for
  that one block, which is what you want there: a moment without the effect is
  better than a gap. Notes are not audio, though, and losing one leaves
  everything after it wrong

---

## [0.4.0] - 2026-09-16

### Added

- **Racco Guitar**, a sixth built-in plugin and the second instrument — an
  electric guitar modelled string by string rather than sampled:
  - A **648 mm scale** string model. Because the pickup sits a fixed distance
    from the bridge, the same note sounds different played open or fretted,
    the way it does on a real guitar
  - **Brightness**, **Sustain** (in seconds, and the same length at every
    pitch), **Pick Position**, **Hardness** and **Attack**, with the pickup
    switchable between front, centre and rear
  - Six **key switches** (A#1 to D#2) for the articulation: normal, palm mute,
    natural harmonic, pinch harmonic, slide and brushing. Palm mutes get deeper
    the softer you play, and the pinch harmonic's order follows Pick Position
  - **Hammer-ons, pull-offs and slides need no key switch** — they come from
    holding one key and playing the next
  - E2 to D6 sound, as on a 22-fret guitar. Notes outside that range are
    silent, and the keyboard shows them greyed out
  - The **modulation wheel** bends the note upwards only, the way bending a
    string does
  - **No amp or cabinet.** It sends out a DI sound for the amp of your choice

- **Java Rhino Bass**, a seventh built-in plugin and the third instrument — a
  five-string jazz bass, modelled string by string rather than sampled:
  - A **34-inch scale** tuned B-E-A-D-G, with each string given its own
    brightness, decay and level, the way a thicker string behaves
  - **Two pickups you blend.** In the middle both are full up — the jazz bass
    sound, with the scooped middle that comes from summing them
  - **Brightness**, **Sustain**, **Pluck Position**, **Tone**, **Hardness**,
    **Attack** and **Clank** — the last being the strings hitting the frets
    when you slap
  - Six **key switches** (C5 to F5) for the style: finger, pick, slap, mute,
    ghost and harmonic. On slap, the two top strings are **popped** instead,
    the way they are actually played
  - **Hammer-ons, pull-offs and slides** come from how you play, and can be
    switched off with Legato
  - B0 to G4 sound, as on a 24-fret five-string. The keyboard greys out the
    notes between the top of the range and the key switches
  - **No amp or cabinet**, and no compressor either: it sends out a DI sound

---

## [0.3.0] - 2026-09-15

### Added

- Audio clips can be made **mono** from the right-click menu, in the arrange
  view or the audio editor. The file is untouched — the channels are summed as
  it plays, so it can be undone at any time. The waveform is drawn as one
  channel too, so you can see which clips are mono
- Dropping audio into the empty area below the tracks now **makes tracks for
  it**, the way dropping an instrument there already did. Each file gets its
  own track, named after it, and they all start where you dropped them.
  Dropping onto a track still lays them end to end on that one track
- The pen tool now lets you **change the pitch while you are still placing a
  note**. Keep the button held and move up or down: the note follows, sounds as
  it moves, and the note name shows beside the cursor. Before, the pitch was
  settled the moment you pressed, so a note on the wrong row had to be deleted
  and put back

- **Bf Owl Reverb**, a fifth built-in plugin, with seven algorithms:
  - **Room**, **Hall** and **Ambience** — a room, a big space, and the air
    around a sound with barely any tail. Predelay, decay, size, and how the
    early reflections are shaped and spread in time
  - **Plate**, the classic dense metallic sound, built to Dattorro's published
    design. Nothing about it is our own tuning
  - **Random Hall**, a hall whose tail drifts. Turn Modulation up: two of the
    four delay lines move slowly, and the network spreads that movement through
    the rest
  - **Twin Delays**, two delays with one on each side, which can swap what they
    feed back so the repeats bounce between them. Not a reverb
  - **Panorama**, a stereo tool with width, mono sum, phase invert and a
    channel swap. No tail at all — turn Mix up to 100% to use it
  - Damping for the highs and the lows, so the tail darkens as it decays the
    way a real room does; and a saturation that only bites on the loud part
  - **Two engines**, wired as Single, Cascade, Mono Split or Stereo Split, each
    with its own algorithm and its own settings. The knobs show one at a time
  - A display that draws what you will hear: the gap before the reverb starts,
    each early reflection, and the slope of the tail — or the repeats, for Twin
    Delays, and the stereo width for Panorama

### Changed

- **Opening a project no longer leaves the screen empty.** The startup window
  stays where it is and says what it is doing, instead of closing and leaving
  nothing on screen until the project appears. Opening still takes as long as
  it did — a project with a lot of plugins has to build every one of them, and
  the spinner stops while that happens, so the window says so
- **Velocity bars are a third as wide.** At eight bars they ran together into a
  single band once the notes were close, so you could not count them. They are
  no easier to miss — the area you can grab is held separately from the width
  you see

### Fixed

- Where notes **start at the same time**, their velocity bars sit exactly on top
  of one another, and dragging one always took the same note however you had
  selected. It now follows the selection — and the selected bar is drawn in
  front, so you can see which one you have. Drawing velocity with the pen
  follows the selection in the same way, so you can thin out one note of a chord
  without touching the rest

- Ctrl-dragging a **multiple selection** of audio clips duplicated only the one
  you were holding. All of them are copied now, and the copies end up selected
- The **Linux window icon** was a generic cog. JUCE's icon setting covers
  Windows and macOS only; on Linux the icon has to be handed to the window by
  the application, and it has to be small enough for X to carry — the source
  image is 1000 px square, which was quietly too large to deliver
- The Linux archive now includes the icon and `install-desktop-entry.sh`.
  **GNOME ignores the window's own icon** and looks for a matching desktop
  entry, so without one it shows a generic icon however correct the window is
- The application reported version 0.1.0 to the system however it was built,
  because the number was written out in a second place instead of being read
  from the one that sets it

---

## [0.2.0] - 2026-09-12

### Added

- **Hawkbill Delay**, a fourth built-in plugin:
  - One echo line with tempo sync (dotted and triplet divisions), feedback,
    mix, and a timeline showing where the repeats fall
  - Four characters, applied to every repeat rather than once at the input, so
    the sound ages as it decays: Digital Clean, Analog BBD (darker the longer
    the delay, with a noise floor), Tape Echo (saturation, wow and flutter) and
    Lo-Fi (bit and rate crushing). Knobs a character does not use are greyed
    out rather than hidden, so you can see what it has
  - A filter inside the feedback loop - low pass, high pass, band pass, bell or
    notch - placed either before or after the character
  - An LFO on the delay time, with sine, triangle and sample-and-hold shapes.
    It works with every character, unlike wow and flutter
  - Ducking: the repeats pull back while the dry signal plays and come forward
    in the gaps, with a meter showing how far they are being pulled down
  - Up to eight taps, each with its own level and pan. A tap sits a whole
    number of Time steps back, so the same pattern plays the same rhythm
    whether Time is free or synced to the tempo. The feedback repeats the
    pattern as a unit
  - Two engines, A and B, wired as Single, Dual, Series, Split L/R or
    Ping-Pong, each with its own time, character, filter, modulation, ducking
    and taps, plus a level and a pan to balance them. The knobs show one engine
    at a time
  - Cross-feedback, which swaps what the two engines feed back into each other.
    At zero the two run in parallel; turn it up and the repeats start bouncing
    between them. The swap is a blend, so the repeats never grow louder than
    the feedback amount allows, however far it is turned
  - **Freeze**, which loops what is in the delay line right now and lets
    nothing new in. Nothing is coloured while it is held, so what loops stays
    exactly as it was rather than slowly darkening
  - **Reverse**, which turns the input around over a window as long as the
    delay time, so the reversed phrase lands neatly between the repeats and
    sits on the beat when Time is synced
  - **Diffuse**, which smears the repeats towards a reverb. Four fixed
    all-pass stages, slightly different lengths left and right so the smear
    opens outwards instead of collecting in the middle
- Plugins now receive the song tempo and position from Ember, so **third-party
  plugins that sync to tempo work**. They could not before
- Chord Pad can create a chord track when there is not one yet

### Changed

- Buttons that do the same kind of job share a width, and the piano roll tool
  row lines up with the display-mode buttons above it
- "+ Track" and "+ Audio" moved into the corner above the track headers,
  freeing a row of working space
- The status areas below the arrange view and the piano roll toolbar now
  appear only when there is something to say, and retire themselves
- Dragging a multiple selection onto a folder track moves all of it, in one
  undo step. A folder row shows a faint block spanning what its tracks contain

---

## [0.1.0] - 2026-09-10

The first release. Everything below is new, because there was nothing before
it — later entries will be shorter.

### Added

**Arranging and editing**

- Multi-track timeline with audio and MIDI tracks, folder tracks, send tracks,
  VCA tracks and a chord track
- Audio clips: trim, fade, reverse, transpose, time-stretch, gain, and
  transient (hit-point) detection
- Piano roll with velocity editing, quantise, groove quantise, CC lanes, and a
  drum editor with named rows and choke groups
- Chord pad and chord regions, with scale-aware note colouring
- Automation for volume, pan, plugin parameters and the master bus, with
  Read / Touch / Latch / Write modes and linear, curved and stepped shapes

**Mixing**

- Console with faders, pan, mute, solo, insert slots and sends
- Side-chain routing between tracks
- Plugin delay compensation

**Plugins**

- VST3 and LV2 hosting, with background scanning and a crash-resistant plugin
  list
- Three built-in plugins: **Ember EQ**, **Ember Comp** and **Red Panda**
  (see the README)

**Recording and export**

- Audio and MIDI recording with count-in and a metronome
- Mixdown, per-track stems, and MIDI file export
- WAV and FLAC output on every platform; MP3 on Windows only

**Platforms**

- Windows 10 and 11, 64-bit
- Linux, 64-bit, as an AppImage or a `.tar.gz` (built against glibc 2.39, so
  Ubuntu 24.04 / Linux Mint 22 and newer). ALSA and JACK instead of Windows
  Audio; the audio input is not opened on the first launch — choose it under
  Preferences → Audio when you want to record

**Everything else**

- Light and dark themes
- English and Japanese interface
- Auto-save every three minutes, with crash recovery
- Project templates and a startup project chooser

---

<!--
  8.193：**次の版を出すときは、この上に節を足してください**（Phase 230）。

  **まず直したぶんを`## [0.5.0] - 2026-09-17`へ書き、出すときに版と日付へ変えます。**

      ## [0.5.0] - 2026-09-17          ← 作業中はこれ
      ## [0.4.0] - 2026-XX-XX  ← 出すときにこう変える
      ### Added / Changed / Fixed / Removed

  **`CMakeLists.txt`の`project(... VERSION ...)`と揃えること。**
  タグとその版が食い違うと、ワークフローが**貼る前に止まります**
  （`.github/workflows/ember-linux.yml` の「Read the version」）。

  **exeの中の版も揃うか確かめること**（8.191・8.226。3回踏みました）。
  `out`と`out-ember`の`PersonalDAW_resources.rc`を消して、
  **`cmake -S . -B out`を打ち直す**——ビルドし直すだけでは古いままです。

  **利用者に見えることだけ書くこと。** 内部の作り直しは、
  動きが変わらないなら書かなくて構いません。
  開発の記録はリポジトリの HANDOVER にあります（公開版には含めていません）。
-->
