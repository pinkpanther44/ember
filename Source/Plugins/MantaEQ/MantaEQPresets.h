#pragma once

#include "../MantaFactoryPresets.h"
#include "MantaEQParameters.h"

//==============================================================================
/**
    8.259：**Manta EQ の工場プリセット**（Phase 267／本人の要望）。

    作りは`MantaFactoryPresets.h`のとおり（値の表。書いていないものは既定値へ）。

    ### バンドは「使うぶんだけ点ける」

    12本あるバンドは**既定で消えています**（`bandEnabled`が`false`）。
    表に書いたバンドだけ`enabled`を1にして、周波数・ゲイン・Q・形を入れます。
    **書かなかったバンドは消えたまま**なので、前に選んだプリセットのバンドは残りません。

    ### 出発点であって、当てて終わりではありません

    EQは**素材で決まる**ものなので、ここにあるのは「だいたいこの辺から始める」という値です。
    曲線はグラフで直せます——**プリセットは、どのバンドを使うかの提案**だと思ってください。
*/
namespace MantaEQPresets
{
    /** 表に書くバンド1本ぶん。**`index`は0から**（画面の「Band 1」が0）。 */
    struct BandSetting
    {
        int   index;
        MantaEQParams::Shape shape;
        float frequency;
        float gainDb;
        float q;
    };

    /** バンドの並びを、パラメータの表に変換する。 */
    inline std::map<juce::String, float> bands (std::initializer_list<BandSetting> settings)
    {
        std::map<juce::String, float> values;

        for (const auto& setting : settings)
        {
            values[MantaEQParams::bandParamId (setting.index, MantaEQParams::bandEnabled)] = 1.0f;
            values[MantaEQParams::bandParamId (setting.index, MantaEQParams::bandShape)] = (float) setting.shape;
            values[MantaEQParams::bandParamId (setting.index, MantaEQParams::bandFreq)] = setting.frequency;
            values[MantaEQParams::bandParamId (setting.index, MantaEQParams::bandGain)] = setting.gainDb;
            values[MantaEQParams::bandParamId (setting.index, MantaEQParams::bandQ)] = setting.q;
        }

        return values;
    }

    inline const std::vector<MantaFactoryPresets::Preset>& all()
    {
        using Shape = MantaEQParams::Shape;

        // **`static`であること**（`makeToolbarPresets()`が参照で掴みます）
        static const std::vector<MantaFactoryPresets::Preset> presets
        {
            { "Vocal Air", "Vocal",
              bands ({ { 0, Shape::lowCut,    80.0f,   0.0f, 0.707f },
                       { 1, Shape::bell,     300.0f,  -3.0f, 1.2f },
                       { 2, Shape::bell,    3000.0f,   2.0f, 1.0f },
                       { 3, Shape::highShelf, 10000.0f, 3.5f, 0.707f } }) },

            { "Vocal Clean Up", "Vocal",
              bands ({ { 0, Shape::lowCut,   100.0f,   0.0f, 0.707f },
                       { 1, Shape::bell,     450.0f,  -4.0f, 2.0f },
                       { 2, Shape::bell,    1000.0f,  -2.0f, 1.6f },
                       { 3, Shape::bell,    2500.0f,   1.5f, 1.0f } }) },

            { "Kick Punch", "Drums",
              bands ({ { 0, Shape::lowShelf,  60.0f,   4.0f, 0.707f },
                       { 1, Shape::bell,     350.0f,  -4.0f, 1.5f },
                       { 2, Shape::bell,    3500.0f,   3.0f, 1.2f } }) },

            { "Snare Crack", "Drums",
              bands ({ { 0, Shape::lowCut,   120.0f,   0.0f, 0.707f },
                       { 1, Shape::bell,     200.0f,   3.0f, 1.4f },
                       { 2, Shape::bell,    5000.0f,   4.0f, 1.0f },
                       { 3, Shape::highShelf, 12000.0f, 2.0f, 0.707f } }) },

            { "Bass Tighten", "Bass",
              bands ({ { 0, Shape::lowCut,    35.0f,   0.0f, 0.707f },
                       { 1, Shape::bell,      90.0f,   2.5f, 1.2f },
                       { 2, Shape::bell,     400.0f,  -3.0f, 1.8f },
                       { 3, Shape::bell,    1600.0f,   2.0f, 1.0f } }) },

            { "Guitar Body Cut", "Guitar",
              bands ({ { 0, Shape::lowCut,    90.0f,   0.0f, 0.707f },
                       { 1, Shape::bell,     250.0f,  -3.5f, 1.6f },
                       { 2, Shape::bell,    1200.0f,   1.5f, 1.0f },
                       { 3, Shape::highShelf, 8000.0f,  2.0f, 0.707f } }) },

            { "Mix Bus Glue", "Bus",
              bands ({ { 0, Shape::lowCut,    25.0f,   0.0f, 0.707f },
                       { 1, Shape::lowShelf, 100.0f,   1.0f, 0.707f },
                       { 2, Shape::bell,     400.0f,  -1.5f, 1.2f },
                       { 3, Shape::highShelf, 12000.0f, 1.5f, 0.707f } }) },

            // 1本だけ。**傾けるEQ**は「全体を少し明るく／暗く」に効きます
            { "Tilt Bright", "Bus",
              bands ({ { 0, Shape::tiltShelf, 1000.0f, 2.0f, 0.707f } }) },
        };

        return presets;
    }
}
