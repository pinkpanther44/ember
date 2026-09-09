#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "LevelMeterComponent.h"
#include "ValueEntrySlider.h"

//==============================================================================
/**
    8.61：アレンジ画面のトラックヘッダーに置く、**音量・パン・メーター**
    （Phase 99／改善案⑨⑩。設計書2.3.1）。

    ### なぜ部品にしたか

    Phase 98まで、ヘッダーのパンは**自前で描いた横棒**でした。Consoleのつまみとは
    別物なので、**同じ「パン」なのに操作が違い**（押した位置へ飛ぶ／飛ばない、
    数値を打ち込めない、右クリックのメニューが出ない）、
    直すたびに2箇所を揃える必要がありました（1.27）。

    **Consoleと同じ部品**（`ValueEntrySlider`＋`ValueReadoutLabel`＋`LevelMeterComponent`）を
    そのまま置けば、**操作は勝手に揃います**。8.25の表もそのまま効きます。

    ### 置き方

    `TimelineComponent`が`getHeaderControlRow()`の矩形をそのまま渡します。

    ```
    ┌─ 上段（`buttonRowHeight`）… ● A S M は親が描く ──┬─┐
    ├─ 下段 ………………… 音量フェーダー │ dB表示 ──┤▮│
    │  （行を高くしたぶんは空ける）              │▮│
    └──────────────────────────┴─┘
                                          メーター
    ```

    ### パンは置きません

    8.64：**Phase 102で外しました**。Phase 100で右端に列を作って置いていましたが、
    **ヘッダーが窮屈になり、フェーダーもボタンも小さいまま**でした。
    パンはConsoleとインスペクタで触れるので、
    **ヘッダーには「よく触るもの」だけを残す**という判断です。

    ### メーターは行の高さに合わせて伸びる

    8.64：**下端はトラック行の下端と揃います**（Phase 102）。
    行を高くしたら、そのぶんメーターも伸びます
    （`getHeaderControlRow()`が行の高さから求まるようになったので、
    こちらは`removeFromRight()`で全高を取るだけで付いてきます）。

    フェーダーとボタンは**大きさを固定**します。行の高さで伸び縮みすると、
    行ごとに操作量が変わってしまうためです。

    **この部品自身はクリックを受け取りません**（`setInterceptsMouseClicks(false, true)`）。
    上段の左半分には親が描いた ● A S M があり、そこを覆ってしまうと押せなくなるためです。
    **中身のつまみは今までどおり受け取ります**（第2引数がtrue）。

    ### 担当トラックは付け替える

    `ChannelStripComponent`は**作り直す**形ですが（8.19）、こちらは
    **並べ直しがタイマーから毎回呼ばれる**ので、作り直すと毎フレーム作っては捨てることになります。
    `setTrack()`で付け替え、**購読も同時に付け替えて**います
    （外し忘れると、消したトラックのツリーを掴んだまま残る。1.15）。
*/
class TrackHeaderControls : public juce::Component,
                             private juce::ValueTree::Listener
{
public:
    explicit TrackHeaderControls (ProjectModel& projectToUse);
    ~TrackHeaderControls() override;

    /** 担当トラックを決める／付け替える。**同じトラックなら何もしません**
        （並べ直しはタイマーから毎回来るので、素通りできるようにしてある）。 */
    void setTrack (const Track& newTrack);

    juce::String getTrackId() const { return track.getId(); }

    /** 仕様書5.7：メーターに出すレベル（`TimelineComponent`がタイマーで配る）。 */
    void setLevels (float leftLevel, float rightLevel) { meter.setLevels (leftLevel, rightLevel); }

    /** 8.54：再生位置。**フェーダーとパンはここの値を表示します**（Phase 93）。 */
    void setPlayheadSeconds (double seconds);


    /** 音量・パンが動いたときに呼ばれる（エンジンへの反映用）。 */
    std::function<void()> onMixerValueChanged;

    /** 8.126：**音量が動いたぶん（dB）**を返す（Phase 162／改善案35）。

        渡すのは**差**であって、いくつになったかではありません。
        フェーダーの位置はトラックごとに違うので、揃えるとバランスが壊れます。

        **誰が選ばれているかを知っているのは呼び出し側**（`TimelineComponent`）なので、
        まとめて動かすかどうかの判断はそちらの仕事です。 */
    std::function<void (const juce::String& trackId, float deltaDb)> onVolumeNudged;


    /** 仕様書5.6：つまみに触れた／離した（Touch/Latchの記録開始・終了）。

        **Consoleのつまみと同じ扱いにすること。** 片方だけ記録が始まると、
        「どこで動かしたか」で結果が変わります。 */
    std::function<void (const juce::String& trackId, const juce::String& targetId)> onTouchStart;
    std::function<void (const juce::String& trackId, const juce::String& targetId)> onTouchEnd;

    void resized() override;

    //==========================================================================
    // 中の割り付け。**親（`TimelineComponent`）も同じ値で ● A S M を置く**ので、
    // ここを変えたら8.61を読み直すこと。

    /** 名前の行の高さ。**親（`TimelineComponent::headerNameRowHeight`）と同じ値**にすること。

        8.65：この部品は**名前の行も含めた領域**を渡されるので（メーターを行の上端まで
        伸ばすため）、名前のぶんを自分で空ける必要がある。 */
    static constexpr int nameRowHeight = 24;

    /** 上段（● A S M が並ぶ帯）の高さ。 */
    static constexpr int buttonRowHeight = 16;

    /** 右端の縦メーターの幅。 */
    static constexpr int meterWidth = 8;

    /** 8.64：音量フェーダーの帯の高さ（Phase 102）。**行の高さで変えない**。 */
    static constexpr int faderRowHeight = 18;

    /** dB表示の幅。 */
    static constexpr int readoutWidth = 36;

private:
    void updateControlsFromModel();
    void updateVisibilityForType();

    /** 他の画面（Console・インスペクタ）で同じ値が変えられたときも表示を合わせる。 */
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    ProjectModel& project;
    Track track { juce::ValueTree() };

    /** 8.54：いまの再生位置（Phase 93）。 */
    double playheadSeconds = 0.0;


    // Consoleのストリップと**同じ部品**（8.25の表がそのまま効く）。
    // 8.64：**パンは持ちません**（Phase 102。Consoleとインスペクタで触る）
    ValueEntrySlider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    ValueReadoutLabel volumeReadout;
    LevelMeterComponent meter;

    bool isUpdatingFromModel = false; // モデル→UI反映中に、UI→モデルの書き戻しを防ぐ

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeaderControls)
};
