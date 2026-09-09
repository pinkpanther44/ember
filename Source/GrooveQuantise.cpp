#include "GrooveQuantise.h"

#include <cmath>
#include <map>

namespace GrooveQuantise
{

double getGridSeconds (double tempo, int gridDivision)
{
    const double secondsPerBeat = 60.0 / juce::jmax (1.0, tempo);

    return secondsPerBeat / juce::jmax (1, gridDivision);
}

double getGridPositionAt (const ProjectModel& project, double timeSeconds, int gridDivision)
{
    // 8.135：**拍で数える**（Phase 173／8.105の宿題2）。
    //
    // Phase 172までは`秒 ÷ 1マスの秒数`でした。**1マスの秒数は曲の途中で変わる**ので
    // （テンポマップ。8.102）、割り算だと後ろへ行くほどマス目からずれます。
    // 拍の位置に分割数を掛ければ、テンポがどう変わっても正しいマス番号になります
    return project.getBeatPositionAt (timeSeconds) * (double) juce::jmax (1, gridDivision);
}

GrooveTemplate extractFromTrack (ProjectModel& project, const Track& track,
                                 int gridDivision, int patternLength,
                                 const juce::String& name)
{
    const int numNotes = track.getNumNotes();

    if (numNotes == 0)
        return GrooveTemplate (juce::ValueTree());

    const double tempo = project.getTempo();
    const int patternSlots = juce::jmax (1, patternLength);

    // ベロシティの倍率は「このトラックの平均」を基準にする。
    // 絶対値で持つと、適用先の音量感を丸ごと上書きしてしまう。
    double velocitySum = 0.0;

    for (int n = 0; n < numNotes; ++n)
        velocitySum += track.getNote (n).getVelocity();

    const double averageVelocity = juce::jmax (1.0, velocitySum / (double) numNotes);

    // 同じマスに複数のノートが乗ることがある（和音・重ね）。合計と個数を持って後で平均する。
    struct Accumulator
    {
        double offsetSum = 0.0;
        double velocitySum = 0.0;
        int count = 0;
    };

    std::map<int, Accumulator> slots;

    for (int n = 0; n < numNotes; ++n)
    {
        auto note = track.getNote (n);

        // 8.135：**マス番号も、ズレも、マスを単位にして測る**（Phase 173／8.105の宿題2）。
        // 秒はもう出てきません
        const double gridPosition = getGridPositionAt (project, note.getStartTime(), gridDivision);
        const double gridIndex = std::round (gridPosition);
        const double offsetGrids = gridPosition - gridIndex;

        // パターンは繰り返すので、マス番号は周回で畳む。
        // 負の値になり得るため（グリッドより手前のノート）、剰余を正へ寄せておく。
        int slot = (int) ((juce::int64) gridIndex % patternSlots);

        if (slot < 0)
            slot += patternSlots;

        auto& accumulator = slots[slot];
        accumulator.offsetSum += offsetGrids;
        accumulator.velocitySum += note.getVelocity() / averageVelocity;
        accumulator.count += 1;
    }

    auto grooveTemplate = project.addGrooveTemplate (name, gridDivision, patternSlots, tempo,
                                                      &project.getUndoManager());

    for (const auto& [slot, accumulator] : slots)
    {
        if (accumulator.count <= 0)
            continue;

        grooveTemplate.addPoint (slot,
                                  accumulator.offsetSum / accumulator.count,
                                  (float) (accumulator.velocitySum / accumulator.count),
                                  &project.getUndoManager());
    }

    return grooveTemplate;
}

NoteResult applyToNote (const ProjectModel& project,
                         const GrooveTemplate& grooveTemplate,
                         double noteStartTime, int noteVelocity,
                         int targetGridDivision, double strength)
{
    NoteResult result;
    result.startTime = noteStartTime;
    result.velocity = noteVelocity;

    if (! grooveTemplate.state.isValid() || grooveTemplate.isEmpty())
        return result;

    const double limitedStrength = juce::jlimit (0.0, 1.0, strength);
    const int patternSlots = grooveTemplate.getPatternLength();
    const int division = juce::jmax (1, targetGridDivision);

    // 8.135：**ここから先は全部マスの単位**（Phase 173／8.105の宿題2）。
    // テンポも、抽出時のテンポも、グリッドの秒数も出てきません
    const double gridPosition = getGridPositionAt (project, noteStartTime, division);
    const double gridIndex = std::round (gridPosition);

    int slot = (int) ((juce::int64) gridIndex % patternSlots);

    if (slot < 0)
        slot += patternSlots;

    auto point = grooveTemplate.findPoint (slot);

    // そのマスにデータが無い場合は、素のグリッドへ寄せる（＝通常のクオンタイズ）。
    // 何もしないより自然：テンプレートは「ここは前ノリ」という情報を持つが、
    // 記録が無いマスは「グリッドどおり」を意味するため。
    //
    // 8.135：**古いテンプレートの換算は`getPointOffsetGrids()`が引き受けます**。
    // ここでミリ秒を見ないこと——2箇所で換算すると必ず食い違います（1.27）
    const double offsetGrids = point.state.isValid()
                                 ? grooveTemplate.getPointOffsetGrids (point)
                                 : 0.0;

    // **マスの位置を秒へ戻すのは最後の1回だけ。** 拍へ直してから時刻を引くので、
    // 曲の途中でテンポが変わっていても、そのマスのテンポで正しい長さになります
    const double grooveBeats = (gridIndex + offsetGrids) / (double) division;
    const double grooveTime = project.getTimeForBeatPosition (grooveBeats);

    // 強さは「元の位置」と「テンプレートの位置」の間の補間。
    // 0%で元のまま、100%でテンプレートどおりになる（仕様書5.3.4）。
    result.startTime = juce::jmax (0.0, noteStartTime + (grooveTime - noteStartTime) * limitedStrength);

    if (point.state.isValid())
    {
        // ベロシティも同じ考え方で、等倍とテンプレートの倍率の間を補間する
        const double velocityScale = 1.0 + (point.getVelocityScale() - 1.0) * limitedStrength;

        result.velocity = juce::jlimit (1, 127, (int) std::lround (noteVelocity * velocityScale));
    }

    return result;
}

} // namespace GrooveQuantise
