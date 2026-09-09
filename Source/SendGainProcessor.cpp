#include "SendGainProcessor.h"

SendGainProcessor::SendGainProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

SendGainProcessor::~SendGainProcessor() = default;

void SendGainProcessor::prepareToPlay (double, int) {}
void SendGainProcessor::releaseResources() {}

void SendGainProcessor::setLevelDb (float newLevelDb)
{
    gain.store (newLevelDb <= silenceThresholdDb ? 0.0f : juce::Decibels::decibelsToGain (newLevelDb));
}

void SendGainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.applyGain (gain.load());
}

const juce::String SendGainProcessor::getName() const   { return "Send"; }
double SendGainProcessor::getTailLengthSeconds() const   { return 0.0; }
bool SendGainProcessor::acceptsMidi() const              { return false; }
bool SendGainProcessor::producesMidi() const             { return false; }

juce::AudioProcessorEditor* SendGainProcessor::createEditor() { return nullptr; }
bool SendGainProcessor::hasEditor() const                     { return false; }

int SendGainProcessor::getNumPrograms()                        { return 1; }
int SendGainProcessor::getCurrentProgram()                     { return 0; }
void SendGainProcessor::setCurrentProgram (int)                {}
const juce::String SendGainProcessor::getProgramName (int)     { return {}; }
void SendGainProcessor::changeProgramName (int, const juce::String&) {}

void SendGainProcessor::getStateInformation (juce::MemoryBlock&) {}
void SendGainProcessor::setStateInformation (const void*, int)   {}
