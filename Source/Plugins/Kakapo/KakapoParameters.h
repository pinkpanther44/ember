#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    8.292：**Kakapo のパラメータ**（Phase 285／本人の仕様書8章）。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    **足すときは、いちばん末尾へ。**

    ```
    0  Hold Mode    NOTES / SECONDS
    1  Hold Length  1〜32（**モードで意味が変わります**）
    2  Volume       0〜1.5
    3  MIDI Thru    ON / OFF
    ```

    **4つだけです。** 仕様書8章の表と同じ数で、`Reset`だけ入っていません
    ——あれは**押したときに履歴を消すだけ**で、覚えておく値がありません
    （設計書12章も「APVTS管理外」と書いています）。

    ### `Hold Length`が1つなのはなぜか

    仕様書5.1は「**保持ノート数**または**保持時間（秒）**のいずれかで間引く
    （パラメータで切替）」。**つまみを2本持つ**こともできますが、
    **どちらか片方は必ず効いていません**——効かないつまみが画面にあると、
    「動かしたのに何も起きない」を毎回説明することになります。

    **1本にして、モードで意味を変えます**（画面の単位表示も一緒に変わります）。

    ### `MIDI Thru`は、この本体では受け手がいません

    仕様書2章・8章にあるので**入れてあります**。動きも本物です
    （`producesMidi()`が真で、受けた音符をそのまま出口へ流します）。

    ただし**Manta Studio／Emberでは、音源のMIDI出力を誰も受け取りません**
    （`AudioEngine`は音だけを繋ぎます）。**画面の説明文にもそう書いてあります。**
    外へVST3として出すことがあれば、そのとき効きます。
*/
namespace KakapoParams
{
    inline constexpr const char* holdMode   = "holdMode";
    inline constexpr const char* holdLength = "holdLength";
    inline constexpr const char* volume     = "volume";
    inline constexpr const char* midiThru   = "midiThru";

    /** `holdMode`の選択肢。**0＝音数／1＝秒**（`NoteHistory::Mode`と同じ並び）。 */
    juce::StringArray getHoldModeNames();

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
