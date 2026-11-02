#include "PluginProcessor.h"
#include "ParamMap.h"
#include "../editor/PluginEditor.h"

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout ClawShaperProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const int v = 1; // parameter version
    for (const auto& d : cs::kParams)
    {
        const String id (d.id);
        if (d.isInt && d.mx - d.mn <= 1.0f)
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, v }, d.name, d.df > 0.5f));
        else if (d.isInt)
            p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, v }, d.name, (int) d.mn, (int) d.mx, (int) d.df));
        else
        {
            NormalisableRange<float> r (d.mn, d.mx, 0.001f);
            if (d.log)
            {
                const float lo = d.mn, ratio = d.mx / d.mn;
                r = NormalisableRange<float> (lo, d.mx,
                    [lo, ratio] (float, float, float n) { return lo * std::pow (ratio, n); },
                    [lo, ratio] (float, float, float x) { return std::log (x / lo) / std::log (ratio); },
                    [lo, mx = d.mx] (float, float, float x) { return jlimit (lo, mx, x); });
            }
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, d.name, r, d.df));
        }
    }
    return { p.begin(), p.end() };
}

ClawShaperProcessor::ClawShaperProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CLAWSHAPER", createLayout())
{
    for (int i = 0; i < cs::kNumParams; ++i) raw[(size_t) i] = apvts.getRawParameterValue (cs::kParams[i].id);
}

bool ClawShaperProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == AudioChannelSet::stereo() && l.getMainInputChannelSet() == AudioChannelSet::stereo();
}

var ClawShaperProcessor::getSetting (const char* key, const var& fallback) const { return apvts.state.getProperty (key, fallback); }
void ClawShaperProcessor::setSetting (const char* key, const var& v) { apvts.state.setProperty (key, v, nullptr); }

void ClawShaperProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateNow = sampleRate;
    engine.prepare (sampleRate);
    for (int i = 0; i < cs::kNumParams; ++i) vals[i] = raw[(size_t) i]->load();
    cs::applyParams (vals, P);
    engine.setParams (P);
    std::memcpy (lastVals, vals, sizeof (vals)); haveLast = true;
    engine.reset();
    lastLatency = engine.latencySamples();
    reportedLatency.store (lastLatency);
    setLatencySamples (lastLatency);
}

void ClawShaperProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n <= 0) return;

    for (int i = 0; i < cs::kNumParams; ++i) vals[i] = raw[(size_t) i]->load();
    if (! haveLast || std::memcmp (vals, lastVals, sizeof (vals)) != 0)
    {
        cs::applyParams (vals, P);
        engine.setParams (P);
        std::memcpy (lastVals, vals, sizeof (vals)); haveLast = true;
        const int lat = engine.latencySamples();
        if (lat != lastLatency) { lastLatency = lat; reportedLatency.store (lat); triggerAsyncUpdate(); }
    }
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
}

AudioProcessorEditor* ClawShaperProcessor::createEditor() { return new ClawShaperEditor (*this); }

void ClawShaperProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void ClawShaperProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml)); // unknown keys are ignored by APVTS
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ClawShaperProcessor(); }
