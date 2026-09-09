#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "ValueEntrySlider.h"   // Phase 61：ダブルクリックでの数値入力（8.1のC2）

class AudioEngine;

//==============================================================================
/**
    1本のトラックの「信号の通り道」をまとめて表示・操作する部品（Phase 29）。

    中身は上から順に、書き込みモード（仕様書5.6）・VCA割り当て（5.2.4）・
    音源スロット（5.3）・インサート（5.7）・センド（5.2.2）・プリ／ポスト（5.2.2）。
    **並び順は信号の流れと同じ**にしてある。

    ### なぜ切り出したか

    もともとこれらは`ChannelStripComponent`の中だけにあり、Consoleビューでしか
    触れなかった。Phase 29でインスペクタからも同じ操作をできるようにするにあたり、
    **プラグインの選択メニュー・サイドチェイン・センドの追加削除を2箇所へ写すと、
    必ずどちらかが古くなる**（HANDOVER 8.2の「同じ判定を複数箇所に書くと、必ずずれる」）。
    そのため表示も操作もこのクラス1つに集め、ストリップとインスペクタは
    「置く場所」だけを決める形にした。

    ### 幅で見た目を変える

    ミキサーのストリップは104pxしかなく、インスペクタは200px前後ある。
    行の高さと見出しの有無だけを`Layout`で切り替え、**中身と操作は完全に同じ**にしている。

    値の実体はすべてProjectModel（ValueTree）側にあり、この部品は状態を持たない
    （設計書1.2）。
*/
class TrackRackComponent : public juce::Component,
                            public juce::DragAndDropTarget,
                            private juce::ValueTree::Listener
{
public:
    /** 置かれる場所に応じた見た目の違い。中身と操作は共通。 */
    enum class Layout
    {
        Strip,     // Consoleのチャンネルストリップ内（幅が狭い。見出しを出さない）
        Inspector  // 左のインスペクタパネル内（幅に余裕があるので見出しを出す）
    };

    TrackRackComponent (const Track& trackToControl, ProjectModel& projectToUse,
                         AudioEngine& audioEngineToUse, Layout layoutToUse,
                         bool isMasterBusToUse = false);
    ~TrackRackComponent() override;

    /** 8.69：**マスターバス用のラックを作る**（Phase 108／D6。設計書1.4の`MASTERBUS/INSERTS`）。

        出すのは**インサートだけ**です。マスターは音源も送りもVCAも持たず、
        書き込みモードはマスターのストリップが自分で出しています（重ねない）。

        中身は`Track`の道具をそのまま使っています
        （`ProjectModel::getMasterBusInsertHost()`の説明を読むこと）。
        エンジンへは**トラックIDとして空文字**が渡り、「空文字＝マスター」の
        決まりで振り分けられます。 */
    static std::unique_ptr<TrackRackComponent> createForMasterBus (ProjectModel& projectToUse,
                                                                    AudioEngine& audioEngineToUse,
                                                                    Layout layoutToUse);

    void resized() override;

    /** 8.66：インサートの落とし先を示す予告線（Phase 104／改善案㉘㉙）。

        **子の上へ描く**（`paint()`ではなく）。スロットは1pxずつ詰めて並べてあるので、
        地に描くとボタンに隠れて線が細切れになる。 */
    void paintOverChildren (juce::Graphics& g) override;

    /** 音量に影響する操作（センド量・VCA割り当て等）の後に呼ばれる。
        エンジンへ反映するのは置いた側の責務。 */
    std::function<void()> onMixerValueChanged;

    /** スロットの本数が変わって、必要な高さが変わったときに呼ばれる。
        置いた側は自分のレイアウトをやり直すこと。 */
    std::function<void()> onPreferredHeightChanged;

    //==========================================================================
    /** 与えられた幅で並べたときに必要な高さ。置いた側がレイアウト計算に使う。 */
    int getPreferredHeight (int width);

    /** 仕様書5.7：インサートスロットの表示を作り直す（追加・削除の後に呼ぶ）。 */
    void refreshInsertSlots();

    /** 仕様書5.2.2：センドの表示を作り直す（追加・削除の後に呼ぶ）。 */
    void refreshSendSlots();

    /** 仕様書5.3：音源スロットの表示を更新する（割り当て・解除の後に呼ぶ）。 */
    void refreshInstrumentSlot();

    /** 仕様書5.2.4：VCAの割り当て表示を更新する。

        リンク情報はVCAトラック側のValueTreeに入っているため、
        **リンクされる側は自分の購読では変化に気づけない**（HANDOVER 1.19）。
        割り当てが変わったら、外から全ストリップに対してこれを呼ぶ。 */
    void refreshVcaAssignment();

    /** 上の4つをまとめて呼ぶ（ドロップ後など、何が変わったか分からないとき用）。 */
    void refreshAll();

    //==========================================================================
    // 仕様書4.4・6章：ブラウザからのドラッグ&ドロップ（Phase 21）。
    //
    // **受け口（DragAndDropTarget）は置いた側が持つ。** ストリップはフェーダーを含む
    // 全体を落とし先にしており、落とせる範囲をラックだけに狭めたくないため。
    // 判定と実行だけをここに集め、両方の受け口から呼ぶ。

    /** このトラックがドラッグ中のプラグインを受け取れるか（音源はMIDIトラックのみ等）。 */
    static bool canAcceptPluginDrag (AudioEngine& audioEngine, const Track& track,
                                      const juce::var& dragDescription);

    /** ドロップされたプラグインを挿す。挿せたら`onInserted`を呼ぶ（表示の作り直し用）。 */
    static void handlePluginDrop (AudioEngine& audioEngine, const Track& track,
                                   const juce::var& dragDescription,
                                   std::function<void()> onInserted);

    //==========================================================================
    // 8.66：**インサートを箱として動かす**（Phase 104／改善案㉘㉙＋D7）
    //
    // ### なぜ受け口をここに持つのか
    //
    // ブラウザからのプラグインは「このトラックへ挿す」だけなので、
    // 落とし先はストリップ全体で足りていた（上の静的関数の説明）。
    // インサートの並べ替えは**どのスロットの間に落としたか**が意味を持つので、
    // **スロットの位置を知っている側**でないと受けられない。
    //
    // **プラグインのドラッグには興味を示さない。** JUCEは
    // 「興味が無ければ親を見に行く」ので（`DragImageComponent::findTarget`）、
    // ラックの上へプラグインを落としたときは今までどおりストリップが受ける。
    //
    // ### 同じトラックなら並べ替え、違うトラックならコピー
    //
    // 修飾キーでは分けない。**押し間違いに気づけない**（見た目が変わらない）のと、
    // 「別のトラックへ移す」を用意しても、元を消す操作が増えるだけで
    // 得るものが無いため（消したいなら右クリックのメニューから消せる）。

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

private:
    /** 8.66：運んでいるものの種類（Phase 104・105）。

        **インサートとセンドは別の帯**として扱う。同じ扱いにすると、
        センドをインサートの位置へ落としたときに黙って挿さってしまう。 */
    enum class SlotKind { insert, send };

    /** 8.66：ドラッグの中身から種類を見分ける。どちらでもなければfalse。 */
    static bool getSlotKindForDrag (const juce::var& description, SlotKind& kindOut);

    /** 8.66：その位置に落としたら**何番目の前**に入るか。落とせない位置なら-1。

        **その種類の帯の中だけ**を落とし先にしている（インサートならスロットの並び＋
        「+ Insert」）。音源や別の種類の上へ落としたときに、
        黙って並べ替わらないようにするため。 */
    int getSlotDropIndexForPosition (SlotKind kind, juce::Point<int> localPosition) const;

    /** 8.66：予告線を引くY座標。 */
    int getSlotDropLineY (SlotKind kind, int dropIndex) const;

    /** 8.66：ドラッグ中の落とし先（-1＝ドラッグしていない／落とせない位置）。 */
    int slotDropIndex = -1;
    SlotKind slotDropKind = SlotKind::insert;

    /** 実際の配置。`apply`がfalseなら位置を動かさず、必要な高さだけを返す。
        **測る側と置く側で計算を2つ書かないための作り**。片方だけ直して
        高さと実際の配置がずれる、という壊れ方を避けている。 */
    int layOutContents (juce::Rectangle<int> area, bool apply);

    //==========================================================================
    // 仕様書5.3・5.7：スロットの操作（Phase 61で左クリックと右クリックに分けた。8.21）
    //
    //   左クリック … プラグインのGUIを開く（未設定なら選択ダイアログ）
    //   右クリック … メニュー（変更・削除・サイドチェイン）

    /** 音源スロットの左クリック。音源が載っていればGUI、無ければ選択ダイアログ。 */
    void instrumentSlotClicked();

    /** 音源スロットの右クリックメニュー。 */
    void showInstrumentSlotMenu();

    void chooseInstrument();
    void addInsertClicked();

    /** インサートスロットの左クリック。GUIを開く。 */
    void insertSlotClicked (int insertIndex);

    /** インサートスロットの右クリックメニュー（GUI・サイドチェイン・削除）。 */
    void showInsertSlotMenu (int insertIndex);
    void addSendClicked();
    void sendSlotClicked (int sendIndex);
    void prePostClicked();
    void vcaButtonClicked();

    /** モデルの現在値を、状態を持つコントロール（書き込みモード・プリ／ポスト）へ流し込む。 */
    void updateControlsFromModel();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    /** インサート／センド／音源が**他の画面から**足された・外されたときに追従する。

        Phase 29でラックが2箇所（Consoleとインスペクタ）に同時に出るようになったため、
        「操作した側だけが表示を作り直す」やり方では、もう片方が古いまま残る
        （HANDOVER 1.15と同じ形の破綻）。器の子の増減をここで拾って作り直す。 */
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int) override;

    /** 8.66：**並べ替え**に追従する（Phase 104／改善案㉘㉙）。

        増減と違い、`childAdded`も`childRemoved`も飛んでこない。
        これが無いと、Consoleで並べ替えたときに**インスペクタ側だけ古い並びのまま**残る
        （Phase 29で作り直しの取りこぼしを塞いだのと同じ話）。 */
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override;

    /** 上2つの共通処理。どの器が変わったかでスロットの作り直し先を決める。 */
    void refreshSlotsForChangedChild (const juce::ValueTree& parent, const juce::ValueTree& child);

    void notifyMixerChanged();
    void notifyHeightChanged();

    /** 見出しラベルを出すか（幅に余裕のあるインスペクタのみ）。 */
    bool showsCaptions() const { return layout == Layout::Inspector; }

    Track track;
    ProjectModel& project;
    AudioEngine& audioEngine;
    const Layout layout;

    // 仕様書5.2.4：VCAトラックは音声を通さないので、音源・インサート・センドを持たない
    const bool isVca;

    /** 8.69：マスターバスのラックか（Phase 108／D6）。インサートだけを出す。 */
    const bool isMasterBus;

    // 仕様書5.6：オートメーションの書き込みモード（Read/Touch/Latch/Write。Phase 20）
    juce::ComboBox automationModeBox;

    // 仕様書5.2.4：割り当て先の選択ボタン（リンクされる側）と、リンク本数の表示（VCA自身）
    juce::TextButton vcaButton { "VCA: -" };
    juce::Label vcaLinkLabel;

    /** 音源・インサートのスロットに使うボタン（Phase 61／8.1のC1）。

        **左クリックでGUI、右クリックでメニュー**にしてある。
        Phase 60までは左クリックでメニューを出していたが、
        **ポップアップメニューが出ている間はボタンへクリックが届かない**ため、
        「左クリック＝メニュー」と「ダブルクリック＝GUI」は両立できない
        （メニューはモーダルで、2回目のクリックを自分で食べてしまう。8.21）。

        `juce::TextButton`は右クリックでもクリック扱いになるので、
        基底へ渡さないこと（1.39。`ChordPadPanel`の`LeftClickOnlyButton`と同じ話）。 */
    struct SlotButton : public juce::TextButton
    {
        using juce::TextButton::TextButton;

        std::function<void()> onRightClick;

        /** 8.66：**このスロットをドラッグで運ぶときの中身**（Phase 104。`DragAndDropIds`）。

            **空のままなら、そのスロットはドラッグできない。** 音源スロットは1トラックに
            1台しか無いので、並べ替えもコピーも意味がない。 */
        juce::var dragDescription;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onRightClick != nullptr)
                    onRightClick();

                return;
            }

            isDraggingSlot = false;
            juce::TextButton::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            // 8.66：少し動かしたらドラッグに切り替える（Phase 104）。
            // **すぐには始めない**：スロットは高さ20pxしかなく、
            // クリックのたびに数px動くので、いきなり始めるとGUIが開けなくなる
            if (! isDraggingSlot && ! dragDescription.isVoid()
                 && e.getDistanceFromDragStart() > dragStartDistance)
            {
                if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
                {
                    isDraggingSlot = true;
                    container->startDragging (dragDescription, this);
                    return;
                }
            }

            if (! isDraggingSlot)
                juce::TextButton::mouseDrag (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (isDraggingSlot)
            {
                // **基底へ渡さない。** 渡すとクリック扱いになり、
                // 運んだ先に落としたのにGUIまで開いてしまう
                // （JUCEはドラッグ中もドラッグ元へmouseUpを届ける。
                //   `juce_DragAndDropContainer.cpp`の`DragImageComponent::mouseUp`）。
                //
                // ただし押された見た目は自分で戻すこと（基底が戻してくれなくなるため）
                isDraggingSlot = false;
                setState (buttonNormal);
                return;
            }

            juce::TextButton::mouseUp (e);
        }

    private:
        /** クリックとドラッグを分ける距離（px）。 */
        static constexpr int dragStartDistance = 4;

        bool isDraggingSlot = false;
    };

    // 仕様書5.3：音源スロット（MIDIトラックのみ）
    juce::Label instrumentCaption;
    SlotButton instrumentButton { "+ Instrument" };

    // 仕様書5.7：インサートスロット。ボタン1つ＝スロット1つ
    juce::Label insertCaption;
    juce::OwnedArray<SlotButton> insertButtons;
    juce::TextButton addInsertButton { "+ Insert" };

    /** 8.63：**インサートをまとめて通す／通さない**（Phase 101／改善案㉘）。

        1本ずつ切るのは右クリックのメニューから。こちらは
        **「このトラックのインサートを全部外して聴く」**ための入口で、
        ミックスの確認でいちばんよく使う操作。

        キャプション（「インサート」）の行の右端に置く。 */
    juce::TextButton bypassAllInsertsButton;

    /** 全部を通す／通さないを切り替える。**1つでも生きていれば「全部止める」** */
    void toggleAllInsertsBypassed();

    /** いま全部止まっているか（ボタンの見た目に使う）。 */
    bool areAllInsertsBypassed() const;

    // 仕様書5.2.2：センド。1本につき「送り先名のボタン」と「送り量スライダー」の組
    //
    // 8.66：**インサートと同じ`SlotButton`にした**（Phase 105）。
    // ドラッグで運べるようにするのと、右クリックでもメニューが出るようにするため
    // （それまでは左クリックだけで、インサートと操作が食い違っていた）
    juce::Label sendCaption;
    juce::OwnedArray<SlotButton> sendButtons;
    // Phase 61：ダブルクリックで数値入力（8.1のC2）
    juce::OwnedArray<ValueEntrySlider> sendSliders;
    juce::TextButton addSendButton { "+ Send" };

    // 仕様書5.2.2：センドトラック自身の設定（プリ／ポストフェーダー）
    juce::TextButton prePostButton { "Post" };

    bool isUpdatingFromModel = false; // モデル→UI反映中に、UI→モデルの書き戻しを防ぐ

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackRackComponent)
};
