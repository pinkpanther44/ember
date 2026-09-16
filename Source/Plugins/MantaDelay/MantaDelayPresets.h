#pragma once

#include "../MantaFactoryPresets.h"
#include "MantaDelayParameters.h"

//==============================================================================
/**
    8.259：**Manta Delay の工場プリセット**（Phase 267／本人の要望）。

    作りは`MantaFactoryPresets.h`のとおり（値の表。書いていないものは既定値へ）。

    ### テンポシンクが既定です

    `sync`は既定でONなので、**表に音価（`syncDivision`）を書けば曲に付いてきます**。
    `Slapback`だけ`sync`を切って、ミリ秒で指定しています——
    **あれはテンポではなく「壁の近さ」**なので、曲が速くなっても伸びてほしくありません。

    音価の番号は`getSyncDivisionNames()`の並びです：

    ```
    0=1/32  1=1/16T  2=1/16  3=1/16.  4=1/8T  5=1/8  6=1/8.
    7=1/4T  8=1/4    9=1/4.  10=1/2T  11=1/2  12=1/2.  13=1/1
    ```

    ### エンジンBを使うのは1つだけ

    `Ping-Pong Wide`が`routingMode=4`でBを鳴らします（IDは`b_`で始まる。8.204）。
    ほかは**Single**のままです——2本目は「必要になったら開ける」ものなので、
    プリセットで勝手に開けると、**なぜ音が2つ返るのか分からなくなります**。
*/
namespace MantaDelayPresets
{
    inline const std::vector<MantaFactoryPresets::Preset>& all()
    {
        using namespace MantaDelayParams;

        // エンジンBのID（`b_`が付く）
        const auto b = [] (const char* suffix) { return engineParamId (1, suffix); };

        // **`static`であること**（`makeToolbarPresets()`が参照で掴みます）
        static const std::vector<MantaFactoryPresets::Preset> presets
        {
            // **テンポに付いてこないほうが良いもの**（上の説明）
            { "Slapback", "Short",
              { { sync, 0.0f }, { timeMs, 110.0f }, { feedback, 0.12f }, { mix, 0.25f },
                { character, 2.0f }, { drive, 0.25f }, { tone, 0.45f } } },

            { "Dotted Eighth", "Rhythmic",
              { { syncDivision, 6.0f }, { feedback, 0.38f }, { mix, 0.30f },
                { filterType, 1.0f }, { filterFreq, 6000.0f } } },

            { "Quarter Echo", "Rhythmic",
              { { syncDivision, 8.0f }, { feedback, 0.40f }, { mix, 0.28f },
                { character, 1.0f }, { tone, 0.40f } } },

            { "Tape Echo", "Character",
              { { syncDivision, 5.0f }, { feedback, 0.45f }, { mix, 0.32f },
                { character, 2.0f }, { drive, 0.45f }, { tone, 0.40f },
                { wowDepth, 0.35f }, { flutterDepth, 0.30f } } },

            { "Analog BBD", "Character",
              { { syncDivision, 8.0f }, { feedback, 0.40f }, { mix, 0.28f },
                { character, 1.0f }, { tone, 0.35f },
                { filterType, 1.0f }, { filterFreq, 3500.0f } } },

            { "Lo-Fi Crumble", "Character",
              { { syncDivision, 4.0f }, { feedback, 0.40f }, { mix, 0.30f },
                { character, 3.0f }, { drive, 0.60f }, { tone, 0.30f } } },

            // 原音が鳴っているあいだだけ引っ込む。**歌の後ろで使うもの**
            { "Ducked Vocal", "Utility",
              { { syncDivision, 6.0f }, { feedback, 0.35f }, { mix, 0.35f },
                { duckAmount, 0.70f }, { duckAttack, 5.0f }, { duckRelease, 300.0f } } },

            { "Diffuse Wash", "Utility",
              { { syncDivision, 9.0f }, { feedback, 0.55f }, { mix, 0.40f },
                { diffusion, 0.80f }, { filterType, 1.0f }, { filterFreq, 4000.0f },
                { lfoRate, 0.30f }, { lfoDepth, 0.25f } } },

            // **2本目を開ける唯一のもの**（上の説明）
            { "Ping-Pong Wide", "Utility",
              { { routingMode, 4.0f },
                { syncDivision, 5.0f }, { feedback, 0.42f }, { mix, 0.35f },
                { b (syncDivision), 6.0f }, { b (feedback), 0.42f } } },
        };

        return presets;
    }
}
