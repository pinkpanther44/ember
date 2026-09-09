#pragma once

#include "ProjectIds.h"

//==============================================================================
/**
    仕様書5.6：オートメーションの書き込みモード。

    - **Read**  … 書かれているオートメーションに従う（既定）
    - **Touch** … つまみを動かしている**間だけ**記録し、離すとReadへ戻る
    - **Latch** … つまみに触れた時点から記録を始め、**停止するまで**記録し続ける
    - **Write** … 再生中はずっと記録する（既存のオートメーションを塗り潰す）
*/
enum class AutomationMode
{
    Read,
    Touch,
    Latch,
    Write
};

juce::String automationModeToString (AutomationMode mode);
AutomationMode automationModeFromString (const juce::String& text);
juce::String getAutomationModeDisplayName (AutomationMode mode);


//==============================================================================
/**
    仕様書5.6・設計書1.3：オートメーションの対象パラメータ。

    値は**0.0〜1.0に正規化して**保存する。こうしておくとレーンの描画・編集が
    パラメータの種類によらず同じコードで書け、将来プラグインパラメータ
    （JUCEも0〜1で扱う）を対象に加えるときもそのまま乗せられる。
    実際のdB値やパン値への変換は、この名前空間の関数が受け持つ。
*/
namespace AutomationTargets
{
    /** 対象の識別子。設計書1.4の`<LANE targetId="volume">`に入る値。 */
    extern const juce::String volume;
    extern const juce::String pan;

    //==========================================================================
    // 仕様書5.6：プラグインパラメータ（Phase 20）
    //
    // 識別子は`"insert:<スロット番号>:<パラメータ番号>"`、
    // 音源なら`"instrument:<パラメータ番号>"`という形にする。
    // レーンはトラック配下にあるので、どのトラックかは持たなくてよい。
    //
    // パラメータを**番号**で覚えているのは、JUCEの`AudioProcessorParameter`が
    // 安定したIDを必ずしも持たないため。プラグインを差し替えると番号の意味が
    // 変わり得るが、その場合は割り当て直す前提とする（多くのホストも同様）。

    /** insertIndexが負なら音源スロットを指す。 */
    juce::String makePluginTarget (int insertIndex, int parameterIndex);

    bool isPluginTarget (const juce::String& targetId);

    /** 識別子を分解する。読み取れなければfalse。 */
    bool parsePluginTarget (const juce::String& targetId, int& insertIndexOut, int& parameterIndexOut);

    /** フェーダーの範囲。**ミキサーのスライダーと必ず同じ値にすること**
        （ずれると、同じ値なのにUIとオートメーションで音量が食い違う）。 */
    constexpr float minVolumeDb = -60.0f;
    constexpr float maxVolumeDb = 6.0f;

    /** 正規化値（0〜1）→ そのパラメータの実際の値（volumeはdB、panは-1〜+1）。 */
    float toParameterValue (const juce::String& targetId, float normalised);

    /** 実際の値 → 正規化値。UIが現在値から点を作るときに使う。 */
    float fromParameterValue (const juce::String& targetId, float value);

    /** レーンの見出しに出す名前。 */
    juce::String getDisplayName (const juce::String& targetId);

    /** 値を人が読める形にする（レーンの表示用）。 */
    juce::String formatValue (const juce::String& targetId, float normalised);
}

//==============================================================================
/** 仕様書5.6・設計書1.3のcurveType：点から次の点までの繋ぎ方。 */
enum class AutomationCurve
{
    Linear, // 直線
    Ease,   // 曲線（S字。始点と終点で緩やかになる）
    Step    // ステップ（次の点まで値を保つ）
};

juce::String automationCurveToString (AutomationCurve curve);
AutomationCurve automationCurveFromString (const juce::String& text);

//==============================================================================
/** 設計書1.3のAutomationPointに対応する、ValueTreeの薄いラッパークラス。 */
class AutomationPoint
{
public:
    explicit AutomationPoint (juce::ValueTree treeToWrap);

    juce::ValueTree state;

    /** 曲の時刻（秒）。8.139：**保存されている拍から換算した値**です（Phase 177）。 */
    double getTime() const;
    void setTime (double newTimeSeconds, juce::UndoManager* undoManager);

    /** 8.139：**保存されているのはこちら**（Phase 177／8.105の宿題3）。
        `Note::getStartBeats()`と同じ扱いです（`MusicalTime.h`）。 */
    double getTimeBeats() const;
    void setTimeBeats (double newTimeBeats, juce::UndoManager* undoManager);

    /** 0.0〜1.0の正規化値（AutomationTargets参照）。 */
    float getValue() const;
    void setValue (float newValue, juce::UndoManager* undoManager);

    /** この点から**次の点まで**の繋ぎ方（仕様書5.6）。
        「点そのものの形」ではなく「そこから先の区間の形」を表す点に注意
        （多くのDAWと同じ考え方で、区間ごとに形を変えられる）。 */
    AutomationCurve getCurve() const;
    void setCurve (AutomationCurve newCurve, juce::UndoManager* undoManager);

    /** 仕様書5.6：この区間の**曲がり具合**（Phase 77／8.37）。

        **-1.0〜+1.0で、0が直線。** 線の途中に出る小さな丸を上下にドラッグすると変わる。
        - **正**（0より大きい）… はじめに大きく動いて、終わりが緩やか
        - **負**（0より小さい）… はじめは緩やかで、終わりに大きく動く

        **`Linear`のときだけ意味を持ちます。** S字とステップは形が決まっているので、
        曲がり具合を持たせても行き先が2通りになるだけ（ドラッグすると`Linear`へ変わる）。
        **保存されていないものは0**（Phase 76以前のプロジェクトは今までどおりの直線）。 */
    float getCurveAmount() const;
    void setCurveAmount (float newAmount, juce::UndoManager* undoManager);
};

/** 曲がり具合の範囲（Phase 77）。**これ以上曲げても線が潰れて見えないだけ**なので、
    ドラッグもメニューもこの範囲に収める。 */
constexpr float maxAutomationCurveAmount = 1.0f;

/** 2点の間を、指定のカーブで補間する。0.0〜1.0のtに対する係数を返す。
    エンジン側（TrackChannelProcessor）とモデル側で同じ形になるよう、ここに集約している。

    `amount`は曲がり具合（Phase 77）。**`Linear`のときだけ効きます。** */
float applyAutomationCurve (AutomationCurve curve, float t, float amount = 0.0f);

/** 線の途中を掴んで動かしたときの、**曲がり具合を逆算する**（Phase 77／8.37）。

    `t`は区間の中でのその場所（0〜1）、`shaped`は動かした先の高さ
    （0＝手前の点、1＝次の点）。`applyAutomationCurve()`の逆算なので、
    **式を2箇所に書かないためにここへ置いてある**（画面ごとに曲がり方が違う、を防ぐ）。 */
float automationCurveAmountFromDrag (float t, float shaped);

//==============================================================================
/**
    仕様書5.6・設計書1.3のAutomationLane。1つのパラメータの時間変化を持つ。

    点は**時刻の昇順で並んでいることを前提**にしている。追加時に正しい位置へ挿し、
    移動後は並べ替えることで保つ（`sortPoints()`）。順序が崩れると、
    値の補間（`getValueAt`）が意味を成さなくなる。

    カーブ種別（設計書1.3のcurveType）は保存だけしておき、現状は直線補間のみ。
*/
class AutomationLane
{
public:
    explicit AutomationLane (juce::ValueTree treeToWrap);

    juce::ValueTree state;

    /** どのパラメータを動かすレーンか（AutomationTargetsの識別子）。 */
    juce::String getTargetId() const;

    //==========================================================================
    // 8.56：アレンジ画面での見せ方（Phase 94／D3）。
    //
    // **「見せるかどうか」と「どのくらいの高さで」はレーン自身が持つ。**
    // Phase 93まではトラックが`automationView`で1つだけ選ぶ形だったので、
    // 2つのパラメータを並べて見ることができなかった。

    /** アレンジ画面に専用の行として出すか。 */
    bool isVisible() const;
    void setVisible (bool shouldBeVisible, juce::UndoManager* undoManager);

    /** 行の高さ（ピクセル）。入っていなければ`defaultLaneHeight`。 */
    int getRowHeight() const;
    void setRowHeight (int newHeight, juce::UndoManager* undoManager);

    /** 8.59：**このレーンを鳴らさない**（Phase 96）。

        **点は消さずに、効かせるのをやめる**（インサートのバイパスと同じ）。
        書いたものを残したまま「無し」と聴き比べられる。
        音の側（`AudioEngine`）とフェーダーの表示（8.54）の**両方が見ること**。 */
    bool isBypassed() const;
    void setBypassed (bool shouldBypass, juce::UndoManager* undoManager);

    /** 8.59：レーンの行の色（Phase 96）。

        **入っていなければ親トラックの色**。`getColourString()`は
        入っていなければ空文字を返すので、**呼ぶ側が親の色へ落とす**こと
        （`TimelineComponent::getAutomationRowColour()`が1箇所で面倒を見ている）。 */
    juce::String getColourString() const;
    bool hasCustomColour() const;
    void setColourString (const juce::String& argbHex, juce::UndoManager* undoManager);

    /** 親トラックの色へ戻す（プロパティごと消す）。 */
    void clearColour (juce::UndoManager* undoManager);

    /** レーンの行の既定の高さ。**トラック行より低くする**：
        オートメーションは1本の線なので、クリップほどの縦を必要としない。 */
    static constexpr int defaultLaneHeight = 46;
    static constexpr int minimumLaneHeight = 24;
    static constexpr int maximumLaneHeight = 240;

    int getNumPoints() const;
    AutomationPoint getPoint (int index) const;

    /** 点を追加する（時刻順の正しい位置へ挿す）。 */
    AutomationPoint addPoint (double timeSeconds, float value, juce::UndoManager* undoManager);

    /** 8.139：**拍で足す**（Phase 177）。`Track::addNoteBeats()`と同じ扱いで、
        切り離されたツリーから戻すときはこちらを使うこと（`MusicalTime.h`）。 */
    AutomationPoint addPointBeats (double timeBeats, float value, juce::UndoManager* undoManager);

    void removePoint (int index, juce::UndoManager* undoManager);

    /** 点を時刻順に並べ替える。点をドラッグして前後関係が入れ替わった後に呼ぶこと。 */
    void sortPoints (juce::UndoManager* undoManager);

    /** 指定した時間範囲（fromTime < t <= toTime）の点を消す。
        仕様書5.6の書き込みモードで、既存のオートメーションを上書きするのに使う。 */
    void removePointsInRange (double fromTime, double toTime, juce::UndoManager* undoManager);

    /** 仕様書5.6：書き込みモードでの記録。
        直前に書いた位置から今の位置までを消してから、現在値の点を置く。
        これで「なぞった区間だけが新しい値に置き換わる」挙動になる。 */
    void writeValue (double fromTime, double toTime, float value, juce::UndoManager* undoManager);

    /** 指定時刻の値を直線補間で求める。点が無ければfallbackを返す。
        最初の点より前は最初の点の値、最後の点より後は最後の点の値で伸ばす
        （多くのDAWと同じ挙動）。 */
    float getValueAt (double timeSeconds, float fallback) const;

    bool isEmpty() const { return getNumPoints() == 0; }
};
