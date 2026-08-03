# Draft feature request for Chromatik / LX

**Not filed yet.**

---

## Option to lock a component's OSC address

When you rename or move a component, its OSC address changes and anything external
pointed at it breaks.

**Suggested fix:** either be able to set a custom OSC path that stays fixed, or be
able to lock the existing path so it doesn't change when the component is moved or
renamed. Right-click → "Set OSC address…" / "Lock OSC address" would cover it. Just
`MacroKnobs` / `MacroTriggers` would handle almost all external-control use.

**Separately, if it's ever cheap:** a "last received" indicator on a component would
make a dead link obvious.
