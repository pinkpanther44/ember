/*
    8.259：**内蔵プラグインを、窓を出さずに確かめる道具**（Phase 267／本人の要望）。

    ─────────────────────────────────────────────────────────────────────
    何ができるか
    ─────────────────────────────────────────────────────────────────────

        PluginPreview.exe --snapshots [出力先]   … 9つの画面をPNGで撮る（ライト／ダーク）
        PluginPreview.exe --presets              … 工場プリセットのIDが全部あるか見る
        PluginPreview.exe --audio                … 音源5つのピッチ・減衰・大きさを測る
        PluginPreview.exe --eq-band              … EQにバンドを1つ足して、1つだけか見る
        PluginPreview.exe --icons                … ブラウザの絵がブランドの色を受けるか見る
        PluginPreview.exe --scale                … Kakapoのスケール判定（仕様書18章の試験）
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
#include "Branding.h"
#include "IconAssets.h"

#include "Plugins/MantaFactoryPresets.h"
#include "Plugins/MantaPluginFormat.h"   // 8.288：表と名乗りを突き合わせる（Phase 281）

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
#include "Plugins/OrangutanDrums/OrangutanDrumsProcessor.h"
#include "Plugins/OrangutanDrums/OrangutanDrumsPresets.h"
#include "Plugins/Kakapo/KakapoProcessor.h"

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

    //==========================================================================
    /** 8.292：**ゼロ交差で音程を数える**（Phase 285／8.289で書いたものを外へ出しました）。

        `measure()`の自己相関は、**2つの場面で嘘をつきます**：

        | | |
        |---|---|
        | 速く減衰する低音 | いちばん短いラグへ寄る（8.289。ドラムのベースゾーンで踏みました） |
        | 減衰しない波形 | **周期の何倍のところも同じ相関**になり、どれが選ばれるか分からない（`Kakapo`のリードで踏みました。440Hzが「40Hz」と出ました） |

        どちらも**上りのゼロ交差を数える**ほうが素直です。しきい値は峰の10%で、
        0の近くの雑音を数えないようにしてあります。

        > **測り方は、測るものに合わせること。** 同じ物差しを使い回して
        > 「壊れている」と読み違えるところでした（2回目）。 */
    float hzByZeroCrossings (const std::vector<float>& samples, double sampleRate,
                              double fromSeconds, double toSeconds)
    {
        const size_t from = (size_t) (fromSeconds * sampleRate);
        const size_t to = juce::jmin (samples.size(), (size_t) (toSeconds * sampleRate));

        if (to <= from + 2)
            return 0.0f;

        float peak = 0.0f;

        for (size_t i = from; i < to; ++i)
            peak = juce::jmax (peak, std::abs (samples[i]));

        if (peak < 1.0e-4f)
            return 0.0f;

        const float threshold = peak * 0.1f;

        int crossings = 0;
        bool above = samples[from] > 0.0f;
        size_t firstCrossing = 0, lastCrossing = 0;

        for (size_t i = from; i < to; ++i)
        {
            if (above && samples[i] < -threshold)
            {
                above = false;
            }
            else if (! above && samples[i] > threshold)
            {
                above = true;
                ++crossings;

                if (crossings == 1) firstCrossing = i;
                lastCrossing = i;
            }
        }

        if (crossings < 2)
            return 0.0f;

        const double periods = (double) (crossings - 1);
        const double seconds = (double) (lastCrossing - firstCrossing) / sampleRate;

        return (float) (periods / juce::jmax (1.0e-6, seconds));
    }

    /** 1音鳴らして、**左chの波形をそのまま**返す。
        `prepare`で、鳴らす前にパラメータをいじれます。 */
    template <typename ProcessorType>
    std::vector<float> renderNoteSamples (int midiNote, double seconds,
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

        return captured;
    }

    /** 1音鳴らして測る。 */
    template <typename ProcessorType>
    NoteResult renderNote (int midiNote, double seconds,
                            std::function<void (juce::AudioProcessorValueTreeState&)> prepare = {})
    {
        return measure (renderNoteSamples<ProcessorType> (midiNote, seconds, std::move (prepare)),
                         48000.0);
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
    // 8.266：**ブラウザの絵が、本当に塗り替わるか**（Phase 269／本人の指定）
    //
    // `Drawable::replaceColour()`は**差し替え元の色が1ビットでも違うと、
    // 黙って何もしません**（戻り値のboolだけが教えてくれます）。
    // 絵を描き直したときに色が変わっていると、**画面を開くまで気づけません**。
    //
    // 絵も1枚落とします（`browser_icons.png`）。**絵のままの色と、
    // アクセントの色を並べて**、ライト／ダークそれぞれの地の上に描くので、
    // **どちらが良いか目で比べられます**（地が違うと見え方が変わるため、
    // 白の上に並べても判断できません）。

    void runIconCheck()
    {
        say ("--- browser icons ---");

        struct Entry
        {
            const char* resource;
            juce::uint32 source;
            const char* name;
        };

        const Entry entries[]
        {
            { "browser_folder_svg", Branding::browserFolderSourceColour, "folder" },
            { "browser_file_svg",   Branding::browserFileSourceColour,   "file" },
        };

        // **小さすぎると見比べられません**（56pxで出したら、
        // ライトの2つの違いが分かりませんでした）
        constexpr int cell = 120;
        constexpr int labelWidth = 76;
        constexpr int headerHeight = 34;

        juce::Image sheet (juce::Image::ARGB, labelWidth + cell * 4, headerHeight + cell * 2, true);

        {
            juce::Graphics g (sheet);

            for (int themeIndex = 0; themeIndex < 2; ++themeIndex)
            {
                const bool dark = themeIndex == 1;

                AppColours::setTheme (dark ? AppColours::Theme::Dark : AppColours::Theme::Light);

                const int x0 = labelWidth + themeIndex * cell * 2;

                // **その地の上に描くこと**（白の上では判断できません）
                g.setColour (AppColours::background);
                g.fillRect (x0, 0, cell * 2, sheet.getHeight());

                g.setColour (AppColours::textSecondary);
                g.setFont (juce::Font (juce::FontOptions (14.0f)));
                g.drawText (dark ? "dark" : "light", x0, 3, cell * 2, 16,
                             juce::Justification::centred, false);
                g.setFont (juce::Font (juce::FontOptions (12.0f)));
                g.drawText ("now", x0, 19, cell, 13, juce::Justification::centred, false);
                g.drawText ("accent", x0 + cell, 19, cell, 13, juce::Justification::centred, false);

                int row = 0;

                for (const auto& entry : entries)
                {
                    const bool isFolder = juce::String (entry.resource) == "browser_folder_svg";
                    const juce::uint32 accent = isFolder ? Branding::browserFolderColour (dark)
                                                          : Branding::browserFileColour (dark);

                    int column = 0;

                    for (const juce::uint32 wanted : { entry.source, accent })
                    {
                        auto icon = IconAssets::load (entry.resource);

                        if (icon == nullptr)
                        {
                            problem (juce::String (entry.name) + ": the picture did not load");
                            continue;
                        }

                        // **戻り値を見ること**（上の説明）
                        if (! icon->replaceColour (juce::Colour (entry.source),
                                                    juce::Colour (wanted)))
                        {
                            problem (juce::String (entry.name) + ": "
                                       + juce::String::toHexString ((int) entry.source)
                                       + " is not in the picture any more"
                                         " - the brand colour would be ignored");
                        }

                        icon->drawWithin (g,
                                           juce::Rectangle<float> ((float) (x0 + column * cell) + 12.0f,
                                                                    (float) (headerHeight + row * cell) + 12.0f,
                                                                    (float) cell - 24.0f,
                                                                    (float) cell - 24.0f),
                                           juce::RectanglePlacement::centred, 1.0f);
                        ++column;
                    }

                    ++row;
                }
            }

            // 行の名前（左端）
            AppColours::setTheme (AppColours::Theme::Light);

            g.setColour (AppColours::background);
            g.fillRect (0, 0, labelWidth, sheet.getHeight());

            g.setColour (AppColours::textPrimary);
            g.setFont (juce::Font (juce::FontOptions (14.0f)));

            int row = 0;

            for (const auto& entry : entries)
            {
                g.drawText (entry.name, 6, headerHeight + row * cell, labelWidth - 10, cell,
                             juce::Justification::centredLeft, false);
                ++row;
            }
        }

        if (problems == 0)
            say ("  ok    both pictures take the colour they are given");

        say ("  (folder: now " + juce::String::toHexString ((int) Branding::browserFolderSourceColour)
               + " -> accent " + juce::String::toHexString ((int) Branding::browserFolderColour (false))
               + " / " + juce::String::toHexString ((int) Branding::browserFolderColour (true)) + ")");
        say ("  (file:   now " + juce::String::toHexString ((int) Branding::browserFileSourceColour)
               + " -> accent " + juce::String::toHexString ((int) Branding::browserFileColour (false))
               + " / " + juce::String::toHexString ((int) Branding::browserFileColour (true)) + ")");

        const auto file = outputFolder.getChildFile ("browser_icons.png");

        file.deleteFile();

        juce::PNGImageFormat png;

        if (auto stream = file.createOutputStream())
            png.writeImageToStream (sheet, *stream);

        say ("  (a picture of it: " + file.getFullPathName() + ")");
    }

    //==========================================================================
    // 8.292：**スケール判定の試験**（Phase 285／本人の仕様書18章）。
    //
    // 仕様書に**期待値まで書いてある**ので、そのまま機械にやらせます。
    //
    // | 入れるもの | 期待するもの |
    // |---|---|
    // | Cメジャーの7音 | Major=C／Minor=A／**メジャー優勢** |
    // | 半音階12音 | どこにも寄らない（一致率が拮抗する） |
    // | 2音だけ | **判定中**（情報不足） |
    // | Aナチュラルマイナー＋Aを多め | **マイナー優勢** |
    //
    // **プラグインを丸ごと通します**（判定だけを呼ぶのではなく、MIDIを入れて
    // 画面へ渡る写しを読む）——途中の配線が外れていても、ここで出ます。

    /** 用意した音を順に弾く（**1音ずつ、鳴らして離す**）。 */
    void feedNotes (KakapoProcessor& processor, const std::vector<int>& notes)
    {
        constexpr int blockSize = 512;
        constexpr int blocksPerNote = 8;   // 約85ms

        juce::AudioBuffer<float> block (2, blockSize);

        for (const int note : notes)
        {
            for (int i = 0; i < blocksPerNote; ++i)
            {
                juce::MidiBuffer midi;

                if (i == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
                else if (i == blocksPerNote - 1)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), blockSize - 1);

                block.clear();
                processor.processBlock (block, midi);
            }
        }
    }

    KakapoProcessor::AnalysisSnapshot playNotes (const std::vector<int>& notes)
    {
        KakapoProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        feedNotes (processor, notes);

        return processor.readSnapshot();
    }

    void runScaleCheck()
    {
        say ("--- Kakapo: the scale detector (spec 18) ---");

        auto describe = [] (const KakapoProcessor::AnalysisSnapshot& snapshot)
        {
            if (! snapshot.result.hasEnoughNotes)
                return juce::String ("listening (not enough notes)");

            juce::String line;

            line << kakapo::pitchClassName (snapshot.result.major.root) << " major "
                 << juce::String (juce::roundToInt (snapshot.result.major.matchRatio * 100.0f)) << "%"
                 << "   " << kakapo::pitchClassName (snapshot.result.minor.root) << " minor "
                 << juce::String (juce::roundToInt (snapshot.result.minor.matchRatio * 100.0f)) << "%"
                 << "   centre " << kakapo::pitchClassName (snapshot.result.tonalCentre)
                 << "   favours " << (snapshot.result.majorFavoured ? "major" : "minor");

            return line;
        };

        //----------------------------------------------------------------------
        // ① Cメジャー（C D E F G A B）
        {
            const auto snapshot = playNotes ({ 60, 62, 64, 65, 67, 69, 71 });

            say ("  C major scale   " + describe (snapshot));

            if (! snapshot.result.hasEnoughNotes)
                problem ("C major scale: the plugin says there are not enough notes");

            if (snapshot.result.major.root != 0)
                problem ("C major scale: the major candidate is "
                           + juce::String (kakapo::pitchClassName (snapshot.result.major.root))
                           + " (expected C)");

            if (snapshot.result.minor.root != 9)
                problem ("C major scale: the minor candidate is "
                           + juce::String (kakapo::pitchClassName (snapshot.result.minor.root))
                           + " (expected A)");

            if (snapshot.result.major.matchRatio < 0.999f)
                problem ("C major scale: the major match is only "
                           + juce::String (snapshot.result.major.matchRatio, 3));

            // **並行調です**——点数では差が付かないので、トーナルセンターで決めます
            if (! kakapo::isRelativeKey (snapshot.result.major, snapshot.result.minor))
                problem ("C major scale: C major and A minor should be relative keys");

            if (! snapshot.result.majorFavoured)
                problem ("C major scale: major should be favoured (C is played first and longest)");
        }

        //----------------------------------------------------------------------
        // ② 半音階（どこにも寄らないこと）
        {
            const auto snapshot = playNotes ({ 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71 });

            say ("  chromatic       " + describe (snapshot));

            const float gap = std::abs (snapshot.result.major.matchRatio
                                          - snapshot.result.minor.matchRatio);

            if (gap > 0.05f)
                problem ("chromatic: one candidate won by " + juce::String (gap, 3)
                           + " - all twelve notes should leave them level");

            // 7/12 ＝ 0.583。**満点にならないこと**が肝心です
            if (snapshot.result.major.matchRatio > 0.65f)
                problem ("chromatic: the major match is "
                           + juce::String (snapshot.result.major.matchRatio, 3)
                           + " - twelve notes cannot fit a seven note scale");
        }

        //----------------------------------------------------------------------
        // ③ 2音だけ（情報不足）
        {
            const auto snapshot = playNotes ({ 60, 64 });

            say ("  two notes       " + describe (snapshot));

            if (snapshot.result.hasEnoughNotes)
                problem ("two notes: the plugin should say it is still listening");
        }

        //----------------------------------------------------------------------
        // ④ Aナチュラルマイナー、Aを多めに（マイナー優勢）
        {
            const auto snapshot = playNotes ({ 69, 71, 72, 74, 76, 77, 79, 69, 69, 69 });

            say ("  A minor (A led) " + describe (snapshot));

            if (snapshot.result.minor.root != 9)
                problem ("A minor: the minor candidate is "
                           + juce::String (kakapo::pitchClassName (snapshot.result.minor.root))
                           + " (expected A)");

            if (snapshot.result.tonalCentre != 9)
                problem ("A minor: the strongest note is "
                           + juce::String (kakapo::pitchClassName (snapshot.result.tonalCentre))
                           + " (expected A)");

            if (snapshot.result.majorFavoured)
                problem ("A minor: minor should be favoured when A is played most");
        }

        //----------------------------------------------------------------------
        // ⑤ **消えること**（画面のRESET）。仕様書18章の手動確認のぶん
        {
            constexpr double sampleRate = 48000.0;
            constexpr int blockSize = 512;

            KakapoProcessor processor;
            processor.prepareToPlay (sampleRate, blockSize);

            juce::AudioBuffer<float> block (2, blockSize);

            for (const int note : { 60, 62, 64, 65 })
            {
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
                midi.addEvent (juce::MidiMessage::noteOff (1, note), blockSize - 1);

                block.clear();
                processor.processBlock (block, midi);
            }

            if (processor.readSnapshot().noteCount != 4)
                problem ("reset: four notes should be in the buffer before the reset");

            processor.requestReset();

            juce::MidiBuffer empty;
            block.clear();
            processor.processBlock (block, empty);

            const auto after = processor.readSnapshot();

            if (after.noteCount == 0 && ! after.result.hasEnoughNotes)
                say ("  ok    reset clears the buffer and the verdict");
            else
                problem ("reset: the buffer still holds " + juce::String (after.noteCount)
                           + " note(s)");
        }

        //----------------------------------------------------------------------
        // ⑥ **音が出ること**・**音程**・**0dBFSを越えないこと**（内蔵リード）
        //
        // **音程はゼロ交差で数えます**（`hzByZeroCrossings`の説明）。
        // 内蔵リードは減衰しないので、自己相関では**周期の何倍のところも同点**になり、
        // 440Hzが「40Hz」と出ました（11周期ぶんのラグが選ばれていました）。
        {
            const auto samples = renderNoteSamples<KakapoProcessor> (69, 1.0);
            const auto result = measure (samples, 48000.0);

            reportNote ("kakapo A4", result, 0.0f);

            const float hz = hzByZeroCrossings (samples, 48000.0, 0.1, 0.9);
            const float cents = 1200.0f * std::log2 (juce::jmax (1.0f, hz) / 440.0f);

            say ("  kakapo A4 pitch  hz=" + juce::String (hz, 2)
                   + " (expected 440.00, " + juce::String (cents, 1) + " cent)");

            if (std::abs (cents) > 20.0f)
                problem ("kakapo: the lead voice is off by " + juce::String (cents, 1) + " cent");

            if (result.peak < 0.02f)
                problem ("kakapo: the lead voice made no sound");
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
        snapshots<OrangutanDrumsProcessor> ("orangutan_drums");
        snapshots<KakapoProcessor> ("kakapo");

        // 8.292：**Kakapoは、何か弾いたところも撮ります**（Phase 285）。
        // 既定の状態は「まだ聞いています」なので、**判定が出ている姿は誰も見ません**
        // ——`Orangutan Drums`で頁を撮り忘れたのと同じ話（8.290）
        for (const auto theme : { AppColours::Theme::Light, AppColours::Theme::Dark })
        {
            AppColours::setTheme (theme);

            KakapoProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            // Cメジャー（Cを多めに弾いて、メジャー優勢にする）
            feedNotes (processor, { 60, 62, 64, 65, 67, 69, 71, 72, 60, 67, 64, 60 });

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

            if (editor == nullptr)
            {
                problem ("kakapo_playing: createEditor() returned nothing");
                continue;
            }

            editor->setBounds (0, 0, editor->getWidth(), editor->getHeight());
            editor->resized();

            // **音を先に入れておくこと。** 画面は作られたときに1度だけ
            // 写しを読みます（`KakapoEditor`のコンストラクタが`timerCallback()`を呼ぶ）
            // ——先に開いてから弾いても、撮れるのは「まだ聞いています」の姿です

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false);
            const auto file = outputFolder.getChildFile (
                juce::String ("kakapo_playing_")
                  + (theme == AppColours::Theme::Light ? "light" : "dark") + ".png");

            file.deleteFile();

            juce::PNGImageFormat png;

            if (auto stream = file.createOutputStream())
                png.writeImageToStream (image, *stream);

            say ("  " + file.getFileName() + "  " + juce::String (image.getWidth())
                   + "x" + juce::String (image.getHeight()));
        }

        // 8.289：**ドラムは頁が2枚あります**（Phase 282）。
        // 既定で作ると1枚目しか撮れないので、`<UI>`へ頁を書いてから開きます
        // ——**撮っていない頁は、誰も見ていない頁**です
        for (const auto theme : { AppColours::Theme::Light, AppColours::Theme::Dark })
        {
            AppColours::setTheme (theme);

            OrangutanDrumsProcessor processor;
            processor.prepareToPlay (48000.0, 512);
            processor.getUiState().setProperty ("page", 1, nullptr);

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

            if (editor == nullptr)
            {
                problem ("orangutan_drums_knobs: createEditor() returned nothing");
                continue;
            }

            editor->setBounds (0, 0, editor->getWidth(), editor->getHeight());
            editor->resized();

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false);
            const auto file = outputFolder.getChildFile (
                juce::String ("orangutan_drums_knobs_")
                  + (theme == AppColours::Theme::Light ? "light" : "dark") + ".png");

            file.deleteFile();

            juce::PNGImageFormat png;

            if (auto stream = file.createOutputStream())
                png.writeImageToStream (image, *stream);

            say ("  " + file.getFileName() + "  " + juce::String (image.getWidth())
                   + "x" + juce::String (image.getHeight()));
        }
    }

    //==========================================================================
    // 8.288：**表と、プラグインが名乗る説明が合っているか**（Phase 281）
    //
    // `fillInPluginDescription()`は`MantaPlugins::findDescription()`を通します
    // （1.27）。**識別子を1文字打ち間違えると、そこで何も入りません**——
    // 説明が空のまま挿さり、**保存して開き直したときに初めて**見失います。
    //
    // 表を全部作って、名乗りが表と一致するかを数えます。
    // **増やしたときに、ここが自動で増えます**（表を回しているので）。

    void runTableCheck()
    {
        say ("--- the plugin table ---");

        for (const auto& entry : MantaPlugins::getEntries())
        {
            auto instance = entry.create();

            if (instance == nullptr)
            {
                problem (juce::String (entry.name) + ": create() returned nothing");
                continue;
            }

            juce::PluginDescription description;
            instance->fillInPluginDescription (description);

            if (description.fileOrIdentifier != entry.identifier)
            {
                problem (juce::String (entry.name) + ": says it is \""
                           + description.fileOrIdentifier + "\", the table says \""
                           + entry.identifier + "\"");
                continue;
            }

            if (description.name != entry.name)
            {
                problem (juce::String (entry.name) + ": the name does not match the table ("
                           + description.name + ")");
                continue;
            }

            say ("  ok    " + juce::String (entry.identifier).paddedRight (' ', 14)
                   + description.name + "   [" + description.createIdentifierString() + "]");
        }
    }

    void runPresetCheck()
    {
        runTableCheck();

        say ("--- factory presets ---");

        checkPresets<MantaEQProcessor> ("Manta EQ", MantaEQPresets::all());
        checkPresets<MantaCompProcessor> ("Manta Comp", MantaCompPresets::all());
        checkPresets<MantaDelayProcessor> ("Manta Delay", MantaDelayPresets::all());
        checkPresets<MantaReverbProcessor> ("Manta Reverb", MantaReverbPresets::all());
        checkPresets<RaccoGuitarProcessor> ("Racco Guitar", RaccoGuitarPresets::all());
        checkPresets<JavaRhinoBassProcessor> ("Java Rhino Bass", JavaRhinoBassPresets::all());
        checkPresets<OrangutanDrumsProcessor> ("Orangutan Drums", OrangutanDrumsPresets::all());

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

        //----------------------------------------------------------------------
        // 8.288：**Orangutan Drums**（Phase 281。8.289で作り直し）。
        //
        // どれも**聞いているだけでは数えられない**ものです。
        //
        // | | |
        // |---|---|
        // | 16のエンジン | **鳴っていること**と、**0dBFSを越えないこと** |
        // | パラアウト | `DIRECT`のパッドが**バスへ出て、メインには出ない**こと |
        // | チョーク | クローズハットが**オープンハットを止める**こと |
        // | 保存 | **開き直すと、パッドと選んでいるパッドが戻る**こと |

        say ("  --- Orangutan Drums ---");

        {
            using namespace OrangutanDrumsParams;

            // ① 16のエンジンを、パッド1へ順に割り当てて鳴らす
            auto setEngine = [] (int engine)
            {
                return [engine] (juce::AudioProcessorValueTreeState& apvts)
                {
                    if (auto* parameter = apvts.getParameter (padId (0, padEngine)))
                        parameter->setValueNotifyingHost (
                            parameter->convertTo0to1 ((float) engine));
                };
            };

            for (int engine = 0; engine < orangutan::ENG_COUNT; ++engine)
            {
                const auto result = renderNote<OrangutanDrumsProcessor> (
                    OrangutanDrumsProcessor::padBaseNote, 2.0, setEngine (engine));

                const juce::String name = juce::String ("drum ") + orangutan::engineName (engine);

                reportNote (name, result, 0.0f);

                // **鳴っていないエンジンを見つける**（つまみの既定値しだいで消えることがあります）
                if (result.peak < 0.02f)
                    problem (name + ": nothing came out (peak "
                               + juce::String (result.peak, 4) + ")");
            }

            // ② 8.289：**ベースゾーンは廃止**（Phase 282／本人の指定）。
            //    36〜51の外は、もう何も鳴りません——**前は52〜96が鳴っていました**
            reportNote ("drum note 60 (silent)",
                         renderNote<OrangutanDrumsProcessor> (60, 0.5), 0.0f);

            // ③ 8.289：**パラアウト**（Phase 282／本人の要望で復活）。
            //
            // **本体と同じ順でやること**（8.144）：欲しい形を全部組んで、
            // 確かめて、**グラフへ入れる前に1回だけ**渡す。ここではグラフが無いので
            // `prepareToPlay()`の前に渡します。
            //
            // 見るのは**行き先**です——`DIRECT`のパッドは**バスへ出て、メインには出ない**。
            // 音が鳴っているかだけ見ると、**メインへ落ちていても気づけません**
            // （このプラグインは、バスが無効なときMAINへ落とすので）。
            {
                constexpr double sampleRate = 48000.0;
                constexpr int blockSize = 512;

                auto renderWithDirectOut = [] (bool direct, float& mainPeak, float& busPeak)
                {
                    OrangutanDrumsProcessor processor;

                    auto layout = processor.getBusesLayout();

                    // **本体は16本まとめて有効にします**（`enableExtraOutputBusesOn()`）。
                    // 1本だけで試すと、**34chの形**を一度も通らずに合格します
                    for (int bus = 1; bus < layout.outputBuses.size(); ++bus)
                        layout.outputBuses.getReference (bus) = juce::AudioChannelSet::stereo();

                    const bool accepted = processor.checkBusesLayoutSupported (layout)
                                            && processor.setBusesLayout (layout);

                    processor.prepareToPlay (sampleRate, blockSize);

                    if (auto* parameter = processor.getValueTreeState()
                                                     .getParameter (padId (0, padOut)))
                        parameter->setValueNotifyingHost (
                            parameter->convertTo0to1 (direct ? 1.0f : 0.0f));

                    const int channels = processor.getTotalNumOutputChannels();

                    juce::AudioBuffer<float> block (juce::jmax (2, channels), blockSize);

                    mainPeak = 0.0f;
                    busPeak = 0.0f;

                    for (int position = 0; position < (int) (0.5 * sampleRate); position += blockSize)
                    {
                        juce::MidiBuffer midi;

                        if (position == 0)
                            midi.addEvent (juce::MidiMessage::noteOn (
                                1, OrangutanDrumsProcessor::padBaseNote, 0.9f), 1);

                        block.clear();
                        processor.processBlock (block, midi);

                        for (int i = 0; i < blockSize; ++i)
                        {
                            mainPeak = juce::jmax (mainPeak, std::abs (block.getSample (0, i)));

                            if (block.getNumChannels() > 2)
                                busPeak = juce::jmax (busPeak, std::abs (block.getSample (2, i)));
                        }
                    }

                    return accepted;
                };

                float mainPeak = 0.0f, busPeak = 0.0f;

                if (! renderWithDirectOut (false, mainPeak, busPeak))
                    problem ("drums: the plugin refused a layout with one direct out enabled");

                say ("  drum out MAIN     main=" + juce::String (mainPeak, 4)
                       + " bus1=" + juce::String (busPeak, 4));

                if (mainPeak < 0.02f)
                    problem ("drums: pad 1 on MAIN made no sound on the main bus");

                if (busPeak > 0.001f)
                    problem ("drums: pad 1 on MAIN leaked into the direct out bus");

                renderWithDirectOut (true, mainPeak, busPeak);

                say ("  drum out DIRECT   main=" + juce::String (mainPeak, 4)
                       + " bus1=" + juce::String (busPeak, 4));

                if (busPeak < 0.02f)
                    problem ("drums: pad 1 on DIRECT made no sound on its own bus");

                if (mainPeak > 0.001f)
                    problem ("drums: pad 1 on DIRECT is still coming out of the main bus");
            }

            // ④ **保存して開き直す。** パラメータだけでなく、画面が`<UI>`へ入れる
            //    「選んでいるパッド」と「開いている頁」も戻ること（`OrangutanDrumsEditor`）
            {
                OrangutanDrumsProcessor saved;
                saved.prepareToPlay (48000.0, 512);

                auto& savedState = saved.getValueTreeState();

                // パッド4のエンジンを`COWBELL`（10）、TUNEを+7、出口をDIRECTへ
                if (auto* parameter = savedState.getParameter (padId (3, padEngine)))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (10.0f));

                if (auto* parameter = savedState.getParameter (padId (3, padTune)))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (7.0f));

                if (auto* parameter = savedState.getParameter (padId (3, padOut)))
                    parameter->setValueNotifyingHost (parameter->convertTo0to1 (1.0f));

                saved.getUiState().setProperty ("selectedPad", 3, nullptr);
                saved.getUiState().setProperty ("page", 1, nullptr);

                juce::MemoryBlock block;
                saved.getStateInformation (block);

                OrangutanDrumsProcessor restored;
                restored.prepareToPlay (48000.0, 512);
                restored.setStateInformation (block.getData(), (int) block.getSize());

                const int engine = restored.getPadEngine (3);
                const float tuneValue = restored.getValueTreeState()
                                                  .getRawParameterValue (padId (3, padTune))->load();
                const bool direct = restored.isPadDirectOut (3);
                const int pad = (int) restored.getUiState().getProperty ("selectedPad", -1);
                const int page = (int) restored.getUiState().getProperty ("page", -1);

                if (engine == 10 && std::abs (tuneValue - 7.0f) < 0.01f
                     && direct && pad == 3 && page == 1)
                    say ("  ok    saving and reopening keeps the pads, the direct out and the page");
                else
                    problem ("after reopening: engine " + juce::String (engine)
                               + " (expected 10), tune " + juce::String (tuneValue, 2)
                               + " (expected 7.00), direct " + juce::String (direct ? 1 : 0)
                               + " (expected 1), selected pad " + juce::String (pad)
                               + " (expected 3), page " + juce::String (page) + " (expected 1)");
            }

            // ⑤ チョーク。**オープンハット（パッド6）を鳴らしてから、
            //    クローズハット（パッド5）を叩いて、尻尾が消えるか**
            auto renderHats = [] (bool withChoke)
            {
                constexpr double sampleRate = 48000.0;
                constexpr int blockSize = 512;

                OrangutanDrumsProcessor processor;
                processor.prepareToPlay (sampleRate, blockSize);

                juce::AudioBuffer<float> block (2, blockSize);

                const int totalSamples = (int) (0.5 * sampleRate);
                const int chokeAt = (int) (0.1 * sampleRate);

                std::vector<float> captured;
                captured.reserve ((size_t) totalSamples);

                int position = 0;

                while (position < totalSamples)
                {
                    juce::MidiBuffer midi;

                    if (position == 0)
                        midi.addEvent (juce::MidiMessage::noteOn (
                            1, OrangutanDrumsProcessor::padBaseNote + 5, 0.9f), 1);

                    if (withChoke && position <= chokeAt && chokeAt < position + blockSize)
                        midi.addEvent (juce::MidiMessage::noteOn (
                            1, OrangutanDrumsProcessor::padBaseNote + 4, 0.9f),
                            chokeAt - position);

                    block.clear();
                    processor.processBlock (block, midi);

                    for (int i = 0; i < blockSize && position + i < totalSamples; ++i)
                        captured.push_back (block.getSample (0, i));

                    position += blockSize;
                }

                return measure (captured, sampleRate);
            };

            const auto openOnly = renderHats (false);
            const auto choked   = renderHats (true);

            say ("  open hat tail=" + juce::String (openOnly.tailRms, 5)
                   + "   after a closed hat tail=" + juce::String (choked.tailRms, 5));

            // **尻尾で見ること**（頭はクローズハット自身の音で大きくなります）
            if (choked.tailRms < openOnly.tailRms * 0.5f)
                say ("  ok    the closed hat chokes the open one");
            else
                problem ("the closed hat did not choke the open one (tail "
                           + juce::String (choked.tailRms, 5) + " vs "
                           + juce::String (openOnly.tailRms, 5) + ")");
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
        const bool wantIcons     = all || args.containsOption ("--icons");
        const bool wantScale     = all || args.containsOption ("--scale");

        outputFolder = args.size() > 0 && args[args.size() - 1].isLongOption() == false
                         ? juce::File::getCurrentWorkingDirectory()
                                .getChildFile (args[args.size() - 1].text.unquoted())
                         : juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                .getParentDirectory().getChildFile ("preview");

        outputFolder.createDirectory();

        say ("PluginPreview -> " + outputFolder.getFullPathName());

        if (wantSnapshots) runSnapshots();
        if (wantEqBand) runEqBandCheck();
        if (wantIcons) runIconCheck();
        if (wantScale) runScaleCheck();
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
