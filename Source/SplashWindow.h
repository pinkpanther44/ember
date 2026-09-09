#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    8.151：**起動画面**（Phase 189／改善案⑰）。

    ### 何を隠しているか

    Phase 188まで、起動すると**組み立ての途中が見えていました**——
    メインウィンドウを`setVisible(true)`してから中身を並べ、そのあと
    `setFullScreen(true)`で広げていたので、**小さいまま描かれた画面が
    一瞬だけ大きな窓の左上に貼り付いて見える**、という状態でした。

    直し方は2つあります。

    | | |
    |---|---|
    | 描き終わるまで窓を出さない | **これ。** 途中が見えない |
    | 途中を速くする | 速くしても0にはならない。デバッグビルドでは特に |

    起動画面は、**その「出さない間」に見せるもの**です。
    出さないだけだと、押しても何も起きないアプリに見えます。

    ### 動かさない

    **進み具合を回すような絵を入れていません。** 重い初期化
    （`AudioEngine::initialise()`、プラグイン一覧、ビューの組み立て）は
    **メッセージスレッドで動きます**——その間このウィンドウは再描画されないので、
    animationは必ず途中で止まります。**止まった動く絵は、固まったアプリに見えます。**
    代わりに、いま何をしているかを文字で出しています（`setStatus()`）。

    ### 絵の差し替え

    `Resources/Splash/splash.png`を置いてビルドすると、それが全面に出ます
    （CMakeが**あるときだけ**埋め込みます。無ければアプリのアイコン＋名前を描きます）。
    **ファイル名はASCIIにすること**——`juce_add_binary_data`はファイル名から
    C++の識別子を作ります（8.133と同じ話）。
*/
class SplashWindow : public juce::Component,
                      private juce::Timer
{
public:
    SplashWindow();
    ~SplashWindow() override;

    /** 下に出す一行（「オーディオを準備しています…」など）。 */
    void setStatus (const juce::String& newStatus);

    /** 閉じて、**最低表示時間が過ぎてから**`next`を呼ぶ。

        `next`は**非同期で呼ばれます**。この中でこのウィンドウが破棄されるので、
        自分のスタックの上で呼ぶわけにいきません（1.5）。 */
    void closeAndThen (std::function<void()> next);

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;
    void finish();

    /** これより短くは出しません。**速い環境で一瞬だけ光るのを避けるため**——
        点滅は、出さないより印象が悪い。 */
    static constexpr int minimumDisplayMs = 700;

    juce::Image artwork;      // 差し替え用の絵。無ければ空
    juce::Image appIcon;      // 既定の絵に使うアプリのアイコン
    juce::String status;

    juce::uint32 shownAtMs = 0;
    std::function<void()> onClosed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplashWindow)
};
