# ChromatikMacro

A VST3 plugin that streams DAW modulation to any OSC receiver. A direct replacement
for the third-party **OSCpar** plugin, addressing specific annoyances with it (below)
while keeping the same basic model: N macro parameters, an OSC prefix, one instance
per destination.

**This is not Chromatik-specific and not Bitwig-specific.** Nothing in the design ties
it to either — it sends OSC to a configurable host, port and prefix, from any VST3
host. Chromatik/LX is simply the receiver it is being built for first, and Bitwig the
host it is being tested in. See "Naming and portability" — the product name needs
deciding *before* any show projects are built on it.

**Status: v1 prototype built and largely validated.** File-backed mappings, cached
state, rate-limited OSC, hot reload and project-dirty notification all work and are
tested (see validation log). Remaining v1 work: reset-on-load defaults, a minimal
editor, and the host-capability and edge-case tests listed below.

**Scope note:** this is now scoped as an *OSCpar replacement*, not the larger
Bitwig↔Chromatik linking redesign. Prefix-based addressing stays. The Chromatik-side
component and discovery work is deferred to v2 — see "Deferred to v2".

---

## Why build this rather than keep OSCpar

OSCpar works. The case for replacing it rests mainly on one recurring, per-session
annoyance; the rest are setup-time costs that are already largely paid.

| annoyance | severity | fixed here? |
|---|---|---|
| **Macro values restore stale on project load.** Save mid-timeline with a knob at 50%, reopen, and it is still at 50%. Every session means manually turning every macro down, saving at position 0, then playing so modulation takes over. | **every session** | yes — see reset-on-load |
| New instance defaults to 0–255 scaling on every macro, fixed by hand each time | setup | yes — configurable defaults |
| Config only editable through its own UI, one instance at a time; no bulk edit, not scriptable | setup / repair | yes — watched config file |
| Costs money, per-machine licensing | one-off | yes |
| No signal when a link is dead | continuous, low-grade | partly — needs the Chromatik side (v2) |
| Prefix breaks silently when a Chromatik component is renamed or moved | rare | no — v2 linking work |

**The stale-value problem is the main justification.** It is the only item that taxes
every working session rather than initial setup.

### Not needed from OSCpar

- 10 macros — 8 is sufficient for every bank in the current show
- `PassTransport`
- per-macro `Type` settings (verify nothing in the show relies on these)

---

## v1 scope

Already built and validated:

- 8 macro float parameters, modulatable from Bitwig
- OSC output, rate-limited ~50Hz with epsilon suppression and coalescing
- Full snapshot on load, on mapping change, and every 5s
- Watched config file with hot reload
- Cached config in the plugin state chunk
- Project-dirty notification on external config change

Remaining:

- **Reset-on-load defaults** — per-macro initial value, default 0, with an opt-out
  for macros that are hand-set rather than modulated. This is the stale-knob fix.
- **Minimal editor** — a name field and a status line (OSC target, sending or not).
  Required because VST3 parameters cannot be strings, and Bitwig's generic parameter
  panel does not allow typing an exact value, so nothing human-editable can be a
  parameter.
- **Identity = a self-generated UUID, with a human name as a separate field.**
  Decided — see "Identity" below. Change this before the editor is written; the
  prototype currently keys config by slot number.
- Host-capability and edge-case tests below.

### Config semantics — important

Three rules, two of which are already implemented and verified:

1. **Plugin state is authoritative.** Config travels inside the plugin, so a track
   copied to another project carries its configuration with it. *(verified)*
2. **A missing file entry means "keep what you have"**, never "clear". *(verified —
   the moved-file test)*
3. **The config file only ever pushes.** It is a bulk-edit surface for whatever
   project is currently open, not a database of all instances.

Framing the file as a *bulk editor rather than a database* resolves most of the
multi-project edge cases below.

---

## Deferred to v2

- MCP/API control of config (currently a hand- or agent-edited text file)
- **Master UI** — because every instance reads the same config file, any instance's
  editor can render all instances with the current one highlighted. No separate app
  or process needed.
- Chromatik-side `BitwigLink` component with a discovered-source picker.
  **Verified feasible:** `lx.engine.osc.addListener()` exists, and custom listeners
  receive *every* incoming message (see `LXOscEngine.EngineListener.oscMessage`,
  dispatch loop is unconditional), on the **engine thread** — the socket thread only
  parses and enqueues, `LXOscEngine.dispatch()` drains to `engineThreadEventQueue`
  and then invokes listeners. So a discovery component needs no locking.
  - Gotcha: `shouldAddressBeExcluded(prefixFilters, ...)` runs before listener
    dispatch. A prefix filter on the connection would silently drop messages.
  - Gotcha: with no custom listener registered, unmatched addresses throw and log
    per message — at 50Hz that floods the log. Register the listener before sending.
- Activity / last-seen indicator inside the Chromatik module. Cheap when wanted:
  Chromatik is the receiver, so it needs no reply path or protocol change. An
  indicator inside the *VST* would need one, and is not planned.
- Trigger variant for the `Palette` and `Monkeys` banks (`MacroTriggers` in Chromatik).

---

## Naming and portability

**Decide the product name before any show project is built on this.** A VST3's name
and class ID are written into every project that uses it. Renaming afterwards means
every existing project fails to resolve the device and loses its modulation
assignments. This is the *only* now-or-never decision here — adding formats, features
or config fields later is all additive.

`ChromatikMacro` is too narrow. Nothing about the plugin is Chromatik-specific — it
is a generic "expose N modulatable parameters and stream them as OSC" device.
Something like `OscMacro` / `MacroOut` describes what it actually is. The repo name
matters much less and can change any time.

### Host portability

The plugin uses only standard VST3/JUCE facilities, so it should work in any VST3
host. Points to keep in mind:

- **Ableton Live** has no Bitwig-style modulators. You would drive the macros with
  automation, Max for Live LFOs, or macro mapping. The parameters are ordinary
  automatable floats, so all of that works — but the blocking-test results below were
  measured in Bitwig and would need re-checking per host, especially device
  suspension behaviour.
- **`updateTrackProperties()`** (capability test 1) is well supported in Live and
  Logic. If it works there but not in Bitwig, automatic labels may be a
  host-dependent nicety rather than something to design around.
- **VST3 only. Do not build AU.** Bitwig does not host Audio Units at all (VST2,
  VST3 and CLAP only), and Ableton Live on macOS takes VST3. AU is needed solely for
  Logic Pro and GarageBand, which are not in use here. Adding a format later is
  **additive and non-breaking** — the VST3 keeps its class ID and existing projects
  are unaffected — so there is no reason to do it pre-emptively. (`FORMATS VST3 AU`
  in `CMakeLists.txt` whenever it is actually wanted.)
- CLAP would be the more interesting future format for Bitwig specifically, but JUCE 8
  does not support it natively — it needs the third-party `clap-juce-extensions`
  wrapper. Not worth it unless a concrete need appears.
- Nothing in the config format, OSC output or state handling is host-specific.

---

## Host-capability tests to run

All four are cheap, and each removes or creates work. Run them together in one pass
with the probe plugin and read the log.

| # | question | why it matters |
|---|---|---|
| 1 | Does Bitwig populate JUCE's `updateTrackProperties()` (track name, colour)? | If yes, instances get automatic sensible labels ("Moon", "WildRain") with no user action. Note two instances on one track would both report the same name, so it is a default, not an identity. |
| 2 | Does Bitwig attach a file path via VST3 `IStreamAttributes` on state load? | If yes, config can be scoped per project (`~/.chromatik-macros/<project>.json`) and the multi-tab problem disappears. If no, scope by name instead. |
| 3 | Are plugin instances live in an **inactive Bitwig tab**? | Only one tab has an active audio engine. If inactive-tab instances are unloaded, the cross-tab config problem is moot. If they are loaded but not processing, their config watcher is still running and will adopt file edits. |
| 4 | Can an exact value be typed into a parameter in Bitwig's generic panel? | **Already answered: no.** This is why identity is a name in an editor, not a numeric parameter. |

---

## Edge cases, and how to test each

| # | case | expected | status |
|---|---|---|---|
| 1 | Copy a track containing the VST into **another project** | Config travels in the state chunk; no file entry needed; instance keeps working | Implied by the moved-file test; **verify explicitly** |
| 2 | **Two Bitwig tabs** open, one audio engine active, both containing an instance with the same name; edit the config file | Only affects instances that are loaded. Depends on capability test 3 | **untested** |
| 3 | Same UUID present in **two unrelated projects** (a track copied between them); edit the file | Both get updated — they share one config entry | **untested**; related to the deferred duplication limitation |
| 4 | **Duplicate a track within one project** | Two instances share a UUID → one config entry, one OSC address → last-packet-wins | **known limitation, deferred** — see "Duplicating a track". Workaround: re-point the copy by hand |
| 5 | Project moved to another machine **without** the config file | Cached state keeps everything working | **verified** |
| 6 | **Offline bounce / render** | OSC suppressed — a bounce must not disturb a live rig | implemented, **unverified** |
| 7 | **Device turned off** | Processing and modulation stop; recover on re-enable | **verified** — known limitation, keep devices enabled |
| 8 | **Silent track while transport runs**, other tracks producing audio | Modulation continues | **untested — highest remaining risk.** Distinct from stopped transport; this is the per-device smart-suspend case and the actual show condition |
| 9 | **Malformed or invalid config file** | Keep last valid mapping, report in `~/.chromatik-macros/plugin.log` | implemented, **unverified** |
| 10 | Two instances configured to the **same OSC destination** | Detect, log, suppress until resolved | implemented, **unverified** |
| 11 | **Reset-on-load**: save mid-timeline with macros at non-zero, reopen | Macros return to their configured initial value (default 0), not the saved position | **not implemented** |

---

## Blocking-test results

The controller extension this replaces subscribes to `RemoteControl.modulatedValue()`,
which the host pushes independently of audio processing. A VST3 plugin only receives
parameter changes during `processBlock()` — so if Bitwig suspends or sleeps the
device, modulation could stop reaching it. This was the go/no-go question.

| condition | modulation still arriving? |
|---|---|
| transport stopped | **yes** |
| track muted | **yes** |
| track silent while other tracks play | **not tested** — see edge case 8 |
| device turned off | **no**; recovers when re-enabled |
| device deactivated / suspended | expected no; keep the device enabled |
| plugin editor closed | not applicable in the prototype; re-check once an editor exists |
| offline bounce / render | suppression implemented; unverified |

Second blocking test — **does an externally-driven state change mark the project
dirty?** The plugin calls VST3 non-parameter-state notification when a resolved file
mapping changes, and Bitwig marks a previously saved project modified. **Verified.**

### Prototype validation log — 2026-08-02

Setup: ChromatikMacro on Slot 1, transport stopped. A Bitwig LFO modulated `macro1`;
`macro2` and `macro3` held static. `mappings.json` targeted a temporary UDP receiver
at `127.0.0.1:39031`, not the live Chromatik port. Macros 1–3 enabled at
`/chromatik-macro/test`.

- Initial snapshot contained `macro1=0`, `macro2=0.405`, `macro3=0.520`; the latter
  two matched Bitwig's generic parameter controls.
- `macro1` produced changing OSC float packets at ~40–50Hz while transport was
  stopped and while the track was muted.
- A periodic full snapshot of all three enabled macros arrived five seconds later.
- Turning the plugin off stopped `processBlock()` and froze the modulated value;
  turning it back on restored modulation immediately.
- Editing the file live changed the prefix from `/chromatik-macro/test` to
  `/chromatik-macro/reloaded` without restarting Bitwig, and caused an immediate
  snapshot on the new addresses.
- Changing `macro1` scale from `[0,1]` to `[-1,1]` immediately produced the expected
  bipolar values.
- Changing only the mapping `name` marked a previously saved project modified;
  repeated after saving again, passed both times.
- After saving and closing Bitwig, `mappings.json` was moved aside. Reopening the
  project restored the cached prefix, bipolar scale, macro values, continuous LFO
  stream and periodic snapshots without the file. The file was then restored.

The temporary mapping at `~/.chromatik-macros/mappings.json` still points to port
39031 and is harmless when no test receiver is running. Replace it with production
mappings before testing against Chromatik on port 3030.

---

## Design

**Parameters:** 8 macro floats, modulatable. Nothing else is a parameter.

### Identity

VST3 provides no per-instance identifier — the class ID is shared by every instance
of a plugin. So the plugin **generates its own UUID on first instantiation** and
persists it in the state chunk.

That UUID is the config key. A separate `name` field carries the human label:

- **Rename-safe** — the name is display-only, so changing it breaks nothing. Keying
  by name would break the link on every rename, which is the same location-not-identity
  trap that path-based addressing has.
- **Self-registering** — on first run the plugin writes its own entry with an empty
  prefix and `"(unnamed)"`. The UUID is never typed or copied by a human; you fill in
  the name and prefix on an entry that already exists.
- **Travels with a copied track**, since it lives in the state chunk.

### Duplicating a track — known limitation, deferred

Duplicating a track copies the state chunk verbatim, UUID included, so both instances
share one config entry and send to the same OSC address. Last-packet-wins, which looks
like jitter rather than an obvious failure.

The plugin cannot detect this on its own: a duplicate and an ordinary project load both
arrive as `setStateInformation` with identical bytes, and VST3 gives no "you were just
cloned" signal.

**Not a regression** — duplicating a track with OSCpar today produces two devices on
the same OSC path, equally silently. Logged as a known bug to solve later rather than
designed around now. Workaround for the moment: after duplicating a track, point the
copy at a different destination by hand.

Notes for whenever it is picked up: a process-wide registry of live UUIDs would detect
the collision, but Bitwig runs multiple tabs in one process, so two separate projects
each containing a copied track look identical to a real duplicate. Host-capability
test 2 (project path via `IStreamAttributes`) is what would make them distinguishable
and the fix automatic.

**Config file** — `~/.chromatik-macros/mappings.json`, watched:

```json
{
  "a3f2c19d": { "name": "MoonOut",
                "prefix": "/lx/mixer/channel/NightChorus/modulation/Moon",
                "target": { "host": "127.0.0.1", "port": 3030 },
                "macros": { "1": {"scale": [0,1], "initial": 0},
                            "2": {"scale": [0,1], "initial": 0} } },
  "77bd4e02": { "name": "LevelsOutA",
                "prefix": "/lx/mixer/channel/Rain/modulation/LevelsA",
                "macros": { "1": {"scale": [0,1]} } },
  "c410a8f6": { "name": "(unnamed)",
                "prefix": "" }
}
```

OSC address for macro N is `<prefix>/macro<N>`, sent as normalized floats, which is
what LX expects for ranged parameters. `target` defaults to `127.0.0.1:3030`, `scale`
defaults to `[0,1]`, `initial` defaults to `0`. An empty `prefix` means the instance
is unconfigured and emits nothing. Only macros present in the mapping emit OSC.
Invalid edits retain the last valid mapping and are reported in
`~/.chromatik-macros/plugin.log`.

Editing by hand or by agent works by locating the entry via its `name` field, so the
UUIDs stay out of the way.

*(The current prototype keys this file by slot number; switching to UUID keys with a
name field is part of remaining v1 work.)*

### Runtime behavior

- A dedicated worker performs all file and UDP work; the audio callback only reads
  host state and passes audio through.
- Values coalesced and sent at no more than 50Hz with epsilon suppression.
- Full snapshots after mapping/connect changes and every five seconds.
- Offline processing suppresses OSC.
- Duplicate destinations within the process are suppressed and logged.

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
  no file reads, no threads, no sockets until after state restoration and activation.
- **Detect duplicate OSC destinations.** Two instances driving one address is
  last-packet-wins and looks like random jitter.
- **Log the resolved config source on load** — "from state" or "from file entry".
  When something behaves oddly after a track copy, this shows immediately which won.

---

## Migration from OSCpar

The initial `mappings.json` can be **generated**, not hand-written. OSCpar stores its
full config as plaintext XML inside a ZIP appended to the `.bwproject` file:

```xml
<Preset Prefix="lx/mixer/channel/Sunrise/modulation/LevelsA"
        Address="0.0.0.0" Port="3030">
  <Macros><Macro Name="macro1" ScaleMin="0.0" ScaleMax="1.0" Type="0"/>...</Macros>
</Preset>
```

25 such instances exist in the current show. A working extractor already exists — see
the `bitwig-project` skill at `~/Source/bitwig-osc-bridge/.agents/skills/bitwig-project/`
(`scripts/bwproject.py presets <file>`), which also documents the `.bwproject` format
and its parsing traps.

Current show project:
`/Users/danoved/Dropbox/Projects/Apotheneum-DanO/Bitwig Project/Apotheneum/TreetopTransmission-Burn26.bwproject`

### How much of the switchover can be scripted?

Honest split — the addressing config can be generated, the device swap cannot.

| step | scriptable? |
|---|---|
| Back up the project | yes — the `.bwproject` is a single file, just copy it |
| Extract all 25 OSCpar prefixes, ports and per-macro scaling | **yes** — already working (`bwproject.py presets`) |
| Generate `mappings.json` from that | **yes** |
| Replace the OSCpar device with the new plugin in the project file | **no — do not attempt.** Swapping the device means rewriting the VST3 class ID, plugin name, vendor and file path, all length-prefixed strings of different lengths, inside a binary container whose size/offset fields are not understood. One wrong byte and the project will not open. |
| Re-assign each Bitwig modulator to the new device's parameters | **no** — manual |

So the realistic cost is the modulation re-assignments: 25 instances with up to 8
modulators each, worst case ~100 drag operations. The *addressing* — prefixes,
scaling, which macros are live — comes across automatically, which is the part that
was tedious and error-prone to redo by hand.

Practical approach:

1. Copy the project as a backup.
2. Generate `mappings.json` from the existing OSCpar states.
3. Migrate **one track at a time**. Both plugins can coexist — different class IDs,
   no conflict — so there is no flag day.
4. While a track is mid-migration, make sure only one of the two is sending to a
   given OSC address. Two senders on one address is last-packet-wins and looks like
   random jitter rather than an obvious failure.
5. Delete the OSCpar instance once its replacement is verified.

The one route that avoids the manual re-assignment entirely is class-ID
impersonation, below — but it conflicts with shipping this as a general-purpose
plugin.

### OSCpar class-ID compatibility option

A private-rig migration could build the replacement using OSCpar's VST3 class ID and
an exactly matching parameter layout. If Bitwig resolves the existing devices to it,
all current modulation routing survives with no project edits at all. Verify first
that Bitwig's `PID8eaca05`–`PID8eaca0c` parameter derivation matches the replacement
schema.

Not the default strategy: reusing another vendor's UID is class-ID squatting, breaks
if OSCpar is also installed, and is acceptable only as a controlled private technique.

---

## Codex review — what it flagged

- **Agreed a plugin can replace the controller extension** for this use case. Only a
  controller extension can see whole-project track/device structure, the Bitwig
  selection, probe cursors, and arbitrary remote controls / Grid outputs — none of
  which matter if we only care about values routed into the plugin's own parameters.
  Transport is not a loss; the extension never streamed it.
- **Plugin state should be authoritative**, for portability and preset-copy sanity.
  Adopted — see config semantics.
- **The suspension risk** was called the most significant behavioural regression, and
  is why the blocking test came first. Mostly cleared; edge case 8 remains.
- **The project-dirty problem** on externally-driven state changes. Verified working.
- Recommended a narrow proof of concept with commitment contingent on those tests.
  That is what happened.

A hub/registry daemon was considered and rejected: reconnect logic, startup ordering,
port ownership and instance-ID collisions, all to solve what a watched file solves.

---

## Toolchain

- `cmake` 4.4.2 (Homebrew), Apple clang 17, Command Line Tools
- JUCE 8.0.4 via `FetchContent` (shallow) — first configure takes a while
- `CMakeLists.txt`: VST3 only, `COPY_PLUGIN_AFTER_BUILD TRUE`, links
  `juce_audio_utils` and `juce_osc`

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # mapping parser tests
```

Built plugin lands in `~/Library/Audio/Plug-Ins/VST3/`.

---

## Related, and one live hazard

- `~/Source/bitwig-osc-bridge` — a Bitwig controller extension doing the same job a
  different way. **Hazard: it currently holds ~48 bindings pointing at many of the
  same Chromatik macros that OSCpar devices in the show project also drive.** Two
  senders on one address is last-packet-wins and looks like random jitter. Clear its
  slots before running the show, or before testing this plugin against port 3030.
- `docs/linking-design.md` is **out of date** — it documents learn-by-demonstration,
  an announce packet, UUIDs and slot numbers, all discarded. The v2 notes above
  supersede it.
- Chromatik OSC receive: `127.0.0.1:3030`.
- Channel names work in OSC addresses (`/lx/mixer/channel/Sunrise/...`) — verified
  live; they do not have to be numeric indices.
- LX source for reference: `~/Source/lx`
