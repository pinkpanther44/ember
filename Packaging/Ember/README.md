# Ember

A digital audio workstation for Windows. Record, arrange, edit MIDI, host VST3
plugins, and mix — with three instruments and effects built in.

Ember is free and open source.

---

## Requirements

| | |
|---|---|
| OS | Windows 10 or 11, 64-bit |
| CPU | Any x64 processor (developed on an Intel i7-9700K) |
| RAM | 4 GB or more |
| Audio | Any device Windows can see. A dedicated audio interface is recommended but not required |
| Plugins | VST3 (64-bit) |

**No extra runtime is needed.** The Visual C++ runtime is built into the
executable, so `Ember.exe` runs as it is.

## Installing

Two ways — pick either:

- **Portable** — unzip anywhere and run `Ember.exe`. Nothing is written outside
  your user folders. Good if you want to keep several versions side by side.
- **Installer** — run `Ember-Setup.exe` and follow the prompts.

Windows may warn that the publisher is unknown, because the download is not
code-signed. Choose **More info → Run anyway** if you are happy to continue.

## First run

1. **Pick your audio device.** File → Preferences → Audio. Ember uses Windows
   Audio, which shares the device with other applications, so your browser and
   media player keep working while Ember is open.
2. **Wait for the plugin scan.** Ember looks through the standard VST3 folders
   in the background and tells you how many it found. This takes a while the
   first time and is quick afterwards.
3. **Choose a starting point.** The startup screen offers a template, a recent
   project, or an empty one.

> **If Windows asks about the firewall during the first scan**, it is a plugin
> being loaded, not Ember. Ember never opens a network connection, so you can
> decline safely.

## What is included

Three plugins are built in — nothing to install separately.

| | |
|---|---|
| **Ember EQ** | 12-band parametric EQ. Nine shapes, 6–96 dB/oct slopes, Mid/Side per band, dynamic EQ, spectrum analyser, and a linear-phase mode |
| **Ember Comp** | Compressor. Soft knee, auto/adaptive envelope, look-ahead, external side-chain, parallel mix, and a transfer curve overlaid on the level history |
| **Red Panda** | Virtual analog synthesizer. Two unison oscillators, sub and noise, filter, two envelopes, two LFOs, a 4-slot modulation matrix, two effect slots, EQ — and 156 factory presets |

## Where your files are kept

| | |
|---|---|
| Projects | `Documents` (change it in Preferences) |
| Recordings | `Documents\Ember Recordings` |
| Templates | `Documents\Ember Templates` |
| Settings, auto-save, plugin list | `%APPDATA%\Ember` |

Project files use the `.em1` extension.

## Uninstalling

Delete the folder (portable), or use **Add or remove programs** (installer).

The uninstaller offers to remove `%APPDATA%\Ember` as well. That folder holds your
settings, the plugin list, any plugin presets you saved, and the auto-save backups.
**Your projects and recordings are never touched.**

With the portable version, delete `%APPDATA%\Ember` by hand to remove the same things.

## Building from source

```
cmake -S . -B build -DMANTA_BRAND=ember
cmake --build build --config Release
```

CMake fetches JUCE on the first configure, so the first build needs an internet
connection. The result is `build/PersonalDAW_artefacts/Release/Ember.exe`.

## Licence

See `LICENSE.txt`.

Ember is built with [JUCE](https://juce.com) and hosts VST3 plugins.
VST is a trademark of Steinberg Media Technologies GmbH.
