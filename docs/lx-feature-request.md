# Draft feature request for Chromatik / LX

**Not filed yet.** Review, then post to wherever Heron Arts takes requests
(`heronarts/LX` GitHub issues, or the Chromatik community).

---

## Title

Allow a component's OSC address to be set explicitly and pinned, independent of its
label and location

## Problem

A component's OSC address is derived entirely from where it sits and what it is
called. In `LXModulator`:

```java
public String getOscAddress() {
  LXComponent parent = getParent();
  if (parent instanceof LXOscComponent) {
    return parent.getOscAddress() + "/" + getOscPath();
  }
  return null;
}

public String getOscPath() {
  String path = super.getOscPath();
  if (path != null) {
    return path;
  }
  return getOscLabel();   // falls back to the label
}
```

So a `MacroKnobs` bank reachable at:

```
/lx/mixer/channel/9/modulation/LevelsB/macro3
```

changes address if you rename the modulator, rename the channel, or move the
modulator to a different parent. Reordering channels is safe, since channels also
resolve by name — but renames and moves are not.

That is reasonable as a default. The difficulty is that **OSC is one-way over UDP**,
so an external sender has no way to learn its address stopped resolving. A rename in
Chromatik silently breaks every external controller pointed at that component, with no
error on either side. The symptom is a control that simply stops doing anything, which
is easy to attribute to the controller, the network, or one's own patch.

## Why it matters in practice

We drive a Chromatik show from Bitwig Studio, with ~25 plugin instances each sending
to a different macro bank, and ~150 modulation wirings hanging off those banks. The
addresses are typed into each plugin instance by hand.

Every rename or reorganisation on the Chromatik side is therefore a silent breakage
we only discover later, and auditing it means reconstructing the mapping from outside
Chromatik entirely. Renaming a channel — an ordinary thing to do while building a
show — is currently a destructive act for external control.

## Request

An optional, user-settable, persisted OSC address for a component, which is used in
preference to the derived one and does not change when the component is renamed or
moved.

Two parts, either useful alone:

1. **Explicit address** — set the OSC path for a component by hand.
2. **Pinned/locked** — once set, it survives rename and reparenting.

## Possible shape

Not prescriptive, just to show it need not be invasive:

- an optional `oscAlias` string on `LXOscComponent` (or initially just `LXModulator`),
  persisted with the component
- `getOscAddress()` returns the alias when set, otherwise the current derived address
- the OSC engine keeps an alias → component map, rebuilt on load and on alias change,
  and consults it when resolving an incoming address
- a warning if two components claim the same alias

Scoping it to `MacroKnobs` / `MacroTriggers` first would cover the external-control
case almost entirely, since those are what external surfaces target.

## Alternatives we considered, and why they fall short

- **Naming discipline** — treat channel and modulator labels as a frozen API. Works
  until someone renames something, and the failure is still silent.
- **A custom component with its own identity** that registers at a stable address.
  Viable, and we may do it — but replacing an existing `MacroKnobs` breaks every
  modulation wire attached to it, so it is a heavy migration for what is essentially
  an addressing concern.
- **Repair tooling outside Chromatik** — detect broken links by comparing external
  config against the live project over the MCP API. This is what we are doing now. It
  finds breakage after the fact; it cannot prevent it.

None of these can be solved outside Chromatik, because the address is computed inside
Chromatik from state only Chromatik owns.

## Related nicety, if it is ever cheap

A per-component indication of recent OSC activity — "last received 0.2s ago" — would
make a dead external link visible immediately. Chromatik is the receiver, so it needs
no reply path or protocol change to know this. It would have caught every problem
described above at the moment it happened rather than weeks later.
