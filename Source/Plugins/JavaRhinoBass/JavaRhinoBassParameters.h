#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "JavaRhinoBassExcitation.h"   // BassStyle

//==============================================================================
/**
    8.257：**Java Rhino Bass のパラメータ**（Phase 265）。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    **足すときは、いちばん末尾へ。**

    ### `JBass5`から持って来なかったもの

    元にはアンプ段が**7つ**ありました（`ampOn` / `comp` / `drive` /
    `ampBass` / `ampMid` / `ampTreble` / `ampLevel`）。
    **本人の指定で載せていません**（`Racco Guitar`と同じ切り分け）。

    > **`comp`もアンプ段の中にありました。** 向こうではAMPがOFFでも効く
    > 「DI用のコンプ」でしたが、`AmpSim`ごと持って来ていないので**入っていません**。
    > 要るなら、つまみ1本として足せます（コンプ自体は`MantaComp`もあります）。

    残した11個のIDと範囲は**そのまま写して**あります。

    ### 奏法（`style`）は**パラメータです**

    `Racco Guitar`では奏法をパラメータにしませんでした（キースイッチが音のスレッドから
    来るため）。**こちらは元からパラメータ**なので、そのまま残しています——
    音のスレッドからの書き戻しは`juce::AsyncUpdater`でメッセージスレッドへ渡します
    （`JavaRhinoBassProcessor`）。
*/
namespace JavaRhinoBassParams
{
    inline constexpr const char* style      = "style";
    inline constexpr const char* brightness = "brightness";
    inline constexpr const char* sustain    = "sustain";
    inline constexpr const char* pluckPos   = "pluckPos";
    inline constexpr const char* hardness   = "hardness";
    inline constexpr const char* attack     = "attack";
    inline constexpr const char* clank      = "clank";
    inline constexpr const char* blend      = "blend";
    inline constexpr const char* tone       = "tone";
    inline constexpr const char* gain       = "gain";
    inline constexpr const char* legato     = "legato";

    /** 奏法の選択肢。**`Pop`は入りません**——スラップのとき高音弦で自動的に選ばれるものです。 */
    juce::StringArray getStyleNames();

    /** 選択肢の番号を`BassStyle`へ。

        **`(BassStyle) choice`と書かないこと。** `BassStyle`には`Pop`が
        `Slap`の次に入っているので、番号が3つめから1つずれます
        （`JBass5`はそう書いてあり、**Muteを選ぶとPopが鳴り、Harmonicは鳴らせません**でした。
        移すときに見つけた元のバグです）。 */
    BassStyle styleFromChoice (int choice);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
