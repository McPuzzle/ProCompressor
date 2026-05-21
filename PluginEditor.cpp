#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Custom LookAndFeel for the rotary knobs
// ─────────────────────────────────────────────────────────────────────────────
class CompressorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit CompressorLookAndFeel(juce::Colour fillColour)
        : fill(fillColour) {}

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider&) override
    {
        const float radius   = std::min(width, height) * 0.5f - 4.0f;
        const float centreX  = x + width  * 0.5f;
        const float centreY  = y + height * 0.5f;
        const float angle    = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Track arc (background)
        juce::Path trackArc;
        trackArc.addArc(centreX - radius, centreY - radius,
                        radius * 2.0f, radius * 2.0f,
                        rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(Colors::knobTrack);
        g.strokePath(trackArc, juce::PathStrokeType(3.5f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Filled arc
        juce::Path fillArc;
        fillArc.addArc(centreX - radius, centreY - radius,
                       radius * 2.0f, radius * 2.0f,
                       rotaryStartAngle, angle, true);
        g.setColour(fill);
        g.strokePath(fillArc, juce::PathStrokeType(3.5f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Knob body
        const float innerR = radius - 6.0f;
        g.setColour(juce::Colour(0xff252540));
        g.fillEllipse(centreX - innerR, centreY - innerR, innerR * 2.0f, innerR * 2.0f);

        // Indicator dot
        const float pointerLength = innerR * 0.65f;
        const float px = centreX + pointerLength * std::sin(angle);
        const float py = centreY - pointerLength * std::cos(angle);
        g.setColour(fill);
        g.fillEllipse(px - 3.0f, py - 3.0f, 6.0f, 6.0f);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// GRMeter
// ─────────────────────────────────────────────────────────────────────────────
GRMeter::GRMeter(ProCompressorAudioProcessor& p) : proc(p)
{
    startTimerHz(30);
}

void GRMeter::timerCallback()
{
    float raw = proc.gainReductionDb.load(std::memory_order_relaxed);
    // Smooth display value (fast attack, slow release)
    if (raw < displayGR)
        displayGR = raw;
    else
        displayGR += (raw - displayGR) * 0.05f;   // gentle release

    repaint();
}

void GRMeter::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(0xff0d0d1a));
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(juce::Colour(0xff2a2a4a));
    g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);

    // Header label
    const float headerH = 16.0f;
    g.setColour(Colors::labelText);
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("GR", b.removeFromTop(headerH), juce::Justification::centred);

    const float footerH = 14.0f;
    auto footerBounds = b.removeFromBottom(footerH);

    // Scale lines + dB markers
    const float meterH = b.getHeight();
    const float meterX = b.getX();
    const float meterW = b.getWidth();

    const int marks[] = { 0, 3, 6, 9, 12, 18, 24 };
    for (int db : marks)
    {
        float y = b.getY() + (db / kMaxDb) * meterH;
        g.setColour(juce::Colour(0x40ffffff));
        g.drawHorizontalLine(juce::roundToInt(y), meterX, meterX + meterW * 0.4f);
    }

    // GR fill
    float clampedGR = juce::jlimit(-kMaxDb, 0.0f, displayGR);
    float fillRatio = -clampedGR / kMaxDb;
    float fillH     = meterH * fillRatio;

    if (fillH > 0.0f)
    {
        float greenH  = std::min(fillH, meterH * (6.0f  / kMaxDb));
        float yellowH = std::max(0.0f, std::min(fillH - greenH, meterH * (6.0f  / kMaxDb)));
        float redH    = std::max(0.0f, fillH - greenH - yellowH);

        float top = b.getY();
        if (greenH > 0.0f)
        {
            g.setColour(Colors::grGreen);
            g.fillRect(meterX + 2.0f, top, meterW - 4.0f, greenH);
            top += greenH;
        }
        if (yellowH > 0.0f)
        {
            g.setColour(Colors::grYellow);
            g.fillRect(meterX + 2.0f, top, meterW - 4.0f, yellowH);
            top += yellowH;
        }
        if (redH > 0.0f)
        {
            g.setColour(Colors::grRed);
            g.fillRect(meterX + 2.0f, top, meterW - 4.0f, redH);
        }
    }

    // dB readout
    g.setColour(Colors::valueText);
    g.setFont(juce::Font(9.0f));
    g.drawText(juce::String(std::abs(clampedGR), 1) + " dB",
               footerBounds, juce::Justification::centred);
}

void GRMeter::resized() {}

// ─────────────────────────────────────────────────────────────────────────────
// KnobWithLabel
// ─────────────────────────────────────────────────────────────────────────────
KnobWithLabel::KnobWithLabel(const juce::String& labelText, juce::Colour trackColour)
{
    // Attach a custom LookAndFeel to this slider
    auto* laf = new CompressorLookAndFeel(trackColour);
    slider.setLookAndFeel(laf);

    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    slider.setColour(juce::Slider::textBoxTextColourId,     Colors::valueText);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff0f0f1a));
    slider.setColour(juce::Slider::textBoxOutlineColourId,  juce::Colour(0x00000000));

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(9.5f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, Colors::labelText);

    addAndMakeVisible(slider);
    addAndMakeVisible(label);
}

void KnobWithLabel::resized()
{
    auto b = getLocalBounds();
    label.setBounds(b.removeFromBottom(16));
    slider.setBounds(b);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main editor
// ─────────────────────────────────────────────────────────────────────────────
ProCompressorAudioProcessorEditor::ProCompressorAudioProcessorEditor(
    ProCompressorAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), grMeter(p)
{
    // Attach sliders to APVTS
    auto& apvts = audioProcessor.apvts;
    threshAtt  = std::make_unique<Attachment>(apvts, "threshold",  thresholdKnob.slider);
    ratioAtt   = std::make_unique<Attachment>(apvts, "ratio",      ratioKnob.slider);
    attackAtt  = std::make_unique<Attachment>(apvts, "attack",     attackKnob.slider);
    releaseAtt = std::make_unique<Attachment>(apvts, "release",    releaseKnob.slider);
    kneeAtt    = std::make_unique<Attachment>(apvts, "knee",       kneeKnob.slider);
    makeupAtt  = std::make_unique<Attachment>(apvts, "makeupGain", makeupKnob.slider);
    mixAtt     = std::make_unique<Attachment>(apvts, "mix",        mixKnob.slider);
    scAtt      = std::make_unique<BtnAttach> (apvts, "sidechain",  sidechainBtn);

    // Sidechain button style
    sidechainBtn.setColour(juce::ToggleButton::textColourId,    Colors::labelText);
    sidechainBtn.setColour(juce::ToggleButton::tickColourId,    Colors::accent);
    sidechainBtn.setColour(juce::ToggleButton::tickDisabledColourId, Colors::knobTrack);

    for (auto* knob : { &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob,
                        &kneeKnob, &makeupKnob, &mixKnob })
        addAndMakeVisible(*knob);

    addAndMakeVisible(sidechainBtn);
    addAndMakeVisible(grMeter);

    setSize(640, 260);
}

ProCompressorAudioProcessorEditor::~ProCompressorAudioProcessorEditor()
{
    // Release LookAndFeels on knob sliders before destruction
    thresholdKnob.slider.setLookAndFeel(nullptr);
    ratioKnob.slider.setLookAndFeel(nullptr);
    attackKnob.slider.setLookAndFeel(nullptr);
    releaseKnob.slider.setLookAndFeel(nullptr);
    kneeKnob.slider.setLookAndFeel(nullptr);
    makeupKnob.slider.setLookAndFeel(nullptr);
    mixKnob.slider.setLookAndFeel(nullptr);
}

void ProCompressorAudioProcessorEditor::paint(juce::Graphics& g)
{
    // ── Background ───────────────────────────────────────────────────────
    g.fillAll(Colors::bg);

    // ── Header bar ───────────────────────────────────────────────────────
    juce::Rectangle<float> header(0.0f, 0.0f, (float)getWidth(), 36.0f);
    g.setColour(Colors::panelBg);
    g.fillRect(header);

    // Title
    g.setColour(Colors::accent);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("PRO COMPRESSOR", header.reduced(12.0f, 0.0f),
               juce::Justification::centredLeft);

    // Subtle version tag
    g.setColour(Colors::labelText);
    g.setFont(juce::Font(10.0f));
    g.drawText("v1.0  |  VST3", header.reduced(12.0f, 0.0f),
               juce::Justification::centredRight);

    // Separator line
    g.setColour(Colors::accent.withAlpha(0.3f));
    g.drawHorizontalLine(36, 0.0f, (float)getWidth());

    // ── Knobs section background ─────────────────────────────────────────
    g.setColour(Colors::panelBg.withAlpha(0.5f));
    g.fillRoundedRectangle(8.0f, 44.0f, getWidth() - 70.0f, getHeight() - 52.0f, 6.0f);
}

void ProCompressorAudioProcessorEditor::resized()
{
    const int headerH = 36;
    const int pad     = 14;
    const int meterW  = 50;

    // GR meter on the right
    grMeter.setBounds(getWidth() - meterW - 6, headerH + 6,
                      meterW, getHeight() - headerH - 12);

    // Knob area
    auto knobArea = juce::Rectangle<int>(pad, headerH + pad,
                                         getWidth() - meterW - pad * 2 - 10,
                                         getHeight() - headerH - pad * 2);

    const int knobW    = knobArea.getWidth() / 7;
    const int btnH     = 22;
    const int knobH    = knobArea.getHeight() - btnH - 6;

    // 7 knobs in a row
    KnobWithLabel* knobs[] = {
        &thresholdKnob, &ratioKnob, &attackKnob, &releaseKnob,
        &kneeKnob, &makeupKnob, &mixKnob
    };

    for (int i = 0; i < 7; ++i)
    {
        knobs[i]->setBounds(knobArea.getX() + i * knobW,
                             knobArea.getY(),
                             knobW, knobH);
    }

    // Sidechain button bottom-right of the knob area
    sidechainBtn.setBounds(knobArea.getRight() - 90,
                            knobArea.getBottom() - btnH,
                            90, btnH);
}
