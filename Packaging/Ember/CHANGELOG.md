# Changelog

All notable changes to Ember are listed here. Dates are the release dates.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

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
