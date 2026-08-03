#include "MappingConfig.h"

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
constexpr auto changeEpsilon = 0.0001f;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "slot", 1 },
        "Slot",
        1,
        64,
        1,
        juce::AudioParameterIntAttributes().withAutomatable (false)));

    for (auto index = 0; index < chromatik::macroCount; ++index)
    {
        const auto id = "macro" + juce::String (index + 1);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 },
            id,
            juce::NormalisableRange<float> { 0.0f, 1.0f },
            0.0f));
    }

    return layout;
}

class DestinationRegistry
{
public:
    static bool acquire (const void* owner, const chromatik::Mapping& mapping)
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
    static std::vector<std::string> keysFor (const chromatik::Mapping& mapping)
    {
        std::vector<std::string> keys;

        for (auto index = 0; index < chromatik::macroCount; ++index)
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
}

class ChromatikMacroProcessor final : public juce::AudioProcessor,
                                      private juce::AsyncUpdater
{
public:
    ChromatikMacroProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          parameters (*this, nullptr, "ChromatikMacro", createParameterLayout())
    {
        slot = parameters.getRawParameterValue ("slot");

        for (auto index = 0; index < chromatik::macroCount; ++index)
            macros[static_cast<size_t> (index)] =
                parameters.getRawParameterValue ("macro" + juce::String (index + 1));
    }

    ~ChromatikMacroProcessor() override
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
        active.store (true, std::memory_order_release);
        startWorker();
    }

    void releaseResources() override
    {
        active.store (false, std::memory_order_release);
        stopWorker();
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
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    void getStateInformation (juce::MemoryBlock& destination) override
    {
        auto state = parameters.copyState();

        {
            const juce::ScopedLock lock (cacheLock);
            state.setProperty ("cachedMappingSlot", cachedMappingSlot, nullptr);
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
                return;

            {
                const juce::ScopedLock lock (cacheLock);
                cachedMappingSlot = static_cast<int> (state.getProperty ("cachedMappingSlot", 0));
                cachedMappingJson = state.getProperty ("cachedMappingJson").toString();
            }

            state.removeProperty ("cachedMappingSlot", nullptr);
            state.removeProperty ("cachedMappingJson", nullptr);
            parameters.replaceState (state);
        }
    }

private:
    class Worker final : public juce::Thread
    {
    public:
        explicit Worker (ChromatikMacroProcessor& processorToUse)
            : Thread ("ChromatikMacro OSC"), processor (processorToUse)
        {
            lastSent.fill (std::numeric_limits<float>::quiet_NaN());
        }

        ~Worker() override
        {
            DestinationRegistry::release (this);
        }

        void run() override
        {
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
        void reloadMapping()
        {
            const auto requestedSlot = juce::roundToInt (
                processor.slot->load (std::memory_order_relaxed));
            const auto slotChanged = requestedSlot != currentSlot;

            if (slotChanged)
            {
                currentSlot = requestedSlot;
                clearMapping();
            }

            const auto file = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                  .getChildFile (".chromatik-macros")
                                  .getChildFile ("mappings.json");

            if (file.existsAsFile())
            {
                chromatik::Mapping candidate;
                juce::String resolvedJson;
                const auto result = chromatik::parseMappingsFile (
                    file.loadFileAsString(), currentSlot, candidate, resolvedJson);

                if (result.wasOk())
                {
                    applyMapping (candidate, resolvedJson);
                    processor.updateCachedMapping (currentSlot, resolvedJson);
                    lastError.clear();
                    return;
                }

                if (result.getErrorMessage().startsWith ("No mapping exists"))
                    clearMapping();

                reportError ("Slot " + juce::String (currentSlot) + ": "
                             + result.getErrorMessage());
            }

            if (! hasMapping)
            {
                const auto cached = processor.getCachedMapping();

                if (cached.slot == currentSlot && cached.json.isNotEmpty())
                {
                    chromatik::Mapping candidate;
                    const auto result = chromatik::parseResolvedMapping (
                        cached.json, currentSlot, candidate);

                    if (result.wasOk())
                        applyMapping (candidate, cached.json);
                    else
                        reportError ("Cached mapping is invalid: " + result.getErrorMessage());
                }
            }
        }

        void applyMapping (const chromatik::Mapping& candidate,
                           const juce::String& sourceJson)
        {
            if (hasMapping && sourceJson == activeSourceJson)
            {
                if (collision && DestinationRegistry::acquire (this, mapping))
                {
                    collision = false;
                    connected = sender.connect (mapping.host, mapping.port);
                    fullSnapshotNeeded = true;
                    lastError.clear();
                }

                return;
            }

            clearMapping();
            mapping = candidate;
            activeSourceJson = sourceJson;
            hasMapping = true;
            collision = ! DestinationRegistry::acquire (this, mapping);

            if (collision)
            {
                reportError ("Duplicate OSC destination detected for slot "
                             + juce::String (mapping.slot));
                return;
            }

            connected = sender.connect (mapping.host, mapping.port);

            if (! connected)
                reportError ("Could not connect OSC sender to " + mapping.host + ":"
                             + juce::String (mapping.port));

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
            activeSourceJson.clear();
            fullSnapshotNeeded = true;
        }

        void sendValues (uint32_t now)
        {
            const auto suppressed = ! processor.active.load (std::memory_order_acquire)
                                 || processor.offline.load (std::memory_order_acquire);

            if (suppressed)
            {
                wasSuppressed = true;
                return;
            }

            if (wasSuppressed)
            {
                fullSnapshotNeeded = true;
                wasSuppressed = false;
            }

            if (! hasMapping || collision)
                return;

            if (! connected)
            {
                connected = sender.connect (mapping.host, mapping.port);

                if (! connected)
                    return;

                fullSnapshotNeeded = true;
            }

            const auto periodicSnapshot = now - lastSnapshot >= snapshotIntervalMs;
            auto sentAny = false;

            for (auto index = 0; index < chromatik::macroCount; ++index)
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
                    break;
                }
            }

            if (sentAny && (fullSnapshotNeeded || periodicSnapshot))
            {
                fullSnapshotNeeded = false;
                lastSnapshot = now;
            }
        }

        void reportError (const juce::String& message)
        {
            if (message == lastError)
                return;

            lastError = message;
            const auto directory = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                       .getChildFile (".chromatik-macros");

            if (directory.createDirectory().failed())
                return;

            juce::FileOutputStream stream (directory.getChildFile ("plugin.log"));

            if (stream.openedOk())
                stream.writeText (juce::Time::getCurrentTime().toISO8601 (true)
                                      + " " + message + "\n",
                                  false, false, "\n");
        }

        ChromatikMacroProcessor& processor;
        juce::OSCSender sender;
        chromatik::Mapping mapping;
        std::array<float, chromatik::macroCount> lastSent {};
        juce::String activeSourceJson;
        juce::String lastError;
        int currentSlot = 0;
        uint32_t lastSnapshot = 0;
        bool hasMapping = false;
        bool connected = false;
        bool collision = false;
        bool fullSnapshotNeeded = true;
        bool wasSuppressed = false;
    };

    struct CachedMapping
    {
        int slot = 0;
        juce::String json;
    };

    CachedMapping getCachedMapping() const
    {
        const juce::ScopedLock lock (cacheLock);
        return { cachedMappingSlot, cachedMappingJson };
    }

    void updateCachedMapping (int mappingSlot, const juce::String& json)
    {
        {
            const juce::ScopedLock lock (cacheLock);

            if (cachedMappingSlot == mappingSlot && cachedMappingJson == json)
                return;

            cachedMappingSlot = mappingSlot;
            cachedMappingJson = json;
        }

        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        updateHostDisplay (
            juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
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
    std::atomic<float>* slot = nullptr;
    std::array<std::atomic<float>*, chromatik::macroCount> macros {};
    std::atomic<bool> active { false };
    std::atomic<bool> offline { false };
    std::unique_ptr<Worker> worker;

    mutable juce::CriticalSection cacheLock;
    int cachedMappingSlot = 0;
    juce::String cachedMappingJson;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromatikMacroProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChromatikMacroProcessor();
}
