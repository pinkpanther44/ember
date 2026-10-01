#pragma once

#include "../MantaFactoryPresets.h"
#include "ShifterParameters.h"

//==============================================================================
/**
    8.336：Manta Shifter／Gibbon Voice の工場プリセット（`MantaFactoryPresets.h`の値の表。書いていないものは既定値）。

    選択肢は**番号**（Mode：0 Transpose／1 Quantize／2 Robot／3 MIDI、Key：0 C … 11 B、
    Scale：0 Chromatic／1 Major／2 Minor）、ON/OFF は 0 か 1。

    **Key は C のままにしてあります**——曲のキーは使う人が合わせるもので、プリセットが決めるものではないため。
*/
namespace MantaShifterPresets
{
    inline const std::vector<MantaFactoryPresets::Preset>& all()
    {
        using namespace ShifterParams;
        // `link`だけは名前空間を書く：Linuxでは`<unistd.h>`の`link()`とぶつかって曖昧になる（8.335）

        // **`static`であること**（`makeToolbarPresets()`が参照で掴みます）
        static const std::vector<MantaFactoryPresets::Preset> presets
        {
            // 声の太さ・細さ（ピッチはそのまま）
            { "Bigger Voice", "Character", { { formant, -3.0f } } },
            { "Smaller Voice", "Character", { { formant, 3.0f } } },
            { "Deep Voice", "Character", { { pitch, -5.0f }, { formant, -2.0f } } },
            { "Monster", "Character", { { pitch, -12.0f }, { formant, -5.0f }, { driveOn, 1.0f }, { drive, 40.0f } } },
            // 8.341：Link はつまみの連動になったので、Formant も明示する（Pitch と同じだけ＝早回しの声）
            { "Chipmunk", "Character", { { pitch, 7.0f }, { formant, 7.0f }, { ShifterParams::link, 1.0f } } },
            { "Radio Voice", "Character", { { driveOn, 1.0f }, { drive, 70.0f }, { output, -4.0f } } },

            // 音程を動かす
            { "Octave Down", "Pitch", { { pitch, -12.0f } } },
            { "Octave Up", "Pitch", { { pitch, 12.0f } } },
            { "Fifth Harmony", "Pitch", { { pitch, 7.0f }, { mix, 50.0f } } },

            // 補正
            { "Hard Tune", "Correction", { { mode, 1.0f }, { scale, 1.0f }, { retune, 0.0f } } },
            { "Natural Tune", "Correction", { { mode, 1.0f }, { scale, 1.0f }, { retune, 60.0f } } },
            { "Robot C4", "Correction", { { mode, 2.0f }, { retune, 0.0f } } },
            { "MIDI Follow", "Correction", { { mode, 3.0f }, { retune, 10.0f } } },
        };

        return presets;
    }
}
