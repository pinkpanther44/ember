#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MantaCompProcessor.h"

#include <vector>

//==============================================================================
/**
    設計書4-2の`CompressorDisplay`：伝達特性グラフ＋In/Out/GRメーター。

    ```
      0┤        ╱‾‾‾‾‾    ／＼    ／＼        │In│Out│GR│
       │      ╱           ／  ＼__／  ＼       │  │  │  │
    -20┤‥‥╱‥‥‥‥‥‥‥‥‥‥‥‥‥‥‥‥‥‥ ← Threshold │  │  │  │
       │ ╱                                    │  │  │  │
    -60┤╱                     ← 左へ流れる     │  │  │  │
       └──────────────────────────────────────┴──┴──┴──┘
    ```

    ### 1枚に重ねる（Phase 210／本人の要望。Pro-C 2と同じ形）

    Phase 209では**伝達特性とレベル履歴を左右に並べて**いましたが、
    **縦軸がどちらもdB**なので、重ねられます。

    - **縦軸** … dB（-60〜0）。**2つの絵で共通**
    - **横軸** … 履歴は時間（右端がいま、左へ流れる）。
      伝達特性は**入力レベル**を幅いっぱいに割り当てたもの

    重ねる利点は場所だけではありません——**Thresholdの横線が、
    伝達特性の折れ目をちょうど通ります**（縦軸と横軸が同じ目盛りなので）。
    線1本で「どこから効くか」が、履歴とカーブの両方で読めます。

    | 何 | 色 |
    |---|---|
    | 伝達特性（**前**） | パープル＋45度の基準線 |
    | 入力レベル | 薄いグレー（うっすら塗る） |
    | 出力レベル | パープル |
    | **圧縮した量** | 2本のあいだを**オレンジ**で塗る |
    | Threshold | オレンジの点線（横） |

    **圧縮した量に別の目盛りを作っていません。** 出力＝入力−リダクション（＋Makeup）
    なので、**2本の線のあいだが、そのまま削った量**です。
    別の軸を重ねると、同じ絵の中に3つ目のdBが並んで読めなくなります。

    **数字は縦軸だけに出します。** 横軸は履歴では時間・カーブでは入力レベルの
    二役なので、そこへ数字を置くと**どちらの目盛りなのか分からなくなります**。

    ### 折れ線は音と同じ式から出す

    `CompressorEngine::computeOutputDb()`を呼んでいます——
    **音を出しているのとまったく同じ関数**です（1.27）。
    別々に書くと、聴こえている効きと描いてある線が食い違い、
    しかも「どちらが正しいのか」が分からなくなります。

    ### いまの位置を点で出す

    折れ線の上に、**いまの入力レベルの位置**を丸で出しています。
    「Thresholdをどれだけ超えているか」は数字より点のほうが速く読めます。

    ### グラフの上でのドラッグ

    仕様書2-2は「できると理想（初期実装では省略可）」としていますが、
    **入れてあります**——Manta EQでカーブを直接掴む操作に慣れたあとで、
    こちらだけつまみに戻るのはちぐはぐなためです。

    重ねたことで、**縦がdBの1本道**になったので割り当ても変えました（Phase 210）。

    | 操作 | 何が変わるか |
    |---|---|
    | 縦ドラッグ | **Threshold**（点線をそのまま持ち上げる） |
    | 横ドラッグ | Ratio |
    | ホイール | Knee |
*/
class CompressorDisplay : public juce::Component,
                           private juce::Timer
{
public:
    explicit CompressorDisplay (MantaCompProcessor& processorToUse);
    ~CompressorDisplay() override;

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    void timerCallback() override;

    /** グラフの場所（メーター3本を除いたぶん）。**1枚だけです**（Phase 210）。 */
    juce::Rectangle<float> getGraphArea() const;

    /** 入力レベル(dB) → 横位置。**伝達特性のためだけ**に使います
        （履歴の横軸は時間）。 */
    float levelToX (float db) const;

    /** dB → 縦位置。**履歴と伝達特性で共通**。 */
    float levelToY (float db) const;

    float xToLevel (float x) const;

    /** 縦位置 → dB（Thresholdのドラッグで使う）。 */
    float yToLevel (float y) const;

    void drawGrid (juce::Graphics& g) const;
    void drawTransferCurve (juce::Graphics& g) const;
    void drawHistory (juce::Graphics& g) const;
    void drawMeters (juce::Graphics& g) const;

    juce::RangedAudioParameter* getParameter (const char* id) const;

    MantaCompProcessor& processor;

    /** メーターの表示値（落ちるのはゆっくり）。 */
    float displayedInputDb[2] { -100.0f, -100.0f };
    float displayedOutputDb[2] { -100.0f, -100.0f };
    float displayedReductionDb = 0.0f;

    /** ドラッグを始めたときの値（そこからの差で動かす）。 */
    bool dragging = false;
    float dragStartRatio = 4.0f;

    /** グラフの目盛り。**下は-60dB、上は0dB**（Thresholdの範囲と揃えてある）。 */
    static constexpr float minDb = -60.0f;
    static constexpr float maxDb = 0.0f;

    static constexpr int meterWidth = 14;
    static constexpr int meterGap = 3;

    /** 縦軸の数字を出す幅（グラフの中の左端）。 */
    static constexpr int scaleWidth = 26;

    /** 履歴を読むための入れ物。**`paint()`のたびに確保しない**ので、
        いちばん広いときのぶんを先に持っておきます。 */
    mutable std::vector<CompressorEngine::HistoryFrame> historyBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorDisplay)
};
