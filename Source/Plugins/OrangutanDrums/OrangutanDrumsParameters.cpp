#include "OrangutanDrumsParameters.h"

#include "OrangutanDrumsDSP.h"
#include "OrangutanDrumsKits.h"

namespace OrangutanDrumsParams
{
    juce::StringArray getEngineNames()
    {
        juce::StringArray names;

        for (int engine = 0; engine < orangutan::ENG_COUNT; ++engine)
            names.add (orangutan::engineName (engine));

        return names;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using Float  = juce::AudioParameterFloat;
        using Choice = juce::AudioParameterChoice;
        using Range  = juce::NormalisableRange<float>;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto engineNames = getEngineNames();

        //----------------------------------------------------------------------
        // パッド16個 × 8つ
        //
        // **既定値は1つめの工場キット**（`808 CLASSIC`）。挿した直後に鳴るのがこれです。
        // 表を直に読むので、キットを直せば既定値も一緒に動きます（1.27）
        for (int pad = 0; pad < numPads; ++pad)
        {
            const auto& defaults = OrangutanDrumsKits::kits[0].pads[pad];

            // ホストのオートメーション欄に**137個**並ぶので、
            // 頭に`P01 `を付けて、どのパッドのものか見分けが付くようにします
            const juce::String tag = "P" + juce::String (pad + 1).paddedLeft ('0', 2) + " ";

            layout.add (std::make_unique<Choice> (
                juce::ParameterID { padId (pad, padEngine), 1 }, tag + "Engine",
                engineNames, defaults.engine));

            layout.add (std::make_unique<Float> (
                juce::ParameterID { padId (pad, padTune), 1 }, tag + "Tune",
                Range (-24.0f, 24.0f, 0.01f), defaults.tune,
                juce::AudioParameterFloatAttributes().withLabel ("st")));

            layout.add (std::make_unique<Float> (
                juce::ParameterID { padId (pad, padDecay), 1 }, tag + "Decay",
                Range (0.0f, 1.0f, 0.001f), defaults.decay));

            layout.add (std::make_unique<Float> (
                juce::ParameterID { padId (pad, padTone), 1 }, tag + "Tone",
                Range (0.0f, 1.0f, 0.001f), defaults.tone));

            layout.add (std::make_unique<Float> (
                juce::ParameterID { padId (pad, padSnap), 1 }, tag + "Snap",
                Range (0.0f, 1.0f, 0.001f), defaults.snap));

            layout.add (std::make_unique<Float> (
                juce::ParameterID { padId (pad, padLevel), 1 }, tag + "Level",
                Range (0.0f, 1.5f, 0.001f), defaults.level));

            layout.add (std::make_unique<Float> (
                juce::ParameterID { padId (pad, padPan), 1 }, tag + "Pan",
                Range (-1.0f, 1.0f, 0.001f), defaults.pan));

            layout.add (std::make_unique<Float> (
                juce::ParameterID { padId (pad, padSend), 1 }, tag + "Send",
                Range (0.0f, 1.0f, 0.001f), defaults.send));

            // 8.289：**このパッドの出口**（Phase 282／本人の要望で復活）。
            //
            // `DIRECT`にすると、そのパッドは**マスター段を通らずに**専用のバスへ出ます
            // （本体のドラムアウトトラックで受けます。8.144）。
            // **バスが無効なときはMAINへ落とします**——受け皿を作る前に切り替えても
            // 音が消えないように（消えるより分かりやすい）。
            layout.add (std::make_unique<Choice> (
                juce::ParameterID { padId (pad, padOut), 1 }, tag + "Output",
                juce::StringArray { "MAIN", "DIRECT" }, 0));
        }

        //----------------------------------------------------------------------
        // マスター
        const auto& masterDefaults = OrangutanDrumsKits::kits[0];

        layout.add (std::make_unique<Float> (
            juce::ParameterID { drive, 1 }, "Drive",
            Range (0.0f, 1.0f, 0.001f), masterDefaults.drive));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { glue, 1 }, "Glue",
            Range (0.0f, 1.0f, 0.001f), masterDefaults.glue));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { reverb, 1 }, "Reverb",
            Range (0.0f, 1.0f, 0.001f), masterDefaults.reverb));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { size, 1 }, "Size",
            Range (0.0f, 1.0f, 0.001f), masterDefaults.size));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { damp, 1 }, "Damp",
            Range (0.0f, 1.0f, 0.001f), masterDefaults.damp));

        // **既定は0.80**（`MAGAZINE`は0.90）。
        //
        // 出口にソフトリミッタがあるので、ここを上げても**約0.99で頭打ち**になります
        // ——つまり0.90だと「常にリミッタに当たった音」が既定になります。
        // トラックに挿して使うものなので、**当たらないところ**から始めます
        // （`Java Rhino Bass`で0.30にしたのと同じ考え。8.269）。
        layout.add (std::make_unique<Float> (
            juce::ParameterID { volume, 1 }, "Volume",
            Range (0.0f, 1.5f, 0.001f), 0.80f));

        return layout;
    }
}
