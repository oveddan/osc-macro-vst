# Draft feature request for Chromatik / LX

**Not filed yet.**

---

## Option to pin a component's OSC address

Renaming a channel or a modulator changes its OSC address, so any external controller
pointed at it silently stops working. Same if a modulator gets moved to a different
parent. Nothing errors — the control just goes dead, and you usually find out much
later.

We drive a show from Bitwig with ~25 senders hitting different macro banks, so a
rename while building is a breakage we don't notice until something looks wrong on
stage.

**Suggested fix:** let a component's OSC path be set explicitly and pinned, so it
stops tracking the label and survives being renamed or moved. Right-click →
"Set OSC address…" on the component would cover it. Doing it just for `MacroKnobs` /
`MacroTriggers` would handle almost all external-control use.

**Separately, if it's ever cheap:** a "last received" indicator on a component would
make a dead link obvious immediately.
