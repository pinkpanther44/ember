#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginSandboxHost.h"

#include <memory>

//==============================================================================
/**
    設計書1.2・3.3の`PluginHostProxy`。**グラフから見ると、普通のプラグイン**です。

    8.260：**Phase 268で「挿して使える」ところまで広げました**。

    ```
    グラフ ──▶ SandboxedPluginProcessor ──共有メモリ──▶ 子プロセスの実体
                 ├ パラメータ（同じ数・同じ名前で立てる）
                 ├ 状態（保存のときに取り出し、復元のときに入れる）
                 ├ MIDI（音源も通せる）
                 └ レイテンシ（プラグインの申告＋1ブロック）
    ```

    ### なぜ`AudioPluginInstance`なのか

    `fillInPluginDescription()`が要るからです。プロジェクトに書かれるのは
    `PluginDescription`＋`getStateInformation()`の中身（設計書3.8）なので、
    **ここが本物と同じ説明を返せば、保存も復元もいままでの道**を通ります——
    「ネイティブで読んだか、サンドボックス越しか」を上位は知りません（1.2の狙い）。

    ### パラメータを同じ数だけ立てる

    オートメーションは**番号**で覚えています（`insert:スロット:番号`。仕様書5.6）。
    プロキシのパラメータが0個だと、**サンドボックスへ回した瞬間に
    オートメーションが全部外れます**。

    子から受け取った表のぶんだけ`SandboxedParameter`を立て、
    動かされたら次のブロックで子へ送ります。

    > おまけ：パラメータがあるので、`hasEditor()`がfalseでも
    > **本体が`GenericAudioProcessorEditor`を出してくれます**（`AudioEngine`の3252行）。
    > プラグイン本来のGUIはPhase 269（8.259の段階表のC）。

    ### 子が落ちたら

    `processBlock()`は**入力をそのまま通します**（バイパス）。音は変わりますが、
    **DAWは止まりません**。落ちたことは`onSandboxCrashed`で上へ伝えます。
*/
class SandboxedPluginEditor;

class SandboxedPluginProcessor : public juce::AudioPluginInstance,
                                  private juce::AsyncUpdater
{
public:
    /** 子プロセスを起こしてプラグインを読み、**成功したときだけ**中身を返す。

        **メッセージスレッドから呼ぶこと**（返事を待ちます）。 */
    static std::unique_ptr<SandboxedPluginProcessor> create (const juce::PluginDescription& description,
                                                              double sampleRate, int blockSize,
                                                              juce::String& errorMessage);

    ~SandboxedPluginProcessor() override;

    void fillInPluginDescription (juce::PluginDescription& description) const override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    const juce::String getName() const override;
    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override;

    /** 子のプラグインが自分の画面を持っているか（8.262/Phase 269）。 */
    bool hasEditor() const override { return pluginInfo.hasEditor; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    /** 子プロセスが落ちた（＝プラグインが落ちた）。**メッセージスレッドで呼ばれます**。 */
    std::function<void()> onSandboxCrashed;

    /** いま子プロセスが生きているか。 */
    bool isSandboxAlive() const { return host.isAlive(); }

    /** 間に合わなかったブロックの数（`PluginSandboxHost::getDropoutCount()`）。 */
    int getDropoutCount() const { return host.getDropoutCount(); }

    /** 行列に入り切らずに捨てたMIDIの数（**0であるべき数**。8.263）。 */
    juce::uint32 getMidiDropCount() const { return host.getMidiDropCount(); }

    /** 子がまだ居るかを、その場で確かめる（`--sandbox-selftest`が使います）。 */
    bool probeSandbox() { return host.probeWorker(); }

    /** 本来のGUIがはめ込めているか（`--sandbox-selftest`が使います）。 */
    bool isEditorEmbedded() const { return editorIsEmbedded; }

    /** 開いている画面が自分を名乗ります（落ちたことを伝えるため）。

        **メッセージスレッドだけが触ります**——画面の作成も破棄もそこだけです。 */
    void setActiveSandboxEditor (SandboxedPluginEditor* editor, bool embedded);

private:
    SandboxedPluginProcessor (const juce::PluginDescription& description,
                               PluginSandboxHost::LoadedPluginInfo info);

    void handleAsyncUpdate() override;

    //==========================================================================
    /** 子のパラメータ1つに対応する、こちら側のパラメータ。

        **`HostedParameter`であること。** `AudioPluginInstance`は`addParameter()`を
        隠していて、`addHostedParameter()`しか受け付けません——
        ホストしているものは**番号ではなくID**で覚えるべき、という考えからです
        （プラグインの版が上がると番号は動きます）。 */
    class SandboxedParameter final : public juce::AudioPluginInstance::HostedParameter
    {
    public:
        SandboxedParameter (PluginSandboxHost& hostToUse, int indexToUse,
                             PluginSandboxHost::LoadedPluginInfo::Parameter info);

        juce::String getParameterID() const override { return details.id; }

        float getValue() const override { return value.load(); }
        void setValue (float newValue) override;

        float getDefaultValue() const override { return details.defaultValue; }
        juce::String getName (int maximumStringLength) const override;
        juce::String getLabel() const override { return details.label; }
        int getNumSteps() const override;
        bool isDiscrete() const override { return details.isDiscrete; }
        juce::String getText (float normalised, int maximumStringLength) const override;
        float getValueForText (const juce::String& text) const override;

        /** 子のGUIで動いたぶん。**送り返しません**（往復すると振動します）。 */
        void setValueFromWorker (float newValue) { value.store (newValue); }

    private:
        PluginSandboxHost& host;
        int index;
        PluginSandboxHost::LoadedPluginInfo::Parameter details;
        std::atomic<float> value;
    };

    //==========================================================================
    juce::PluginDescription pluginDescription;
    PluginSandboxHost::LoadedPluginInfo pluginInfo;

    PluginSandboxHost host;

    std::vector<SandboxedParameter*> sandboxedParameters;

    std::atomic<bool> crashed { false };

    SandboxedPluginEditor* activeSandboxEditor = nullptr;
    bool editorIsEmbedded = false;

    /** 子から返ってきたパラメータ変化を読む置き場（**音のスレッドで確保しない**）。 */
    std::vector<SandboxIPC::ParamChange> incomingChanges;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SandboxedPluginProcessor)
};
