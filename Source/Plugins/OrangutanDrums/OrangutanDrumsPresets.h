#pragma once

#include "../MantaFactoryPresets.h"
#include "OrangutanDrumsKits.h"
#include "OrangutanDrumsParameters.h"

//==============================================================================
/**
    8.288：**Orangutan Drums の工場プリセット**（Phase 281）。

    ほかの7つは`{"パラメータID", 値}`を手で並べています（`JavaRhinoBassPresets.h`）。
    **ここだけ組み立てているのは、1つが133行あるから**です——
    10キットで1330行になり、パッドのつまみを1つ足すたびに全部を書き直すことになります。

    出どころは`OrangutanDrumsKits.h`の表1つ。**IDの綴りは`padId()`だけ**です。

    ### 書かない値

    `VOLUME`とベースゾーンの3つは**表に入れていません**。
    `applyValues()`は書いていないものを**既定値へ戻す**ので、
    キットを選ぶたびに出力とベースゾーンが既定へ戻ります——
    これは意図どおりです（キットは「音色一式」で、出口の設定ではありません）。

    > **入れてしまうと、キットを選ぶたびに出力が跳ねます。**
    > 音色を選んだだけのつもりで音量が変わるのは、いちばん驚くところです。
*/
namespace OrangutanDrumsPresets
{
    inline const std::vector<MantaFactoryPresets::Preset>& all()
    {
        using namespace OrangutanDrumsParams;

        // **`static`であること**（`makeToolbarPresets()`が表の要素を参照で掴みます。
        // `MantaFactoryPresets.h`）
        static const std::vector<MantaFactoryPresets::Preset> presets = []
        {
            std::vector<MantaFactoryPresets::Preset> result;
            result.reserve ((size_t) OrangutanDrumsKits::numKits);

            for (const auto& kit : OrangutanDrumsKits::kits)
            {
                MantaFactoryPresets::Preset preset;
                preset.name = kit.name;
                preset.category = {};   // 10個なので、小分けせずメニューへ直に並べます

                for (int pad = 0; pad < numPads; ++pad)
                {
                    const auto& padValues = kit.pads[pad];

                    // 選択肢は**番号**を書きます（`MantaFactoryPresets.h`）
                    preset.values[padId (pad, padEngine)] = (float) padValues.engine;
                    preset.values[padId (pad, padTune)]   = padValues.tune;
                    preset.values[padId (pad, padDecay)]  = padValues.decay;
                    preset.values[padId (pad, padTone)]   = padValues.tone;
                    preset.values[padId (pad, padSnap)]   = padValues.snap;
                    preset.values[padId (pad, padLevel)]  = padValues.level;
                    preset.values[padId (pad, padPan)]    = padValues.pan;
                    preset.values[padId (pad, padSend)]   = padValues.send;
                }

                preset.values[drive]  = kit.drive;
                preset.values[glue]   = kit.glue;
                preset.values[reverb] = kit.reverb;
                preset.values[size]   = kit.size;
                preset.values[damp]   = kit.damp;

                result.push_back (std::move (preset));
            }

            return result;
        }();

        return presets;
    }
}
