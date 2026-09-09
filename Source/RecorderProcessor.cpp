#include "RecorderProcessor.h"
#include "Utf8.h"

RecorderProcessor::RecorderProcessor (Transport& transportToUse)
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      transport (transportToUse)
{
    // ThreadedWriterが実際のディスク書き込みを行うためのスレッド。
    // 録音のたびに起動/停止するのではなく、常駐させておく（起動コストを録音開始時に持ち込まない）。
    backgroundThread.startThread();
}

RecorderProcessor::~RecorderProcessor()
{
    stopRecording();
    backgroundThread.stopThread (2000);
}

void RecorderProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    for (auto& level : inputLevels)
        level.store (0.0f);
}

void RecorderProcessor::releaseResources()
{
    for (auto& level : inputLevels)
        level.store (0.0f);
}

void RecorderProcessor::setInputMonitoringEnabled (bool shouldMonitor)
{
    monitoring.store (shouldMonitor);
}

float RecorderProcessor::getInputLevel (int channel) const
{
    if (! juce::isPositiveAndBelow (channel, numMeterChannels))
        return 0.0f;

    return inputLevels[channel].load();
}

juce::String RecorderProcessor::startRecording (const juce::File& file)
{
    stopRecording(); // 前の録音が残っていれば確実に閉じてから始める

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

    if (stream == nullptr)
        return utf8 ("録音ファイルを作成できませんでした: ") + file.getFullPathName();

    // 24bit固定。仕様書3章では16/24/32bit floatに対応予定だが、
    // プロジェクト設定（ProjectModelのbitDepth）との連動は別フェーズで行う。
    juce::WavAudioFormat wavFormat;
    auto writer = wavFormat.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                        .withSampleRate (currentSampleRate)
                                                        .withNumChannels (2)
                                                        .withBitsPerSample (24));

    if (writer == nullptr)
        return utf8 ("WAVファイルの書き出し形式を用意できませんでした。");

    // 成功した時点でstreamの所有権はwriterへ移っている（新APIはunique_ptrで受け渡すため、
    // 旧APIのようにrelease()を書き忘れて二重解放、という事故が起きない）。
    threadedWriter.reset (new juce::AudioFormatWriter::ThreadedWriter (writer.release(), backgroundThread, 32768));
    recordedSamples.store (0);

    // オーディオスレッドから見えるポインタの差し替えはロック内で行う
    const juce::ScopedLock lock (writerLock);
    activeWriter.store (threadedWriter.get());

    return {};
}

void RecorderProcessor::stopRecording()
{
    {
        // 先にオーディオスレッドからの参照を切る。これ以降、processBlockは書き込みを行わない。
        const juce::ScopedLock lock (writerLock);
        activeWriter.store (nullptr);
    }

    // ThreadedWriterの破棄で、バッファに残っているぶんが書き出されファイルが閉じられる
    threadedWriter.reset();
}

double RecorderProcessor::getRecordedSeconds() const
{
    return currentSampleRate > 0.0 ? (double) recordedSamples.load() / currentSampleRate : 0.0;
}

void RecorderProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), numMeterChannels);

    // 入力レベルの計測。ブロック内のピーク値をそのまま表示すると点滅して読み取りづらいため、
    // 前回値を減衰させたものと比較し、大きい方を採用する（一般的なピークメーターの挙動）。
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float peak = buffer.getMagnitude (ch, 0, numSamples);
        const float decayed = inputLevels[ch].load() * meterDecayPerBlock;
        inputLevels[ch].store (juce::jmax (peak, decayed));
    }

    // 入力チャンネルが1本しか無い（モノラル入力）場合、右チャンネルのメーターが
    // 動かないままになるので、左の値をそのまま流用する
    if (numChannels == 1)
        inputLevels[1].store (inputLevels[0].load());

    // 仕様書5.4：録音中なら、入力バッファをそのままファイルへ書き出す。
    // 出力用にバッファをクリアする前に行う必要がある（クリア後だと無音が録れてしまう）。
    // ここでのロックは、startRecording/stopRecordingによるライター差し替えとの競合を
    // 防ぐためのもの。書き込み自体はThreadedWriterがバックグラウンドで行うため、
    // オーディオスレッドがディスクI/Oを待つことはない。
    //
    // **トランスポートが動いている間だけ書く**（Phase 39）。
    // カウントイン中はクリックだけ鳴ってトランスポートは止まっているので、
    // ここで弾かないとカウントインぶんまでファイルに入る。
    // この判定にしておくと、録音の開始位置がサンプル単位で正確になる。
    if (transport.isPlaying())
    {
        const juce::ScopedLock lock (writerLock);

        if (auto* writer = activeWriter.load())
        {
            if (buffer.getNumChannels() >= 2)
            {
                writer->write (buffer.getArrayOfReadPointers(), numSamples);
                recordedSamples.fetch_add ((juce::int64) numSamples);
            }
        }
    }

    // モニタリングOFFのときは、入力音を出力へ通さない。
    // 内蔵マイクとスピーカーの組み合わせではハウリングが起きやすいため、
    // 明示的にONにしたときだけ音を返す方針にしている（仕様書5.4の入力モニタリング）。
    if (! monitoring.load())
        buffer.clear();
}

const juce::String RecorderProcessor::getName() const   { return "Recorder"; }
double RecorderProcessor::getTailLengthSeconds() const   { return 0.0; }
bool RecorderProcessor::acceptsMidi() const              { return false; }
bool RecorderProcessor::producesMidi() const             { return false; }

juce::AudioProcessorEditor* RecorderProcessor::createEditor() { return nullptr; }
bool RecorderProcessor::hasEditor() const                     { return false; }

int RecorderProcessor::getNumPrograms()                        { return 1; }
int RecorderProcessor::getCurrentProgram()                     { return 0; }
void RecorderProcessor::setCurrentProgram (int)                {}
const juce::String RecorderProcessor::getProgramName (int)     { return {}; }
void RecorderProcessor::changeProgramName (int, const juce::String&) {}

void RecorderProcessor::getStateInformation (juce::MemoryBlock&) {}
void RecorderProcessor::setStateInformation (const void*, int)   {}
