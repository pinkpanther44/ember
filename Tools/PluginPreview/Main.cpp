/*
    8.259：**内蔵プラグインを、窓を出さずに確かめる道具**（Phase 267／本人の要望）。

    ─────────────────────────────────────────────────────────────────────
    何ができるか
    ─────────────────────────────────────────────────────────────────────

        PluginPreview.exe --snapshots [出力先]   … 7つの画面をPNGで撮る（ライト／ダーク）
        PluginPreview.exe --presets              … 工場プリセットのIDが全部あるか見る
        PluginPreview.exe --audio                … 音源3つのピッチ・減衰・大きさを測る
        PluginPreview.exe --eq-band              … EQにバンドを1つ足して、1つだけか見る
        PluginPreview.exe --all                  … 上ぜんぶ

    出力先を省くと、exeの隣の`preview`フォルダへ出します。
    結果は**標準出力と`preview.log`の両方**へ書きます
    （GUIアプリなので、殻によっては標準出力が見えません）。

    ─────────────────────────────────────────────────────────────────────
    なぜ窓を出さないのか
    ─────────────────────────────────────────────────────────────────────

    `Component::createComponentSnapshot()`は**ピアを要りません**。
    DAWを起動して → プロジェクトを作って → トラックを足して → 挿して、を
    毎回やらずに、**絵と音だけをまとめて取れます**。

    Phase 264・265（ギターとベース）は、これで作りました。**見つかったもの**：

    - 数値の桁がばらついていた（`SliderAttachment`が`textFromValueFunction`を差し替える）
    - 絵が`fillDestination`だと真ん中の帯しか出ない
    - **`JBass5`の奏法が1つずれていた**（Muteを選ぶとPopが鳴り、Harmonicが選べない）
    - アンプ段を外したぶん、ベースの素の大きさが0dBFSを超えていた

    どれも**聞いているだけでは気づけない**ものでした。

    ─────────────────────────────────────────────────────────────────────
    使うときの注意
    ─────────────────────────────────────────────────────────────────────

    **PowerShellはGUIサブシステムのexeを待ちません。** `& exe`で呼ぶと
    書き終わる前に次の行へ進みます——`Start-Process -Wait`で待つこと
    （8.256で、書きかけのPNGを「落ちた」と読み違えかけました）。
*/

#include <juce_gui_extra/juce_gui_extra.h>

#include "AppColours.h"

#include "Plugins/MantaFactoryPresets.h"

#include "Plugins/MantaEQ/MantaEQProcessor.h"
#include "Plugins/MantaEQ/EQFilterDesign.h"
#include "Plugins/MantaEQ/MantaEQPresets.h"
#include "Plugins/MantaComp/MantaCompProcessor.h"
#include "Plugins/MantaComp/MantaCompPresets.h"
#include "Plugins/MantaSynth/MantaSynthProcessor.h"
#include "Plugins/MantaSynth/MantaSynthPresets.h"
#include "Plugins/MantaDelay/MantaDelayProcessor.h"
#include "Plugins/MantaDelay/MantaDelayPresets.h"
#include "Plugins/MantaReverb/MantaReverbProcessor.h"
#include "Plugins/MantaReverb/MantaReverbPresets.h"
#include "Plugins/RaccoGuitar/RaccoGuitarProcessor.h"
#include "Plugins/RaccoGuitar/RaccoGuitarPresets.h"
#include "Plugins/JavaRhinoBass/JavaRhinoBassProcessor.h"
#include "Plugins/JavaRhinoBass/JavaRhinoBassPresets.h"

#include <memory>
#include <vector>

//==============================================================================
namespace
{
    juce::File outputFolder;
    juce::String report;
    int problems = 0;

    void say (const juce::String& line)
    {
        report << line << "\n";
        std::cout << line << std::endl;
    }

    void problem (const juce::String& line)
    {
        ++problems;
        say ("  ** " + line);
    }

    //==========================================================================
    // 画面を撮る

    template <typename ProcessorType>
    void snapshot (const juce::String& fileStem, AppColours::Theme theme, const char* themeName)
    {
        AppColours::setTheme (theme);

        ProcessorType processor;
        processor.prepareToPlay (48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

        if (editor == nullptr)
        {
            problem (fileStem + ": createEditor() returned nothing");
            return;
        }

        editor->setBounds (0, 0, editor->getWidth(), editor->getHeight());
        editor->resized();

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false);
        const auto file = outputFolder.getChildFile (fileStem + "_" + themeName + ".png");

        file.deleteFile();

        juce::PNGImageFormat png;

        if (auto stream = file.createOutputStream())
            png.writeImageToStream (image, *stream);

        say ("  " + file.getFileName() + "  " + juce::String (image.getWidth())
               + "x" + juce::String (image.getHeight()));
    }

    template <typename ProcessorType>
    void snapshots (const juce::String& fileStem)
    {
        snapshot<ProcessorType> (fileStem, AppColours::Theme::Light, "light");
        snapshot<ProcessorType> (fileStem, AppColours::Theme::Dark, "dark");
    }

    //==========================================================================
    // 工場プリセットの点検
    //
    // **IDを1文字間違えても、何も起きません**——`getParameter()`がnullptrを返し、
    // `apply()`はそれを黙って飛ばします。**書いたのに効かないプリセット**が
    // できあがるので、機械に数えさせます

    template <typename ProcessorType>
    void checkPresets (const juce::String& label,
                        const std::vector<MantaFactoryPresets::Preset>& presets)
    {
        ProcessorType processor;
        processor.prepareToPlay (48000.0, 512);

        auto& apvts = processor.getValueTreeState();

        int missing = 0;
        int written = 0;

        for (const auto& preset : presets)
        {
            for (const auto& pair : preset.values)
            {
                if (apvts.getParameter (pair.first) == nullptr)
                {
                    problem (label + " / " + preset.name + ": no such parameter \""
                               + pair.first + "\"");
                    ++missing;
                    continue;
                }

                ++written;
            }

            // **当てて、落ちないことも見ます**（範囲の外の値はここで丸められます）
            MantaFactoryPresets::apply (apvts, preset);
        }

        say ("  " + label + ": " + juce::String ((int) presets.size()) + " presets, "
               + juce::String (written) + " values"
               + (missing > 0 ? ("  ** " + juce::String (missing) + " missing **") : juce::String()));
    }

    //==========================================================================
    // 音を測る

    struct NoteResult
    {
        float rms = 0.0f;
        float peak = 0.0f;
        float estimatedHz = 0.0f;
        float tailRms = 0.0f;
    };

    NoteResult measure (const std::vector<float>& captured, double sampleRate)
    {
        NoteResult result;

        double sum = 0.0;

        for (auto sample : captured)
        {
            sum += (double) sample * sample;
            result.peak = juce::jmax (result.peak, std::abs (sample));
        }

        result.rms = (float) std::sqrt (sum / juce::jmax<size_t> (1, captured.size()));

        const size_t tailSamples = (size_t) (0.2 * sampleRate);
        const size_t tailStart = captured.size() > tailSamples ? captured.size() - tailSamples : 0;
        double tail = 0.0;

        for (size_t i = tailStart; i < captured.size(); ++i)
            tail += (double) captured[i] * captured[i];

        result.tailRms = (float) std::sqrt (tail / juce::jmax<size_t> (1, captured.size() - tailStart));

        // 自己相関（鳴り始めの0.2秒は避ける）。
        //
        // **項数を固定すること。** ラグごとに変えると、伸びるほど項が減って
        // 小さいラグへ偏ります（8.257でB0が「1500Hz」と出ました）
        const size_t analysisStart = juce::jmin<size_t> (captured.size(), tailSamples);
        const size_t analysisLength = juce::jmin<size_t> (captured.size() - analysisStart, 32768);

        if (analysisLength > 4096)
        {
            const size_t terms = analysisLength / 2;
            double best = 0.0;
            int bestLag = 0;

            for (int lag = (int) (sampleRate / 1500.0); lag < (int) (analysisLength / 2); ++lag)
            {
                double correlation = 0.0;

                for (size_t i = 0; i < terms; ++i)
                    correlation += (double) captured[analysisStart + i]
                                 * (double) captured[analysisStart + i + (size_t) lag];

                if (correlation > best)
                {
                    best = correlation;
                    bestLag = lag;
                }
            }

            if (bestLag > 0)
                result.estimatedHz = (float) (sampleRate / bestLag);
        }

        return result;
    }

    /** 1音鳴らして測る。`prepare`で、鳴らす前にパラメータをいじれます。 */
    template <typename ProcessorType>
    NoteResult renderNote (int midiNote, double seconds,
                            std::function<void (juce::AudioProcessorValueTreeState&)> prepare = {})
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        ProcessorType processor;
        processor.prepareToPlay (sampleRate, blockSize);

        if (prepare)
            prepare (processor.getValueTreeState());

        juce::AudioBuffer<float> block (2, blockSize);
        const int totalSamples = (int) (seconds * sampleRate);

        std::vector<float> captured;
        captured.reserve ((size_t) totalSamples);

        int position = 0;

        while (position < totalSamples)
        {
            juce::MidiBuffer midi;

            if (position == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, midiNote, 0.9f), 1);

            block.clear();
            processor.processBlock (block, midi);

            for (int i = 0; i < blockSize && position + i < totalSamples; ++i)
                captured.push_back (block.getSample (0, i));

            position += blockSize;
        }

        return measure (captured, sampleRate);
    }

    void reportNote (const juce::String& name, const NoteResult& result, float expectedHz)
    {
        juce::String line = "  " + name.paddedRight (' ', 18)
                              + " rms=" + juce::String (result.rms, 4)
                              + " peak=" + juce::String (result.peak, 4)
                              + " tail=" + juce::String (result.tailRms, 5)
                              + " hz=" + juce::String (result.estimatedHz, 2);

        if (expectedHz > 0.0f)
        {
            const float cents = 1200.0f * std::log2 (juce::jmax (1.0f, result.estimatedHz) / expectedHz);

            line << " (expected " << juce::String (expectedHz, 2)
                 << ", " << juce::String (cents, 1) << " cent)";

            // **測り方の分解能より粗いところは見ません**（8.257）
            if (std::abs (cents) > 20.0f)
                problem (name + ": pitch is off by " + juce::String (cents, 1) + " cent");
        }

        // **1.0を超えたら知らせる**（アンプ段を外したぶんの話。8.257）
        if (result.peak > 1.0f)
            problem (name + ": peak " + juce::String (result.peak, 2) + " is over full scale");

        say (line);
    }

    //==========================================================================
    // 8.265：**バンドを1つ足したとき、本当に1つだけか**（Phase 269／本人の報告）
    //
    // 本人からの報告：**「追加したバンドの他に、高音に2か所バンドが増える」**
    // （Ubuntu機のEmber EQ）。絵だけでは「バンドが増えた」のか
    // 「線の描き方がおかしい」のか分かれないので、**数で見ます**。
    //
    // 見るのは2つ：
    //
    // | | |
    // |---|---|
    // | 有効なバンドの数 | `enableFreeBand()`は1つだけ立てるはず |
    // | 高いところの応答 | 150Hzのベルなら、10kHzより上は**ほぼ0dB**のはず |
    //
    // **画面も1枚撮ります**（`manta_eq_one_band.png`）。
    // WindowsとLinuxで並べれば、描き方の違いはそこで分かります。

    void runEqBandCheck()
    {
        say ("--- one band in the EQ ---");

        AppColours::setTheme (AppColours::Theme::Light);

        MantaEQProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        auto countEnabled = [&processor]
        {
            int enabled = 0;

            for (int band = 0; band < MantaEQParams::numBands; ++band)
                if (processor.getBandSettings (band).enabled)
                    ++enabled;

            return enabled;
        };

        if (countEnabled() != 0)
            problem ("a fresh Manta EQ already has " + juce::String (countEnabled()) + " band(s) on");

        // **画面のダブルクリックと同じ道**（`EQCurveComponent::mouseDoubleClick`）
        const int created = processor.enableFreeBand (150.0f, -6.0f);

        if (created < 0)
        {
            problem ("enableFreeBand() refused to make a band");
            return;
        }

        const int enabled = countEnabled();

        if (enabled == 1)
            say ("  ok    adding one band turned on exactly one (band " + juce::String (created) + ")");
        else
            problem ("adding one band turned on " + juce::String (enabled) + " of them");

        // **合計の応答**。画面のカーブと同じ足し方です
        // （`EQCurveComponent::drawCurves`。1.27：数えるところを2つ持たない）
        auto totalDbAt = [&processor] (double frequency)
        {
            double total = 0.0;

            for (int band = 0; band < MantaEQParams::numBands; ++band)
            {
                const auto settings = processor.getBandSettings (band);

                if (! settings.active)
                    continue;

                const auto sections = EQFilterDesign::designBand (settings, 48000.0);

                if (sections.numSections == 0)
                    continue;

                total += juce::Decibels::gainToDecibels (
                    EQFilterDesign::magnitudeAt (sections, frequency, 48000.0), -60.0);
            }

            return total;
        };

        juce::String line = "  (response:";
        double worstHigh = 0.0;

        for (const double frequency : { 50.0, 150.0, 500.0, 2000.0, 8000.0, 13000.0, 19000.0 })
        {
            const double db = totalDbAt (frequency);
            line += " " + juce::String ((int) frequency) + "Hz=" + juce::String (db, 2);

            if (frequency >= 8000.0)
                worstHigh = juce::jmax (worstHigh, std::abs (db));
        }

        say (line + ")");

        // 150Hzのベル1つなら、8kHzより上は動かないはず
        if (worstHigh > 0.5)
            problem ("the response above 8 kHz moved by " + juce::String (worstHigh, 2)
                       + " dB - one bell at 150 Hz should leave it alone");
        else
            say ("  ok    nothing happens above 8 kHz (worst " + juce::String (worstHigh, 3) + " dB)");

        // **低いレートだと、どう見えるか**（8.265）。
        // ナイキストより上は、デジタルフィルタの応答が**折り返して**同じ形を繰り返します。
        // カーブは20kHzまで描くので、レートが低いと**折り返しが画面に入ります**
        for (const double rate : { 16000.0, 8000.0 })
        {
            MantaEQProcessor low;
            low.prepareToPlay (rate, 512);
            low.enableFreeBand (150.0f, -6.0f);

            juce::String lowLine = "  (at " + juce::String ((int) rate) + " Hz:";

            for (const double frequency : { 150.0, 6700.0, 8000.0, 13000.0, 15600.0, 19000.0 })
            {
                const auto settings = low.getBandSettings (0);
                const auto sections = EQFilterDesign::designBand (settings, rate);
                const double db = juce::Decibels::gainToDecibels (
                    EQFilterDesign::magnitudeAt (sections, frequency, rate), -60.0);

                lowLine += " " + juce::String ((int) frequency) + "Hz=" + juce::String (db, 2);
            }

            say (lowLine + ")");
        }

        // 目で見るための2枚（**低いレートのほうが肝心**。8.265）
        auto shoot = [] (MantaEQProcessor& target, const juce::String& stem)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (target.createEditor());

            if (ed == nullptr)
                return;

            ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());
            ed->resized();

            const auto image = ed->createComponentSnapshot (ed->getLocalBounds(), false);
            const auto file = outputFolder.getChildFile (stem + ".png");

            file.deleteFile();

            juce::PNGImageFormat png;

            if (auto stream = file.createOutputStream())
                png.writeImageToStream (image, *stream);

            say ("  (a picture of it: " + file.getFullPathName() + ")");
        };

        {
            MantaEQProcessor low;
            low.prepareToPlay (16000.0, 512);
            low.enableFreeBand (150.0f, -6.0f);
            shoot (low, "manta_eq_low_rate");
        }

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

        if (editor != nullptr)
        {
            editor->setBounds (0, 0, editor->getWidth(), editor->getHeight());
            editor->resized();

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false);
            const auto file = outputFolder.getChildFile ("manta_eq_one_band.png");

            file.deleteFile();

            juce::PNGImageFormat png;

            if (auto stream = file.createOutputStream())
                png.writeImageToStream (image, *stream);

            say ("  (a picture of it: " + file.getFullPathName() + ")");
        }
    }

    //==========================================================================
    void runSnapshots()
    {
        say ("--- snapshots ---");

        snapshots<MantaEQProcessor> ("manta_eq");
        snapshots<MantaCompProcessor> ("manta_comp");
        snapshots<MantaSynthProcessor> ("manta_synth");
        snapshots<MantaDelayProcessor> ("manta_delay");
        snapshots<MantaReverbProcessor> ("manta_reverb");
        snapshots<RaccoGuitarProcessor> ("racco_guitar");
        snapshots<JavaRhinoBassProcessor> ("java_rhino_bass");
    }

    void runPresetCheck()
    {
        say ("--- factory presets ---");

        checkPresets<MantaEQProcessor> ("Manta EQ", MantaEQPresets::all());
        checkPresets<MantaCompProcessor> ("Manta Comp", MantaCompPresets::all());
        checkPresets<MantaDelayProcessor> ("Manta Delay", MantaDelayPresets::all());
        checkPresets<MantaReverbProcessor> ("Manta Reverb", MantaReverbPresets::all());
        checkPresets<RaccoGuitarProcessor> ("Racco Guitar", RaccoGuitarPresets::all());
        checkPresets<JavaRhinoBassProcessor> ("Java Rhino Bass", JavaRhinoBassPresets::all());

        // シンセだけ表の型が別（156個。`MantaSynthPresets.h`）
        {
            MantaSynthProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            auto& apvts = processor.getValueTreeState();
            int missing = 0, written = 0;

            for (const auto& preset : MantaSynthPresets::all())
            {
                for (const auto& pair : preset.values)
                {
                    if (apvts.getParameter (pair.first) == nullptr)
                    {
                        problem ("Manta Synthesizer / " + preset.name
                                   + ": no such parameter \"" + pair.first + "\"");
                        ++missing;
                        continue;
                    }

                    ++written;
                }

                MantaSynthPresets::apply (apvts, preset);
            }

            say ("  Manta Synthesizer: "
                   + juce::String ((int) MantaSynthPresets::all().size()) + " presets, "
                   + juce::String (written) + " values"
                   + (missing > 0 ? ("  ** " + juce::String (missing) + " missing **")
                                   : juce::String()));
        }
    }

    void runAudioCheck()
    {
        say ("--- instruments ---");

        // Racco Guitar：E2 / A3 / E5、そしてキースイッチ（音が出ないこと）
        reportNote ("guitar E2", renderNote<RaccoGuitarProcessor> (40, 1.5), 82.41f);
        reportNote ("guitar A3", renderNote<RaccoGuitarProcessor> (57, 1.5), 220.00f);
        reportNote ("guitar E5", renderNote<RaccoGuitarProcessor> (76, 1.5), 659.26f);
        reportNote ("guitar C1 (silent)", renderNote<RaccoGuitarProcessor> (24, 0.5), 0.0f);

        // Java Rhino Bass：B0 / A1 / G4 と、奏法ごとの減衰
        auto setStyle = [] (int choice)
        {
            return [choice] (juce::AudioProcessorValueTreeState& apvts)
            {
                if (auto* parameter = apvts.getParameter (JavaRhinoBassParams::style))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) choice));
            };
        };

        reportNote ("bass B0", renderNote<JavaRhinoBassProcessor> (23, 2.0), 30.87f);
        reportNote ("bass A1", renderNote<JavaRhinoBassProcessor> (33, 2.0), 55.00f);
        reportNote ("bass G4", renderNote<JavaRhinoBassProcessor> (67, 2.0), 392.00f);
        reportNote ("bass slap A1", renderNote<JavaRhinoBassProcessor> (33, 2.0, setStyle (2)), 55.00f);
        reportNote ("bass mute A1", renderNote<JavaRhinoBassProcessor> (33, 2.0, setStyle (3)), 0.0f);
        reportNote ("bass A4 (silent)", renderNote<JavaRhinoBassProcessor> (69, 0.5), 0.0f);

        // Manta Synthesizer：鳴っていること（物理モデルではないのでピッチは見ない）
        reportNote ("synth A3", renderNote<MantaSynthProcessor> (57, 1.0), 0.0f);

        //----------------------------------------------------------------------
        // キースイッチ（音のスレッド）→ AsyncUpdater → パラメータ（8.257）
        {
            JavaRhinoBassProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, JavaRhinoBassProcessor::ksHarmonic, 0.9f), 0);

            block.clear();
            processor.processBlock (block, midi);

            // AsyncUpdaterはメッセージスレッドで動くので、回してやる
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

            const int choice = (int) processor.getValueTreeState()
                                                .getRawParameterValue (JavaRhinoBassParams::style)->load();

            if (choice == 5)
                say ("  bass key switch F5 -> style 5  ok");
            else
                problem ("bass key switch F5 gave style " + juce::String (choice) + " (expected 5)");
        }
    }
}

//==============================================================================
class PluginPreviewApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "PluginPreview"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }

    void initialise (const juce::String& commandLine) override
    {
        juce::ArgumentList args ("PluginPreview", commandLine);

        const bool all = args.containsOption ("--all") || args.size() == 0;
        const bool wantSnapshots = all || args.containsOption ("--snapshots");
        const bool wantPresets   = all || args.containsOption ("--presets");
        const bool wantAudio     = all || args.containsOption ("--audio");
        const bool wantEqBand    = all || args.containsOption ("--eq-band");

        outputFolder = args.size() > 0 && args[args.size() - 1].isLongOption() == false
                         ? juce::File::getCurrentWorkingDirectory()
                                .getChildFile (args[args.size() - 1].text.unquoted())
                         : juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                .getParentDirectory().getChildFile ("preview");

        outputFolder.createDirectory();

        say ("PluginPreview -> " + outputFolder.getFullPathName());

        if (wantSnapshots) runSnapshots();
        if (wantEqBand) runEqBandCheck();
        if (wantPresets)   runPresetCheck();
        if (wantAudio)     runAudioCheck();

        say ("--- " + juce::String (problems) + " problem(s) ---");

        outputFolder.getChildFile ("preview.log").replaceWithText (report);

        setApplicationReturnValue (problems == 0 ? 0 : 1);
        quit();
    }

    void shutdown() override {}
};

START_JUCE_APPLICATION (PluginPreviewApplication)
