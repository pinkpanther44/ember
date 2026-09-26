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
    const juce::String exportFormatKey     { "exportFormat" };        // 8.153（0=WAV／1=MP3／2=FLAC。`Format`の番号）
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

    /** MP3で選べるレートはここまで（MPEG-1 Layer III にあるのは3つだけ。`Mp3Writer.h`）。

        8.319：**上の一覧の先頭3つと同じ並びにしておくこと**（Phase 312）。
        `getSelectedSampleRate()`は、どちらの一覧が入っていても**上の一覧で読みます**
        ——先頭が揃っていれば、何番目かが同じ値を指すからです。 */
    const double mp3SampleRateChoices[] = { 0.0, 44100.0, 48000.0 };

    //--------------------------------------------------------------------------
    // 8.319：形式の選択肢（Phase 312）
    //
    // **項目IDは`Format`の番号＋1**（0は「選ばれていない」の予約）。
    // 並べる順（WAV・FLAC・MP3）と番号の順（WAV・MP3・FLAC）が違うので、
    // **何番目か（index）で読まないこと**——MP3が無いOSでは番目がずれます

    int formatToItemId (ExportOptions::Format format)   { return (int) format + 1; }

    ExportOptions::Format itemIdToFormat (int itemId)
    {
        switch (itemId)
        {
            case 2:  return ExportOptions::Format::mp3;
            case 3:  return ExportOptions::Format::flac;
            default: return ExportOptions::Format::wav;
        }
    }

    /** ビット深度の一覧を入れ直す（**項目ID＝ビット数**）。

        **FLACには32bit floatがありません**（JUCEの`FlacAudioFormat`は16と24だけ）。
        選べないものは並べず、選んでいた値が消えたら24へ落とします。
        **MP3では使わないので灰色**にします（ビットレートで決まるため）。 */
    void fillBitDepthBox (juce::ComboBox& box, ExportOptions::Format format, int wantedBits)
    {
        box.clear (juce::dontSendNotification);

        for (const auto& choice : bitDepthChoices)
            if (! (format == ExportOptions::Format::flac && choice.bits == 32))
                box.addItem (choice.label, choice.bits);

        box.setSelectedId (box.indexOfItemId (wantedBits) >= 0 ? wantedBits : 24, juce::dontSendNotification);
        box.setEnabled (format != ExportOptions::Format::mp3);
    }

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

    /** 8.319：**どちらの一覧が入っていても、広いほうの一覧で読みます**（Phase 312）。
        MP3の一覧は広いほうの**先頭と同じ並び**なので、何番目かが同じ値を指します。

        Phase 311までは「どちらの一覧か」を呼ぶ側が渡していました。形式が3つになると
        **切り替える前がどれだったか**を覚えておく必要が出るので、要らない形にしました。 */
    double getSelectedSampleRate (const juce::ComboBox& box)
    {
        return sampleRateChoices[juce::jlimit (0, juce::numElementsInArray (sampleRateChoices) - 1,
                                               box.getSelectedItemIndex())];
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

std::unique_ptr<juce::AudioFormatWriter> ExportOptions::createWriter (const juce::File& file, double sampleRate,
                                                                      int numChannels, juce::String& error) const
{
    // 8.153：**MP3は自前の書き手**（Windowsの仕組みを借りる。`Mp3Writer.h`）
    if (format == Format::mp3)
    {
        juce::String mp3Error;
        auto writer = Mp3Writer::createWriter (file, sampleRate, numChannels, mp3BitrateKbps, mp3Error);

        if (writer == nullptr)
            error = mp3Error.isNotEmpty() ? mp3Error : utf8 ("MP3の書き出しを用意できませんでした。");

        return writer;
    }

    // WAVとFLACは**JUCEが持っている書き手**。ファイルの開き方は同じです
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

    if (stream == nullptr)
    {
        error = utf8 ("ファイルを作成できませんでした: ") + file.getFullPathName();
        return nullptr;
    }

    const auto writerOptions = juce::AudioFormatWriterOptions{}
                                   .withSampleRate (sampleRate)
                                   .withNumChannels (numChannels)
                                   .withBitsPerSample (getEffectiveBitsPerSample());

    if (format == Format::flac)
    {
        // 8.319：**圧縮の強さは5**（Phase 312）。FLACの参照実装の既定と同じで、
        // JUCEの選択肢でも`"5 (Default)"`です。**音は何番でも同じ**——
        // 違うのはファイルの大きさと、書くのにかかる時間だけです
        juce::FlacAudioFormat flacFormat;
        auto writer = flacFormat.createWriterFor (stream, writerOptions.withQualityOptionIndex (5));

        if (writer == nullptr)
            error = utf8 ("FLACファイルの書き出し形式を用意できませんでした。");

        return writer;
    }

    // **32bitはJUCEが浮動小数点で書きます**（`WavAudioFormat`が`bits == 32`を
    // そう扱う）。ダイアログの表記も「32 bit float」にしてあります。
    juce::WavAudioFormat wavFormat;
    auto writer = wavFormat.createWriterFor (stream, writerOptions);

    if (writer == nullptr)
        error = utf8 ("WAVファイルの書き出し形式を用意できませんでした。");

    return writer;
}

//==============================================================================
namespace
{
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

    // 8.153：形式（Phase 191／8.1のD9b）。
    //
    // 8.319：**いつも出します**（Phase 312）。Phase 311までは「MP3が使えないときは出さない」で、
    // Linuxでは形式を選ぶ欄そのものがありませんでした（WAVしか無かったため）。
    // **FLACはどのOSでも書ける**ので、選べるものが1つだけ、ということがなくなりました
    const bool canUseMp3 = Mp3Writer::isAvailable();

    const auto initialFormat = [canUseMp3]
    {
        switch (AppSettings::getInt (exportFormatKey, 0))
        {
            // **前回MP3でも、使えないOSならWAVへ**（Windowsで出した設定を持ち込んだとき）
            case 1:  return canUseMp3 ? ExportOptions::Format::mp3 : ExportOptions::Format::wav;
            case 2:  return ExportOptions::Format::flac;
            default: return ExportOptions::Format::wav;
        }
    }();

    window.addComboBox ("format", {}, utf8 ("形式"));

    if (auto* box = window.getComboBoxComponent ("format"))
    {
        // **劣化しないものを先に**並べます。番号の順（WAV・MP3・FLAC）とは違うので、
        // 読むときは項目IDで（`itemIdToFormat()`）
        box->addItem ("WAV", formatToItemId (ExportOptions::Format::wav));
        box->addItem ("FLAC", formatToItemId (ExportOptions::Format::flac));

        if (canUseMp3)
            box->addItem ("MP3", formatToItemId (ExportOptions::Format::mp3));

        box->setSelectedId (formatToItemId (initialFormat), juce::dontSendNotification);
    }

    // ビット深度。**WAVとFLACで使い、MP3では灰色**（`fillBitDepthBox()`）。
    // 前回の値に戻す（設計書2.5）。無い／消えた値なら既定の24bitへ
    window.addComboBox ("bitDepth", {}, utf8 ("ビット深度"));

    if (auto* box = window.getComboBoxComponent ("bitDepth"))
        fillBitDepthBox (*box, initialFormat, AppSettings::getInt (exportBitDepthKey, 24));

    // 8.153：MP3のビットレート（Phase 191）。**MP3のときだけ使うので、ほかでは灰色**
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
            box->setEnabled (initialFormat == ExportOptions::Format::mp3);
        }
    }

    // 8.153：サンプルレート（Phase 191／8.1のD9a）。
    // **Phase 190でクリップ再生がリサンプリングするようになったので出せます**（8.152）
    window.addComboBox ("sampleRate", { describeSampleRate (0.0) }, utf8 ("サンプルレート"));

    {
        const double savedRate = AppSettings::getDouble (exportSampleRateKey, 0.0);
        if (auto* box = window.getComboBoxComponent ("sampleRate"))
            fillSampleRateBox (*box, initialFormat == ExportOptions::Format::mp3, savedRate);
    }

    // **形式を変えたら、選べるものも入れ替える。**
    // MP3に88.2kや96kは無く、FLACに32bit floatは無いので、
    // 選べるまま書き出して失敗させない（`Mp3Writer.h`・`fillBitDepthBox()`）
    {
        auto* formatBox = window.getComboBoxComponent ("format");
        auto* rateBox = window.getComboBoxComponent ("sampleRate");
        auto* bitDepthBox = window.getComboBoxComponent ("bitDepth");
        auto* bitrateBox = window.getComboBoxComponent ("mp3Bitrate");   // MP3が無いOSでは無い

        if (formatBox != nullptr && rateBox != nullptr && bitDepthBox != nullptr)
            formatBox->onChange = [formatBox, rateBox, bitDepthBox, bitrateBox]
            {
                const auto format = itemIdToFormat (formatBox->getSelectedId());

                // **入れ替える前に、いま選んでいる値を控える**（無ければ既定へ落ちる）
                fillSampleRateBox (*rateBox, format == ExportOptions::Format::mp3,
                                   getSelectedSampleRate (*rateBox));
                fillBitDepthBox (*bitDepthBox, format, bitDepthBox->getSelectedId());

                if (bitrateBox != nullptr)
                    bitrateBox->setEnabled (format == ExportOptions::Format::mp3);
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

                // 8.153：形式・ビットレート・サンプルレート（Phase 191／D9a・D9b）。
                // 8.319：**項目IDで読むこと**（Phase 312）。並べる順と番号の順が違います
                options.format = ExportOptions::Format::wav;

                if (auto* box = owned->window->getComboBoxComponent ("format"))
                    options.format = itemIdToFormat (box->getSelectedId());

                if (options.format == ExportOptions::Format::mp3 && ! canUseMp3)
                    options.format = ExportOptions::Format::wav;   // 並べていないので来ないはず

                const bool isMp3 = (options.format == ExportOptions::Format::mp3);

                if (canUseMp3)
                    if (auto* box = owned->window->getComboBoxComponent ("mp3Bitrate"))
                    {
                        const auto& choices = Mp3Writer::getBitrateChoices();
                        options.mp3BitrateKbps = choices[juce::jlimit (0, choices.size() - 1,
                                                                        box->getSelectedItemIndex())];
                    }

                if (auto* box = owned->window->getComboBoxComponent ("sampleRate"))
                    options.sampleRate = getSelectedSampleRate (*box);

                // 8.319：**項目ID＝ビット数**（`fillBitDepthBox()`）。FLACでは32が並ばないので、
                // 番目で読むと24のつもりが32になります
                if (auto* box = owned->window->getComboBoxComponent ("bitDepth"))
                    if (box->getSelectedId() > 0)
                        options.bitsPerSample = box->getSelectedId();

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
                AppSettings::setInt (exportFormatKey, (int) options.format);   // 8.319：`Format`の番号のまま
                AppSettings::setInt (exportMp3BitrateKey, options.mp3BitrateKbps);
                AppSettings::setDouble (exportSampleRateKey, options.sampleRate);

                owned.reset();
                onAccepted (options);
            }),
        false);
}
