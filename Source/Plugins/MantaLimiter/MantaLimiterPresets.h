#pragma once

#include "../MantaFactoryPresets.h"
#include "MantaLimiterParameters.h"

//==============================================================================
/**
    8.333：Manta Limiter／Af Elephant Limiter の工場プリセット（仕様書「A/B比較・Undo/Redo・プリセット：DAW共通機構を流用」）。

    作りは`MantaFactoryPresets.h`のとおり（値の表。書いていないものは既定値）。
    選択肢は**番号**（Style：0 Transparent／1 Punchy／2 Aggressive／3 Safe、Oversampling：0 Off／1 2x／2 4x／3 8x、
    Dither：0 Off／1 16／2 20／3 24 bit）、ON/OFFは0か1。

    ### Gain はここでは上げません

    **音圧は入ってくる音の大きさで決まる**ので、Gainはプリセットでは0 dBのまま。
    **天井・スタイル・時定数・オーバーサンプリング**のほうがプリセットの本体です——そこが決まっていれば、
    あとはGainを上げるだけで狙いに届きます（Manta Compのしきい値と同じ考え方）。
*/
namespace MantaLimiterPresets
{
    inline const std::vector<MantaFactoryPresets::Preset>& all()
    {
        using namespace MantaLimiterParams;
        // `link`だけは名前空間を書く：Linuxでは`<unistd.h>`の`link()`とぶつかって曖昧になる（8.335）

        // **`static`であること**（`makeToolbarPresets()`が参照で掴みます）
        static const std::vector<MantaFactoryPresets::Preset> presets
        {
            // マスタリング
            { "Transparent Master", "Mastering",
              { { output, -1.0f }, { style, 0.0f }, { lookahead, 2.0f }, { release, 150.0f },
                { oversampling, 2.0f }, { truePeak, 1.0f } } },
            { "Loud Master", "Mastering",
              { { output, -1.0f }, { style, 1.0f }, { lookahead, 1.5f }, { release, 60.0f },
                { oversampling, 2.0f }, { truePeak, 1.0f } } },
            { "Safe Master", "Mastering",
              { { output, -1.0f }, { style, 3.0f }, { release, 200.0f },
                { oversampling, 3.0f }, { truePeak, 1.0f } } },

            // 配信先の上限（-1 dBTP がよく使われる。天井だけ決めて、音量はメーターの目標で合わせる）
            { "Streaming -1 dBTP", "Delivery",
              { { output, -1.0f }, { style, 0.0f }, { oversampling, 1.0f }, { truePeak, 1.0f } } },
            { "Broadcast -2 dBTP", "Delivery",
              { { output, -2.0f }, { style, 3.0f }, { oversampling, 1.0f }, { truePeak, 1.0f } } },
            { "CD 16 bit", "Delivery",
              { { output, -0.3f }, { style, 0.0f }, { oversampling, 2.0f }, { truePeak, 1.0f },
                { dither, 1.0f }, { noiseShaping, 1.0f } } },

            // トラック・バス
            { "Mix Bus Catch", "Bus",
              { { output, -0.5f }, { style, 0.0f }, { lookahead, 1.0f }, { release, 200.0f },
                { truePeak, 0.0f } } },
            { "Drum Bus Punch", "Drums",
              { { output, -1.0f }, { style, 1.0f }, { lookahead, 0.5f }, { attack, 60.0f },
                { release, 40.0f }, { autoRelease, 0.0f } } },
            { "Snare Clip", "Drums",
              { { output, -3.0f }, { style, 2.0f }, { lookahead, 0.2f }, { release, 20.0f },
                { autoRelease, 0.0f }, { truePeak, 0.0f } } },
            { "Vocal Peak Tamer", "Vocal",
              { { output, -3.0f }, { style, 0.0f }, { lookahead, 3.0f }, { release, 120.0f },
                { truePeak, 0.0f } } },
            { "Bass Glue", "Bass",
              { { output, -2.0f }, { style, 0.0f }, { lookahead, 5.0f }, { release, 300.0f },
                { MantaLimiterParams::link, 100.0f }, { dcFilter, 1.0f } } },
        };

        return presets;
    }
}
