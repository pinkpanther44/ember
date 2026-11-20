#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    仕様書6.1「カラーパレット」・6.2「ダークモード」に対応する配色定義。

    ### 使い方

    今までどおり`AppColours::background`のように**値として**読んでください。
    Phase 35でテーマ切り替えに対応しましたが、**呼び出し側の書き方は変えていません**
    （`const`を外して、テーマ切り替え時に中身を差し替える形にしてあります）。
    こうしないと、200箇所以上ある参照をすべて関数呼び出しへ書き換えることになります。

    値の読み書きはすべてメッセージスレッド（描画とUI構築）からなので、
    差し替えと読み取りが競合することはありません。

    ### ライトテーマの値は変えないこと

    ライトは**Phase 34までの見た目そのまま**です。ここを触ると、
    「ダークモードを足したらライトも変わった」という形の後戻りになります。

    ### 制約：切り替えにはアプリの再起動が要る

    `paint()`の中で読んでいる色は差し替えるだけで追従しますが、
    **コンストラクタで`setColour()`して覚え込ませている色は追従しません**
    （Label::textColourId、TextButton::buttonColourId など）。
    半分だけ変わると壊れて見えるため、再起動を促す作りにしています。
    HANDOVER 6.3・1.34を参照。
*/
namespace AppColours
{
    /** 設定に保存する値は宣言順（Light=0, Dark=1, Retro=2）。**並べ替えないこと**——
        保存済みの設定が別の配色として読まれます。 */
    enum class Theme
    {
        Light,
        Dark,

        /** 8.368：**90年代のパソコン風**（Phase 356／本人の要望「UIを全体的にWin95風に切り替えたい」）。

            灰色の地・紺の選択・白い編集面に、**浮き出し／へこみの縁**（`drawBevel()`）を付けたもの。
            明るい地なので、**「ダークかどうか」で分けている所（内蔵プラグインなど）はライト扱い**になります。
            画面に出す名前は「レトロ」（製品名を借りない）。 */
        Retro
    };

    //==========================================================================
    // 面と文字

    extern juce::Colour background;    // ベース（ウィンドウ全体の地）
    extern juce::Colour panel;         // パネル背景（トップバー、ストリップ等）
    extern juce::Colour border;

    extern juce::Colour textPrimary;
    extern juce::Colour textSecondary;

    //==========================================================================
    // 識別色。**テーマが変わっても役割は同じ**なので、明るさだけを合わせて調整する

    extern juce::Colour purple;        // 選択中の要素・アクティブなページタブ
    extern juce::Colour orange;        // 録音・警告・VCAトラックの識別色

    /** 8.360：**パンチイン／アウトの範囲の色**（Phase 349）。ループ（オレンジ）と見分けるための赤。
        ライト／ダーク・Manta／Ember のどれでも同じ値（帯の上で白い字を使わないので、地に合わせなくてよい）。 */
    inline juce::Colour punch() { return juce::Colour (0xffe0453a); }

    /** 8.314：**「借りている点灯」の濃さ**（Phase 307／本人の要望）。

        フォルダをミュート／ソロすると、**中のトラックのM・Sも点きます**
        ——音はもう止まっているのに、**画面のどこにも「黙っている」と
        書いていなかった**ためです。

        **自分で押したものとは濃さで分けます。** 同じ濃さにすると、
        押しても何も起きないように見えます（8.161）——薄い状態から押せば濃くなるので、
        「そのトラック自身のMも立った」ことが分かります。

        **ここに置いてあるのは、使う場所が3つある**からです
        （トラックヘッダー・Console・インスペクタ）。
        色の決めごとは1箇所に集めること（1.34）。 */
    inline constexpr float borrowedOnAlpha = 0.38f;

    /** 仕様書5.1・5.9：拍子とテンポのレーンの札（Phase 142／改善案㉒㉓）。

        **2種類が同じ帯に並ぶ**ので、色で見分けられるようにしてある
        （"3/4"と"140"は形が似ていて、離れて見ると区別できない）。
        どちらも**白い文字を載せる前提**なので、明るくしすぎないこと。 */
    extern juce::Colour timeSignatureMarker;   // 拍子の変化点
    extern juce::Colour tempoMarker;           // テンポの変化点

    //==========================================================================
    // 編集領域（Phase 35で追加）。
    // それまで`juce::Colours::white`を直に塗っていた場所。ダークでは当然そのままでは困る。

    extern juce::Colour canvas;        // タイムライン／ピアノロールの地
    extern juce::Colour canvasAlt;     // 1行おきの薄い帯、黒鍵の行。**canvasより暗いこと**

    extern juce::Colour pianoWhiteKey;
    extern juce::Colour pianoBlackKey;

    /** 仕様書5.2.2：センドトラックの目印（Phase 62／8.1のC6）。

        **半透明で、下の地に重ねる前提**の色です。アレンジ画面の行の地（`canvas`）と
        Consoleのストリップの地（`panel`）という**別々の色の上に載る**ので、
        塗りつぶしの色を2つ持つより、重ねる1色にしたほうが揃います。

        ライトでは黒、ダークでは白を薄く重ねています。**「濃く」ではなく「差をつける」**
        のが目的で、ダークで黒を重ねても隣との差がほとんど出ません。 */
    extern juce::Colour sendTrackTint;

    //==========================================================================
    // 仕様書5.3.1：構成音カラーリング（Phase 46）。
    //
    // ChordCanvasの`DegreeColors.h`にあたる。**あちらは単一テーマ前提**だったので、
    // そのまま持ち込むとライトで飛ぶ／ダークで沈む色が出る（HANDOVER 1.34・8.5）。
    // ここに置いてライトとダークの両方を持たせている。
    //
    // 塗るときは**半透明で行の地に重ねる**前提。不透明で塗ると、
    // 黒鍵の行かどうかが分からなくなる。

    /** 度数ごとの色。`degreeIndex`は`ChordDegree`をintにしたもの
        （0=Root, 1=3rd, 2=4th, 3=5th, 4=6th, 5=7th, 6=9th, 7=11th, 8=13th）。

        **`ChordDegree`の並びと対。** 片方だけ増やすと色がずれる。
        範囲外の値はグレーを返す。 */
    juce::Colour chordDegreeColour (int degreeIndex);

    extern juce::Colour scaleTone;      // スケール内の音
    extern juce::Colour scaleRootTone;  // キーのルート音（スケール内より明るい）

    //==========================================================================

    /** 配色を切り替える。**既に構築済みのコンポーネントには全ては反映されない**ので、
        呼び出し側は再起動を促すこと（このヘッダの説明を参照）。 */
    void setTheme (Theme newTheme);

    Theme getTheme();

    /** 8.368：いまレトロで描いているか。**縁を付けるかどうかは、これで分けること**。 */
    inline bool isRetro() { return getTheme() == Theme::Retro; }

    /** アプリ設定に保存されているテーマを読み出して適用する（起動時に1回呼ぶ）。 */
    void loadThemeFromSettings();

    /** 設定に保存されているテーマ（**再起動するまで、いま描いているものとは違うことがある**）。 */
    Theme getSavedTheme();

    /** 8.368：設定に書く数から配色へ（0＝ライト・1＝ダーク・2＝レトロ。**知らない数はライト**）。
        先の版で配色が増えても、古い版が設定を読んで黒い画面にならないための決まりです。 */
    Theme themeFromStoredValue (int value);

    /** テーマを選んでアプリ設定へ保存する（次回起動から効く）。 */
    void saveThemeToSettings (Theme newTheme);

    /** 今のテーマに合わせた既定のLookAndFeelを作る（Phase 53）。

        **JUCEの既定（`LookAndFeel_V4`）はダーク配色**で、`defaultText`が白です。
        そのため、色を明示していない`Label`／`ComboBox`／`TextEditor`／`ListBox`／
        `PopupMenu`／`AlertWindow`は、**ライトテーマの明るい地の上に白い文字**を
        描いていました（8.1のA1）。ひとつずつ`setColour()`して回っても、
        次に足す部品でまた同じことが起きます。パレットから`ColourScheme`を作って
        既定に据えることで、ここを1箇所で揃えます（1.34）。

        **戻り値の寿命は呼び出し側が持つこと。** `juce::Desktop`へ登録したまま
        壊すとアサートで止まるため、`Main.cpp`が持ち、終了時に既定を外してから
        解放しています。

        **`setTheme()`より後に呼ぶこと。** 呼んだ時点のパレットで作ります。 */
    /** 8.117：アプリの配色を`LookAndFeel_V4`へ渡す形にしたもの（Phase 152）。

        **被せるLookAndFeelは、必ずこれを通すこと。**
        `LookAndFeel_V4`の既定コンストラクタは**ダーク配色**なので、
        渡し忘れると**ライトテーマで白い地に白い文字**になります
        （1.43でつまずいたのと同じ罠。`MixerLookAndFeel`が実際にそうなっていました）。 */
    juce::LookAndFeel_V4::ColourScheme createColourScheme();

    //==========================================================================
    /** 8.131：**角の丸み**（Phase 167／改善案リスト3の43。実験枠）。

        「ボタンや枠、フェーダーのつまみなど、角が丸まっている部分を
        丸めない四角を採用してみてほしい」という試みです。

        ### 戻し方

        **`useSquareCorners`を`false`にするだけ**です。それぞれの場所が
        元々どれだけ丸かったか（3.0f、4.0f……）は`corner()`の引数として
        **そのまま残してある**ので、falseにすればPhase 166までの見た目へ戻ります。

        ### 使い方

        **角を丸める場所は、必ずこれを通すこと。**
        直に数字を書くと、切り替えたときにそこだけ丸いまま残ります（1.27）。

        ```cpp
        g.fillRoundedRectangle (area, AppColours::corner (4.0f));
        ```

        つまみの指針（`MixerLookAndFeel`の回転つまみ）のように、
        **角ではなく形そのものが丸いもの**は対象外です。 */
    constexpr bool useSquareCorners = true;

    /** 8.132：**トランスポートのボタンを記号で出す**（Phase 168／実験枠の44）。

        `false`にすると文字（"Play" / "Stop" / "Rec" / "|<"）へ戻ります。
        **文字はボタンに設定したまま残してある**ので、戻すのはこの1行だけです。 */
    constexpr bool useTransportIcons = true;


    inline float corner (float radiusWhenRounded)
    {
        return useSquareCorners ? 0.0f : radiusWhenRounded;
    }

    /** 8.348：**スクロールバーのつまみも角ばらせる**（Phase 336／本人の要望）。

        `LookAndFeel_V4::drawScrollbar()`をなぞって、丸み（4px）だけ`corner()`に通したもの。
        **スクロールバーを描く LookAndFeel は全部これを呼ぶこと**——アプリ全体の`AppLookAndFeel`と、
        ブラウザのファイル一覧の`FileRowLookAndFeel`（`LookAndFeel_V4`から直に作っているので、
        呼ばないとそこだけ丸いまま残る）。 */
    //==========================================================================
    /** 8.368：**浮き出し／へこみの縁**（Phase 356。レトロの配色だけで描く）。

        | | 外側の左上 | 内側の左上 | 内側の右下 | 外側の右下 | 使う所 |
        |---|---|---|---|---|---|
        | `raised` | 白 | 明るい灰 | 灰 | 黒 | ボタン・つまみ・窓のような面 |
        | `sunken` | 灰 | 黒 | 明るい灰 | 白 | 入力欄・一覧・メーター・編集面 |
        | `raisedThin` | 白 | – | – | 灰 | 並んだ行（トラックの行など。2px だと重い） |
        | `sunkenThin` | 灰 | – | – | 白 | 小さな窪み（フェーダーの溝など） |

        **レトロでなければ何も描かずに false を返します。** 呼ぶ側はいままでの枠をそのまま残して

        ```cpp
        if (! AppColours::drawBevel (g, area, AppColours::Bevel::raised))
            { いままでの枠 }
        ```

        と書けば、ライト／ダークの見た目は1px も変わりません。

        縁は`area`の**内側**に描きます（幅は太いもので2px、細いもので1px）。 */
    enum class Bevel
    {
        raised,
        sunken,
        raisedThin,
        sunkenThin
    };

    bool drawBevel (juce::Graphics& g, juce::Rectangle<int> area, Bevel bevel);

    inline bool drawBevel (juce::Graphics& g, juce::Rectangle<float> area, Bevel bevel)
    {
        return drawBevel (g, area.toNearestInt(), bevel);
    }

    /** レトロの縁の4色（`drawBevel()`の表）。ほかの所で同じ色が要るとき用。 */
    namespace RetroShade
    {
        inline constexpr juce::uint32 highlight  = 0xffffffff;
        inline constexpr juce::uint32 light      = 0xffdfdfdf;
        inline constexpr juce::uint32 shadow     = 0xff808080;
        inline constexpr juce::uint32 darkShadow = 0xff000000;
        inline constexpr juce::uint32 face       = 0xffc0c0c0;

        /** 文字や絵を置く面（入力欄・一覧）。 */
        inline constexpr juce::uint32 window     = 0xffffffff;

        /** ツールチップの地（淡い黄色）。 */
        inline constexpr juce::uint32 info       = 0xffffffe1;
    }

    inline void drawScrollbarThumb (juce::Graphics& g, juce::ScrollBar& scrollbar, int x, int y, int width, int height,
                                    bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver)
    {
        // 8.368：レトロでは、溝を白と灰の市松に、つまみを浮き出した灰の板にする（Phase 356）
        if (isRetro())
        {
            const juce::Rectangle<int> track (x, y, width, height);
            const auto thumb = isScrollbarVertical ? juce::Rectangle<int> (x, thumbStartPosition, width, thumbSize)
                                                   : juce::Rectangle<int> (thumbStartPosition, y, thumbSize, height);

            // 市松は2×2の絵を敷き詰める（1点ずつ描くと、長いスクロールバーで重い）
            static const juce::Image checker = []
            {
                juce::Image image (juce::Image::ARGB, 2, 2, false);
                image.setPixelAt (0, 0, juce::Colour (RetroShade::highlight));
                image.setPixelAt (1, 1, juce::Colour (RetroShade::highlight));
                image.setPixelAt (1, 0, juce::Colour (RetroShade::face));
                image.setPixelAt (0, 1, juce::Colour (RetroShade::face));
                return image;
            }();

            g.setFillType (juce::FillType (checker, juce::AffineTransform()));
            g.fillRect (track);

            if (thumbSize > 0)
            {
                g.setColour (juce::Colour (RetroShade::face));
                g.fillRect (thumb);
                drawBevel (g, thumb, Bevel::raised);
            }

            juce::ignoreUnused (scrollbar, isMouseOver);
            return;
        }

        const auto thumbBounds = isScrollbarVertical ? juce::Rectangle<int> (x, thumbStartPosition, width, thumbSize)
                                                     : juce::Rectangle<int> (thumbStartPosition, y, thumbSize, height);

        const auto colour = scrollbar.findColour (juce::ScrollBar::ColourIds::thumbColourId);
        g.setColour (isMouseOver ? colour.brighter (0.25f) : colour);
        g.fillRoundedRectangle (thumbBounds.reduced (1).toFloat(), corner (4.0f));
    }


    std::unique_ptr<juce::LookAndFeel_V4> createLookAndFeel();
}
