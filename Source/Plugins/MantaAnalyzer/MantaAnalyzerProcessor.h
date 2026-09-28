#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "AnalyzerFifo.h"
#include "AnalyzerSettings.h"

#include <atomic>

//==============================================================================
/**
    8.329：Manta Analyzer／Ember Analyzer の本体（アナライザー設計書3章の`PluginProcessor`）。

    ### 音には触らない

    **入力をそのまま出力します**（仕様書1.2「ビットパーフェクトなスルー」）。
    `processBlock()`はバッファに**一切書き込みません**——入力と出力は同じバッファなので、
    何もしないことがそのまま「無加工で出す」になります。レイテンシーは0（`setLatencySamples()`を呼ばない）。

    ### 画面が開いているときだけ書き写す

    `editorOpen`が立っているときだけ`AnalyzerFifo`へ書きます（仕様書4.4：閉じているあいだは
    CPUを使わない）。**音のスレッドが見る値はこれだけ**です（設計書7章）。

    ### 設定はパラメータにしない

    `AnalyzerSettings`を`ValueTree`で持ち、`getStateInformation()`で保存します（仕様書6章：
    音に影響しないのでオートメーション対象にしない）。**画面のスレッドだけが触ります。**
*/
class MantaAnalyzerProcessor : public juce::AudioPluginInstance
{
public:
    MantaAnalyzerProcessor();
    ~MantaAnalyzerProcessor() override = default;

    void fillInPluginDescription (juce::PluginDescription& description) const override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //--------------------------------------------------------------------------
    // 画面から

    AnalyzerFifo& getFifo() noexcept { return fifo; }

    void setEditorOpen (bool isOpen) noexcept { editorOpen.store (isOpen); }
    bool isEditorOpen() const noexcept { return editorOpen.load(); }

    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }

    /** モノで挿されているか（仕様書7.1：モノのときは解析チャンネルの設定を無効にする）。 */
    bool isMono() const noexcept { return monoInput.load(); }

    const AnalyzerSettings& getSettings() const noexcept { return settings; }
    void setSettings (const AnalyzerSettings& newSettings);

    /** 設定が外から（読み込みで）変わったとき、画面へ知らせる。 */
    std::function<void()> onSettingsLoaded;

private:
    AnalyzerFifo fifo;
    std::atomic<bool> editorOpen { false };
    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<bool> monoInput { false };

    AnalyzerSettings settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaAnalyzerProcessor)
};
