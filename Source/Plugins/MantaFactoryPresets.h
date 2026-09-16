#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaPluginToolbar.h"

#include <map>
#include <vector>

//==============================================================================
/**
    8.259：**内蔵プラグインの工場プリセット、その共通部分**（Phase 267／本人の要望）。

    Phase 213で`Manta Synthesizer`に156個入れたときの作り（`MantaSynthPresets.h`）を、
    **ほかの6つでも使えるように出しました**。同じ`apply()`を7回書くと、
    必ずどれかが古くなります（1.27）。

    ### なぜ「値の表」なのか（XMLではなく）

    `MantaPluginToolbar`が保存するユーザープリセットは`apvts.state`ごとのXMLです。
    工場プリセットは**そうしていません**——`%APPDATA%`へファイルを撒くことになり、

    - **アプリを消しても残ります**（次に入れたとき、古い版のものが混ざる）
    - 本人が保存したものと**同じ場所に並んで見分けが付きません**
    - パラメータを1つ足すたびに、全部を作り直すことになります

    表なら**exeの中**にあり、書いていないパラメータは既定値へ戻るので、
    パラメータが増えても壊れません。

    ### 「書いていないものは既定値へ」

    `apply()`は**まず全部を既定値へ戻してから**、表にある値だけを書きます。
    戻さないと、**前に選んだプリセットの設定が残ります**
    （「短いルームを選んだのに、さっきの残響が伸びたまま」）。

    ### 値の書き方

    `{"パラメータID", 実値}`です。**0..1の正規化値ではありません**——
    `apply()`が`convertTo0to1()`を通すので、**範囲を変えたときに表のほうが追従します**。

    選択肢（`juce::AudioParameterChoice`）は**番号**、ON/OFFは**0か1**を書きます。
*/
namespace MantaFactoryPresets
{
    struct Preset
    {
        juce::String name;
        juce::String category;   ///< メニューの小分け（空ならメニューの直下）
        std::map<juce::String, float> values;
    };

    /** 全パラメータを既定値へ戻す。 */
    inline void resetToDefaults (juce::AudioProcessorValueTreeState& apvts)
    {
        for (auto* parameter : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
    }

    /** 表にある値だけを当てる（既定値へ戻してから上書き）。

        **`setValueNotifyingHost()`を通すこと。** つまみもオートメーションの記録も
        ここを見ているので、`apvts.state`へ直に書くと画面が追いつきません。 */
    inline void applyValues (juce::AudioProcessorValueTreeState& apvts,
                              const std::map<juce::String, float>& values)
    {
        resetToDefaults (apvts);

        for (const auto& pair : values)
            if (auto* parameter = apvts.getParameter (pair.first))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (pair.second));
    }

    inline void apply (juce::AudioProcessorValueTreeState& apvts, const Preset& preset)
    {
        applyValues (apvts, preset.values);
    }

    /** 表を、ツールバーが飲み込める形にする。

        > **表は`static`な入れ物に置くこと。** ここで作る関数は表の要素を
        > **参照で掴みます**（156個ぶんの`std::map`を人数分コピーしないため）。
        > 各プラグインの`all()`が`static const std::vector<Preset>`を返すのは、そのためです。 */
    inline std::vector<MantaPluginToolbar::FactoryPreset>
        makeToolbarPresets (juce::AudioProcessorValueTreeState& apvts,
                             const std::vector<Preset>& presets)
    {
        std::vector<MantaPluginToolbar::FactoryPreset> result;
        result.reserve (presets.size());

        for (const auto& preset : presets)
            result.push_back ({ preset.category, preset.name,
                                 [&apvts, &preset] { apply (apvts, preset); } });

        return result;
    }
}
