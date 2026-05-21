#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Colour palette
// ─────────────────────────────────────────────────────────────────────────────
namespace Colors
{
    inline const juce::Colour bg         { 0xff0f0f1a };
    inline const juce::Colour panelBg    { 0xff1a1a2e };
    inline const juce::Colour accent     { 0xff4fc3f7 };
    inline const juce::Colour knobTrack  { 0xff2a2a4a };
    inline const juce::Colour knobFill   { 0xff4fc3f7 };
    inline const juce::Colour labelText  { 0xffaaaacc };
    inline const juce::Colour valueText  { 0xffffffff };
    inline const juce::Colour grGreen    { 0xff00e676 };
    inline const juce::Colour grYellow   { 0xffffab00 };
    inline const juce::Colour grRed      { 0xffff5252 };
}

// ─────────────────────────────────────────────────────────────────────────────
// GR Meter — vertical LED-style bar, polled at 30 Hz
// ─────────────────────────────────────────────────────────────────────────────
class GRMeter final : public juce::Component, private juce::Timer
{
public:
    explicit GRMeter(ProCompressorAudioProcessor& p);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    ProCompressorAudioProcessor& proc;
    float displayGR = 0.0f;   // smoothed value for display
    static constexpr float kMaxDb = 24.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// KnobWithLabel — rotary slider + label underneath
// ─────────────────────────────────────────────────────────────────────────────
class KnobWithLabel final : public juce::Component
{
public:
    KnobWithLabel(const juce::String& labelText, juce::Colour trackColour = Colors::knobFill);

    void resized() override;

    juce::Slider slider;
    juce::Label  label;
};

// ─────────────────────────────────────────────────────────────────────────────
// Main editor
// ─────────────────────────────────────────────────────────────────────────────
class ProCompressorAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ProCompressorAudioProcessorEditor(ProCompressorAudioProcessor&);
    ~ProCompressorAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BtnAttach  = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ProCompressorAudioProcessor& audioProcessor;

    KnobWithLabel thresholdKnob  { "THRESH",  Colors::knobFill };
    KnobWithLabel ratioKnob      { "RATIO",   Colors::knobFill };
    KnobWithLabel attackKnob     { "ATTACK",  juce::Colour(0xff80cbc4) };
    KnobWithLabel releaseKnob    { "RELEASE", juce::Colour(0xff80cbc4) };
    KnobWithLabel kneeKnob       { "KNEE",    juce::Colour(0xffce93d8) };
    KnobWithLabel makeupKnob     { "MAKEUP",  juce::Colour(0xffffd54f) };
    KnobWithLabel mixKnob        { "MIX",     juce::Colour(0xffef9a9a) };

    juce::ToggleButton sidechainBtn { "EXT SC" };
    GRMeter            grMeter;

    std::unique_ptr<Attachment> threshAtt, ratioAtt, attackAtt, releaseAtt;
    std::unique_ptr<Attachment> kneeAtt, makeupAtt, mixAtt;
    std::unique_ptr<BtnAttach>  scAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProCompressorAudioProcessorEditor)
};
