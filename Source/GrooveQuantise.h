#pragma once

#include "ProjectModel.h"

//==============================================================================
/**
    仕様書5.3.4「グルーヴクオンタイズ」の計算部分。

    基準クリップから「グリッドからのタイミング・ベロシティのズレ」を抜き出して
    グルーヴテンプレートにし、他のクリップへ**強さ（0〜100%）**を指定して適用する。

    **ここには計算だけを置き、モデルへの書き込みとUndoの区切りは呼び出し側が行う。**
    既存のグリッドクオンタイズ（`PianoRollComponent::quantiseNotes()`）と同じ形に
    揃えることで、「どちらの経路を通ってもノートの扱いが同じ」と言い切れるようにしている。
*/
namespace GrooveQuantise
{
    /** グリッド1マスぶんの秒数を求める。

        8.135：**もう計算には使いません**（Phase 173／8.105の宿題2）。
        抽出も適用も**拍で数える**ようになったので、秒は出てきません。
        表示や外からの問い合わせのために残してあります。 */
    double getGridSeconds (double tempo, int gridDivision);

    /** 8.135：時刻を「グリッドのマス番号（小数）」へ（Phase 173／8.105の宿題2）。

        **拍で数えます。** 秒で割ると、曲の途中でテンポが変わったときに
        後ろへ行くほどマス目からずれます（8.98で挙げた「時間で変わるもの」）。 */
    double getGridPositionAt (const ProjectModel& project, double timeSeconds, int gridDivision);

    /** 仕様書5.3.4：基準クリップからテンプレートを抽出してプロジェクトへ登録する。

        patternLengthはパターン1周のマス数（1小節ぶんなら「拍数 × 分割数」）。
        同じマスに複数のノートがある場合は、ズレを**平均**して1つにまとめる
        （和音を弾いたクリップでも、代表的な揺れが取れるようにするため）。

        ノートが1つも無い場合は`state.isValid()`がfalseのテンプレートを返す。 */
    GrooveTemplate extractFromTrack (ProjectModel& project, const Track& track,
                                     int gridDivision, int patternLength,
                                     const juce::String& name);

    /** ノート1つぶんの適用結果。 */
    struct NoteResult
    {
        double startTime = 0.0;
        int velocity = 100;
    };

    /** 仕様書5.3.4：1つのノートへテンプレートを適用した結果を求める。

        strengthは0.0〜1.0で、**テンプレートへどの程度寄せるか**。
        0.0なら元のまま、1.0ならテンプレートの位置・強さにぴったり合わせる。

        8.135：**テンポを渡しません**（Phase 173／8.105の宿題2）。
        ズレが「マス何個ぶん」になったので、**当てる側はテンポを見ません**。
        曲の途中でテンポが変わっても、そのマスのテンポで自動的に正しくなります。 */
    NoteResult applyToNote (const ProjectModel& project,
                             const GrooveTemplate& grooveTemplate,
                             double noteStartTime, int noteVelocity,
                             int targetGridDivision, double strength);
}
