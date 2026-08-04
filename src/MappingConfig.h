#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace oscmacro
{
constexpr auto macroCount = 8;

struct MacroRoute
{
    bool enabled = false;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float initial = 0.0f;
    // Reset persisted host parameter state to initial when a project is loaded.
    // Hand-set macros can retain their saved value by setting resetOnLoad to false.
    bool resetOnLoad = true;
};

struct Mapping
{
    juce::String identity;
    juce::String name;
    juce::String prefix;
    juce::String host = "127.0.0.1";
    int port = 3030;
    std::array<MacroRoute, macroCount> macros {};

    [[nodiscard]] juce::String addressFor (int macroIndex) const;
};

juce::Result parseMappingsFile (const juce::String& json,
                                const juce::String& identity,
                                Mapping& mapping,
                                juce::String& resolvedJson);

juce::Result parseResolvedMapping (const juce::String& json,
                                   const juce::String& identity,
                                   Mapping& mapping);

// Returns a complete mappings-file document after registering or renaming one
// instance. An empty fallbackResolvedJson creates a fresh, unconfigured entry;
// a non-empty fallback must be one resolved mapping object from plugin state.
juce::Result upsertMappingName (const juce::String& currentJson,
                                const juce::String& identity,
                                const juce::String& name,
                                const juce::String& fallbackResolvedJson,
                                juce::String& updatedJson);

// ---------------------------------------------------------------------------
// OSCpar migration ("adoption")
//
// OSCpar is also a JUCE plugin, so when Bitwig hands its state chunk to
// OSCMacro in its place (see the VST3 class-UID compatibility declaration in
// PluginProcessor.cpp / the "Migration from OSCpar" section of README.md),
// juce::getXmlFromBinary() parses it successfully - the root element is just
// <Preset ...> rather than OSCMacro's own APVTS-derived state type. These
// functions convert that payload into an OSCMacro mapping instead of
// rejecting it.
struct OscParAdoption
{
    // Deterministic identity: MD5 of the full OSCpar XML payload text, 32
    // lowercase hex characters, no dashes - formatted like OSCMacro's normal
    // generated identities. This is only stable across repeated loads of the
    // *unmodified* OSCpar chunk; once Bitwig saves the project again, OSCMacro
    // persists its own state chunk (with its own generated UUID lineage) and
    // this derived identity is no longer involved.
    juce::String identity;

    // Readable name derived from the prefix's path segments, e.g.
    // "Sunrise/LevelsA" - better than "(unnamed)" across many adopted instances.
    juce::String name;

    // The converted routing: prefix, target and up to the first 8 macro routes
    // (OSCpar's Macro9/Macro10 are dropped - OSCMacro has 8 macros).
    Mapping mapping;

    // `mapping` serialized as one resolved-mapping JSON object, matching the
    // shape parseResolvedMapping()/upsertMappingName() expect - ready to drive
    // self-registration into mappings.json exactly as a normal cached-state
    // restore does.
    juce::String resolvedJson;
};

// True if `root` is an OSCpar state chunk (root element <Preset>) rather than
// OSCMacro's own APVTS-derived state (root element "ChromatikMacro" - see
// PluginProcessor.cpp).
bool isOscParPreset (const juce::XmlElement& root);

// Converts an OSCpar <Preset> payload into an adoption record. `root` must be
// the parsed XML (as returned by juce::getXmlFromBinary on the raw state
// chunk); `fullPayloadText` is that same payload as text, used verbatim for
// the identity hash - typically root.toString(). Fails (leaving `result`
// unspecified) when `root` is not a <Preset> element, or when its Prefix
// attribute is empty/missing - OSCpar's own convention for "unconfigured",
// which callers should treat exactly like an unrecognised chunk.
juce::Result adoptOscParPreset (const juce::XmlElement& root,
                                const juce::String& fullPayloadText,
                                OscParAdoption& result);
}
