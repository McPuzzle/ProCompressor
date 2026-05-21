#include "PluginProcessor.h"
#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

ProCompressorAudioProcessor::ProCompressorAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",     juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output",    juce::AudioChannelSet::stereo(), true)
                         .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

ProCompressorAudioProcessor::~ProCompressorAudioProcessor() {}

// ─────────────────────────────────────────────────────────────────────────────
// Parameter layout
// ─────────────────────────────────────────────────────────────────────────────

juce::AudioProcessorValueTreeState::ParameterLayout
ProCompressorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "threshold", "Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -18.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ratio", "Ratio",
        juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f), 4.0f,
        juce::AudioParameterFloatAttributes().withLabel(":1")));

    // Skewed so low values feel responsive
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack",
        juce::NormalisableRange<float>(0.1f, 200.0f, 0.1f, 0.5f), 10.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "release", "Release",
        juce::NormalisableRange<float>(5.0f, 2000.0f, 1.0f, 0.5f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "knee", "Knee",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 6.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "makeupGain", "Makeup",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "sidechain", "Sidechain", false));

    return { params.begin(), params.end() };
}

// ─────────────────────────────────────────────────────────────────────────────
// Boilerplate
// ─────────────────────────────────────────────────────────────────────────────

const juce::String ProCompressorAudioProcessor::getName() const  { return JucePlugin_Name; }
bool ProCompressorAudioProcessor::acceptsMidi()  const           { return false; }
bool ProCompressorAudioProcessor::producesMidi() const           { return false; }
bool ProCompressorAudioProcessor::isMidiEffect() const           { return false; }
double ProCompressorAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int  ProCompressorAudioProcessor::getNumPrograms()               { return 1; }
int  ProCompressorAudioProcessor::getCurrentProgram()            { return 0; }
void ProCompressorAudioProcessor::setCurrentProgram(int)         {}
const juce::String ProCompressorAudioProcessor::getProgramName(int) { return {}; }
void ProCompressorAudioProcessor::changeProgramName(int, const juce::String&) {}

// ─────────────────────────────────────────────────────────────────────────────
// Prepare / Release
// ─────────────────────────────────────────────────────────────────────────────

void ProCompressorAudioProcessor::prepareToPlay(double sampleRate, int /*block*/)
{
    sampleRate_ = sampleRate;
    envelope.assign(2, 0.0);  // stereo
}

void ProCompressorAudioProcessor::releaseResources() {}

bool ProCompressorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto mainIn  = layouts.getMainInputChannelSet();
    auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)                                    return false;
    if (mainIn != juce::AudioChannelSet::mono() &&
        mainIn != juce::AudioChannelSet::stereo())            return false;

    // Sidechain bus may be mono, stereo, or disabled
    auto sc = layouts.getChannelSet(true, 1);
    if (!sc.isDisabled() &&
        sc != juce::AudioChannelSet::mono() &&
        sc != juce::AudioChannelSet::stereo())                return false;

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Gain computer (static helper)
//
//  Standard soft-knee formula (AES / Zolzer):
//      below knee  → no gain change
//      in knee     → quadratic interpolation
//      above knee  → linear compression
//
//  Returns gain reduction in dB (always <= 0).
// ─────────────────────────────────────────────────────────────────────────────

float ProCompressorAudioProcessor::gainComputer(float xDb, float threshold,
                                                float ratio, float knee) noexcept
{
    const float overshoot = xDb - threshold;
    const float halfKnee  = knee * 0.5f;

    if (2.0f * overshoot < -knee)
        return 0.0f;   // below knee: pass-through

    if (knee > 0.0f && 2.0f * std::abs(overshoot) <= knee)
    {
        // Soft knee region — quadratic interpolation
        float t = overshoot + halfKnee;
        return (1.0f / ratio - 1.0f) * (t * t) / (2.0f * knee);
    }

    // Above knee — standard compression
    return overshoot * (1.0f / ratio - 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Process block
// ─────────────────────────────────────────────────────────────────────────────

void ProCompressorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // ── Read parameters ────────────────────────────────────────────────────
    const float threshold = *apvts.getRawParameterValue("threshold");
    const float ratio     = *apvts.getRawParameterValue("ratio");
    const float attackMs  = *apvts.getRawParameterValue("attack");
    const float releaseMs = *apvts.getRawParameterValue("release");
    const float knee      = *apvts.getRawParameterValue("knee");
    const float makeupDb  = *apvts.getRawParameterValue("makeupGain");
    const float mixRatio  = *apvts.getRawParameterValue("mix") * 0.01f;
    const bool  scOn      = *apvts.getRawParameterValue("sidechain") > 0.5f;

    // ── Time constants (one-pole IIR coefficients) ────────────────────────
    //   coeff = exp(-1 / (time_s * sampleRate))
    //   smaller time → smaller coeff → faster tracking
    const double aCoeff = std::exp(-1.0 / (attackMs  * 0.001 * sampleRate_));
    const double rCoeff = std::exp(-1.0 / (releaseMs * 0.001 * sampleRate_));
    const float  makeup = juce::Decibels::decibelsToGain(makeupDb);

    // ── Get bus buffers ────────────────────────────────────────────────────
    auto mainBuf = getBusBuffer(buffer, true,  0);
    auto scBuf   = getBusBuffer(buffer, true,  1);   // may be empty if SC bus disabled
    auto outBuf  = getBusBuffer(buffer, false, 0);

    const int numSamples  = mainBuf.getNumSamples();
    const int numChannels = mainBuf.getNumChannels();

    if ((int)envelope.size() < numChannels)
        envelope.resize(numChannels, 0.0);

    float peakGR = 0.0f;   // for GR meter

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* mainIn = mainBuf.getReadPointer(ch);

        // Sidechain source: external SC bus (if enabled & active) else main input
        const float* scIn = (scOn && scBuf.getNumChannels() > 0)
                                ? scBuf.getReadPointer(std::min(ch, scBuf.getNumChannels() - 1))
                                : mainIn;

        float* out = outBuf.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = mainIn[i];

            // ── 1. Peak envelope follower ──────────────────────────────────
            //   Rising edge  → track with attack coefficient
            //   Falling edge → decay with release coefficient
            const double absIn = std::abs(static_cast<double>(scIn[i]));
            if (absIn > envelope[ch])
                envelope[ch] = aCoeff * envelope[ch] + (1.0 - aCoeff) * absIn;
            else
                envelope[ch] = rCoeff * envelope[ch] + (1.0 - rCoeff) * absIn;

            // ── 2. Level → dB (floor at -120 dB) ──────────────────────────
            const float levelDb = juce::Decibels::gainToDecibels(
                static_cast<float>(envelope[ch]), -120.0f);

            // ── 3. Gain computer (soft knee) ───────────────────────────────
            const float grDb = gainComputer(levelDb, threshold, ratio, knee);

            // ── 4. Apply gain reduction + makeup ──────────────────────────
            const float gainLin = juce::Decibels::decibelsToGain(grDb) * makeup;
            const float wet     = dry * gainLin;

            // ── 5. Parallel mix (dry/wet blend) ───────────────────────────
            out[i] = dry * (1.0f - mixRatio) + wet * mixRatio;

            if (grDb < peakGR) peakGR = grDb;
        }
    }

    gainReductionDb.store(peakGR, std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────────────────
// State save / load
// ─────────────────────────────────────────────────────────────────────────────

void ProCompressorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void ProCompressorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

bool ProCompressorAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ProCompressorAudioProcessor::createEditor()
{
    return new ProCompressorAudioProcessorEditor(*this);
}

// Required factory function
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ProCompressorAudioProcessor();
}
