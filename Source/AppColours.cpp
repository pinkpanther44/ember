#include "AppColours.h"
#include "AppSettings.h"
#include "Branding.h"   // 8.175：ブランドごとのアクセント色（Phase 216）

namespace AppColours
{
    namespace
    {
        const juce::String themeKey { "theme" };

        Theme currentTheme = Theme::Light;

        /** 1テーマぶんの値。**追加するときはlight/darkの両方を必ず埋めること**
            （片方だけ足すと、そのテーマでだけ黒（初期値）になる）。 */
        struct Palette
        {
            juce::uint32 background;
            juce::uint32 panel;
            juce::uint32 border;
            juce::uint32 textPrimary;
            juce::uint32 textSecondary;
            juce::uint32 purple;
            juce::uint32 orange;
            juce::uint32 timeSignatureMarker;
            juce::uint32 tempoMarker;
            juce::uint32 canvas;
            juce::uint32 canvasAlt;
            juce::uint32 pianoWhiteKey;
            juce::uint32 pianoBlackKey;
            juce::uint32 sendTrackTint;

            // 仕様書5.3.1：度数ごとの色（ChordDegreeの並びと対）と、スケール音モードの2色
            juce::uint32 chordDegree[9];
            juce::uint32 scaleTone;
            juce::uint32 scaleRootTone;
        };

        // 仕様書6.1：ライトテーマ。**Phase 34までの見た目そのまま**なので値を変えないこと。
        // canvas / canvasAlt / pianoXxxKey は、それまで直に塗っていた色をそのまま写したもの。
        constexpr Palette lightPalette
        {
            0xfff0f0f2, // background   ベース：明るめグレー
            0xffe4e4e7, // panel
            0xffc9c9cc, // border
            0xff2c2c2a, // textPrimary
            0xff5f5e5a, // textSecondary
            0xff7c5cff, // purple
            0xffff8a3d, // orange
            0xff2f6fd0, // timeSignatureMarker（青。白文字が乗る濃さ）
            0xff1f8a5a, // tempoMarker（緑。同上）
            0xffffffff, // canvas       （旧 juce::Colours::white）
            0xfff0f0f2, // canvasAlt    （旧 background の流用）
            0xffffffff, // pianoWhiteKey（旧 juce::Colours::white）
            0xff000000, // pianoBlackKey（旧 juce::Colours::black）
            0x14000000, // sendTrackTint（黒を薄く重ねる。明るい地の上）

            // 仕様書5.3.1の対応表（Root=赤系／3rd=水色／4th=エメラルド／5th=黄色／
            // 6th=オレンジ／7th=青／9th=紫／11th=緑／13th=ピンク）。
            // **色みはChordCanvasのDegreeColors.hと同じ**で、明るい地に半透明で
            // 重ねても分かるよう、こちらは濃いめの値にしてある。
            {
                0xffd23f3f, // Root       赤
                0xff2196c4, // Third      水色
                0xff1fae86, // Fourth     エメラルド（sus4）
                0xffc9a90f, // Fifth      黄色
                0xffd97a1f, // Sixth      オレンジ
                0xff3448c8, // Seventh    青
                0xff9333c8, // Ninth      紫
                0xff2fa83f, // Eleventh   緑
                0xffcc3f92, // Thirteenth ピンク
            },
            0xff3a5ad9, // scaleTone
            0xff6b86f0, // scaleRootTone
        };

        // 仕様書6.2：ダークテーマ（Phase 35）。
        // **役割の対比はライトと揃えてある**：panelはbackgroundより明るく、
        // canvasAltはcanvasより暗い（黒鍵の行が暗く見える関係を保つため）。
        // パープルとオレンジは、暗い地の上で沈まないよう少しだけ明るくしている。
        constexpr Palette darkPalette
        {
            0xff1e1e22, // background
            0xff2a2a30, // panel
            0xff3f3f47, // border
            0xffe8e8ea, // textPrimary
            0xff9a9aa2, // textSecondary
            0xff8f74ff, // purple
            0xffff9a55, // orange
            0xff4a86e8, // timeSignatureMarker（暗い地の上なのでライトより明るい）
            0xff2fa86e, // tempoMarker
            0xff1a1a1e, // canvas
            0xff141418, // canvasAlt
            0xffb6b6bc, // pianoWhiteKey
            0xff141418, // pianoBlackKey
            0x18ffffff, // sendTrackTint（暗い地の上では白を薄く重ねる）

            // 暗い地の上に重ねるので、ライトより明るい値。
            // **ChordCanvasのDegreeColors.hの値そのまま**（あちらが暗い地の前提だった）。
            {
                0xffe05555, // Root       赤
                0xff55c8e0, // Third      水色
                0xff55e0b8, // Fourth     エメラルド（sus4）
                0xffe6d84f, // Fifth      黄色
                0xffe09a55, // Sixth      オレンジ
                0xff4d66e8, // Seventh    青
                0xffb45fe8, // Ninth      紫
                0xff5fe86b, // Eleventh   緑
                0xffe85fb4, // Thirteenth ピンク
            },
            0xff4a6cf0, // scaleTone
            0xff7a94ff, // scaleRootTone
        };

        /** 仕様書5.3.1：度数ごとの色の実体。`chordDegreeColour()`から引く。

            他の色と違って`extern`で直接出していないのは、**添字を間違えたまま
            使われないため**（`ChordDegree`の並びと対であることを関数側で守る）。
            初期値は他の色と揃えてライト。 */
        juce::Colour degreeColours[9]
        {
            juce::Colour (lightPalette.chordDegree[0]), juce::Colour (lightPalette.chordDegree[1]),
            juce::Colour (lightPalette.chordDegree[2]), juce::Colour (lightPalette.chordDegree[3]),
            juce::Colour (lightPalette.chordDegree[4]), juce::Colour (lightPalette.chordDegree[5]),
            juce::Colour (lightPalette.chordDegree[6]), juce::Colour (lightPalette.chordDegree[7]),
            juce::Colour (lightPalette.chordDegree[8]),
        };

        void applyPalette (const Palette& p)
        {
            background    = juce::Colour (p.background);
            panel         = juce::Colour (p.panel);
            border        = juce::Colour (p.border);
            textPrimary   = juce::Colour (p.textPrimary);
            textSecondary = juce::Colour (p.textSecondary);
            purple        = juce::Colour (p.purple);
            orange        = juce::Colour (p.orange);
            timeSignatureMarker = juce::Colour (p.timeSignatureMarker);
            tempoMarker         = juce::Colour (p.tempoMarker);
            canvas        = juce::Colour (p.canvas);
            canvasAlt     = juce::Colour (p.canvasAlt);
            pianoWhiteKey = juce::Colour (p.pianoWhiteKey);
            pianoBlackKey = juce::Colour (p.pianoBlackKey);
            sendTrackTint = juce::Colour (p.sendTrackTint);

            scaleTone     = juce::Colour (p.scaleTone);
            scaleRootTone = juce::Colour (p.scaleRootTone);

            for (int i = 0; i < 9; ++i)
                degreeColours[i] = juce::Colour (p.chordDegree[i]);
        }
    }

    //==========================================================================
    // 実体。初期値はライト（設定を読み込む前でも、まともな色で描けるように）。
    juce::Colour background    { lightPalette.background };
    juce::Colour panel         { lightPalette.panel };
    juce::Colour border        { lightPalette.border };
    juce::Colour textPrimary   { lightPalette.textPrimary };
    juce::Colour textSecondary { lightPalette.textSecondary };
    // 8.175：**この2つだけブランドで決まります**（Phase 216）。
    // `setTheme()`が呼ばれる前に読まれても、Emberでパープルが出ないように
    juce::Colour purple        { Branding::accentPrimary (false) };
    juce::Colour orange        { Branding::accentSecondary (false) };
    juce::Colour timeSignatureMarker { lightPalette.timeSignatureMarker };
    juce::Colour tempoMarker         { lightPalette.tempoMarker };
    juce::Colour canvas        { lightPalette.canvas };
    juce::Colour canvasAlt     { lightPalette.canvasAlt };
    juce::Colour pianoWhiteKey { lightPalette.pianoWhiteKey };
    juce::Colour pianoBlackKey { lightPalette.pianoBlackKey };
    juce::Colour sendTrackTint { lightPalette.sendTrackTint };

    juce::Colour scaleTone     { lightPalette.scaleTone };
    juce::Colour scaleRootTone { lightPalette.scaleRootTone };

    juce::Colour chordDegreeColour (int degreeIndex)
    {
        // 添字が範囲外なら、色分けが効いていないことが見て分かるグレーにする
        if (! juce::isPositiveAndBelow (degreeIndex, 9))
            return juce::Colours::grey;

        return degreeColours[degreeIndex];
    }

    //==========================================================================
    void setTheme (Theme newTheme)
    {
        currentTheme = newTheme;
        applyPalette (newTheme == Theme::Dark ? darkPalette : lightPalette);

        // 8.175：**アクセント2色だけブランドで差し替える**（Phase 216／`Branding.h`）。
        //
        // パレットを丸ごと2セット持つ形にはしていません。**同じ表を2つ持つと、
        // 必ずどちらかが古くなります**（8.2）——地・文字・目盛り・度数の色は
        // どちらのアプリでも同じでよく、違うのは「選択中」と「いま起きていること」の
        // 2色だけでした。
        //
        // Manta Studioでは`Branding`が**いまと同じ値**を返すので、見た目は変わりません。
        const bool dark = (newTheme == Theme::Dark);

        purple = juce::Colour (Branding::accentPrimary (dark));
        orange = juce::Colour (Branding::accentSecondary (dark));
    }

    Theme getTheme()
    {
        return currentTheme;
    }

    void loadThemeFromSettings()
    {
        setTheme (AppSettings::getInt (themeKey, 0) == 1 ? Theme::Dark : Theme::Light);
    }

    void saveThemeToSettings (Theme newTheme)
    {
        AppSettings::setInt (themeKey, newTheme == Theme::Dark ? 1 : 0);
    }


    //==========================================================================
    /** メニューバーの地だけを差し替えたLookAndFeel（Phase 73）。

        **JUCEの既定（`LookAndFeel_V4::drawMenuBarBackground`）は、
        `TextButton`の色を40%の不透明度で敷いたうえに縦のグラデーションを掛けます。**
        画面の地とは違う色の帯になり、しかもメニューバーは必要な幅しか無いので、
        **左上だけ色の違う四角が出ている**ように見えていました（1.43と同じ「既定の配色は
        こちらの都合を知らない」という話）。

        フッター（`TransportBarComponent`）と同じく`panel`で平らに塗ります。 */
    class AppLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        using juce::LookAndFeel_V4::LookAndFeel_V4;

        void drawMenuBarBackground (juce::Graphics& g, int width, int height,
                                     bool, juce::MenuBarComponent&) override
        {
            g.fillAll (panel);

            // 下端の境目。**メニュー行の残り（曲名側）はMainComponentが引く**ので、
            // ここは自分の幅ぶんだけ
            g.setColour (border);
            g.drawLine (0.0f, (float) height - 0.5f, (float) width, (float) height - 0.5f);
        }

        //======================================================================
        // 8.131：角の丸み（Phase 167／実験枠の43）
        //
        // **自分で描いている場所は`corner()`を通せば済みますが、ここは別**です。
        // ボタンとコンボは**JUCEが描いています**（`LookAndFeel_V4`が丸みを直に
        // 書いている）。被せないと、**自作の枠だけ角ばって、ボタンは丸いまま**
        // という半端な見た目になります。
        //
        // どちらも**`LookAndFeel_V4`の実装をなぞって、丸みだけ`corner()`に
        // 差し替えた**ものです。色の決め方は変えていません。

        void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                    const juce::Colour& backgroundColour,
                                    bool shouldDrawButtonAsHighlighted,
                                    bool shouldDrawButtonAsDown) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced (0.5f, 0.5f);

            auto baseColour = backgroundColour.withMultipliedSaturation (
                                    button.hasKeyboardFocus (true) ? 1.3f : 0.9f)
                                 .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);

            if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
                baseColour = baseColour.contrasting (shouldDrawButtonAsDown ? 0.2f : 0.05f);

            const float cornerSize = corner (4.0f);

            g.setColour (baseColour);

            // 角ばらせているときは、丸めるものが無いので素直に矩形で済ませる。
            // **繋がっている辺の扱いも要らなくなる**（角が無いので落としようがない）
            if (cornerSize <= 0.0f)
            {
                g.fillRect (bounds);

                g.setColour (button.findColour (juce::ComboBox::outlineColourId));
                g.drawRect (bounds, 1.0f);
                return;
            }

            const bool flatOnLeft   = button.isConnectedOnLeft();
            const bool flatOnRight  = button.isConnectedOnRight();
            const bool flatOnTop    = button.isConnectedOnTop();
            const bool flatOnBottom = button.isConnectedOnBottom();

            if (flatOnLeft || flatOnRight || flatOnTop || flatOnBottom)
            {
                juce::Path path;
                path.addRoundedRectangle (bounds.getX(), bounds.getY(),
                                           bounds.getWidth(), bounds.getHeight(),
                                           cornerSize, cornerSize,
                                           ! (flatOnLeft  || flatOnTop),
                                           ! (flatOnRight || flatOnTop),
                                           ! (flatOnLeft  || flatOnBottom),
                                           ! (flatOnRight || flatOnBottom));
                g.fillPath (path);

                g.setColour (button.findColour (juce::ComboBox::outlineColourId));
                g.strokePath (path, juce::PathStrokeType (1.0f));
            }
            else
            {
                g.fillRoundedRectangle (bounds, cornerSize);

                g.setColour (button.findColour (juce::ComboBox::outlineColourId));
                g.drawRoundedRectangle (bounds, cornerSize, 1.0f);
            }
        }

        void drawComboBox (juce::Graphics& g, int width, int height, bool,
                            int, int, int, int, juce::ComboBox& box) override
        {
            const float cornerSize = corner (4.0f);
            const auto boxBounds = juce::Rectangle<int> (0, 0, width, height)
                                       .toFloat().reduced (0.5f, 0.5f);

            g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
            g.fillRoundedRectangle (boxBounds, cornerSize);

            g.setColour (box.findColour (juce::ComboBox::outlineColourId));
            g.drawRoundedRectangle (boxBounds, cornerSize, 1.0f);

            // 右端の三角。**角の話ではない**ので、JUCEの既定と同じ形のまま
            const juce::Rectangle<int> arrowZone (width - 30, 0, 20, height);

            juce::Path path;
            path.startNewSubPath ((float) arrowZone.getX() + 3.0f,
                                   (float) arrowZone.getCentreY() - 2.0f);
            path.lineTo ((float) arrowZone.getCentreX(), (float) arrowZone.getCentreY() + 3.0f);
            path.lineTo ((float) arrowZone.getRight() - 3.0f,
                          (float) arrowZone.getCentreY() - 2.0f);

            g.setColour (box.findColour (juce::ComboBox::arrowColourId)
                             .withAlpha (box.isEnabled() ? 0.9f : 0.2f));
            g.strokePath (path, juce::PathStrokeType (2.0f));
        }
    };

    juce::LookAndFeel_V4::ColourScheme createColourScheme()
    {
        // `ColourScheme`の並びは`UIColour`の宣言順と対（ここを入れ替えると、
        // 文字色に地の色が入るような壊れ方をする）。
        //
        // widgetBackgroundに`panel`ではなく`background`を割り当てているのは、
        // **既に色を指定してあるボタンやコンボボックスが`AppColours::background`を
        // 使っているから**（`ChordPadPanel`など）。ここを揃えておかないと、
        // 指定してあるものと無いものが並んだときに、面の色がわずかに食い違う。
        //
        // highlightedTextだけは白のまま：**重なる相手がpurpleだから**で、
        // ライトでもダークでもパープルの上は白が読みやすい。
        // ここをtextPrimaryにすると、ライトで「パープルの帯に黒文字」になる。
        juce::LookAndFeel_V4::ColourScheme scheme
        {
            background,           // windowBackground
            background,           // widgetBackground（ボタン・コンボ・入力欄の面）
            panel,                // menuBackground
            border,               // outline
            textPrimary,          // defaultText
            purple,               // defaultFill（キャレット、スクロールバーのつまみ）
            juce::Colours::white, // highlightedText
            purple,               // highlightedFill（選択行、ツールチップの地）
            textPrimary           // menuText
        };

        return scheme;
    }

    std::unique_ptr<juce::LookAndFeel_V4> createLookAndFeel()
    {
        return std::make_unique<AppLookAndFeel> (createColourScheme());
    }
}
