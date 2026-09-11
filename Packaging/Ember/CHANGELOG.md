# Changelog

All notable changes to Ember are listed here. Dates are the release dates.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

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
  - Still to come: multi-tap, dual engines, reverse and freeze
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
