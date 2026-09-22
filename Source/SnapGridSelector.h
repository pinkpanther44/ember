#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SnapGrid.h"
#include "ToolbarLayout.h"   // 8.307：入切ボタンはツールと同じ大きさ（Phase 300）

//==============================================================================
/**
    仕様書5.5・5.9：**編集の刻み（スナップ）を選ぶ小さな部品**（Phase 55）。

    ### なぜ部品にしたか

    Phase 54では入口をトランスポートバーの1つだけにしていましたが、
    **エディタをポップアウトすると別ウィンドウになる**ため、そこからは触れませんでした
    （ノートを打ち込んでいる最中こそ刻みを変えたい）。
    そこでアレンジ画面とピアノロールの両方に置いています。

    **同じ部品を2箇所に出すことになるので、値は必ずモデルから来ること**（HANDOVER 1.27）。
    このクラスは表示役で、**選ばれた値を自分では覚えません。**
    変更は`onSnapGridChanged`で外へ渡し、表示は`setSnapGrid()`で外から合わせます。
    片方で変えたらもう片方も追従させるのは`MainComponent`の仕事です。

    ### 「Snap」と書いてあるのはなぜか

    ピアノロールには**クオンタイズのグリッド**（`gridBox`）が並んでいて、
    そちらも「1/16」のような同じ表示になります。**どちらがどちらか分からなくなる**ので、
    見出しを付けています。記号は使いません（この環境のフォントに無い。1.30）。

    ### 8.307：見出しは**ボタン**になりました（Phase 300／本人の指定）

    > 「Arrange、Editor画面の[Snap]をボタンにしよう。デザインサイズは、
    > ツールボタン、自動スクロールボタンと同じ。**デフォルトで[Snap]ボタンはOn**。
    > Off時にフリーとなる。その為、**Snap選択タブ内のフリーは無くそう**」

    入切と刻みが**別の操作**になりました。Phase 299までは
    「フリー」もコンボの1項目だったので、**寄せるのをやめるには、
    いま選んでいる音価を覚えてから「フリー」を選び、戻すときに選び直す**必要がありました
    ——押して戻せるボタンなら、覚えておくのは画面の仕事になります。

    **切ったときに選んでいた音価は、コンボがそのまま持っています。**
    モデルへ渡す値だけが`SnapGrid::off`になり、入れ直せば元の音価へ戻ります。

    ### 「3」ボタン（8.271／Phase 272）

    コンボの右隣にあり、**選んでいる音価を3連にします**。
    コンボは5つ（小節・1/4〜1/32。8.307でフリーが外れました）で、
    モデルへ渡す値だけが`SnapGrid::eighthTriplet`のような合わさったものになります。
*/
class SnapGridSelector : public juce::Component
{
public:
    SnapGridSelector();

    void resized() override;

    /** 刻みが選び直されたときに呼ばれる。**モデルを書き換えるのは受け手の仕事。** */
    std::function<void (SnapGrid)> onSnapGridChanged;

    /** 表示をモデルの値へ合わせる（通知は飛ばさない）。

        **もう片方の入口で変えられたときにも呼ぶこと。** 呼ばないと、
        アレンジとピアノロールで違う値が出たまま残ります（1.27）。 */
    void setSnapGrid (SnapGrid grid);

    /** 8.271：「3」ボタンの幅と、コンボとのあいだ（Phase 272）。

        **`preferredWidth`より前に書くこと。** static constexprメンバの初期化式は
        「クラスを読み終えてから」ではなく**その場**で名前を引くので、
        後ろに置くと`preferredWidth`から見えません。 */
    static constexpr int tripletButtonWidth = 26;
    static constexpr int tripletButtonGap = 4;

    /** 見出しと本体と「3」を並べたときの推奨幅。呼び出し側のレイアウト計算に使う。

        8.271：**3連のボタンぶん広がりました**（Phase 272）。
        `PianoRollView`は折り返すかどうかをこの値から決めていますが、
        `preferredWidth`を読んでいるだけなので、あちらを直す必要はありません。 */
    static constexpr int preferredWidth = ToolbarLayout::toolButtonWidth + 4 + 74
                                            + tripletButtonGap + tripletButtonWidth;

private:
    /** 8.307：**入切のボタン**（Phase 300／本人の指定）。

        **大きさはツール・自動スクロールと同じ**（`ToolbarLayout::toolButtonWidth`）。
        同じ帯に並ぶ押しものは、同じ大きさであること（8.196）。 */
    juce::TextButton snapButton { "Snap" };

    juce::ComboBox box;

    /** 「Snap」の見た目（選択色）を合わせる。**ツールのボタンと同じ形**にすること
        （`ArrangeView::updateAutoScrollButton()`と揃えてあります）。 */
    void updateSnapButton();

    //==========================================================================
    // 8.271：**3連符**（Phase 272／本人の要望。仕様書5.5）
    //
    // ### なぜコンボの項目ではなくボタンなのか
    //
    // 項目にすると**10個**（フリー・小節・4音価・3連4つ）になり、
    // 開いてから目で探す手間が増えます。3連は**同じ音価のまま裏返す**操作なので、
    // 押して戻せるボタンのほうが、行ったり来たりするときに速い。
    //
    // **モデルには合わさった値が1つだけ**入ります（`SnapGrid.h`）。
    // 画面が2つに分かれているのはここだけの話です。

    juce::TextButton tripletButton { "3" };

    /** コンボと「3」から刻みを組み立てて、外へ渡す。 */
    void sendCurrentGrid();

    /** 「3」の見た目（選択色）と、押せるかどうかを合わせる。

        **小節とフリーでは押せません**（`snapGridCanBeTriplet()`）。
        押せるのに何も起きないほうが、理由が分からず困ります。 */
    void updateTripletButton();

    /** モデル→表示の反映中に、表示→モデルの書き戻しを防ぐ（TransportBarComponentと同じ形）。 */
    bool isUpdatingFromModel = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapGridSelector)
};
