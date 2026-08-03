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

bool isObjectJson (const juce::String& json)
{
    juce::var value;
    return juce::JSON::parse (json, value).wasOk() && value.getDynamicObject() != nullptr;
}
}

int main()
{
    const juce::String identity = "a3f2c19d";
    const juce::String validJson = R"json(
        {
          "a3f2c19d": {
            "name": "MoonOut",
            "prefix": "lx/mixer/channel/NightChorus/modulation/Moon/",
            "target": { "host": "127.0.0.1", "port": 3030 },
            "macros": {
              "1": { "scale": [0, 1], "initial": 0.25 },
              "2": { "scale": [-1, 1], "resetOnLoad": false }
            }
          }
        })json";

    chromatik::Mapping mapping;
    juce::String resolved;
    auto result = chromatik::parseMappingsFile (validJson, identity, mapping, resolved);

    expect (result.wasOk(), "valid mapping parses");
    expect (mapping.identity == identity, "UUID identity is retained");
    expect (mapping.prefix == "/lx/mixer/channel/NightChorus/modulation/Moon",
            "prefix is normalized");
    expect (mapping.addressFor (1).endsWith ("/macro2"), "macro address is formed");
    expect (mapping.macros[0].enabled && mapping.macros[1].enabled,
            "configured macros are enabled");
    expect (! mapping.macros[2].enabled, "unconfigured macros are disabled");
    expect (std::abs (mapping.macros[1].minimum + 1.0f) < 0.0001f,
            "scale minimum parses");
    expect (std::abs (mapping.macros[0].initial - 0.25f) < 0.0001f,
            "initial value parses");
    expect (mapping.macros[0].resetOnLoad,
            "reset-on-load defaults to true");
    expect (! mapping.macros[1].resetOnLoad,
            "reset-on-load can be opted out");

    chromatik::Mapping cached;
    expect (chromatik::parseResolvedMapping (resolved, identity, cached).wasOk(),
            "resolved cache parses");
    expect (cached.prefix == mapping.prefix, "cached mapping matches source mapping");
    expect (cached.identity == identity, "cached mapping retains supplied identity");
    expect (chromatik::parseResolvedMapping (resolved, {}, cached).failed(),
            "cached mapping requires an identity");

    chromatik::Mapping missing;
    juce::String ignored;
    expect (chromatik::parseMappingsFile (validJson, "different-id", missing, ignored).failed(),
            "missing identity key fails");
    expect (chromatik::parseMappingsFile (validJson, {}, missing, ignored).failed(),
            "missing identity argument fails");

    const auto unrelatedMalformed = validJson.replace (
        "}\n        }", "},\n          \"broken-instance\": 1\n        }");
    expect (chromatik::parseMappingsFile (unrelatedMalformed, identity, mapping, resolved).wasOk(),
            "a malformed unrelated entry does not disable a valid identity");

    const auto invalidScale = validJson.replace ("[-1, 1]", "[0]");
    expect (chromatik::parseMappingsFile (invalidScale, identity, missing, ignored).failed(),
            "invalid scale fails");
    const auto nonNumericScale = validJson.replace ("[-1, 1]", "[\"low\", 1]");
    expect (chromatik::parseMappingsFile (nonNumericScale, identity, missing, ignored).failed(),
            "non-numeric scale fails");

    const juce::String defaultsJson = R"json(
        { "defaults": { "prefix": "/test", "macros": { "1": {} } } })json";
    expect (chromatik::parseMappingsFile (defaultsJson, "defaults", mapping, resolved).wasOk(),
            "default target and scale parse");
    expect (mapping.host == "127.0.0.1" && mapping.port == 3030,
            "target defaults are applied");
    expect (mapping.macros[0].initial == 0.0f && mapping.macros[0].resetOnLoad,
            "macro reset defaults apply");

    const juce::String unconfiguredJson = R"json(
        { "new-instance": { "name": "(unnamed)", "prefix": "" } })json";
    expect (chromatik::parseMappingsFile (unconfiguredJson, "new-instance", mapping, resolved).wasOk(),
            "empty self-registered entry parses");
    expect (mapping.prefix.isEmpty() && ! mapping.macros[0].enabled,
            "unconfigured entry emits no macro routes");
    expect (mapping.addressFor (0).isEmpty(), "empty prefix has no OSC address");

    const auto malformedInitial = validJson.replace ("\"initial\": 0.25", "\"initial\": \"zero\"");
    expect (chromatik::parseMappingsFile (malformedInitial, identity, missing, ignored).failed(),
            "non-numeric initial fails");

    const auto outOfRangeInitial = validJson.replace ("\"initial\": 0.25", "\"initial\": 1.1");
    expect (chromatik::parseMappingsFile (outOfRangeInitial, identity, missing, ignored).failed(),
            "out-of-range initial fails");

    const auto malformedBoolean = validJson.replace ("\"resetOnLoad\": false", "\"resetOnLoad\": 0");
    expect (chromatik::parseMappingsFile (malformedBoolean, identity, missing, ignored).failed(),
            "non-boolean reset-on-load fails");

    const juce::String existingJson = R"json(
        {
          "a3f2c19d": {
            "name": "Before",
            "prefix": "/existing",
            "target": { "host": "10.0.0.4", "port": 4000 },
            "macros": { "1": { "scale": [-1, 1], "initial": 0.4, "resetOnLoad": false } }
          },
          "other-instance": { "name": "Other", "prefix": "/other", "macros": { "2": {} } }
        })json";
    juce::String upserted;
    expect (chromatik::upsertMappingName (existingJson, identity, "After", "{}", upserted).wasOk(),
            "existing entry name upsert succeeds");
    expect (chromatik::parseMappingsFile (upserted, identity, mapping, resolved).wasOk(),
            "upserted existing entry remains parseable");
    expect (mapping.name == "After" && mapping.prefix == "/existing",
            "existing entry changes only its name");
    expect (mapping.host == "10.0.0.4" && mapping.port == 4000
            && ! mapping.macros[0].resetOnLoad
            && std::abs (mapping.macros[0].initial - 0.4f) < 0.0001f,
            "existing route fields are preserved");
    expect (chromatik::parseMappingsFile (upserted, "other-instance", mapping, resolved).wasOk()
            && mapping.name == "Other" && mapping.prefix == "/other",
            "unrelated entries are preserved");

    const juce::String fallback = R"json(
        { "name": "Cached", "prefix": "/cached", "macros": { "3": { "initial": 0.6 } } })json";
    expect (chromatik::upsertMappingName ("{}", "from-cache", "Restored", fallback, upserted).wasOk(),
            "missing entry inserts valid fallback");
    expect (chromatik::parseMappingsFile (upserted, "from-cache", mapping, resolved).wasOk()
            && mapping.name == "Restored" && mapping.prefix == "/cached"
            && mapping.macros[2].enabled
            && std::abs (mapping.macros[2].initial - 0.6f) < 0.0001f,
            "fallback fields survive registration while its name is updated");

    expect (chromatik::upsertMappingName (juce::String(), "fresh", "(unnamed)",
                                          juce::String(), upserted).wasOk(),
            "empty file creates a fresh unconfigured entry");
    expect (chromatik::parseMappingsFile (upserted, "fresh", mapping, resolved).wasOk()
            && mapping.name == "(unnamed)" && mapping.prefix.isEmpty()
            && ! mapping.macros[0].enabled,
            "fresh entry has an empty prefix and no routes");

    expect (chromatik::upsertMappingName ("[]", "fresh", "Name", juce::String(), upserted).failed(),
            "non-object root is rejected");
    expect (chromatik::upsertMappingName (R"json({ "fresh": 1 })json", "fresh", "Name",
                                          juce::String(), upserted).failed(),
            "malformed entry is rejected");
    expect (chromatik::upsertMappingName ("{}", "fresh", "Name", "[]", upserted).failed(),
            "non-object fallback is rejected");
    expect (chromatik::upsertMappingName ("{}", "fresh", "Name",
                                          R"json({ "prefix": "/bad", "macros": { "1": { "initial": "bad" } } })json",
                                          upserted).failed(),
            "malformed fallback route is rejected");

    expect (isObjectJson (upserted), "upsert returns complete JSON");

    if (failures == 0)
        std::cout << "All mapping config tests passed\n";

    return failures == 0 ? 0 : 1;
}
