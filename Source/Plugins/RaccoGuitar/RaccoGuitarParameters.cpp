#include "RaccoGuitarParameters.h"

namespace RaccoGuitarParams
{
    namespace
    {
        /** ブリッジからの割合。**`getPickupNames()`と同じ並び**であること（1.27）。 */
        constexpr float pickupPositions[3] = { 0.35f, 0.22f, 0.10f };
    }

    juce::StringArray getPickupNames()
    {
        return { "Front", "Center", "Rear" };
    }

    float pickupPosition (int selection)
    {
        return pickupPositions[juce::jlimit (0, 2, selection)];
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using Float = juce::AudioParameterFloat;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        //----------------------------------------------------------------------
        // 弦の音色
        layout.add (std::make_unique<Float> (
            juce::ParameterID { brightness, 1 }, "Brightness",
            juce::NormalisableRange<float> (0.05f, 0.99f, 0.0001f), 0.55f));

        // **既定は10秒**（＝最大）。かなり長いサステイン（本人の指定。KSGuitar v15）
        layout.add (std::make_unique<Float> (
            juce::ParameterID { sustain, 1 }, "Sustain",
            juce::NormalisableRange<float> (0.3f, 10.0f, 0.01f, 0.5f), 10.0f,
            juce::AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { pickPos, 1 }, "Pick Position",
            juce::NormalisableRange<float> (0.02f, 0.5f, 0.001f), 0.25f));

        // 3択。**既定はRear**（ブリッジ寄りの硬い音）
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { pickupSel, 1 }, "Pickup", getPickupNames(), 2));

        //----------------------------------------------------------------------
        // ピッキング
        layout.add (std::make_unique<Float> (
            juce::ParameterID { hardness, 1 }, "Hardness",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { attack, 1 }, "Attack",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));

        //----------------------------------------------------------------------
        // 出力
        //
        // 8.269：**既定は0.70**（Phase 271まで0.80）。
        //
        // 種を固定してから測り直したら、**E2の峰が1.0028**でした——わずかですが
        // フルスケールを超えています。種が毎回違っていた頃は0.70〜1.00を行き来していて、
        // **14回に1回だけ超える**状態だったので、気づけていませんでした。
        //
        // | | 0.80のとき | 0.70のとき |
        // |---|---|---|
        // | E2 | **1.0028**（超えている） | 0.877 |
        // | 参考：Java Rhino BassのG4 | — | 0.886 |
        //
        // **種を選び直して下に落とすのは、当たりを選んでいるだけ**です
        // （分布そのものが1.0に届いている）。音量のほうを下げました。
        // ベースと同じ扱いで、峰も並びます。
        //
        // **保存済みのプロジェクトは動きません**（保存された値が優先されます）。
        layout.add (std::make_unique<Float> (
            juce::ParameterID { gain, 1 }, "Output",
            juce::NormalisableRange<float> (0.0f, 1.5f, 0.001f), 0.70f));

        return layout;
    }
}
