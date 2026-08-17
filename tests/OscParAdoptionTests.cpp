// Covers converting a real OSCpar plugin-state XML payload (<Preset ...>)
// into an OSCMacro Mapping - see the "OSCpar migration (adoption)" section of
// MappingConfig.h and README.md's "Migration from OSCpar".

#include "MappingConfig.h"

#include <cmath>
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

bool nearlyEqual (float a, float b, float epsilon = 0.0001f)
{
    return std::abs (a - b) < epsilon;
}

bool isHex32 (const juce::String& text)
{
    if (text.length() != 32)
        return false;

    for (auto index = 0; index < text.length(); ++index)
    {
        const auto c = text[index];
        const auto isLowerHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');

        if (! isLowerHex)
            return false;
    }

    return true;
}
}

int main()
{
    // A real, complete OSCpar payload, pasted verbatim (whitespace as-is).
    const juce::String realPayload =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?> <Preset "
        "Prefix=\"lx/mixer/channel/Sunrise/modulation/LevelsA\" Address=\"0.0.0.0\" "
        "Port=\"3030\" FileName=\"123.xml\"><PARAM id=\"Macro1\" value=\"1.00000\"/>"
        "<PARAM id=\"Macro2\" value=\"0.36000\"/><PARAM id=\"Macro3\" value=\"0.25000\"/>"
        "<PARAM id=\"Macro4\" value=\"0.00000\"/><PARAM id=\"Macro5\" value=\"1.00000\"/>"
        "<PARAM id=\"Macro6\" value=\"0.53000\"/><PARAM id=\"Macro7\" value=\"0.01000\"/>"
        "<PARAM id=\"Macro8\" value=\"1.00000\"/><PARAM id=\"Macro9\" value=\"0.00000\"/>"
        "<PARAM id=\"Macro10\" value=\"0.00000\"/><PARAM id=\"PassTransport\" value=\"On\"/>"
        "<Macros><Macro Name=\"macro1\" ScaleMin=\"0.0\" ScaleMax=\"1.0\" Type=\"0\"/>"
        "<Macro Name=\"macro2\" ScaleMin=\"0.0\" ScaleMax=\"1.0\" Type=\"0\"/>"
        "<Macro Name=\"macro3\" ScaleMin=\"0.0\" ScaleMax=\"1.0\" Type=\"0\"/>"
        "<Macro Name=\"macro4\" ScaleMin=\"0.0\" ScaleMax=\"1.0\" Type=\"0\"/>"
        "<Macro Name=\"macro5\" ScaleMin=\"0.0\" ScaleMax=\"1.0\" Type=\"0\"/>"
        "<Macro Name=\"macro6\" ScaleMin=\"0.0\" ScaleMax=\"1.0\" Type=\"0\"/>"
        "<Macro Name=\"macro7\" ScaleMin=\"0.0\" ScaleMax=\"1.0\" Type=\"0\"/>"
        "<Macro Name=\"macro8\" ScaleMin=\"0.0\" ScaleMax=\"1.0\" Type=\"0\"/>"
        "<Macro Name=\"Macro9\" ScaleMin=\"0.0\" ScaleMax=\"10.0\" Type=\"0\"/>"
        "<Macro Name=\"Macro10\" ScaleMin=\"0.0\" ScaleMax=\"10.0\" Type=\"0\"/>"
        "</Macros></Preset>";

    auto realXml = juce::XmlDocument::parse (realPayload);
    expect (realXml != nullptr, "real OSCpar payload parses as XML");
    expect (realXml != nullptr && oscmacro::isOscParPreset (*realXml),
            "real payload is recognised as an OSCpar preset");

    oscmacro::OscParAdoption adoption;
    const auto result = oscmacro::adoptOscParPreset (*realXml, realPayload, adoption);
    expect (result.wasOk(), "real payload adopts successfully");

    // Leading slash added (OSCpar stores the prefix without one).
    expect (adoption.mapping.prefix == "/lx/mixer/channel/Sunrise/modulation/LevelsA",
            "leading slash is added to the prefix");

    // Address 0.0.0.0 -> 127.0.0.1 (bind address is not a valid destination).
    expect (adoption.mapping.host == "127.0.0.1",
            "0.0.0.0 destination address maps to 127.0.0.1");

    // Port parsed.
    expect (adoption.mapping.port == 3030, "port is parsed from the Port attribute");

    // Name derived from the prefix's path segments.
    expect (adoption.name == "Sunrise/LevelsA",
            "name is derived from the prefix's last two meaningful segments");

    // Only 8 macros adopted (Macro9/Macro10 dropped), scales correct,
    // matched by POSITION in <Macros> rather than by Name string (the real
    // payload mixes "macro1".."macro8" lowercase with "Macro9"/"Macro10").
    for (auto index = 0; index < oscmacro::macroCount; ++index)
    {
        const auto& route = adoption.mapping.macros[static_cast<size_t> (index)];
        expect (route.enabled, "macro slot is enabled for each of the first 8 positions");
        expect (nearlyEqual (route.minimum, 0.0f), "ScaleMin adopts as 0.0 for macro1-8");
        expect (nearlyEqual (route.maximum, 1.0f), "ScaleMax adopts as 1.0 for macro1-8");
    }

    // The 9th/10th <Macro> entries (ScaleMax 10.0) must not leak into any of
    // the 8 adopted routes - if position-based matching were broken this
    // would show up as a maximum of 10.0 somewhere in macros[0..7].
    for (auto index = 0; index < oscmacro::macroCount; ++index)
        expect (! nearlyEqual (adoption.mapping.macros[static_cast<size_t> (index)].maximum, 10.0f),
                "Macro9/Macro10's wider scale does not leak into an adopted macro1-8 route");

    // Initial values picked up from the capitalised PARAM ids.
    const std::array<float, oscmacro::macroCount> expectedInitial {
        1.00000f, 0.36000f, 0.25000f, 0.00000f, 1.00000f, 0.53000f, 0.01000f, 1.00000f
    };

    for (auto index = 0; index < oscmacro::macroCount; ++index)
        expect (nearlyEqual (adoption.mapping.macros[static_cast<size_t> (index)].initial,
                            expectedInitial[static_cast<size_t> (index)]),
                "initial value matches the capitalised PARAM id's value");

    // Identity is a 32-character lowercase hex string, no dashes.
    expect (isHex32 (adoption.identity), "identity is 32 lowercase hex characters");
    expect (adoption.mapping.identity == adoption.identity,
            "mapping.identity matches the reported identity");

    // Identity deterministic across repeated parses of the same payload.
    oscmacro::OscParAdoption secondParse;
    auto secondXml = juce::XmlDocument::parse (realPayload);
    expect (secondXml != nullptr
                && oscmacro::adoptOscParPreset (*secondXml, realPayload, secondParse).wasOk(),
            "second parse of the same payload succeeds");
    expect (secondParse.identity == adoption.identity,
            "identity is deterministic across repeated parses of the same payload");

    // Identity differs for a payload differing in one macro value.
    const auto differingPayload = realPayload.replace ("\"Macro2\" value=\"0.36000\"",
                                                       "\"Macro2\" value=\"0.37000\"");
    expect (differingPayload != realPayload, "test payload variant actually differs");
    auto differingXml = juce::XmlDocument::parse (differingPayload);
    oscmacro::OscParAdoption differingAdoption;
    expect (differingXml != nullptr
                && oscmacro::adoptOscParPreset (*differingXml, differingPayload,
                                               differingAdoption).wasOk(),
            "differing payload adopts successfully");
    expect (differingAdoption.identity != adoption.identity,
            "identity differs for a payload differing in one macro value");

    // resolvedJson round-trips back into an equivalent Mapping (this is what
    // self-registers the full route into mappings.json on adoption).
    oscmacro::Mapping roundTripped;
    expect (oscmacro::parseResolvedMapping (adoption.resolvedJson, adoption.identity,
                                            roundTripped).wasOk(),
            "resolvedJson parses back as a resolved mapping");
    expect (roundTripped.prefix == adoption.mapping.prefix
                && roundTripped.host == adoption.mapping.host
                && roundTripped.port == adoption.mapping.port,
            "round-tripped mapping matches the adopted prefix/target");
    expect (roundTripped.macros[0].enabled
                && nearlyEqual (roundTripped.macros[0].initial, expectedInitial[0]),
            "round-tripped mapping keeps macro routes and initial values");

    // Unconfigured OSCpar state (no Prefix) is treated like an unrecognised
    // chunk, not adopted.
    const juce::String unconfiguredPayload =
        R"xml(<Preset Prefix="" Address="0.0.0.0" Port="3030"></Preset>)xml";
    auto unconfiguredXml = juce::XmlDocument::parse (unconfiguredPayload);
    oscmacro::OscParAdoption unconfiguredAdoption;
    expect (unconfiguredXml != nullptr
                && oscmacro::adoptOscParPreset (*unconfiguredXml, unconfiguredPayload,
                                               unconfiguredAdoption).failed(),
            "an OSCpar preset with an empty Prefix is not adopted");

    // Regression guard: an OSCMacro-native chunk (root type "ChromatikMacro",
    // matching AudioProcessorValueTreeState's state tree type in
    // PluginProcessor.cpp) must still be recognised as NOT an OSCpar preset,
    // so setStateInformation's existing native-state path is untouched.
    const juce::String nativePayload =
        R"xml(<ChromatikMacro instanceIdentity="a3f2c19d" instanceName="MoonOut"></ChromatikMacro>)xml";
    auto nativeXml = juce::XmlDocument::parse (nativePayload);
    expect (nativeXml != nullptr, "native-format sample payload parses as XML");
    expect (nativeXml != nullptr && ! oscmacro::isOscParPreset (*nativeXml),
            "a native OSCMacro state chunk is not misidentified as an OSCpar preset");

    oscmacro::OscParAdoption nativeAdoption;
    expect (nativeXml != nullptr
                && oscmacro::adoptOscParPreset (*nativeXml, nativePayload, nativeAdoption).failed(),
            "adoptOscParPreset refuses a native OSCMacro chunk");

    if (failures == 0)
        std::cout << "All OSCpar adoption tests passed\n";

    return failures == 0 ? 0 : 1;
}
