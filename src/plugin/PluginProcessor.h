#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/ClawShaperCore.h"
#include "ParamTable.h"
#include <atomic>
#include <cstring>

// Parameter ids are the ones of the approved mockup (see tools/params.json). Never rename them.
namespace setting
{
    inline constexpr const char* look = "look";       // "A", "B", "C"
    inline constexpr const char* tips = "tips";       // info bubbles
    inline constexpr const char* power = "power";     // power saving redraw
    inline constexpr const char* preset = "preset";   // name of the loaded preset
}

class ClawShaperProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    ClawShaperProcessor();
    ~ClawShaperProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ClawShaper"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    cs::Engine engine;      // the editor reads its meters and rings (plain floats, benign races)
    double sampleRateNow = 48000.0;

    juce::var getSetting (const char* key, const juce::var& fallback) const;
    void setSetting (const char* key, const juce::var& v);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override { setLatencySamples (reportedLatency.load()); updateHostDisplay (ChangeDetails().withLatencyChanged (true)); }

    std::array<std::atomic<float>*, cs::kNumParams> raw {};
    float vals[cs::kNumParams] {}, lastVals[cs::kNumParams] {};
    bool haveLast = false;
    cs::ShaperP P;
    std::atomic<int> reportedLatency { 0 };
    int lastLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClawShaperProcessor)
};
