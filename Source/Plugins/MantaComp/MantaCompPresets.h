#pragma once

#include "../MantaFactoryPresets.h"
#include "MantaCompParameters.h"

//==============================================================================
/**
    8.259：**Manta Comp の工場プリセット**（Phase 267／本人の要望）。

    作りは`MantaFactoryPresets.h`のとおり（値の表。書いていないものは既定値へ）。

    ### しきい値は素材で変わります

    `Threshold`は**入ってくる音の大きさ**で決まるので、
    ここの値は「だいたいこの辺」でしかありません。
    **比率・時定数・ニー・混ぜ具合**のほうがプリセットの本体です——
    そこが決まっていれば、あとはしきい値を上げ下げするだけで狙いに届きます。

    ### サイドチェインのフィルタ

    `Bass Control`だけ`scFilterOn`を点けてあります（60Hz以下を検出から外す）。
    **低音でコンプが上下する**のを止めるための、いちばんよくある使い方です。
*/
namespace MantaCompPresets
{
    inline const std::vector<MantaFactoryPresets::Preset>& all()
    {
        using namespace MantaCompParams;

        // **`static`であること**（`makeToolbarPresets()`が参照で掴みます）
        static const std::vector<MantaFactoryPresets::Preset> presets
        {
            { "Vocal Smooth", "Vocal",
              { { threshold, -22.0f }, { ratio, 3.0f }, { knee, 8.0f },
                { attack, 12.0f }, { release, 150.0f }, { makeupGain, 3.0f } } },

            { "Vocal Parallel", "Vocal",
              { { threshold, -32.0f }, { ratio, 8.0f }, { knee, 4.0f },
                { attack, 3.0f }, { release, 120.0f }, { makeupGain, 2.0f },
                { mix, 45.0f } } },

            { "Drum Punch", "Drums",
              { { threshold, -18.0f }, { ratio, 4.0f }, { knee, 3.0f },
                { attack, 25.0f }, { release, 80.0f }, { makeupGain, 3.0f } } },

            { "Drum Bus Glue", "Drums",
              { { threshold, -14.0f }, { ratio, 2.0f }, { knee, 12.0f },
                { attack, 30.0f }, { release, 250.0f }, { adaptiveRelease, 1.0f },
                { makeupGain, 2.0f } } },

            // **低音で上下しないように**、検出だけ60Hz以下を外します（上の説明）
            { "Bass Control", "Bass",
              { { threshold, -20.0f }, { ratio, 4.0f }, { knee, 6.0f },
                { attack, 8.0f }, { release, 120.0f }, { makeupGain, 3.0f },
                { scFilterOn, 1.0f }, { scLowCut, 60.0f } } },

            { "Mix Bus Glue", "Bus",
              { { threshold, -12.0f }, { ratio, 2.0f }, { knee, 18.0f },
                { attack, 30.0f }, { release, 300.0f }, { autoEnvelope, 1.0f },
                { makeupGain, 1.5f } } },

            { "Gentle Levelling", "Bus",
              { { threshold, -28.0f }, { ratio, 2.0f }, { knee, 12.0f },
                { attack, 20.0f }, { release, 400.0f }, { adaptiveRelease, 1.0f },
                { makeupGain, 2.0f } } },

            // 頭を止めるだけ。**Look Aheadは既定でON**なので触っていません
            { "Peak Catcher", "Utility",
              { { threshold, -6.0f }, { ratio, 20.0f }, { knee, 0.0f },
                { attack, 0.5f }, { release, 50.0f } } },
        };

        return presets;
    }
}
