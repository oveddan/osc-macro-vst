# Linking design — how a Bitwig plugin instance finds its Chromatik component

Status: proposal. Nothing below is implemented. The v1 prototype currently uses
path-rooted addressing (option A) and works; this document is about what replaces it.

---

## 1. The problem

A Bitwig VST3 instance streams 8 macro values over OSC. Somewhere in a Chromatik
(LX) project, a component must receive them. Something has to establish *which*
component, and that association must survive normal show-building activity:

- renaming a Chromatik channel or modulator
- moving a modulator to a different channel
- reordering channels
- duplicating a Bitwig track, or a Chromatik component
- copying the project to another machine
- rebuilding a bank from scratch mid-rehearsal

The current show has ~25 such links and ~150 modulation wirings hanging off them.

**The defining constraint: OSC over UDP is fire-and-forget.** The sender cannot
know whether anything received a packet. So any broken link is silent by default,
and silence is indistinguishable from "that macro just isn't moving right now".

This is not hypothetical. In the session that produced this project, several
mappings had been dead for an unknown length of time, and finding out required
parsing a proprietary binary project file and cross-referencing a live MCP
connection — a full session of work that still produced a fragile answer.

**Design goal: make a broken link loud, and make establishing one unmissable.**

---

## 2. Alternatives considered

### A. Path-rooted addressing (what OSCpar does, and the v1 prototype)

The plugin stores a prefix; the address is `<prefix>/macroN`.

```
/lx/mixer/channel/NightChorus/modulation/Moon/macro1
```

| | |
|---|---|
| Chromatik code | none — works with stock `MacroKnobs` |
| Rename channel/modulator | **breaks, silently** |
| Move component to another parent | **breaks, silently** |
| Reorder channels | survives (LX resolves by label, not index) |
| Duplicate component | two components answer one address |
| Discoverability | none |

Rejected as the end state. A path is a *location*, not an identity, so it inherits
every rename and move. Retained as the v1 prototype behaviour only because it
required no Chromatik-side code to prove the plugin approach worked at all.

### B. Shared human-typed name

Both sides store the same string, e.g. `MoonOut`. Address becomes
`/link/MoonOut/macroN`.

| | |
|---|---|
| Rename/move on either side | survives |
| Setup | type the same name in two places |
| Failure mode | typo or one-sided rename → silent orphan |
| Readability | good — the JSON and the LX component both self-describe |

Better than A, and it was the working proposal for a while. The weakness is that
correctness depends on typing discipline across two applications, and the failure
is still silent. It also tempts you to encode meaning into the key (`Rain.LevelsA`),
which quietly reintroduces the rename problem the moment the thing it names changes.

### C. Generated UUID, pasted across

Same as B, but the key is opaque and machine-generated.

Removes the "meaningful name that later becomes wrong" trap, but makes every other
part of the experience worse: unreadable config, nothing self-describes, and
copy-paste of a 36-character string is its own error source. Duplication still
copies the UUID.

### D. Hub / registry daemon

A resident local daemon; each plugin instance connects out over WebSocket,
registers, and receives config. MCP is a thin client of the daemon.

Reviewed by Codex, which agreed the topology works but argued configuration should
not live solely in the hub. Rejected here for cost rather than correctness: it
requires reconnect-with-backoff, startup ordering (Bitwig may load before the hub),
fixed-port ownership and conflict handling, and an `instanceId` collision scheme —
a lot of moving parts, all of which exist to solve a problem a watched file already
solves.

### E. Learn by demonstration, plus announce  ← recommended

Identity is generated on the Bitwig side and adopted by Chromatik through a
listen-and-capture gesture, with a periodic announcement making the whole set
enumerable.

Detailed below.

---

## 3. Recommended design

### Identity

Each VST instance carries an opaque, auto-generated `linkId` (`link-a3f2`) plus a
cosmetic `label` ("MoonOut"). The `linkId` is the key; the label is for humans and
may be changed freely with no functional effect.

Keeping these separate is deliberate. Any scheme where the human-readable name *is*
the key reintroduces rename fragility the first time the thing it describes changes.

Addressing is unchanged from B:

```
/link/<linkId>/macro1 .. macro8
```

### Pairing: listen and capture

1. Add a `KnobLink` (or `TriggerLink`) in Chromatik.
2. Click **Listen**.
3. Move a knob on the Bitwig instance.
4. The component adopts the `linkId` of the first message it hears, and persists it.

No name is typed anywhere, so typos and one-sided renames cannot produce a
mismatch. It also matches how the work is actually done — the thing you just
touched is the thing you meant to link. This is the MIDI-learn pattern, which is
already the established idiom for exactly this problem.

### Announce: defeating UDP's one-way blindness

Each instance periodically emits:

```
/link/announce  <linkId>  <label>  <type: knobs|triggers>  <macroCount>
```

Chromatik keeps a registry of what it has heard. This costs one small packet per
instance per interval and needs no hub, no TCP, and no second channel — but it
turns an unanswerable question into a table:

| link | announced from Bitwig | claimed by a component | status |
|---|---|---|---|
| `link-a3f2` "MoonOut" | yes | `KnobLink` on NightChorus | OK |
| `link-77bd` "LevelsOutB" | yes | — | **orphaned** — nothing is listening |
| `link-c410` "Palette" | no | `TriggerLink` | **unclaimed** — nothing is sending |

The orphaned row is the failure that is invisible under every other option here.

### Where truth lives

| location | role |
|---|---|
| VST state chunk | authoritative for that instance; verified to survive a missing `mappings.json` |
| `~/.chromatik-macros/mappings.json` | editing surface — MCP writes it, the plugin adopts changes |
| LX component | authoritative for the Chromatik end; set by Listen |

Two independent declarations that must agree, plus a registry that reports when
they do not. This is deliberately preferred over a single source of truth that
cannot be verified from both ends.

### Collisions

Duplicating a Bitwig device copies its `linkId`; duplicating a Chromatik component
copies its claim. Both surface in the registry as a conflict.

**Never auto-resolve.** Auto-rekeying on a duplicate would mutate a legitimate
instance when two project versions are open, or when connection order at project
load differs. List the conflict and let a human choose.

---

## 4. Chromatik side

Two component classes, not one: `KnobLink` and `TriggerLink`. They mirror the
existing `MacroKnobs` and `MacroTriggers` parameter surfaces, which is what makes a
one-for-one class swap possible while preserving `macro1..8` parameter names and the
modulation wires referencing them.

Trigger contract: momentary with automatic reset. Each activation produces an event
and returns low. A latched trigger cannot fire twice without an intervening off
message — a poor fit for repeated show cues. Latching, if wanted, is an explicit
mode rather than the default.

Migration is a scripted edit of a **copy** of the `.lxp`, replacing the class string
while retaining component ID and parameter names, so all ~150 existing wirings
survive. Explicitly *not* via MCP `add_modulator`, which mints a new component ID
and would force rebuilding every wiring. Main risk is class-not-found on load —
Chromatik may drop an unknown component along with its modulations — so the classes
must be installed first, Chromatik closed during the edit, and wiring counts
verified per channel before the copy replaces anything.

---

## 5. Bitwig side

Two products from one codebase: `ChromatikKnobs` and `ChromatikTriggers`. With no
custom editor, a single combined product would show eight dead controls in every
instance on Bitwig's generic parameter panel.

Identity within Bitwig remains the non-automatable `Slot` integer, because VST3
gives a plugin no way to read its own host device name. `Slot` maps to a `linkId`
via the JSON and the cached state chunk.

---

## 6. Open questions

1. **Does `Listen` need a timeout or an explicit confirm?** A component left in
   listen mode will capture the next instance that moves, which may not be the one
   intended.
2. **Should the plugin announce on a distinct port** from the macro stream, so a
   Chromatik restart does not miss announcements queued during downtime? Or is a
   short announce interval (~2s) sufficient?
3. **Should `linkId` be visible/editable** in the Chromatik component, for manual
   repair when Listen is impractical (e.g. rebuilding from a backup with Bitwig
   closed)?
4. **Announce interval vs show traffic.** 25 instances at 2s is trivial, but the
   macro stream is already ~50Hz per active macro; confirm no interaction.
5. Does the registry belong in the Chromatik component tree, or in the MCP layer
   that queries it?
