# ChromatikMacro

A VST3 plugin that streams Bitwig modulation to Chromatik/LX over OSC, configured
from a JSON file that MCP can write. Intended to replace the third-party **OSCpar**
plugin, and to retire the `bitwig-osc-bridge` controller extension entirely.

**Status: not built yet.** `CMakeLists.txt` exists; no source written. Start with
the blocking test below — everything else is contingent on it.

---

## Why

The existing approach (`~/Source/bitwig-osc-bridge`) is a Bitwig *controller
extension* that reads **remote controls** and forwards their values to OSC. That
works, but it forces:

- exposing every modulator and Grid output on a remote-controls page by hand
- "probe" cursors to reach devices nested inside Grid chains
- an 8-knob-per-page limit, so banks spill onto second pages
- track coordinates (`t25.d1.k2`) that churn whenever tracks are added or reordered

A **plugin parameter is addressable wherever it sits**. You modulate it in place,
exactly like OSCpar today. All of the above disappears.

A full session was spent mapping ~48 bindings through the extension, and most of
the difficulty was this plumbing rather than the actual mapping.

---

## THE BLOCKING TEST — do this first

The controller extension subscribes to `RemoteControl.modulatedValue()`, which the
host pushes **independently of audio processing**. A VST3 plugin only receives
parameter changes during `processBlock()`.

**So: if Bitwig suspends, bypasses, or sleeps the device, modulation may stop
reaching the plugin entirely.** If that happens, this whole design is dead and the
extension stays.

Build a stub that logs, from a **non-audio thread**, every ~250ms:

- wall-clock timestamp
- number of `processBlock()` calls since the last line
- the current value of each macro parameter

Then in Bitwig, assign a Grid output / LFO / Follower to a macro parameter and
check whether values keep updating when:

| condition | modulation still arriving? |
|---|---|
| transport stopped | ? |
| track silent (no audio) | ? |
| track muted | ? |
| device bypassed | ? |
| device deactivated / suspended | ? |
| plugin editor closed (there is no editor) | ? |
| offline bounce / render | should NOT send — see below |

Log to `~/.chromatik-macros/probe.log`.

Second, smaller blocking test: **does an externally-driven state change mark the
Bitwig project dirty?** If MCP reconfigures an instance and Bitwig doesn't know its
state changed, the config silently fails to persist.

---

## Design (v1)

**No editor.** `hasEditor()` returns false; Bitwig renders its generic parameter
panel. Modulation routing works against that, so nothing is lost. All configuration
is JSON + MCP.

**Parameters:**

```
"Slot"    integer 1..64, NON-AUTOMATABLE   <- identity
"macro1".."macro8"  float 0..1             <- the values that get sent
```

**Why a Slot parameter:** VST3 gives a plugin no way to read its own Bitwig device
name — there is no API for "what did the user rename this device to", and JUCE does
not expose one. So the instance cannot know it is "MoonOut". Making identity an
explicit integer parameter solves it with no UI and no networking: the user sets
`Slot = 7` in Bitwig's generic panel, and the plugin loads slot 7 from the JSON.
Duplicating a track produces two instances on slot 7 — a *visible* collision fixed
by bumping one, rather than an invisible UUID clash.

Mark it non-automatable so it can't be accidentally modulated.

**Config file** — `~/.chromatik-macros/mappings.json`, watched for changes:

```json
{
  "7":  { "name": "MoonOut",
          "prefix": "/lx/mixer/channel/NightChorus/modulation/Moon",
          "target": { "host": "127.0.0.1", "port": 3030 },
          "macros": { "1": {"scale": [0,1]}, "2": {"scale": [0,1]} } },
  "12": { "name": "LevelsOutA",
          "prefix": "/lx/mixer/channel/Rain/modulation/LevelsA",
          "macros": { "1": {"scale": [0,1]} } }
}
```

OSC address for macro N = `<prefix>/macro<N>`. Values sent as normalized 0–1 floats,
which is what LX expects for ranged parameters.

**No hub, no WebSocket, no daemon, no port to bind.** The plugin watches a file; MCP
writes that file. This deliberately rejects the hub/registry design — see "Codex
review" below for what that avoids.

**Also cache resolved config in the plugin state chunk**, so a project that moves to
another machine without the JSON keeps working. The file is the editing surface; the
state chunk is the fallback.

---

## Implementation notes

- **Never** do OSC or file I/O in `processBlock()`. Hand values to a worker thread
  via a lock-free queue, or "latest value + dirty flag" atomics.
- **Output cadence:** do not emit OSC per automation point. Cap at ~30–100 Hz with
  last-value coalescing and epsilon suppression.
- **Send a full snapshot** on connect, on mapping change, and periodically — UDP
  change-only output leaves Chromatik stale after a restart.
- **Suppress OSC during offline rendering.** A bounce must not alter a live rig.
- **Fix the parameter count at build time.** Hosts cache VST3 parameter schemas;
  never add/remove macros dynamically. Keep parameter IDs stable forever.
- **Do nothing in the constructor.** Hosts instantiate plugins during scanning —
  no file reads, no threads, no sockets until after state restoration and
  activation.
- **Detect duplicate OSC destinations.** Two instances driving the same address is
  last-packet-wins and looks like random jitter.

---

## Migration from OSCpar

The initial `mappings.json` can be **generated**, not hand-written. OSCpar stores
its full config as plaintext XML inside a ZIP appended to the `.bwproject` file:

```xml
<Preset Prefix="lx/mixer/channel/Sunrise/modulation/LevelsA"
        Address="0.0.0.0" Port="3030">
  <Macros><Macro Name="macro1" ScaleMin="0.0" ScaleMax="1.0" Type="0"/>...</Macros>
</Preset>
```

There are 25 such instances in the current show project. A working extractor
already exists — see the `bitwig-project` skill at
`~/Source/bitwig-osc-bridge/.agents/skills/bitwig-project/` (`scripts/bwproject.py presets <file>`).
That skill also documents the `.bwproject` format and its parsing traps.

Current show project:
`/Users/danoved/Dropbox/Projects/Apotheneum-DanO/Bitwig Project/Apotheneum/TreetopTransmission-Burn26.bwproject`

---

## Codex review — what it flagged

Codex reviewed this design. Points worth keeping:

- **Agreed the plugin can replace the extension** for this use case. What is only
  available to a controller extension: whole-project track/device discovery, the
  Bitwig selection and probe mechanism, and arbitrary remote controls / Grid
  outputs. None of that is needed if we only care about values routed *into* the
  plugin's own parameters. Transport is not a loss — the extension never streamed it.
- **Argued plugin state should be authoritative**, with any hub as a discovery
  layer only, for portability and preset-copy sanity. The JSON-file design honours
  this via the state-chunk fallback.
- **Warned hard about the suspension risk** above — it called this the most
  significant behavioural regression, and it is the reason for the blocking test.
- **Warned about the project-dirty problem** on externally-driven state changes.
- Recommended a narrow 8-parameter proof of concept with retirement contingent on
  those two tests passing. That is exactly the plan.

The hub/registry design was considered and rejected: it required reconnect logic,
startup-ordering handling, port-conflict ownership, and an `instanceId` collision
scheme. A watched JSON file plus a Slot parameter removes all of it.

---

## Toolchain

- `cmake` 4.4.2 — installed via Homebrew
- Apple clang 17, Command Line Tools at `/Library/Developer/CommandLineTools`
- JUCE 8.0.4 pulled by `FetchContent` (shallow) — first configure will take a while
- `CMakeLists.txt` is written: VST3 only, `COPY_PLUGIN_AFTER_BUILD TRUE`, links
  `juce_audio_utils` and `juce_osc`

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Built plugin lands in `~/Library/Audio/Plug-Ins/VST3/`.

Only `src/PluginProcessor.cpp` is referenced by `CMakeLists.txt` and it does not
exist yet — that is the first file to write.

---

## Related

- `~/Source/bitwig-osc-bridge` — the controller extension this would retire. It
  currently holds ~48 working bindings for the Burn26 show; do not break it until
  the plugin is proven.
- Chromatik OSC receive: `127.0.0.1:3030`.
- Channel names work in OSC addresses (`/lx/mixer/channel/Sunrise/...`), verified
  live — they don't have to be numeric indices.
