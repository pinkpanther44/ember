#pragma once

#include "MantaReverbAlgorithm.h"
#include "MantaReverbAllPass.h"
#include "MantaReverbDelayLine.h"
#include "MantaReverbEarly.h"
#include "MantaReverbFdn.h"
#include "MantaReverbPlate.h"
#include "MantaReverbPanorama.h"
#include "MantaReverbSaturation.h"
#include "MantaReverbTwinDelays.h"

#include <array>
#include <cmath>

//==============================================================================
/**
    8.240：**リバーブ1系統ぶん**（Phase 254／リバーブ仕様書3-2の信号フロー）。

    ```
        入力 → プリディレイ → 入口の拡散 ─┬→ 初期反射 ──→ ×Early ─┐
                                          │                        ├→ Width → 出力
                                          └→ FDN（テール）─────────┘
    ```

    ### どこまで入っているか（設計書8章の段階表）

    | | 中身 |
    |---|---|
    | Phase 1 | Room 1系統。Predelay／Decay／Size／Diffusion／Damping／Early／Width／Mix |
    | **Phase 2（いまここ）** | **Plate（Dattorro型）と Hall。`Algorithm`のつまみ** |
    | Phase 3 | Ambience、Size/Shape/Spread |
    | Phase 4 | Twin Delays、Panorama |
    | Phase 5 | デュアルエンジンとルーティング |
    | Phase 6 | Random Hall、Saturation |

    ### 8.243：**形は2つ**（Phase 255）

    | | 形 | 初期反射 |
    |---|---|---|
    | `Room` | 4×4 FDN（寸法：短い） | あり |
    | `Hall` | **同じ4×4 FDN**（寸法：長い） | あり |
    | `Plate` | **Dattorro型タンク**（`MantaReverbPlate`） | **なし**（入口の4段が兼ねる） |

    RoomとHallで箱を分けていないのは、**設計書4-2でHallも4×4の基本FDNと
    決めた**ためです。違いは`MantaReverbAlgorithm`の寸法の表だけ。

    ### 1サンプルずつ回します

    ディレイがPhase 5bでそうしたのと同じ理由です（8.218）。
    FDNは**左右が1本の輪でつながっている**ので、チャンネルごとにブロックを
    回す形では書けません（左を1ブロック進めた時点で、右が読む輪の中身が変わります）。

    ここではまだエンジンが1つですが、**Phase 5でAとBを組むときに、
    この形がそのまま要ります**。

    ### 用意は`prepare()`で

    9.4：**`processBlock()`では確保しない。** ここで確保するのは
    「いちばん長い場合」で、つまみで変わるのは**その中で読む位置**だけです
    （`MantaReverbDelayLine::setLength()`）。
*/
class MantaReverbEngine
{
public:
    /** つまみから作る設定。**ブロックの頭で1回**渡します。 */
    struct Settings
    {
        /** 8.243：どのアルゴリズムか（Phase 255）。 */
        MantaReverbAlgorithm::Kind kind = MantaReverbAlgorithm::Kind::room;

        float predelayMs = 0.0f;
        float decaySeconds = 1.2f;

        /** 1.0が基準。`MantaReverbFdn::maxSizeScale`まで。 */
        float sizeScale = 1.0f;

        /** 0〜1。0なら**入口の拡散を通しません**（8.220の「0は何もしない」）。 */
        float diffusion = 0.7f;

        float highDampFrequency = 6000.0f;
        float highDampAmount = 0.5f;
        float lowDampFrequency = 200.0f;
        float lowDampAmount = 0.2f;

        /** 初期反射の混ぜ量（0〜1）。 */
        float earlyLevel = 0.6f;

        /** 8.246：初期反射の山の位置と散らばり（Phase 256）。**0.5が「表のまま」**。 */
        float shape = 0.5f;
        float spread = 0.5f;

        /** 0＝真ん中に寄せる、1＝そのまま、2＝広げる。**基準は1**（0ではない）。 */
        float width = 1.0f;

        /** 8.247：`Twin Delays`のときだけ使います（Phase 257）。 */
        MantaReverbTwinDelays::Settings twin;

        /** 8.250〜8.251：Phase 6（Phase 260）。**0なら何もしません。** */
        float modulation = 0.0f;
        float saturation = 0.0f;

        /** 8.248：`Panorama`のときだけ（Phase 258）。**`width`はここからも読みます。** */
        bool panoramaMonoSum = false;
        bool panoramaInvertRight = false;
        bool panoramaSwap = false;
    };

    /** 拡散のつまみ0〜1を、オールパスの係数へ移す上限。

        `MantaReverbAllPass::maxCoefficient`いっぱいまでは上げません——
        入口の段は短いので、**強くすると「コッ」という響きが前に出ます。** */
    static constexpr float maxInputDiffusionCoefficient = 0.70f;

    /** プリディレイの上限（ミリ秒）。確保の長さを決めます。 */
    static constexpr double maxPredelayMs = 250.0;

    void prepare (double sampleRateToUse, int maximumBlockSize)
    {
        sampleRate = juce::jmax (8000.0, sampleRateToUse);

        const int predelaySamples = juce::jmax (1, (int) std::ceil (maxPredelayMs
                                                                      * 0.001 * sampleRate));

        // 8.243：**確保はどの表でもいちばん長い寸法で**（Phase 255）
        const auto longest = MantaReverbAlgorithm::getLongestFdnTuning();

        for (int channel = 0; channel < 2; ++channel)
        {
            predelays[(size_t) channel].prepare (predelaySamples);
            early[(size_t) channel].prepare (sampleRate, channel);

            const double ratio = (channel == 0) ? 1.0 : MantaReverbAlgorithm::inputDiffuserRatio;

            for (int stage = 0; stage < numInputDiffuserStages; ++stage)
                diffusers[(size_t) channel][(size_t) stage].prepare (
                    juce::jmax (1, (int) std::ceil (longest.inputDiffuserMs[(size_t) stage]
                                                      * ratio * 0.001 * sampleRate)));
        }

        fdn.prepare (sampleRate);
        plate.prepare (sampleRate);                       // 8.244：Phase 255
        twin.prepare (sampleRate, maximumBlockSize);      // 8.247：Phase 257

        reset();
    }

    void reset()
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            predelays[(size_t) channel].reset();
            early[(size_t) channel].reset();

            for (auto& stage : diffusers[(size_t) channel])
                stage.reset();
        }

        fdn.reset();
        plate.reset();
        twin.reset();
    }

    /** ブロックの頭で1回。 */
    void setSettings (const Settings& newSettings)
    {
        // 8.243：**アルゴリズムが変わったら、移った先を片付けます**（Phase 255／設計書4-5）。
        //
        // 片付けないと、**前に選んでいたときの尾**が数秒後に返ってきます。
        // 設計書の決定どおり**クロスフェードはしません**——
        // 切り替えた瞬間に前の響きが切れますが、それが「切り替えた」ということです
        if (newSettings.kind != settings.kind)
        {
            reset();
            settings.kind = newSettings.kind;
        }

        settings = newSettings;

        // 8.251：**歪みはどの道でも最後に通ります**（Phase 260／仕様書3-2の流れ）。
        // アルゴリズムごとの分岐より前に置くこと——下でreturnする道があるので
        saturation.setAmount (settings.saturation);

        const int predelaySamples = juce::jmax (1, (int) std::round (
            juce::jlimit (0.0f, (float) maxPredelayMs, settings.predelayMs) * 0.001 * sampleRate));

        for (auto& line : predelays)
            line.setLength (predelaySamples);

        const float diffusion = juce::jlimit (0.0f, 1.0f, settings.diffusion);

        //----------------------------------------------------------------------
        // 8.244：**Plateは自分で全部やります**（Phase 255）。
        //
        // 入口の拡散（Dattorroの4段）も、拾い出しの左右も、原典が決めています。
        // こちらの入口の拡散と初期反射は**通しません**
        // （`MantaReverbAlgorithm::getCapabilities()`の`early`が偽）

        //----------------------------------------------------------------------
        // 8.248：**Panoramaもリバーブではありません**（Phase 258／仕様書2-1）。
        // 響きは一切足さず、入ってきたステレオを組み替えるだけです

        if (settings.kind == MantaReverbAlgorithm::Kind::panorama)
        {
            diffuserActive = false;
            lateLevel = 1.0f;

            MantaReverbPanorama::Settings panoramaSettings;
            panoramaSettings.width = settings.width;
            panoramaSettings.monoSum = settings.panoramaMonoSum;
            panoramaSettings.invertRight = settings.panoramaInvertRight;
            panoramaSettings.swapChannels = settings.panoramaSwap;

            panorama.setSettings (panoramaSettings);
            return;
        }

        //----------------------------------------------------------------------
        // 8.247：**Twin Delaysはリバーブではありません**（Phase 257／仕様書2-1）。
        //
        // 部屋の寸法も初期反射も拡散も通りません——通っている道はプリディレイと
        // `Width`だけです（`MantaReverbAlgorithm::getCapabilities()`）

        if (settings.kind == MantaReverbAlgorithm::Kind::twinDelays)
        {
            diffuserActive = false;
            lateLevel = 1.0f;

            auto twinSettings = settings.twin;
            twinSettings.spread = settings.spread;   // **Spreadは2本の間隔として効きます**

            twin.setSettings (twinSettings);

            // **`setSettings()`のあとで呼ぶこと。** `beginBlock()`は寄せる先の
            // ディレイタイムをそこから出します（`MantaDelayEngine`）
            twin.beginBlock();
            return;
        }

        if (settings.kind == MantaReverbAlgorithm::Kind::plate)
        {
            diffuserActive = false;
            lateLevel = 1.0f;

            plate.setSize (settings.sizeScale);
            plate.setDecaySeconds (settings.decaySeconds);
            plate.setDiffusion (diffusion);
            plate.setDamping (settings.highDampFrequency, settings.highDampAmount,
                               settings.lowDampFrequency, settings.lowDampAmount);
            return;
        }

        //----------------------------------------------------------------------
        // `Room`と`Hall`。**違うのは寸法の表だけ**（8.243）

        const auto& tuning = MantaReverbAlgorithm::getFdnTuning (settings.kind);
        const auto& pattern = MantaReverbAlgorithm::getEarlyPattern (settings.kind);

        // **0は素通し**（8.220の②）。混ぜ具合で畳むので、切り替えで音は飛びません
        diffuserAmount = diffusion;
        diffuserActive = diffusion > 0.0001f;

        for (int channel = 0; channel < 2; ++channel)
        {
            const double ratio = (channel == 0) ? 1.0 : MantaReverbAlgorithm::inputDiffuserRatio;

            for (int stage = 0; stage < numInputDiffuserStages; ++stage)
            {
                auto& allPass = diffusers[(size_t) channel][(size_t) stage];

                allPass.setLength (juce::jmax (1, (int) std::round (
                    tuning.inputDiffuserMs[(size_t) stage] * ratio * 0.001 * sampleRate)));

                allPass.setCoefficient (diffusion * maxInputDiffusionCoefficient);
            }

            early[(size_t) channel].setPattern (pattern);
            early[(size_t) channel].setSize (settings.sizeScale);
            early[(size_t) channel].setShapeAndSpread (settings.shape, settings.spread);
        }

        // 8.245：**テールの釣り合いは表が持っています**（Phase 256）。
        // `Ambience`だけ下げてあります——「空気感」なのにテールが同じ大きさで出ていると、
        // ただの狭い部屋になります
        lateLevel = (float) tuning.lateLevel;

        fdn.setTuning (tuning);
        fdn.setSize (settings.sizeScale);
        fdn.setDecaySeconds (settings.decaySeconds);

        // 8.250：**揺らすのは`Random Hall`だけ**（Phase 260）。
        // 判断は`getCapabilities()`ただ1つ——画面のグレーアウトと同じものを見ます。
        // **`setSize()`の後で呼ぶこと**：深さはラインの長さから決まります
        fdn.setModulation (MantaReverbAlgorithm::getCapabilities (settings.kind).modulation
                               ? settings.modulation
                               : 0.0f);

        // **ライン内のオールパスは、入口より強くしてよい**——
        // 長さが10ms前後あるので、詰まった感じにはなりません
        fdn.setDiffusion (diffusion * MantaReverbAllPass::maxCoefficient);

        fdn.setDamping (settings.highDampFrequency, settings.highDampAmount,
                         settings.lowDampFrequency, settings.lowDampAmount);
    }

    /** 1サンプルぶん。**出てくるのはウェットだけ**——原音との混ぜ具合は呼ぶ側です。 */
    void processSample (float inputLeft, float inputRight, float& wetLeft, float& wetRight)
    {
        std::array<float, 2> diffused { { 0.0f, 0.0f } };
        std::array<float, 2> earlyOut { { 0.0f, 0.0f } };

        const std::array<float, 2> input { { inputLeft, inputRight } };

        // 8.248：**Panoramaは別の道**（Phase 258）。プリディレイだけ通します。
        //
        // **`applyWidth()`は通しません**——`MantaReverbPanorama`が
        // `Width`も`Mono Sum`もまとめて持っているので、通すと**2回掛かります**
        if (settings.kind == MantaReverbAlgorithm::Kind::panorama)
        {
            panorama.processSample (predelays[0].processSample (inputLeft),
                                     predelays[1].processSample (inputRight),
                                     wetLeft, wetRight);

            // 8.251：**ここにも掛けること**（Phase 260）。`Panorama`は`applyWidth()`を
            // 通らない（自分でM/Sを持っている）ので、書き忘れるとここだけ歪みません
            if (saturation.isActive())
            {
                wetLeft = saturation.processSample (wetLeft);
                wetRight = saturation.processSample (wetRight);
            }

            return;
        }

        // 8.247：**Twin Delaysは別の道**（Phase 257）。
        // プリディレイだけ通して、あとは2本のディレイと`Width`です
        if (settings.kind == MantaReverbAlgorithm::Kind::twinDelays)
        {
            const float delayedLeft = predelays[0].processSample (inputLeft);
            const float delayedRight = predelays[1].processSample (inputRight);

            float twinLeft = 0.0f;
            float twinRight = 0.0f;

            twin.processSample (delayedLeft, delayedRight, twinLeft, twinRight);

            applyWidth (twinLeft, twinRight, wetLeft, wetRight);
            return;
        }

        const bool isPlate = (settings.kind == MantaReverbAlgorithm::Kind::plate);

        for (int channel = 0; channel < 2; ++channel)
        {
            float x = predelays[(size_t) channel].processSample (input[(size_t) channel]);

            if (diffuserActive)
            {
                float d = x;

                for (auto& stage : diffusers[(size_t) channel])
                    d = stage.processSample (d);

                x = x * (1.0f - diffuserAmount) + d * diffuserAmount;
            }

            diffused[(size_t) channel] = x;

            // **初期反射は拡散した後から取ります。** 拡散の前から取ると、
            // 反射1発1発が裸のままで、部屋というより山びこに聞こえます。
            //
            // 8.244：**Plateでは回しません**（Phase 255）。板に「壁の形」はありません
            if (! isPlate)
                earlyOut[(size_t) channel] = early[(size_t) channel].processSample (x);
        }

        float lateLeft = 0.0f;
        float lateRight = 0.0f;

        if (isPlate)
            plate.processSample (diffused[0], diffused[1], lateLeft, lateRight);
        else
            fdn.processSample (diffused[0], diffused[1], lateLeft, lateRight);

        const float earlyLevel = isPlate ? 0.0f : settings.earlyLevel;

        float left = lateLeft * lateLevel + earlyOut[0] * earlyLevel;
        float right = lateRight * lateLevel + earlyOut[1] * earlyLevel;

        applyWidth (left, right, wetLeft, wetRight);
    }

private:
    /** **Widthは最後**（仕様書3-2の`Wet Out → Level/Width`）。

        M/Sの取り方はManta EQと同じ（`M=(L+R)/2`、`S=(L−R)/2`）——
        **1倍で素通し**になる取り方であること。

        8.247：**どの道からも通します**（Phase 257）。Twin Delaysだけ
        別に書くと、片方だけ直す日が来ます（1.27）。 */
    void applyWidth (float left, float right, float& outputLeft, float& outputRight) const
    {
        // 8.251：**歪みは`Width`より前**（Phase 260／仕様書3-2の
        // `… → Saturation → Wet Out → Level/Width`）。
        //
        // 後に掛けると、`Width`で持ち上がったサイドだけが先に潰れて、
        // **広げるほど真ん中へ寄っていく**という妙な効きになります
        if (saturation.isActive())
        {
            left = saturation.processSample (left);
            right = saturation.processSample (right);
        }

        const float mid = 0.5f * (left + right);
        const float side = 0.5f * (left - right) * settings.width;

        outputLeft = mid + side;
        outputRight = mid - side;
    }

    static constexpr int numInputDiffuserStages = 2;

    double sampleRate = 44100.0;

    Settings settings;

    std::array<MantaReverbDelayLine, 2> predelays;
    std::array<std::array<MantaReverbAllPass, (size_t) numInputDiffuserStages>, 2> diffusers;
    std::array<MantaReverbEarly, 2> early;

    /** 8.243：**どちらか一方だけが回ります**（Phase 255）。

        両方持っておくのは、**切り替えで確保が起きないようにする**ためです（9.4）。
        使っていないほうは回らないので、CPUは食いません。 */
    MantaReverbFdn fdn;
    MantaReverbPlate plate;
    MantaReverbTwinDelays twin;   // 8.247（Phase 257）
    MantaReverbPanorama panorama; // 8.248（Phase 258）。**状態を持ちません**

    /** 8.251：**出口の歪み**（Phase 260）。こちらも状態を持ちません。 */
    MantaReverbSaturation saturation;

    float diffuserAmount = 0.0f;
    bool diffuserActive = false;

    /** 8.245：テールと初期反射の釣り合い（`FdnTuning::lateLevel`）。 */
    float lateLevel = 1.0f;
};
