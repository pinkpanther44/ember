#include "Mp3Writer.h"
#include "Utf8.h"

#if JUCE_WINDOWS

// **JUCEのヘッダより後に入れること。** Windowsのヘッダは`min`/`max`をマクロにするので、
// 先に入れると`juce::jmin`の中の`std::min`が壊れます（`NOMINMAX`で止めています）
#ifndef NOMINMAX
 #define NOMINMAX 1
#endif

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

namespace
{
    /** COMのポインタを持ち回るだけの入れ物。**手で`Release()`を書かない**
        （途中で戻る道が何本もあるので、必ずどこかで忘れます）。 */
    template <typename Type>
    struct ComPtr
    {
        ComPtr() = default;
        ComPtr (const ComPtr&) = delete;
        ComPtr& operator= (const ComPtr&) = delete;

        ~ComPtr() { reset(); }

        void reset()
        {
            if (ptr != nullptr)
            {
                ptr->Release();
                ptr = nullptr;
            }
        }

        Type** address()   { reset(); return &ptr; }
        Type* get() const  { return ptr; }
        Type* operator->() const { return ptr; }
        explicit operator bool() const { return ptr != nullptr; }

        Type* ptr = nullptr;
    };

    /** `MFStartup()`と`CoInitializeEx()`の面倒を見る。

        **スレッドごとに要ります。** 書き出しは`ThreadWithProgressWindow`の
        別スレッドで走るので、そこで初期化しないとMFの呼び出しが軒並み失敗します。 */
    struct MediaFoundationScope
    {
        MediaFoundationScope()
        {
            const auto hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);

            // **既に別のモデルで初期化済みでも先へ進みます**（RPC_E_CHANGED_MODE）。
            // そのときは`CoUninitialize()`を呼ばないこと——他人の初期化を外すことになる
            comInitialised = SUCCEEDED (hr);

            mfStarted = SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_LITE));
        }

        ~MediaFoundationScope()
        {
            if (mfStarted)
                MFShutdown();

            if (comInitialised)
                CoUninitialize();
        }

        bool isReady() const { return mfStarted; }

        bool comInitialised = false;
        bool mfStarted = false;
    };

    /** そのレート・チャンネル数・ビットレートで書けるMP3の出力形式を探す。

        **自分で組み立てず、OSに「書ける形」を挙げてもらいます**——
        MP3の出力形式は`MPEGLAYER3WAVEFORMAT`の付帯情報まで揃っている必要があり、
        手で埋めると環境によって通ったり通らなかったりします。 */
    bool findMp3OutputType (double sampleRate, int numChannels, int bitrateKbps,
                             ComPtr<IMFMediaType>& typeOut)
    {
        ComPtr<IMFCollection> types;

        if (FAILED (MFTranscodeGetAudioOutputAvailableTypes (MFAudioFormat_MP3, MFT_ENUM_FLAG_ALL,
                                                              nullptr, types.address())))
            return false;

        DWORD numTypes = 0;

        if (FAILED (types->GetElementCount (&numTypes)) || numTypes == 0)
            return false;

        const UINT32 wantedRate = (UINT32) juce::roundToInt (sampleRate);
        const UINT32 wantedChannels = (UINT32) numChannels;
        const UINT32 wantedBytesPerSecond = (UINT32) (bitrateKbps * 1000 / 8);

        IMFMediaType* best = nullptr;
        UINT32 bestDistance = std::numeric_limits<UINT32>::max();

        for (DWORD i = 0; i < numTypes; ++i)
        {
            ComPtr<IUnknown> element;

            if (FAILED (types->GetElement (i, element.address())))
                continue;

            ComPtr<IMFMediaType> candidate;

            if (FAILED (element->QueryInterface (IID_PPV_ARGS (candidate.address()))))
                continue;

            UINT32 rate = 0, channels = 0, bytesPerSecond = 0;

            if (FAILED (candidate->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate))
                 || FAILED (candidate->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &channels))
                 || FAILED (candidate->GetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytesPerSecond)))
                continue;

            if (rate != wantedRate || channels != wantedChannels)
                continue;

            // **ぴったり無ければ、いちばん近いビットレート**を採る
            const UINT32 distance = bytesPerSecond > wantedBytesPerSecond
                                        ? bytesPerSecond - wantedBytesPerSecond
                                        : wantedBytesPerSecond - bytesPerSecond;

            if (distance < bestDistance)
            {
                bestDistance = distance;

                if (best != nullptr)
                    best->Release();

                best = candidate.ptr;
                best->AddRef();
            }
        }

        if (best == nullptr)
            return false;

        *typeOut.address() = best;
        return true;
    }

    //==========================================================================
    /** 8.153：Media Foundationへ流し込む`AudioFormatWriter`（Phase 191／D9b）。

        **浮動小数点で受け取ります**（`usesFloatingPointData = true`）。
        `write()`へ来る`const int**`は、そのとき実際にはfloatの配列です
        （JUCEの決まり）。MFへ渡す直前に16bit整数へ直します——
        MP3のエンコーダは整数しか受け取りません。 */
    class MediaFoundationMp3Writer : public juce::AudioFormatWriter
    {
    public:
        MediaFoundationMp3Writer (double rate, int channels)
            : AudioFormatWriter (nullptr, "MP3", rate, (unsigned int) channels, 16)
        {
            usesFloatingPointData = true;
        }

        ~MediaFoundationMp3Writer() override
        {
            finish();
        }

        bool open (const juce::File& file, int bitrateKbps, juce::String& errorOut)
        {
            if (! scope.isReady())
            {
                errorOut = utf8 ("Media Foundationを初期化できませんでした。");
                return false;
            }

            ComPtr<IMFMediaType> outputType;

            if (! findMp3OutputType (sampleRate, (int) numChannels, bitrateKbps, outputType))
            {
                errorOut = utf8 ("この組み合わせで書けるMP3の形式が見つかりませんでした（")
                             + juce::String (juce::roundToInt (sampleRate)) + utf8 ("Hz・")
                             + juce::String ((int) numChannels) + utf8 ("ch・")
                             + juce::String (bitrateKbps) + utf8 ("kbps）。");
                return false;
            }

            file.deleteFile();

            if (FAILED (MFCreateSinkWriterFromURL (file.getFullPathName().toWideCharPointer(),
                                                    nullptr, nullptr, sinkWriter.address())))
            {
                errorOut = utf8 ("MP3ファイルを作成できませんでした: ") + file.getFullPathName();
                return false;
            }

            if (FAILED (sinkWriter->AddStream (outputType.get(), &streamIndex)))
            {
                errorOut = utf8 ("MP3の書き出しストリームを用意できませんでした。");
                return false;
            }

            ComPtr<IMFMediaType> inputType;

            if (FAILED (MFCreateMediaType (inputType.address())))
            {
                errorOut = utf8 ("入力形式を用意できませんでした。");
                return false;
            }

            const UINT32 blockAlign = (UINT32) numChannels * 2;   // 16bit＝2バイト

            inputType->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            inputType->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
            inputType->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            inputType->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) juce::roundToInt (sampleRate));
            inputType->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, (UINT32) numChannels);
            inputType->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
            inputType->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                   (UINT32) juce::roundToInt (sampleRate) * blockAlign);
            inputType->SetUINT32 (MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

            if (FAILED (sinkWriter->SetInputMediaType (streamIndex, inputType.get(), nullptr)))
            {
                errorOut = utf8 ("MP3のエンコーダを用意できませんでした。");
                return false;
            }

            if (FAILED (sinkWriter->BeginWriting()))
            {
                errorOut = utf8 ("MP3の書き出しを開始できませんでした。");
                return false;
            }

            targetFile = file;
            return true;
        }

        bool write (const int** samplesToWrite, int numSamples) override
        {
            if (! sinkWriter || numSamples <= 0)
                return sinkWriter.get() != nullptr;

            // JUCEの決まり：`usesFloatingPointData`ならfloatの配列が来る
            const auto* const* channels = reinterpret_cast<const float* const*> (samplesToWrite);

            const int channelCount = (int) numChannels;
            const DWORD numBytes = (DWORD) (numSamples * channelCount * 2);

            ComPtr<IMFMediaBuffer> mediaBuffer;

            if (FAILED (MFCreateMemoryBuffer (numBytes, mediaBuffer.address())))
                return false;

            BYTE* raw = nullptr;

            if (FAILED (mediaBuffer->Lock (&raw, nullptr, nullptr)))
                return false;

            auto* destination = reinterpret_cast<juce::int16*> (raw);

            for (int i = 0; i < numSamples; ++i)
                for (int ch = 0; ch < channelCount; ++ch)
                {
                    // **必ず頭を押さえること。** 0dBを超えたサンプルをそのまま
                    // 16bitへ落とすと、上下で折り返して激しく歪みます
                    const float value = juce::jlimit (-1.0f, 1.0f, channels[ch][i]);
                    *destination++ = (juce::int16) juce::roundToInt (value * 32767.0f);
                }

            mediaBuffer->Unlock();
            mediaBuffer->SetCurrentLength (numBytes);

            ComPtr<IMFSample> sample;

            if (FAILED (MFCreateSample (sample.address())))
                return false;

            sample->AddBuffer (mediaBuffer.get());

            // 100ナノ秒単位。**時刻を入れないと、長さの分からないファイルになります**
            const LONGLONG duration = (LONGLONG) ((double) numSamples * 10000000.0 / sampleRate);

            sample->SetSampleTime (sampleTime);
            sample->SetSampleDuration (duration);
            sampleTime += duration;

            return SUCCEEDED (sinkWriter->WriteSample (streamIndex, sample.get()));
        }

        void finish()
        {
            if (sinkWriter)
            {
                sinkWriter->Finalize();
                sinkWriter.reset();
            }
        }

    private:
        // **`scope`をいちばん先に置くこと。** メンバは宣言と逆の順で壊れるので、
        // `MFShutdown()`はCOMのポインタを全部手放した後になります
        MediaFoundationScope scope;

        ComPtr<IMFSinkWriter> sinkWriter;
        DWORD streamIndex = 0;
        LONGLONG sampleTime = 0;
        juce::File targetFile;
    };
}

#endif // JUCE_WINDOWS

//==============================================================================
namespace Mp3Writer
{
    bool isSupportedSampleRate (double sampleRate)
    {
        const int rate = juce::roundToInt (sampleRate);

        // MPEG-1 Layer III にあるのはこの3つだけ
        return rate == 32000 || rate == 44100 || rate == 48000;
    }

    const juce::Array<int>& getBitrateChoices()
    {
        static const juce::Array<int> choices { 128, 192, 256, 320 };
        return choices;
    }

   #if JUCE_WINDOWS

    bool isAvailable()
    {
        // **1度だけ調べます。** 毎回MFを起こして一覧を引くと、
        // ダイアログを開くたびに待たされます
        static const bool available = []
        {
            MediaFoundationScope scope;

            if (! scope.isReady())
                return false;

            ComPtr<IMFMediaType> type;

            // いちばん普通の組み合わせで1つでも見つかれば、エンコーダはある
            return findMp3OutputType (44100.0, 2, 192, type);
        }();

        return available;
    }

    std::unique_ptr<juce::AudioFormatWriter> createWriter (const juce::File& file,
                                                            double sampleRate,
                                                            int numChannels,
                                                            int bitrateKbps,
                                                            juce::String& errorOut)
    {
        if (! isSupportedSampleRate (sampleRate))
        {
            errorOut = utf8 ("MP3は 32000／44100／48000 Hz にしか対応していません。");
            return nullptr;
        }

        auto writer = std::make_unique<MediaFoundationMp3Writer> (sampleRate, numChannels);

        if (! writer->open (file, bitrateKbps, errorOut))
            return nullptr;

        return writer;
    }

   #else

    bool isAvailable() { return false; }

    std::unique_ptr<juce::AudioFormatWriter> createWriter (const juce::File&, double, int, int,
                                                            juce::String& errorOut)
    {
        errorOut = utf8 ("MP3の書き出しはWindowsでのみ使えます。");
        return nullptr;
    }

   #endif
}
