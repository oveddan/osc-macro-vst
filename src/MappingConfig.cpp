#include "MappingConfig.h"

#include <juce_cryptography/juce_cryptography.h>

#include <cmath>

namespace oscmacro
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

            if (configuredPort < 1.0 || configuredPort > 65535.0)
                return juce::Result::fail ("Target port must be between 1 and 65535");

            candidate.port = static_cast<int> (configuredPort);
        }
    }

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

            if (macro.initial < 0.0f || macro.initial > 1.0f)
                return juce::Result::fail ("Macro initial value must be between 0 and 1");

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

juce::Result validateMappingsRoot (const juce::var& root)
{
    const auto* object = root.getDynamicObject();

    if (object == nullptr)
        return juce::Result::fail ("Mappings file must contain a JSON object");

    for (const auto& property : object->getProperties())
    {
        Mapping ignored;

        if (const auto result = parseMappingObject (property.value,
                                                     property.name.toString(),
                                                     ignored);
            result.failed())
            return juce::Result::fail ("Invalid mapping " + property.name.toString()
                                       + ": " + result.getErrorMessage());
    }

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

juce::Result upsertMappingName (const juce::String& currentJson,
                                const juce::String& identity,
                                const juce::String& name,
                                const juce::String& fallbackResolvedJson,
                                juce::String& updatedJson)
{
    const auto normalizedIdentity = identity.trim();

    if (normalizedIdentity.isEmpty())
        return juce::Result::fail ("Mapping identity is required");

    juce::var root;

    if (currentJson.trim().isEmpty())
        root = juce::var (new juce::DynamicObject());
    else if (const auto result = juce::JSON::parse (currentJson, root); result.failed())
        return result;

    if (const auto rootResult = validateMappingsRoot (root); rootResult.failed())
        return rootResult;

    auto* object = root.getDynamicObject();
    const auto key = juce::Identifier (normalizedIdentity);
    auto value = object->getProperty (key);

    if (value.isVoid())
    {
        if (fallbackResolvedJson.trim().isNotEmpty())
        {
            juce::var fallback;

            if (const auto result = juce::JSON::parse (fallbackResolvedJson, fallback); result.failed())
                return result;

            Mapping ignored;

            if (const auto result = parseMappingObject (fallback, normalizedIdentity, ignored);
                result.failed())
                return juce::Result::fail ("Invalid fallback mapping: " + result.getErrorMessage());

            value = fallback;
        }
        else
        {
            auto* fresh = new juce::DynamicObject();
            fresh->setProperty ("prefix", "");
            value = juce::var (fresh);
        }

        object->setProperty (key, value);
    }

    auto* entry = value.getDynamicObject();

    if (entry == nullptr)
        return juce::Result::fail ("Mapping entry must be a JSON object");

    entry->setProperty ("name", name);
    updatedJson = juce::JSON::toString (root, true);
    return juce::Result::ok();
}

namespace
{
// Path segments between "channel" and the macro bank tend to look like
// ".../channel/<Track>/modulation/<Bank>". Prefer <Track>/<Bank> over the
// literal last two segments ("modulation/<Bank>") when a "modulation"
// segment is present with something before it; otherwise fall back to the
// literal last two segments (e.g. a prefix that is just "/modulation/Globals").
juce::String deriveNameFromPrefix (const juce::String& prefix)
{
    auto withoutLeadingSlash = prefix.startsWithChar ('/') ? prefix.substring (1) : prefix;

    juce::StringArray segments;
    segments.addTokens (withoutLeadingSlash, "/", "");
    segments.removeEmptyStrings();

    if (segments.isEmpty())
        return "(unnamed)";

    if (segments.size() == 1)
        return segments[0];

    const auto modulationIndex = segments.indexOf ("modulation");

    if (modulationIndex > 0)
        return segments[modulationIndex - 1] + "/" + segments[segments.size() - 1];

    return segments[segments.size() - 2] + "/" + segments[segments.size() - 1];
}

juce::String mappingToResolvedJson (const Mapping& mapping)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("name", mapping.name);
    root->setProperty ("prefix", mapping.prefix);

    auto* target = new juce::DynamicObject();
    target->setProperty ("host", mapping.host);
    target->setProperty ("port", mapping.port);
    root->setProperty ("target", juce::var (target));

    auto* macros = new juce::DynamicObject();

    for (auto index = 0; index < macroCount; ++index)
    {
        const auto& route = mapping.macros[static_cast<size_t> (index)];

        if (! route.enabled)
            continue;

        auto* routeObject = new juce::DynamicObject();
        juce::Array<juce::var> scale;
        scale.add (route.minimum);
        scale.add (route.maximum);
        routeObject->setProperty ("scale", scale);
        routeObject->setProperty ("initial", route.initial);
        routeObject->setProperty ("resetOnLoad", route.resetOnLoad);
        macros->setProperty (juce::Identifier (juce::String (index + 1)), juce::var (routeObject));
    }

    root->setProperty ("macros", juce::var (macros));
    return juce::JSON::toString (juce::var (root), true);
}
}

bool isOscParPreset (const juce::XmlElement& root)
{
    return root.hasTagName ("Preset");
}

juce::Result adoptOscParPreset (const juce::XmlElement& root,
                                const juce::String& fullPayloadText,
                                OscParAdoption& result)
{
    if (! isOscParPreset (root))
        return juce::Result::fail ("Not an OSCpar preset payload");

    const auto prefixRaw = root.getStringAttribute ("Prefix").trim();

    if (prefixRaw.isEmpty())
        return juce::Result::fail ("OSCpar preset has no Prefix (unconfigured)");

    // OSCpar stores the prefix without a leading slash; OSCMacro's config
    // requires one.
    const auto prefix = prefixRaw.startsWithChar ('/') ? prefixRaw : "/" + prefixRaw;

    // "0.0.0.0" is OSCpar's bind-address placeholder, not a valid OSC
    // destination - every real instance in the show project stores it, so it
    // has to be mapped to something sendable.
    const auto addressRaw = root.getStringAttribute ("Address").trim();
    const auto host = (addressRaw.isEmpty() || addressRaw == "0.0.0.0")
                          ? juce::String ("127.0.0.1")
                          : addressRaw;

    auto port = 3030;
    const auto portRaw = root.getStringAttribute ("Port").trim();

    if (portRaw.isNotEmpty() && portRaw.containsOnly ("0123456789"))
    {
        const auto parsedPort = portRaw.getIntValue();

        if (parsedPort >= 1 && parsedPort <= 65535)
            port = parsedPort;
    }

    Mapping mapping;
    mapping.identity = juce::MD5 (fullPayloadText.toUTF8()).toHexString();
    mapping.name = deriveNameFromPrefix (prefix);
    mapping.prefix = prefix;
    mapping.host = host;
    mapping.port = port;

    // OSCpar has 10 macros (<Macros><Macro Name=".." ScaleMin=".." ScaleMax=".."/>...);
    // OSCMacro has 8. Adopt the first 8 by POSITION - real payloads mix
    // "macro1".."macro8" with "Macro9"/"Macro10" casing, so name-matching is
    // not reliable, and Macro9/Macro10 are dropped entirely regardless.
    if (auto* macrosElement = root.getChildByName ("Macros"))
    {
        auto index = 0;

        for (auto* macroElement : macrosElement->getChildWithTagNameIterator ("Macro"))
        {
            if (index >= macroCount)
                break;

            auto& route = mapping.macros[static_cast<size_t> (index)];
            route.enabled = true;
            route.minimum = static_cast<float> (macroElement->getDoubleAttribute ("ScaleMin", 0.0));
            route.maximum = static_cast<float> (macroElement->getDoubleAttribute ("ScaleMax", 1.0));
            ++index;
        }
    }

    // <PARAM id="MacroN" value="..."/> entries carry each macro's last value,
    // used as the route's initial. Their ids are capitalised ("Macro1") while
    // <Macro Name=...> is usually lowercase ("macro1") - match case-insensitively
    // and do not assume the two lists share an order or casing convention.
    for (auto index = 0; index < macroCount; ++index)
    {
        const auto paramId = "Macro" + juce::String (index + 1);

        for (auto* paramElement : root.getChildWithTagNameIterator ("PARAM"))
        {
            if (! paramElement->getStringAttribute ("id").equalsIgnoreCase (paramId))
                continue;

            const auto value = static_cast<float> (paramElement->getDoubleAttribute ("value", 0.0));
            mapping.macros[static_cast<size_t> (index)].initial = juce::jlimit (0.0f, 1.0f, value);
            break;
        }
    }

    result.identity = mapping.identity;
    result.name = mapping.name;
    result.mapping = mapping;
    result.resolvedJson = mappingToResolvedJson (mapping);

    return juce::Result::ok();
}
}
