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

    oscmacro::Mapping mapping;
    juce::String resolved;
    auto result = oscmacro::parseMappingsFile (validJson, identity, mapping, resolved);

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

    oscmacro::Mapping cached;
    expect (oscmacro::parseResolvedMapping (resolved, identity, cached).wasOk(),
            "resolved cache parses");
    expect (cached.prefix == mapping.prefix, "cached mapping matches source mapping");
    expect (cached.identity == identity, "cached mapping retains supplied identity");
    expect (oscmacro::parseResolvedMapping (resolved, {}, cached).failed(),
            "cached mapping requires an identity");

    oscmacro::Mapping missing;
    juce::String ignored;
    expect (oscmacro::parseMappingsFile (validJson, "different-id", missing, ignored).failed(),
            "missing identity key fails");
    expect (oscmacro::parseMappingsFile (validJson, {}, missing, ignored).failed(),
            "missing identity argument fails");

    const auto unrelatedMalformed = validJson.replace (
        "}\n        }", "},\n          \"broken-instance\": 1\n        }");
    expect (oscmacro::parseMappingsFile (unrelatedMalformed, identity, mapping, resolved).wasOk(),
            "a malformed unrelated entry does not disable a valid identity");

    const auto invalidScale = validJson.replace ("[-1, 1]", "[0]");
    expect (oscmacro::parseMappingsFile (invalidScale, identity, missing, ignored).failed(),
            "invalid scale fails");
    const auto nonNumericScale = validJson.replace ("[-1, 1]", "[\"low\", 1]");
    expect (oscmacro::parseMappingsFile (nonNumericScale, identity, missing, ignored).failed(),
            "non-numeric scale fails");

    const juce::String defaultsJson = R"json(
        { "defaults": { "prefix": "/test", "macros": { "1": {} } } })json";
    expect (oscmacro::parseMappingsFile (defaultsJson, "defaults", mapping, resolved).wasOk(),
            "default target and scale parse");
    expect (mapping.host == "127.0.0.1" && mapping.port == 3030,
            "target defaults are applied");
    expect (mapping.macros[0].initial == 0.0f && mapping.macros[0].resetOnLoad,
            "macro reset defaults apply");

    const juce::String unconfiguredJson = R"json(
        { "new-instance": { "name": "(unnamed)", "prefix": "" } })json";
    expect (oscmacro::parseMappingsFile (unconfiguredJson, "new-instance", mapping, resolved).wasOk(),
            "empty self-registered entry parses");
    expect (mapping.prefix.isEmpty() && ! mapping.macros[0].enabled,
            "unconfigured entry emits no macro routes");
    expect (mapping.addressFor (0).isEmpty(), "empty prefix has no OSC address");

    const auto malformedInitial = validJson.replace ("\"initial\": 0.25", "\"initial\": \"zero\"");
    expect (oscmacro::parseMappingsFile (malformedInitial, identity, missing, ignored).failed(),
            "non-numeric initial fails");

    const auto outOfRangeInitial = validJson.replace ("\"initial\": 0.25", "\"initial\": 1.1");
    expect (oscmacro::parseMappingsFile (outOfRangeInitial, identity, missing, ignored).failed(),
            "out-of-range initial fails");

    const auto malformedBoolean = validJson.replace ("\"resetOnLoad\": false", "\"resetOnLoad\": 0");
    expect (oscmacro::parseMappingsFile (malformedBoolean, identity, missing, ignored).failed(),
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
    expect (oscmacro::upsertMappingName (existingJson, identity, "After", "{}", upserted).wasOk(),
            "existing entry name upsert succeeds");
    expect (oscmacro::parseMappingsFile (upserted, identity, mapping, resolved).wasOk(),
            "upserted existing entry remains parseable");
    expect (mapping.name == "After" && mapping.prefix == "/existing",
            "existing entry changes only its name");
    expect (mapping.host == "10.0.0.4" && mapping.port == 4000
            && ! mapping.macros[0].resetOnLoad
            && std::abs (mapping.macros[0].initial - 0.4f) < 0.0001f,
            "existing route fields are preserved");
    expect (oscmacro::parseMappingsFile (upserted, "other-instance", mapping, resolved).wasOk()
            && mapping.name == "Other" && mapping.prefix == "/other",
            "unrelated entries are preserved");

    const juce::String fallback = R"json(
        { "name": "Cached", "prefix": "/cached", "macros": { "3": { "initial": 0.6 } } })json";
    expect (oscmacro::upsertMappingName ("{}", "from-cache", "Restored", fallback, upserted).wasOk(),
            "missing entry inserts valid fallback");
    expect (oscmacro::parseMappingsFile (upserted, "from-cache", mapping, resolved).wasOk()
            && mapping.name == "Restored" && mapping.prefix == "/cached"
            && mapping.macros[2].enabled
            && std::abs (mapping.macros[2].initial - 0.6f) < 0.0001f,
            "fallback fields survive registration while its name is updated");

    expect (oscmacro::upsertMappingName (juce::String(), "fresh", "(unnamed)",
                                          juce::String(), upserted).wasOk(),
            "empty file creates a fresh unconfigured entry");
    expect (oscmacro::parseMappingsFile (upserted, "fresh", mapping, resolved).wasOk()
            && mapping.name == "(unnamed)" && mapping.prefix.isEmpty()
            && ! mapping.macros[0].enabled,
            "fresh entry has an empty prefix and no routes");

    expect (oscmacro::upsertMappingName ("[]", "fresh", "Name", juce::String(), upserted).failed(),
            "non-object root is rejected");
    expect (oscmacro::upsertMappingName (R"json({ "fresh": 1 })json", "fresh", "Name",
                                          juce::String(), upserted).failed(),
            "malformed entry is rejected");
    expect (oscmacro::upsertMappingName ("{}", "fresh", "Name", "[]", upserted).failed(),
            "non-object fallback is rejected");
    expect (oscmacro::upsertMappingName ("{}", "fresh", "Name",
                                          R"json({ "prefix": "/bad", "macros": { "1": { "initial": "bad" } } })json",
                                          upserted).failed(),
            "malformed fallback route is rejected");

    expect (isObjectJson (upserted), "upsert returns complete JSON");

    if (failures == 0)
        std::cout << "All mapping config tests passed\n";


    // ---- customPath: arbitrary OSC path per knob ----
    {
        const juce::String id = "custompath";
        const juce::String json = R"json(
            {
              "custompath": {
                "name": "ClockMacro",
                "prefix": "/lx/modulation/Tempo-Tap",
                "target": { "host": "127.0.0.1", "port": 3030 },
                "macros": {
                  "1": { "scale": [0, 1] },
                  "3": { "scale": [0, 1], "customPath": true, "path": "beat" },
                  "4": { "scale": [0, 1], "customPath": true, "path": "/leading" },
                  "5": { "scale": [0, 1], "path": "parked" }
                }
              }
            })json";

        oscmacro::Mapping m;
        juce::String resolvedJson;
        expect (oscmacro::parseMappingsFile (json, id, m, resolvedJson).wasOk(),
                "customPath mapping parses");
        expect (m.addressFor (0) == "/lx/modulation/Tempo-Tap/macro1",
                "macroN remains the default suffix");
        expect (m.addressFor (2) == "/lx/modulation/Tempo-Tap/beat",
                "customPath replaces the macroN suffix");
        expect (m.addressFor (3) == "/lx/modulation/Tempo-Tap/leading",
                "a leading slash on the path is not doubled");
        expect (! m.macros[4].customPath && m.addressFor (4) == "/lx/modulation/Tempo-Tap/macro5",
                "a path with customPath false is parked, not applied");

        // The toggle must survive self-registration, or a configured path silently reverts
        // to macroN and sends to a live address that looks configured and is not.
        oscmacro::Mapping roundTripped;
        expect (oscmacro::parseResolvedMapping (resolvedJson, id, roundTripped).wasOk(),
                "resolved customPath mapping re-parses");
        expect (roundTripped.addressFor (2) == "/lx/modulation/Tempo-Tap/beat",
                "customPath round-trips through serialization");

        const juce::String rootPrefix = R"json(
            {
              "custompath": {
                "prefix": "/",
                "macros": { "1": { "customPath": true, "path": "only" } }
              }
            })json";
        oscmacro::Mapping rootMapping;
        juce::String rootResolved;
        expect (oscmacro::parseMappingsFile (rootPrefix, id, rootMapping, rootResolved).wasOk(),
                "root prefix with customPath parses");
        expect (rootMapping.addressFor (0) == "/only",
                "root prefix does not double the separator");

        const juce::String emptyPath = R"json(
            {
              "custompath": {
                "prefix": "/lx",
                "macros": { "1": { "customPath": true } }
              }
            })json";
        oscmacro::Mapping rejected;
        juce::String ignoredResolved;
        expect (oscmacro::parseMappingsFile (emptyPath, id, rejected, ignoredResolved).failed(),
                "customPath without a path is rejected rather than falling back");

        const juce::String badToggle = R"json(
            {
              "custompath": {
                "prefix": "/lx",
                "macros": { "1": { "customPath": "yes", "path": "x" } }
              }
            })json";
        expect (oscmacro::parseMappingsFile (badToggle, id, rejected, ignoredResolved).failed(),
                "non-boolean customPath is rejected");

        const juce::String badPath = R"json(
            {
              "custompath": {
                "prefix": "/lx",
                "macros": { "1": { "customPath": true, "path": 3 } }
              }
            })json";
        expect (oscmacro::parseMappingsFile (badPath, id, rejected, ignoredResolved).failed(),
                "non-string path is rejected");
    }

    return failures == 0 ? 0 : 1;
}
