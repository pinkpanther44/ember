#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

//==============================================================================
/**
    内蔵プラグインで共有するツールバー（Phase 208）。

    ```
    Undo Redo │ A Copy │ Presets                        （右は使う側の場所）
    ```

    Manta EQ（仕様書4.17）で作ったものを、2つ目の内蔵プラグイン（Manta Comp）を
    足すときに切り出しました。**同じものを2つ持つと、必ずどちらかが古くなります**（8.2）。

    ### Undo／Redoの粒度

    プラグインのパラメータは`juce::UndoManager`の対象外なので、
    **値の写しを積む**方式です。

    - 5回／秒で見比べて、**変わっていたら1つ積む**
    - **マウスのボタンが下りているあいだは積まない**
      （ドラッグの途中が刻まれると、Undoを何十回も押すことになります）

    つまり**「手を離すたびに1つ」**の粒度になります。

    ### A/B と Presets

    - **A/B**：値の写しを2つ持ち、ボタンで入れ替える
    - **Copy**：いま鳴っているほうを、もう片方へ写す（A/Bの出発点をそろえる）
    - **Presets**：`%APPDATA%\PersonalDAW\Presets\<フォルダ名>\*.xml`
      （**`apvts.state`ごと**保存するので、画面の見せ方も一緒に戻ります）

    ### 右側は使う側が使う

    `getTrailingArea()`が空いている場所を返します。**使う側が自分の部品を
    このツールバーの子として足し**、そこへ置いてください
    （Manta EQは処理モードとレイテンシー、Manta Compはサイドチェインの表示）。
*/
class MantaPluginToolbar : public juce::Component,
                            private juce::Timer
{
public:
    /** `presetFolderName`は`Presets/`の下に作るフォルダ名（例：`"MantaEQ"`）。 */
    MantaPluginToolbar (juce::AudioProcessor& processorToUse,
                         juce::AudioProcessorValueTreeState& stateToUse,
                         juce::String presetFolderName);
    ~MantaPluginToolbar() override;

    //==========================================================================
    /** 8.173：**exeに入っている出来合いの音**（Phase 213／Manta Synthesizerのため）。

        ユーザーが保存するプリセット（`%APPDATA%`のXML）とは別物です。
        シンセのように「まず何か鳴らしてみたい」ものには、
        **保存済みが1つも無い状態で開くこと自体が壁**になります。

        `apply`は**値を書くだけ**にしてください（画面の作り直しは
        `onStateRestored`が呼ばれます。ユーザープリセットと同じ道）。 */
    struct FactoryPreset
    {
        juce::String category;   ///< メニューの小分け（空なら直下に並ぶ）
        juce::String name;
        std::function<void()> apply;
    };

    /** 工場プリセットを渡す。**渡さなければメニューは今までどおり**です
        （Manta EQ・Manta Compは渡していません）。 */
    void setFactoryPresets (std::vector<FactoryPreset> presets);

    /** 値が外から入れ替わったとき（Undo・Redo・A/B・プリセット）に呼ばれる。

        **画面の作り直しは使う側の仕事**です——つまみの繋ぎ先や、
        `<UI>`から読んでいる表示は、ここでは触りません。 */
    std::function<void()> onStateRestored;

    void resized() override;

    /** ボタンの右に残っている場所（**このツールバーの座標**）。 */
    juce::Rectangle<int> getTrailingArea() const { return trailingArea; }

    /** 内蔵プラグイン共通のボタンの見た目（使う側の帯でも使えるように公開）。 */
    static void styleButton (juce::TextButton& button, const juce::String& text);

    static constexpr int preferredHeight = 32;

private:
    void timerCallback() override;

    std::vector<float> captureParameterValues() const;
    void applyParameterValues (const std::vector<float>& values);

    void pushUndoSnapshotIfChanged();
    void undo();
    void redo();
    void toggleAB();
    void copyToOtherSlot();
    void showPresetMenu();
    void applyFactoryPreset (int index);
    void savePresetAs (const juce::String& name);
    void loadPreset (const juce::File& file);
    void refreshButtons();

    juce::File getPresetFolder() const;

    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    juce::String presetFolder;

    juce::TextButton undoButton, redoButton, abButton, copyButton, presetButton;

    std::vector<std::vector<float>> undoStack, redoStack;
    std::vector<float> lastSnapshot, slotA, slotB;
    bool usingSlotB = false;

    std::vector<FactoryPreset> factoryPresets;

    juce::Rectangle<int> trailingArea;

    static constexpr size_t maxUndoSteps = 64;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaPluginToolbar)
};
