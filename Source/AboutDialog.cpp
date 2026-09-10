#include "AboutDialog.h"
#include "AppColours.h"
#include "Branding.h"
#include "BinaryData.h"
#include "Utf8.h"

namespace
{
    constexpr int dialogWidth  = 520;
    constexpr int dialogHeight = 480;

    constexpr int iconSize = 72;
    constexpr int margin   = 24;

    juce::Image loadEmbedded (const char* binaryDataName)
    {
        int size = 0;

        if (const auto* data = BinaryData::getNamedResource (binaryDataName, size))
            return juce::ImageFileFormat::loadFrom (data, (size_t) size);

        return {};
    }

    /** 下の読み取り専用エディタに流し込む本文。

        **改行を含む長文なので、`utf8()`の中に日本語を直接書きます**（9.4）——
        `const char*`を持ち回すと、訳の表を作る道具から見えず、訳が落ちます。 */
    juce::String buildBody()
    {
        juce::String text;

        if (Branding::isPublished)
        {
            text << utf8 ("このソフトウェアはフリーソフトウェアです。フリーソフトウェアファウンデーションが公開する")
                 << juce::newLine
                 << juce::String (Branding::licenceName)
                 << utf8 ("の条件のもとで、再配布および改変ができます。")
                 << juce::newLine << juce::newLine
                 // **ここに`**`を書かないこと。** これはMarkdownではなく、
                 // ただのテキストエディタです。記号がそのまま画面に出ます
                 << utf8 ("有用であることを願って配布されていますが、いかなる保証もありません。")
                 << utf8 ("商品性や特定目的への適合性についての黙示的な保証も含みます。")
                 << utf8 ("詳しくはライセンス全文をご覧ください。")
                 << juce::newLine << juce::newLine;
        }

        text << utf8 ("使用しているもの")
             << juce::newLine
             << utf8 ("  ・JUCE — このアプリの土台になっているフレームワーク")
             << juce::newLine
             << utf8 ("  ・VST3 SDK — プラグインを読み込むために使用")
             << juce::newLine
             << utf8 ("    VSTは Steinberg Media Technologies GmbH の商標です")
             << juce::newLine
             // 8.189：**LV2のぶんを足すこと**（Phase 227）。JUCEに同梱されている
             // lilv／serd／sord／sratom（どれもISC、David Robillard）を通しています
             << utf8 ("  ・LV2 と lilv / serd / sord / sratom — LV2プラグインを読み込むために使用")
             << juce::newLine;

        if (! Branding::hasAsioSupport)
            text << juce::newLine
                 << utf8 ("Steinberg ASIO SDK は含まれておらず、ASIOのコードも入っていません。");

        return text;
    }
}

//==============================================================================

AboutDialog::AboutDialog()
{
    // 8.175と同じ引き方。CMakeはそのブランドのぶんだけ埋め込むので、名前で引きます
    appIcon = loadEmbedded (Branding::isEmber ? "ember_icon_png" : "app_icon_png");

    titleLabel.setText (Branding::productName, juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
    addAndMakeVisible (titleLabel);

    // **バージョンはCMakeから渡ってきます**（`JUCE_APPLICATION_VERSION_STRING`）。
    // ここに数字を書くと、`CMakeLists.txt`の`project(... VERSION ...)`と
    // **2箇所になります**（1.27）
    versionLabel.setText (juce::String (JUCE_APPLICATION_VERSION_STRING), juce::dontSendNotification);
    versionLabel.setFont (juce::Font (juce::FontOptions (16.0f)));
    versionLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (versionLabel);

    copyrightLabel.setText (juce::String ("Copyright (C) ") + Branding::copyrightYear
                              + " " + Branding::copyrightHolder,
                            juce::dontSendNotification);
    copyrightLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    copyrightLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (copyrightLabel);

    // **ラベルではなくエディタにしてあります。** 不具合の報告や引用のときに
    // **選んでコピーできる**ほうがよいためです（読み取り専用なので書き換えられません）
    licenceText.setMultiLine (true);
    licenceText.setReadOnly (true);
    licenceText.setScrollbarsShown (true);
    licenceText.setCaretVisible (false);
    licenceText.setPopupMenuEnabled (true);
    licenceText.setFont (juce::Font (juce::FontOptions (13.0f)));
    licenceText.setColour (juce::TextEditor::backgroundColourId, AppColours::panel);
    licenceText.setColour (juce::TextEditor::outlineColourId, AppColours::border);
    licenceText.setColour (juce::TextEditor::textColourId, AppColours::textPrimary);
    licenceText.setText (buildBody(), false);
    addAndMakeVisible (licenceText);

    // **AGPLが求める「ソースの入手先」**（8.182）。配っていないManta Studioでは出しません
    if (Branding::isPublished)
    {
        sourceButton.setButtonText (juce::String (Branding::sourceUrl));
        sourceButton.setURL (juce::URL (Branding::sourceUrl));
        sourceButton.setFont (juce::Font (juce::FontOptions (13.0f)), false,
                               juce::Justification::centredLeft);
        sourceButton.setColour (juce::HyperlinkButton::textColourId, AppColours::purple);
        addAndMakeVisible (sourceButton);
    }

    closeButton.setButtonText (utf8 ("閉じる"));
    closeButton.onClick = [this]
    {
        // **自分で自分を消さないこと**（9.4）。窓に頼みます
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            dialog->exitModalState (0);
    };
    addAndMakeVisible (closeButton);

    setSize (dialogWidth, dialogHeight);
}

void AboutDialog::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);

    if (appIcon.isValid())
    {
        const auto area = juce::Rectangle<int> (margin, margin, iconSize, iconSize);

        g.drawImage (appIcon, area.toFloat(),
                      juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    }
}

void AboutDialog::resized()
{
    auto area = getLocalBounds().reduced (margin);

    // 上：アイコンの右に、名前・バージョン・著作権を積む
    {
        auto header = area.removeFromTop (iconSize);

        header.removeFromLeft (iconSize + 16);

        titleLabel.setBounds (header.removeFromTop (28));
        versionLabel.setBounds (header.removeFromTop (22));
        copyrightLabel.setBounds (header);
    }

    area.removeFromTop (18);

    // 下：閉じるボタンとソースのリンクは**先に場所を取る**。
    // 残り全部を本文へ渡すので、順番を変えると本文がボタンの下に潜ります
    {
        auto footer = area.removeFromBottom (30);

        closeButton.setBounds (footer.removeFromRight (100));

        if (Branding::isPublished)
        {
            footer.removeFromRight (12);
            sourceButton.setBounds (footer);
        }

        area.removeFromBottom (12);
    }

    licenceText.setBounds (area);
}

void AboutDialog::launch()
{
    juce::DialogWindow::LaunchOptions options;

    options.content.setOwned (new AboutDialog());
    options.dialogTitle = utf8 ("バージョン情報");
    options.dialogBackgroundColour = AppColours::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;

    // **大きさは1つだけ**（8.172の「画面は固定」と同じ理由。広げても隙間が増えるだけ）
    options.resizable = false;

    // 閉じた時点で自分を破棄します（`PreferencesDialog::launch()`と同じ方式）
    options.launchAsync();
}
