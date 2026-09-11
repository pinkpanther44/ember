# Ember

A digital audio workstation for Windows and Linux. Record, arrange, edit MIDI,
host VST3 and LV2 plugins, and mix — with three instruments and effects built in.

Ember is free and open source.

---

## Requirements

| | |
|---|---|
| OS | Windows 10 or 11, 64-bit — or a 64-bit Linux with glibc 2.39 or newer (Ubuntu 24.04, Linux Mint 22 and later) |
| CPU | Any x64 processor (developed on an Intel i7-9700K) |
| RAM | 4 GB or more |
| Audio | Any device the system can see. A dedicated audio interface is recommended but not required |
| Plugins | VST3 and LV2 (64-bit) |

**No extra runtime is needed on Windows.** The Visual C++ runtime is built into
the executable, so `Ember.exe` runs as it is.

### What differs on Linux

| | |
|---|---|
| Audio | ALSA or JACK, instead of Windows Audio |
| Audio input | **Not opened on the first launch.** Choose your input under Preferences → Audio when you want to record. A broken capture device could otherwise take the whole application down with it |
| MP3 export | **Not available.** It uses Windows Media Foundation. WAV and FLAC work as usual |
| Plugins | VST3 from `~/.vst3`, `/usr/local/lib/vst3` and `/usr/lib/vst3`; LV2 from `~/.lv2`, `/usr/local/lib/lv2` and `/usr/lib/lv2` (and anything on `LV2_PATH`) |

## Downloading

Every build is on the **[Releases page](https://github.com/pinkpanther44/ember/releases)** —
the Windows installer, a portable Windows zip, a Linux AppImage, and a Linux
`.tar.gz`. No account is needed.

## Installing

### Windows

- **Portable** — unzip anywhere and run `Ember.exe`. Nothing is written outside
  your user folders. Good if you want to keep several versions side by side.
- **Installer** — run `Ember-Setup.exe` and follow the prompts.

Windows may warn that the publisher is unknown, because the download is not
code-signed. Choose **More info → Run anyway** if you are happy to continue.

### Linux

- **AppImage** — one file, any distribution:

  ```
  chmod +x Ember-*-x86_64.AppImage
  ./Ember-*-x86_64.AppImage
  ```

  If it stops with `error loading libfuse.so.2`, your system has no FUSE 2.
  Either run it with `--appimage-extract-and-run`, or install `libfuse2t64`
  (`libfuse2` on older releases).

- **Archive** — `tar xzf Ember-*-Linux-x86_64.tar.gz`, then run `./Ember`
  from inside the folder.

**Start it from a terminal the first time.** A desktop launcher hides any
message the application prints if something goes wrong.

## First run

1. **Pick your audio device.** File → Preferences → Audio. Ember uses Windows
   Audio, which shares the device with other applications, so your browser and
   media player keep working while Ember is open.
2. **Wait for the plugin scan.** Ember looks through the standard VST3 and LV2
   folders in the background and tells you how many it found. This takes a while
   the first time and is quick afterwards.
3. **Choose a starting point.** The startup screen offers a template, a recent
   project, or an empty one.

> **If Windows asks about the firewall during the first scan**, it is a plugin
> being loaded, not Ember. Ember never opens a network connection, so you can
> decline safely.

## What is included

Four plugins are built in — nothing to install separately.

| | |
|---|---|
| **Ember EQ** | 12-band parametric EQ. Nine shapes, 6–96 dB/oct slopes, Mid/Side per band, dynamic EQ, spectrum analyser, and a linear-phase mode |
| **Ember Comp** | Compressor. Soft knee, auto/adaptive envelope, look-ahead, external side-chain, parallel mix, and a transfer curve overlaid on the level history |
| **Red Panda** | Virtual analog synthesizer. Two unison oscillators, sub and noise, filter, two envelopes, two LFOs, a 4-slot modulation matrix, two effect slots, EQ — and 156 factory presets |
| **Hawkbill Delay** | Delay. One echo line with tempo sync (dotted and triplet divisions), feedback and mix, and a timeline showing where the repeats fall. Four characters — Digital Clean, Analog BBD, Tape Echo and Lo-Fi — a filter inside the feedback loop, an LFO on the delay time, ducking, and up to eight taps with their own level and pan. Two engines can run as Single, Dual, Series, Split L/R or Ping-Pong, with cross-feedback between them. **More is on the way** — reverse, diffusion and freeze are planned |

## Where your files are kept

| | Windows | Linux |
|---|---|---|
| Projects | `Documents` (change it in Preferences) | `~/Documents` |
| Recordings | `Documents\Ember Recordings` | `~/Documents/Ember Recordings` |
| Templates | `Documents\Ember Templates` | `~/Documents/Ember Templates` |
| Settings, auto-save, plugin list | `%APPDATA%\Ember` | `~/.config/Ember` |

Project files use the `.em1` extension.

## Uninstalling

On Windows: delete the folder (portable), or use **Add or remove programs**
(installer). On Linux: delete the AppImage or the unpacked folder.

The Windows uninstaller offers to remove `%APPDATA%\Ember` as well. That folder
holds your settings, the plugin list, any plugin presets you saved, and the
auto-save backups. **Your projects and recordings are never touched.**

Otherwise, delete that folder by hand to remove the same things.

## Building from source

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMANTA_BRAND=ember
cmake --build build
```

CMake fetches JUCE on the first configure, so the first build needs an internet
connection.

On Linux, install the dependencies first:

```
sudo apt install build-essential cmake ninja-build pkg-config \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libxi-dev libglu1-mesa-dev mesa-common-dev \
  libcurl4-openssl-dev
```

## Licence

Copyright (C) 2026 pinkpanther44

Ember is free software: you can redistribute it and/or modify it under the terms
of the **GNU Affero General Public License, version 3 or later**, as published by
the Free Software Foundation.

Ember is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

The full text is in `LICENSE.txt`, and also at <https://www.gnu.org/licenses/>.

**The source code is at <https://github.com/pinkpanther44/ember>.**

### Third-party notices

| | |
|---|---|
| [JUCE](https://juce.com) | The framework Ember is built with, used here under the AGPLv3 option of the JUCE licence |
| LV2, with lilv, serd, sord and sratom | Used for hosting LV2 plugins. Bundled with JUCE; ISC licence, Copyright David Robillard |
| VST3 SDK | Used for hosting plugins, under the GPLv3 option. VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries |

Ember does **not** include the Steinberg ASIO SDK and contains no ASIO code.

"Ember" and the Ember icon are not covered by the AGPLv3 — the licence covers the
code. If you distribute a modified version, please give it a different name so
that people can tell the two apart.
