#include "MappingConfig.h"
#include "ParamIds.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_osc/juce_osc.h>

#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace
{
constexpr auto sendIntervalMs = 20;
constexpr auto configPollIntervalMs = 250;
constexpr auto snapshotIntervalMs = 5000;
constexpr auto reconnectIntervalMs = 1000;
constexpr auto processingHeartbeatTimeoutMs = 1000;
constexpr auto registryRetryIntervalMs = 1000;
constexpr auto changeEpsilon = 0.0001f;

juce::File configDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile (".osc-macro");
}

juce::File mappingsFile()
{
    return configDirectory().getChildFile ("mappings.json");
}

juce::File legacyMappingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile (".chromatik-macros")
        .getChildFile ("mappings.json");
}

std::mutex& mappingsWriteMutex()
{
    static std::mutex instance;
    return instance;
}

juce::Result migrateLegacyConfigIfNeeded()
{
    const std::scoped_lock fileLock (mappingsWriteMutex());
    const auto destination = mappingsFile();
    const auto legacy = legacyMappingsFile();

    if (destination.existsAsFile() || ! legacy.existsAsFile())
        return juce::Result::ok();

    if (destination.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("Could not create "
                                   + destination.getParentDirectory().getFullPathName());

    juce::TemporaryFile temporary (destination);

    if (! temporary.getFile().replaceWithText (legacy.loadFileAsString()))
        return juce::Result::fail ("Could not stage legacy mappings migration");

    if (! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not migrate legacy mappings file");

    return juce::Result::ok();
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (auto index = 0; index < oscmacro::macroCount; ++index)
    {
        // ParameterID is the cryptic VST3-compatibility string (see ParamIds.h);
        // the second argument is the human-visible name, which stays "macroN".
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { oscmacro::paramIds[static_cast<size_t> (index)], 1 },
            oscmacro::paramName (index),
            juce::NormalisableRange<float> { 0.0f, 1.0f },
            0.0f));
    }

    return layout;
}

// Whether to advertise VST3 class-ID compatibility with OSCpar (see
// OSCMacroVST3ClientExtensions below). Flip to false to disable without
// deleting the implementation - whether Bitwig honours VST3
// IPluginCompatibility / getCompatibleClasses() declarations at all is
// UNTESTED.
constexpr bool advertiseOscParCompatibility = true;

// OSCpar's VST3 class UID, formatted exactly as
// juce::VST3ClientExtensions::getCompatibleClasses() documents: "a
// 32-character string consisting only of the characters 0-9 and A-F".
//
// This is a *compatibility* declaration, not identity impersonation: OSCMacro
// keeps its own VST3 class UID (derived from PLUGIN_MANUFACTURER_CODE /
// PLUGIN_CODE in CMakeLists.txt, both unchanged). This only tells a host that
// implements IPluginCompatibility that this plugin can stand in for OSCpar -
// it does not claim to *be* OSCpar.
constexpr auto oscParCompatibleClassId = "ABCDEF019182FAEB4550666C4F534368";

class OSCMacroVST3ClientExtensions final : public juce::VST3ClientExtensions
{
public:
    std::vector<juce::String> getCompatibleClasses() const override
    {
        if (! advertiseOscParCompatibility)
            return {};

        return { juce::String (oscParCompatibleClassId) };
    }
};

bool hasEnabledRoute (const oscmacro::Mapping& mapping)
{
    for (const auto& route : mapping.macros)
        if (route.enabled)
            return true;

    return false;
}

class DestinationRegistry
{
public:
    static bool acquire (const void* owner, const oscmacro::Mapping& mapping)
    {
        auto keys = keysFor (mapping);
        const std::scoped_lock lock (mutex());

        for (const auto& key : keys)
            if (const auto found = destinations().find (key);
                found != destinations().end() && found->second != owner)
                return false;

        for (const auto& key : keys)
            destinations()[key] = owner;

        return true;
    }

    static void release (const void* owner)
    {
        const std::scoped_lock lock (mutex());

        for (auto iterator = destinations().begin(); iterator != destinations().end();)
            if (iterator->second == owner)
                iterator = destinations().erase (iterator);
            else
                ++iterator;
    }

private:
    static std::vector<std::string> keysFor (const oscmacro::Mapping& mapping)
    {
        std::vector<std::string> keys;

        for (auto index = 0; index < oscmacro::macroCount; ++index)
            if (mapping.macros[static_cast<size_t> (index)].enabled)
                keys.push_back ((mapping.host + ":" + juce::String (mapping.port)
                                 + mapping.addressFor (index)).toStdString());

        return keys;
    }

    static std::mutex& mutex()
    {
        static std::mutex instance;
        return instance;
    }

    static std::map<std::string, const void*>& destinations()
    {
        static std::map<std::string, const void*> instance;
        return instance;
    }
};

juce::Result writeMappingEntry (const juce::File& file,
                                const juce::String& identity,
                                const juce::String& name,
                                const juce::String& fallbackResolvedJson)
{
    // Several instances can be activated together when a project opens. Keep their
    // read-modify-write registrations from replacing one another.
    const std::scoped_lock fileLock (mappingsWriteMutex());
    const auto currentJson = file.existsAsFile() ? file.loadFileAsString() : juce::String();
    juce::String updatedJson;

    if (const auto result = oscmacro::upsertMappingName (currentJson, identity, name,
                                                         fallbackResolvedJson, updatedJson);
        result.failed())
        return result;

    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail ("Could not create " + file.getParentDirectory().getFullPathName());

    juce::TemporaryFile temporary (file);

    if (! temporary.getFile().replaceWithText (updatedJson + "\n"))
        return juce::Result::fail ("Could not write temporary mappings file");

    if (! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not replace mappings file");

    return juce::Result::ok();
}
}

class OSCMacroProcessor;

class OSCMacroEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit OSCMacroEditor (OSCMacroProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    OSCMacroProcessor& owner;
    juce::Label nameCaption;
    juce::Label nameEditor;
    juce::Label status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OSCMacroEditor)
};

class OSCMacroProcessor final : public juce::AudioProcessor,
                                private juce::AsyncUpdater
{
public:
    OSCMacroProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          // Keep the legacy ValueTree type so existing Bitwig state chunks restore.
          parameters (*this, nullptr, "ChromatikMacro", createParameterLayout()),
          instanceIdentity (juce::Uuid().toString()),
          instanceName ("(unnamed)")
    {
        for (auto index = 0; index < oscmacro::macroCount; ++index)
            macros[static_cast<size_t> (index)] =
                parameters.getRawParameterValue (oscmacro::paramIds[static_cast<size_t> (index)]);
    }

    ~OSCMacroProcessor() override
    {
        stopWorker();
        cancelPendingUpdate();
    }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void prepareToPlay (double, int) override
    {
        offline.store (isNonRealtime(), std::memory_order_release);
        lastProcessBlockMs.store (0, std::memory_order_release);
        active.store (true, std::memory_order_release);
        startWorker();
    }

    void releaseResources() override
    {
        active.store (false, std::memory_order_release);
        lastProcessBlockMs.store (0, std::memory_order_release);
        stopWorker();
        setRuntimeStatus (RuntimeState::inactive);
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& input = layouts.getMainInputChannelSet();
        const auto& output = layouts.getMainOutputChannelSet();
        return input == output
            && (output == juce::AudioChannelSet::mono()
                || output == juce::AudioChannelSet::stereo());
    }

    template <typename Sample>
    void process (juce::AudioBuffer<Sample>& buffer)
    {
        juce::ScopedNoDenormals noDenormals;
        offline.store (isNonRealtime(), std::memory_order_release);
        lastProcessBlockMs.store (juce::Time::getMillisecondCounter(),
                                  std::memory_order_release);

        for (auto channel = getTotalNumInputChannels();
             channel < getTotalNumOutputChannels();
             ++channel)
            buffer.clear (channel, 0, buffer.getNumSamples());
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        process (buffer);
    }

    void processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override
    {
        process (buffer);
    }

    bool supportsDoublePrecisionProcessing() const override { return true; }
    bool hasEditor() const override { return true; }

    juce::VST3ClientExtensions* getVST3ClientExtensions() override
    {
        return &vst3ClientExtensions;
    }

    juce::AudioProcessorEditor* createEditor() override
    {
        return new OSCMacroEditor (*this);
    }

    void getStateInformation (juce::MemoryBlock& destination) override
    {
        auto state = parameters.copyState();

        {
            const juce::ScopedLock lock (stateLock);
            state.setProperty ("instanceIdentity", instanceIdentity, nullptr);
            state.setProperty ("instanceName", instanceName, nullptr);
            state.setProperty ("cachedMappingIdentity", cachedMappingIdentity, nullptr);
            state.setProperty ("cachedMappingJson", cachedMappingJson, nullptr);
        }

        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destination);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
        {
            auto state = juce::ValueTree::fromXml (*xml);

            if (! state.hasType (parameters.state.getType()))
            {
                // Not our own APVTS-derived state. If it's an OSCpar chunk (Bitwig
                // hands us OSCpar's state when OSCMacro replaces it in place - see
                // the VST3 class-UID compatibility declaration above), adopt its
                // routing instead of silently discarding it. Anything else
                // (unrecognised chunk, or an OSCpar chunk with no Prefix, i.e.
                // "unconfigured") falls through unchanged, exactly as before.
                if (oscmacro::isOscParPreset (*xml))
                {
                    oscmacro::OscParAdoption adoption;

                    if (oscmacro::adoptOscParPreset (*xml, xml->toString(), adoption).wasOk())
                        adoptResolvedIdentity (adoption.identity, adoption.name,
                                              adoption.identity, adoption.resolvedJson);
                }

                return;
            }

            const auto restoredIdentity = state.getProperty ("instanceIdentity").toString().trim();
            const auto restoredName = state.getProperty ("instanceName", "(unnamed)")
                                          .toString().trim();
            const auto restoredJson = state.getProperty ("cachedMappingJson").toString();
            auto restoredCachedIdentity = state.getProperty ("cachedMappingIdentity")
                                              .toString().trim();

            adoptResolvedIdentity (restoredIdentity, restoredName,
                                   restoredCachedIdentity, restoredJson);

            state.removeProperty ("instanceIdentity", nullptr);
            state.removeProperty ("instanceName", nullptr);
            state.removeProperty ("cachedMappingIdentity", nullptr);
            state.removeProperty ("cachedMappingSlot", nullptr);
            state.removeProperty ("cachedMappingJson", nullptr);
            parameters.replaceState (state);
        }
    }

    juce::String getInstanceName() const
    {
        const juce::ScopedLock lock (stateLock);
        return instanceName;
    }

    void setInstanceNameFromEditor (juce::String name)
    {
        name = name.trim();

        if (name.isEmpty())
            name = "(unnamed)";

        {
            const juce::ScopedLock lock (stateLock);

            if (instanceName == name)
                return;

            instanceName = name;
            ++nameEditRevision;
        }

        requestAsyncUpdate (asyncHostDirty);
    }

    juce::String getStatusText() const
    {
        const juce::ScopedLock lock (statusLock);
        return statusText;
    }

private:
    enum class RuntimeState
    {
        sending,
        unconfigured,
        collision,
        error,
        offline,
        inactive
    };

    struct StateSnapshot
    {
        juce::String identity;
        juce::String name;
        juce::String cachedIdentity;
        juce::String cachedJson;
        uint64_t identityVersion = 0;
        uint64_t editedNameVersion = 0;
        uint64_t persistedNameVersion = 0;
    };

    StateSnapshot getStateSnapshot() const
    {
        const juce::ScopedLock lock (stateLock);
        return { instanceIdentity,
                 instanceName,
                 cachedMappingIdentity,
                 cachedMappingJson,
                 identityRevision.load (std::memory_order_acquire),
                 nameEditRevision,
                 persistedNameEditRevision };
    }

    // Adopts a resolved identity/name/cached-mapping triple, whether it came
    // from OSCMacro's own persisted state or (see setStateInformation) from a
    // converted OSCpar chunk. Queues the reset-on-load dispatch and a worker
    // reload exactly as a normal state restore does, so an adopted OSCpar
    // route self-registers into mappings.json the same way.
    void adoptResolvedIdentity (const juce::String& restoredIdentity,
                                const juce::String& restoredName,
                                juce::String restoredCachedIdentity,
                                const juce::String& restoredJson)
    {
        {
            const juce::ScopedLock lock (stateLock);

            if (restoredIdentity.isNotEmpty())
                instanceIdentity = restoredIdentity;

            instanceName = restoredName.isNotEmpty() ? restoredName : "(unnamed)";

            if (restoredCachedIdentity.isEmpty() && restoredJson.isNotEmpty())
                restoredCachedIdentity = instanceIdentity;

            cachedMappingIdentity = restoredCachedIdentity;
            cachedMappingJson = restoredJson;
            resetRequestedIdentity = instanceIdentity;
            resetAwaitingMapping = true;
            resetDispatchReady = false;
            resetInProgress.store (true, std::memory_order_release);
            persistedNameEditRevision = nameEditRevision;
        }

        identityRevision.fetch_add (1, std::memory_order_release);
    }

    void updateResolvedMapping (const oscmacro::Mapping& mapping,
                                const juce::String& json)
    {
        auto changed = false;

        {
            const juce::ScopedLock lock (stateLock);

            if (mapping.identity != instanceIdentity)
                return;

            if (cachedMappingIdentity != mapping.identity || cachedMappingJson != json)
            {
                cachedMappingIdentity = mapping.identity;
                cachedMappingJson = json;
                changed = true;
            }

            const auto resolvedName = mapping.name.trim().isNotEmpty()
                                        ? mapping.name.trim()
                                        : juce::String ("(unnamed)");

            if (nameEditRevision == persistedNameEditRevision
                && instanceName != resolvedName)
            {
                instanceName = resolvedName;
                changed = true;
            }
        }

        if (changed)
            requestAsyncUpdate (asyncHostDirty);
    }

    void setRuntimeStatus (RuntimeState state,
                           const juce::String& target = {},
                           const juce::String& detail = {})
    {
        juce::String description;

        switch (state)
        {
            case RuntimeState::sending:      description = "sending"; break;
            case RuntimeState::unconfigured: description = "unconfigured"; break;
            case RuntimeState::collision:    description = "collision"; break;
            case RuntimeState::error:        description = "error"; break;
            case RuntimeState::offline:      description = "offline"; break;
            case RuntimeState::inactive:     description = "inactive"; break;
        }

        if (detail.isNotEmpty())
            description += ": " + detail;

        const auto next = target.isNotEmpty() ? target + " — " + description : description;
        const juce::ScopedLock lock (statusLock);
        statusText = next;
    }

    void acknowledgeNameEdit (uint64_t revision)
    {
        const juce::ScopedLock lock (stateLock);

        if (revision > persistedNameEditRevision)
            persistedNameEditRevision = revision;
    }

    void queueResetFromResolvedMapping (const oscmacro::Mapping& mapping)
    {
        {
            const juce::ScopedLock lock (stateLock);

            if (! resetAwaitingMapping || mapping.identity != resetRequestedIdentity
                || mapping.identity != instanceIdentity)
                return;

            resetAwaitingMapping = false;
            resetDispatchReady = true;
            resetDispatchIdentity = mapping.identity;

            for (auto index = 0; index < oscmacro::macroCount; ++index)
            {
                const auto& route = mapping.macros[static_cast<size_t> (index)];
                resetDispatchEnabled[static_cast<size_t> (index)] =
                    route.enabled && route.resetOnLoad;
                resetDispatchValues[static_cast<size_t> (index)] = route.initial;
            }
        }

        requestAsyncUpdate (asyncResetDispatch);
    }

    void dispatchPendingReset()
    {
        std::array<float, oscmacro::macroCount> values {};
        std::array<bool, oscmacro::macroCount> enabled {};

        {
            const juce::ScopedLock lock (stateLock);

            if (! resetDispatchReady || resetDispatchIdentity != instanceIdentity)
                return;

            resetDispatchReady = false;
            values = resetDispatchValues;
            enabled = resetDispatchEnabled;
        }

        for (auto index = 0; index < oscmacro::macroCount; ++index)
        {
            if (! enabled[static_cast<size_t> (index)])
                continue;

            if (auto* parameter = parameters.getParameter (oscmacro::paramIds[static_cast<size_t> (index)]))
                parameter->setValueNotifyingHost (
                    parameter->convertTo0to1 (values[static_cast<size_t> (index)]));
        }

        resetInProgress.store (false, std::memory_order_release);
    }

    void requestAsyncUpdate (unsigned int reason)
    {
        pendingAsyncActions.fetch_or (reason, std::memory_order_release);
        triggerAsyncUpdate();
    }

    void dispatchAsyncActions()
    {
        const auto actions = pendingAsyncActions.exchange (0, std::memory_order_acq_rel);

        if ((actions & asyncResetDispatch) != 0)
            dispatchPendingReset();

        if ((actions & asyncHostDirty) != 0)
            updateHostDisplay (
                juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));

        // If a worker request arrived while this callback was executing, make sure
        // it receives another message-thread turn.
        if (pendingAsyncActions.load (std::memory_order_acquire) != 0)
            triggerAsyncUpdate();
    }

    static constexpr unsigned int asyncHostDirty = 1u << 0;
    static constexpr unsigned int asyncResetDispatch = 1u << 1;

    class Worker final : public juce::Thread
    {
    public:
        explicit Worker (OSCMacroProcessor& processorToUse)
            : Thread ("OSCMacro OSC"), processor (processorToUse)
        {
            lastSent.fill (std::numeric_limits<float>::quiet_NaN());
        }

        ~Worker() override
        {
            DestinationRegistry::release (this);
        }

        void run() override
        {
            if (const auto result = migrateLegacyConfigIfNeeded(); result.failed())
                reportError (result.getErrorMessage());

            reloadMapping();
            auto lastConfigPoll = juce::Time::getMillisecondCounter();

            while (! threadShouldExit())
            {
                if (wait (sendIntervalMs))
                    break;

                const auto now = juce::Time::getMillisecondCounter();

                if (now - lastConfigPoll >= configPollIntervalMs)
                {
                    reloadMapping();
                    lastConfigPoll = now;
                }

                sendValues (now);
            }

            DestinationRegistry::release (this);
            sender.disconnect();
        }

    private:
        enum class MappingSource
        {
            none,
            file,
            cachedState
        };

        void reloadMapping()
        {
            const auto snapshot = processor.getStateSnapshot();

            if (snapshot.identityVersion != currentIdentityVersion
                || snapshot.identity != currentIdentity)
            {
                currentIdentityVersion = snapshot.identityVersion;
                currentIdentity = snapshot.identity;
                processedNameEditRevision = 0;
                loggedSource = MappingSource::none;
                clearMapping();
            }

            const auto file = mappingsFile();

            if (snapshot.editedNameVersion > snapshot.persistedNameVersion
                && snapshot.editedNameVersion != processedNameEditRevision)
            {
                const auto fallback = snapshot.cachedIdentity == snapshot.identity
                                          ? snapshot.cachedJson
                                          : juce::String();
                const auto result = writeMappingEntry (file, snapshot.identity,
                                                       snapshot.name, fallback);

                if (result.failed())
                    reportError (result.getErrorMessage());
                else
                {
                    processedNameEditRevision = snapshot.editedNameVersion;
                    processor.acknowledgeNameEdit (snapshot.editedNameVersion);
                }
            }

            if (file.existsAsFile())
            {
                oscmacro::Mapping candidate;
                juce::String resolvedJson;
                const auto result = oscmacro::parseMappingsFile (
                    file.loadFileAsString(), currentIdentity, candidate, resolvedJson);

                if (result.wasOk())
                {
                    applyMapping (candidate, resolvedJson, MappingSource::file);
                    lastError.clear();
                    return;
                }

                if (! result.getErrorMessage().startsWith ("No mapping exists"))
                {
                    reportError ("Identity " + currentIdentity + ": "
                                 + result.getErrorMessage());
                    restoreCachedMapping (snapshot);
                    return;
                }
            }

            if (restoreCachedMapping (snapshot))
            {
                // Re-register the cached object so it remains editable, without
                // replacing its routing with a blank first-run entry.
                const auto result = writeMappingEntry (file, snapshot.identity,
                                                       snapshot.name,
                                                       snapshot.cachedJson);
                if (result.failed())
                    reportError (result.getErrorMessage());
                return;
            }

            const auto result = writeMappingEntry (file, snapshot.identity,
                                                   snapshot.name, {});
            if (result.failed())
                reportError (result.getErrorMessage());
            else
                processor.setRuntimeStatus (RuntimeState::unconfigured,
                                            "127.0.0.1:3030");
        }

        bool restoreCachedMapping (const StateSnapshot& snapshot)
        {
            if (hasMapping || snapshot.cachedJson.isEmpty()
                || snapshot.cachedIdentity != snapshot.identity)
                return hasMapping;

            oscmacro::Mapping candidate;
            const auto result = oscmacro::parseResolvedMapping (
                snapshot.cachedJson, snapshot.identity, candidate);

            if (result.wasOk())
            {
                applyMapping (candidate, snapshot.cachedJson, MappingSource::cachedState);
                return true;
            }

            reportError ("Cached mapping is invalid: " + result.getErrorMessage());
            return false;
        }

        void applyMapping (const oscmacro::Mapping& candidate,
                           const juce::String& sourceJson,
                           MappingSource source)
        {
            if (candidate.identity != currentIdentity)
                return;

            noteResolvedSource (source);
            processor.queueResetFromResolvedMapping (candidate);

            if (hasMapping && sourceJson == activeSourceJson)
                return;

            clearMapping();
            mapping = candidate;
            activeSourceJson = sourceJson;
            hasMapping = true;
            processor.updateResolvedMapping (mapping, sourceJson);

            if (mapping.prefix.isEmpty() || ! hasEnabledRoute (mapping))
            {
                processor.setRuntimeStatus (RuntimeState::unconfigured, targetFor (mapping));
                return;
            }

            lastSent.fill (std::numeric_limits<float>::quiet_NaN());
            fullSnapshotNeeded = true;
        }

        void clearMapping()
        {
            DestinationRegistry::release (this);
            sender.disconnect();
            hasMapping = false;
            connected = false;
            collision = false;
            ownsDestination = false;
            activeSourceJson.clear();
            fullSnapshotNeeded = true;
        }

        void sendValues (uint32_t now)
        {
            if (processor.identityRevision.load (std::memory_order_acquire)
                != currentIdentityVersion)
                return;

            if (processor.resetInProgress.load (std::memory_order_acquire))
                return;

            const auto isOffline = processor.offline.load (std::memory_order_acquire);
            const auto suppressed = ! processor.active.load (std::memory_order_acquire)
                                 || isOffline;

            if (suppressed)
            {
                relinquishDestination();

                if (isOffline)
                    processor.setRuntimeStatus (RuntimeState::offline,
                                                hasMapping ? targetFor (mapping)
                                                           : juce::String());
                wasSuppressed = true;
                return;
            }

            const auto lastProcess = processor.lastProcessBlockMs.load (
                std::memory_order_acquire);
            const auto processAge = static_cast<int32_t> (now - lastProcess);

            if (lastProcess == 0
                || processAge > processingHeartbeatTimeoutMs)
            {
                relinquishDestination();
                processor.setRuntimeStatus (RuntimeState::inactive,
                                            hasMapping ? targetFor (mapping)
                                                       : juce::String());
                wasSuppressed = true;
                return;
            }

            if (wasSuppressed)
            {
                fullSnapshotNeeded = true;
                wasSuppressed = false;

                if (hasMapping && ! collision && connected)
                    processor.setRuntimeStatus (RuntimeState::sending, targetFor (mapping));
            }

            if (! hasMapping || mapping.prefix.isEmpty()
                || ! hasEnabledRoute (mapping))
                return;

            if (! ownsDestination)
            {
                if (now - lastRegistryAttempt < registryRetryIntervalMs)
                    return;

                lastRegistryAttempt = now;
                ownsDestination = DestinationRegistry::acquire (this, mapping);
                collision = ! ownsDestination;

                if (collision)
                {
                    processor.setRuntimeStatus (RuntimeState::collision, targetFor (mapping));
                    reportError ("Duplicate OSC destination detected for identity "
                                 + mapping.identity, false);
                    return;
                }

                connected = false;
                fullSnapshotNeeded = true;
            }

            if (! connected)
            {
                if (now - lastConnectAttempt < reconnectIntervalMs)
                    return;

                connected = attemptConnect (now);

                if (! connected)
                    return;

                fullSnapshotNeeded = true;
                processor.setRuntimeStatus (RuntimeState::sending, targetFor (mapping));
            }

            const auto periodicSnapshot = now - lastSnapshot >= snapshotIntervalMs;
            auto sentAny = false;

            for (auto index = 0; index < oscmacro::macroCount; ++index)
            {
                const auto& route = mapping.macros[static_cast<size_t> (index)];

                if (! route.enabled)
                    continue;

                const auto normalized = processor.macros[static_cast<size_t> (index)]->load (
                    std::memory_order_relaxed);
                const auto value = route.minimum + normalized * (route.maximum - route.minimum);
                const auto previous = lastSent[static_cast<size_t> (index)];

                if (! fullSnapshotNeeded && ! periodicSnapshot
                    && std::isfinite (previous)
                    && std::abs (value - previous) <= changeEpsilon)
                    continue;

                if (sender.send (juce::OSCMessage (mapping.addressFor (index), value)))
                {
                    lastSent[static_cast<size_t> (index)] = value;
                    sentAny = true;
                }
                else
                {
                    connected = false;
                    lastConnectAttempt = now;
                    processor.setRuntimeStatus (RuntimeState::error, targetFor (mapping),
                                                "send failed");
                    break;
                }
            }

            if (sentAny && (fullSnapshotNeeded || periodicSnapshot))
            {
                fullSnapshotNeeded = false;
                lastSnapshot = now;
            }
        }

        static juce::String targetFor (const oscmacro::Mapping& value)
        {
            return value.host + ":" + juce::String (value.port);
        }

        bool attemptConnect (uint32_t now)
        {
            lastConnectAttempt = now;
            return sender.connect (mapping.host, mapping.port);
        }

        void relinquishDestination()
        {
            if (! ownsDestination && ! connected && ! collision)
                return;

            if (ownsDestination)
                DestinationRegistry::release (this);

            ownsDestination = false;
            collision = false;
            connected = false;
            sender.disconnect();
        }

        void noteResolvedSource (MappingSource source)
        {
            if (source == loggedSource)
                return;

            loggedSource = source;
            logMessage ("Identity " + currentIdentity + " resolved from "
                        + (source == MappingSource::file ? "file entry"
                                                        : "cached state"));
        }

        void reportError (const juce::String& message, bool updateStatus = true)
        {
            if (updateStatus)
                processor.setRuntimeStatus (RuntimeState::error,
                                            hasMapping ? targetFor (mapping)
                                                       : juce::String(),
                                            message);

            if (message == lastError)
                return;

            lastError = message;
            logMessage (message);
        }

        static void logMessage (const juce::String& message)
        {
            const auto directory = mappingsFile().getParentDirectory();

            if (directory.createDirectory().failed())
                return;

            juce::FileOutputStream stream (directory.getChildFile ("plugin.log"));

            if (stream.openedOk())
                stream.writeText (juce::Time::getCurrentTime().toISO8601 (true)
                                      + " " + message + "\n",
                                  false, false, "\n");
        }

        OSCMacroProcessor& processor;
        juce::OSCSender sender;
        oscmacro::Mapping mapping;
        std::array<float, oscmacro::macroCount> lastSent {};
        juce::String activeSourceJson;
        juce::String currentIdentity;
        juce::String lastError;
        uint64_t currentIdentityVersion = std::numeric_limits<uint64_t>::max();
        uint64_t processedNameEditRevision = 0;
        uint32_t lastSnapshot = 0;
        uint32_t lastConnectAttempt = 0;
        uint32_t lastRegistryAttempt = 0;
        MappingSource loggedSource = MappingSource::none;
        bool hasMapping = false;
        bool connected = false;
        bool collision = false;
        bool ownsDestination = false;
        bool fullSnapshotNeeded = true;
        bool wasSuppressed = false;
    };

    void handleAsyncUpdate() override
    {
        dispatchAsyncActions();
    }

    void startWorker()
    {
        if (worker != nullptr)
            return;

        worker = std::make_unique<Worker> (*this);
        worker->startThread();
    }

    void stopWorker()
    {
        if (worker == nullptr)
            return;

        worker->signalThreadShouldExit();
        worker->notify();
        worker->stopThread (2000);
        worker.reset();
    }

    juce::AudioProcessorValueTreeState parameters;
    OSCMacroVST3ClientExtensions vst3ClientExtensions;
    std::array<std::atomic<float>*, oscmacro::macroCount> macros {};
    std::atomic<bool> active { false };
    std::atomic<bool> offline { false };
    std::atomic<bool> resetInProgress { false };
    std::atomic<uint32_t> lastProcessBlockMs { 0 };
    std::atomic<uint64_t> identityRevision { 0 };
    std::atomic<unsigned int> pendingAsyncActions { 0 };
    std::unique_ptr<Worker> worker;

    mutable juce::CriticalSection stateLock;
    juce::String instanceIdentity;
    juce::String instanceName;
    juce::String cachedMappingIdentity;
    juce::String cachedMappingJson;
    uint64_t nameEditRevision = 0;
    uint64_t persistedNameEditRevision = 0;
    juce::String resetRequestedIdentity;
    juce::String resetDispatchIdentity;
    std::array<float, oscmacro::macroCount> resetDispatchValues {};
    std::array<bool, oscmacro::macroCount> resetDispatchEnabled {};
    bool resetAwaitingMapping = false;
    bool resetDispatchReady = false;

    mutable juce::CriticalSection statusLock;
    juce::String statusText { "inactive" };

    friend class OSCMacroEditor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OSCMacroProcessor)
};

OSCMacroEditor::OSCMacroEditor (OSCMacroProcessor& processorToUse)
    : AudioProcessorEditor (processorToUse), owner (processorToUse)
{
    nameCaption.setText ("Name", juce::dontSendNotification);
    nameCaption.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (nameCaption);

    nameEditor.setText (owner.getInstanceName(), juce::dontSendNotification);
    nameEditor.setEditable (true, true, false);
    nameEditor.setColour (juce::Label::backgroundColourId,
                          getLookAndFeel().findColour (juce::TextEditor::backgroundColourId));
    nameEditor.setColour (juce::Label::outlineColourId,
                          getLookAndFeel().findColour (juce::TextEditor::outlineColourId));
    nameEditor.onTextChange = [this]
    {
        owner.setInstanceNameFromEditor (nameEditor.getText());
    };
    addAndMakeVisible (nameEditor);

    status.setText (owner.getStatusText(), juce::dontSendNotification);
    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);

    setSize (440, 104);
    startTimerHz (4);
}

void OSCMacroEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void OSCMacroEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto nameRow = area.removeFromTop (32);
    nameCaption.setBounds (nameRow.removeFromLeft (48));
    nameEditor.setBounds (nameRow);
    area.removeFromTop (10);
    status.setBounds (area.removeFromTop (26));
}

void OSCMacroEditor::timerCallback()
{
    if (! nameEditor.isBeingEdited())
        nameEditor.setText (owner.getInstanceName(), juce::dontSendNotification);

    status.setText (owner.getStatusText(), juce::dontSendNotification);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OSCMacroProcessor();
}
