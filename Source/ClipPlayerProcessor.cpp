#include "ClipPlayerProcessor.h"
#include "AudioTransform.h"   // 8.147：トランスポーズ済みファイルの場所（Phase 185）
#include <cmath>
#include <algorithm>

namespace
{
    /** 8.152：補間のために、ブロックの前後へ余分に読むサンプル数（Phase 190）。

        4点（前1・後2）を使うので4あれば足りますが、切り上げの端数ぶんを足して8。 */
    constexpr int interpolationMargin = 8;

    /** 8.152：**倍率がちょうど1と見なす幅**（Phase 190）。

        44100と48000のような食い違いは拾いたい一方で、**丸め誤差で補間へ落ちない**ように。
        1サンプルあたり10億分の1のずれは、1時間の曲でも4サンプルに届きません。 */
    constexpr double rateRatioTolerance = 1.0e-9;

    /** 8.152：4点の3次補間（Catmull-Rom。Phase 190）。

        **位置だけで決まります**——フィルタの状態を持たないので、
        どこへ飛んでも継ぎ目が出ません（このクラスは毎ブロック飛び直します）。

        線形補間より素直で、**取り込んだ44.1kを48kで鳴らす**くらいの倍率なら
        耳で分かる粗さは出ません。**大きく下げる**（96k→44.1k等）と、
        折り返しが乗ります——そこまで要るなら、`AudioTransform`のように
        **書き出して差し替える**側でやることになります。 */
    inline float interpolateCatmullRom (float y0, float y1, float y2, float y3, float t)
    {
        const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float b =         y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c = -0.5f * y0             + 0.5f * y2;

        return ((a * t + b) * t + c) * t + y1;
    }
}

ClipPlayerProcessor::ClipPlayerProcessor (ProjectModel& projectToUse, Transport& transportToUse,
                                            juce::String trackIdToPlay)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      project (projectToUse),
      transport (transportToUse),
      trackId (std::move (trackIdToPlay))
{
    formatManager.registerBasicFormats();
}

ClipPlayerProcessor::~ClipPlayerProcessor() = default;

void ClipPlayerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = juce::jmax (1, samplesPerBlock);
    scratchBuffer.setSize (2, currentBlockSize + interpolationMargin);
}

void ClipPlayerProcessor::releaseResources()
{
    const juce::SpinLock::ScopedLockType lock (clipSourcesLock);
    clipSources.clear();
}

void ClipPlayerProcessor::prepareClipsForPlayback()
{
    // 新しいリストはロックの外（メッセージスレッド上）で組み立て、
    // 完成したら一瞬だけロックして入れ替える（オーディオスレッド側の待ち時間を最小化する）。
    std::vector<ActiveClip> newClipSources;
    double maxSamplesPerOutput = 1.0;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        // 自分の担当トラック以外は見ない（Phase 12cで1トラック1プレイヤーになった）
        if (track.getType() != TrackType::Audio || track.getId() != trackId)
            continue;

        for (int c = 0; c < track.getNumClips(); ++c)
        {
            auto clip = track.getClip (c);
            juce::File file (clip.getSourceFilePath());

            // 8.147／8.149：**「どのファイルを開くか」と、offsetの掛け算だけ**
            // （Phase 185／改善案㉞、Phase 187／8.48）。
            //
            // 音程と長さを作り直したものは`AudioTransform`がキャッシュに持っています。
            // **ソースの位置`t`は、出来上がりでは`t × stretch`**なので、
            // `offset`にそれを掛ければ読む場所が決まります。
            // `lengthSamples`はタイムラインの長さなので**掛けません**
            // （出来上がりはタイムラインと等速で並んでいる）。
            // フェードも逆再生も、クリップの中の位置で決まるので**そのまま**です。
            //
            // **まだ出来ていなければ素のファイルを鳴らします**（無音にしない）。
            // 作るのは`ClipAudioRenderer`が別のスレッドでやっていて、
            // 出来上がったら`prepareClipsForPlayback()`が呼び直されます
            const int semitones = clip.getTranspose();
            const auto warpMap = clip.getRenderWarpMap();

            // ソースの時刻に掛ける倍率。**作り直したものを開けたときだけ効かせること**
            // ——素のファイルを鳴らしている間に掛けると、**別の場所から読みます**
            //
            // 8.150：**マーカーがあっても、掛け算のままでよい**（Phase 188/8.48）。
            // クリップの頭より前は`getStretch()`倍のまっすぐな線と決めてあるので、
            // 読み始める場所は`offset × stretch`のままです（`getRenderWarpMap()`）
            double sourceScale = 1.0;

            if (! AudioTransform::isIdentity (semitones, warpMap))
                if (auto rendered = AudioTransform::getRenderedFileFor (file, semitones, warpMap);
                     rendered.existsAsFile())
                {
                    file = rendered;
                    sourceScale = clip.getStretch();
                }

            std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
            if (reader == nullptr)
                continue; // 読み込めないファイルは無視して再生を続ける

            // 8.152：**ファイルのレートと出力のレートは別物**（Phase 190／D9aの前提）。
            //
            // Phase 189まで、読む位置を**出力のサンプル数**で数えていました。
            // 48kのプロジェクトに44.1kのファイルを入れると、1秒ぶん進めるつもりで
            // 48000サンプル進むので、**8.8%速く（＝1.5半音高く）鳴っていました**。
            //
            // 読む位置は**ファイルのサンプル数**で持ち、
            // 出力1サンプルあたり`fileRate / outputRate`ずつ進めます
            const double fileSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate
                                                                     : currentSampleRate;

            ActiveClip active;
            active.startSample = (juce::int64) (clip.getStartTime() * currentSampleRate);
            active.lengthSamples = (juce::int64) (clip.getLength() * currentSampleRate);
            active.fadeInSamples = (juce::int64) (clip.getFadeInSeconds() * currentSampleRate);
            active.fadeOutSamples = (juce::int64) (clip.getFadeOutSeconds() * currentSampleRate);

            // **秒で持っているものへは、ファイルのレートを掛けること**
            // （`sourceScale`は伸縮ぶん。8.149）
            active.sourceStartSamples = clip.getOffset() * sourceScale * fileSampleRate;
            active.sourceSamplesPerOutput = fileSampleRate / currentSampleRate;

            active.gainLinear = clip.getGainLinear();   // Phase 80：クリップゲイン（8.40）
            active.reversed = clip.isReversed();        // Phase 86：逆再生（8.46）
            active.mono     = clip.isMono();            // 8.228：モノラル化（Phase 249）
            active.readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);

            maxSamplesPerOutput = juce::jmax (maxSamplesPerOutput, active.sourceSamplesPerOutput);

            newClipSources.push_back (std::move (active));
        }
    }

    // 8.152：**読み置き場をロックの外で広げる**（Phase 190）。
    // 96kのファイルを48kで鳴らすなら、1ブロックにその倍のサンプルが要ります。
    // オーディオスレッドが待っている最中に確保しないよう、ここで作って差し替えるだけにする
    const int neededSamples = (int) std::ceil ((double) currentBlockSize * maxSamplesPerOutput)
                                  + interpolationMargin;

    juce::AudioBuffer<float> newScratch;

    if (neededSamples > scratchBuffer.getNumSamples())
        newScratch.setSize (2, neededSamples);

    const juce::SpinLock::ScopedLockType lock (clipSourcesLock);
    clipSources = std::move (newClipSources);

    if (newScratch.getNumSamples() > 0)
        scratchBuffer = std::move (newScratch);
}

juce::int64 ClipPlayerProcessor::getEndPositionSamples() const
{
    const juce::SpinLock::ScopedLockType lock (clipSourcesLock);

    juce::int64 endPosition = 0;

    for (const auto& clipSrc : clipSources)
        endPosition = juce::jmax (endPosition, clipSrc.startSample + clipSrc.lengthSamples);

    return endPosition;
}

void ClipPlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();

    if (! transport.isPlaying())
        return;

    const juce::SpinLock::ScopedLockType lock (clipSourcesLock);

    // 再生位置は共有のTransportから読む（自分では進めない。Transport.h参照）
    const juce::int64 blockStartSample = transport.getPositionSamples();
    const int numSamples = buffer.getNumSamples();

    for (auto& clipSrc : clipSources)
    {
        const juce::int64 clipEndSample = clipSrc.startSample + clipSrc.lengthSamples;

        // このブロックの範囲とクリップの範囲が重ならなければスキップ
        if (blockStartSample >= clipEndSample || blockStartSample + numSamples <= clipSrc.startSample)
            continue;

        const int blockOffsetForClipStart = (int) juce::jmax ((juce::int64) 0, clipSrc.startSample - blockStartSample);
        const juce::int64 sourceStartInThisSegment = juce::jmax ((juce::int64) 0, blockStartSample - clipSrc.startSample);
        const int samplesAvailableInClip = (int) juce::jmin ((juce::int64) (numSamples - blockOffsetForClipStart),
                                                               clipSrc.lengthSamples - sourceStartInThisSegment);

        if (samplesAvailableInClip <= 0)
            continue;

        // 8.152：**クリップの中の位置 → ファイルの位置**（Phase 190）。
        //
        // 8.46：逆再生は、クリップの中のp番目が**末尾から数えてp番目**に当たる
        // （Phase 86。ファイルは触らず、読む向きを変えるだけ）。
        // Phase 189までは「鏡の位置から読んで、読んだ後にひっくり返す」でしたが、
        // **位置を鏡にするほうへ寄せました**——補間は位置で決まるので、
        // 並べ替えてから補間すると継ぎ目が出ます
        const double ratio = clipSrc.sourceSamplesPerOutput;

        auto sourcePositionFor = [&clipSrc, ratio] (juce::int64 positionInClip)
        {
            const juce::int64 p = clipSrc.reversed ? (clipSrc.lengthSamples - 1 - positionInClip)
                                                    : positionInClip;

            return clipSrc.sourceStartSamples + (double) p * ratio;
        };

        const double firstPosition = sourcePositionFor (sourceStartInThisSegment);
        const double lastPosition  = sourcePositionFor (sourceStartInThisSegment
                                                          + samplesAvailableInClip - 1);

        // **逆再生では終わりのほうが手前**なので、両端を見て小さいほうから読む
        const double lowestPosition  = juce::jmin (firstPosition, lastPosition);
        const double highestPosition = juce::jmax (firstPosition, lastPosition);

        // 補間は前1・後2サンプルを見るので、その余白ぶん広く読む
        const juce::int64 firstNeeded = (juce::int64) std::floor (lowestPosition) - 1;
        const juce::int64 lastNeeded  = (juce::int64) std::floor (highestPosition) + 2;

        const juce::int64 readStart = juce::jmax ((juce::int64) 0, firstNeeded);
        const int numToRead = (int) juce::jlimit ((juce::int64) 1,
                                                   (juce::int64) scratchBuffer.getNumSamples(),
                                                   lastNeeded - readStart + 1);

        clipSrc.readerSource->setNextReadPosition (readStart);

        juce::AudioSourceChannelInfo info (&scratchBuffer, 0, numToRead);
        clipSrc.readerSource->getNextAudioBlock (info);

        // **倍率がちょうど1なら補間しない**（Phase 189までと同じ道）。
        // ほとんどのファイルはプロジェクトと同じレートなので、
        // そこを補間へ通すと、直す前より音が悪くなります
        const bool needsInterpolation = std::abs (ratio - 1.0) > rateRatioTolerance;

        // フェードイン/アウトのゲインランプ。
        // 単純な線形フェード（t をそのままゲインにする）は、人間の音量知覚が
        // 対数的であるため体感的に「効きが弱い」と感じやすい。多くのDAWが標準採用する
        // イコールパワーカーブ（sin/cosベース）を採用し、体感的な効果を強めている。
        //
        // **鏡にしない位置で掛けること**——フェードはクリップの中の位置で決まるので、
        // 逆再生でも「頭がフェードイン」のままです
        auto gainAt = [&clipSrc] (juce::int64 positionInClip)
        {
            // 8.40：**クリップゲインが土台**（Phase 80）。フェードはこれに掛かる
            // （順番を逆にしても結果は同じだが、「全体の音量→端の増減」と読めるようにしている）
            float gain = clipSrc.gainLinear;

            if (clipSrc.fadeInSamples > 0 && positionInClip < clipSrc.fadeInSamples)
            {
                const float t = (float) positionInClip / (float) clipSrc.fadeInSamples;
                gain *= std::sin (t * juce::MathConstants<float>::halfPi);
            }

            const juce::int64 samplesFromEnd = clipSrc.lengthSamples - positionInClip;

            if (clipSrc.fadeOutSamples > 0 && samplesFromEnd < clipSrc.fadeOutSamples)
            {
                const float t = juce::jmax (0.0f, (float) samplesFromEnd / (float) clipSrc.fadeOutSamples);
                gain *= std::sin (t * juce::MathConstants<float>::halfPi);
            }

            return gain;
        };

        // 8.228：**モノラル化**（Phase 249）。読んだチャンネルを混ぜて、
        // どの出口へも同じ音を出します。**ファイルは触りません**（逆再生と同じ）。
        //
        // **混ぜるのは補間より前**です——チャンネルごとに補間してから混ぜても
        // 結果は同じですが、**読む位置は全チャンネルで同じ**なので、
        // 先に混ぜたほうが補間が1回で済みます
        const int sourceChannels = juce::jmax (1, scratchBuffer.getNumChannels());
        const bool mixToMono = clipSrc.mono && sourceChannels > 1;
        const float monoScale = 1.0f / (float) sourceChannels;

        // 音量・パンはここでは適用しない（後段のTrackChannelProcessorが行う）
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const int sourceChannel = juce::jmin (ch, sourceChannels - 1);
            const auto* source = scratchBuffer.getReadPointer (sourceChannel);
            auto* destination = buffer.getWritePointer (ch) + blockOffsetForClipStart;

            for (int i = 0; i < samplesAvailableInClip; ++i)
            {
                const juce::int64 positionInClip = sourceStartInThisSegment + i;
                const double position = sourcePositionFor (positionInClip);
                const double base = std::floor (position);

                // 読んだ範囲の中での番号。**端は詰める**——ファイルの先頭より前や
                // 末尾より後ろは、読めたぶんの端をそのまま使う
                const int index = (int) (base - (double) readStart);

                auto tap = [&] (int offset)
                {
                    const int at = juce::jlimit (0, numToRead - 1, index + offset);

                    if (! mixToMono)
                        return source[at];

                    float sum = 0.0f;

                    for (int c = 0; c < sourceChannels; ++c)
                        sum += scratchBuffer.getReadPointer (c)[at];

                    return sum * monoScale;
                };

                const float value = needsInterpolation
                    ? interpolateCatmullRom (tap (-1), tap (0), tap (1), tap (2),
                                              (float) (position - base))
                    : tap (0);

                destination[i] += value * gainAt (positionInClip);
            }
        }
    }
}

const juce::String ClipPlayerProcessor::getName() const     { return "ClipPlayer"; }
double ClipPlayerProcessor::getTailLengthSeconds() const     { return 0.0; }
bool ClipPlayerProcessor::acceptsMidi() const                { return false; }
bool ClipPlayerProcessor::producesMidi() const                { return false; }

juce::AudioProcessorEditor* ClipPlayerProcessor::createEditor() { return nullptr; }
bool ClipPlayerProcessor::hasEditor() const                      { return false; }

int ClipPlayerProcessor::getNumPrograms()                          { return 1; }
int ClipPlayerProcessor::getCurrentProgram()                       { return 0; }
void ClipPlayerProcessor::setCurrentProgram (int)                  {}
const juce::String ClipPlayerProcessor::getProgramName (int)       { return {}; }
void ClipPlayerProcessor::changeProgramName (int, const juce::String&) {}

void ClipPlayerProcessor::getStateInformation (juce::MemoryBlock&) {}
void ClipPlayerProcessor::setStateInformation (const void*, int)   {}
