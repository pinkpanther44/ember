#include "SandboxedPluginProcessor.h"

SandboxedPluginProcessor::SandboxedPluginProcessor (PluginSandboxHost& hostToUse)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      sandboxHost (hostToUse)
{
}

void SandboxedPluginProcessor::prepareToPlay (double, int)
{
}

void SandboxedPluginProcessor::releaseResources()
{
}

void SandboxedPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // 共有メモリ経由で子プロセスへ処理を依頼する（設計書3.4）。
    // 失敗した場合（子プロセスが落ちた・応答しない等）は、
    // バッファをそのまま通す＝バイパス動作にする。
    // 無音にするより、少なくとも元の音が残る方が作業への影響が小さい。
    sandboxHost.processBlockViaSandbox (buffer);
}

const juce::String SandboxedPluginProcessor::getName() const     { return "SandboxedPlugin"; }
double SandboxedPluginProcessor::getTailLengthSeconds() const     { return 0.0; }
bool SandboxedPluginProcessor::acceptsMidi() const                { return false; }
bool SandboxedPluginProcessor::producesMidi() const                { return false; }

juce::AudioProcessorEditor* SandboxedPluginProcessor::createEditor() { return nullptr; }
bool SandboxedPluginProcessor::hasEditor() const                      { return false; }

int SandboxedPluginProcessor::getNumPrograms()                          { return 1; }
int SandboxedPluginProcessor::getCurrentProgram()                       { return 0; }
void SandboxedPluginProcessor::setCurrentProgram (int)                  {}
const juce::String SandboxedPluginProcessor::getProgramName (int)       { return {}; }
void SandboxedPluginProcessor::changeProgramName (int, const juce::String&) {}

void SandboxedPluginProcessor::getStateInformation (juce::MemoryBlock&) {}
void SandboxedPluginProcessor::setStateInformation (const void*, int)   {}
