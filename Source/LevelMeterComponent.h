#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

//==============================================================================
/**
    入力／出力レベルを横棒で表示する、汎用のレベルメーター（仕様書5.4、設計書2.3.2）。

    Phase 7aで、録音時の入力レベル確認用に新設。将来ミキサー（仕様書5.7）の
    チャンネルストリップでも同じものを使い回せるよう、AudioEngineには依存させず
    「表示したい値を外から渡してもらう」だけの部品にしてある。

    目盛りはdB基準（-60dB〜0dB）。振幅（0.0〜1.0）をそのまま棒の長さにすると、
    人間の音量知覚と合わず「小さい音のときにほとんど動かないメーター」になるため。

    ### ピーク保持（Phase 59／8.1のC3）

    棒だけだと**一瞬の突っ込みが見えません。** 目で追える速さではないので、
    いちばん大きかったところに印を残し、しばらく持ってから下げています。

    **時間の進みは`setLevels()`の中で数えています。** タイマーを持たせると、
    メーターの数だけタイマーが増えるうえ、値を配る側にもタイマーがあって二重になります。
    値は30fpsで配られてくるので、そのたびに経過時間を見れば足ります
    （**配るのを止めるとピークもそこで止まります**。それは「今どこまで出たか」を
    見るための表示なので、止まった画面で下がり続けるより素直です）。
*/
class LevelMeterComponent : public juce::Component
{
public:
    LevelMeterComponent() = default;

    /** 表示するレベル（振幅0.0〜1.0）を設定する。値が変わったときだけ再描画する。 */
    void setLevels (float leftLevel, float rightLevel);

    /** 縦向き（下から上へ伸びる）表示に切り替える。ミキサーのチャンネルストリップ用。
        既定は横向き（入力レベル表示など、横長の場所に置く用途）。 */
    void setVertical (bool shouldBeVertical);

    /** ピークのdB値を数字でも出す（Phase 59）。

        **狭い場所では出さないこと。** アレンジ画面のトラックヘッダーのように
        36×14pxしかない場所では、数字が入らず棒を潰すだけになる。
        印（ピークの位置に引く線）は、出さない設定でも描かれる。 */
    void setShowPeakText (bool shouldShow);

    /** 保持しているピークを捨てる。**クリックでも同じことが起きる**
        （どのミキサーにもある操作なので、説明が無くても手が伸びる）。 */
    void resetPeak();

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    /** 8.177：棒の塗り方（Phase 218）。Emberは段組み、Manta Studioは連続。
        `filled`は塗る範囲、`whole`は棒ぜんたい（段の大きさを決めるのに使う）。 */
    static void fillLevelBar (juce::Graphics& g, juce::Rectangle<float> filled,
                               juce::Rectangle<float> whole, bool isVertical);

    /** 振幅（0.0〜1.0）を、メーターの長さの割合（0.0〜1.0）へ変換する。 */
    static float levelToProportion (float level);

    /** ピークを、いま表示すべき値まで下げる。保持時間を過ぎてから下がり始める。 */
    void decayPeaks (double nowMs);

    static constexpr float minimumDecibels = -60.0f;

    /** ピークを止めておく時間。短いと読む前に消え、長いと次の突っ込みに気づけない。 */
    static constexpr double peakHoldMs = 1500.0;

    /** 保持のあとの下がり方（dB/秒）。棒の落ち方より遅くしないと、印が棒に埋もれる。 */
    static constexpr float peakDecayDbPerSecond = 12.0f;

    /** 数字を出すのに要る最低の幅。これより狭ければ印だけにする。

        **縦向きと横向きで違う。** 縦向きは数字を下へ置くので幅は文字ぶんで足りるが、
        横向きは棒と横に並べるため、棒が潰れない幅が要る。 */
    static constexpr int minimumWidthForVerticalText = 26;
    static constexpr int minimumWidthForHorizontalText = 60;
    static constexpr int peakTextHeight = 11;

    float levels[2] { 0.0f, 0.0f };
    float peaks[2] { 0.0f, 0.0f };

    /** それぞれのチャンネルでピークを付けた時刻（ミリ秒）。保持時間の起点。 */
    double peakSetTimeMs[2] { 0.0, 0.0 };

    /** 前回`setLevels()`が呼ばれた時刻。下げ幅を「経過時間×速度」で出すために持つ。 */
    double lastUpdateMs = 0.0;

    bool vertical = false;
    bool showPeakText = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeterComponent)
};
