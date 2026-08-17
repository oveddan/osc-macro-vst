---
name: oscpar-migration
description: Replace a VST3 plugin with a different one inside a Bitwig .bwproject file, preserving all modulation wiring. Written for migrating OSCpar to OSCMacro, but the technique is general.
---

# Swapping a VST3 plugin inside a .bwproject, keeping the wiring

Bitwig has no "replace this plugin with that one" command. Doing it by hand means
re-dragging every modulator: the real show project had 25 instances and ~125
modulation references, roughly 100 drag operations.

This works instead, and it is a **length-neutral byte patch**. Nothing in the
container is resized, so the format's nested length fields never have to be
understood — which is what makes it safe.

**It has been done successfully on the real show project.** Do not treat any of
the below as theory.

---

## The two mechanisms it rests on

### 1. Bitwig resolves a plugin device by class UID alone

The device record also caches the plugin's name, vendor, category, version and
file path. **All of those are cosmetic.** Patch only the 32-character class UID
and Bitwig loads the new plugin correctly — the device will still be *labelled*
with the old plugin's name, which is harmless.

This is the crux. Leaving the stale strings alone means no string changes length,
so no enclosing length field and no header offset needs recomputing.

### 2. Modulation targets reference the plugin's own VST3 parameter IDs

Bitwig stores modulation targets as strings like:

```
CONTENTS/POST_FX/Chain/DEVICE_CHAIN/0:CONTENTS/ROOT_GENERIC_MODULE/PID8eaca07
```

That is `PID` + lowercase hex of the **VST3 parameter ID**. It also keeps a
sparse per-instance table of materialised parameter IDs (field `0x1319`, u32),
each followed by that parameter's cached value (field `0x131a`, 8-byte double).

So the wiring survives a plugin swap **only if the new plugin emits the same
parameter IDs**. For a JUCE plugin you can force this exactly — see below.

---

## Making a JUCE plugin emit chosen VST3 parameter IDs

JUCE derives the VST3 param ID from the `ParameterID` string
(`juce_audio_plugin_client_VST3.cpp`, ~line 666):

```cpp
auto paramHash = static_cast<Vst::ParamID> (juceParamID.hashCode());
paramHash &= ~(((Vst::ParamID) 1) << 31);     // clear the sign bit
```

and `juce::String::hashCode()` is `result = 31 * result + char` accumulated in
`uint32`. Since 31 is odd it is invertible mod 2^32, so **any target parameter ID
has a reachable preimage**.

Because incrementing the last character increments the hash by exactly one, a
single prefix yields a consecutive run of IDs. For OSCpar's
`0x08eaca05`..`0x08eaca0c` the solved prefix is `Jlkb~q`, i.e. `"Jlkb~q1"` ..
`"Jlkb~q8"`. That is why `src/ParamIds.h` contains those otherwise inexplicable
strings. `paramID` is internal to VST3 and never displayed, so the cryptic value
is invisible to users; the human-readable parameter *name* is a separate argument.

`scripts/solve_param_ids.py` solves a new preimage if this is ever needed for a
different plugin.

**Changing these strings orphans every existing project.** They are a permanent
interface, not an implementation detail.

---

## Container facts needed for the patch

```
offset 0    "BtWg" + 38 ASCII hex chars (42 bytes total)
              [16:24] = u32 hex = offset where the main TLV body begins
              [32:40] = u32 hex = offset of the appended ZIP
offset 42   metadata section, space-padded with 5000 spaces of slack, then '\n'
body        TLV stream:  <u32 fieldId><u8 type><payload>,  fieldId 0 ends an object
ZIP         standard zip holding plugin-states/<instance-uuid>.vstpreset
```

Both header offsets are absolute and were verified byte-exact on two independent
files. A length-neutral patch leaves both untouched.

Useful field IDs on a plugin device record: `0x009b` vendor, `0x03eb` name,
`0x03ec` path, `0x0be5` `<uuid>.vstpreset`, `0x198e` class UID hex, `0x198f`
version, `0x1319` parameter ID, `0x131a` cached parameter value.

Type `0x0d` is a nested length-prefixed blob (payload begins `BtWg`). **If you
ever resize a string you must fix its enclosing `0x0d` lengths and both header
offsets.** Staying length-neutral avoids this entirely; that path is unexplored.

---

## Procedure

1. **Quit Bitwig completely.** Verify with `pgrep -f BitwigStudio`. It will
   overwrite your edit on save otherwise.
2. **Back up the project file** and verify the copy with `cmp`.
3. **Patch the class UID**, in both the body and every `.vstpreset` in the ZIP:
   ```
   scripts/patch_plugin_uid.py in.bwproject out.bwproject <OLD_UID_HEX> <NEW_UID_HEX>
   ```
   Both UIDs must be 32 hex chars. The script refuses to write if the body
   length changes.
4. **Clear Bitwig's plugin-state cache** — see the trap below. This step is not
   optional and its omission looks like an unrelated bug.
5. **Open and verify the wiring**, then save.

`scripts/extract_oscpar_config.py` dumps each instance's OSC prefix, host, port
and per-macro scaling from the pre-patch file — useful for auditing, and for
reconstructing config if state adoption is unavailable.

### Invariants to assert after patching

All of these held on the real migration:

- body length unchanged; header `[32:40]` still equals the ZIP offset
- count of `PID<hex>` references unchanged
- `0x1319` parameter-ID table identical, same distribution across parameters
- `0x131a` cached values identical (the real project had 15 nonzero)
- zero occurrences of the old UID; ZIP entry count unchanged; CRCs valid
- every `.vstpreset` header re-stamped to the new class UID

---

## Traps

- **Bitwig caches extracted plugin states OUTSIDE the project**, at
  `~/Library/Application Support/Bitwig/Bitwig Studio/plugin-states/<uuid>.vstpreset`,
  keyed by instance UUID. It reuses a cached copy rather than re-extracting from
  the patched ZIP, so a stale entry hands the new plugin a preset stamped with the
  *old* class UID and the host reports **"Could not load plug-in — Error loading
  VST3 preset"**. Delete the cache while Bitwig is closed; it is derived data.

  Worse, the UUIDs are identical between a project and any copy of it, so opening
  the *unpatched* original re-poisons the cache for the patched one. While
  switching back and forth, clear the cache every time.

- **A `.vstpreset` carries the class UID in its own header** (bytes 8..40, ASCII).
  Patch it there too or the host rejects the preset before the plugin ever sees it.

- **Changing parameter IDs orphans pre-existing instances of your own plugin.**
  Any project already using the new plugin loses its wiring. One-time cost; do it
  before the plugin has real users.

- **`0x1319` is sparse.** It lists only *materialised* parameters, not all of them
  — 110 entries across 25 instances, not 25×8. Don't assume a fixed stride.

- **The same UUID-looking string can be a machine identity.** A UUID appearing in
  every device record next to the computer name is the host identity, not a class
  ID. The class ID is the 32-char hex string with no dashes.

- **A filename is not the project.** Verify which file you are about to edit by
  its internal title (field `0x0044` near the body start) and its sha256, not by
  its name. In the real migration two files named `-Burn26` and `-Burn26-old`
  had swapped contents relative to expectations, and one was byte-identical to a
  previous year's show.

- **Plugin format compatibility declarations do not help here.** JUCE 8.0.4's
  `VST3ClientExtensions::getCompatibleClasses()` correctly emits a
  `Compatibility` block into `moduleinfo.json`, but **Bitwig 6.1 ignores it** —
  tested, the device reported "plugin missing". The declaration is harmless to
  keep; do not rely on it.
