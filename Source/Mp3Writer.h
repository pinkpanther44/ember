#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

//==============================================================================
/**
    8.153：**MP3の書き出し**（Phase 191／8.1のD9b。仕様書5.10）。

    ### なぜLAMEを抱えなかったか

    8.80は「JUCE標準にエンコーダが無いので、LAME等の外部を抱える話＋
    配布時のライセンスの話が付いてくる」と書いて見送っていました。

    **Windowsが最初から持っています**（Media Foundation）。

    | | 抱えるもの | 配るときの手間 |
    |---|---|---|
    | LAME | ライブラリかexe1本 | 一緒に配る。ライセンスの確認が要る |
    | **Media Foundation** | **無し**（OSの一部） | **無し** |

    このDAWはWindows専用です（ASIO・VST3・サンドボックスの作りが全部そう）。
    **他のOSへ移すときに書き直す場所が1つ増えますが、いま増える荷物はゼロ**——
    その取り引きを選んでいます。

    > MP3の特許は2017年に切れています。**残っていた「ライセンスの話」は、
    > いまは配布物の話だけ**で、OSの機能を呼ぶぶんには何も付いてきません。

    ### 使いかた

    `juce::AudioFormatWriter`として返るので、**WAVと同じ道で書けます**
    （`AudioEngine::renderOfflineToFile()`は、どちらを受け取っても同じループ）。

    ### 書けるレートは3つだけ

    MPEG-1 Layer III は **32000／44100／48000 Hz** しかありません。
    88.2kや96kは**MP3という形式に無い**ので、`isSupportedSampleRate()`で
    先に断ります（書き始めてから失敗させない）。
*/
namespace Mp3Writer
{
    /** この環境でMP3を書けるか。**Media Foundationにエンコーダがあるかまで見ます。**

        初回だけ調べて、以降は覚えた答えを返します。 */
    bool isAvailable();

    /** MP3として成り立つサンプルレートか（32000／44100／48000）。 */
    bool isSupportedSampleRate (double sampleRate);

    /** 選べるビットレート（kbps）。**表示と値の対応はここ1箇所**。 */
    const juce::Array<int>& getBitrateChoices();

    /** 書き出し用のライターを作る。**作れなければnullptr**を返し、`errorOut`に理由を入れます。

        @param file          書き出し先（既にあれば消してから作ります）
        @param sampleRate    32000／44100／48000 のいずれか
        @param numChannels   1（モノ）か2（ステレオ）
        @param bitrateKbps   `getBitrateChoices()`のいずれか。近いものへ丸めます
    */
    std::unique_ptr<juce::AudioFormatWriter> createWriter (const juce::File& file,
                                                            double sampleRate,
                                                            int numChannels,
                                                            int bitrateKbps,
                                                            juce::String& errorOut);
}
