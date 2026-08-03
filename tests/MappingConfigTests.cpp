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
}

int main()
{
    const juce::String validJson = R"json(
        {
          "7": {
            "name": "MoonOut",
            "prefix": "lx/mixer/channel/NightChorus/modulation/Moon/",
            "target": { "host": "127.0.0.1", "port": 3030 },
            "macros": {
              "1": { "scale": [0, 1] },
              "2": { "scale": [-1, 1] }
            }
          }
        })json";

    chromatik::Mapping mapping;
    juce::String resolved;
    auto result = chromatik::parseMappingsFile (validJson, 7, mapping, resolved);

    expect (result.wasOk(), "valid mapping parses");
    expect (mapping.slot == 7, "slot is retained");
    expect (mapping.prefix == "/lx/mixer/channel/NightChorus/modulation/Moon",
            "prefix is normalized");
    expect (mapping.addressFor (1).endsWith ("/macro2"), "macro address is formed");
    expect (mapping.macros[0].enabled && mapping.macros[1].enabled,
            "configured macros are enabled");
    expect (! mapping.macros[2].enabled, "unconfigured macros are disabled");
    expect (std::abs (mapping.macros[1].minimum + 1.0f) < 0.0001f,
            "scale minimum parses");

    chromatik::Mapping cached;
    expect (chromatik::parseResolvedMapping (resolved, 7, cached).wasOk(),
            "resolved cache parses");
    expect (cached.prefix == mapping.prefix, "cached mapping matches source mapping");

    chromatik::Mapping missing;
    juce::String ignored;
    expect (chromatik::parseMappingsFile (validJson, 8, missing, ignored).failed(),
            "missing slot fails");

    const auto invalidScale = validJson.replace ("[-1, 1]", "[0]");
    expect (chromatik::parseMappingsFile (invalidScale, 7, missing, ignored).failed(),
            "invalid scale fails");

    const juce::String defaultsJson = R"json(
        { "1": { "prefix": "/test", "macros": { "1": {} } } })json";
    expect (chromatik::parseMappingsFile (defaultsJson, 1, mapping, resolved).wasOk(),
            "default target and scale parse");
    expect (mapping.host == "127.0.0.1" && mapping.port == 3030,
            "target defaults are applied");

    if (failures == 0)
        std::cout << "All mapping config tests passed\n";

    return failures == 0 ? 0 : 1;
}
