#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

//==============================================================================
/**
    内蔵プラグインで共有するツールバー（Phase 208）。

    ```
    Undo Redo │ A Copy │ Presets                        （右は使う側の場所）
    ```

    Manta EQ（仕様書4.17）で作ったものを、2つ目の内蔵プラグイン（Manta Comp）を
    足すときに切り出しました。**同じものを2つ持つと、必ずどちらかが古くなります**（8.2）。

    ### Undo／Redoの粒度

    プラグインのパラメータは`juce::UndoManager`の対象外なので、
    **値の写しを積む**方式です。

    - 5回／秒で見比べて、**変わっていたら1つ積む**
    - **マウスのボタンが下りているあいだは積まない**
      （ドラッグの途中が刻まれると、Undoを何十回も押すことになります）

    つまり**「手を離すたびに1つ」**の粒度になります。

    ### A/B と Presets

    - **A/B**：値の写しを2つ持ち、ボタンで入れ替える
    - **Copy**：いま鳴っているほうを、もう片方へ写す（A/Bの出発点をそろえる）
    - **Presets**：`%APPDATA%\PersonalDAW\Presets\<フォルダ名>\*.xml`
      （**`apvts.state`ごと**保存するので、画面の見せ方も一緒に戻ります）

    ### 右側は使う側が使う

    `getTrailingArea()`が空いている場所を返します。**使う側が自分の部品を
    このツールバーの子として足し**、そこへ置いてください
    （Manta EQは処理モードとレイテンシー、Manta Compはサイドチェインの表示）。
*/
class MantaPluginToolbar : public juce::Component,
                            private juce::Timer
{
public:
    /** `presetFolderName`は`Presets/`の下に作るフォルダ名（例：`"MantaEQ"`）。 */
    MantaPluginToolbar (juce::AudioProcessor& processorToUse,
                         juce::AudioProcessorValueTreeState& stateToUse,
                         juce::String presetFolderName);
    ~MantaPluginToolbar() override;

    //==========================================================================
    /** 8.173：**exeに入っている出来合いの音**（Phase 213／Manta Synthesizerのため）。

        ユーザーが保存するプリセット（`%APPDATA%`のXML）とは別物です。
        シンセのように「まず何か鳴らしてみたい」ものには、
        **保存済みが1つも無い状態で開くこと自体が壁**になります。

        `apply`は**値を書くだけ**にしてください（画面の作り直しは
        `onStateRestored`が呼ばれます。ユーザープリセットと同じ道）。 */
    struct FactoryPreset
    {
        juce::String category;   ///< メニューの小分け（空なら直下に並ぶ）
        juce::String name;
        std::function<void()> apply;
    };

    /** 工場プリセットを渡す。**渡さなければメニューは今までどおり**です
        （Manta EQ・Manta Compは渡していません）。 */
    void setFactoryPresets (std::vector<FactoryPreset> presets);

    //==========================================================================
    /** 8.318：**いまのプリセットの名前を出す**（Phase 311／本人の要望）。

        > 「専用音源のプリセット表示エリアがあると便利だ。今どのプリセットを
        > 選択しているか一目でわかるし、シンセサイザーなど複数立ち上げる音源は、
        > プリセットが表示されていることで区別化できる」

        **Presetsボタンがそのまま表示欄になります。** 押せば今までどおりメニューが開き、
        ふだんは`Warm Pad ▾`のように**いまの名前**が出ます。
        別に欄を足さなかったのは、**Orangutan Drumsの帯に38pxしか空きが無い**ためです
        （`OrangutanDrumsEditor.h`）——ボタンが広がるだけなら、どの窓にも入ります。

        | | 表示 |
        |---|---|
        | 何も当てていない | `Presets ▾`（今までと同じ言葉） |
        | 当てた直後 | `Warm Pad ▾` |
        | **当てたあとで触った** | `Warm Pad * ▾` |

        ### 名前は**プラグインの状態**に入ります

        `apvts.state`の`presetName`／`presetModified`です。**プロジェクトを開き直しても
        同じ名前が出ます**——出なければ、複数立ち上げた音源の見分けには使えません。

        ### Undo・A/Bでも名前が付いてきます

        値の写しと一緒に**名前も積みます**。Undoで当てる前の音へ戻ったのに
        名前だけ残っている、は起こしません。

        ### 使う側

        **音源4つとエフェクト4つが点けています**。音源が先（Phase 311）で、
        エフェクトは本人の指定で続けて点けました（同じPhase）。
        **Kakapoだけは点けていません**——本人の判断で「要らない」。

        `preferredWidth`は**広げてよい上限**です。帯が狭ければそこまで縮みます
        （Presetsの元の幅74pxより狭くはしません）。

        `reservedTrailingWidth`は**右に残しておく幅**です。帯の右に自分の部品を
        置いている窓は、**その幅を渡すこと**——渡さないと、名前の欄が広がって
        **その部品が押しつぶされます**（Manta EQは処理モードとレイテンシーで434px。
        渡し忘れると、処理モードの箱が**118px→48px**に潰れました）。

        > **重なりはしません。潰れます。** `Rectangle::removeFromRight()`は
        > 残りの幅に合わせて縮むので、見た目は「箱が細い」だけで、
        > 重なりを探しても見つかりません（`PluginPreview`で1度それを数えて、通ってしまいました）。 */
    void setShowsCurrentPreset (bool shouldShow, int preferredWidth = 200, int reservedTrailingWidth = 0);

    /** いま当たっているプリセットの名前（空＝どれでもない）。 */
    juce::String getCurrentPresetName() const   { return currentPreset.name; }

    /** 当てたあとで値を触ったか。名前が空なら常に`false`。 */
    bool isCurrentPresetModified() const;

    /** `apvts.state`に書く名前（**変えないこと**——保存済みのプロジェクトが読めなくなります）。 */
    static const juce::Identifier presetNameProperty;
    static const juce::Identifier presetModifiedProperty;

    //==========================================================================
    /** 8.318：**試験用**（`PluginPreview --all`）。画面から押すのと**同じ道**を通ります。 */
    void choosePresetForTesting (int factoryIndex)  { applyFactoryPreset (factoryIndex); }
    void undoForTesting()                           { undo(); }
    void toggleABForTesting()                       { toggleAB(); }
    void tickForTesting()                           { timerCallback(); }
    void stepPresetForTesting (int direction)       { stepPreset (direction); }

    /** 値が外から入れ替わったとき（Undo・Redo・A/B・プリセット）に呼ばれる。

        **画面の作り直しは使う側の仕事**です——つまみの繋ぎ先や、
        `<UI>`から読んでいる表示は、ここでは触りません。 */
    std::function<void()> onStateRestored;

    void resized() override;

    /** ボタンの右に残っている場所（**このツールバーの座標**）。 */
    juce::Rectangle<int> getTrailingArea() const { return trailingArea; }

    /** 内蔵プラグイン共通のボタンの見た目（使う側の帯でも使えるように公開）。 */
    static void styleButton (juce::TextButton& button, const juce::String& text);

    static constexpr int preferredHeight = 32;

private:
    /** 8.318：**どのプリセットか**（Phase 311）。 */
    struct PresetIdentity
    {
        juce::String name;                  ///< 空＝どのプリセットでもない

        /** 当てた直後の値。**触ったかどうかはこれと見比べて決めます。**
            空＝分からない（開き直した時点で、もう触ってあった）。 */
        std::vector<float> reference;

        /** `reference`が空のときだけ見る。開き直す前に触ってあったなら`true`。 */
        bool modifiedWithoutReference = false;
    };

    /** 値の写しと、**そのときのプリセット**（8.318。Undo・A/Bで名前も戻すため）。 */
    struct Snapshot
    {
        std::vector<float> values;
        PresetIdentity preset;
    };

    /** 8.318：**名前を出すPresetsボタン**。`showsName`が偽なら、ただのTextButtonです。 */
    class PresetButton final : public juce::TextButton
    {
    public:
        bool showsName = false;
        juce::String presetName;
        bool modified = false;

        void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;
    };

    /** 8.321：**前・次のプリセットへ送るボタン**（Phase 312）。三角を描くだけの小さなボタン。

        文字（`◀`）で書かないのは、**フォントに無いと□になる**ためです
        （8.164で★が化けました）。形で描けば、どのフォントでも同じに出ます。 */
    class StepButton final : public juce::TextButton
    {
    public:
        explicit StepButton (bool shouldPointLeft) : pointsLeft (shouldPointLeft) {}

        void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    private:
        const bool pointsLeft;
    };

    /** 8.321：送る順に並べたプリセット（**メニューの並びと同じ**）。 */
    struct PresetStep
    {
        int factoryIndex = -1;   ///< 工場のものなら番号。ユーザーのものなら-1
        juce::File file;         ///< ユーザーのもの
        juce::String name;
    };

    std::vector<PresetStep> getPresetsInMenuOrder() const;

    /** 工場プリセットを**メニューに並ぶ順**で（小分けごと、出てきた順）。
        **メニューと送るボタンの両方がこれを使うこと**——別々に並べると、
        メニューで見た次と、▶で行く先が食い違います。 */
    std::vector<int> getFactoryPresetsInMenuOrder() const;

    /** ユーザーのプリセット（名前順）。**無ければフォルダを作りません**
        （`getPresetFolder()`は作ります。送るボタンを押しただけで、
        本人の置き場所にフォルダが増えるのは避ける）。 */
    juce::Array<juce::File> findUserPresetFiles() const;

    /** `direction`は+1（次）か-1（前）。端まで行ったら反対の端へ回ります。 */
    void stepPreset (int direction);

    void timerCallback() override;

    std::vector<float> captureParameterValues() const;
    void applyParameterValues (const std::vector<float>& values);

    Snapshot captureSnapshot() const;
    void restoreSnapshot (const Snapshot& snapshot);

    PresetIdentity readPresetFromState() const;
    void setCurrentPreset (PresetIdentity preset);
    void writePresetToState (bool modified);
    void refreshPresetDisplay();

    void pushUndoSnapshotIfChanged();
    void undo();
    void redo();
    void toggleAB();
    void copyToOtherSlot();
    void showPresetMenu();
    void applyFactoryPreset (int index);
    void savePresetAs (const juce::String& name);
    void loadPreset (const juce::File& file);
    void refreshButtons();

    juce::File getPresetFolder() const;

    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    juce::String presetFolder;

    juce::TextButton undoButton, redoButton, abButton, copyButton;
    PresetButton presetButton;
    StepButton previousPresetButton { true }, nextPresetButton { false };   // 8.321

    static constexpr int stepButtonWidth = 16;   ///< 8.321：◀・▶の幅（8.324で20→16。名前に場所を回すため）
    static constexpr int stepButtonGap = 2;      ///< 8.321：◀・▶と名前の欄のあいだ

    /** 8.323：**Undo・Redoの幅**（Phase 312／本人の指定で細く。58→48px）。
        狭い窓（Orangutan・EQ）に◀▶の場所を空けるためで、**全部の窓で同じ**です。

        **48より細くしないこと。** `Undo`の文字に47px要ります——最初40pxにして、
        `PluginPreview`の「文字が収まるか」で全部の窓が落ちました。 */
    static constexpr int undoRedoWidth = 48;

    /** 8.324：**プリセット名の字の大きさ**（Phase 312／本人の指定で共通に小さく。14.4→12px）。
        隣のボタン（Undo・A・Copy）は14.4px（JUCEの既定＝ボタンの高さ24pxの60%）のままです。

        **12pxにした理由**（`PluginPreview`の`info`行。全部見える数／`*`付きで見える数）：

        | | 14.4 | 13 | **12** | 11 |
        |---|---|---|---|---|
        | Orangutan（10） | 7／3 | 8／7 | **9／7** | 10／8 |
        | EQ（8） | 4／2 | 6／3 | **7／6** | 8／6 |

        ほかの6つの窓はどの大きさでも全部見えます。11pxでも全部は収まらず、
        隣のボタンの字との差が大きくなりすぎるので、12pxで止めました。 */
    static constexpr float presetNameFontHeight = 12.0f;

public:
    /** 8.324：**名前を描く字**。描くところと、`PluginPreview`が「収まるか」を数えるところで
        **同じものを使うこと**——別々に決めると、数えた結果と見た目が食い違います。 */
    static float presetNameFontHeightForTesting()   { return presetNameFontHeight; }

    /** 8.324：**名前の欄のうち、文字に使える幅**（左右の余白と▾を引いたもの）。
        描くところと`PluginPreview`が**同じ式を使うこと**。 */
    static int getPresetNameTextWidth (int boxWidth)
    {
        return boxWidth - 2 * presetNamePadding - presetChevronWidth - presetChevronGap;
    }

    /** 8.324：**「触った」印（`*`）の幅**。名前との間は**2pxの隙間**にしています——
        空白1文字（12pxで約3px）にすると、EQの`Guitar Body Cut *`が1pxはみ出しました。 */
    static int getPresetMarkWidth (const juce::Font& font)
    {
        return presetMarkGap + juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (font, "*")));
    }

    static constexpr int presetMarkGap = 2;        ///< 8.324：名前と`*`のあいだ
    static constexpr int presetNamePadding = 6;    ///< 8.324：左右の余白（8→6）
    static constexpr int presetChevronWidth = 9;   ///< ▾の幅
    static constexpr int presetChevronGap = 4;     ///< 8.324：名前と▾のあいだ（6→4）

    static juce::Font getPresetNameFont (juce::TextButton& button)
    {
        return button.getLookAndFeel().getTextButtonFont (button, button.getHeight()).withHeight (presetNameFontHeight);
    }

private:
    /** 8.323：**◀▶を出しても、名前の欄にこれだけは残す**（Phase 312）。
        `808 CLASSIC`・`Clean Strum`・`TRAP 808`くらいの名前が収まる幅です。
        これより狭くなる窓では◀▶を出しません（8.321で`808 … *`になったため）。 */
    static constexpr int minimumNameWidthWithSteps = 100;

    std::vector<Snapshot> undoStack, redoStack;
    std::vector<float> lastSnapshot;
    Snapshot slotA, slotB;
    bool usingSlotB = false;

    std::vector<FactoryPreset> factoryPresets;

    PresetIdentity currentPreset;
    bool showsCurrentPreset = false;
    int presetDisplayWidth = 200;
    int reservedTrailing = 0;

    static constexpr int presetButtonWidth = 74;   ///< 名前を出さないときの幅（Phase 208から）

    juce::Rectangle<int> trailingArea;

    static constexpr size_t maxUndoSteps = 64;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaPluginToolbar)
};
