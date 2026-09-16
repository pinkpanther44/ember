#pragma once

#include "../MantaFactoryPresets.h"
#include "JavaRhinoBassParameters.h"

//==============================================================================
/**
    8.259：**Java Rhino Bass の工場プリセット**（Phase 267／本人の要望）。

    ### こちらは奏法も入ります

    ギター（`RaccoGuitarPresets.h`）と違い、**`style`はパラメータ**なので
    プリセットに入れられます。番号は`getStyleNames()`の並びで、
    **0=Finger / 1=Pick / 2=Slap / 3=Mute / 4=Ghost / 5=Harmonic**。

    > **`BassStyle`の番号ではありません**（あちらには`Pop`が入っています。
    > `JavaRhinoBassParameters.h`の`styleFromChoice()`）。

    ### Blendの向き

    **0＝ネックPUだけ／0.5＝両方フル（ジャズベの定番）／1＝ブリッジPUだけ**。
    0.5は「真ん中」ではなく**両方とも全開**で、和を取るぶん中域がへこみます。

    ### 出力（`gain`）は触っていません

    既定の0.35のままにしてあります（弦の素の峰が1.6〜2.2あるため。8.257）。
*/
namespace JavaRhinoBassPresets
{
    inline const std::vector<MantaFactoryPresets::Preset>& all()
    {
        using namespace JavaRhinoBassParams;

        // **`static`であること**（`makeToolbarPresets()`が参照で掴みます）
        static const std::vector<MantaFactoryPresets::Preset> presets
        {
            //------------------------------------------------------------------
            { "Finger Standard", "Finger",
              { { style, 0.0f }, { brightness, 0.62f }, { sustain, 9.0f },
                { pluckPos, 0.22f }, { hardness, 0.60f }, { attack, 0.60f },
                { blend, 0.50f }, { tone, 7.0f } } },

            { "Motown Flat", "Finger",
              { { style, 0.0f }, { brightness, 0.35f }, { sustain, 6.0f },
                { pluckPos, 0.30f }, { hardness, 0.30f }, { attack, 0.35f },
                { blend, 0.10f }, { tone, 4.5f } } },

            { "Bridge Bite", "Finger",
              { { style, 0.0f }, { brightness, 0.80f }, { sustain, 9.0f },
                { pluckPos, 0.10f }, { hardness, 0.80f }, { attack, 0.80f },
                { blend, 0.95f }, { tone, 8.5f } } },

            { "Dub Deep", "Finger",
              { { style, 0.0f }, { brightness, 0.18f }, { sustain, 11.0f },
                { pluckPos, 0.35f }, { hardness, 0.20f }, { attack, 0.20f },
                { blend, 0.00f }, { tone, 2.5f } } },

            { "Smooth Legato", "Finger",
              { { style, 0.0f }, { brightness, 0.45f }, { sustain, 12.0f },
                { pluckPos, 0.28f }, { hardness, 0.25f }, { attack, 0.15f },
                { blend, 0.30f }, { tone, 5.5f }, { legato, 1.0f } } },

            //------------------------------------------------------------------
            { "Pick Rock", "Pick",
              { { style, 1.0f }, { brightness, 0.72f }, { sustain, 7.0f },
                { pluckPos, 0.16f }, { hardness, 0.80f }, { attack, 0.85f },
                { blend, 0.65f }, { tone, 8.0f } } },

            { "Pick Bright", "Pick",
              { { style, 1.0f }, { brightness, 0.85f }, { sustain, 6.0f },
                { pluckPos, 0.12f }, { hardness, 0.95f }, { attack, 1.00f },
                { blend, 0.80f }, { tone, 9.5f } } },

            //------------------------------------------------------------------
            // **Slapでは、D弦とG弦が自動でPop（プル）になります**（8.257）
            { "Slap Funk", "Slap",
              { { style, 2.0f }, { brightness, 0.78f }, { sustain, 5.0f },
                { hardness, 0.70f }, { attack, 0.75f }, { clank, 0.70f },
                { blend, 0.60f }, { tone, 9.0f } } },

            { "Slap Heavy", "Slap",
              { { style, 2.0f }, { brightness, 0.70f }, { sustain, 4.0f },
                { hardness, 0.85f }, { attack, 0.90f }, { clank, 1.00f },
                { blend, 0.55f }, { tone, 8.5f } } },

            //------------------------------------------------------------------
            { "Mute Short", "Mute",
              { { style, 3.0f }, { brightness, 0.50f }, { hardness, 0.45f },
                { attack, 0.50f }, { blend, 0.35f }, { tone, 6.0f } } },

            { "Ghost Groove", "Mute",
              { { style, 4.0f }, { brightness, 0.60f }, { hardness, 0.60f },
                { attack, 0.80f }, { clank, 0.60f }, { blend, 0.50f },
                { tone, 7.5f } } },

            //------------------------------------------------------------------
            { "Harmonic Bell", "Character",
              { { style, 5.0f }, { brightness, 0.80f }, { sustain, 12.0f },
                { pluckPos, 0.25f }, { hardness, 0.50f }, { attack, 0.40f },
                { blend, 0.45f }, { tone, 8.5f } } },
        };

        return presets;
    }
}
