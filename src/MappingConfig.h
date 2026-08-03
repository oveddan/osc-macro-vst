#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace chromatik
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
}
