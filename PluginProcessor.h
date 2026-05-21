#pragma once
#include <JuceHeader.h>

class ProCompressorAudioProcessor : public juce::AudioProcessor
{
public:
    ProCompressorAudioProcessor();
    ~ProCompressorAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Live gain reduction in dB (always <= 0) — read by GR meter in UI
    std::atomic<float> gainReductionDb { 0.0f };

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Soft-knee gain computer: returns gain reduction in dB (<=0)
    static float gainComputer(float inputDb, float threshold, float ratio, float knee) noexcept;

    std::vector<double> envelope;   // per-channel peak envelope
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProCompressorAudioProcessor)
};
