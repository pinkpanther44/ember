#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppColours.h"

/**
    8.195：**知らせは出るときだけ出す**（Phase 232／改善案5の2・5）。

    ─────────────────────────────────────────────────────────────────────────
    何が問題だったか
    ─────────────────────────────────────────────────────────────────────────

    アレンジ画面の下に**44px、ピアノロールに22px**、常に居座る帯がありました。
    本人からの要望は「**省略・廃止して作業エリアを広げたい**」。

    ただ、単に消すことはできません。`showStatusMessage()`は**12箇所から
    使われていて**、こういうものが流れます：

        自動保存しました (17:38:58)
        カウントイン中に停止しました（録音していません）。
        再生位置がクリップの内側にありません（分割できませんでした）。

    **消すと、これらが行き場を失います。**

    一方、あの帯が常に占めていた中身の大半は

        Tracks: 3
          - Red Panda [Midi] (12 notes)        ← トラックヘッダーを見れば分かる
        オートメーションの行：線の上をクリックで…  ← 常設の説明文

    で、**トラックヘッダーと重複する一覧**と**説明文**でした。こちらは廃止します。

    ─────────────────────────────────────────────────────────────────────────
    決めたこと
    ─────────────────────────────────────────────────────────────────────────

    | | |
    |---|---|
    | ふだん | **`setVisible(false)`。場所も取りません**（親が`isVisible()`で見る） |
    | 知らせが来たとき | 下端に**1行だけ**出る |
    | そのあと | **数秒で自分から引っ込む** |

    **場所を取らないことが要点です。** 高さ0で置いておくと、親のレイアウトは
    その1行ぶんを引き算し続けます——**広げたいのに広がりません。**

    使う側：

        addChildComponent (statusStrip);   // **`addAndMakeVisible`ではない**

        // 出す
        statusStrip.show (utf8 ("自動保存しました…"));

        // resized() では、見えているときだけ場所を取る
        if (statusStrip.isVisible())
            statusStrip.setBounds (area.removeFromBottom (StatusStrip::height));

    引っ込むときに`onVisibilityChanged`が飛ぶので、**親はそこで`resized()`を
    呼び直します**（8.164：見え方を変えたら、場所を決める人を呼ぶ）。
*/
class StatusStrip : public juce::Component,
                     private juce::Timer
{
public:
    /** 1行ぶん。**文字の高さ＋上下の余白**で決めてあります。 */
    static constexpr int height = 22;

    StatusStrip()
    {
        setInterceptsMouseClicks (false, false);   // 下のものを掴めなくしない
    }

    /** 知らせを出す。**同じ文が続けて来たら、時間だけ延ばします**
        （同じ文で出たり消えたりすると、点滅して見えます）。 */
    void show (const juce::String& message, int millisecondsToKeep = 6000)
    {
        if (message.isEmpty())
            return;

        const bool wasVisible = isVisible();

        text = message;

        setVisible (true);
        repaint();
        startTimer (millisecondsToKeep);

        // **出はじめたときだけ親へ知らせます。** 出ている最中に文が変わっても
        // 場所は同じなので、レイアウトを走らせる必要はありません
        if (! wasVisible && onVisibilityChanged != nullptr)
            onVisibilityChanged();
    }

    /** すぐ引っ込める。 */
    void hide()
    {
        if (! isVisible())
            return;

        stopTimer();
        setVisible (false);

        if (onVisibilityChanged != nullptr)
            onVisibilityChanged();
    }

    /** 出入りのたびに呼ばれます。**親は`resized()`を呼び直してください。** */
    std::function<void()> onVisibilityChanged;

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();

        g.setColour (AppColours::panel);
        g.fillRect (area);

        g.setColour (AppColours::border);
        g.fillRect (area.removeFromTop (1));

        g.setColour (AppColours::textSecondary);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));

        // **1行に収める。** 複数行のメッセージが来ても帯は太らせません——
        // 太らせると、出るたびに下の画面が跳ねます
        g.drawText (text.upToFirstOccurrenceOf ("\n", false, false),
                     area.reduced (8, 0), juce::Justification::centredLeft, true);
    }

private:
    void timerCallback() override
    {
        hide();
    }

    juce::String text;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusStrip)
};
