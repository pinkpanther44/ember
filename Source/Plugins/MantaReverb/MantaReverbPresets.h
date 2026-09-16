#pragma once

#include "../MantaFactoryPresets.h"
#include "MantaReverbParameters.h"

//==============================================================================
/**
    8.259：**Manta Reverb の工場プリセット**（Phase 267／本人の要望）。

    作りは`MantaFactoryPresets.h`のとおり（値の表。書いていないものは既定値へ）。

    ### アルゴリズムの番号

    `getAlgorithmNames()`の並びです：

    ```
    0=Room  1=Plate  2=Hall  3=Ambience  4=Random Hall  5=Twin Delays  6=Panorama
    ```

    **切り替えるといま鳴っている響きは止まります**（プラグインの説明どおり）。
    プリセットを選ぶと切り替わるので、**伸ばしている音の上で選ぶと途切れます**。

    ### Mixは控えめにしてあります

    どれも0.18〜0.35です。**送りで使うなら100%へ**上げてください——
    挿して混ぜる使い方（インサート）のほうを既定にしてあります。

    ### `Panorama`のプリセットは置いていません

    あれは**ステレオ加工そのもの**で、`Mix`を100%にして使うものです
    （残響ではないので、「響きの出発点」という形になりません）。
*/
namespace MantaReverbPresets
{
    inline const std::vector<MantaFactoryPresets::Preset>& all()
    {
        using namespace MantaReverbParams;

        // **`static`であること**（`makeToolbarPresets()`が参照で掴みます）
        static const std::vector<MantaFactoryPresets::Preset> presets
        {
            { "Small Room", "Room",
              { { algorithm, 0.0f }, { decay, 0.8f }, { size, 0.35f },
                { earlyLevel, 0.70f }, { highDampAmount, 0.50f }, { mix, 0.20f } } },

            { "Drum Room", "Room",
              { { algorithm, 0.0f }, { decay, 1.1f }, { size, 0.45f },
                { earlyLevel, 0.80f }, { shape, 0.35f }, { highDampAmount, 0.40f },
                { mix, 0.22f } } },

            { "Vocal Plate", "Plate",
              { { algorithm, 1.0f }, { decay, 1.8f }, { predelay, 20.0f },
                { diffusion, 0.70f }, { highDampAmount, 0.40f }, { mix, 0.28f } } },

            { "Bright Plate", "Plate",
              { { algorithm, 1.0f }, { decay, 2.4f }, { predelay, 10.0f },
                { diffusion, 0.80f }, { highDampAmount, 0.20f }, { lowDampAmount, 0.35f },
                { mix, 0.30f } } },

            { "Big Hall", "Hall",
              { { algorithm, 2.0f }, { decay, 3.5f }, { size, 0.75f },
                { predelay, 35.0f }, { earlyLevel, 0.45f }, { lowDampAmount, 0.30f },
                { mix, 0.30f } } },

            { "Cathedral", "Hall",
              { { algorithm, 2.0f }, { decay, 8.0f }, { size, 0.90f },
                { predelay, 60.0f }, { highDampAmount, 0.55f }, { lowDampAmount, 0.35f },
                { mix, 0.35f } } },

            // **テールがゆっくり漂います**（Random Hall専用のModulation）
            { "Drifting Hall", "Hall",
              { { algorithm, 4.0f }, { decay, 4.5f }, { size, 0.70f },
                { predelay, 25.0f }, { modulation, 0.50f }, { mix, 0.30f } } },

            { "Ambience", "Short",
              { { algorithm, 3.0f }, { decay, 0.6f }, { size, 0.40f },
                { earlyLevel, 0.80f }, { mix, 0.18f } } },

            { "Wide Air", "Short",
              { { algorithm, 3.0f }, { decay, 0.5f }, { spread, 0.70f },
                { width, 1.40f }, { earlyLevel, 0.85f }, { mix, 0.22f } } },

            { "Twin Bounce", "Delays",
              { { algorithm, 5.0f }, { twinTime, 200.0f }, { twinFeedback, 0.45f },
                { twinCross, 0.60f }, { mix, 0.30f } } },
        };

        return presets;
    }
}
