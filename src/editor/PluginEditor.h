#pragma once
#include "../plugin/PluginProcessor.h"

// Native port of the approved mockup window (the mockup IS the plugin). The layout comes from
// tools/layout.json via tools/gen_layout.py; one drawing surface draws boxes, texts, controls and displays.
class ClawShaperEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ClawShaperEditor (ClawShaperProcessor&);
    ~ClawShaperEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void setRate (int hz);
    struct Impl;
private:
    void timerCallback() override;
    std::unique_ptr<Impl> d;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClawShaperEditor)
};
