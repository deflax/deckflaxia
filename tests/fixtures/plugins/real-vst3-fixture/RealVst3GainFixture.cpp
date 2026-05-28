#include "RealVst3GainFixture.h"

namespace
{
constexpr auto gainParameterId = "gain";
constexpr auto gainDefault = 0.5f;
}

DeckflaxiaRealVst3GainProcessor::DeckflaxiaRealVst3GainProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameterLayout()),
      gain(parameters.getRawParameterValue(gainParameterId))
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DeckflaxiaRealVst3GainProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> layout;
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{gainParameterId, 1},
        "Gain",
        juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f},
        gainDefault));
    return {layout.begin(), layout.end()};
}

void DeckflaxiaRealVst3GainProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void DeckflaxiaRealVst3GainProcessor::releaseResources()
{
}

bool DeckflaxiaRealVst3GainProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return output == layouts.getMainInputChannelSet();
}

void DeckflaxiaRealVst3GainProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const auto activeGain = gain != nullptr ? gain->load(std::memory_order_relaxed) : gainDefault;
    const auto inputChannels = getTotalNumInputChannels();
    const auto outputChannels = getTotalNumOutputChannels();

    for (auto channel = inputChannels; channel < outputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    for (auto channel = 0; channel < inputChannels; ++channel)
        buffer.applyGain(channel, 0, buffer.getNumSamples(), activeGain);
}

juce::AudioProcessorEditor* DeckflaxiaRealVst3GainProcessor::createEditor()
{
    return nullptr;
}

bool DeckflaxiaRealVst3GainProcessor::hasEditor() const
{
    return false;
}

const juce::String DeckflaxiaRealVst3GainProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DeckflaxiaRealVst3GainProcessor::acceptsMidi() const
{
    return false;
}

bool DeckflaxiaRealVst3GainProcessor::producesMidi() const
{
    return false;
}

bool DeckflaxiaRealVst3GainProcessor::isMidiEffect() const
{
    return false;
}

double DeckflaxiaRealVst3GainProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DeckflaxiaRealVst3GainProcessor::getNumPrograms()
{
    return 1;
}

int DeckflaxiaRealVst3GainProcessor::getCurrentProgram()
{
    return 0;
}

void DeckflaxiaRealVst3GainProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String DeckflaxiaRealVst3GainProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void DeckflaxiaRealVst3GainProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void DeckflaxiaRealVst3GainProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto state = parameters.copyState();
    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DeckflaxiaRealVst3GainProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DeckflaxiaRealVst3GainProcessor();
}
