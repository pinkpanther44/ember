#include "SplashWindow.h"
#include "AppColours.h"
#include "Branding.h"   // 8.175：ブランドのアイコン（Phase 216）
#include "BinaryData.h"
#include "Utf8.h"

namespace
{
    constexpr int splashWidth  = 560;
    constexpr int splashHeight = 320;

    juce::Image loadEmbedded (const char* binaryDataName)
    {
        int size = 0;

        if (const auto* data = BinaryData::getNamedResource (binaryDataName, size))
            return juce::ImageFileFormat::loadFrom (data, (size_t) size);

        return {};
    }
}

SplashWindow::SplashWindow()
{
    // **名前で引いています**（`BinaryData::splash_png`と直に書いていない）。
    // CMakeは`Resources/Splash/splash.png`が**あるときだけ**埋め込むので、
    // 直に書くと、置いていない環境でビルドが通りません
    artwork = loadEmbedded ("splash_png");
    // 8.175：**ブランドのアイコンを引く**（Phase 216）。CMakeはそのブランドのぶんだけ
    // 埋め込むので、名前で引いているここを変えるだけで済みます
    appIcon = loadEmbedded (Branding::isEmber ? "ember_icon_png" : "app_icon_png");

    setOpaque (false);
    setSize (splashWidth, splashHeight);

    // 画面の中央。**メインウィンドウはまだ出ていない**ので、
    // 重ねる相手が無くても困りません
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        setCentrePosition (display->userBounds.getCentre().roundToInt());

    addToDesktop (juce::ComponentPeer::windowIsTemporary
                   | juce::ComponentPeer::windowHasDropShadow);

    setAlwaysOnTop (true);
    setVisible (true);
    toFront (false);   // **フォーカスは奪わない**：この後に出る本体へ渡したい

    shownAtMs = juce::Time::getMillisecondCounter();
}

SplashWindow::~SplashWindow()
{
    removeFromDesktop();
}

void SplashWindow::setStatus (const juce::String& newStatus)
{
    if (status == newStatus)
        return;

    status = newStatus;

    // **その場で描き直す。** この後に重い処理が続くと、
    // 次にメッセージループが回るのはそれが終わった後で、
    // 「準備しています」が終わってから出ることになります
    repaint();

    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();
}

void SplashWindow::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float corner = AppColours::corner (10.0f);

    juce::Path panel;
    panel.addRoundedRectangle (bounds, corner);

    if (artwork.isValid())
    {
        // 差し替えの絵。**丸めた形で切り抜いてから**引き伸ばす。
        // `ScopedSaveState`が要ります——`reduceClipRegion()`は戻らないので、
        // これが無いと**この下の縁が描かれません**
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (panel);
        g.drawImage (artwork, bounds, juce::RectanglePlacement::fillDestination);
    }
    else
    {
        g.setColour (AppColours::panel);
        g.fillPath (panel);

        auto content = getLocalBounds().reduced (30);
        auto statusArea = content.removeFromBottom (22);

        if (appIcon.isValid())
        {
            // 正方形のアイコンを左に置き、右に名前を出す。
            //
            // **枠も角丸も付けていません。** もらったアイコンは
            // **絵の地がグレーで塗ってあり**（PNGにαはあるが、外周だけ透明で、
            // 中は塗ってある）、枠を足すと、枠とグレーの四角が二重に見えます。
            // 見栄えをここで作り込むより、`Resources/Splash/splash.png`を
            // 置いてもらうほうが早い、と判断しています
            auto iconArea = content.removeFromLeft (content.getHeight()).toFloat();

            g.drawImage (appIcon, iconArea, juce::RectanglePlacement::centred);

            content.removeFromLeft (24);
        }

        // 文字のかたまりは**縦の中央**へ。上から積むと、アイコンの高さが
        // 変わったときに頭だけ揃って見えます
        auto textArea = content.withSizeKeepingCentre (content.getWidth(), 84);

        g.setColour (AppColours::textPrimary);
        g.setFont (juce::FontOptions (32.0f, juce::Font::bold));
        g.drawText (JUCE_APPLICATION_NAME_STRING,
                     textArea.removeFromTop (42), juce::Justification::centredLeft, false);

        g.setColour (AppColours::textSecondary);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (utf8 ("バージョン ") + JUCE_APPLICATION_VERSION_STRING,
                     textArea.removeFromTop (18), juce::Justification::centredLeft, false);

        textArea.removeFromTop (12);

        g.setColour (AppColours::purple);
        g.fillRect (textArea.removeFromTop (3).withWidth (64));

        if (status.isNotEmpty())
        {
            g.setColour (AppColours::textSecondary);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (status, statusArea, juce::Justification::centredLeft, true);
        }
    }

    // 縁。**絵を入れても付けます**：真っ白な絵を置いたときに、
    // どこまでがウィンドウか分からなくなるのを防ぐ
    g.setColour (AppColours::border);
    g.strokePath (panel, juce::PathStrokeType (1.0f));
}

void SplashWindow::closeAndThen (std::function<void()> next)
{
    onClosed = std::move (next);

    const auto elapsed = juce::Time::getMillisecondCounter() - shownAtMs;

    if (elapsed >= (juce::uint32) minimumDisplayMs)
    {
        finish();
        return;
    }

    startTimer ((int) ((juce::uint32) minimumDisplayMs - elapsed));
}

void SplashWindow::timerCallback()
{
    stopTimer();
    finish();
}

void SplashWindow::finish()
{
    setVisible (false);

    // **自分のコールバックの中で自分を破棄させない**（1.5）。
    // 呼ぶ側（`Main.cpp`）はここでこのウィンドウを捨てるので、
    // このスタックを抜けてから呼ぶ
    if (onClosed != nullptr)
        juce::MessageManager::callAsync (std::move (onClosed));

    onClosed = nullptr;
}
