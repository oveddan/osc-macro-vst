#pragma once

#include "MappingConfig.h"

#include <array>

namespace oscmacro
{
// The eight VST3 ParameterID strings used for the macro parameters.
//
// These are deliberately NOT "macro1".."macro8". JUCE derives a plugin's VST3
// Steinberg::Vst::ParamID from the ParameterID string via
//     paramHash = juceParamID.hashCode() & ~(1u << 31)
// (see modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp,
// around line 666 in JUCE 8.0.4), where juce::String::hashCode() computes
//     result = 31 * result + character   (accumulated in uint32_t)
// over the string's characters.
//
// The commercial plugin this one replaces, OSCpar, exposes its eight macro
// parameters at VST3 IDs 0x08eaca05 .. 0x08eaca0c. Bitwig's project files
// record modulation targets and per-instance parameter tables using those raw
// IDs (e.g. "PID8eaca05"). To let OSCMacro drop into an existing Bitwig
// project *without rewiring every modulator by hand*, its macro parameters
// must hash to the exact same eight IDs.
//
// The strings below are a solved preimage of juce::String::hashCode(): each
// one hashes (after the VST3 wrapper's high-bit clear) to OSCpar's ID for the
// same macro slot. They were found by brute-force/algebraic search against
// the hash function above, and verified against the raw bytes in a real
// Bitwig project file. See tests/VST3ParamIdTests.cpp for the regression
// check that reimplements the same hash and asserts all eight values.
//
//   index  ParameterID   VST3 ParamID (hex)
//   0      "Jlkb~q1"     0x08eaca05
//   1      "Jlkb~q2"     0x08eaca06
//   2      "Jlkb~q3"     0x08eaca07
//   3      "Jlkb~q4"     0x08eaca08
//   4      "Jlkb~q5"     0x08eaca09
//   5      "Jlkb~q6"     0x08eaca0a
//   6      "Jlkb~q7"     0x08eaca0b
//   7      "Jlkb~q8"     0x08eaca0c
//
// IMPORTANT: changing any of these strings changes the VST3 parameter ID it
// produces, which silently breaks modulation wiring in every existing Bitwig
// project that targets it by "PID<hex>". Do not "clean these up" without
// understanding this.
//
// The human-visible parameter NAME (what Bitwig's generic panel displays, and
// what the rest of this codebase looks up via APVTS::getRawParameterValue /
// getParameter) is unaffected and stays "macro1".."macro8" - see
// PluginProcessor.cpp's use of oscmacro::paramIds and oscmacro::paramName.
inline constexpr std::array<const char*, macroCount> paramIds {
    "Jlkb~q1",
    "Jlkb~q2",
    "Jlkb~q3",
    "Jlkb~q4",
    "Jlkb~q5",
    "Jlkb~q6",
    "Jlkb~q7",
    "Jlkb~q8",
};

// The human-visible parameter name for macro `index` (0-based). This is what
// Bitwig shows in its generic parameter panel and what code elsewhere in the
// plugin uses to look up a macro's raw value/parameter object - it is
// intentionally decoupled from the VST3 ParameterID above.
inline juce::String paramName (int index)
{
    return "macro" + juce::String (index + 1);
}
}
