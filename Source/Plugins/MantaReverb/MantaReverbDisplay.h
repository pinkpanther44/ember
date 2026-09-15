#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MantaReverbAlgorithm.h"
#include "MantaReverbEngine.h"
#include "MantaReverbTheme.h"

#include <cmath>

//==============================================================================
/**
    8.241：**響きの形を出す**（Phase 254／リバーブ設計書2章`DetailPanel`の下敷き）。

    ─────────────────────────────────────────────────────────────────────────
    何を描くか
    ─────────────────────────────────────────────────────────────────────────

    **原音のあと、いつ・どのくらいの大きさで返ってくるか**だけです。

    ```
        │█                                            原音
        │  ┆                                          ← プリディレイ（空白）
        │  ▌ ▌  ▌   ▌    ▌                            初期反射（`Early`）
        │  ╲╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌                   テールの減衰（`Decay`）
        └────────────────────────────────────────
         0        0.5s        1.0s        1.5s
    ```

    **測っているのではなく、つまみから描いています。** 実際の音を解析すると
    重くなりますし、**つまみと絵がずれる余地**ができます。
    **設定は`getDisplaySettings()`から取ります**——`processBlock()`が
    使うのと同じ`buildEngineSettings()`を通るので、ずれません（1.27）。

    ─────────────────────────────────────────────────────────────────────────
    ドラッグできません——**それでよい**
    ─────────────────────────────────────────────────────────────────────────

    ディレイのタイムラインと同じ扱いです（8.206）。
    掴めるように見せないため`setInterceptsMouseClicks (false, false)`——
    **押しても何も起きないものを、押せそうに見せない**（8.161）。

    ─────────────────────────────────────────────────────────────────────────
    横の目盛り
    ─────────────────────────────────────────────────────────────────────────

    **聞こえなくなるところまで**を画面いっぱいに収めます。
    固定の秒数にすると、`Decay`を短くしたとき**左端に全部固まって読めません。**

    縦は**dB**です。振幅のまま描くと、減衰の後半（いちばん長く鳴っているところ）が
    床に貼り付いて、`Decay`を回しても何も動かないように見えます。
*/
class MantaReverbDisplay : public juce::Component
{
public:
    MantaReverbDisplay()
    {
        setInterceptsMouseClicks (false, false);
    }

    /** どこまで描くか（dB）。**-60dBがRT60の定義そのもの**なので、少し下まで。 */
    static constexpr float floorDb = -66.0f;

    void setSettings (const MantaReverbEngine::Settings& newSettings)
    {
        // **変わったときだけ描き直す。** タイマーで毎回呼ばれるので、
        // そのまま`repaint()`すると止まっていても描き続けます
        if (settings.kind == newSettings.kind
             && juce::approximatelyEqual (settings.predelayMs, newSettings.predelayMs)
             && juce::approximatelyEqual (settings.decaySeconds, newSettings.decaySeconds)
             && juce::approximatelyEqual (settings.sizeScale, newSettings.sizeScale)
             && juce::approximatelyEqual (settings.earlyLevel, newSettings.earlyLevel)
             && juce::approximatelyEqual (settings.shape, newSettings.shape)
             && juce::approximatelyEqual (settings.spread, newSettings.spread)
             && juce::approximatelyEqual (settings.width, newSettings.width)
             && juce::approximatelyEqual (settings.twin.timeSeconds, newSettings.twin.timeSeconds)
             && juce::approximatelyEqual (settings.twin.feedback, newSettings.twin.feedback)
             && settings.panoramaMonoSum == newSettings.panoramaMonoSum
             && settings.panoramaInvertRight == newSettings.panoramaInvertRight
             && settings.panoramaSwap == newSettings.panoramaSwap)
            return;

        settings = newSettings;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();

        g.setColour (MantaTheme::graphBackground());
        g.fillRect (area);

        auto plot = area.reduced (14, 12);

        if (plot.getWidth() <= 20 || plot.getHeight() <= 20)
        {
            g.setColour (MantaTheme::border());
            g.drawRect (area, 1);
            return;
        }

        // 8.248：`Panorama`は**時間で起きることが何もありません**（Phase 258）。
        //
        // 減衰の絵を出すと**嘘になります**（テールが無いので）。
        // 代わりに**いまの左右の広がり**を出します——それがこのアルゴリズムの中身です
        if (settings.kind == MantaReverbAlgorithm::Kind::panorama)
        {
            paintPanorama (g, area, plot);
            return;
        }

        const double predelaySeconds = settings.predelayMs / 1000.0;

        // 見える範囲。**原音＋プリディレイ＋減衰**が収まるところまで。
        //
        // 8.247：`Twin Delays`は減衰時間を持たないので、**聞こえなくなる反復まで**
        // を数えます（Phase 257）
        const bool isTwin = (settings.kind == MantaReverbAlgorithm::Kind::twinDelays);

        const double visibleSeconds = juce::jmax (0.25, predelaySeconds
                                                          + (isTwin ? getTwinVisibleSeconds()
                                                                    : settings.decaySeconds * 1.1));

        const auto timeToX = [&plot, visibleSeconds] (double seconds)
        {
            return (float) plot.getX()
                     + (float) (seconds / visibleSeconds) * (float) plot.getWidth();
        };

        const auto dbToY = [&plot] (float db)
        {
            const float proportion = juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
            return (float) plot.getBottom() - proportion * (float) plot.getHeight();
        };

        const auto gainToY = [&dbToY] (float gain)
        {
            return dbToY (juce::Decibels::gainToDecibels (juce::jmax (1.0e-4f, gain), floorDb));
        };

        //----------------------------------------------------------------------
        // 目盛り。**0.5秒ごと**（`Decay`を回すと本数が変わるので、いま何秒を見ているか分かる）

        g.setFont (juce::FontOptions (9.0f));

        for (double seconds = 0.5; seconds < visibleSeconds; seconds += 0.5)
        {
            const float x = timeToX (seconds);

            g.setColour (MantaTheme::grid());
            g.drawVerticalLine ((int) x, (float) plot.getY(), (float) plot.getBottom());

            g.setColour (MantaTheme::textDim().withAlpha (0.7f));
            g.drawText (juce::String (seconds, 1) + "s",
                         juce::Rectangle<int> ((int) x + 2, plot.getBottom() - 12, 34, 12),
                         juce::Justification::centredLeft, false);
        }

        // -60dBの線。**RT60はここまでの時間**なので、いちばん見たい高さです
        if (! isTwin)
        {
            const float y = dbToY (-60.0f);

            g.setColour (MantaTheme::gridStrong());
            g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());

            g.setColour (MantaTheme::textDim().withAlpha (0.7f));
            g.drawText ("-60 dB", juce::Rectangle<int> (plot.getX() + 2, (int) y - 12, 46, 12),
                         juce::Justification::centredLeft, false);
        }

        //----------------------------------------------------------------------
        // 原音。**いちばん左に1本**——初期反射が「そこからどれだけ遅れるか」の基準です

        g.setColour (MantaTheme::textDim());
        g.fillRect (juce::Rectangle<float> (timeToX (0.0), (float) plot.getY(),
                                             2.0f, (float) plot.getBottom() - (float) plot.getY()));

        //----------------------------------------------------------------------
        // 8.247：`Twin Delays`（Phase 257）。**テールの線は引きません**——
        // 減衰の連続ではなく、**数えられる反復**だからです。
        //
        // 左を主の色、右を副の色で出します（画面のつまみの色と合わせてあります）

        if (isTwin)
        {
            const double timeA = juce::jlimit (MantaReverbTwinDelays::minTimeSeconds,
                                                MantaReverbTwinDelays::maxTimeSeconds,
                                                settings.twin.timeSeconds);
            const double timeB = timeA * MantaReverbTwinDelays::ratioFor (settings.spread);

            const float feedback = juce::jlimit (0.0f, MantaReverbTwinDelays::maxFeedback,
                                                  settings.twin.feedback);

            for (int side = 0; side < 2; ++side)
            {
                const double step = (side == 0) ? timeA : timeB;

                g.setColour ((side == 0 ? MantaReverbTheme::accent() : MantaReverbTheme::highlight())
                                 .withAlpha (0.85f));

                float level = 1.0f;

                for (int repeat = 1; repeat <= maxTwinRepeats; ++repeat)
                {
                    const double seconds = predelaySeconds + step * repeat;

                    if (seconds >= visibleSeconds)
                        break;

                    const float top = gainToY (level);

                    // **左右で少しずらして描きます**——同じ時刻に重なったとき、
                    // 片方が完全に隠れて「1本しか無い」ように見えるため
                    const float x = timeToX (seconds) + (side == 0 ? 0.0f : 3.0f);

                    g.fillRect (juce::Rectangle<float> (x, top, 2.0f, (float) plot.getBottom() - top));

                    level *= feedback;
                }
            }

            g.setColour (MantaTheme::border());
            g.drawRect (area, 1);
            return;
        }

        //----------------------------------------------------------------------
        // 8.240：初期反射。**`MantaReverbAlgorithm`の表をそのまま読みます**（1.27）——
        // 別の数字で描くと、**絵のとおりに鳴っていない**ことになります。
        //
        // 8.243：**Plateには無いので描きません**（Phase 255）。
        // 判断は`getCapabilities()`ただ1つ——画面のグレーアウトと同じものを見ます

        const float earlyLevel = MantaReverbAlgorithm::getCapabilities (settings.kind).early
                                     ? juce::jlimit (0.0f, 1.0f, settings.earlyLevel)
                                     : 0.0f;

        if (earlyLevel > 0.001f)
        {
            const auto& pattern = MantaReverbAlgorithm::getEarlyPattern (settings.kind);

            // 8.246：**`Shape`と`Spread`も絵に出すこと**（Phase 256）。
            //
            // 換算は`MantaReverbAlgorithm`の2つを通します（1.27）——
            // **`MantaReverbEarly`が使うのと同じ関数**なので、
            // 描いた位置と鳴る位置、描いた大きさと鳴る大きさがずれません
            const double timeScale = settings.sizeScale
                                       * MantaReverbAlgorithm::spreadScaleFor (settings.spread);
            const float slope = MantaReverbAlgorithm::shapeSlopeFor (settings.shape);

            // **割り直しもやること。** `MantaReverbEarly`は傾けたあと全体で割るので、
            // ここで割らないと**Shapeを回すと絵だけ大きくなります**
            double sumOfSquares = 0.0;

            for (int tap = 0; tap < MantaReverbAlgorithm::numEarlyTaps; ++tap)
            {
                const float position = (float) tap / (float) (MantaReverbAlgorithm::numEarlyTaps - 1);
                const float weighted = pattern.left[(size_t) tap].gain
                                         * std::exp (slope * (position - 0.5f));

                sumOfSquares += (double) weighted * weighted;
            }

            const float normalisation = (float) (1.0 / std::sqrt (juce::jmax (1.0e-6, sumOfSquares)));

            g.setColour (MantaReverbTheme::highlight().withAlpha (0.85f));

            for (int tap = 0; tap < MantaReverbAlgorithm::numEarlyTaps; ++tap)
            {
                const auto& entry = pattern.left[(size_t) tap];

                const double seconds = predelaySeconds + entry.milliseconds * timeScale * 0.001;

                if (seconds >= visibleSeconds)
                    break;

                const float position = (float) tap / (float) (MantaReverbAlgorithm::numEarlyTaps - 1);
                const float gain = entry.gain * std::exp (slope * (position - 0.5f)) * normalisation;

                // **符号は絵に出しません**（棒の向きにすると位相の話に見えます）。
                // 出すのは大きさだけ
                const float top = gainToY (std::abs (gain) * earlyLevel);
                const float x = timeToX (seconds);

                g.fillRect (juce::Rectangle<float> (x, top, 2.0f, (float) plot.getBottom() - top));
            }
        }

        //----------------------------------------------------------------------
        // テール。**`RT60`そのままの直線**（縦がdBなので、指数の減衰は直線になります）

        {
            const float startX = timeToX (predelaySeconds);

            // 床（`floorDb`）へ着くところ。**見える範囲の外なら、そこで切ります**
            const double endSeconds = juce::jmin (visibleSeconds,
                                                   predelaySeconds + settings.decaySeconds
                                                                       * (-floorDb / 60.0));

            // **切った先の高さは、切った時刻のもの**であること——
            // 床の値で決め打ちにすると、`Predelay`を伸ばしたときに線が寝ます
            const float endDb = (float) (-60.0 * (endSeconds - predelaySeconds)
                                           / juce::jmax (1.0e-3f, settings.decaySeconds));

            juce::Path path;
            path.startNewSubPath (startX, dbToY (0.0f));
            path.lineTo (timeToX (endSeconds), dbToY (juce::jmax (floorDb, endDb)));

            g.setColour (MantaReverbTheme::accent());
            g.strokePath (path, juce::PathStrokeType (2.0f));

            // 下を薄く塗る。**線だけだと「減衰」ではなく「境界」に見えます**
            juce::Path filled (path);
            filled.lineTo (path.getCurrentPosition().getX(), (float) plot.getBottom());
            filled.lineTo (startX, (float) plot.getBottom());
            filled.closeSubPath();

            g.setColour (MantaReverbTheme::accent().withAlpha (0.16f));
            g.fillPath (filled);
        }

        //----------------------------------------------------------------------

        g.setColour (MantaTheme::border());
        g.drawRect (area, 1);
    }

private:
    /** 8.248：`Panorama`の絵（Phase 258）。**左右の広がりを1本の帯で出します。**

        ```
            L ──────┤▓▓▓▓▓▓▓▓▓▓▓▓▓├────── R
                    （Width 100%）
        ```

        帯が`L`と`R`の印まで届いていれば素通し、狭ければ真ん中寄り、
        印を越えていれば**スピーカーの外側**まで広げている、という読み方です。 */
    void paintPanorama (juce::Graphics& g, juce::Rectangle<int> area, juce::Rectangle<int> plot) const
    {
        const float width = settings.panoramaMonoSum ? 0.0f
                                                      : juce::jlimit (0.0f, 2.0f, settings.width);

        auto strip = plot.withSizeKeepingCentre (plot.getWidth(), 40);

        const float centreX = (float) strip.getCentreX();

        // **素通し（100%）の位置に印を置くこと。** 印が無いと、
        // 帯の長さが「広い」のか「普通」なのか読めません
        const float unitHalf = (float) strip.getWidth() * 0.34f;

        g.setFont (juce::FontOptions (10.0f));

        for (int side = 0; side < 2; ++side)
        {
            const float x = centreX + (side == 0 ? -unitHalf : unitHalf);

            g.setColour (MantaTheme::gridStrong());
            g.drawVerticalLine ((int) x, (float) strip.getY() - 6.0f, (float) strip.getBottom() + 6.0f);

            g.setColour (MantaTheme::textDim());
            g.drawText (side == 0 ? "L" : "R",
                         juce::Rectangle<int> ((int) x - 20, strip.getBottom() + 6, 40, 12),
                         juce::Justification::centred, false);
        }

        // 真ん中の線
        g.setColour (MantaTheme::grid());
        g.drawVerticalLine ((int) centreX, (float) strip.getY() - 6.0f, (float) strip.getBottom() + 6.0f);

        // 帯そのもの。**0%でも消さずに細く残す**——「無くなった」のか
        // 「真ん中に寄った」のかを区別できるように
        const float half = juce::jmax (1.5f, unitHalf * width);

        g.setColour (MantaReverbTheme::accent().withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (centreX - half, (float) strip.getY(),
                                             half * 2.0f, (float) strip.getHeight()));

        g.setColour (MantaReverbTheme::accent());
        g.drawRect (juce::Rectangle<float> (centreX - half, (float) strip.getY(),
                                             half * 2.0f, (float) strip.getHeight()), 1.5f);

        //----------------------------------------------------------------------
        // 入っているボタン。**押したものが画面に出ていること**——
        // ボタンの色だけだと、絵のほうを見ているときに気づけません

        juce::StringArray active;

        if (settings.panoramaMonoSum)      active.add ("Mono Sum");
        if (settings.panoramaInvertRight)  active.add ("Invert R");
        if (settings.panoramaSwap)         active.add ("Swap L/R");

        g.setColour (MantaReverbTheme::highlight());
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (active.isEmpty() ? juce::String ("Width " + juce::String (juce::roundToInt (width * 100.0f)) + " %")
                                      : active.joinIntoString ("   ·   "),
                     plot.removeFromTop (plot.getHeight() / 2 - 40),
                     juce::Justification::centredBottom, false);

        g.setColour (MantaTheme::border());
        g.drawRect (area, 1);
    }

    /** 8.247：描く反復の上限（Phase 257）。**`feedback`が95%でも、
        24回で−30dB**まで落ちます。これ以上は棒が重なって読めません。 */
    static constexpr int maxTwinRepeats = 24;

    /** 8.247：`Twin Delays`で、**聞こえなくなるまで**の長さ（秒）。

        固定の秒数にすると、`Time`を短くしたとき左端に全部固まって読めません
        （テールのときと同じ理由）。 */
    double getTwinVisibleSeconds() const
    {
        const double time = juce::jlimit (MantaReverbTwinDelays::minTimeSeconds,
                                           MantaReverbTwinDelays::maxTimeSeconds,
                                           settings.twin.timeSeconds);

        const float feedback = juce::jlimit (0.0f, MantaReverbTwinDelays::maxFeedback,
                                              settings.twin.feedback);

        const float floorGain = juce::Decibels::decibelsToGain (floorDb);

        int repeats = 1;
        float level = 1.0f;

        while (repeats < maxTwinRepeats && level > floorGain)
        {
            level *= feedback;
            ++repeats;
        }

        return time * (repeats + 1);
    }

    MantaReverbEngine::Settings settings;
};
