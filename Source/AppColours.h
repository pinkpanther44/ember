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
    enum class Theme
    {
        Light,
        Dark
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

    /** アプリ設定に保存されているテーマを読み出して適用する（起動時に1回呼ぶ）。 */
    void loadThemeFromSettings();

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


    std::unique_ptr<juce::LookAndFeel_V4> createLookAndFeel();
}
