#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SegmentDisplay.h"      // 8.177：数字を棒で描く（Phase 219）
#include "MixerLookAndFeel.h"   // 8.62：つまみとフェーダーの見た目（Phase 100）

//==============================================================================
/**
    **右クリックで数値を打ち込めるスライダー**（Phase 61で新設、Phase 62で操作を見直し。
    8.1のC2／8.21・8.22）。

    | 操作 | 何が起きるか |
    |---|---|
    | ドラッグ | 従来どおり |
    | ダブルクリック | 初期値へ戻る（フェーダー＝0dB、パン＝中央） |
    | 右クリック | メニュー（数値を入力／初期値に戻す） |

    ### 割り当ての経緯

    Phase 61〜63で3回作り直したあと、**Phase 64で表にして決め直しました**（8.25）。
    途中の経緯は8.21・8.22・8.24にありますが、**いま守るべきはこの表だけ**です。

    - **ダブルクリックで初期値へ戻すのは、手が覚えている操作**です。
      数値入力のために取り上げると、戻そうとして入力欄が開きます
    - **右クリックはメニューに統一**しました。Consoleの中でスロットも
      つまみも右クリック＝メニューになり、覚えることが1つで済みます

    ### 右クリックでは値を動かさないこと

    `juce::Slider`は**右クリックでもドラッグとして扱います**
    （`menuEnabled`がfalseだと、右ボタンでも`normRange`の分岐へ落ちる）。
    さらに`setSliderSnapsToMousePosition()`が既定でtrueなので、
    **右クリックした位置へ値が飛びます。**
    そのため`mouseDown()`で右クリックを捕まえ、**基底へ渡していません**。

    ### なぜ`showTextBox()`を使わないか

    JUCE標準の`juce::Slider::showTextBox()`は、**テキストボックス付きで作った
    スライダーにしか効きません**（`valueBox`がnullptrだと何もしない）。
    ミキサーのフェーダー・パンつまみ・センド量はどれも`NoTextBox`で、
    テキストボックスを常時出すと狭いストリップが埋まります。
    そこで、右クリックのときだけ入力欄を重ねる形にしてあります。

    ### Undoとオートメーション

    打ち込みでも`onDragStart` / `onDragEnd`を呼びます。
    **区切り（`beginAction`）とTouch/Latchの記録開始は、どのスライダーも
    そこに書いてある**ので、ここで同じことを書き写すと必ずずれます（HANDOVER 8.2）。
*/
class ValueEntrySlider : public juce::Slider
{
public:
    /** 8.60：**つまみは掴んで動かしたときだけ動く**（Phase 97／改善案㉗）。

        `juce::Slider`は既定で`setSliderSnapsToMousePosition(true)`で、
        **押した位置へ値が飛びます**。細長いフェーダーではこれが事故のもとで、
        「値を読もうとして触ったら-∞まで落ちた」が起きていました。

        **ここ1箇所で切っている**ので、Console・インスペクタ・センド量のどれも
        同じ操作になります（入口ごとに設定すると、必ずどれかを忘れる。1.27）。 */
    ValueEntrySlider (juce::Slider::SliderStyle style, juce::Slider::TextEntryBoxPosition textBoxPosition)
        : juce::Slider (style, textBoxPosition)
    {
        setSliderSnapsToMousePosition (false);

        // 8.62：**見た目もここで被せる**（Phase 100）。Console・インスペクタ・
        // トラックヘッダー・センド量が全部ここを通っているので、
        // 1箇所で全部の見た目が揃う（1.27）
        setLookAndFeel (&mixerLookAndFeel.get());
    }

    ~ValueEntrySlider() override;

    /** 入力欄に出す小数点以下の桁数（既定は1）。 */
    void setValueEntryDecimals (int numDecimals) { entryDecimals = numDecimals; }

    //==========================================================================
    /** 8.60：**見せ方だけを別の単位にする**（Phase 97／改善案⑥）。

        **モデルの値は変えません。** パンは-1.0〜+1.0、Swingは0.0〜1.0のままです。
        ここを変えると**保存済みのプロジェクトが壊れます**（開くたびに値が100倍になる）。

        変えるのは「読み書きする文字」だけで、`getTextFromValue()`と
        `getValueFromText()`の対で実現しています。**対にしておくこと**：
        出す側だけ直すと、打ち込んだ「50」が0.5ではなく50として入ります。 */
    enum class DisplayUnit
    {
        plain,        ///< そのまま（既定）
        panPercent,   ///< -1.0〜+1.0 を **-100〜0〜100** で見せる
        percent       ///<  0.0〜1.0  を **0〜100** で見せる
    };

    void setDisplayUnit (DisplayUnit unit);

    juce::String getTextFromValue (double value) override;
    double getValueFromText (const juce::String& text) override;

    /** 右クリックのメニューを出す（Phase 64／8.25）。

        中身は「数値を入力...」と「初期値に戻す」の2つ。
        **dB表示の右クリックからも同じものを出します**（`ValueReadoutLabel`）。 */
    void showValueMenu();

    /** メニューの「初期値に戻す」に添える説明（"0 dB"、"中央" など）。 */
    void setDefaultValueDescription (const juce::String& text) { defaultValueDescription = text; }

    /** ダブルクリックで戻る値（`setDoubleClickReturnValue()`で決めたもの）へ戻す。

        **`onDragStart` / `onDragEnd`を通します。** dB表示のダブルクリックからも
        同じ動きにするための口で、ここを通さないと
        「つまみをダブルクリックしたときだけUndoの区切りが付く」ことになります。 */
    void resetToDefaultValue();

    /** 数値入力の欄を開く。**外から開く口**でもある

        （Consoleのフェーダーは細長く、つまみの上をクリックすると値が動いてしまうため、
        下のdB表示をクリックしたときにここを呼んでいる。8.22）。 */
    void showValueEntry();

    /** 打ち込まれた文字列を値として適用する。

        **数字を含まない文字列は無視します**（"-∞ dB"や空欄で0dBへ飛ばないように）。
        単位が後ろに付いていても構いません（"-6.0 dB"→-6.0）。

        `onDragStart` / `onDragEnd`を通すので、**Undoの区切りとTouch/Latchの記録も
        つまみで動かしたときと同じ**になります。
        dB表示のラベルから打ち込むときにも、ここを呼んでください（8.22）。 */
    void applyTextValue (const juce::String& text);

    void mouseDown (const juce::MouseEvent& e) override;

private:
    /** 入力を確定する。読み取れない文字列は無視して閉じるだけ。 */
    void commitValueEntry();

    /** 入力欄を片付ける。**コールバックの中から直接deleteしない**ため、
        メッセージスレッドへ回してから捨てる。 */
    void dismissValueEntry();

    /** 入力欄を開いている間だけ、ウィンドウ全体のクリックを見張る。

        **これが無いと入力欄を閉じられません。** フォーカスを受け取らない部品
        （ラベル、自前で描いているタイムライン等）をクリックしても
        `onFocusLost`が飛ばないため、欄が開いたまま残ります（8.22）。 */
    void startWatchingForClicksOutside();
    void stopWatchingForClicksOutside();

    /** 8.62：つまみの見た目（Phase 100）。**`SharedResourcePointer`で持つこと**：
        LookAndFeelは、使っているコンポーネントより長生きする必要がある。

        **メンバーの並びで最初に置くこと。** メンバーは宣言と逆順に壊れるので、
        ここが最後に壊れる（`setLookAndFeel(nullptr)`はデストラクタで呼んでいる）。 */
    juce::SharedResourcePointer<MixerLookAndFeel> mixerLookAndFeel;

    std::unique_ptr<juce::TextEditor> valueEntry;
    juce::Component* watchedTopLevel = nullptr;
    juce::String defaultValueDescription;
    int entryDecimals = 1;
    DisplayUnit displayUnit = DisplayUnit::plain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueEntrySlider)
};

//==============================================================================
/**
    **つまみの値を数字で出すラベル**（Phase 63／8.24）。

    | 操作 | 何が起きるか |
    |---|---|
    | 左クリック | その場で打ち込める（`showEditor()`） |
    | 右クリック | つまみと同じメニュー |

    ### なぜ必要か

    縦のフェーダーは細長く、**つまみの上をクリックすると値が動きます**
    （`juce::Slider::setSliderSnapsToMousePosition()`が既定でtrue）。
    数値を打ち込む場所としては使えないので、**数字が出ているところ**を入口にしています。

    `juce::Label`の`setEditable()`は使いません。**右クリックでも編集が始まってしまい**、
    そちらをメニューに使えないためです。
    **クリックの割り当ては呼び出し側で決められるよう、通知だけを出します。**
*/
class ValueReadoutLabel : public juce::Label
{
public:
    using juce::Label::Label;

    std::function<void()> onLeftClick;
    std::function<void()> onRightClick;

    /** 8.177：**数字は棒で、単位は文字のまま**（Phase 219／`SegmentDisplay.h`）。

        `"0.0 dB"`なら`0.0`を棒で描き、`dB`は右へ小さく添えます。
        Manta Studioでは`canDraw()`が常にfalseなので、**今までどおり**です。 */
    void paint (juce::Graphics& g) override
    {
        if (isBeingEdited() || ! SevenSegment::canDraw (getText()))
        {
            juce::Label::paint (g);
            return;
        }

        SevenSegment::draw (g, getLocalBounds().toFloat(), getText(),
                             findColour (juce::Label::textColourId),
                             getJustificationType());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // **基底へは渡さない。** `juce::Label`は`setEditable()`を使っていると
        // 右クリックでも編集を始めてしまう（1.39のTextButtonと同じ話）
        if (e.mods.isPopupMenu())
        {
            if (onRightClick != nullptr)
                onRightClick();

            return;
        }

        if (onLeftClick != nullptr)
        {
            onLeftClick();
            return;
        }

        juce::Label::mouseDown (e);
    }
};
