# OSCMacro

A VST3 plugin that streams DAW modulation to any OSC receiver. A direct replacement
for the third-party **OSCpar** plugin, addressing specific annoyances with it (below)
while keeping the same basic model: N macro parameters, an OSC prefix, one instance
per destination.

**This is not Chromatik-specific and not Bitwig-specific.** Nothing in the design ties
it to either — it sends OSC to a configurable host, port and prefix, from any VST3
host. Chromatik/LX is simply the receiver it is being built for first, and Bitwig the
host it is being tested in.

**Status: v1 implementation complete; final host validation remains.** File-backed
mappings, UUID identity, cached state, rate-limited OSC, hot reload, reset-on-load,
the minimal editor and project-dirty notification are implemented. The earlier
prototype behavior and reset-on-load are validated below; the new editor still needs
its explicit Bitwig test before this should be used against the live port.

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

Now implemented, awaiting host validation:

- **Reset-on-load defaults** — per-macro `initial`, default 0. Set
  `"resetOnLoad": false` on a macro that should retain its saved hand-set value.
  Reset is queued once per project-state restoration, after resolving the current
  file entry first and cached state as fallback; config hot reload never resets a
  running macro.
- **Minimal editor** — an editable name field and a status line showing the OSC
  target and `sending`, `unconfigured`, `collision`, `error`, `offline` or
  `inactive`. Required because VST3 parameters cannot be strings, and Bitwig's
  generic parameter panel does not allow typing an exact value.
- **Identity = a self-generated UUID, with a human name as a separate field.**
  Implemented — see "Identity" below.
- Host-capability and edge-case tests below.

### Config semantics — important

Three rules define the behavior:

1. **Plugin state is authoritative.** Config travels inside the plugin, so a track
   copied to another project carries its configuration with it. *(verified)*
2. **A missing file entry means "keep what you have"**, never "clear". *(verified —
   the moved-file test)*
3. **Routing config only pushes.** The plugin writes only the narrow registration
   surface (UUID plus name); prefix, target and macro routes flow from the file into
   plugin state. The file is a bulk editor for loaded instances, not the authority
   for whether a saved instance keeps working.

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

The final product name is **OSCMacro**: a generic "expose N modulatable parameters
and stream them as OSC" device. Neither the product nor its config path names a host
or receiver.

The prototype's VST3 class ID (`Dovd` / `Cmac`) is intentionally retained so the
existing Bitwig test project can resolve the renamed plugin without losing parameter
assignments. Its internal `ChromatikMacro` state-tree type is also retained solely for
backward-compatible state restoration; neither legacy identifier is user-facing.
The repository directory and remote can be renamed independently. Verified in
Bitwig: the existing test project resolved the device as OSCMacro with its previous
macro assignments intact.

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
| 2 | Does Bitwig attach a file path via VST3 `IStreamAttributes` on state load? | If yes, config can be scoped per project (`~/.osc-macro/<project>.json`) and the multi-tab problem disappears. If no, scope by name instead. |
| 3 | Are plugin instances live in an **inactive Bitwig tab**? | Only one tab has an active audio engine. If inactive-tab instances are unloaded, the cross-tab config problem is moot. If they are loaded but not processing, their config watcher is still running and will adopt file edits. |
| 4 | Can an exact value be typed into a parameter in Bitwig's generic panel? | **Already answered: no.** This is why identity is a name in an editor, not a numeric parameter. |

---

## Edge cases, and how to test each

| # | case | expected | status |
|---|---|---|---|
| 1 | Copy a track containing the VST into **another project** | Config travels in the state chunk; no file entry needed; instance keeps working | Implied by the moved-file test; **verify explicitly** |
| 2 | **Two Bitwig tabs** open, one audio engine active, both containing an instance with the same name; edit the config file | Both loaded instances may adopt the edit, but only an instance receiving recent process callbacks may own and send to the destination | **implemented, untested** |
| 3 | Same UUID present in **two unrelated projects** (a track copied between them); edit the file | Both get updated — they share one config entry | **untested**; related to the deferred duplication limitation |
| 4 | **Duplicate a track within one project** | Two instances share a UUID and one config entry; the active destination registry lets one send and reports a collision on the other | **known limitation, deferred** — see "Duplicating a track" |
| 5 | Project moved to another machine **without** the config file | Cached state keeps everything working | **verified** |
| 6 | **Offline bounce / render** | OSC suppressed — a bounce must not disturb a live rig | implemented, **unverified** |
| 7 | **Device turned off** | Processing and modulation stop; recover on re-enable | **verified** — known limitation, keep devices enabled |
| 8 | **Silent track while transport runs**, other tracks producing audio | Modulation continues | **untested — highest remaining risk.** Distinct from stopped transport; this is the per-device smart-suspend case and the actual show condition |
| 9 | **Malformed or invalid config file** | Keep last valid mapping, report in `~/.osc-macro/plugin.log` | implemented, **unverified** |
| 10 | Two active instances configured to the **same OSC destination** | Detect, log, suppress until resolved; inactive instances relinquish ownership | implemented, **unverified** |
| 11 | **Reset-on-load**: save mid-timeline with macros at non-zero, reopen | Macros return to their configured initial value (default 0), not the saved position | **verified** — macro2 saved at 0.83 and reopened at 0.0 |

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
| plugin editor closed | editor is now implemented; **re-check** |
| offline bounce / render | suppression implemented; unverified |

Second blocking test — **does an externally-driven state change mark the project
dirty?** The plugin calls VST3 non-parameter-state notification when a resolved file
mapping changes, and Bitwig marks a previously saved project modified. **Verified.**

Third blocking test — **does reset-on-load beat Bitwig's saved parameter value?**
`macro2` was set to `0.83`, the project was saved and Bitwig was closed. After
reopening `ChromatikMacroTest` without touching the control, its OSC snapshot reported
`macro2=0.000000`. **Verified.** No stale `0.83` packet was observed after reopening.

### Prototype validation log — 2026-08-02

Setup: the prototype (then named ChromatikMacro) on Slot 1, transport stopped. A Bitwig LFO modulated `macro1`;
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

The temporary mapping migrates automatically from
`~/.chromatik-macros/mappings.json` to `~/.osc-macro/mappings.json` on first run and
still points to port 39031. It is harmless when no test receiver is running. Replace
it with production mappings before testing against Chromatik on port 3030. The
one-time migration was verified byte-for-byte after loading OSCMacro in Bitwig.

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
share one config entry and destination. The process-wide destination registry now
lets one actively processing instance send and reports `collision` on the other,
preventing last-packet-wins jitter inside one host process.

The unresolved part is identity: changing that shared file entry changes both copies.
For an independent copy today, remove and reinsert the plugin on the duplicated track
to mint a new UUID, then reconnect its Bitwig modulation assignments.

Automatic re-identification is deferred because a duplicate and an ordinary project
load both arrive as identical state restoration, and VST3 supplies no "just cloned"
signal. Project path via `IStreamAttributes` (host-capability test 2) may eventually
make true within-project duplication distinguishable from a copied track in another
project. Separate host processes cannot share the in-process collision registry.

**Config file** — `~/.osc-macro/mappings.json`, watched. If it does not yet exist,
OSCMacro imports the legacy `~/.chromatik-macros/mappings.json` once and leaves the
legacy file untouched:

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
`~/.osc-macro/plugin.log`.

Editing by hand or by agent works by locating the entry via its `name` field, so the
UUIDs stay out of the way.

The key is the instance UUID generated by the plugin. A fresh instance atomically
self-registers an `(unnamed)` entry with an empty prefix; editing its name in the
plugin updates only that entry's name and preserves its routes. Deleted instances do
not currently prune their entries, so remove confirmed orphans by hand when needed.

### Runtime behavior

- A dedicated worker performs all file and UDP work; the audio callback only reads
  host state and passes audio through.
- Values coalesced and sent at no more than 50Hz with epsilon suppression.
- Full snapshots after mapping/connect changes and every five seconds.
- Offline processing suppresses OSC.
- Duplicate destinations within the process are suppressed and logged.
- Destination ownership requires a recent process callback. This prevents a loaded
  but inactive Bitwig tab from permanently winning the process-wide collision
  registry; ownership is retried when processing resumes.

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
- **Detect duplicate OSC destinations.** Lease each destination to one recently
  processing instance and suppress colliding instances; otherwise two senders create
  last-packet-wins jitter.
- **Log the resolved config source on load** — "from state" or "from file entry".
  When something behaves oddly after a track copy, this shows immediately which won.

---

## Migration from OSCpar

The routing portion of `mappings.json` can be **generated**, not hand-written, after
the new UUID entries have self-registered. OSCpar stores its full config as plaintext
XML inside a ZIP appended to the `.bwproject` file:

```xml
<Preset Prefix="lx/mixer/channel/Sunrise/modulation/LevelsA"
        Address="0.0.0.0" Port="3030">
  <Macros><Macro Name="macro1" ScaleMin="0.0" ScaleMax="1.0" Type="0"/>...</Macros>
</Preset>
```

The current show contains 25 such instances. The migration uses an existing local
`.bwproject` preset extractor (`bwproject.py presets <file>`); that extractor and the
show project are not part of this repository.

### Modulation wiring now survives the device swap

Bitwig stores a modulation target as a string of the form
`...CONTENTS/ROOT_GENERIC_MODULE/PID<hex>`, where `<hex>` is the lowercase hex of the
target plugin's VST3 parameter ID — and JUCE derives that ID from the plugin's
`ParameterID` string via `juceParamID.hashCode() & ~(1u << 31)`. OSCMacro's eight
macro parameters use `ParameterID`s that are a **solved preimage** of that hash,
chosen so they land on OSCpar's exact IDs (`0x08eaca05`–`0x08eaca0c`, i.e.
`PID8eaca05`–`PID8eaca0c`). See `src/ParamIds.h` for the string table and the
regression test in `tests/VST3ParamIdTests.cpp` that pins all eight values.

This does **not** make the OSCpar → OSCMacro device swap itself scriptable — see the
table below, that part is still manual/UI-driven — but it removes the cost that used
to follow the swap: **once an OSCMacro instance replaces an OSCpar instance on a
track, Bitwig's existing modulator connections re-attach to it automatically**,
because the parameter IDs they reference already match. No per-modulator drag
operations, and no rewriting the human names to match a Bitwig-visible label. The
human-visible parameter name (`macro1`..`macro8`, what Bitwig's generic panel shows)
is unaffected by this — only the underlying VST3 parameter ID changed, and that ID is
not normally user-visible at all.

### How much of the switchover can be scripted?

| step | scriptable? |
|---|---|
| Back up the project | yes — the `.bwproject` is a single file, just copy it |
| Extract all 25 OSCpar prefixes, ports and per-macro scaling | **yes** — already working (`bwproject.py presets`) |
| Merge those routes into self-registered UUID entries, matched by human name | **yes, after the replacement instances have registered** |
| Replace the OSCpar device with the new plugin in the project file | **no — do not attempt via direct file edits.** Swapping the device means rewriting the VST3 class ID, plugin name, vendor and file path, all length-prefixed strings of different lengths, inside a binary container whose size/offset fields are not understood. One wrong byte and the project will not open. Do this in the Bitwig UI (delete OSCpar, insert OSCMacro) instead. |
| Re-assign each Bitwig modulator to the new device's parameters | **no longer needed** — the parameter IDs match, so existing modulator connections resolve to the new device automatically once it occupies the same slot. Verify this is actually true on a real project before relying on it broadly (tracked as an open item; not yet exercised end-to-end against a live Bitwig project). |

So the realistic remaining cost is UI-driven device replacement across 25 instances
(delete OSCpar, insert OSCMacro, in the Bitwig UI), not the modulator re-assignment
that used to follow it. The *addressing* — prefixes, scaling, which macros are live —
still comes across via the generated/merged `mappings.json` entries as before.

Practical approach:

1. Copy the project as a backup.
2. Add the replacement instances. Each creates an opaque UUID entry in
   `mappings.json`; give each instance a unique name in its editor.
3. Generate or merge the extracted OSCpar routes into those registered entries by
   matching the human names. UUIDs cannot be generated ahead of instantiation.
4. Migrate **one track at a time**, in the Bitwig UI: insert the OSCMacro instance,
   confirm its existing modulator connections carried over, then delete the OSCpar
   instance. Both plugins can coexist during the swap — different class IDs, no
   conflict — so there is no flag day.
5. While a track is mid-migration, make sure only one of the two is sending to a
   given OSC address. Two senders on one address is last-packet-wins and looks like
   random jitter rather than an obvious failure.
6. Delete the OSCpar instance once its replacement is verified.

### OSCpar class-ID compatibility declaration

Matching parameter IDs (above) handles the *within-a-device* wiring, but Bitwig still
has to be told that a *new* OSCMacro instance should inherit an *existing* OSCpar
device's identity to skip the manual delete-and-reinsert step entirely.

The VST3 spec has a mechanism for exactly this: `IPluginCompatibility`, which lets a
plugin declare "I can replace class UID X." OSCMacro declares this via
`AudioProcessor::getVST3ClientExtensions()` returning a
`VST3ClientExtensions::getCompatibleClasses()` override that lists OSCpar's class UID
(`ABCDEF019182FAEB4550666C4F534368`) — see `src/PluginProcessor.cpp`
(`OSCMacroVST3ClientExtensions`). This shows up in the built plugin's
`moduleinfo.json` as a `Compatibility` entry mapping OSCMacro's own class UID to
OSCpar's as an "Old" ID it supersedes. **Crucially, OSCMacro keeps its own VST3 class
UID** (from `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` in `CMakeLists.txt`, unchanged) —
this is a compatibility declaration, not UID reuse or impersonation, so it does not
conflict with OSCpar also being installed and does not require OSCMacro to pretend to
be OSCpar.

**Status: implemented but UNTESTED.** It is not known whether Bitwig actually reads
`IPluginCompatibility` / `moduleinfo.json` compatibility entries and offers
"replace with a compatible plugin" for an existing device — this varies by host, and
Bitwig's behaviour here has not been checked against a real project. It is gated
behind `advertiseOscParCompatibility` in `PluginProcessor.cpp` so it can be disabled
without removing the code if it turns out to cause problems (e.g. Bitwig picking
OSCMacro instead of OSCpar in some listing, or vice versa, when both are installed).
If it works, combined with the matching parameter IDs above, migration could become
close to zero-rewiring: Bitwig replaces the device and keeps the wiring, both from one
mechanism. If it doesn't work, the parameter-ID matching above still saves the
modulator re-assignment step after a manual device swap in the UI.

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

- A separate Bitwig controller extension can do the same job a different way.
  **Hazard: the current rig has ~48 bindings pointing at many of the
  same Chromatik macros that OSCpar devices in the show project also drive.** Two
  senders on one address is last-packet-wins and looks like random jitter. Clear its
  slots before running the show, or before testing this plugin against port 3030.
- `docs/linking-design.md` is **out of date** — it documents learn-by-demonstration,
  an announce packet, UUIDs and slot numbers, all discarded. The v2 notes above
  supersede it.
- Chromatik OSC receive: `127.0.0.1:3030`.
- Channel names work in OSC addresses (`/lx/mixer/channel/Sunrise/...`) — verified
  live; they do not have to be numeric indices.
- LX source is useful for reference when extending the Chromatik receiver side.
