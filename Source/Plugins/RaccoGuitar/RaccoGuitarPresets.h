#pragma once

#include "../MantaFactoryPresets.h"
#include "RaccoGuitarParameters.h"

//==============================================================================
/**
    8.259：**Racco Guitar の工場プリセット**（Phase 267／本人の要望）。

    作りは`MantaFactoryPresets.h`のとおり（値の表。書いていないものは既定値へ）。

    ### 奏法は入っていません

    `Racco Guitar`の奏法は**パラメータではありません**（キースイッチで決まる状態。
    `RaccoGuitarParameters.h`）。プリセットが触るのは**音色の7つだけ**です——
    パームミュートやハーモニクスは、プリセットを選んだあとに鍵盤で切り替えます。

    ### Pickupの向き

    `Front`（0.35＝ネック側）が丸く、`Rear`（0.10＝ブリッジ側）が硬い音です。
    **番号は`getPickupNames()`の並び**（0=Front / 1=Center / 2=Rear）。
*/
namespace RaccoGuitarPresets
{
    inline const std::vector<MantaFactoryPresets::Preset>& all()
    {
        using namespace RaccoGuitarParams;

        // **`static`であること**（`makeToolbarPresets()`が参照で掴みます）
        static const std::vector<MantaFactoryPresets::Preset> presets
        {
            //------------------------------------------------------------------
            { "Clean Strum", "Clean",
              { { brightness, 0.55f }, { sustain, 6.0f }, { pickPos, 0.25f },
                { pickupSel, 1.0f }, { hardness, 0.55f }, { attack, 0.50f } } },

            { "Glassy Center", "Clean",
              { { brightness, 0.80f }, { sustain, 7.0f }, { pickPos, 0.28f },
                { pickupSel, 1.0f }, { hardness, 0.50f }, { attack, 0.45f } } },

            { "Sustain Lead", "Clean",
              { { brightness, 0.68f }, { sustain, 10.0f }, { pickPos, 0.22f },
                { pickupSel, 1.0f }, { hardness, 0.60f }, { attack, 0.50f } } },

            //------------------------------------------------------------------
            { "Bright Chime", "Bright",
              { { brightness, 0.85f }, { sustain, 9.0f }, { pickPos, 0.15f },
                { pickupSel, 2.0f }, { hardness, 0.80f }, { attack, 0.75f } } },

            { "Hard Pick", "Bright",
              { { brightness, 0.72f }, { sustain, 5.0f }, { pickPos, 0.12f },
                { pickupSel, 2.0f }, { hardness, 1.00f }, { attack, 1.00f } } },

            { "Tight Bridge", "Bright",
              { { brightness, 0.62f }, { sustain, 2.5f }, { pickPos, 0.08f },
                { pickupSel, 2.0f }, { hardness, 0.80f }, { attack, 0.70f } } },

            // ピッキングハーモニクスの次数は**Pick Posで決まります**（0.2で5次）。
            // スクイールを狙うときの出発点として置いてあります
            { "Squeal Ready", "Bright",
              { { brightness, 0.75f }, { sustain, 8.0f }, { pickPos, 0.20f },
                { pickupSel, 2.0f }, { hardness, 0.95f }, { attack, 0.90f } } },

            //------------------------------------------------------------------
            { "Warm Neck", "Warm",
              { { brightness, 0.38f }, { sustain, 7.0f }, { pickPos, 0.33f },
                { pickupSel, 0.0f }, { hardness, 0.35f }, { attack, 0.35f } } },

            { "Round Fingerstyle", "Warm",
              { { brightness, 0.30f }, { sustain, 6.0f }, { pickPos, 0.35f },
                { pickupSel, 0.0f }, { hardness, 0.12f }, { attack, 0.20f } } },

            { "Soft Felt", "Warm",
              { { brightness, 0.25f }, { sustain, 8.0f }, { pickPos, 0.40f },
                { pickupSel, 0.0f }, { hardness, 0.00f }, { attack, 0.10f } } },

            { "Dark Jazz", "Warm",
              { { brightness, 0.22f }, { sustain, 5.0f }, { pickPos, 0.42f },
                { pickupSel, 0.0f }, { hardness, 0.20f }, { attack, 0.25f },
                { gain, 0.90f } } },

            //------------------------------------------------------------------
            { "Funk Cutting", "Percussive",
              { { brightness, 0.75f }, { sustain, 1.2f }, { pickPos, 0.12f },
                { pickupSel, 2.0f }, { hardness, 0.90f }, { attack, 0.90f },
                { gain, 0.85f } } },
        };

        return presets;
    }
}
