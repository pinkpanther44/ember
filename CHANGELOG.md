# Changelog

All notable changes to Ember are listed here. Dates are the release dates.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

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

  **まず直したぶんを`## [Unreleased]`へ書き、出すときに版と日付へ変えます。**

      ## [Unreleased]          ← 作業中はこれ
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
