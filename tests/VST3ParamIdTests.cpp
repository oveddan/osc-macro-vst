// Regression guard for the cryptic ParameterID strings in src/ParamIds.h.
//
// Those strings are a solved preimage of juce::String::hashCode(), chosen so
// that JUCE's VST3 wrapper derives the exact same Vst::ParamID values that
// the commercial OSCpar plugin uses for its eight macro parameters
// (0x08eaca05 .. 0x08eaca0c). This is what lets OSCMacro drop into an
// existing Bitwig project and keep its modulation wiring intact.
//
// The hash and high-bit clear are reimplemented here (not called through
// PluginProcessor.cpp / JUCE's VST3 wrapper) so this test has no dependency
// on the VST3 SDK or the plugin-client build. If this test ever fails after
// touching src/ParamIds.h, the paramID strings changed and every existing
// Bitwig project using OSCpar-compatible modulation wiring will break.

#include "ParamIds.h"

#include <cstdint>
#include <iostream>

namespace
{
int failures = 0;

void expect (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Reimplementation of juce::String::hashCode(): result = 31*result + char,
// accumulated in a 32-bit unsigned integer, iterating the string's UTF-8/
// ASCII bytes (these ParameterID strings are all plain ASCII, so char-by-char
// iteration of the narrow string matches JUCE's behaviour here).
uint32_t juceHashCode (const char* text)
{
    uint32_t hash = 0;
    for (const char* p = text; *p != '\0'; ++p)
        hash = 31u * hash + static_cast<uint32_t> (static_cast<unsigned char> (*p));
    return hash;
}

// modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp (~line 666):
//   auto paramHash = static_cast<Vst::ParamID> (juceParamID.hashCode());
//   paramHash &= ~(((Vst::ParamID) 1) << 31);
uint32_t vst3ParamIdFor (const char* juceParamId)
{
    return juceHashCode (juceParamId) & ~(1u << 31);
}
}

int main()
{
    constexpr std::array<uint32_t, oscmacro::macroCount> expectedIds {
        0x08eaca05u,
        0x08eaca06u,
        0x08eaca07u,
        0x08eaca08u,
        0x08eaca09u,
        0x08eaca0au,
        0x08eaca0bu,
        0x08eaca0cu,
    };

    for (size_t index = 0; index < oscmacro::macroCount; ++index)
    {
        const auto actual = vst3ParamIdFor (oscmacro::paramIds[index]);
        expect (actual == expectedIds[index], "macro ParameterID derives OSCpar's VST3 param ID");
    }

    if (failures == 0)
        std::cout << "All VST3 param ID tests passed\n";

    return failures == 0 ? 0 : 1;
}
