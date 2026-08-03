#include "MappingConfig.h"

#include <cmath>

namespace chromatik
{
namespace
{
bool isNumber (const juce::var& value)
{
    return value.isInt() || value.isInt64() || value.isDouble();
}

juce::Result parseFiniteNumber (const juce::var& value,
                                const juce::String& fieldName,
                                float& destination)
{
    if (! isNumber (value))
        return juce::Result::fail (fieldName + " must be a number");

    const auto number = static_cast<float> (value);

    if (! std::isfinite (number))
        return juce::Result::fail (fieldName + " must be finite");

    destination = number;
    return juce::Result::ok();
}

juce::Result parseMappingObject (const juce::var& value,
                                 const juce::String& identity,
                                 Mapping& mapping)
{
    const auto* object = value.getDynamicObject();

    if (object == nullptr)
        return juce::Result::fail ("Mapping must be a JSON object");

    const auto normalizedIdentity = identity.trim();

    if (normalizedIdentity.isEmpty())
        return juce::Result::fail ("Mapping identity is required");

    Mapping candidate;
    candidate.identity = normalizedIdentity;

    if (object->hasProperty ("name"))
    {
        const auto name = object->getProperty ("name");

        if (! name.isString())
            return juce::Result::fail ("Mapping name must be a string");

        candidate.name = name.toString();
    }

    if (object->hasProperty ("prefix"))
    {
        const auto prefix = object->getProperty ("prefix");

        if (! prefix.isString())
            return juce::Result::fail ("Mapping prefix must be a string");

        candidate.prefix = prefix.toString().trim();
    }

    if (candidate.prefix.isNotEmpty() && ! candidate.prefix.startsWithChar ('/'))
        candidate.prefix = "/" + candidate.prefix;

    while (candidate.prefix.length() > 1 && candidate.prefix.endsWithChar ('/'))
        candidate.prefix = candidate.prefix.dropLastCharacters (1);

    if (object->hasProperty ("target"))
    {
        const auto* target = object->getProperty ("target").getDynamicObject();

        if (target == nullptr)
            return juce::Result::fail ("Mapping target must be a JSON object");

        if (target->hasProperty ("host"))
        {
            const auto host = target->getProperty ("host");

            if (! host.isString())
                return juce::Result::fail ("Target host must be a string");

            const auto configuredHost = host.toString().trim();

            if (configuredHost.isNotEmpty())
                candidate.host = configuredHost;
        }

        if (target->hasProperty ("port"))
        {
            const auto port = target->getProperty ("port");

            if (! isNumber (port))
                return juce::Result::fail ("Target port must be a number");

            const auto configuredPort = static_cast<double> (port);

            if (! std::isfinite (configuredPort)
                || std::floor (configuredPort) != configuredPort)
                return juce::Result::fail ("Target port must be an integer");

            candidate.port = static_cast<int> (configuredPort);
        }
    }

    if (candidate.port < 1 || candidate.port > 65535)
        return juce::Result::fail ("Target port must be between 1 and 65535");

    if (object->hasProperty ("macros"))
    {
        const auto* macros = object->getProperty ("macros").getDynamicObject();

        if (macros == nullptr)
            return juce::Result::fail ("Mapping macros must be a JSON object");

        for (auto index = 0; index < macroCount; ++index)
        {
            const auto routeValue = macros->getProperty (juce::Identifier (juce::String (index + 1)));

            if (routeValue.isVoid())
                continue;

            const auto* route = routeValue.getDynamicObject();

            if (route == nullptr)
                return juce::Result::fail ("Macro route must be a JSON object");

            MacroRoute macro;
            macro.enabled = true;

            if (route->hasProperty ("scale"))
            {
                const auto scaleValue = route->getProperty ("scale");
                const auto* scale = scaleValue.getArray();

                if (scale == nullptr || scale->size() != 2)
                    return juce::Result::fail ("Macro scale must contain exactly two numbers");

                if (const auto result = parseFiniteNumber (scale->getReference (0),
                                                           "Macro scale value",
                                                           macro.minimum);
                    result.failed())
                    return result;

                if (const auto result = parseFiniteNumber (scale->getReference (1),
                                                           "Macro scale value",
                                                           macro.maximum);
                    result.failed())
                    return result;
            }

            if (route->hasProperty ("initial"))
                if (const auto result = parseFiniteNumber (route->getProperty ("initial"),
                                                           "Macro initial value",
                                                           macro.initial);
                    result.failed())
                    return result;

            if (route->hasProperty ("resetOnLoad"))
            {
                const auto resetOnLoad = route->getProperty ("resetOnLoad");

                if (! resetOnLoad.isBool())
                    return juce::Result::fail ("Macro resetOnLoad must be a boolean");

                macro.resetOnLoad = static_cast<bool> (resetOnLoad);
            }

            candidate.macros[static_cast<size_t> (index)] = macro;
        }
    }

    mapping = std::move (candidate);
    return juce::Result::ok();
}
}

juce::String Mapping::addressFor (int macroIndex) const
{
    if (prefix.isEmpty())
        return {};

    if (prefix == "/")
        return "/macro" + juce::String (macroIndex + 1);

    return prefix + "/macro" + juce::String (macroIndex + 1);
}

juce::Result parseMappingsFile (const juce::String& json,
                                const juce::String& identity,
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

    const auto normalizedIdentity = identity.trim();

    if (normalizedIdentity.isEmpty())
        return juce::Result::fail ("Mapping identity is required");

    const auto value = object->getProperty (juce::Identifier (normalizedIdentity));

    if (value.isVoid())
        return juce::Result::fail ("No mapping exists for identity " + normalizedIdentity);

    if (const auto parsed = parseMappingObject (value, normalizedIdentity, mapping); parsed.failed())
        return parsed;

    resolvedJson = juce::JSON::toString (value, true);
    return juce::Result::ok();
}

juce::Result parseResolvedMapping (const juce::String& json,
                                   const juce::String& identity,
                                   Mapping& mapping)
{
    juce::var value;
    const auto result = juce::JSON::parse (json, value);

    if (result.failed())
        return result;

    return parseMappingObject (value, identity, mapping);
}
}
