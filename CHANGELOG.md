# Changelog

All notable changes to Ember are listed here. Dates are the release dates.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

### Added

- Audio clips can be made **mono** from the right-click menu, in the arrange
  view or the audio editor. The file is untouched — the channels are summed as
  it plays, so it can be undone at any time. The waveform is drawn as one
  channel too, so you can see which clips are mono
- Dropping audio into the empty area below the tracks now **makes tracks for
  it**, the way dropping an instrument there already did. Each file gets its
  own track, named after it, and they all start where you dropped them.
  Dropping onto a track still lays them end to end on that one track

### Fixed

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

      ## [0.2.0] - 2026-XX-XX
      ### Added / Changed / Fixed / Removed

  **`CMakeLists.txt`の`project(... VERSION ...)`と揃えること。**
  タグとその版が食い違うと、ワークフローが**貼る前に止まります**
  （`.github/workflows/ember-linux.yml` の「Read the version」）。

  **利用者に見えることだけ書くこと。** 内部の作り直しは、
  動きが変わらないなら書かなくて構いません。
  開発の記録はリポジトリの HANDOVER にあります（公開版には含めていません）。
-->
