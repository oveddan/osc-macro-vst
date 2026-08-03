#include "MappingConfig.h"

#include <cmath>

namespace chromatik
{
namespace
{
juce::Result parseMappingObject (const juce::var& value, int slot, Mapping& mapping)
{
    const auto* object = value.getDynamicObject();

    if (object == nullptr)
        return juce::Result::fail ("Mapping must be a JSON object");

    Mapping candidate;
    candidate.slot = slot;
    candidate.name = object->getProperty ("name").toString();
    candidate.prefix = object->getProperty ("prefix").toString().trim();

    if (candidate.prefix.isEmpty())
        return juce::Result::fail ("Mapping prefix is required");

    if (! candidate.prefix.startsWithChar ('/'))
        candidate.prefix = "/" + candidate.prefix;

    while (candidate.prefix.length() > 1 && candidate.prefix.endsWithChar ('/'))
        candidate.prefix = candidate.prefix.dropLastCharacters (1);

    if (const auto* target = object->getProperty ("target").getDynamicObject())
    {
        const auto configuredHost = target->getProperty ("host").toString().trim();
        const auto configuredPort = static_cast<int> (target->getProperty ("port"));

        if (configuredHost.isNotEmpty())
            candidate.host = configuredHost;

        if (configuredPort != 0)
            candidate.port = configuredPort;
    }

    if (candidate.port < 1 || candidate.port > 65535)
        return juce::Result::fail ("Target port must be between 1 and 65535");

    const auto* macros = object->getProperty ("macros").getDynamicObject();

    if (macros == nullptr)
        return juce::Result::fail ("Mapping macros must be a JSON object");

    auto enabledCount = 0;

    for (auto index = 0; index < macroCount; ++index)
    {
        const auto routeValue = macros->getProperty (juce::Identifier (juce::String (index + 1)));
        const auto* route = routeValue.getDynamicObject();

        if (route == nullptr)
            continue;

        auto minimum = 0.0f;
        auto maximum = 1.0f;
        const auto scaleValue = route->getProperty ("scale");

        if (const auto* scale = scaleValue.getArray())
        {
            if (scale->size() != 2)
                return juce::Result::fail ("Macro scale must contain exactly two numbers");

            minimum = static_cast<float> (scale->getReference (0));
            maximum = static_cast<float> (scale->getReference (1));
        }

        if (! std::isfinite (minimum) || ! std::isfinite (maximum))
            return juce::Result::fail ("Macro scale values must be finite");

        candidate.macros[static_cast<size_t> (index)] = { true, minimum, maximum };
        ++enabledCount;
    }

    if (enabledCount == 0)
        return juce::Result::fail ("Mapping must enable at least one macro");

    mapping = std::move (candidate);
    return juce::Result::ok();
}
}

juce::String Mapping::addressFor (int macroIndex) const
{
    return prefix + "/macro" + juce::String (macroIndex + 1);
}

juce::Result parseMappingsFile (const juce::String& json,
                                int slot,
                                Mapping& mapping,
                                juce::String& resolvedJson)
{
    juce::var root;
    const auto result = juce::JSON::parse (json, root);

    if (result.failed())
        return result;

    const auto* object = root.getDynamicObject();

    if (object == nullptr)
        return juce::Result::fail ("Mappings file must contain a JSON object");

    const auto value = object->getProperty (juce::Identifier (juce::String (slot)));

    if (value.isVoid())
        return juce::Result::fail ("No mapping exists for slot " + juce::String (slot));

    if (const auto parsed = parseMappingObject (value, slot, mapping); parsed.failed())
        return parsed;

    resolvedJson = juce::JSON::toString (value, true);
    return juce::Result::ok();
}

juce::Result parseResolvedMapping (const juce::String& json,
                                   int slot,
                                   Mapping& mapping)
{
    juce::var value;
    const auto result = juce::JSON::parse (json, value);

    if (result.failed())
        return result;

    return parseMappingObject (value, slot, mapping);
}
}
