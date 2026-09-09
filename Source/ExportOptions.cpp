#include "ExportOptions.h"
#include "AppSettings.h"
#include "Mp3Writer.h"   // 8.153：MP3が使えるか・ビットレートの選択肢（Phase 191）
#include "Utf8.h"

namespace
{
    // 設計書2.5：前回の設定を覚えておくキー（8.80／Phase 120）。
    //
    // **プロジェクトではなくアプリの設定へ。** 「どう書き出すか」は作業のやり方で、
    // 曲そのものの内容ではないためです（コードパッドの発音パラメータと同じ扱い。8.1のA3）。
    const juce::String exportBitDepthKey  { "exportBitDepth" };
    const juce::String exportFormatKey     { "exportFormat" };        // 8.153（0=WAV／1=MP3）
    const juce::String exportSampleRateKey { "exportSampleRate" };    // 8.153（0=そのまま）
    const juce::String exportMp3BitrateKey { "exportMp3Bitrate" };    // 8.153

    /** ビット深度の選択肢。**表示と値の対応はここ1箇所**（読む側で数え直さない）。 */
    struct BitDepthChoice { const char* label; int bits; };

    const BitDepthChoice bitDepthChoices[] =
    {
        { "16 bit", 16 },
        { "24 bit", 24 },
        { "32 bit float", 32 },
    };

    /** 8.153：サンプルレートの選択肢（Phase 191／8.1のD9a）。

        **先頭は必ず「そのまま」（0）**にしておくこと。読む側が
        `index == 0`ではなく**値が0か**で判断できるようにしてあります。 */
    const double sampleRateChoices[] = { 0.0, 44100.0, 48000.0, 88200.0, 96000.0 };

    /** MP3で選べるレートはここまで（MPEG-1 Layer III にあるのは3つだけ。`Mp3Writer.h`）。 */
    const double mp3SampleRateChoices[] = { 0.0, 44100.0, 48000.0 };

    juce::String describeSampleRate (double rate)
    {
        if (rate <= 0.0)
            return utf8 ("そのまま（いまの再生と同じ）");

        return juce::String (juce::roundToInt (rate)) + " Hz";
    }

    /** 形式に応じたレートの一覧を、コンボボックスへ入れ直す。

        **選んでいた値は、あれば選び直します**——形式を切り替えただけで
        レートの選択が黙って変わるのは事故のもとです。 */
    void fillSampleRateBox (juce::ComboBox& box, bool isMp3, double wantedRate)
    {
        const double* rates = isMp3 ? mp3SampleRateChoices : sampleRateChoices;
        const int numRates = isMp3 ? juce::numElementsInArray (mp3SampleRateChoices)
                                    : juce::numElementsInArray (sampleRateChoices);

        box.clear (juce::dontSendNotification);

        int wantedIndex = 0;

        for (int i = 0; i < numRates; ++i)
        {
            // **項目IDは1から。** 0は「選ばれていない」の意味で予約されています
            box.addItem (describeSampleRate (rates[i]), i + 1);

            if (std::abs (rates[i] - wantedRate) < 0.5)
                wantedIndex = i;
        }

        box.setSelectedItemIndex (wantedIndex, juce::dontSendNotification);
    }

    double getSelectedSampleRate (const juce::ComboBox& box, bool isMp3)
    {
        const double* rates = isMp3 ? mp3SampleRateChoices : sampleRateChoices;
        const int numRates = isMp3 ? juce::numElementsInArray (mp3SampleRateChoices)
                                    : juce::numElementsInArray (sampleRateChoices);

        return rates[juce::jlimit (0, numRates - 1, box.getSelectedItemIndex())];
    }

    juce::String formatSeconds (double seconds)
    {
        const int totalSeconds = (int) seconds;

        return juce::String (totalSeconds / 60) + ":"
                + juce::String (totalSeconds % 60).paddedLeft ('0', 2);
    }
}

//==============================================================================
bool ExportOptions::shouldExportStem (const juce::String& trackId) const
{
    for (const auto& entry : stemTracks)
        if (entry.trackId == trackId)
            return entry.include;

    // **一覧に無ければ書き出す。** トラックが後から増えたときに、
    // 黙って落ちるより出るほうが安全です
    return true;
}

bool ExportOptions::isStemMono (const juce::String& trackId) const
{
    for (const auto& entry : stemTracks)
        if (entry.trackId == trackId)
            return entry.mono;

    return false;
}

//==============================================================================
namespace
{
    /**
        8.81：ステムのトラック一覧（Phase 121／8.1のD10a）。

        1行につき **［書き出す］トラック名［モノ］** の3つ。
        フォルダの中のトラックは**字下げ**して、どこに入っているかが分かるようにします。

        **`juce::AlertWindow::addCustomComponent()`はコンポーネントを所有しません。**
        ダイアログが消えるまで生かしておくのは呼び出し側の仕事です（下の`DialogHolder`）。
    */
    class StemTrackList : public juce::Component
    {
    public:
        explicit StemTrackList (const std::vector<ExportStemTrack>& tracksToShow)
            : tracks (tracksToShow)
        {
            for (size_t i = 0; i < tracks.size(); ++i)
            {
                auto* row = rows.add (new Row());

                row->include.setToggleState (tracks[i].include, juce::dontSendNotification);
                row->mono.setToggleState (tracks[i].mono, juce::dontSendNotification);

                row->name.setText (juce::String::repeatedString ("    ", tracks[i].depth)
                                     + (tracks[i].isFolder ? utf8 ("［フォルダ］") : juce::String())
                                     + tracks[i].name,
                                    juce::dontSendNotification);
                row->name.setFont (juce::FontOptions (12.0f));

                // **ラベルにマウスを食べさせない。** 名前を押しても何も起きないのは
                // 構いませんが、押した感じだけ出るのは紛らわしい（8.74と同じ話）
                row->name.setInterceptsMouseClicks (false, false);

                content.addAndMakeVisible (row->include);
                content.addAndMakeVisible (row->name);
                content.addAndMakeVisible (row->mono);
            }

            selectAllButton.onClick = [this] { setAllIncluded (true); };
            noneButton.onClick      = [this] { setAllIncluded (false); };

            addAndMakeVisible (selectAllButton);
            addAndMakeVisible (noneButton);

            headerLabel.setText (utf8 ("左＝書き出す　／　右＝モノラル"), juce::dontSendNotification);
            headerLabel.setFont (juce::FontOptions (11.0f));
            addAndMakeVisible (headerLabel);

            viewport.setViewedComponent (&content, false);
            viewport.setScrollBarsShown (true, false);
            addAndMakeVisible (viewport);

            setSize (420, 220);
        }

        /** 画面の状態を`ExportStemTrack`へ書き戻す。 */
        std::vector<ExportStemTrack> getResult() const
        {
            auto result = tracks;

            for (size_t i = 0; i < result.size() && i < (size_t) rows.size(); ++i)
            {
                result[i].include = rows[(int) i]->include.getToggleState();
                result[i].mono    = rows[(int) i]->mono.getToggleState();
            }

            return result;
        }

        void resized() override
        {
            auto area = getLocalBounds();

            auto buttonRow = area.removeFromTop (22);
            selectAllButton.setBounds (buttonRow.removeFromLeft (72));
            buttonRow.removeFromLeft (4);
            noneButton.setBounds (buttonRow.removeFromLeft (84));
            buttonRow.removeFromLeft (8);
            headerLabel.setBounds (buttonRow);

            area.removeFromTop (4);
            viewport.setBounds (area);

            content.setSize (juce::jmax (200, viewport.getMaximumVisibleWidth()),
                              juce::jmax (1, rows.size() * rowHeight));

            for (int i = 0; i < rows.size(); ++i)
            {
                auto row = juce::Rectangle<int> (0, i * rowHeight, content.getWidth(), rowHeight);

                rows[i]->include.setBounds (row.removeFromLeft (28));
                rows[i]->mono.setBounds (row.removeFromRight (28));
                rows[i]->name.setBounds (row);
            }
        }

    private:
        struct Row
        {
            juce::ToggleButton include;
            juce::Label name;
            juce::ToggleButton mono;
        };

        void setAllIncluded (bool shouldInclude)
        {
            for (auto* row : rows)
                row->include.setToggleState (shouldInclude, juce::dontSendNotification);
        }

        static constexpr int rowHeight = 22;

        std::vector<ExportStemTrack> tracks;
        juce::OwnedArray<Row> rows;

        juce::Component content;
        juce::Viewport viewport;
        juce::Label headerLabel;
        juce::TextButton selectAllButton { utf8 ("全部") };
        juce::TextButton noneButton { utf8 ("全部外す") };
    };

    /** ダイアログと、その中に置いた部品をまとめて持つ入れ物。

        **`AlertWindow`は中身を所有しません。** 別々に持つと、片方だけ先に消えて
        「消えたコンポーネントを親が握ったまま」になります（1.5）。 */
    struct DialogHolder
    {
        std::unique_ptr<juce::AlertWindow> window;
        std::unique_ptr<StemTrackList> stemList;
    };
}

//==============================================================================
void ExportOptionsDialog::show (bool isStems, double loopRangeSeconds,
                                 const ExportOptions& previousOptions,
                                 std::function<void (const ExportOptions&)> onAccepted)
{
    if (onAccepted == nullptr)
        return;

    auto holder = std::make_unique<DialogHolder>();

    holder->window = std::make_unique<juce::AlertWindow> (
                         isStems ? utf8 ("ステムの書き出し") : utf8 ("ミックスダウンの書き出し"),
                         utf8 ("書き出しの設定を選んでください。"),
                         juce::MessageBoxIconType::NoIcon);

    auto& window = *holder->window;

    // 8.153：形式（Phase 191／8.1のD9b）。**MP3が使えないときは出しません**
    // （Windows以外や、Media Foundationにエンコーダが無い環境）
    const bool canUseMp3 = Mp3Writer::isAvailable();

    if (canUseMp3)
    {
        window.addComboBox ("format", { "WAV", "MP3" }, utf8 ("形式"));

        if (auto* box = window.getComboBoxComponent ("format"))
            box->setSelectedItemIndex (AppSettings::getInt (exportFormatKey, 0) == 1 ? 1 : 0,
                                        juce::dontSendNotification);
    }

    juce::StringArray bitDepthLabels;

    for (const auto& choice : bitDepthChoices)
        bitDepthLabels.add (choice.label);

    window.addComboBox ("bitDepth", bitDepthLabels,
                         canUseMp3 ? utf8 ("ビット深度（WAV）") : utf8 ("ビット深度"));

    // 前回の値に戻す（設計書2.5）。**無い／消えた値なら既定の24bitへ**
    {
        const int savedBits = AppSettings::getInt (exportBitDepthKey, 24);
        int savedIndex = 1;   // 24 bit

        for (int i = 0; i < juce::numElementsInArray (bitDepthChoices); ++i)
            if (bitDepthChoices[i].bits == savedBits)
                savedIndex = i;

        if (auto* box = window.getComboBoxComponent ("bitDepth"))
            box->setSelectedItemIndex (savedIndex, juce::dontSendNotification);
    }

    // 8.153：MP3のビットレート（Phase 191）
    if (canUseMp3)
    {
        juce::StringArray bitrateLabels;

        for (auto kbps : Mp3Writer::getBitrateChoices())
            bitrateLabels.add (juce::String (kbps) + " kbps");

        window.addComboBox ("mp3Bitrate", bitrateLabels, utf8 ("ビットレート（MP3）"));

        if (auto* box = window.getComboBoxComponent ("mp3Bitrate"))
        {
            const int saved = AppSettings::getInt (exportMp3BitrateKey, 320);
            box->setSelectedItemIndex (juce::jmax (0, Mp3Writer::getBitrateChoices().indexOf (saved)),
                                        juce::dontSendNotification);
        }
    }

    // 8.153：サンプルレート（Phase 191／8.1のD9a）。
    // **Phase 190でクリップ再生がリサンプリングするようになったので出せます**（8.152）
    window.addComboBox ("sampleRate", { describeSampleRate (0.0) }, utf8 ("サンプルレート"));

    {
        const double savedRate = AppSettings::getDouble (exportSampleRateKey, 0.0);
        const bool startsAsMp3 = canUseMp3 && AppSettings::getInt (exportFormatKey, 0) == 1;

        if (auto* box = window.getComboBoxComponent ("sampleRate"))
            fillSampleRateBox (*box, startsAsMp3, savedRate);
    }

    // **形式を変えたら、選べるレートも入れ替える。**
    // MP3に88.2kや96kは無いので、選べるまま書き出して失敗させない（`Mp3Writer.h`）
    if (canUseMp3)
    {
        auto* formatBox = window.getComboBoxComponent ("format");
        auto* rateBox = window.getComboBoxComponent ("sampleRate");

        if (formatBox != nullptr && rateBox != nullptr)
            formatBox->onChange = [formatBox, rateBox]
            {
                const bool isMp3 = (formatBox->getSelectedItemIndex() == 1);

                // 入れ替える前に、いま選んでいる値を控える（無ければ「そのまま」へ落ちる）
                const double current = getSelectedSampleRate (*rateBox,
                                                               ! isMp3);   // 入れ替え前の一覧で読む
                fillSampleRateBox (*rateBox, isMp3, current);
            };
    }

    // 8.1のD11：ループ範囲だけ。**引かれていなければ項目自体を出しません**
    // （選べない項目を灰色で並べるより、無いほうが読みやすい）
    const bool canUseLoopRange = (loopRangeSeconds > 0.0);

    if (canUseLoopRange)
        window.addComboBox ("range",
                             { utf8 ("曲全体"),
                               utf8 ("ループ範囲だけ（") + formatSeconds (loopRangeSeconds) + utf8 ("）") },
                             utf8 ("書き出す範囲"));

    // 8.81：ステムは**トラックごとに**書き出す／モノを選ぶ（Phase 121／D10a）
    if (isStems && ! previousOptions.stemTracks.empty())
    {
        holder->stemList = std::make_unique<StemTrackList> (previousOptions.stemTracks);
        window.addCustomComponent (holder->stemList.get());
    }

    window.addButton (utf8 ("書き出す"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window.addButton (utf8 ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window.enterModalState (true,
        juce::ModalCallbackFunction::create (
            [owned = std::move (holder), isStems, canUseLoopRange, canUseMp3,
              previousOptions, onAccepted] (int result) mutable
            {
                // **中身を読んでから閉じること。** 先に消すとコンボボックスも消えます
                if (result != 1)
                {
                    owned.reset();
                    return;
                }

                ExportOptions options = previousOptions;

                // 8.153：形式・ビットレート・サンプルレート（Phase 191／D9a・D9b）
                options.format = ExportOptions::Format::wav;

                if (canUseMp3)
                    if (auto* box = owned->window->getComboBoxComponent ("format"))
                        if (box->getSelectedItemIndex() == 1)
                            options.format = ExportOptions::Format::mp3;

                const bool isMp3 = (options.format == ExportOptions::Format::mp3);

                if (canUseMp3)
                    if (auto* box = owned->window->getComboBoxComponent ("mp3Bitrate"))
                    {
                        const auto& choices = Mp3Writer::getBitrateChoices();
                        options.mp3BitrateKbps = choices[juce::jlimit (0, choices.size() - 1,
                                                                        box->getSelectedItemIndex())];
                    }

                if (auto* box = owned->window->getComboBoxComponent ("sampleRate"))
                    options.sampleRate = getSelectedSampleRate (*box, isMp3);

                if (auto* box = owned->window->getComboBoxComponent ("bitDepth"))
                {
                    const int index = juce::jlimit (0, juce::numElementsInArray (bitDepthChoices) - 1,
                                                     box->getSelectedItemIndex());
                    options.bitsPerSample = bitDepthChoices[index].bits;
                }

                // **毎回入れ直す。** 前回の設定を土台にしているので、
                // 項目を出さなかったときに前回の「ループ範囲だけ」が残ってしまう
                options.loopRangeOnly = false;

                if (canUseLoopRange)
                    if (auto* box = owned->window->getComboBoxComponent ("range"))
                        options.loopRangeOnly = (box->getSelectedItemIndex() == 1);

                if (isStems && owned->stemList != nullptr)
                    options.stemTracks = owned->stemList->getResult();

                // 次に開いたときも同じ設定から始める（設計書2.5）。
                // **範囲は覚えません**——「今回はここだけ」という一度きりの指定で、
                // 次の書き出しで黙ってループ範囲になっていると事故になります。
                //
                // 8.81：**トラックごとの選択はアプリの設定へ保存しません**（Phase 121）。
                // トラックIDはプロジェクトごとに違うので、置いても別の曲で意味を持ちません。
                // その代わり`MainComponent`が**このセッションのあいだ**覚えています
                AppSettings::setInt (exportBitDepthKey, options.bitsPerSample);

                // 8.153：形式とレートも覚える（Phase 191）。
                // **レートは覚えます**——「範囲」と違って、44.1kで出すと決めた人は
                // 次も44.1kで出します（一度きりの指定ではない）
                AppSettings::setInt (exportFormatKey, isMp3 ? 1 : 0);
                AppSettings::setInt (exportMp3BitrateKey, options.mp3BitrateKbps);
                AppSettings::setDouble (exportSampleRateKey, options.sampleRate);

                owned.reset();
                onAccepted (options);
            }),
        false);
}
