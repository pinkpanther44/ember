#include "AudioTransform.h"
#include "Branding.h"   // 8.175：保存先の名前（Phase 216）

#include "Utf8.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace AudioTransform
{

namespace
{
    /** 窓の大きさ。**42ms前後**（48kHz）です。

        大きくすると周波数の分解能が上がって和音が濁りにくくなり、
        小さくすると立ち上がりの滲みが減ります。**2048は音楽素材での定番の折衷**です。 */
    constexpr int fftOrder = 11;
    constexpr int fftSize = 1 << fftOrder;

    /** 合成側のホップ。**こちらを固定します**（`AudioTransform.h`の説明）。 */
    constexpr int synthesisHop = fftSize / 4;

    /** これより長いソースは断ります。**まるごとメモリに載せる**作りなので、
        際限なく受けると落ちます。 */
    constexpr double maxSourceSeconds = 600.0;

    /** 窓の重なりの合計が、これを下回るところは割らない。
        **端で0除算にならないため**の下駄です（前後を`fftSize`ぶん詰めてあるので、
        実際に使う区間はいつも十分な重なりがあります）。 */
    constexpr float minWindowSum = 1.0e-4f;

    float wrapToPi (float phase)
    {
        // fmodではなく引き算で回すのは、**回る回数がいつも1〜2回**だからです
        // （位相の進みは1ホップぶんしか溜まらない）
        while (phase > juce::MathConstants<float>::pi)  phase -= juce::MathConstants<float>::twoPi;
        while (phase < -juce::MathConstants<float>::pi) phase += juce::MathConstants<float>::twoPi;
        return phase;
    }

    std::vector<float> makeHannWindow()
    {
        std::vector<float> window ((size_t) fftSize);

        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                           * (float) i / (float) fftSize);

        return window;
    }

    /** 1チャンネルぶんを、**表のとおりに**伸ばす（**音程はそのまま**）。

        8.150：**分析側の刻み幅は場所によって変わります**（Phase 188／8.48）。
        合成側は`synthesisHop`のまま等間隔で、**出力の位置`f × Hs`に対して、
        表が指すソースの位置を読みに行きます**。
        表が点2つ（＝一定の倍率）のときは刻み幅も一定になるので、
        Phase 187までとまったく同じ動きです。

        戻り値は、**元の位置0が出力の位置0に来るように詰めた**波形です
        （前後に`fftSize`ぶんの余白を足して分析し、そのぶんを捨てています）。
        途中で止められたら空を返します。 */
    std::vector<float> timeStretchChannel (const float* input, int numInput,
                                            const WarpMap& map, double sampleRate,
                                            juce::dsp::FFT& fft,
                                            const std::vector<float>& window,
                                            const std::function<bool()>& shouldAbort)
    {
        // **前後に余白を足す。** 足さないと、先頭の`fftSize`ぶんだけ窓の重なりが
        // 足りず、**曲の頭が薄くなります**（そこが立ち上がりだと目立つ）
        const int padded = numInput + 2 * fftSize;

        std::vector<float> paddedInput ((size_t) padded, 0.0f);
        std::copy (input, input + numInput, paddedInput.begin() + fftSize);

        // 8.150：**出力の長さは表が決めます**（Phase 188）。前後の余白ぶんも、
        // いちばん端の区間の速さで延長された位置に来ます（`WarpMap.h`）
        const double padSeconds = (double) fftSize / sampleRate;

        const double warpedSpanSeconds = map.sourceToWarped ((double) numInput / sampleRate + padSeconds)
                                           - map.sourceToWarped (-padSeconds);

        const int numFrames = juce::jmax (1, (int) std::llround (warpedSpanSeconds * sampleRate)
                                                 / synthesisHop + 1);
        const int outLength = (numFrames - 1) * synthesisHop + fftSize;

        std::vector<float> output ((size_t) outLength, 0.0f);
        std::vector<float> windowSum ((size_t) outLength, 0.0f);

        const int numBins = fftSize / 2;
        std::vector<float> previousPhase ((size_t) numBins + 1, 0.0f);
        std::vector<float> runningPhase ((size_t) numBins + 1, 0.0f);

        // フレームごとの作業場。**外に出して使い回す**（毎フレーム確保しない）
        std::vector<float> magnitude ((size_t) numBins + 1, 0.0f);
        std::vector<float> currentPhase ((size_t) numBins + 1, 0.0f);
        std::vector<float> newPhase ((size_t) numBins + 1, 0.0f);
        std::vector<int> peaks;
        peaks.reserve ((size_t) numBins);

        // juce::dsp::FFTの実数入力は**複素で書き戻される**ので、2倍の場所が要る
        std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);

        // 詰めたぶん、表の原点をずらしておく（詰めた入力の0はソースの`-padSeconds`）
        const double warpedAtPadStart = map.sourceToWarped (-padSeconds);

        int previousInStart = 0;

        for (int frame = 0; frame < numFrames; ++frame)
        {
            if (shouldAbort != nullptr && (frame % 64) == 0 && shouldAbort())
                return {};

            // 8.150：**出力の位置から、読むべきソースの位置を引く**（Phase 188）。
            // 「刻み幅を足していく」形にすると、区間をまたぐたびに誤差が積もります
            const double warpedSeconds = warpedAtPadStart + (double) (frame * synthesisHop) / sampleRate;
            const double sourceSeconds = map.warpedToSource (warpedSeconds) + padSeconds;

            const int inStart = juce::jlimit (0, juce::jmax (0, padded - 1),
                                               (int) std::llround (sourceSeconds * sampleRate));

            // **位相の進みは「実際に進んだぶん」で測ること。**
            // 一定の刻み幅で書くと、速さが変わったところで音程がずれます
            const int analysisHop = (frame == 0) ? synthesisHop
                                                 : juce::jmax (1, inStart - previousInStart);
            previousInStart = inStart;

            const double phaseScale = (double) synthesisHop / (double) analysisHop;

            std::fill (fftData.begin(), fftData.end(), 0.0f);

            for (int i = 0; i < fftSize; ++i)
            {
                const int index = inStart + i;

                if (index < padded)
                    fftData[(size_t) i] = paddedInput[(size_t) index] * window[(size_t) i];
            }

            fft.performRealOnlyForwardTransform (fftData.data(), false);

            for (int k = 0; k <= numBins; ++k)
            {
                const float re = fftData[(size_t) (2 * k)];
                const float im = fftData[(size_t) (2 * k + 1)];

                magnitude[(size_t) k] = std::sqrt (re * re + im * im);
                currentPhase[(size_t) k] = std::atan2 (im, re);
            }

            // **山を探す。** 1つの音は隣のビンにも裾を広げていて、
            // その**裾と山の位相の関係**が波の形を決めています。
            // ビンごとに勝手に位相を進めると関係が崩れ、
            // **重ね合わせで打ち消し合って音が小さくなります**
            // （実測：+12半音で-12dB。これがその正体でした）。
            //
            // 山だけを進めて、裾は**山との差を保ったまま**付いていかせます
            // （Laroche & Dolsonの位相ロック）
            peaks.clear();

            for (int k = 1; k < numBins; ++k)
                if (magnitude[(size_t) k] > magnitude[(size_t) (k - 1)]
                     && magnitude[(size_t) k] >= magnitude[(size_t) (k + 1)])
                    peaks.push_back (k);

            if (peaks.empty())
                peaks.push_back (0);   // 無音のフレーム。全部を1つの「山」として扱う

            // 山の位相を進める（ここが伸縮の本体）
            for (const int k : peaks)
            {
                const float expected = juce::MathConstants<float>::twoPi
                                        * (float) k * (float) analysisHop / (float) fftSize;

                // 実際の進みとの差（＝そのビンの中での周波数のずれ）
                const float deviation = wrapToPi (currentPhase[(size_t) k]
                                                   - previousPhase[(size_t) k] - expected);

                // **合成側は違うホップで積む**
                newPhase[(size_t) k] = wrapToPi (runningPhase[(size_t) k]
                                                  + (float) ((double) (expected + deviation) * phaseScale));
            }

            // 裾は、いちばん近い山との差を保つ
            {
                size_t peakIndex = 0;

                for (int k = 0; k <= numBins; ++k)
                {
                    while (peakIndex + 1 < peaks.size()
                            && std::abs (peaks[peakIndex + 1] - k) < std::abs (peaks[peakIndex] - k))
                        ++peakIndex;

                    const int peak = peaks[peakIndex];

                    if (k != peak)
                        newPhase[(size_t) k] = wrapToPi (newPhase[(size_t) peak]
                                                          + currentPhase[(size_t) k]
                                                          - currentPhase[(size_t) peak]);
                }
            }

            for (int k = 0; k <= numBins; ++k)
            {
                previousPhase[(size_t) k] = currentPhase[(size_t) k];
                runningPhase[(size_t) k] = newPhase[(size_t) k];

                const float newRe = magnitude[(size_t) k] * std::cos (newPhase[(size_t) k]);
                const float newIm = magnitude[(size_t) k] * std::sin (newPhase[(size_t) k]);

                fftData[(size_t) (2 * k)] = newRe;
                fftData[(size_t) (2 * k + 1)] = newIm;

                // **上半分へ共役を書き戻すこと。** 実数の波形に戻すために要ります
                // （書かないと振幅が半分になり、虚部が残ります）
                if (k > 0 && k < numBins)
                {
                    const int mirror = fftSize - k;
                    fftData[(size_t) (2 * mirror)] = newRe;
                    fftData[(size_t) (2 * mirror + 1)] = -newIm;
                }
            }

            fft.performRealOnlyInverseTransform (fftData.data());

            const int outStart = frame * synthesisHop;

            for (int i = 0; i < fftSize; ++i)
            {
                const int index = outStart + i;

                if (index >= outLength)
                    break;

                const float w = window[(size_t) i];

                output[(size_t) index] += fftData[(size_t) i] * w;
                windowSum[(size_t) index] += w * w;
            }
        }

        // **窓の重なりで割る。** 定数で割らずに実際の合計で割るのは、
        // 端まで振幅が正しくなるためです
        for (int i = 0; i < outLength; ++i)
            output[(size_t) i] /= juce::jmax (minWindowSum, windowSum[(size_t) i]);

        // 詰めたぶんを捨てる。**ソースの位置0が来る場所は、表に訊く**（8.150）。
        // 一定の倍率のときは`fftSize × 倍率`で、Phase 187と同じ値になります
        const double zeroWarped = map.sourceToWarped (0.0) - warpedAtPadStart;
        const double endWarped = map.sourceToWarped ((double) numInput / sampleRate) - warpedAtPadStart;

        const int skip = (int) std::llround (zeroWarped * sampleRate);
        const int wanted = juce::jmax (1, (int) std::llround ((endWarped - zeroWarped) * sampleRate));

        std::vector<float> trimmed ((size_t) (wanted + fftSize), 0.0f);   // 後ろは補間の余白

        for (int i = 0; i < wanted; ++i)
        {
            const int index = skip + i;

            if (index >= outLength)
                break;

            trimmed[(size_t) i] = output[(size_t) index];
        }

        return trimmed;
    }
}

double getPitchRatio (int semitones)
{
    return std::pow (2.0, (double) semitones / 12.0);
}

juce::File getCacheFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile (Branding::dataFolderName)
             .getChildFile ("TransposeCache");
}

juce::File getRenderedFileFor (const juce::File& source, int semitones, double stretch)
{
    return getRenderedFileFor (source, semitones,
                                WarpMap::makeLinear (juce::jlimit (minStretch, maxStretch, stretch), 1.0));
}

juce::File getRenderedFileFor (const juce::File& source, int semitones, const WarpMap& map)
{
    // **倍率は文字にしてから混ぜること。** doubleをそのまま`<<`すると
    // 桁数が処理系任せになり、同じ値でも違う名前になり得ます
    const juce::String ratioText (map.getOverallRatio(), 6);

    // **ソースの中身が変わったら別のファイルにする。** パスだけを混ぜると、
    // 同じ名前で差し替えられたときに古いキャッシュを本物として掴みます
    juce::String key;
    key << source.getFullPathName().toLowerCase()
        << "|" << source.getSize()
        << "|" << source.getLastModificationTime().toMilliseconds()
        << "|" << semitones;

    // 8.150：**折れ線そのものを混ぜる**（Phase 188）。マーカーを1つ動かしたら
    // 別のファイルになってほしいので、全部の点を入れます
    for (const auto& point : map.points)
        key << "|" << juce::String (point.sourceSeconds, 6)
            << ">" << juce::String (point.warpedSeconds, 6);

    const juce::String hash (juce::String::toHexString (key.hashCode64()));

    juce::String name (source.getFileNameWithoutExtension());
    name << "_" << (semitones >= 0 ? "+" : "-") << std::abs (semitones)
         << "_x" << ratioText
         << (map.isLinear() ? "" : "w")   // 折れているものは名前で見分けられるように
         << "_" << hash << ".wav";

    return getCacheFolder().getChildFile (juce::File::createLegalFileName (name));
}

bool isIdentity (int semitones, double stretch)
{
    // **比べ方をここ1箇所に**（1.27）。倍率は浮動小数なので、
    // `== 1.0`を各所で書くと、丸めの差で「作り直しが要る」と判断が割れます
    return semitones == 0 && std::abs (stretch - 1.0) < 1.0e-9;
}

bool isIdentity (int semitones, const WarpMap& map)
{
    // 8.150：**折れているなら、倍率が1でも作り直しが要ります**（Phase 188）。
    // 「全体としては等倍だが、中で速くなったり遅くなったりする」形があるためです
    return isIdentity (semitones, map.getOverallRatio()) && map.isLinear();
}

juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input,
                                   int semitones, double stretch,
                                   std::function<bool()> shouldAbort)
{
    // 一定の倍率は、**点2つの表**として扱う（分岐を増やさない。1.27）。
    //
    // **サンプルレートは何でも構いません。** まっすぐな表は縮尺を持たないので、
    // 秒とサンプルの読み替えがどの値でも同じ答えになります
    constexpr double nominalRate = 48000.0;

    return process (input, semitones,
                     WarpMap::makeLinear (juce::jlimit (minStretch, maxStretch, stretch), 1.0),
                     nominalRate, shouldAbort);
}

juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input,
                                   int semitones, const WarpMap& map, double sampleRate,
                                   std::function<bool()> shouldAbort)
{
    const int numSamples = input.getNumSamples();
    const int numChannels = input.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || sampleRate <= 0.0)
        return {};

    if (isIdentity (semitones, map))
        return input;   // そのまま

    const double pitchRatio = getPitchRatio (semitones);

    // 8.150：**表を「音程のぶんだけ引き伸ばしたもの」に作り替える**（Phase 188）。
    //
    // 伸ばしてから`pitchRatio`倍の速さで読み直すと、長さが元の表のとおりに戻り、
    // 音程だけが`pitchRatio`倍になります（`AudioTransform.h`）。
    // **表全体に掛ければよい**ので、区間ごとに考える必要はありません
    WarpMap vocoderMap = map;

    for (auto& point : vocoderMap.points)
        point.warpedSeconds *= pitchRatio;

    const double inputSeconds = (double) numSamples / sampleRate;
    const double outSeconds = map.sourceToWarped (inputSeconds) - map.sourceToWarped (0.0);
    const int outSamples = juce::jmax (1, (int) std::llround (outSeconds * sampleRate));

    juce::AudioBuffer<float> output (numChannels, outSamples);
    output.clear();

    juce::dsp::FFT fft (fftOrder);
    const auto window = makeHannWindow();

    // **チャンネルごとに処理する。** 全チャンネルぶんの伸ばした波形を同時に
    // 持つと、2オクターブ上げでメモリが4倍×チャンネル数になります
    for (int channel = 0; channel < numChannels; ++channel)
    {
        if (shouldAbort != nullptr && shouldAbort())
            return {};

        auto stretched = timeStretchChannel (input.getReadPointer (channel), numSamples,
                                              vocoderMap, sampleRate, fft, window, shouldAbort);

        if (stretched.empty())
            return {};   // 途中で止められた

        // 伸ばしたぶんを`pitchRatio`倍の速さで読み直す。
        // **音程を変えないときは等速**なので、ここは素通しになります
        juce::LagrangeInterpolator interpolator;
        interpolator.reset();
        interpolator.process (pitchRatio, stretched.data(),
                               output.getWritePointer (channel), outSamples);
    }

    return output;
}

juce::String renderToFile (const juce::File& source, int semitones, double stretch,
                            std::function<bool()> shouldAbort)
{
    return renderToFile (source, semitones,
                          WarpMap::makeLinear (juce::jlimit (minStretch, maxStretch, stretch), 1.0),
                          shouldAbort);
}

juce::String renderToFile (const juce::File& source, int semitones, const WarpMap& map,
                            std::function<bool()> shouldAbort)
{
    if (isIdentity (semitones, map))
        return {};   // 素のファイルをそのまま使うので、作るものがありません

    if (! source.existsAsFile())
        return utf8 ("元のファイルが見つかりません: ") + source.getFullPathName();

    const auto destination = getRenderedFileFor (source, semitones, map);

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    // **既に在って読めるなら、何もしない。** 読めないものは作りかけなので作り直す
    if (destination.existsAsFile())
        if (auto existing = std::unique_ptr<juce::AudioFormatReader> (
                formatManager.createReaderFor (destination)))
            return {};

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (source));

    if (reader == nullptr)
        return utf8 ("元のファイルを読み取れません: ") + source.getFileName();

    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return utf8 ("元のファイルに中身がありません: ") + source.getFileName();

    const double lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;

    if (lengthSeconds > maxSourceSeconds)
        return utf8 ("ファイルが長すぎます（")
                + juce::String (lengthSeconds / 60.0, 1) + utf8 ("分）。音程・長さの作り直しは")
                + juce::String ((int) (maxSourceSeconds / 60.0))
                + utf8 ("分までです——先に必要なところだけ切り出してください。");

    const int numChannels = juce::jmax (1, (int) reader->numChannels);
    const int numSamples = (int) reader->lengthInSamples;

    juce::AudioBuffer<float> buffer (numChannels, numSamples);

    if (! reader->read (&buffer, 0, numSamples, 0, true, true))
        return utf8 ("元のファイルを読み取れませんでした: ") + source.getFileName();

    const double sampleRate = reader->sampleRate;
    reader.reset();

    buffer = process (buffer, semitones, map, sampleRate, shouldAbort);

    if (buffer.getNumSamples() <= 0)
        return {};   // 途中で止められた（作りかけは書かない）

    auto folderResult = getCacheFolder().createDirectory();

    if (folderResult.failed())
        return utf8 ("キャッシュ用フォルダを作れませんでした: ") + folderResult.getErrorMessage();

    // **いったん別の名前で書いて、出来てから置き換える。**
    // 途中で落ちると、中途半端なファイルを次から本物として掴みます
    auto temporary = destination.getSiblingFile (destination.getFileNameWithoutExtension() + ".partial");
    temporary.deleteFile();

    {
        juce::WavAudioFormat wavFormat;

        std::unique_ptr<juce::FileOutputStream> stream (temporary.createOutputStream());

        if (stream == nullptr)
            return utf8 ("キャッシュを書き出せませんでした: ") + temporary.getFullPathName();

        // **32bit floatで書く。** 音程を動かすと山が1.0を超えることがあり、
        // 整数で書くとそこが潰れます（キャッシュなので大きさは気にしません）
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wavFormat.createWriterFor (stream.get(), sampleRate,
                                        (unsigned int) numChannels, 32, {}, 0));

        if (writer == nullptr)
            return utf8 ("キャッシュを書き出せませんでした（形式が作れません）: ") + temporary.getFileName();

        stream.release();   // 成功したらwriterが持つ

        if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        {
            writer.reset();
            temporary.deleteFile();
            return utf8 ("キャッシュの書き出しに失敗しました: ") + temporary.getFileName();
        }
    }

    destination.deleteFile();

    if (! temporary.moveFileTo (destination))
    {
        temporary.deleteFile();
        return utf8 ("キャッシュを置き換えられませんでした: ") + destination.getFileName();
    }

    pruneCache();

    return {};
}

void pruneCache (juce::int64 maxTotalBytes)
{
    auto folder = getCacheFolder();

    if (! folder.isDirectory())
        return;

    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.wav");

    juce::int64 total = 0;

    for (const auto& file : files)
        total += file.getSize();

    if (total <= maxTotalBytes)
        return;

    // **古いものから消す。** どれが要るかはここでは分かりません
    // （他のプロジェクトが使っているかもしれない）。消しても
    // 次に開いたときに作り直されるだけです
    std::sort (files.begin(), files.end(),
                [] (const juce::File& a, const juce::File& b)
                {
                    return a.getLastModificationTime() < b.getLastModificationTime();
                });

    for (const auto& file : files)
    {
        if (total <= maxTotalBytes)
            break;

        const auto size = file.getSize();

        if (file.deleteFile())
            total -= size;
    }
}

} // namespace AudioTransform
