#include "ProjectModel.h"
#include <juce_audio_processors/juce_audio_processors.h> // juce::PluginDescription（設計書3.8）
#include "Utf8.h"                                        // 日本語のアクション名に使う
#include "Branding.h"                                     // 8.186：1本目のトラックの色は製品ごとに違う（Phase 225）
// 8.147：半音の上限は`AudioTransform`が持つ（式と定数を2箇所に書かない。1.27）。
// **入れるのは.cppだけ**——ProjectModel.hへ入れると、データモデル層が
// juce_audio_formatsを引き込みます（MidiCCMessage.hと同じ理由。設計書1.1）
#include "AudioTransform.h"

#include <algorithm> // 仕様書5.3.2：チョークグループの並べ替え
#include <cmath>
#include <map>


//==============================================================================
juce::String trackTypeToString (TrackType type)
{
    switch (type)
    {
        case TrackType::Audio:  return "Audio";
        case TrackType::Midi:   return "Midi";
        case TrackType::Send:   return "Send";
        case TrackType::Folder: return "Folder";
        case TrackType::Chord:  return "Chord";
        case TrackType::VCA:    return "VCA";
        case TrackType::DrumOut: return "DrumOut";   // 8.143（Phase 181／改善案⑮）
    }

    jassertfalse; // 未知のTrackTypeが渡された
    return "Audio";
}

TrackType trackTypeFromString (const juce::String& text)
{
    if (text == "Midi")   return TrackType::Midi;
    if (text == "Send")   return TrackType::Send;
    if (text == "Folder") return TrackType::Folder;
    if (text == "Chord")  return TrackType::Chord;
    if (text == "VCA")    return TrackType::VCA;
    if (text == "DrumOut") return TrackType::DrumOut;   // 8.143（Phase 181）

    return TrackType::Audio;
}

//==============================================================================
// 8.142：トラックの時間の基準（Phase 180／8.105の宿題3）

juce::String timeBaseToString (TimeBase base)
{
    return base == TimeBase::musical ? "musical" : "linear";
}

TimeBase timeBaseFromString (const juce::String& text)
{
    return text == "musical" ? TimeBase::musical : TimeBase::linear;
}

TimeBase defaultTimeBaseFor (TrackType type)
{
    // **オーディオだけが`linear`。** 伸び縮みできないので、テンポを変えたときに
    // 開始位置だけ動くと隣のクリップと重なります（`TimeBase`の説明）
    return type == TrackType::Audio ? TimeBase::linear : TimeBase::musical;
}

//==============================================================================
Track::Track (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
    jassert (state.hasType (IDs::TRACK));
}

juce::StringArray Track::getColourPalette()
{
    // 設計書2.4：カラーピッカーを出すより、決め打ちの見本のほうが素早く色分けできる。
    // **ARGBのhex文字列**（データ層はjuce_graphicsに依存しない。設計書1.1）
    //
    // 8.160：**24色（8色×3段）**（Phase 198／本人の要望）。
    //
    // **並びがそのままパレットの3行になります**（`TrackColourPalette`）。
    // 段を増やすときは**8の倍数で足すこと**——読む側が8で割って行を出しています。
    //
    // | 段 | 中身 |
    // |---|---|
    // | 1 | 彩度を上げたもの（はっきり分ける用） |
    // | 2 | **Phase 197までの8色**（既定はここから配る） |
    // | 3 | 彩度を下げたもの（脇役・下ごしらえのトラック用） |
    //
    // **グレーだけは明るさで変えています**——彩度が0なので、
    // 上げても下げても同じ色にしかなりません。
    return {
        // 彩度：高
        "ffc8c8c8", "ff3402ff", "ff007dff", "ff00c461",
        "ffe4bb00", "ffff6500", "ffea072c", "ffc331da",

        // これまでの8色（グレー／パープル／ブルー／グリーン／イエロー／オレンジ／レッド／ピンク）
        "ff808080", "ff7c5cff", "ff4aa3ff", "ff35c07a",
        "ffe0c341", "ffff8a3d", "ffe5566d", "ffc76bd6",

        // 彩度：低
        "ff4f4f4f", "ffbeb1f2", "ffaacef2", "ff7fb69a",
        "ffd5c995", "fff2c4a5", "ffdaa0aa", "ffc5a1cb"
    };
}

int Track::getColourPaletteColumns()
{
    return 8;
}

juce::String Track::getDefaultColourForIndex (int trackIndex)
{
    const auto palette = Track::getColourPalette();
    const int columns = Track::getColourPaletteColumns();

    // 8.160：**既定は真ん中の段から配ります**（Phase 198）。
    // Phase 197までと同じ8色なので、**新しく作るトラックの色は変わりません**。
    //
    // **グレー（段の先頭）は飛ばして回す。** 既定色として配ると、
    // 「色を付けていないトラック」と見分けが付かなくなる
    const int middleRowStart = columns;   // 2段目の先頭＝グレー
    const int numColours = juce::jmax (1, columns - 1);

    // 8.186：**配り始めの位置は製品ごとに違います**（Phase 225／本人の指定）。
    // Manta Studioはパープルから、Emberは**ワインレッドが基調なので赤系から**。
    // 回り方は同じなので、**色の総数も並び順も変わりません**（`Branding.h`）
    const int offset = Branding::defaultTrackColourOffset;

    return palette[middleRowStart + 1 + ((offset + juce::jmax (0, trackIndex)) % numColours)];
}

Track Track::create (const juce::String& name, TrackType type, juce::UndoManager* undoManager)
{
    juce::ValueTree t (IDs::TRACK);

    t.setProperty (IDs::trackId, juce::Uuid().toString(), undoManager);
    t.setProperty (IDs::trackName, name, undoManager);
    t.setProperty (IDs::trackType, trackTypeToString (type), undoManager);
    // 既定のトラックカラー（グレー、ARGB hex）。
    // ここでjuce::Coloursを使わないのは意図的：juce_data_structuresは
    // juce_graphicsに依存していないため、Colourクラスを使うにはモジュール依存を
    // 増やす必要があり、データ層の責務としては筋が良くない（設計書1.1の層分離）。
    t.setProperty (IDs::trackColor, juce::String ("FF808080"), undoManager);
    t.setProperty (IDs::mute, false, undoManager);
    t.setProperty (IDs::solo, false, undoManager);
    t.setProperty (IDs::armed, false, undoManager);

    // 8.60：**新しいトラックの既定は-6dB**（Phase 97／改善案㉖）。
    //
    // 0dBで作ると、数本重ねただけでマスターが振り切れます。少し余裕のある値から
    // 始めるほうが、足すたびに全部を下げ直さずに済みます。
    //
    // **既存のプロジェクトは変わりません。** ここは「作るとき」の値なので、
    // 保存済みのトラックは自分の`volume`を持っています（開くたびに音量が変わったら困る）
    t.setProperty (IDs::volume, defaultTrackVolumeDb, undoManager);
    t.setProperty (IDs::pan, 0.0f, undoManager);

    juce::ValueTree clips (IDs::CLIPS);
    t.addChild (clips, -1, undoManager);

    return Track (t);
}

juce::String Track::getId() const           { return state[IDs::trackId]; }
juce::String Track::getName() const         { return state[IDs::trackName]; }
TrackType Track::getType() const            { return trackTypeFromString (state[IDs::trackType]); }

juce::String Track::getColourString() const { return state[IDs::trackColor]; }

void Track::setColourString (const juce::String& argbHex, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::trackColor, argbHex, undoManager);
}

bool Track::isMuted() const                 { return state[IDs::mute]; }
bool Track::isSoloed() const                { return state[IDs::solo]; }
bool Track::isArmed() const                 { return state[IDs::armed]; }
float Track::getVolumeDb() const            { return state[IDs::volume]; }
float Track::getPan() const                 { return state[IDs::pan]; }

void Track::setName (const juce::String& newName, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::trackName, newName, undoManager);
}

void Track::setMuted (bool shouldBeMuted, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::mute, shouldBeMuted, undoManager);
}

void Track::setSoloed (bool shouldBeSoloed, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::solo, shouldBeSoloed, undoManager);
}

void Track::setArmed (bool shouldBeArmed, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::armed, shouldBeArmed, undoManager);
}

// 8.84：MIDIトラックの入力設定（Phase 124/改善案⑯）
juce::String Track::getMidiInputDeviceName() const { return state[IDs::midiInputDevice]; }
int Track::getMidiInputChannel() const  { return juce::jlimit (0, 16, (int) state[IDs::midiInputChannel]); }
int Track::getMidiOutputChannel() const { return juce::jlimit (0, 16, (int) state[IDs::midiOutputChannel]); }

void Track::setMidiInputDeviceName (const juce::String& deviceName, juce::UndoManager* undoManager)
{
    // **「すべて」はプロパティごと消す。** 空文字を残すと、
    // 保存したファイルに意味の無いものが積み上がります（8.78のグループIDと同じ扱い）
    if (deviceName.isEmpty())
        state.removeProperty (IDs::midiInputDevice, undoManager);
    else
        state.setProperty (IDs::midiInputDevice, deviceName, undoManager);
}

void Track::setMidiInputChannel (int channel, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::midiInputChannel, juce::jlimit (0, 16, channel), undoManager);
}

void Track::setMidiOutputChannel (int channel, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::midiOutputChannel, juce::jlimit (0, 16, channel), undoManager);
}

void Track::setVolumeDb (float newVolumeDb, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::volume, newVolumeDb, undoManager);
}

void Track::setPan (float newPan, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::pan, juce::jlimit (-1.0f, 1.0f, newPan), undoManager);
}

//==============================================================================
// 仕様書5.7・設計書1.4：インサートスロット
//==============================================================================

juce::ValueTree Track::getOrCreateInsertsNode (juce::UndoManager* undoManager)
{
    auto insertsNode = state.getChildWithName (IDs::INSERTS);

    if (! insertsNode.isValid())
    {
        // Phase 12c-2より前に作られたトラックには<INSERTS>が無いため、ここで作る
        // （仕様書7章「プロジェクトファイルの後方互換性」）。
        insertsNode = juce::ValueTree (IDs::INSERTS);
        state.addChild (insertsNode, -1, undoManager);
    }

    return insertsNode;
}

int Track::getNumInserts() const
{
    return state.getChildWithName (IDs::INSERTS).getNumChildren();
}

PluginInstance Track::getInsert (int index) const
{
    return PluginInstance (state.getChildWithName (IDs::INSERTS).getChild (index));
}

PluginInstance Track::addInsert (const juce::PluginDescription& description, juce::UndoManager* undoManager)
{
    juce::ValueTree instanceTree (IDs::PLUGININSTANCE);

    PluginInstance instance (instanceTree);
    instance.setDescription (description, nullptr);

    getOrCreateInsertsNode (undoManager).addChild (instanceTree, -1, undoManager);

    return instance;
}

void Track::removeInsert (int index, juce::UndoManager* undoManager)
{
    auto insertsNode = state.getChildWithName (IDs::INSERTS);

    if (insertsNode.isValid())
        insertsNode.removeChild (index, undoManager);
}

void Track::moveInsert (int fromIndex, int toIndex, juce::UndoManager* undoManager)
{
    auto insertsNode = state.getChildWithName (IDs::INSERTS);

    if (! insertsNode.isValid())
        return;

    const int numInserts = insertsNode.getNumChildren();

    if (! juce::isPositiveAndBelow (fromIndex, numInserts))
        return;

    toIndex = juce::jlimit (0, numInserts - 1, toIndex);

    // `moveChild`は同じ位置でも履歴を作るので、動かないときは触らない
    // （`ProjectModel::moveTrackToSlot()`と同じ決まり）
    if (fromIndex == toIndex)
        return;

    insertsNode.moveChild (fromIndex, toIndex, undoManager);
}

//==============================================================================
// 仕様書5.3：音源スロット（MIDIトラック1本につき1台）
//==============================================================================

bool Track::hasInstrument() const
{
    return getInstrument().state.isValid();
}

PluginInstance Track::getInstrument() const
{
    // <INSTRUMENT>は「音源スロットという入れ物」で、中身のPLUGININSTANCEは0個か1個。
    // 入れ物と中身を分けているのは、音源を外しても「スロットはある」状態を
    // 表現できるようにするため（インサート列と同じ考え方）。
    return PluginInstance (state.getChildWithName (IDs::INSTRUMENT).getChild (0));
}

PluginInstance Track::setInstrument (const juce::PluginDescription& description, juce::UndoManager* undoManager)
{
    auto instrumentNode = state.getChildWithName (IDs::INSTRUMENT);

    if (! instrumentNode.isValid())
    {
        instrumentNode = juce::ValueTree (IDs::INSTRUMENT);
        state.addChild (instrumentNode, -1, undoManager);
    }

    // 差し替え時は作り直す。使い回すと、前の音源の内部状態（setPluginState）が
    // そのまま残り、別のプラグインへ復元しようとして事故になる。
    instrumentNode.removeAllChildren (undoManager);

    juce::ValueTree instanceTree (IDs::PLUGININSTANCE);

    PluginInstance instance (instanceTree);
    instance.setDescription (description, nullptr);

    instrumentNode.addChild (instanceTree, -1, undoManager);

    return instance;
}

void Track::removeInstrument (juce::UndoManager* undoManager)
{
    auto instrumentNode = state.getChildWithName (IDs::INSTRUMENT);

    if (instrumentNode.isValid())
        instrumentNode.removeAllChildren (undoManager);
}

//==============================================================================
// 仕様書5.6・設計書1.3：オートメーション
//==============================================================================

namespace AutomationTargets
{
    const juce::String volume { "volume" };
    const juce::String pan    { "pan" };

    static const juce::String insertPrefix     { "insert:" };
    static const juce::String instrumentPrefix { "instrument:" };

    juce::String makePluginTarget (int insertIndex, int parameterIndex)
    {
        if (insertIndex < 0)
            return instrumentPrefix + juce::String (parameterIndex);

        return insertPrefix + juce::String (insertIndex) + ":" + juce::String (parameterIndex);
    }

    bool isPluginTarget (const juce::String& targetId)
    {
        return targetId.startsWith (insertPrefix) || targetId.startsWith (instrumentPrefix);
    }

    bool parsePluginTarget (const juce::String& targetId, int& insertIndexOut, int& parameterIndexOut)
    {
        if (targetId.startsWith (instrumentPrefix))
        {
            insertIndexOut = -1;
            parameterIndexOut = targetId.fromFirstOccurrenceOf (instrumentPrefix, false, false).getIntValue();
            return parameterIndexOut >= 0;
        }

        if (! targetId.startsWith (insertPrefix))
            return false;

        const auto body = targetId.fromFirstOccurrenceOf (insertPrefix, false, false);

        insertIndexOut = body.upToFirstOccurrenceOf (":", false, false).getIntValue();
        parameterIndexOut = body.fromFirstOccurrenceOf (":", false, false).getIntValue();

        return insertIndexOut >= 0 && parameterIndexOut >= 0;
    }

    float toParameterValue (const juce::String& targetId, float normalised)
    {
        const float limited = juce::jlimit (0.0f, 1.0f, normalised);

        if (targetId == pan)
            return limited * 2.0f - 1.0f; // 0〜1 → -1〜+1

        // プラグインパラメータはJUCE側も0〜1で扱うので、そのまま返す
        if (isPluginTarget (targetId))
            return limited;

        return juce::jmap (limited, 0.0f, 1.0f, minVolumeDb, maxVolumeDb);
    }

    float fromParameterValue (const juce::String& targetId, float value)
    {
        if (targetId == pan)
            return juce::jlimit (0.0f, 1.0f, (value + 1.0f) * 0.5f);

        if (isPluginTarget (targetId))
            return juce::jlimit (0.0f, 1.0f, value);

        return juce::jlimit (0.0f, 1.0f, juce::jmap (value, minVolumeDb, maxVolumeDb, 0.0f, 1.0f));
    }

    juce::String getDisplayName (const juce::String& targetId)
    {
        if (targetId == pan)
            return "Pan";

        if (isPluginTarget (targetId))
        {
            // 実際のパラメータ名はプラグインしか知らないため、ここでは位置だけを返す。
            // 名前を出したいUIは、エンジン経由でAudioProcessorParameterを引く。
            int insertIndex = 0;
            int parameterIndex = 0;

            if (parsePluginTarget (targetId, insertIndex, parameterIndex))
                return (insertIndex < 0 ? juce::String ("Instrument")
                                         : "Insert " + juce::String (insertIndex + 1))
                        + " / Param " + juce::String (parameterIndex);

            return targetId;
        }

        return "Volume";
    }

    juce::String formatValue (const juce::String& targetId, float normalised)
    {
        const float value = toParameterValue (targetId, normalised);

        if (targetId == pan)
            return juce::String (value, 2);

        if (isPluginTarget (targetId))
            return juce::String (value, 3);

        return value <= minVolumeDb ? juce::String::fromUTF8 ("-\xe2\x88\x9e dB")
                                     : juce::String (value, 1) + " dB";
    }
}

//==============================================================================
juce::String automationCurveToString (AutomationCurve curve)
{
    switch (curve)
    {
        case AutomationCurve::Ease: return "ease";
        case AutomationCurve::Step: return "step";
        case AutomationCurve::Linear:
        default:                    return "linear";
    }
}

AutomationCurve automationCurveFromString (const juce::String& text)
{
    if (text == "ease") return AutomationCurve::Ease;
    if (text == "step") return AutomationCurve::Step;

    return AutomationCurve::Linear;
}

float applyAutomationCurve (AutomationCurve curve, float t, float amount)
{
    const float limited = juce::jlimit (0.0f, 1.0f, t);

    switch (curve)
    {
        // S字カーブ（smoothstep）。始点と終点の付近で変化が緩やかになるので、
        // フェードやフィルターのスイープが自然に聞こえる
        case AutomationCurve::Ease: return limited * limited * (3.0f - 2.0f * limited);

        // 次の点に到達するまで前の値を保つ（値が階段状に変わる）
        case AutomationCurve::Step: return 0.0f;

        case AutomationCurve::Linear:
        default:
        {
            // 8.37：**曲がり具合**（Phase 77）。0なら今までどおりの直線。
            //
            // `t`の累乗で曲げている。**0と1は必ず通る**ので、
            // どれだけ曲げても点そのものの位置は動かない（線の途中だけが動く）。
            const float bend = juce::jlimit (-maxAutomationCurveAmount,
                                              maxAutomationCurveAmount, amount);

            if (std::abs (bend) < 0.001f)
                return limited;

            return std::pow (limited, std::pow (4.0f, -bend));
        }
    }
}

float automationCurveAmountFromDrag (float t, float shaped)
{
    // `shaped = t ^ (4 ^ -amount)` を`amount`について解く。
    //
    // **端は避けて丸める。** 0や1をそのまま入れると対数が発散して、
    // 少し動かしただけで曲がり具合が振り切れる
    const float position = juce::jlimit (0.02f, 0.98f, t);
    const float height   = juce::jlimit (0.02f, 0.98f, shaped);

    const float exponent = std::log (height) / std::log (position);

    if (exponent <= 0.0f)
        return 0.0f;

    return juce::jlimit (-maxAutomationCurveAmount, maxAutomationCurveAmount,
                          -std::log (exponent) / std::log (4.0f));
}

//==============================================================================
juce::String automationModeToString (AutomationMode mode)
{
    switch (mode)
    {
        case AutomationMode::Touch: return "touch";
        case AutomationMode::Latch: return "latch";
        case AutomationMode::Write: return "write";
        case AutomationMode::Read:
        default:                    return "read";
    }
}

AutomationMode automationModeFromString (const juce::String& text)
{
    if (text == "touch") return AutomationMode::Touch;
    if (text == "latch") return AutomationMode::Latch;
    if (text == "write") return AutomationMode::Write;

    return AutomationMode::Read;
}

juce::String getAutomationModeDisplayName (AutomationMode mode)
{
    switch (mode)
    {
        case AutomationMode::Touch: return "Touch";
        case AutomationMode::Latch: return "Latch";
        case AutomationMode::Write: return "Write";
        case AutomationMode::Read:
        default:                    return "Read";
    }
}

//==============================================================================
AutomationPoint::AutomationPoint (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
}

float AutomationPoint::getValue() const  { return state[IDs::pointValue]; }

// 8.139：**保存されているのは拍のほう**（Phase 177／8.105の宿題3）。ノートと同じ形（8.138）
double AutomationPoint::getTimeBeats() const { return state[IDs::pointBeats]; }

void AutomationPoint::setTimeBeats (double newTimeBeats, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::pointBeats, juce::jmax (0.0, newTimeBeats), undoManager);
}

double AutomationPoint::getTime() const
{
    return MusicalTime::getSecondsFor (state, getTimeBeats());
}

void AutomationPoint::setTime (double newTimeSeconds, juce::UndoManager* undoManager)
{
    setTimeBeats (MusicalTime::getBeatsFor (state, juce::jmax (0.0, newTimeSeconds)), undoManager);
}

void AutomationPoint::setValue (float newValue, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::pointValue, juce::jlimit (0.0f, 1.0f, newValue), undoManager);
}

AutomationCurve AutomationPoint::getCurve() const
{
    return automationCurveFromString (state[IDs::pointCurve].toString());
}

void AutomationPoint::setCurve (AutomationCurve newCurve, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::pointCurve, automationCurveToString (newCurve), undoManager);
}

float AutomationPoint::getCurveAmount() const
{
    // 8.37：**入っていなければ0（直線）**（Phase 77）。
    // Phase 76以前に保存されたプロジェクトは、今までどおりの見た目・鳴り方になる
    return (float) state.getProperty (IDs::pointCurveAmount, 0.0f);
}

void AutomationPoint::setCurveAmount (float newAmount, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::pointCurveAmount,
                        juce::jlimit (-maxAutomationCurveAmount, maxAutomationCurveAmount, newAmount),
                        undoManager);
}

//==============================================================================
AutomationLane::AutomationLane (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
}

juce::String AutomationLane::getTargetId() const { return state[IDs::laneTargetId]; }
int AutomationLane::getNumPoints() const         { return state.getNumChildren(); }

// 8.56：アレンジ画面での見せ方（Phase 94／D3）
bool AutomationLane::isVisible() const
{
    return (bool) state.getProperty (IDs::laneVisible, false);
}

void AutomationLane::setVisible (bool shouldBeVisible, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::laneVisible, shouldBeVisible, undoManager);
}

int AutomationLane::getRowHeight() const
{
    return juce::jlimit (minimumLaneHeight, maximumLaneHeight,
                          (int) state.getProperty (IDs::laneHeight, defaultLaneHeight));
}

void AutomationLane::setRowHeight (int newHeight, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::laneHeight,
                        juce::jlimit (minimumLaneHeight, maximumLaneHeight, newHeight), undoManager);
}

// 8.59：バイパスと色（Phase 96）
bool AutomationLane::isBypassed() const
{
    return (bool) state.getProperty (IDs::laneBypassed, false);
}

void AutomationLane::setBypassed (bool shouldBypass, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::laneBypassed, shouldBypass, undoManager);
}

juce::String AutomationLane::getColourString() const
{
    return state[IDs::laneColour].toString();
}

bool AutomationLane::hasCustomColour() const
{
    return getColourString().isNotEmpty();
}

void AutomationLane::setColourString (const juce::String& argbHex, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::laneColour, argbHex, undoManager);
}

void AutomationLane::clearColour (juce::UndoManager* undoManager)
{
    // **プロパティごと消す。** 空文字を入れて残すと、「色を持っている」のか
    // 「親から受け継いでいる」のかがツリーを見ても分からなくなる
    state.removeProperty (IDs::laneColour, undoManager);
}

AutomationPoint AutomationLane::getPoint (int index) const
{
    return AutomationPoint (state.getChild (index));
}

AutomationPoint AutomationLane::addPointBeats (double timeBeats, float value,
                                                juce::UndoManager* undoManager)
{
    juce::ValueTree pointTree (IDs::POINT);

    AutomationPoint point (pointTree);

    // 8.139：**拍で書きます**（Phase 177）。この時点ではまだ親に付いていないので、
    // 秒で書こうとするとテンポの表へ辿り着けません（`MusicalTime.h`）。
    // 拍なら換算が要らないので、繋ぐ前でも正しく書けます。Undo対象外なのは従来どおり
    point.setTimeBeats (timeBeats, nullptr);
    point.setValue (value, nullptr);
    pointTree.setProperty (IDs::pointCurve, "linear", nullptr); // 設計書1.3のcurveType

    // 時刻順を保つため、挿す位置を探してから追加する。
    // 末尾へ足して後から並べ替える方式だと、追加のたびに全点が動いて
    // Undoの記録が無駄に大きくなる。
    int insertIndex = state.getNumChildren();

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        if ((double) state.getChild (i)[IDs::pointBeats] > timeBeats)
        {
            insertIndex = i;
            break;
        }
    }

    state.addChild (pointTree, insertIndex, undoManager);
    return point;
}

AutomationPoint AutomationLane::addPoint (double timeSeconds, float value, juce::UndoManager* undoManager)
{
    // 8.139：換算は**レーンのツリー**で（`Track::addNote()`と同じ理由。8.138）
    return addPointBeats (MusicalTime::getBeatsFor (state, juce::jmax (0.0, timeSeconds)),
                           value, undoManager);
}

void AutomationLane::removePoint (int index, juce::UndoManager* undoManager)
{
    state.removeChild (index, undoManager);
}

void AutomationLane::sortPoints (juce::UndoManager* undoManager)
{
    struct PointComparator
    {
        static int compareElements (const juce::ValueTree& a, const juce::ValueTree& b)
        {
            // 8.139：**拍で並べる**（Phase 177）。秒と同じ向きに並ぶので結果は変わらない
            const double timeA = a[IDs::pointBeats];
            const double timeB = b[IDs::pointBeats];

            return timeA < timeB ? -1 : (timeA > timeB ? 1 : 0);
        }
    };

    PointComparator comparator;
    state.sort (comparator, undoManager, true);
}

float AutomationLane::getValueAt (double timeSeconds, float fallback) const
{
    const int numPoints = getNumPoints();

    if (numPoints == 0)
        return fallback;

    // 最初の点より前／最後の点より後は、その点の値のまま伸ばす
    if (timeSeconds <= getPoint (0).getTime())
        return getPoint (0).getValue();

    const auto lastPoint = getPoint (numPoints - 1);

    if (timeSeconds >= lastPoint.getTime())
        return lastPoint.getValue();

    for (int i = 1; i < numPoints; ++i)
    {
        const auto next = getPoint (i);

        if (next.getTime() < timeSeconds)
            continue;

        const auto previous = getPoint (i - 1);
        const double span = next.getTime() - previous.getTime();

        if (span <= 0.0)
            return next.getValue(); // 同じ時刻に点が重なっている場合

        const double t = (timeSeconds - previous.getTime()) / span;

        // 区間の形は「手前の点」が持つ（仕様書5.6・設計書1.3のcurveType）
        const float shaped = applyAutomationCurve (previous.getCurve(), (float) t, previous.getCurveAmount());

        return previous.getValue() + (next.getValue() - previous.getValue()) * shaped;
    }

    return lastPoint.getValue();
}

void AutomationLane::removePointsInRange (double fromTime, double toTime, juce::UndoManager* undoManager)
{
    if (toTime < fromTime)
        std::swap (fromTime, toTime);

    // 後ろから消す。前から消すと、消したぶんだけ後続の番号がずれる
    for (int i = getNumPoints() - 1; i >= 0; --i)
    {
        const double time = getPoint (i).getTime();

        if (time > fromTime && time <= toTime)
            removePoint (i, undoManager);
    }
}

void AutomationLane::writeValue (double fromTime, double toTime, float value, juce::UndoManager* undoManager)
{
    // なぞった区間の既存の点を消してから、今の値を置く。
    // 消さずに足すだけだと、元のカーブと新しい値が混ざって暴れる。
    removePointsInRange (fromTime, toTime, undoManager);
    addPoint (toTime, value, undoManager);
}

//==============================================================================
namespace
{
    /** オートメーションの置き場所は、トラック配下（Track）とマスター配下（MASTERBUS）の
        2種類ある。どちらも作りは同じなので、親のValueTreeを受け取る形にまとめてある。 */
    juce::ValueTree getOrCreateAutomationNodeIn (juce::ValueTree parent, juce::UndoManager* undoManager)
    {
        auto automationNode = parent.getChildWithName (IDs::AUTOMATION);

        if (! automationNode.isValid())
        {
            // Phase 19より前に作られたものには<AUTOMATION>が無い
            //（仕様書7章「プロジェクトファイルの後方互換性」）
            automationNode = juce::ValueTree (IDs::AUTOMATION);
            parent.addChild (automationNode, -1, undoManager);
        }

        return automationNode;
    }

    AutomationLane findAutomationLaneIn (const juce::ValueTree& parent, const juce::String& targetId)
    {
        return AutomationLane (parent.getChildWithName (IDs::AUTOMATION)
                                     .getChildWithProperty (IDs::laneTargetId, targetId));
    }

    AutomationLane getOrCreateAutomationLaneIn (juce::ValueTree parent, const juce::String& targetId,
                                                 juce::UndoManager* undoManager)
    {
        auto existing = findAutomationLaneIn (parent, targetId);

        if (existing.state.isValid())
            return existing;

        juce::ValueTree laneTree (IDs::LANE);
        laneTree.setProperty (IDs::laneTargetId, targetId, nullptr);

        getOrCreateAutomationNodeIn (parent, undoManager).addChild (laneTree, -1, undoManager);

        return AutomationLane (laneTree);
    }
}

juce::ValueTree Track::getOrCreateAutomationNode (juce::UndoManager* undoManager)
{
    return getOrCreateAutomationNodeIn (state, undoManager);
}

AutomationMode Track::getAutomationMode() const
{
    return automationModeFromString (state[IDs::automationMode].toString());
}

void Track::setAutomationMode (AutomationMode newMode, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::automationMode, automationModeToString (newMode), undoManager);
}

//==============================================================================
// 8.56：アレンジ画面に出すレーンの行（Phase 94／D3）
//
// **トラックとマスターで同じ規則にする**ため、中身はここの無名名前空間へ。
// 置き場所（トラック配下か`<MASTERBUS>`配下か）だけが違う。

namespace
{
    int countVisibleLanesIn (const juce::ValueTree& parent)
    {
        int count = 0;

        for (auto lane : parent.getChildWithName (IDs::AUTOMATION))
            if ((bool) lane.getProperty (IDs::laneVisible, false))
                ++count;

        return count;
    }

    AutomationLane getVisibleLaneIn (const juce::ValueTree& parent, int ordinal)
    {
        int index = 0;

        for (auto lane : parent.getChildWithName (IDs::AUTOMATION))
        {
            if (! (bool) lane.getProperty (IDs::laneVisible, false))
                continue;

            if (index == ordinal)
                return AutomationLane (lane);

            ++index;
        }

        return AutomationLane (juce::ValueTree());
    }

    void setLaneVisibleIn (juce::ValueTree parent, const juce::String& targetId,
                            bool shouldBeVisible, juce::UndoManager* undoManager)
    {
        // **隠すときは作らない。** 無いものを隠すために空のレーンを作ると、
        // 触ってもいないパラメータのノードがプロジェクトに増えていく
        if (! shouldBeVisible)
        {
            auto existing = findAutomationLaneIn (parent, targetId);

            if (existing.state.isValid())
                existing.setVisible (false, undoManager);

            return;
        }

        getOrCreateAutomationLaneIn (parent, targetId, undoManager).setVisible (true, undoManager);
    }

    void hideAllLanesIn (juce::ValueTree parent, juce::UndoManager* undoManager)
    {
        for (auto lane : parent.getChildWithName (IDs::AUTOMATION))
            AutomationLane (lane).setVisible (false, undoManager);
    }
}

int Track::getNumVisibleAutomationLanes() const
{
    return countVisibleLanesIn (state);
}

AutomationLane Track::getVisibleAutomationLane (int ordinal) const
{
    return getVisibleLaneIn (state, ordinal);
}

bool Track::isAutomationLaneVisible (const juce::String& targetId) const
{
    auto lane = findAutomationLane (targetId);

    return lane.state.isValid() && lane.isVisible();
}

void Track::setAutomationLaneVisible (const juce::String& targetId, bool shouldBeVisible,
                                       juce::UndoManager* undoManager)
{
    setLaneVisibleIn (state, targetId, shouldBeVisible, undoManager);
}

void Track::hideAllAutomationLanes (juce::UndoManager* undoManager)
{
    hideAllLanesIn (state, undoManager);
}

int Track::getNumAutomationLanes() const
{
    return state.getChildWithName (IDs::AUTOMATION).getNumChildren();
}

AutomationLane Track::getAutomationLane (int index) const
{
    return AutomationLane (state.getChildWithName (IDs::AUTOMATION).getChild (index));
}

AutomationLane Track::findAutomationLane (const juce::String& targetId) const
{
    return findAutomationLaneIn (state, targetId);
}

AutomationLane Track::getOrCreateAutomationLane (const juce::String& targetId, juce::UndoManager* undoManager)
{
    return getOrCreateAutomationLaneIn (state, targetId, undoManager);
}

void Track::removeAutomationLane (const juce::String& targetId, juce::UndoManager* undoManager)
{
    auto automationNode = state.getChildWithName (IDs::AUTOMATION);

    if (automationNode.isValid())
        automationNode.removeChild (automationNode.getChildWithProperty (IDs::laneTargetId, targetId),
                                     undoManager);
}

//==============================================================================
// 8.54：その時刻で実際に効いている値（Phase 93）

namespace
{
    /** レーンと「フェーダーの値」から、その時刻に効いている値を選ぶ。
        **トラックとマスターで同じ判断をさせる**ために切り出してある。 */
    float effectiveValueAt (const AutomationLane& lane, AutomationMode mode,
                             const juce::String& targetId, double timeSeconds, float faderValue)
    {
        // 記録中はフェーダーの値がそのまま音になる（TrackChannelProcessor::setAutomationBypassed）。
        // ここでレーンの値を出すと、**書いている最中に自分の手と表示が食い違う**
        if (mode != AutomationMode::Read)
            return faderValue;

        if (! lane.state.isValid() || lane.isEmpty())
            return faderValue;

        // 8.59：**バイパス中は鳴っていない**ので、表示もフェーダーの値（Phase 96）。
        // ここを忘れると、音は動かないのにフェーダーだけ動く
        if (lane.isBypassed())
            return faderValue;

        const float fallback = AutomationTargets::fromParameterValue (targetId, faderValue);

        return AutomationTargets::toParameterValue (targetId, lane.getValueAt (timeSeconds, fallback));
    }
}

float Track::getEffectiveVolumeDbAt (double timeSeconds) const
{
    return effectiveValueAt (findAutomationLane (AutomationTargets::volume), getAutomationMode(),
                              AutomationTargets::volume, timeSeconds, getVolumeDb());
}

float Track::getEffectivePanAt (double timeSeconds) const
{
    return effectiveValueAt (findAutomationLane (AutomationTargets::pan), getAutomationMode(),
                              AutomationTargets::pan, timeSeconds, getPan());
}

//==============================================================================
// 仕様書5.2.2：センド
//==============================================================================

Send::Send (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
}

juce::String Send::getTargetTrackId() const { return state[IDs::sendTargetTrackId]; }
float Send::getLevelDb() const              { return state[IDs::sendLevel]; }

void Send::setLevelDb (float newLevelDb, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::sendLevel, newLevelDb, undoManager);
}

int Track::getNumSends() const
{
    return state.getChildWithName (IDs::SENDS).getNumChildren();
}

Send Track::getSend (int index) const
{
    return Send (state.getChildWithName (IDs::SENDS).getChild (index));
}

Send Track::addSend (const juce::String& targetTrackId, juce::UndoManager* undoManager)
{
    auto sendsNode = state.getChildWithName (IDs::SENDS);

    if (! sendsNode.isValid())
    {
        sendsNode = juce::ValueTree (IDs::SENDS);
        state.addChild (sendsNode, -1, undoManager);
    }

    // 同じ送り先が既にあれば、それを使い回す（同一トラックへ二重に送らない）
    auto existing = sendsNode.getChildWithProperty (IDs::sendTargetTrackId, targetTrackId);

    if (existing.isValid())
        return Send (existing);

    juce::ValueTree sendTree (IDs::SEND);
    sendTree.setProperty (IDs::sendTargetTrackId, targetTrackId, nullptr);
    // 既定は-6dB。0dBだと送った瞬間に大きく変わって驚くため、控えめから始める。
    sendTree.setProperty (IDs::sendLevel, -6.0f, nullptr);

    sendsNode.addChild (sendTree, -1, undoManager);

    return Send (sendTree);
}

void Track::removeSend (int index, juce::UndoManager* undoManager)
{
    auto sendsNode = state.getChildWithName (IDs::SENDS);

    if (sendsNode.isValid())
        sendsNode.removeChild (index, undoManager);
}

void Track::moveSend (int fromIndex, int toIndex, juce::UndoManager* undoManager)
{
    auto sendsNode = state.getChildWithName (IDs::SENDS);

    if (! sendsNode.isValid())
        return;

    const int numSends = sendsNode.getNumChildren();

    if (! juce::isPositiveAndBelow (fromIndex, numSends))
        return;

    toIndex = juce::jlimit (0, numSends - 1, toIndex);

    // `moveChild`は同じ位置でも履歴を作るので、動かないときは触らない（`moveInsert`と同じ）
    if (fromIndex == toIndex)
        return;

    sendsNode.moveChild (fromIndex, toIndex, undoManager);
}

//==============================================================================
// 仕様書5.2.4：VCAトラック
//==============================================================================

juce::StringArray Track::getLinkedTrackIds() const
{
    // 設計書1.4のスキーマに合わせ、カンマ区切りの1属性として持つ
    // （ヒットポイント／設計書1.4の`linkedTrackIds="id1,id2"`と同じ形式）。
    juce::StringArray ids;
    ids.addTokens (state[IDs::vcaLinkedTrackIds].toString(), ",", "");
    ids.removeEmptyStrings();
    ids.removeDuplicates (false);

    return ids;
}

void Track::setLinkedTrackIds (const juce::StringArray& trackIds, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::vcaLinkedTrackIds, trackIds.joinIntoString (","), undoManager);
}

bool Track::isTrackLinkedToVca (const juce::String& trackId) const
{
    return getLinkedTrackIds().contains (trackId);
}

void Track::linkTrackToVca (const juce::String& trackId, juce::UndoManager* undoManager)
{
    if (trackId.isEmpty())
        return;

    auto ids = getLinkedTrackIds();

    if (ids.contains (trackId))
        return;

    ids.add (trackId);
    setLinkedTrackIds (ids, undoManager);
}

void Track::unlinkTrackFromVca (const juce::String& trackId, juce::UndoManager* undoManager)
{
    auto ids = getLinkedTrackIds();

    if (! ids.contains (trackId))
        return;

    ids.removeString (trackId);
    setLinkedTrackIds (ids, undoManager);
}

bool Track::isPreFader() const
{
    return state[IDs::sendPrePost].toString() == "Pre";
}

void Track::setPreFader (bool shouldBePreFader, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::sendPrePost, shouldBePreFader ? "Pre" : "Post", undoManager);
}

AudioClip Track::addAudioClip (const juce::String& sourceFilePath, double startTimeSeconds,
                                double lengthSeconds, juce::UndoManager* undoManager, double offsetSeconds)
{
    auto clip = AudioClip::create (sourceFilePath, startTimeSeconds, lengthSeconds, undoManager, offsetSeconds);
    auto clipsNode = state.getChildWithName (IDs::CLIPS);
    clipsNode.addChild (clip.state, -1, undoManager);

    // 8.142：**繋いでから位置を置き直す**（Phase 180）。`musical`のトラックなら、
    // ここで秒が拍へ持ち替わります（`linear`なら同じ値を書き直すだけ）
    if (getTimeBase() == TimeBase::musical)
    {
        clip.setStartTime (startTimeSeconds, undoManager);
        clip.state.removeProperty (IDs::clipStartTime, undoManager);
    }

    return clip;
}

int Track::getNumClips() const
{
    // AUDIOCLIPのみを数える（同じCLIPSノード配下にMIDICLIPも入るため）
    auto clipsNode = state.getChildWithName (IDs::CLIPS);
    int count = 0;

    for (int i = 0; i < clipsNode.getNumChildren(); ++i)
        if (clipsNode.getChild (i).hasType (IDs::AUDIOCLIP))
            ++count;

    return count;
}

AudioClip Track::getClip (int index) const
{
    auto clipsNode = state.getChildWithName (IDs::CLIPS);
    int count = 0;

    for (int i = 0; i < clipsNode.getNumChildren(); ++i)
    {
        auto child = clipsNode.getChild (i);

        if (child.hasType (IDs::AUDIOCLIP))
        {
            if (count == index)
                return AudioClip (child);

            ++count;
        }
    }

    return AudioClip (juce::ValueTree (IDs::AUDIOCLIP)); // 見つからない場合の空クリップ
}

void Track::removeClip (const AudioClip& clip, juce::UndoManager* undoManager)
{
    auto clipsNode = state.getChildWithName (IDs::CLIPS);
    clipsNode.removeChild (clip.state, undoManager);
}

//==============================================================================
// 8.91：MIDIはトラックが直接持つ（Phase 131）
//
// **`<TRACK><NOTES>`と`<TRACK><CCEVENTS>`。** 中身の作りはクリップのときと
// 同じで、**違うのは時刻の意味だけ**（中身の時刻 → 曲の時刻）。
// だからノードの識別子（`NOTES`／`CCEVENTS`／`NOTE`／`CC`）も使い回しています。
//==============================================================================

namespace
{
    /** 無ければ作って返す（古いプロジェクトには無いため）。 */
    juce::ValueTree getOrCreateChild (juce::ValueTree parent, const juce::Identifier& type,
                                       juce::UndoManager* undoManager)
    {
        auto child = parent.getChildWithName (type);

        if (! child.isValid())
        {
            child = juce::ValueTree (type);
            parent.addChild (child, -1, undoManager);
        }

        return child;
    }
}

int Track::getNumNotes() const
{
    return state.getChildWithName (IDs::NOTES).getNumChildren();
}

Note Track::getNote (int index) const
{
    return Note (state.getChildWithName (IDs::NOTES).getChild (index));
}

Note Track::addNoteBeats (int pitch, int velocity, double startBeats, double lengthBeats,
                           juce::UndoManager* undoManager)
{
    auto note = Note::create (pitch, velocity, startBeats, lengthBeats, undoManager);

    getOrCreateChild (state, IDs::NOTES, undoManager).addChild (note.state, -1, undoManager);
    return note;
}

Note Track::addNote (int pitch, int velocity, double timelineStartSeconds, double lengthSeconds,
                      juce::UndoManager* undoManager)
{
    // 8.138：**換算はトラックのツリーで行うこと**（Phase 176）。
    //
    // 出来たてのノートはまだ繋がっていないので、そちらで換算すると
    // **既定の120BPMに落ちます**。トラックは既にプロジェクトの一部なので、
    // ここでなら正しい表が引けます（`MusicalTime.h`）
    const double start = juce::jmax (0.0, timelineStartSeconds);
    const double startBeats = MusicalTime::getBeatsFor (state, start);
    const double endBeats = MusicalTime::getBeatsFor (state, start + juce::jmax (0.0, lengthSeconds));

    return addNoteBeats (pitch, velocity, startBeats, endBeats - startBeats, undoManager);
}

void Track::removeNote (const Note& note, juce::UndoManager* undoManager)
{
    state.getChildWithName (IDs::NOTES).removeChild (note.state, undoManager);
}

bool Track::transposeNotes (int semitones, juce::UndoManager* undoManager)
{
    if (semitones == 0)
        return false;

    // **1つでも範囲を外れるなら何もしない**（和音の形が崩れる。8.77）
    for (int i = 0; i < getNumNotes(); ++i)
    {
        const int moved = getNote (i).getPitch() + semitones;

        if (moved < 0 || moved > 127)
            return false;
    }

    for (int i = 0; i < getNumNotes(); ++i)
    {
        auto note = getNote (i);
        note.setPitch (note.getPitch() + semitones, undoManager);
    }

    return true;
}

juce::String Track::getGrooveTemplateId() const
{
    return state[IDs::clipGrooveTemplateId].toString();
}

void Track::setGrooveTemplateId (const juce::String& templateId, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::clipGrooveTemplateId, templateId, undoManager);
}

//==============================================================================
// 仕様書5.3.3：CC（Phase 131でクリップからトラックへ移した）

int Track::getNumCCEvents() const
{
    return state.getChildWithName (IDs::CCEVENTS).getNumChildren();
}

CCEvent Track::getCCEvent (int index) const
{
    return CCEvent (state.getChildWithName (IDs::CCEVENTS).getChild (index));
}

int Track::getNumCCEventsFor (int controllerNumber) const
{
    int count = 0;

    for (int i = 0; i < getNumCCEvents(); ++i)
        if (getCCEvent (i).getControllerNumber() == controllerNumber)
            ++count;

    return count;
}

CCEvent Track::getCCEventFor (int controllerNumber, int index) const
{
    int count = 0;

    for (int i = 0; i < getNumCCEvents(); ++i)
    {
        auto event = getCCEvent (i);

        if (event.getControllerNumber() == controllerNumber)
        {
            if (count == index)
                return event;

            ++count;
        }
    }

    return CCEvent (juce::ValueTree());
}

CCEvent Track::findNextCCEvent (const CCEvent& event) const
{
    if (! event.state.isValid())
        return CCEvent (juce::ValueTree());

    const int controllerNumber = event.getControllerNumber();
    const double time = event.getTime();

    CCEvent best { juce::ValueTree() };

    for (int i = 0; i < getNumCCEvents(); ++i)
    {
        auto candidate = getCCEvent (i);

        if (candidate.getControllerNumber() != controllerNumber || candidate.state == event.state)
            continue;

        if (candidate.getTime() < time)
            continue;

        if (! best.state.isValid() || candidate.getTime() < best.getTime())
            best = candidate;
    }

    return best;
}

CCEvent Track::addCCEventBeats (int controllerNumber, int value, double timeBeats,
                                 juce::UndoManager* undoManager)
{
    auto eventsNode = getOrCreateChild (state, IDs::CCEVENTS, undoManager);

    const double beats = juce::jmax (0.0, timeBeats);
    const int limitedValue = juce::jlimit (0, MidiControllers::getMaxValue (controllerNumber), value);

    // 8.138：**比べるのも拍で**（Phase 176）。秒へ直してから比べると、
    // 1つ足すたびにイベントの数だけ換算が走ります
    for (int i = 0; i < eventsNode.getNumChildren(); ++i)
    {
        CCEvent existing (eventsNode.getChild (i));

        // 同じコントローラーの同じ時刻に既にあるなら、積み増さず値を差し替える
        if (existing.getControllerNumber() == controllerNumber
             && std::abs (existing.getTimeBeats() - beats) < 1.0e-9)
        {
            existing.setValue (limitedValue, undoManager);
            return existing;
        }
    }

    juce::ValueTree eventState (IDs::CC);
    eventState.setProperty (IDs::ccController, controllerNumber, undoManager);
    eventState.setProperty (IDs::ccValue, limitedValue, undoManager);
    eventState.setProperty (IDs::ccTimeBeats, beats, undoManager);

    // 時刻の昇順を保つ位置へ挿す（拍と秒は同じ向きに並ぶので、拍で見て構わない）
    int insertIndex = eventsNode.getNumChildren();

    for (int i = 0; i < eventsNode.getNumChildren(); ++i)
    {
        if (CCEvent (eventsNode.getChild (i)).getTimeBeats() > beats)
        {
            insertIndex = i;
            break;
        }
    }

    eventsNode.addChild (eventState, insertIndex, undoManager);
    return CCEvent (eventState);
}

CCEvent Track::addCCEvent (int controllerNumber, int value, double timelineSeconds,
                            juce::UndoManager* undoManager)
{
    // 8.138：換算は**トラックのツリー**で（`addNote()`と同じ理由。Phase 176）
    return addCCEventBeats (controllerNumber, value,
                             MusicalTime::getBeatsFor (state, juce::jmax (0.0, timelineSeconds)),
                             undoManager);
}

void Track::removeCCEvent (const CCEvent& event, juce::UndoManager* undoManager)
{
    state.getChildWithName (IDs::CCEVENTS).removeChild (event.state, undoManager);
}

void Track::removeAllCCEventsFor (int controllerNumber, juce::UndoManager* undoManager)
{
    auto eventsNode = state.getChildWithName (IDs::CCEVENTS);

    // 後ろから消す（前から消すと、消したぶんだけ後続の番号がずれる）
    for (int i = eventsNode.getNumChildren(); --i >= 0;)
        if (CCEvent (eventsNode.getChild (i)).getControllerNumber() == controllerNumber)
            eventsNode.removeChild (i, undoManager);
}

void Track::sortCCEvents (juce::UndoManager* undoManager)
{
    struct TimeOrder
    {
        static int compareElements (const juce::ValueTree& a, const juce::ValueTree& b)
        {
            // 8.138：**拍で並べる**（Phase 176）。秒と同じ向きに並ぶので結果は変わらず、
            // 換算のぶんだけ速い
            const double timeA = a[IDs::ccTimeBeats];
            const double timeB = b[IDs::ccTimeBeats];

            return timeA < timeB ? -1 : (timeA > timeB ? 1 : 0);
        }
    };

    TimeOrder comparator;
    state.getChildWithName (IDs::CCEVENTS).sort (comparator, undoManager, true);
}

int Track::getCCValueAt (int controllerNumber, double timelineSeconds) const
{
    int value = MidiControllers::getDefaultValue (controllerNumber);

    // **補間はしない（ステップ状に保持する）。** MIDIのCCは「送られた値が
    // 次に送られるまで保たれる」ものなので、間を滑らかに繋ぐと実際の挙動と食い違う
    for (int i = 0; i < getNumCCEvents(); ++i)
    {
        auto event = getCCEvent (i);

        if (event.getControllerNumber() != controllerNumber)
            continue;

        if (event.getTime() > timelineSeconds)
            break;

        value = event.getValue();
    }

    return value;
}

//==============================================================================
// 8.91：アレンジ画面に出す「ノートの塊」（Phase 131）

std::vector<Track::NoteBlock> Track::getNoteBlocks (double gapSeconds) const
{
    std::vector<NoteBlock> blocks;

    const int numNotes = getNumNotes();

    if (numNotes == 0)
        return blocks;

    // **開始順に並べてから走る。** ValueTreeの並びは追加順なので、
    // そのまま走ると塊の切れ目が入力の順番で変わってしまう
    struct Span { double start; double end; };

    std::vector<Span> spans;
    spans.reserve ((size_t) numNotes);

    for (int i = 0; i < numNotes; ++i)
    {
        auto note = getNote (i);
        spans.push_back ({ note.getStartTime(), note.getStartTime() + note.getLength() });
    }

    std::sort (spans.begin(), spans.end(),
                [] (const Span& a, const Span& b) { return a.start < b.start; });

    const double gap = juce::jmax (0.0, gapSeconds);

    NoteBlock current;
    current.startTime = spans.front().start;
    current.endTime = spans.front().end;
    current.numNotes = 1;

    for (size_t i = 1; i < spans.size(); ++i)
    {
        // **切れ目は「前の塊の終わりから`gap`以上空いたとき」。**
        // 音符1つごとに切ると、まばらな旋律が細切れの四角だらけになる
        if (spans[i].start - current.endTime >= gap)
        {
            blocks.push_back (current);

            current = NoteBlock();
            current.startTime = spans[i].start;
            current.endTime = spans[i].end;
            current.numNotes = 1;
            continue;
        }

        current.endTime = juce::jmax (current.endTime, spans[i].end);
        ++current.numNotes;
    }

    blocks.push_back (current);
    return blocks;
}

//==============================================================================
// 仕様書5.2.3・5.11：コードトラック

Scale Track::getChordKey() const
{
    Scale key;
    key.root  = (int)  state.getProperty (IDs::chordKeyRoot,  0);
    key.minor = (bool) state.getProperty (IDs::chordKeyMinor, false);

    // 壊れた値でスケールの表を引かせない
    key.root = ((key.root % 12) + 12) % 12;
    return key;
}

void Track::setChordKey (const Scale& newKey, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::chordKeyRoot,  ((newKey.root % 12) + 12) % 12, undoManager);
    state.setProperty (IDs::chordKeyMinor, newKey.minor, undoManager);
}

juce::ValueTree Track::getOrCreateChordRegionsNode (juce::UndoManager* undoManager)
{
    auto node = state.getChildWithName (IDs::CHORDREGIONS);

    if (! node.isValid())
    {
        node = juce::ValueTree (IDs::CHORDREGIONS);
        state.addChild (node, -1, undoManager);
    }

    return node;
}

ChordRegion Track::addChordRegionBeats (const Chord& chord, double startBeats,
                                         double lengthBeats, juce::UndoManager* undoManager)
{
    auto region = ChordRegion::create (chord, startBeats, lengthBeats, undoManager);
    auto regionsNode = getOrCreateChordRegionsNode (undoManager);

    // 開始位置の順を保ったまま挿す（宣言のコメント参照）。
    // 8.139：**比べるのも拍で**（Phase 177）。秒と同じ向きに並びます
    int insertIndex = regionsNode.getNumChildren();

    for (int i = 0; i < regionsNode.getNumChildren(); ++i)
    {
        auto child = regionsNode.getChild (i);

        if (child.hasType (IDs::CHORDREGION)
            && (double) child.getProperty (IDs::chordRegionStartBeats, 0.0) > region.getStartBeats())
        {
            insertIndex = i;
            break;
        }
    }

    regionsNode.addChild (region.state, insertIndex, undoManager);
    return region;
}

ChordRegion Track::addChordRegion (const Chord& chord, double startTimeSeconds,
                                    double lengthSeconds, juce::UndoManager* undoManager)
{
    // 8.139：換算は**トラックのツリー**で（`addNote()`と同じ理由。8.138）
    const double start = juce::jmax (0.0, startTimeSeconds);
    const double startBeats = MusicalTime::getBeatsFor (state, start);
    const double endBeats = MusicalTime::getBeatsFor (state, start + juce::jmax (0.0, lengthSeconds));

    return addChordRegionBeats (chord, startBeats, endBeats - startBeats, undoManager);
}

int Track::getNumChordRegions() const
{
    auto regionsNode = state.getChildWithName (IDs::CHORDREGIONS);
    int count = 0;

    for (int i = 0; i < regionsNode.getNumChildren(); ++i)
        if (regionsNode.getChild (i).hasType (IDs::CHORDREGION))
            ++count;

    return count;
}

ChordRegion Track::getChordRegion (int index) const
{
    auto regionsNode = state.getChildWithName (IDs::CHORDREGIONS);
    int count = 0;

    for (int i = 0; i < regionsNode.getNumChildren(); ++i)
    {
        auto child = regionsNode.getChild (i);

        if (child.hasType (IDs::CHORDREGION))
        {
            if (count == index)
                return ChordRegion (child);

            ++count;
        }
    }

    return ChordRegion (juce::ValueTree()); // 見つからない場合
}

ChordRegion Track::findChordRegionAt (double timeSeconds) const
{
    const int numRegions = getNumChordRegions();

    for (int i = 0; i < numRegions; ++i)
    {
        auto region = getChordRegion (i);

        if (timeSeconds >= region.getStartTime() && timeSeconds < region.getEndTime())
            return region;
    }

    return ChordRegion (juce::ValueTree());
}

void Track::normaliseChordRegions (juce::UndoManager* undoManager)
{
    const int numRegions = getNumChordRegions();

    for (int i = 0; i < numRegions; ++i)
    {
        auto region = getChordRegion (i);

        // 8.139：**拍で数えます**（Phase 177／8.105の宿題3）。秒で計算して秒で書くと、
        // 中で拍へ戻す往復が挟まります。**「この旗から次の旗まで」は元々musicalな話**なので、
        // 拍のまま扱うほうが素直で、丸めも入りません
        const double start = region.getStartBeats();

        // **「次」は位置で探すこと。** 区間の並び順は位置順とは限りません
        // （足した順に入る）。番号の隣を見ると、飛び越したところに隙間が残ります
        double nextStart = -1.0;

        for (int j = 0; j < numRegions; ++j)
        {
            if (j == i)
                continue;

            const double other = getChordRegion (j).getStartBeats();

            if (other > start + 1.0e-6 && (nextStart < 0.0 || other < nextStart))
                nextStart = other;
        }

        // いちばん後ろは触らない（次が無いので、曲の終わりはそちらの長さが決める）
        if (nextStart < 0.0)
            continue;

        const double wanted = nextStart - start;

        // **同じ値なら書かないこと。** ValueTreeへの書き込みは購読側の
        // 描き直しを呼ぶので、変わっていないものまで書くと毎回全部描き直します
        if (! juce::approximatelyEqual (region.getLengthBeats(), wanted))
            region.setLengthBeats (wanted, undoManager);
    }
}

ChordRegion Track::findChordRegionBefore (double timeSeconds) const

{
    // 波括弧で書くこと。`ChordRegion found (juce::ValueTree());`と丸括弧にすると、
    // C++は「ValueTreeを返す関数を引数に取る関数の宣言」と解釈する（最も苛立たしい解析）。
    ChordRegion found { juce::ValueTree() };
    const int numRegions = getNumChordRegions();

    // 開始時刻順に並んでいるので、条件を満たす最後のものが「いちばん近い前」
    for (int i = 0; i < numRegions; ++i)
    {
        auto region = getChordRegion (i);

        if (region.getStartTime() >= timeSeconds)
            break;

        found = region;
    }

    return found;
}

bool Track::hasOverlappingChordRegion (double startSeconds, double endSeconds,
                                        const juce::String& ignoreRegionId) const
{
    const int numRegions = getNumChordRegions();

    for (int i = 0; i < numRegions; ++i)
    {
        auto region = getChordRegion (i);

        if (ignoreRegionId.isNotEmpty() && region.getId() == ignoreRegionId)
            continue;

        // 端が触れているだけ（前の終わり＝次の始まり）は重なりではない
        if (region.getStartTime() < endSeconds && region.getEndTime() > startSeconds)
            return true;
    }

    return false;
}

void Track::setChordRegionTime (const ChordRegion& region, double newStartSeconds,
                                 double newLengthSeconds, juce::UndoManager* undoManager)
{
    auto regionsNode = state.getChildWithName (IDs::CHORDREGIONS);

    if (! regionsNode.isValid() || ! region.state.isValid())
        return;

    ChordRegion target (region.state);
    target.setStartTime (newStartSeconds, undoManager);
    target.setLength (newLengthSeconds, undoManager);

    // 開始時刻順の並びへ戻す。**一度外してから挿し直す**のが分かりやすい
    // （残っている子だけを見て入る場所を決めればよく、addChordRegion()と同じ形になる）。
    // ValueTreeは参照カウントで持っているので、親から外しても消えない。
    const int currentIndex = regionsNode.indexOf (target.state);

    if (currentIndex < 0)
        return;

    regionsNode.removeChild (currentIndex, undoManager);

    int insertIndex = regionsNode.getNumChildren();

    for (int i = 0; i < regionsNode.getNumChildren(); ++i)
    {
        auto child = regionsNode.getChild (i);

        if (child.hasType (IDs::CHORDREGION)
            && (double) child.getProperty (IDs::chordRegionStartBeats, 0.0) > target.getStartBeats())
        {
            insertIndex = i;
            break;
        }
    }

    regionsNode.addChild (target.state, insertIndex, undoManager);
}

void Track::removeChordRegion (const ChordRegion& region, juce::UndoManager* undoManager)
{
    auto regionsNode = state.getChildWithName (IDs::CHORDREGIONS);
    regionsNode.removeChild (region.state, undoManager);
}

//==============================================================================
ChordRegion::ChordRegion (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
    jassert (! state.isValid() || state.hasType (IDs::CHORDREGION));
}

ChordRegion ChordRegion::create (const Chord& chord, double startBeats,
                                  double lengthBeats, juce::UndoManager* undoManager)
{
    // 8.139：**拍で作ります**（Phase 177）。`Note::create()`と同じ理由——
    // ここで作るツリーはまだ繋がっていないので、秒だと換算できません（8.138）
    juce::ValueTree r (IDs::CHORDREGION);

    r.setProperty (IDs::chordRegionId, juce::Uuid().toString(), undoManager);
    r.setProperty (IDs::chordRegionStartBeats, juce::jmax (0.0, startBeats), undoManager);
    r.setProperty (IDs::chordRegionLengthBeats, juce::jmax (0.0, lengthBeats), undoManager);

    ChordRegion region (r);
    region.setChord (chord, undoManager);
    return region;
}

juce::String ChordRegion::getId() const  { return state[IDs::chordRegionId]; }

// 8.139：**保存されているのは拍のほう**（Phase 177）。ノートと同じ形（8.138）
double ChordRegion::getStartBeats() const  { return state[IDs::chordRegionStartBeats]; }
double ChordRegion::getLengthBeats() const { return state[IDs::chordRegionLengthBeats]; }

void ChordRegion::setStartBeats (double newStartBeats, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::chordRegionStartBeats, juce::jmax (0.0, newStartBeats), undoManager);
}

void ChordRegion::setLengthBeats (double newLengthBeats, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::chordRegionLengthBeats, juce::jmax (0.0, newLengthBeats), undoManager);
}

double ChordRegion::getStartTime() const
{
    return MusicalTime::getSecondsFor (state, getStartBeats());
}

double ChordRegion::getLength() const
{
    // **長さは引き算**（8.137）。テンポの変化点をまたぐ区間でも正しくなります
    const double startBeats = getStartBeats();

    return juce::jmax (0.0, MusicalTime::getSecondsFor (state, startBeats + getLengthBeats())
                             - MusicalTime::getSecondsFor (state, startBeats));
}

void ChordRegion::setStartTime (double newStartTimeSeconds, juce::UndoManager* undoManager)
{
    setStartBeats (MusicalTime::getBeatsFor (state, juce::jmax (0.0, newStartTimeSeconds)), undoManager);
}

void ChordRegion::setLength (double newLengthSeconds, juce::UndoManager* undoManager)
{
    const double startSeconds = getStartTime();
    const double endBeats = MusicalTime::getBeatsFor (state,
                                                       startSeconds + juce::jmax (0.0, newLengthSeconds));

    setLengthBeats (endBeats - getStartBeats(), undoManager);
}

Chord ChordRegion::getChord() const
{
    Chord chord;
    chord.root = (int) state.getProperty (IDs::chordRoot, 0);
    chord.root = ((chord.root % 12) + 12) % 12;

    const int typeIndex = (int) state.getProperty (IDs::chordType, 0);
    chord.type = (typeIndex >= 0 && typeIndex < (int) ChordType::NumTypes)
                    ? (ChordType) typeIndex
                    : ChordType::Maj;   // 宣言のコメント参照

    chord.tensionMask = (int) state.getProperty (IDs::chordTensions, 0);

    const int bass = (int) state.getProperty (IDs::chordBass, -1);
    chord.bass = (bass < 0) ? -1 : (bass % 12);

    return chord;
}

void ChordRegion::setChord (const Chord& newChord, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::chordRoot, ((newChord.root % 12) + 12) % 12, undoManager);
    state.setProperty (IDs::chordType, (int) newChord.type, undoManager);
    state.setProperty (IDs::chordTensions, newChord.tensionMask, undoManager);
    state.setProperty (IDs::chordBass, (newChord.bass < 0) ? -1 : (newChord.bass % 12), undoManager);
}

//==============================================================================
// 仕様書5.5：クリップの分割・結合・複製（Phase 50）

namespace
{
    /** クリップの「次のクリップ」を、同じ種別のものから開始位置の順で探す。 */
    juce::ValueTree findNextClipOfSameType (const juce::ValueTree& clipsNode,
                                             const juce::ValueTree& clipState)
    {
        // 8.142：**アクセサを通すこと**（Phase 180）。`musical`のトラックでは
        // 秒のプロパティが入っていないので、直に読むと全部0になります
        const double thisStart = AudioClip (clipState).getStartTime();

        juce::ValueTree best;
        double bestStart = 0.0;

        for (int i = 0; i < clipsNode.getNumChildren(); ++i)
        {
            auto child = clipsNode.getChild (i);

            if (child == clipState || ! child.hasType (clipState.getType()))
                continue;

            const double start = AudioClip (child).getStartTime();

            if (start < thisStart)
                continue;

            if (! best.isValid() || start < bestStart)
            {
                best = child;
                bestStart = start;
            }
        }

        return best;
    }
}

bool Track::splitClipAt (const juce::ValueTree& clipState, double timeSeconds,
                          juce::UndoManager* undoManager)
{
    auto clipsNode = state.getChildWithName (IDs::CLIPS);

    if (! clipsNode.isValid() || ! clipState.isValid())
        return false;

    // 8.142：**アクセサを通すこと**（Phase 180／1.27）
    const double startTime = AudioClip (clipState).getStartTime();
    const double length = clipState.getProperty (IDs::clipLength, 0.0);
    const double offset = clipState.getProperty (IDs::clipOffset, 0.0);

    // 端ちょうどでは割らない。長さ0のクリップができてしまう
    constexpr double minimumPiece = 0.01;

    if (timeSeconds <= startTime + minimumPiece || timeSeconds >= startTime + length - minimumPiece)
        return false;

    const double firstLength = timeSeconds - startTime;

    // **中身ごと複製してから、見せる窓だけを変える。**
    // 空のクリップを作って中身を写す方式だと、MIDIのノートやCC、
    // オーディオのヒットポイントを写し忘れる（1.18と同じ形の事故）。
    auto secondState = clipState.createCopy();
    secondState.setProperty (IDs::clipId, juce::Uuid().toString(), nullptr);
    secondState.setProperty (IDs::clipLength, length - firstLength, nullptr);

    // 8.149：**`firstLength`はタイムラインの秒**（Phase 187／8.48）。
    // オフセットはソースの秒なので、伸縮ぶんで割ってから足すこと
    // ——割らないと、伸ばしたクリップを割った後半が**別の場所から鳴ります**
    secondState.setProperty (IDs::clipOffset,
                              offset + firstLength / AudioClip (clipState).getStretch(), nullptr);

    // 仕様書5.5：フェードは割れ目に残さない。
    // 前半の終わりと後半の始まりにフェードが残ると、割った場所で音が凹む
    secondState.setProperty (IDs::clipFadeIn, 0.0, nullptr);

    juce::ValueTree firstState (clipState);
    firstState.setProperty (IDs::clipLength, firstLength, undoManager);
    firstState.setProperty (IDs::clipFadeOut, 0.0, undoManager);

    // 8.142：**繋いでから位置を書くこと**（Phase 180）。切り離されたままだと
    // トラックの基準へ辿り着けず、`musical`のトラックでも秒で書いてしまいます
    clipsNode.addChild (secondState, -1, undoManager);
    AudioClip (secondState).setStartTime (timeSeconds, undoManager);
    return true;
}

juce::ValueTree Track::duplicateClip (const juce::ValueTree& clipState, juce::UndoManager* undoManager)
{
    auto clipsNode = state.getChildWithName (IDs::CLIPS);

    if (! clipsNode.isValid() || ! clipState.isValid())
        return {};

    // 8.142：**アクセサを通すこと**（Phase 180／1.27）
    const double startTime = AudioClip (clipState).getStartTime();
    const double length = clipState.getProperty (IDs::clipLength, 0.0);

    auto copy = clipState.createCopy();
    copy.setProperty (IDs::clipId, juce::Uuid().toString(), nullptr);

    // 8.78：**複製したものはグループに入れない**（Phase 118）。
    // 同じグループのまま増やすと、**1つ選んだだけで元も複製も全部選ばれます**。
    // 必要なら複製した側をもう一度Gでまとめてください
    copy.removeProperty (IDs::clipGroupId, nullptr);

    // 8.142：**繋いでから位置を書く**（`splitClipAt()`と同じ理由。Phase 180）
    clipsNode.addChild (copy, -1, undoManager);
    AudioClip (copy).setStartTime (startTime + length, undoManager);
    return copy;
}

juce::ValueTree Track::addClipCopy (const juce::ValueTree& clipState, double startTimeSeconds,
                                     juce::UndoManager* undoManager)
{
    auto clipsNode = state.getChildWithName (IDs::CLIPS);

    if (! clipsNode.isValid() || ! clipState.isValid())
        return {};

    auto copy = clipState.createCopy();

    // **IDは振り直すこと**（`duplicateClip()`と同じ理由。1.32）
    copy.setProperty (IDs::clipId, juce::Uuid().toString(), nullptr);

    // 8.78：グループも引き継がない（`duplicateClip()`と同じ理由。Phase 118）
    copy.removeProperty (IDs::clipGroupId, nullptr);

    // 8.142：**繋いでから位置を書く**（`splitClipAt()`と同じ理由。Phase 180）。
    // クリップボードから戻す経路なので、元のトラックの基準は残っていません
    clipsNode.addChild (copy, -1, undoManager);
    AudioClip (copy).setStartTime (juce::jmax (0.0, startTimeSeconds), undoManager);
    return copy;
}

bool Track::mergeClipWithNext (const juce::ValueTree& clipState, juce::UndoManager* undoManager,
                                juce::String& reasonIfFailed)
{
    auto clipsNode = state.getChildWithName (IDs::CLIPS);

    if (! clipsNode.isValid() || ! clipState.isValid())
        return false;

    auto nextState = findNextClipOfSameType (clipsNode, clipState);

    if (! nextState.isValid())
    {
        reasonIfFailed = juce::String::fromUTF8 (u8"後ろに結合できるクリップがありません。");
        return false;
    }

    const double thisStart = AudioClip (clipState).getStartTime();   // 8.142
    const double thisLength = clipState.getProperty (IDs::clipLength, 0.0);
    const double thisOffset = clipState.getProperty (IDs::clipOffset, 0.0);
    const double nextStart = AudioClip (nextState).getStartTime();   // 8.142
    const double nextLength = nextState.getProperty (IDs::clipLength, 0.0);
    const double nextOffset = nextState.getProperty (IDs::clipOffset, 0.0);

    // 離れているものは結合しない（間の無音を勝手に埋めることになるため）
    constexpr double tolerance = 0.001;

    if (nextStart > thisStart + thisLength + tolerance)
    {
        reasonIfFailed = juce::String::fromUTF8 (u8"隣り合っていないクリップは結合できません。");
        return false;
    }

    if (clipState.hasType (IDs::AUDIOCLIP))
    {
        // **オーディオは「割ったものを戻す」場合しか結合できない。**
        // 別のファイルや飛んだ位置を1本にするには、音を書き出し直す処理が要る。
        const juce::String thisSource = clipState.getProperty (IDs::clipSourceFilePath, {});
        const juce::String nextSource = nextState.getProperty (IDs::clipSourceFilePath, {});

        if (thisSource != nextSource)
        {
            reasonIfFailed = juce::String::fromUTF8 (u8"違うファイルのオーディオクリップは結合できません。");
            return false;
        }

        // 8.149：**伸縮が違うものは結合しない**（Phase 187／8.48）。
        // 1本にすると倍率は1つしか持てないので、**どちらかの長さが変わります**
        const double thisStretch = AudioClip (clipState).getStretch();
        const double nextStretch = AudioClip (nextState).getStretch();

        if (std::abs (thisStretch - nextStretch) > tolerance)
        {
            reasonIfFailed = utf8 ("伸縮の倍率が違うクリップは結合できません"
                                    "（片方の長さが変わってしまいます）。");
            return false;
        }

        // **ソースの秒で比べること**（8.149）。伸縮していると、
        // タイムラインの長さとソースの長さは違います
        if (std::abs (nextOffset - (thisOffset + thisLength / thisStretch)) > tolerance)
        {
            reasonIfFailed = juce::String::fromUTF8 (u8"元のファイルの中で連続していないため、結合できません。");
            return false;
        }
    }

    const double newEnd = juce::jmax (thisStart + thisLength, nextStart + nextLength);

    if (clipState.hasType (IDs::MIDICLIP))
    {
        // 次のクリップのノートを、こちらの中身の座標へ直して移す。
        //
        //   タイムライン上の時刻 = クリップの開始 + （ノートの時刻 - オフセット）  … 1.14
        //
        // 同じ時刻に鳴らすには、こちらの中身の時刻をこう置き直せばよい。
        const double shift = (nextStart - nextOffset) - (thisStart - thisOffset);

        auto thisNotes = clipState.getChildWithName (IDs::NOTES);
        auto nextNotes = nextState.getChildWithName (IDs::NOTES);

        if (thisNotes.isValid() && nextNotes.isValid())
        {
            for (int i = 0; i < nextNotes.getNumChildren(); ++i)
            {
                auto note = nextNotes.getChild (i).createCopy();
                const double noteTime = note.getProperty (IDs::noteStartTime, 0.0);

                note.setProperty (IDs::noteStartTime, noteTime + shift, nullptr);
                thisNotes.addChild (note, -1, undoManager);
            }
        }

        // 仕様書5.3.3：CCも一緒に移す。**忘れるとカーブが黙って消える**
        // （ノートだけ見ていると気づけない種類の欠落。1.9と同じ性質）。
        auto nextEvents = nextState.getChildWithName (IDs::CCEVENTS);

        if (nextEvents.isValid() && nextEvents.getNumChildren() > 0)
        {
            // 波括弧で書くこと（丸括弧だと関数宣言に化ける。最も苛立たしい解析）
            MidiClip merged { juce::ValueTree (clipState) };

            auto thisEvents = merged.state.getChildWithName (IDs::CCEVENTS);

            if (! thisEvents.isValid())
            {
                thisEvents = juce::ValueTree (IDs::CCEVENTS);
                merged.state.addChild (thisEvents, -1, undoManager);
            }

            for (int i = 0; i < nextEvents.getNumChildren(); ++i)
            {
                auto event = nextEvents.getChild (i).createCopy();
                const double eventTime = event.getProperty (IDs::ccTime, 0.0);

                event.setProperty (IDs::ccTime, eventTime + shift, nullptr);
                thisEvents.addChild (event, -1, undoManager);
            }

            // **CCは時刻の昇順であることが前提**（MidiEditModel.h）。混ぜたら並べ直す
            merged.sortCCEvents (undoManager);

            // レーンの構成も引き継ぐ（イベントはあるのにレーンが無いと画面に出ない）
            merged.ensureCCLanesForExistingEvents (undoManager);
        }
    }

    juce::ValueTree firstState (clipState);
    firstState.setProperty (IDs::clipLength, newEnd - thisStart, undoManager);
    firstState.setProperty (IDs::clipFadeOut, nextState.getProperty (IDs::clipFadeOut, 0.0), undoManager);

    clipsNode.removeChild (nextState, undoManager);
    return true;
}

//==============================================================================
juce::String Track::getClipGroupId (const juce::ValueTree& clipState)
{
    return clipState[IDs::clipGroupId];
}

void Track::setClipGroupId (const juce::ValueTree& clipState, const juce::String& groupId,
                             juce::UndoManager* undoManager)
{
    // 8.78：**解除は「消す」**（Phase 118）。空文字を入れて残すと、
    // 保存したファイルに意味の無いプロパティが積み上がる
    if (groupId.isEmpty())
        juce::ValueTree (clipState).removeProperty (IDs::clipGroupId, undoManager);
    else
        juce::ValueTree (clipState).setProperty (IDs::clipGroupId, groupId, undoManager);
}

//==============================================================================
Note::Note (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
    jassert (state.hasType (IDs::NOTE));
}

Note Note::create (int pitch, int velocity, double startBeats,
                    double lengthBeats, juce::UndoManager* undoManager)
{
    // 8.138：**拍で作ります**（Phase 176／8.105の宿題3）。
    //
    // ここで作るツリーは**まだどこにも繋がっていません**。秒で受け取ると、
    // 換算のためにテンポの表を引こうとして**既定の120BPMに落ちます**
    // （`MusicalTime.h`の「登録が無いとき」）。
    // **拍なら換算が要らない**ので、繋ぐ前でも正しく作れます
    juce::ValueTree n (IDs::NOTE);

    n.setProperty (IDs::notePitch, pitch, undoManager);
    n.setProperty (IDs::noteVelocity, velocity, undoManager);
    n.setProperty (IDs::noteStartBeats, juce::jmax (0.0, startBeats), undoManager);
    n.setProperty (IDs::noteLengthBeats, juce::jmax (0.0, lengthBeats), undoManager);

    return Note (n);
}

int Note::getPitch() const        { return state[IDs::notePitch]; }
int Note::getVelocity() const     { return state[IDs::noteVelocity]; }

//==============================================================================
// 8.138：**保存されているのは拍のほう**（Phase 176／8.105の宿題3）。
//
// Phase 175では上下が逆でした——秒が保存されていて、拍が換算でした。
// **入れ替わったのはこの中身だけ**で、呼び出し側は1箇所も変わっていません。
// それが宿題3を「アクセサを通す」から始めた理由です（8.137）。

double Note::getStartBeats() const  { return state[IDs::noteStartBeats]; }
double Note::getLengthBeats() const { return state[IDs::noteLengthBeats]; }

void Note::setStartBeats (double newStartBeats, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::noteStartBeats, juce::jmax (0.0, newStartBeats), undoManager);
}

void Note::setLengthBeats (double newLengthBeats, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::noteLengthBeats, juce::jmax (0.0, newLengthBeats), undoManager);
}

double Note::getStartTime() const
{
    return MusicalTime::getSecondsFor (state, getStartBeats());
}

double Note::getLength() const
{
    // **長さは引き算**（8.137）。拍数をそのままテンポで掛けると、
    // 途中に変化点をまたぐノートで合わなくなります
    const double startBeats = getStartBeats();

    return juce::jmax (0.0, MusicalTime::getSecondsFor (state, startBeats + getLengthBeats())
                             - MusicalTime::getSecondsFor (state, startBeats));
}

void Note::setPitch (int newPitch, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::notePitch, newPitch, undoManager);
}

void Note::setVelocity (int newVelocity, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::noteVelocity, newVelocity, undoManager);
}

void Note::setStartTime (double newStartTimeSeconds, juce::UndoManager* undoManager)
{
    // **長さは触りません。** 秒での長さは「終わりの拍 - 始まりの拍」から
    // 出てくるので、始まりを動かせば秒の長さも自動的にそのテンポのものになります
    setStartBeats (MusicalTime::getBeatsFor (state, juce::jmax (0.0, newStartTimeSeconds)), undoManager);
}

void Note::setLength (double newLengthSeconds, juce::UndoManager* undoManager)
{
    const double startSeconds = getStartTime();
    const double endBeats = MusicalTime::getBeatsFor (state,
                                                       startSeconds + juce::jmax (0.0, newLengthSeconds));

    setLengthBeats (endBeats - getStartBeats(), undoManager);
}

//==============================================================================
MidiClip::MidiClip (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
    jassert (state.hasType (IDs::MIDICLIP));
}

MidiClip MidiClip::create (double startTimeSeconds, double lengthSeconds, juce::UndoManager* undoManager)
{
    juce::ValueTree c (IDs::MIDICLIP);

    c.setProperty (IDs::clipId, juce::Uuid().toString(), undoManager);
    c.setProperty (IDs::clipStartTime, startTimeSeconds, undoManager);
    c.setProperty (IDs::clipLength, lengthSeconds, undoManager);

    juce::ValueTree notes (IDs::NOTES);
    c.addChild (notes, -1, undoManager);

    return MidiClip (c);
}

juce::String MidiClip::getId() const  { return state[IDs::clipId]; }
double MidiClip::getStartTime() const { return state[IDs::clipStartTime]; }
double MidiClip::getLength() const    { return state[IDs::clipLength]; }
double MidiClip::getOffset() const    { return state[IDs::clipOffset]; }

void MidiClip::setOffset (double newOffsetSeconds, juce::UndoManager* undoManager)
{
    // 負のオフセットは「中身の手前」を指してしまい意味を持たない
    state.setProperty (IDs::clipOffset, juce::jmax (0.0, newOffsetSeconds), undoManager);
}

void MidiClip::setStartTime (double newStartTimeSeconds, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::clipStartTime, newStartTimeSeconds, undoManager);
}

void MidiClip::setLength (double newLengthSeconds, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::clipLength, newLengthSeconds, undoManager);
}

Note MidiClip::addNote (int pitch, int velocity, double startTimeSeconds,
                         double lengthSeconds, juce::UndoManager* undoManager)
{
    // 8.138：**古い形のノートを作ります**（Phase 176）。`<MIDICLIP>`は
    // 読み込み時の変換にしか出てこないので、中身は**秒のまま**です
    // （`growToFitNotes()`の説明）。**新しく呼ぶところはありません。**
    juce::ValueTree n (IDs::NOTE);

    n.setProperty (IDs::notePitch, pitch, undoManager);
    n.setProperty (IDs::noteVelocity, velocity, undoManager);
    n.setProperty (IDs::noteStartTime, startTimeSeconds, undoManager);
    n.setProperty (IDs::noteLength, lengthSeconds, undoManager);

    state.getChildWithName (IDs::NOTES).addChild (n, -1, undoManager);
    return Note (n);
}

void MidiClip::removeNote (const Note& note, juce::UndoManager* undoManager)
{
    auto notesNode = state.getChildWithName (IDs::NOTES);
    notesNode.removeChild (note.state, undoManager);
}

int MidiClip::getNumNotes() const
{
    return state.getChildWithName (IDs::NOTES).getNumChildren();
}

Note MidiClip::getNote (int index) const
{
    return Note (state.getChildWithName (IDs::NOTES).getChild (index));
}

bool MidiClip::transposeNotes (int semitones, juce::UndoManager* undoManager)
{
    if (semitones == 0)
        return false;

    const int numNotes = getNumNotes();

    if (numNotes == 0)
        return false;

    // 8.77：**先に全部見てから動かす**（Phase 117／改善案㉝）。
    //
    // 1つでも0〜127を外れるなら何もしません。外れるものだけ止めると
    // **和音の形が崩れ**、12半音上げて戻しても元に戻らなくなります
    // （直しようがない壊れ方なので、まとめて断るほうが親切）。
    for (int i = 0; i < numNotes; ++i)
    {
        const int moved = getNote (i).getPitch() + semitones;

        if (moved < 0 || moved > 127)
            return false;
    }

    for (int i = 0; i < numNotes; ++i)
    {
        auto note = getNote (i);
        note.setPitch (note.getPitch() + semitones, undoManager);
    }

    return true;
}

//==============================================================================
// 仕様書5.3.2：ドラムマップ
//==============================================================================

DrumMapEntry::DrumMapEntry (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
}

int DrumMapEntry::getMidiNote() const          { return state[IDs::drumMidiNote]; }
juce::String DrumMapEntry::getPartName() const { return state[IDs::drumPartName]; }
int DrumMapEntry::getMuteGroup() const         { return state[IDs::drumMuteGroup]; }
bool DrumMapEntry::isMuted() const             { return state[IDs::drumRowMuted]; }

void DrumMapEntry::setMuteGroup (int newGroup, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::drumMuteGroup, juce::jmax (0, newGroup), undoManager);
}

void DrumMapEntry::setPartName (const juce::String& newName, juce::UndoManager* undoManager)
{
    // 空の名前は受け取らない（行の見出しが消えて、どの音か分からなくなる）。
    // 呼び出し側でも弾いているが、**モデル側でも守る**（入口が増えても崩れないように）
    const auto trimmed = newName.trim();

    if (trimmed.isEmpty())
        return;

    state.setProperty (IDs::drumPartName, trimmed, undoManager);
}

void DrumMapEntry::setMuted (bool shouldBeMuted, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::drumRowMuted, shouldBeMuted, undoManager);
}

//==============================================================================

DrumMap::DrumMap (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
}

juce::String DrumMap::getId() const   { return state[IDs::drumMapId]; }
juce::String DrumMap::getName() const { return state[IDs::drumMapName]; }

int DrumMap::getNumEntries() const
{
    return state.getNumChildren();
}

DrumMapEntry DrumMap::getEntry (int index) const
{
    return DrumMapEntry (state.getChild (index));
}

DrumMapEntry DrumMap::findEntry (int midiNote) const
{
    return DrumMapEntry (state.getChildWithProperty (IDs::drumMidiNote, midiNote));
}

DrumMapEntry DrumMap::addEntry (int midiNote, const juce::String& partName, int muteGroup,
                                 juce::UndoManager* undoManager)
{
    auto existing = state.getChildWithProperty (IDs::drumMidiNote, midiNote);

    if (existing.isValid())
        return DrumMapEntry (existing);

    juce::ValueTree entry (IDs::DRUMENTRY);
    entry.setProperty (IDs::drumMidiNote, midiNote, nullptr);
    entry.setProperty (IDs::drumPartName, partName, nullptr);
    entry.setProperty (IDs::drumMuteGroup, muteGroup, nullptr);
    entry.setProperty (IDs::drumRowMuted, false, nullptr);

    state.addChild (entry, -1, undoManager);

    return DrumMapEntry (entry);
}

bool DrumMap::isNoteMuted (int midiNote) const
{
    auto entry = findEntry (midiNote);

    // マップに載っていない音は、ドラムマップの管轄外なので鳴らす
    return entry.state.isValid() && entry.isMuted();
}

int DrumMap::getMuteGroupForNote (int midiNote) const
{
    auto entry = findEntry (midiNote);

    return entry.state.isValid() ? entry.getMuteGroup() : 0;
}

//==============================================================================

void applyChokeGroups (std::vector<ChokeNote>& notes)
{
    // グループごとに「開始時刻の昇順」で見て、次のノートが始まる位置で前を止める。
    // グループ0は「グループなし」なので対象外。
    std::map<int, std::vector<size_t>> byGroup;

    for (size_t i = 0; i < notes.size(); ++i)
        if (notes[i].muteGroup > 0)
            byGroup[notes[i].muteGroup].push_back (i);

    for (auto& [group, indices] : byGroup)
    {
        juce::ignoreUnused (group);

        std::sort (indices.begin(), indices.end(),
                    [&notes] (size_t a, size_t b) { return notes[a].startTime < notes[b].startTime; });

        for (size_t n = 0; n + 1 < indices.size(); ++n)
        {
            auto& current = notes[indices[n]];
            const auto& next = notes[indices[n + 1]];

            // 同時に始まるノートどうしでは切らない（和音のように鳴らしたい場合がある）
            if (next.startTime > current.startTime)
                current.endTime = juce::jmin (current.endTime, next.startTime);
        }
    }
}

//==============================================================================

juce::String Track::getDrumMapId() const
{
    return state[IDs::trackDrumMapId];
}

void Track::setDrumMapId (const juce::String& mapId, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::trackDrumMapId, mapId, undoManager);
}

bool Track::isDrumEditorEnabled() const
{
    return state[IDs::trackDrumEditor];
}

void Track::setDrumEditorEnabled (bool shouldBeEnabled, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::trackDrumEditor, shouldBeEnabled, undoManager);
}

bool Track::isWatermarkVisible() const
{
    return state[IDs::trackWatermark];
}

void Track::setWatermarkVisible (bool shouldBeVisible)
{
    // **UndoManagerを渡しません**（8.14の刻みと同じ：画面の見え方は履歴に混ぜない）
    state.setProperty (IDs::trackWatermark, shouldBeVisible, nullptr);
}

bool Track::isCollapsed() const
{
    return state[IDs::trackCollapsed];
}

void Track::setCollapsed (bool shouldBeCollapsed)
{
    // **UndoManagerを渡しません**（見え方は履歴に混ぜない。`setWatermarkVisible()`と同じ）
    state.setProperty (IDs::trackCollapsed, shouldBeCollapsed, nullptr);
}

int Track::getCustomRowHeight() const
{
    // 8.62：**0は「まだ決めていない」**（Phase 100）。既定を決めるのは画面側1箇所
    return (int) state.getProperty (IDs::trackHeight, 0);
}

void Track::setCustomRowHeight (int newHeight)
{
    // **UndoManagerを渡しません**（見え方は履歴に混ぜない。`setCollapsed()`と同じ）。
    // 保存はされます（プロパティなので、そのままXMLへ載る）
    state.setProperty (IDs::trackHeight, juce::jmax (0, newHeight), nullptr);
}

juce::String Track::getParentFolderId() const
{
    return state[IDs::trackParentFolder].toString();
}

void Track::setParentFolderId (const juce::String& folderId, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::trackParentFolder, folderId, undoManager);
}

//==============================================================================
// 8.143：パラアウトの受け皿（Phase 181／改善案⑮）

juce::String Track::getDrumOutSourceTrackId() const
{
    return state[IDs::drumOutSourceTrackId].toString();
}

int Track::getDrumOutSourceBus() const
{
    // **0は返しません。** 0番のバスは音源トラック自身が受けるので、
    // 受け皿が0を指していたら「まだ決まっていない」のと同じです
    return juce::jmax (0, (int) state.getProperty (IDs::drumOutSourceBus, 0));
}

void Track::setDrumOutSource (const juce::String& sourceTrackId, int busIndex,
                               juce::UndoManager* undoManager)
{
    state.setProperty (IDs::drumOutSourceTrackId, sourceTrackId, undoManager);
    state.setProperty (IDs::drumOutSourceBus, juce::jmax (0, busIndex), undoManager);
}

//==============================================================================
// 8.142：トラックの時間の基準（Phase 180／8.105の宿題3）

TimeBase Track::getTimeBase() const
{
    // **入っていなければ種別ごとの既定。** 古いプロジェクトはこのプロパティが
    // 無いだけなので、読み替えは要りません（8.102のTEMPOMAPと同じ形）
    if (! state.hasProperty (IDs::trackTimeBase))
        return defaultTimeBaseFor (getType());

    return timeBaseFromString (state[IDs::trackTimeBase].toString());
}

void Track::setTimeBase (TimeBase newBase, juce::UndoManager* undoManager)
{
    const TimeBase oldBase = getTimeBase();

    if (newBase == oldBase)
        return;

    // **持ち替えは、書き替える前に読むこと。**
    //
    // `getStartTime()`は「いまの基準」で答えます。先にプロパティを書き替えると、
    // まだ拍の入っていないクリップを`musical`として読むことになり、
    // **全部が曲の頭へ寄ります**。だから秒を全部先に控えておきます
    auto clipsNode = state.getChildWithName (IDs::CLIPS);

    std::vector<double> startSeconds;
    startSeconds.reserve ((size_t) clipsNode.getNumChildren());

    for (int i = 0; i < clipsNode.getNumChildren(); ++i)
        startSeconds.push_back (AudioClip (clipsNode.getChild (i)).getStartTime());

    state.setProperty (IDs::trackTimeBase, timeBaseToString (newBase), undoManager);

    // 控えた秒を、新しい基準で置き直す。**音は動きません**——
    // いまの位置をそのまま写すだけです
    for (int i = 0; i < clipsNode.getNumChildren(); ++i)
    {
        AudioClip clip (clipsNode.getChild (i));

        clip.setStartTime (startSeconds[(size_t) i], undoManager);

        // **古いほうのプロパティは消すこと。** 残すと、同じことを表す値が
        // 2つ並んだまま片方だけ動くようになります（1.27）
        clip.state.removeProperty (newBase == TimeBase::musical ? IDs::clipStartTime
                                                                : IDs::clipStartBeats,
                                    undoManager);
    }
}

//==============================================================================

DrumMap ProjectModel::findDrumMap (const juce::String& mapId) const
{
    if (mapId.isEmpty())
        return DrumMap (juce::ValueTree());

    return DrumMap (state.getChildWithName (IDs::DRUMMAPS)
                          .getChildWithProperty (IDs::drumMapId, mapId));
}

DrumMap ProjectModel::getDrumMapForTrack (const Track& track) const
{
    if (! track.state.isValid())
        return DrumMap (juce::ValueTree());

    return findDrumMap (track.getDrumMapId());
}

DrumMap ProjectModel::getOrCreateDefaultDrumMap()
{
    auto mapsNode = state.getChildWithName (IDs::DRUMMAPS);

    if (! mapsNode.isValid())
    {
        // Phase 25より前に保存されたプロジェクトには無いため、ここで作る
        // （仕様書7章「プロジェクトファイルの後方互換性」）
        mapsNode = juce::ValueTree (IDs::DRUMMAPS);
        state.addChild (mapsNode, -1, nullptr);
    }

    if (mapsNode.getNumChildren() > 0)
        return DrumMap (mapsNode.getChild (0));

    juce::ValueTree mapTree (IDs::DRUMMAP);
    mapTree.setProperty (IDs::drumMapId, juce::Uuid().toString(), nullptr);
    mapTree.setProperty (IDs::drumMapName, "General MIDI", nullptr);
    mapsNode.addChild (mapTree, -1, nullptr);

    DrumMap map (mapTree);

    // General MIDIのパーカッション配列（仕様書5.3.2の例にある36=Kick、38=Snareを含む）。
    //
    // 8.119：**チョークグループは全部「なし」で配る**（Phase 154／改善案34）。
    //
    // Phase 20からは、ハイハット（42・44・46）にグループ1、コンガ（62・63）に
    // グループ2を**最初から入れて**いた。実機の挙動（オープンを鳴らした後に
    // クローズを叩くとオープンが止まる）に寄せたもの。
    //
    // ただし**音源によって、その並びが同じとは限らない**。合っていない音源だと
    // 「入れたはずのノートが途中で切れる」という形で出て、**原因がドラムマップ側に
    // あることに気づけない**（グループはドラムマップの表を開かないと見えない）。
    //
    // **既定は何もしない。** 要るときにドラムエディタで付ければよく、
    // 付けたことは自分で覚えている。`muteGroup`の仕組み自体はそのまま残っている
    // （`applyChokeGroups()`。保存済みのプロジェクトの値も触らない）。
    struct DefaultEntry { int note; const char* name; int muteGroup; };

    static const DefaultEntry defaults[] = {
        { 35, "Acoustic Bass Drum", 0 },
        { 36, "Bass Drum (Kick)",   0 },
        { 37, "Side Stick",         0 },
        { 38, "Acoustic Snare",     0 },
        { 39, "Hand Clap",          0 },
        { 40, "Electric Snare",     0 },
        { 41, "Low Floor Tom",      0 },
        { 42, "Closed Hi-Hat",      0 },
        { 43, "High Floor Tom",     0 },
        { 44, "Pedal Hi-Hat",       0 },
        { 45, "Low Tom",            0 },
        { 46, "Open Hi-Hat",        0 },
        { 47, "Low-Mid Tom",        0 },
        { 48, "Hi-Mid Tom",         0 },
        { 49, "Crash Cymbal 1",     0 },
        { 50, "High Tom",           0 },
        { 51, "Ride Cymbal 1",      0 },
        { 52, "Chinese Cymbal",     0 },
        { 53, "Ride Bell",          0 },
        { 54, "Tambourine",         0 },
        { 55, "Splash Cymbal",      0 },
        { 56, "Cowbell",            0 },
        { 57, "Crash Cymbal 2",     0 },
        { 59, "Ride Cymbal 2",      0 },
        { 60, "Hi Bongo",           0 },
        { 61, "Low Bongo",          0 },
        { 62, "Mute Hi Conga",      0 },
        { 63, "Open Hi Conga",      0 },
        { 64, "Low Conga",          0 },
        { 65, "High Timbale",       0 },
        { 66, "Low Timbale",        0 },
        { 69, "Cabasa",             0 },
        { 70, "Maracas",            0 },
        { 75, "Claves",             0 },
        { 76, "Hi Wood Block",      0 },
        { 77, "Low Wood Block",     0 }
    };

    for (const auto& entry : defaults)
        map.addEntry (entry.note, entry.name, entry.muteGroup, nullptr);

    return map;
}

//==============================================================================
// 仕様書5.3.4：グルーヴテンプレート
//==============================================================================

GroovePoint::GroovePoint (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
}

int GroovePoint::getGridPosition() const     { return state[IDs::groovePosition]; }
double GroovePoint::getTimingOffsetMs() const { return state[IDs::grooveTimingOffsetMs]; }

// 8.135：マス何個ぶんのズレ（Phase 173／8.105の宿題2）。
// **無いときは0を返すだけ**にしてある——古いプロジェクトの換算は
// `GrooveTemplate::getPointOffsetGrids()`が持っている（抽出時のテンポが要るため）
double GroovePoint::getTimingOffsetGrids() const { return state[IDs::grooveTimingOffsetGrids]; }

float GroovePoint::getVelocityScale() const
{
    // 属性が無い場合に0倍になると音が消えてしまうので、既定は等倍にする
    return state.hasProperty (IDs::grooveVelocityScale) ? (float) state[IDs::grooveVelocityScale] : 1.0f;
}

//==============================================================================

GrooveTemplate::GrooveTemplate (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
}

juce::String GrooveTemplate::getId() const   { return state[IDs::grooveId]; }
juce::String GrooveTemplate::getName() const { return state[IDs::grooveName]; }

void GrooveTemplate::setName (const juce::String& newName, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::grooveName, newName, undoManager);
}

int GrooveTemplate::getGridDivision() const
{
    return juce::jmax (1, (int) state[IDs::grooveGridDivision]);
}

int GrooveTemplate::getPatternLength() const
{
    return juce::jmax (1, (int) state[IDs::groovePatternLength]);
}

double GrooveTemplate::getSourceTempo() const
{
    const double tempo = state[IDs::grooveSourceTempo];

    return tempo > 0.0 ? tempo : 120.0;
}

int GrooveTemplate::getNumPoints() const
{
    return state.getNumChildren();
}

GroovePoint GrooveTemplate::getPoint (int index) const
{
    return GroovePoint (state.getChild (index));
}

GroovePoint GrooveTemplate::findPoint (int gridPosition) const
{
    return GroovePoint (state.getChildWithProperty (IDs::groovePosition, gridPosition));
}

double GrooveTemplate::getPointOffsetGrids (const GroovePoint& point) const
{
    if (! point.state.isValid())
        return 0.0;

    // 8.135：**新しい形が入っていればそれを使う**（Phase 173／8.105の宿題2）
    if (point.state.hasProperty (IDs::grooveTimingOffsetGrids))
        return point.getTimingOffsetGrids();

    // 古いプロジェクト（ミリ秒で保存されたもの）。**抽出時のテンポで1マスの長さを出し、
    // その何個ぶんかへ直す**——ここが唯一`sourceTempo`を使う場所です
    const double secondsPerBeat = 60.0 / juce::jmax (1.0, getSourceTempo());
    const double gridSeconds = secondsPerBeat / juce::jmax (1, getGridDivision());

    if (gridSeconds <= 0.0)
        return 0.0;

    return (point.getTimingOffsetMs() / 1000.0) / gridSeconds;
}

GroovePoint GrooveTemplate::addPoint (int gridPosition, double timingOffsetGrids, float velocityScale,
                                       juce::UndoManager* undoManager)
{
    // 8.135：**ミリ秒も一緒に書く**（Phase 173／8.105の宿題2）。
    // 古い版で開いたときに、だいたい合った揺れになるようにするためです
    // （抽出時のテンポでの換算なので、そのテンポで使うぶんには一致します）
    const double secondsPerBeat = 60.0 / juce::jmax (1.0, getSourceTempo());
    const double gridSeconds = secondsPerBeat / juce::jmax (1, getGridDivision());
    const double timingOffsetMs = timingOffsetGrids * gridSeconds * 1000.0;

    auto existing = state.getChildWithProperty (IDs::groovePosition, gridPosition);

    if (existing.isValid())
    {
        existing.setProperty (IDs::grooveTimingOffsetGrids, timingOffsetGrids, undoManager);
        existing.setProperty (IDs::grooveTimingOffsetMs, timingOffsetMs, undoManager);
        existing.setProperty (IDs::grooveVelocityScale, velocityScale, undoManager);
        return GroovePoint (existing);
    }

    juce::ValueTree point (IDs::GROOVEPOINT);
    point.setProperty (IDs::groovePosition, gridPosition, nullptr);
    point.setProperty (IDs::grooveTimingOffsetGrids, timingOffsetGrids, nullptr);
    point.setProperty (IDs::grooveTimingOffsetMs, timingOffsetMs, nullptr);
    point.setProperty (IDs::grooveVelocityScale, velocityScale, nullptr);

    state.addChild (point, -1, undoManager);

    return GroovePoint (point);
}

//==============================================================================

juce::String MidiClip::getGrooveTemplateId() const
{
    return state[IDs::clipGrooveTemplateId];
}

void MidiClip::setGrooveTemplateId (const juce::String& templateId, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::clipGrooveTemplateId, templateId, undoManager);
}

//==============================================================================

juce::ValueTree ProjectModel::getOrCreateGrooveTemplatesNode()
{
    auto node = state.getChildWithName (IDs::GROOVETEMPLATES);

    if (! node.isValid())
    {
        // Phase 24より前に保存されたプロジェクトには無いため、ここで作る
        // （仕様書7章「プロジェクトファイルの後方互換性」）
        node = juce::ValueTree (IDs::GROOVETEMPLATES);
        state.addChild (node, -1, nullptr);
    }

    return node;
}

int ProjectModel::getNumGrooveTemplates() const
{
    return state.getChildWithName (IDs::GROOVETEMPLATES).getNumChildren();
}

GrooveTemplate ProjectModel::getGrooveTemplate (int index) const
{
    return GrooveTemplate (state.getChildWithName (IDs::GROOVETEMPLATES).getChild (index));
}

GrooveTemplate ProjectModel::findGrooveTemplate (const juce::String& templateId) const
{
    if (templateId.isEmpty())
        return GrooveTemplate (juce::ValueTree());

    return GrooveTemplate (state.getChildWithName (IDs::GROOVETEMPLATES)
                                .getChildWithProperty (IDs::grooveId, templateId));
}

GrooveTemplate ProjectModel::addGrooveTemplate (const juce::String& name, int gridDivision,
                                                 int patternLength, double sourceTempo,
                                                 juce::UndoManager* undoManagerToUse)
{
    auto templatesNode = getOrCreateGrooveTemplatesNode();

    juce::ValueTree templateTree (IDs::GROOVETEMPLATE);
    templateTree.setProperty (IDs::grooveId, juce::Uuid().toString(), nullptr);
    templateTree.setProperty (IDs::grooveName, name, nullptr);
    templateTree.setProperty (IDs::grooveGridDivision, juce::jmax (1, gridDivision), nullptr);
    templateTree.setProperty (IDs::groovePatternLength, juce::jmax (1, patternLength), nullptr);
    templateTree.setProperty (IDs::grooveSourceTempo, sourceTempo, nullptr);

    templatesNode.addChild (templateTree, -1, undoManagerToUse);

    return GrooveTemplate (templateTree);
}

void ProjectModel::removeGrooveTemplate (int index, juce::UndoManager* undoManagerToUse)
{
    auto templatesNode = state.getChildWithName (IDs::GROOVETEMPLATES);

    if (templatesNode.isValid())
        templatesNode.removeChild (index, undoManagerToUse);
}

//==============================================================================
// 仕様書5.3.3：CCレーン
//==============================================================================

namespace MidiControllers
{
    juce::String getDisplayName (int controllerNumber)
    {
        if (controllerNumber == pitchBend)       return utf8 ("ピッチベンド");
        if (controllerNumber == channelPressure) return utf8 ("アフタータッチ");

        // よく使うものだけ名前を添える。番号だけだと何のレーンか思い出せないため。
        juce::String name;

        switch (controllerNumber)
        {
            case 1:  name = "Modulation";   break;
            case 7:  name = "Volume";       break;
            case 10: name = "Pan";          break;
            case 11: name = "Expression";   break;
            case 64: name = "Sustain";      break;
            case 71: name = "Resonance";    break;
            case 74: name = "Brightness";   break;
            case 91: name = "Reverb";       break;
            case 93: name = "Chorus";       break;
            default: break;
        }

        auto label = "CC" + juce::String (controllerNumber);

        return name.isEmpty() ? label : label + " " + name;
    }

    int getMaxValue (int controllerNumber)
    {
        // ピッチベンドは14bit。127で決め打ちすると分解能が128分の1になってしまう。
        return controllerNumber == pitchBend ? 16383 : 127;
    }

    int getDefaultValue (int controllerNumber)
    {
        // ピッチベンドだけ「何もしない」が中央値。他は0が「効いていない」状態
        return controllerNumber == pitchBend ? 8192 : 0;
    }

    juce::Array<int> getCommonControllers()
    {
        return { 1, 11, 7, 10, 64, 71, 74, 91, 93, pitchBend, channelPressure };
    }
}

CCEvent::CCEvent (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
}

int CCEvent::getControllerNumber() const { return state[IDs::ccController]; }
int CCEvent::getValue() const            { return state[IDs::ccValue]; }

// 8.138：**保存されているのは拍のほう**（Phase 176／8.105の宿題3）。`Note`と同じ形
double CCEvent::getTimeBeats() const     { return state[IDs::ccTimeBeats]; }

double CCEvent::getTime() const
{
    return MusicalTime::getSecondsFor (state, getTimeBeats());
}

void CCEvent::setValue (int newValue, juce::UndoManager* undoManager)
{
    const int maxValue = MidiControllers::getMaxValue (getControllerNumber());
    state.setProperty (IDs::ccValue, juce::jlimit (0, maxValue, newValue), undoManager);
}

AutomationCurve CCEvent::getCurve() const
{
    // 8.35：**オートメーションと同じ扱い**にしてある（Phase 76）。
    // property が無い（Phase 75以前に保存されたもの）なら直線
    return automationCurveFromString (state[IDs::ccCurve].toString());
}

void CCEvent::setCurve (AutomationCurve newCurve, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::ccCurve, automationCurveToString (newCurve), undoManager);
}

float CCEvent::getCurveAmount() const
{
    // 8.37：**オートメーションの点と同じ扱い**（Phase 77）。入っていなければ0（直線）
    return (float) state.getProperty (IDs::ccCurveAmount, 0.0f);
}

void CCEvent::setCurveAmount (float newAmount, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::ccCurveAmount,
                        juce::jlimit (-maxAutomationCurveAmount, maxAutomationCurveAmount, newAmount),
                        undoManager);
}

void CCEvent::setTimeBeats (double newTimeBeats, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::ccTimeBeats, juce::jmax (0.0, newTimeBeats), undoManager);
}

void CCEvent::setTime (double newTimeSeconds, juce::UndoManager* undoManager)
{
    setTimeBeats (MusicalTime::getBeatsFor (state, juce::jmax (0.0, newTimeSeconds)), undoManager);
}

//==============================================================================

int MidiClip::getNumCCEvents() const
{
    return state.getChildWithName (IDs::CCEVENTS).getNumChildren();
}

CCEvent MidiClip::getCCEvent (int index) const
{
    return CCEvent (state.getChildWithName (IDs::CCEVENTS).getChild (index));
}

int MidiClip::getNumCCEventsFor (int controllerNumber) const
{
    int count = 0;

    for (int i = 0; i < getNumCCEvents(); ++i)
        if (getCCEvent (i).getControllerNumber() == controllerNumber)
            ++count;

    return count;
}

CCEvent MidiClip::getCCEventFor (int controllerNumber, int index) const
{
    int seen = 0;

    for (int i = 0; i < getNumCCEvents(); ++i)
    {
        auto event = getCCEvent (i);

        if (event.getControllerNumber() != controllerNumber)
            continue;

        if (seen == index)
            return event;

        ++seen;
    }

    return CCEvent (juce::ValueTree());
}

CCEvent MidiClip::addCCEvent (int controllerNumber, int value, double timeSeconds,
                               juce::UndoManager* undoManager)
{
    auto eventsNode = state.getChildWithName (IDs::CCEVENTS);

    if (! eventsNode.isValid())
    {
        eventsNode = juce::ValueTree (IDs::CCEVENTS);
        state.addChild (eventsNode, -1, undoManager);
    }

    const double time = juce::jmax (0.0, timeSeconds);
    const int limitedValue = juce::jlimit (0, MidiControllers::getMaxValue (controllerNumber), value);

    // 同じコントローラーの同じ時刻に既にあるなら、積み増さず値を差し替える。
    // 点を置き直すたびに増えていくと、どれが効いているのか分からなくなる。
    for (int i = 0; i < eventsNode.getNumChildren(); ++i)
    {
        CCEvent existing (eventsNode.getChild (i));

        if (existing.getControllerNumber() == controllerNumber
             && std::abs (existing.getTime() - time) < 1.0e-9)
        {
            existing.setValue (limitedValue, undoManager);
            return existing;
        }
    }

    juce::ValueTree eventTree (IDs::CC);
    eventTree.setProperty (IDs::ccController, controllerNumber, nullptr);
    eventTree.setProperty (IDs::ccValue, limitedValue, nullptr);
    eventTree.setProperty (IDs::ccTime, time, nullptr);

    // 時刻順を保つため、挿す位置を探す（末尾に足して後から並べ替えるより、
    // 「常に順序が正しい」ほうが読み出し側で気を遣わずに済む）
    int insertIndex = eventsNode.getNumChildren();

    for (int i = 0; i < eventsNode.getNumChildren(); ++i)
    {
        if (CCEvent (eventsNode.getChild (i)).getTime() > time)
        {
            insertIndex = i;
            break;
        }
    }

    eventsNode.addChild (eventTree, insertIndex, undoManager);

    return CCEvent (eventTree);
}

void MidiClip::removeCCEvent (const CCEvent& event, juce::UndoManager* undoManager)
{
    auto eventsNode = state.getChildWithName (IDs::CCEVENTS);

    if (eventsNode.isValid())
        eventsNode.removeChild (event.state, undoManager);
}

void MidiClip::removeAllCCEventsFor (int controllerNumber, juce::UndoManager* undoManager)
{
    auto eventsNode = state.getChildWithName (IDs::CCEVENTS);

    if (! eventsNode.isValid())
        return;

    // 後ろから消す（前から消すと番号がずれて飛ばしてしまう）
    for (int i = eventsNode.getNumChildren(); --i >= 0;)
        if (CCEvent (eventsNode.getChild (i)).getControllerNumber() == controllerNumber)
            eventsNode.removeChild (i, undoManager);
}

CCEvent MidiClip::findNextCCEvent (const CCEvent& event) const
{
    if (! event.state.isValid())
        return CCEvent (juce::ValueTree());

    const int controllerNumber = event.getControllerNumber();
    const double time = event.getTime();

    CCEvent best { juce::ValueTree() };

    // **時刻で探すこと。** 並び順は`sortCCEvents()`で保っているが、
    // 同じ時刻に別のコントローラーが挟まっていることがある
    for (int i = 0; i < getNumCCEventsFor (controllerNumber); ++i)
    {
        auto candidate = getCCEventFor (controllerNumber, i);

        if (! candidate.state.isValid() || candidate.state == event.state)
            continue;

        if (candidate.getTime() <= time)
            continue;

        if (! best.state.isValid() || candidate.getTime() < best.getTime())
            best = candidate;
    }

    return best;
}

void MidiClip::sortCCEvents (juce::UndoManager* undoManager)
{
    auto eventsNode = state.getChildWithName (IDs::CCEVENTS);

    if (! eventsNode.isValid())
        return;

    struct TimeComparator
    {
        static int compareElements (const juce::ValueTree& a, const juce::ValueTree& b)
        {
            const double timeA = a[IDs::ccTime];
            const double timeB = b[IDs::ccTime];

            return timeA < timeB ? -1 : (timeA > timeB ? 1 : 0);
        }
    };

    TimeComparator comparator;
    eventsNode.sort (comparator, undoManager, true);
}

int MidiClip::getCCValueAt (int controllerNumber, double contentTimeSeconds) const
{
    int result = MidiControllers::getDefaultValue (controllerNumber);

    // イベントは時刻順に並んでいるので、時刻を過ぎたものを順に上書きしていけばよい。
    // **補間せずステップ状に保つ**のは、MIDIのCCが「次が来るまで値を保つ」ものだから。
    for (int i = 0; i < getNumCCEvents(); ++i)
    {
        auto event = getCCEvent (i);

        if (event.getControllerNumber() != controllerNumber)
            continue;

        if (event.getTime() > contentTimeSeconds)
            break;

        result = event.getValue();
    }

    return result;
}

//==============================================================================
// 仕様書5.3.3：表示するレーンの一覧

juce::ValueTree MidiClip::getOrCreateCCLanesNode (juce::UndoManager* undoManager)
{
    auto lanesNode = state.getChildWithName (IDs::CCLANES);

    if (! lanesNode.isValid())
    {
        lanesNode = juce::ValueTree (IDs::CCLANES);
        state.addChild (lanesNode, -1, undoManager);
    }

    return lanesNode;
}

int MidiClip::getNumCCLanes() const
{
    return state.getChildWithName (IDs::CCLANES).getNumChildren();
}

int MidiClip::getCCLaneController (int index) const
{
    auto lane = state.getChildWithName (IDs::CCLANES).getChild (index);

    return lane.isValid() ? (int) lane[IDs::ccController] : -1;
}

bool MidiClip::isCCLaneVisible (int index) const
{
    auto lane = state.getChildWithName (IDs::CCLANES).getChild (index);

    if (! lane.isValid())
        return false;

    // 属性が無い古いデータは「表示」として扱う（追加した以上、見えるのが自然）
    return ! lane.hasProperty (IDs::ccLaneVisible) || (bool) lane[IDs::ccLaneVisible];
}

void MidiClip::setCCLaneVisible (int index, bool shouldBeVisible, juce::UndoManager* undoManager)
{
    auto lane = state.getChildWithName (IDs::CCLANES).getChild (index);

    if (lane.isValid())
        lane.setProperty (IDs::ccLaneVisible, shouldBeVisible, undoManager);
}

int MidiClip::addCCLane (int controllerNumber, juce::UndoManager* undoManager)
{
    auto lanesNode = getOrCreateCCLanesNode (undoManager);

    for (int i = 0; i < lanesNode.getNumChildren(); ++i)
        if ((int) lanesNode.getChild (i)[IDs::ccController] == controllerNumber)
            return i; // 既にある

    juce::ValueTree lane (IDs::CCLANE);
    lane.setProperty (IDs::ccController, controllerNumber, nullptr);
    lane.setProperty (IDs::ccLaneVisible, true, nullptr);

    lanesNode.addChild (lane, -1, undoManager);

    return lanesNode.getNumChildren() - 1;
}

void MidiClip::removeCCLane (int index, juce::UndoManager* undoManager)
{
    auto lanesNode = state.getChildWithName (IDs::CCLANES);

    if (lanesNode.isValid())
        lanesNode.removeChild (index, undoManager);
}

void MidiClip::moveCCLane (int fromIndex, int toIndex, juce::UndoManager* undoManager)
{
    auto lanesNode = state.getChildWithName (IDs::CCLANES);

    if (lanesNode.isValid())
        lanesNode.moveChild (fromIndex, toIndex, undoManager);
}

void MidiClip::ensureCCLanesForExistingEvents (juce::UndoManager* undoManager)
{
    if (getNumCCLanes() > 0 || getNumCCEvents() == 0)
        return;

    for (int i = 0; i < getNumCCEvents(); ++i)
        addCCLane (getCCEvent (i).getControllerNumber(), undoManager);
}

bool MidiClip::contentTimeToTimeline (double contentTimeSeconds, double& timelineTimeOut) const
{
    const double offset = getOffset();

    // 左端のトリムで隠れた部分
    if (contentTimeSeconds < offset)
        return false;

    // ノートの時刻は「中身の先頭」からの相対位置。オフセットを引いてから
    // クリップの開始位置を足すと、タイムライン上の時刻になる（HANDOVER 1.14）。
    const double timelineTime = getStartTime() + (contentTimeSeconds - offset);

    // 右端のトリムで隠れた部分
    if (timelineTime >= getStartTime() + getLength())
        return false;

    timelineTimeOut = timelineTime;
    return true;
}

bool MidiClip::growToFitNotes (juce::UndoManager* undoManager)
{
    double lastNoteEnd = 0.0;
    auto notesNode = state.getChildWithName (IDs::NOTES);

    for (int n = 0; n < notesNode.getNumChildren(); ++n)
    {
        // 8.138：**ここは古いプロパティを直に読みます**（Phase 176／8.105の宿題3）。
        //
        // `<MIDICLIP>`の中のノートは**読み込み時の変換の途中にしか存在しません**
        // （Phase 131でクリップという入れ物をやめたため。8.91）。**まだ秒のまま**なので、
        // `Note::getStartTime()`（拍から換算する）を通すと0が返ります。
        //
        // クリップの中身の時刻は「クリップの先頭からの相対値」で、曲の拍とは
        // 別の座標です。**そもそも拍へ直せません**——直せるのは、
        // `migrateMidiClipsToTrackNotes()`が曲の時刻へ均してからです
        auto noteState = notesNode.getChild (n);
        const double start = noteState.getProperty (IDs::noteStartTime, 0.0);
        const double length = noteState.getProperty (IDs::noteLength, 0.0);

        lastNoteEnd = juce::jmax (lastNoteEnd, start + length);
    }

    // 窓（オフセット〜オフセット+長さ）に収まる必要があるので、オフセットぶんを引く。
    // オフセットより手前のノートは、左端をトリムして意図的に隠したものなので数えない。
    const double requiredLength = lastNoteEnd - getOffset();

    if (requiredLength <= getLength())
        return false; // 既に全部収まっている

    // 縮めないのは意図的：ノートを消しただけでクリップが勝手に短くなると、
    // 打ち込みの途中で表示が伸び縮みして落ち着かないため。
    setLength (requiredLength, undoManager);
    return true;
}

//==============================================================================
AudioClip::AudioClip (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
    jassert (state.hasType (IDs::AUDIOCLIP));
}

AudioClip AudioClip::create (const juce::String& sourceFilePath, double startTimeSeconds,
                              double lengthSeconds, juce::UndoManager* undoManager, double offsetSeconds)
{
    juce::ValueTree c (IDs::AUDIOCLIP);

    c.setProperty (IDs::clipId, juce::Uuid().toString(), undoManager);
    c.setProperty (IDs::clipSourceFilePath, sourceFilePath, undoManager);

    // 8.142：**ここでは秒のまま置きます**（Phase 180）。まだ繋がっていないので
    // トラックの基準が分からないためで、`musical`のトラックへ載せるときは
    // `Track::addAudioClip()`が繋いだ後に置き直します
    c.setProperty (IDs::clipStartTime, startTimeSeconds, undoManager);
    c.setProperty (IDs::clipLength, lengthSeconds, undoManager);
    c.setProperty (IDs::clipOffset, offsetSeconds, undoManager);
    c.setProperty (IDs::clipFadeIn, 0.0, undoManager);
    c.setProperty (IDs::clipFadeOut, 0.0, undoManager);

    return AudioClip (c);
}

juce::String AudioClip::getId() const             { return state[IDs::clipId]; }
juce::String AudioClip::getSourceFilePath() const  { return state[IDs::clipSourceFilePath]; }
//==============================================================================
// 8.142：**開始位置は、トラックの基準で振り分けます**（Phase 180／8.105の宿題3）。
//
// `musical`なら拍から換算し、`linear`なら秒をそのまま読みます。
// **長さ・オフセット・フェード・ヒットポイントは、どちらでも秒のまま**です
// ——オーディオは伸び縮みできないので、変わり得るのは置き場所だけです。

namespace
{
    /** そのクリップが載っているトラックの基準。

        **どこにも載っていなければ`linear`**（クリップボードへ取った複製など）。
        切り離されたツリーの位置は、貼り付ける側が決め直します（1.32）。 */
    TimeBase getTimeBaseForClip (const juce::ValueTree& clipState)
    {
        // <AUDIOCLIP> → <CLIPS> → <TRACK>
        auto trackState = clipState.getParent().getParent();

        if (! trackState.hasType (IDs::TRACK))
            return TimeBase::linear;

        return Track (trackState).getTimeBase();
    }
}

double AudioClip::getStartBeats() const
{
    return state[IDs::clipStartBeats];
}

void AudioClip::setStartBeats (double newStartBeats, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::clipStartBeats, juce::jmax (0.0, newStartBeats), undoManager);
}

double AudioClip::getStartTime() const
{
    if (getTimeBaseForClip (state) == TimeBase::musical)
        return MusicalTime::getSecondsFor (state, getStartBeats());

    return state[IDs::clipStartTime];
}
double AudioClip::getLength() const                { return state[IDs::clipLength]; }
double AudioClip::getOffset() const                { return state[IDs::clipOffset]; }
double AudioClip::getFadeInSeconds() const         { return state[IDs::clipFadeIn]; }
double AudioClip::getFadeOutSeconds() const        { return state[IDs::clipFadeOut]; }

void AudioClip::setStartTime (double newStartTimeSeconds, juce::UndoManager* undoManager)
{
    // 8.142：**基準で書き分けます**（Phase 180）。両方に書かないこと——
    // 同じことを表す値が2つ並ぶと、必ず片方が古くなります（1.27）
    if (getTimeBaseForClip (state) == TimeBase::musical)
    {
        setStartBeats (MusicalTime::getBeatsFor (state, juce::jmax (0.0, newStartTimeSeconds)),
                        undoManager);
        return;
    }

    state.setProperty (IDs::clipStartTime, newStartTimeSeconds, undoManager);
}

void AudioClip::setLength (double newLengthSeconds, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::clipLength, newLengthSeconds, undoManager);
}

void AudioClip::setOffset (double newOffsetSeconds, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::clipOffset, newOffsetSeconds, undoManager);
}

void AudioClip::setFadeInSeconds (double newFadeInSeconds, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::clipFadeIn, newFadeInSeconds, undoManager);
}

void AudioClip::setFadeOutSeconds (double newFadeOutSeconds, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::clipFadeOut, newFadeOutSeconds, undoManager);
}

float AudioClip::getGainDb() const
{
    // 8.40：**入っていなければ0dB**（Phase 79以前のプロジェクトは今までどおりの音量）
    return (float) state.getProperty (IDs::clipGainDb, 0.0f);
}

void AudioClip::setGainDb (float newGainDb, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::clipGainDb,
                        juce::jlimit (minClipGainDb, maxClipGainDb, newGainDb), undoManager);
}

float AudioClip::getGainLinear() const
{
    return juce::Decibels::decibelsToGain (getGainDb(), minClipGainDb);
}

bool AudioClip::isReversed() const
{
    return state[IDs::clipReversed];
}

void AudioClip::setReversed (bool shouldBeReversed, juce::UndoManager* undoManager)
{
    // **音が変わるのでUndoの対象**（畳む・透かしのような見え方とは違う）
    state.setProperty (IDs::clipReversed, shouldBeReversed, undoManager);
}

bool AudioClip::isMono() const
{
    return state[IDs::clipMono];
}

void AudioClip::setMono (bool shouldBeMono, juce::UndoManager* undoManager)
{
    // 8.228：Phase 249。**逆再生と同じ扱い**——ファイルは触らず、
    // 鳴らすときに混ぜます（取り消せる／元に戻せる）
    state.setProperty (IDs::clipMono, shouldBeMono, undoManager);
}

int AudioClip::getTranspose() const
{
    return (int) state.getProperty (IDs::clipTranspose, 0);
}

void AudioClip::setTranspose (int semitones, juce::UndoManager* undoManager)
{
    const int limited = juce::jlimit (-AudioTransform::maxSemitones,
                                       AudioTransform::maxSemitones, semitones);

    // **0のときはプロパティを消す。** 入っていないのと0は同じ意味なので、
    // 残しておくとファイルに意味の無い行が並びます（`clipGainDb`と同じ扱い）
    if (limited == 0)
        state.removeProperty (IDs::clipTranspose, undoManager);
    else
        state.setProperty (IDs::clipTranspose, limited, undoManager);
}

//==============================================================================
// 8.149：クリップ全体の伸縮（Phase 187／8.48）
//==============================================================================

double AudioClip::getStretch() const
{
    const double ratio = state.getProperty (IDs::clipStretch, 1.0);

    // **0や負の値を返さないこと。** 割り算の分母になるので、
    // 壊れたファイルが入ってきたときに無限大や負の長さが出ます
    return (ratio > 0.0) ? juce::jlimit (AudioTransform::minStretch, AudioTransform::maxStretch, ratio)
                          : 1.0;
}

void AudioClip::setStretch (double ratio, juce::UndoManager* undoManager)
{
    const double limited = juce::jlimit (AudioTransform::minStretch, AudioTransform::maxStretch, ratio);

    // **1.0のときはプロパティを消す**（`clipTranspose`と同じ扱い）
    if (std::abs (limited - 1.0) < 1.0e-9)
        state.removeProperty (IDs::clipStretch, undoManager);
    else
        state.setProperty (IDs::clipStretch, limited, undoManager);
}

double AudioClip::getSourceLength() const
{
    return getLength() / getStretch();
}

double AudioClip::getSourceEnd() const
{
    return getOffset() + getSourceLength();
}

double AudioClip::sourceTimeToTimeline (double sourceSeconds) const
{
    // 8.150：**マーカーがあれば折れ線を通る**（Phase 188）。
    // 無いときは`getWarpMap()`が点2つの表を返すので、答えは今までと同じです
    return getStartTime() + getWarpMap().sourceToWarped (sourceSeconds);
}

double AudioClip::timelineToSourceTime (double timelineSeconds) const
{
    return getWarpMap().warpedToSource (timelineSeconds - getStartTime());
}

//==============================================================================
// 8.150：ワープマーカー（Phase 188／8.48）
//==============================================================================

std::vector<AudioClip::WarpMarker> AudioClip::getWarpMarkers() const
{
    std::vector<WarpMarker> markers;

    auto warpNode = state.getChildWithName (IDs::WARP);

    if (! warpNode.isValid())
        return markers;

    for (int i = 0; i < warpNode.getNumChildren(); ++i)
    {
        auto child = warpNode.getChild (i);

        // **ノード種別まで見ること**（1.42）。同じ入れ物に別のものが入る日が来ます
        if (! child.hasType (IDs::WARPMARKER))
            continue;

        markers.push_back ({ (double) child.getProperty (IDs::warpSourceTime, 0.0),
                             (double) child.getProperty (IDs::warpClipTime, 0.0) });
    }

    std::sort (markers.begin(), markers.end(),
                [] (const WarpMarker& a, const WarpMarker& b)
                {
                    return a.sourceSeconds < b.sourceSeconds;
                });

    return markers;
}

bool AudioClip::hasWarpMarkers() const
{
    return ! getWarpMarkers().empty();
}

WarpMap AudioClip::getWarpMap() const
{
    const double offset = getOffset();
    const double length = getLength();
    const double sourceLength = getSourceLength();

    WarpMap map;

    // **両端はクリップの端。** マーカーが増えても、
    // **使うソースの範囲も曲の中の長さも変わりません**（配り直すだけ）
    map.points.push_back ({ offset, 0.0 });

    for (const auto& marker : getWarpMarkers())
    {
        // 端より外のマーカーは入れない（トリムで隠れている範囲のもの）。
        // **消しはしません**——トリムを戻せばまた効きます
        if (marker.sourceSeconds <= offset || marker.sourceSeconds >= offset + sourceLength)
            continue;

        if (marker.clipSeconds <= 0.0 || marker.clipSeconds >= length)
            continue;

        map.points.push_back ({ marker.sourceSeconds, marker.clipSeconds });
    }

    map.points.push_back ({ offset + sourceLength, length });

    // **通らない点はここで捨てる**（`WarpMap::sanitise()`）。
    // 呼ぶ側で弾くより確実で、壊れたファイルが来ても鳴らなくならない
    map.sanitise();

    return map;
}

bool AudioClip::addWarpMarker (double sourceSeconds, double clipSeconds, juce::UndoManager* undoManager)
{
    const double offset = getOffset();
    const double length = getLength();
    const double sourceEnd = offset + getSourceLength();

    // 端の外・端そのものには置けない（端はクリップの両端が持っている）
    if (sourceSeconds <= offset + WarpMap::minimumSegment
         || sourceSeconds >= sourceEnd - WarpMap::minimumSegment
         || clipSeconds <= WarpMap::minimumSegment
         || clipSeconds >= length - WarpMap::minimumSegment)
        return false;

    auto warpNode = getOrCreateChild (state, IDs::WARP, undoManager);

    // 同じソース位置に既にあるなら、鳴る時刻を書き替えるだけ
    for (int i = 0; i < warpNode.getNumChildren(); ++i)
    {
        auto child = warpNode.getChild (i);

        if (! child.hasType (IDs::WARPMARKER))
            continue;

        const double existing = child.getProperty (IDs::warpSourceTime, 0.0);

        if (std::abs (existing - sourceSeconds) < WarpMap::minimumSegment)
        {
            child.setProperty (IDs::warpClipTime, clipSeconds, undoManager);
            return true;
        }
    }

    juce::ValueTree marker (IDs::WARPMARKER);
    marker.setProperty (IDs::warpSourceTime, sourceSeconds, undoManager);
    marker.setProperty (IDs::warpClipTime, clipSeconds, undoManager);

    warpNode.addChild (marker, -1, undoManager);
    return true;
}

bool AudioClip::moveWarpMarker (double sourceSeconds, double newClipSeconds,
                                 juce::UndoManager* undoManager)
{
    auto warpNode = state.getChildWithName (IDs::WARP);

    if (! warpNode.isValid())
        return false;

    // **両隣を追い越させないこと。** 追い越すと時間が戻り、その区間の音が裏返ります。
    // `WarpMap::sanitise()`が捨ててくれますが、**捨てられると掴んだ印が消える**ので、
    // ここで止めて「動かない」ほうが分かりやすい
    double lowerBound = 0.0;
    double upperBound = getLength();

    for (const auto& marker : getWarpMarkers())
    {
        if (marker.sourceSeconds < sourceSeconds - WarpMap::minimumSegment)
            lowerBound = juce::jmax (lowerBound, marker.clipSeconds);
        else if (marker.sourceSeconds > sourceSeconds + WarpMap::minimumSegment)
            upperBound = juce::jmin (upperBound, marker.clipSeconds);
    }

    const double limited = juce::jlimit (lowerBound + WarpMap::minimumSegment,
                                          juce::jmax (lowerBound + 2.0 * WarpMap::minimumSegment,
                                                       upperBound - WarpMap::minimumSegment),
                                          newClipSeconds);

    for (int i = 0; i < warpNode.getNumChildren(); ++i)
    {
        auto child = warpNode.getChild (i);

        if (! child.hasType (IDs::WARPMARKER))
            continue;

        const double existing = child.getProperty (IDs::warpSourceTime, 0.0);

        if (std::abs (existing - sourceSeconds) < WarpMap::minimumSegment)
        {
            child.setProperty (IDs::warpClipTime, limited, undoManager);
            return true;
        }
    }

    return false;
}

void AudioClip::removeWarpMarkerNear (double sourceSeconds, double toleranceSeconds,
                                       juce::UndoManager* undoManager)
{
    auto warpNode = state.getChildWithName (IDs::WARP);

    if (! warpNode.isValid())
        return;

    // **後ろから消すこと**（前から消すと番号がずれる。1.32）
    for (int i = warpNode.getNumChildren(); --i >= 0;)
    {
        auto child = warpNode.getChild (i);

        if (! child.hasType (IDs::WARPMARKER))
            continue;

        const double existing = child.getProperty (IDs::warpSourceTime, 0.0);

        if (std::abs (existing - sourceSeconds) <= toleranceSeconds)
            warpNode.removeChild (child, undoManager);
    }
}

void AudioClip::clearWarpMarkers (juce::UndoManager* undoManager)
{
    auto warpNode = state.getChildWithName (IDs::WARP);

    if (warpNode.isValid())
        state.removeChild (warpNode, undoManager);
}

WarpMap AudioClip::getRenderWarpMap() const
{
    const double offset = getOffset();
    const double stretch = getStretch();
    const double length = getLength();
    const double sourceLength = getSourceLength();

    // **クリップの頭が来る場所。** ここより前は`stretch`倍のまっすぐな線なので、
    // 再生側は`offset × stretch`から読めばよい（Phase 187から変わらない）
    const double clipStartWarped = offset * stretch;

    WarpMap map;
    map.points.push_back ({ 0.0, 0.0 });
    map.points.push_back ({ offset, clipStartWarped });

    for (const auto& marker : getWarpMarkers())
    {
        if (marker.sourceSeconds <= offset || marker.sourceSeconds >= offset + sourceLength)
            continue;

        if (marker.clipSeconds <= 0.0 || marker.clipSeconds >= length)
            continue;

        map.points.push_back ({ marker.sourceSeconds, clipStartWarped + marker.clipSeconds });
    }

    map.points.push_back ({ offset + sourceLength, clipStartWarped + length });

    // **クリップより後ろに点は足しません。** そこは`WarpMap`が
    // いちばん端の区間の速さで延長します——このクリップは鳴らさない範囲なので、
    // 出来上がりのファイルが少し長くなるだけです
    map.sanitise();

    return map;
}

void AudioClip::applyAutoFade (juce::UndoManager* undoManager)
{
    // 切り口の「プツッ」が消える程度の短さ。**長いクリップでも短いまま**でよい
    // （聞こえるフェードが欲しいときは、ハンドルを掴んで伸ばす）
    const double length = getLength();
    const double fade = juce::jmin (autoFadeSeconds, length * 0.5);

    if (fade <= 0.0)
        return;

    // **既に付いている端は触りません。** 指定して作ったフェードを、
    // 「オートフェード」で短く潰されると困る
    if (getFadeInSeconds() <= 0.0)
        setFadeInSeconds (fade, undoManager);

    if (getFadeOutSeconds() <= 0.0)
        setFadeOutSeconds (fade, undoManager);
}

juce::Array<double> AudioClip::getHitPoints() const
{
    juce::Array<double> result;

    const juce::String stored = state[IDs::clipHitPoints].toString();
    if (stored.isEmpty())
        return result;

    auto tokens = juce::StringArray::fromTokens (stored, ",", "");
    for (const auto& token : tokens)
        if (token.isNotEmpty())
            result.add (token.getDoubleValue());

    return result;
}

void AudioClip::setHitPoints (const juce::Array<double>& newHitPoints, juce::UndoManager* undoManager)
{
    juce::StringArray tokens;
    for (auto t : newHitPoints)
        tokens.add (juce::String (t, 6));

    state.setProperty (IDs::clipHitPoints, tokens.joinIntoString (","), undoManager);
}

void AudioClip::addHitPoint (double timeSeconds, juce::UndoManager* undoManager)
{
    auto points = getHitPoints();
    points.add (timeSeconds);
    points.sort();
    setHitPoints (points, undoManager);
}

void AudioClip::removeHitPointNear (double timeSeconds, double toleranceSeconds, juce::UndoManager* undoManager)
{
    auto points = getHitPoints();

    int closestIndex = -1;
    double closestDistance = toleranceSeconds;

    for (int i = 0; i < points.size(); ++i)
    {
        const double distance = std::abs (points[i] - timeSeconds);
        if (distance <= closestDistance)
        {
            closestDistance = distance;
            closestIndex = i;
        }
    }

    if (closestIndex >= 0)
    {
        points.remove (closestIndex);
        setHitPoints (points, undoManager);
    }
}

//==============================================================================
ProjectModel::ProjectModel()
{
    createNewProject();   // 中の`setState()`が`MusicalTime`へ登録します（8.137）
}

ProjectModel::~ProjectModel()
{
    // 8.137：**外し忘れると、壊れたポインタが対応表に残ります**（Phase 175）
    MusicalTime::unregisterSource (this);
}

void ProjectModel::createNewProject()
{
    auto newState = juce::ValueTree (IDs::PROJECT);
    newState.setProperty (IDs::projectName, "Untitled Project", nullptr);

    // 8.138：**作りたては今の形**（Phase 176）。入れておかないと、
    // 次に開いたときに「古い形」と見なされて変換が走ります
    newState.setProperty (IDs::formatVersion, currentFormatVersion, nullptr);
    newState.setProperty (IDs::sampleRate, 48000, nullptr);
    newState.setProperty (IDs::bitDepth, 32, nullptr);
    newState.setProperty (IDs::tempo, 120.0, nullptr);
    newState.setProperty (IDs::timeSignature, "4/4", nullptr);
    newState.addChild (juce::ValueTree (IDs::TRACKS), -1, nullptr);
    newState.addChild (juce::ValueTree (IDs::PLUGINS), -1, nullptr); // 設計書3.8

    juce::ValueTree masterBus (IDs::MASTERBUS); // 設計書1.3のmasterBus
    masterBus.setProperty (IDs::volume, 0.0f, nullptr);
    newState.addChild (masterBus, -1, nullptr);

    setState (newState);

    currentFile = juce::File();
    undoManager.clearUndoHistory();
    markAsSaved(); // 作りたてなので「未保存の変更」は無い
}

void ProjectModel::markAsNewFromTemplate()
{
    // 8.151：`createNewProject()`の末尾3行と同じ（Phase 189／D8）。
    // **中身は触りません**——今あるトラックやクリップはそのまま、
    // 「どこへ保存するか」と「保存済みかどうか」だけを作りたてに戻します
    currentFile = juce::File();
    undoManager.clearUndoHistory();
    markAsSaved();
}

void ProjectModel::setState (juce::ValueTree newState)
{
    // 古いツリーの購読を外してから差し替える。外し忘れると、破棄されずに残った
    // 古いツリーへの変更でも「未保存」と判定されてしまう。
    if (state.isValid())
        state.removeListener (this);

    state = newState;

    // 8.102：**ルートが差し替わったら、テンポの表は必ず捨てる。**
    // リスナーを付け替えるだけでは、差し替えそのものは通知されません
    markDerivedMapsDirty();

    // 8.137：**根が変わったことを対応表にも伝える**（Phase 175）。
    // 伝え忘れると、開き直した後のノートが「どのプロジェクトにも属さない」
    // 扱いになり、**既定の120BPMで換算されます**（`MusicalTime.h`）
    MusicalTime::registerSource (this);
    tracksNode = state.getChildWithName (IDs::TRACKS);

    // 仕様書5.2.3：コードトラックを一番上へ（Phase 60／8.20）。
    // **購読を始める前にやること。** Phase 59以前のプロジェクトはコードトラックが
    // 途中にあり得るが、ここで直すぶんを「未保存の変更」に数えたくない
    ensureChordTrackIsFirst();

    if (state.isValid())
        state.addListener (this);

    // ルートが差し替わったことを、外部の購読者（AudioEngine等）へ知らせる。
    // これが無いと、購読側は破棄された古いツリーを見続けることになる。
    if (onStateReplaced != nullptr)
        onStateReplaced();
}

void ProjectModel::markAsChanged()
{
    if (unsavedChanges)
        return; // 既に変更ありなら通知を繰り返さない（変更のたびにUIを更新しないため）

    unsavedChanges = true;

    if (onSavedStateChanged != nullptr)
        onSavedStateChanged();
}

void ProjectModel::markAsRecovered (const juce::File& originalFile)
{
    currentFile = originalFile;

    // 明示的に「未保存の変更あり」にする。復元した内容はまだ元ファイルに書かれておらず、
    // ここで保存済み扱いにすると、ユーザーが保存しないまま閉じて内容を失う。
    unsavedChanges = true;

    if (onSavedStateChanged != nullptr)
        onSavedStateChanged();
}

void ProjectModel::markAsSaved()
{
    const bool changed = unsavedChanges;
    unsavedChanges = false;

    if (changed && onSavedStateChanged != nullptr)
        onSavedStateChanged();
}

//==============================================================================
// 設計書1.3・3.8：PluginInstance
//==============================================================================

PluginInstance::PluginInstance (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
}

juce::String PluginInstance::getRole() const        { return state[IDs::pluginRole]; }
juce::String PluginInstance::getDisplayName() const { return state[IDs::pluginDisplayName]; }

bool PluginInstance::getDescription (juce::PluginDescription& descriptionOut) const
{
    auto descriptionTree = state.getChildWithName (IDs::PLUGINDESC);

    if (! descriptionTree.isValid())
        return false;

    // PluginDescriptionは自前のXML入出力を持っているため、それをそのままValueTreeへ
    // 入れ子にして保存している。フォーマット・識別子・入出力構成などが一式含まれるので、
    // pluginUidだけを保存して後から検索する方式より復元が確実。
    auto xml = descriptionTree.createXml();

    return xml != nullptr && descriptionOut.loadFromXml (*xml);
}

void PluginInstance::setDescription (const juce::PluginDescription& description, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::pluginDisplayName, description.name, undoManager);
    state.setProperty (IDs::pluginFormat, description.pluginFormatName, undoManager);
    state.setProperty (IDs::pluginUid, description.createIdentifierString(), undoManager);

    state.removeChild (state.getChildWithName (IDs::PLUGINDESC), undoManager);

    if (auto xml = description.createXml())
        state.addChild (juce::ValueTree::fromXml (*xml), -1, undoManager);
}

juce::MemoryBlock PluginInstance::getPluginState() const
{
    juce::MemoryBlock data;
    const juce::String base64 = state[IDs::pluginState];

    if (base64.isNotEmpty())
    {
        juce::MemoryOutputStream stream (data, false);

        if (! juce::Base64::convertFromBase64 (stream, base64))
            return {}; // 壊れていた場合は「状態なし」として扱い、初期状態で読み込む
    }

    return data;
}

void PluginInstance::setPluginState (const juce::MemoryBlock& data, juce::UndoManager* undoManager)
{
    // 設計書3.8：内部状態はバイナリなので、XMLへ入れられるようBase64文字列にする
    state.setProperty (IDs::pluginState,
                        data.getSize() > 0 ? juce::Base64::toBase64 (data.getData(), data.getSize()) : juce::String(),
                        undoManager);
}

juce::String PluginInstance::getSidechainSourceTrackId() const
{
    return state[IDs::sidechainSourceTrackId];
}

void PluginInstance::setSidechainSourceTrackId (const juce::String& trackId, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::sidechainSourceTrackId, trackId, undoManager);
}

// 8.63：インサートのバイパス（Phase 101／改善案㉘）
bool PluginInstance::isBypassed() const
{
    return (bool) state.getProperty (IDs::pluginBypassed, false);
}

void PluginInstance::setBypassed (bool shouldBypass, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::pluginBypassed, shouldBypass, undoManager);
}

//==============================================================================
// 仕様書5.2.4：VCAトラック（ProjectModel側のまとめ役）
//==============================================================================

Track ProjectModel::findTrackById (const juce::String& trackId) const
{
    for (int t = 0; t < getNumTracks(); ++t)
    {
        auto track = getTrack (t);

        if (track.getId() == trackId)
            return track;
    }

    return Track (juce::ValueTree (IDs::TRACK)); // 見つからない場合の空トラック
}

Track ProjectModel::findChordTrack() const
{
    for (int t = 0; t < getNumTracks(); ++t)
    {
        auto track = getTrack (t);

        if (track.getType() == TrackType::Chord)
            return track;
    }

    return Track (juce::ValueTree (IDs::TRACK)); // コードトラックが無い場合
}

Scale ProjectModel::getProjectKey() const
{
    auto chordTrack = findChordTrack();

    // 空トラックはTRACKノードを持つだけで親が無い。キーのプロパティも無いので、
    // getChordKey()は既定のCメジャーを返す。ここではその判定に頼らず明示しておく。
    if (! chordTrack.state.getParent().isValid())
        return Scale();

    return chordTrack.getChordKey();
}

bool ProjectModel::setProjectKey (const Scale& newKey, juce::UndoManager* undoManagerToUse)
{
    auto chordTrack = findChordTrack();

    // **コードトラックが無ければ何もしない。** キーの置き場所はコードトラックなので、
    // ここで勝手に作ると「BPMの隣を触っただけでトラックが増える」ことになる
    if (! chordTrack.state.getParent().isValid())
        return false;

    chordTrack.setChordKey (newKey, undoManagerToUse);
    return true;
}

//==============================================================================
// 仕様書5.11.1：キーの変化点（Phase 143／改善案㉔㉕。8.106）
//
// **テンポ・拍子（8.102）と同じ形です。** ValueTreeから表を作り、
// 「その時刻のキーは？」の質問は全部その表が答えます。
//==============================================================================

const KeyMap& ProjectModel::getKeyMap() const
{
    if (! keyMapDirty)
        return keyMap;

    keyMap = KeyMap();
    keyMap.initialKey = getProjectKey();   // 曲の頭のキー（コードトラックが持っている）

    auto mapNode = state.getChildWithName (IDs::KEYMAP);

    // **古いプロジェクトにはこのノードがありません。** 無ければ変化点ゼロ＝
    // Phase 142までと完全に同じ答えになるので、読み替えは要らない（8.106）
    if (mapNode.isValid())
    {
        for (int i = 0; i < mapNode.getNumChildren(); ++i)
        {
            auto child = mapNode.getChild (i);

            if (! child.hasType (IDs::KEYCHANGE))
                continue;

            KeyMap::KeyChange change;
            change.bar = (int) child.getProperty (IDs::keyChangeBar, 0);
            change.key.root = juce::jlimit (0, 11, (int) child.getProperty (IDs::chordKeyRoot, 0));
            change.key.minor = (bool) child.getProperty (IDs::chordKeyMinor, false);
            keyMap.changes.push_back (change);
        }
    }

    // **並べ替えを忘れないこと**（`getKeyAtBar()`が昇順を前提にしています）
    keyMap.sortAndDeduplicate();

    keyMapDirty = false;
    return keyMap;
}

Scale ProjectModel::getProjectKeyAt (double timeSeconds) const
{
    return getKeyMap().getKeyAtBar (getBarIndexAt (timeSeconds));
}

/** KEYMAPノード。無ければ作る（古いプロジェクトには無い）。 */
static juce::ValueTree getOrCreateKeyMapNode (juce::ValueTree& state, juce::UndoManager* undoManager)
{
    auto node = state.getChildWithName (IDs::KEYMAP);

    if (! node.isValid())
    {
        node = juce::ValueTree (IDs::KEYMAP);
        state.addChild (node, -1, undoManager);
    }

    return node;
}

bool ProjectModel::setKeyChange (int bar, const Scale& newKey, juce::UndoManager* undoManagerToUse)
{
    // **0小節目は置けません**（`setProjectKey()`＝曲の頭のキーの担当）
    if (bar <= 0)
        return false;

    // **コードトラックが無ければ何もしない。** `setProjectKey()`と同じ決まりにすること——
    // 片方だけ緩いと、レーンからは置けるのにフッターからは変えられないキーができます
    // （8.12の「入口が2つあるものは、片方だけ直して食い違う」）
    if (! findChordTrack().state.getParent().isValid())
        return false;

    auto node = getOrCreateKeyMapNode (state, undoManagerToUse);

    for (int i = 0; i < node.getNumChildren(); ++i)
    {
        auto child = node.getChild (i);

        if (child.hasType (IDs::KEYCHANGE) && (int) child.getProperty (IDs::keyChangeBar, -1) == bar)
        {
            child.setProperty (IDs::chordKeyRoot, juce::jlimit (0, 11, newKey.root), undoManagerToUse);
            child.setProperty (IDs::chordKeyMinor, newKey.minor, undoManagerToUse);
            return true;
        }
    }

    juce::ValueTree change (IDs::KEYCHANGE);
    change.setProperty (IDs::keyChangeBar, bar, nullptr);
    change.setProperty (IDs::chordKeyRoot, juce::jlimit (0, 11, newKey.root), nullptr);
    change.setProperty (IDs::chordKeyMinor, newKey.minor, nullptr);
    node.addChild (change, -1, undoManagerToUse);

    return true;
}

void ProjectModel::removeKeyChange (int bar, juce::UndoManager* undoManagerToUse)
{
    auto node = state.getChildWithName (IDs::KEYMAP);

    if (! node.isValid())
        return;

    for (int i = node.getNumChildren(); --i >= 0;)
    {
        auto child = node.getChild (i);

        if (child.hasType (IDs::KEYCHANGE) && (int) child.getProperty (IDs::keyChangeBar, -1) == bar)
            node.removeChild (i, undoManagerToUse);
    }
}

Track ProjectModel::findVcaForTrack (const juce::String& trackId) const
{
    if (trackId.isNotEmpty())
    {
        for (int t = 0; t < getNumTracks(); ++t)
        {
            auto candidate = getTrack (t);

            if (candidate.getType() == TrackType::VCA && candidate.isTrackLinkedToVca (trackId))
                return candidate;
        }
    }

    return Track (juce::ValueTree (IDs::TRACK));
}

ProjectModel::VcaInfluence ProjectModel::getVcaInfluenceFor (const juce::String& trackId) const
{
    VcaInfluence influence;

    auto vca = findVcaForTrack (trackId);

    if (! vca.state.getParent().isValid())
        return influence;

    influence.linked = true;
    influence.offsetDb = vca.getVolumeDb();
    influence.soloed = vca.isSoloed();

    // VCAのフェーダーが下端（-60dB）なら、リンク先を無音にする。
    // 単なるdB加算のままだと「フェーダーは一番下なのに、うっすら鳴っている」
    // という状態になり得るため、フェーダー下端＝無音の扱いをここで揃える
    // （TrackChannelProcessor::silenceThresholdDbと同じ値）。
    influence.muted = vca.isMuted() || influence.offsetDb <= AutomationTargets::minVolumeDb;

    return influence;
}

void ProjectModel::assignTrackToVca (const juce::String& trackId, const juce::String& vcaTrackId)
{
    if (trackId.isEmpty())
        return;

    beginAction (utf8 ("VCAの割り当て"));

    // 1トラックにつきVCAは1つまで（設計書1.3）。付け替えの前に、
    // 今リンクされている他のVCAから必ず外す。
    for (int t = 0; t < getNumTracks(); ++t)
    {
        auto candidate = getTrack (t);

        if (candidate.getType() == TrackType::VCA && candidate.getId() != vcaTrackId)
            candidate.unlinkTrackFromVca (trackId, &undoManager);
    }

    if (vcaTrackId.isEmpty())
        return; // 解除だけして終わり

    auto vca = findTrackById (vcaTrackId);

    if (vca.state.getParent().isValid() && vca.getType() == TrackType::VCA)
        vca.linkTrackToVca (trackId, &undoManager);
}

bool ProjectModel::isAnyVcaSoloed() const
{
    for (int t = 0; t < getNumTracks(); ++t)
    {
        auto track = getTrack (t);

        if (track.getType() == TrackType::VCA && track.isSoloed() && ! track.getLinkedTrackIds().isEmpty())
            return true;
    }

    return false;
}


ProjectModel::FolderInfluence ProjectModel::getFolderInfluenceFor (const Track& track) const
{
    FolderInfluence influence;

    if (! track.state.getParent().isValid())
        return influence;

    auto parentId = track.getParentFolderId();

    for (int guard = 0; guard < maxFolderDepth && parentId.isNotEmpty(); ++guard)
    {
        auto parent = findTrackById (parentId);

        if (! parent.state.getParent().isValid())
            break;

        influence.muted = influence.muted || parent.isMuted();
        influence.soloed = influence.soloed || parent.isSoloed();

        parentId = parent.getParentFolderId();
    }

    return influence;
}
bool ProjectModel::isAnySoloActive() const
{
    for (int t = 0; t < getNumTracks(); ++t)
    {
        auto track = getTrack (t);

        // ソロの対象は自分で音を出すトラック（オーディオ／MIDI）。
        // センドトラックはソロの対象外（isTrackAudible()の説明を参照）
        // 8.51：フォルダのソロも数える（Phase 90／D2）。
        // フォルダ自身は音を通さないが、**中身をまとめてソロにする**ため
        // 8.145：**パラアウトの受け皿もソロにできる**（Phase 183／本人の要望）。
        // ここに無いと**押しても「ソロが立っている」と数えられず、何も起きません**
        const bool canSolo = (track.getType() == TrackType::Audio
                               || track.getType() == TrackType::Midi
                               || track.getType() == TrackType::Folder
                               || track.getType() == TrackType::DrumOut);

        if (canSolo && track.isSoloed())
            return true;
    }

    // 仕様書5.2.4：VCAのソロも「ソロが立っている」に数える。
    // VCA自身は音を通さないので、上のループでは拾えない（Phase 12d-2）
    return isAnyVcaSoloed();
}

bool ProjectModel::isTrackAudible (const Track& track) const
{
    return isTrackAudible (track, isAnySoloActive());
}

bool ProjectModel::isTrackAudible (const Track& track, bool anySoloActive) const
{
    // 仕様書5.2.4：リンク先VCAの影響（ミュート・ソロ）をまとめて取る
    const auto vca = getVcaInfluenceFor (track.getId());

    // 8.51：**入っているフォルダのソロ／ミュートも効く**（Phase 90／D2）。
    // フォルダ自身は音を通さないので、**中身の判定に混ぜる**形にしてある
    // （VCAと同じ考え方。まとめる仕組みは1箇所＝ここに集める）
    const auto folder = getFolderInfluenceFor (track);

    if (track.isMuted() || vca.muted || folder.muted)
        return false; // ミュートはソロより優先する

    if (! anySoloActive)
        return true;

    // 8.145：**音源をソロにしたら、その受け皿も鳴ります**（Phase 183／本人の要望）。
    //
    // 「ドラムをソロで聴く」つもりで音源トラックのSを押したのに、
    // 個別出力へ回した音が全部止まる——では使えません。
    // **逆向きは普通どおり**：受け皿をソロにすれば、その1本だけが鳴ります。
    //
    // Phase 182までは「受け皿はセンドと同じで、ソロ中も素通り」でした。
    // それだと**受け皿をソロにしても他が止まらない**ので、押しても効きません。
    if (track.getType() == TrackType::DrumOut && ! track.isSoloed())
    {
        auto source = findTrackById (track.getDrumOutSourceTrackId());

        if (source.state.getParent().isValid() && source.isSoloed())
            return true;
    }

    // 8.51：**フォルダはセンドと同じ扱いで、ソロ中も音を通す**（Phase 90）。
    // ここで止めると、**中身をソロにしたのに、まとめ先で止まって無音**になります
    return track.isSoloed() || vca.soloed || folder.soloed
            || track.getType() == TrackType::Send || track.getType() == TrackType::Folder;
}

//==============================================================================
juce::ValueTree ProjectModel::getOrCreateMasterBusNode()
{
    auto masterNode = state.getChildWithName (IDs::MASTERBUS);

    if (! masterNode.isValid())
    {
        // Phase 12bより前に保存されたプロジェクトには<MASTERBUS>が無いため、ここで作る
        // （仕様書7章「プロジェクトファイルの後方互換性」）。
        masterNode = juce::ValueTree (IDs::MASTERBUS);
        masterNode.setProperty (IDs::volume, 0.0f, nullptr);
        state.addChild (masterNode, -1, nullptr);
    }

    return masterNode;
}

Track ProjectModel::getMasterBusInsertHost()
{
    // 8.69：インサートの形はトラックと同じなので、`Track`の道具をそのまま使う（Phase 108）
    return Track (getOrCreateMasterBusNode());
}

float ProjectModel::getMasterVolumeDb() const
{
    auto masterNode = state.getChildWithName (IDs::MASTERBUS);

    return masterNode.isValid() ? (float) masterNode[IDs::volume] : 0.0f;
}

void ProjectModel::setMasterVolumeDb (float newVolumeDb, juce::UndoManager* undoManagerToUse)
{
    getOrCreateMasterBusNode().setProperty (IDs::volume, newVolumeDb, undoManagerToUse);
}

//==============================================================================
// 仕様書5.6：マスターチャンネルのオートメーション（Phase 20）
//==============================================================================

AutomationLane ProjectModel::findMasterAutomationLane (const juce::String& targetId) const
{
    return findAutomationLaneIn (state.getChildWithName (IDs::MASTERBUS), targetId);
}

AutomationLane ProjectModel::getOrCreateMasterAutomationLane (const juce::String& targetId,
                                                                juce::UndoManager* undoManagerToUse)
{
    return getOrCreateAutomationLaneIn (getOrCreateMasterBusNode(), targetId, undoManagerToUse);
}

AutomationMode ProjectModel::getMasterAutomationMode() const
{
    return automationModeFromString (state.getChildWithName (IDs::MASTERBUS)[IDs::automationMode].toString());
}

void ProjectModel::setMasterAutomationMode (AutomationMode newMode, juce::UndoManager* undoManagerToUse)
{
    getOrCreateMasterBusNode().setProperty (IDs::automationMode, automationModeToString (newMode),
                                             undoManagerToUse);
}

float ProjectModel::getEffectiveMasterVolumeDbAt (double timeSeconds) const
{
    // 8.54：判断は`effectiveValueAt()`1つ（Phase 93）。マスターだけ別の規則にしない
    return effectiveValueAt (findMasterAutomationLane (AutomationTargets::volume),
                              getMasterAutomationMode(), AutomationTargets::volume,
                              timeSeconds, getMasterVolumeDb());
}

//==============================================================================
// 8.56：マスターのレーンの行（Phase 94／D3）。トラック側と同じ関数を通す

int ProjectModel::getNumVisibleMasterAutomationLanes() const
{
    return countVisibleLanesIn (state.getChildWithName (IDs::MASTERBUS));
}

AutomationLane ProjectModel::getVisibleMasterAutomationLane (int ordinal) const
{
    return getVisibleLaneIn (state.getChildWithName (IDs::MASTERBUS), ordinal);
}

bool ProjectModel::isMasterAutomationLaneVisible (const juce::String& targetId) const
{
    auto lane = findMasterAutomationLane (targetId);

    return lane.state.isValid() && lane.isVisible();
}

void ProjectModel::setMasterAutomationLaneVisible (const juce::String& targetId, bool shouldBeVisible,
                                                    juce::UndoManager* undoManagerToUse)
{
    // 出すときだけ`<MASTERBUS>`を作る（隠すだけなら、無いものは無いままでよい）
    if (! shouldBeVisible && ! state.getChildWithName (IDs::MASTERBUS).isValid())
        return;

    setLaneVisibleIn (getOrCreateMasterBusNode(), targetId, shouldBeVisible, undoManagerToUse);
}

void ProjectModel::hideAllMasterAutomationLanes (juce::UndoManager* undoManagerToUse)
{
    hideAllLanesIn (state.getChildWithName (IDs::MASTERBUS), undoManagerToUse);
}

void ProjectModel::removeMasterAutomationLane (const juce::String& targetId,
                                                juce::UndoManager* undoManagerToUse)
{
    auto automationNode = state.getChildWithName (IDs::MASTERBUS).getChildWithName (IDs::AUTOMATION);

    if (automationNode.isValid())
        automationNode.removeChild (automationNode.getChildWithProperty (IDs::laneTargetId, targetId),
                                     undoManagerToUse);
}

bool ProjectModel::isAnyAutomationLaneVisible() const
{
    if (getNumVisibleMasterAutomationLanes() > 0)
        return true;

    for (int t = 0; t < getNumTracks(); ++t)
        if (getTrack (t).getNumVisibleAutomationLanes() > 0)
            return true;

    return false;
}

juce::ValueTree ProjectModel::getOrCreatePluginsNode()
{
    auto pluginsNode = state.getChildWithName (IDs::PLUGINS);

    if (! pluginsNode.isValid())
    {
        // Phase 9より前に保存されたプロジェクトには<PLUGINS>が無いため、ここで作る。
        // 仕様書7章の「プロジェクトファイルの後方互換性」に対応する箇所。
        pluginsNode = juce::ValueTree (IDs::PLUGINS);
        state.addChild (pluginsNode, -1, nullptr);
    }

    return pluginsNode;
}

PluginInstance ProjectModel::getPluginForRole (const juce::String& role) const
{
    auto pluginsNode = state.getChildWithName (IDs::PLUGINS);

    if (pluginsNode.isValid())
        return PluginInstance (pluginsNode.getChildWithProperty (IDs::pluginRole, role));

    return PluginInstance (juce::ValueTree());
}

PluginInstance ProjectModel::setPluginForRole (const juce::String& role,
                                                 const juce::PluginDescription& description,
                                                 juce::UndoManager* undoManagerToUse)
{
    auto pluginsNode = getOrCreatePluginsNode();

    // 同じ役割のものが既にあれば作り直す（別のプラグインへ差し替えられた場合に、
    // 前のプラグインの内部状態が残らないようにするため）
    pluginsNode.removeChild (pluginsNode.getChildWithProperty (IDs::pluginRole, role), undoManagerToUse);

    juce::ValueTree instanceTree (IDs::PLUGININSTANCE);
    instanceTree.setProperty (IDs::pluginRole, role, nullptr);

    PluginInstance instance (instanceTree);
    instance.setDescription (description, nullptr);

    pluginsNode.addChild (instanceTree, -1, undoManagerToUse);

    return instance;
}

void ProjectModel::removePluginForRole (const juce::String& role, juce::UndoManager* undoManagerToUse)
{
    auto pluginsNode = state.getChildWithName (IDs::PLUGINS);

    if (pluginsNode.isValid())
        pluginsNode.removeChild (pluginsNode.getChildWithProperty (IDs::pluginRole, role), undoManagerToUse);
}

juce::String ProjectModel::getName() const
{
    return state[IDs::projectName];
}

void ProjectModel::setName (const juce::String& newName, juce::UndoManager* undoManagerToUse)
{
    state.setProperty (IDs::projectName, newName, undoManagerToUse);
}

//==============================================================================
// 仕様書5.9：ループ再生（Phase 48）

bool ProjectModel::isLoopEnabled() const
{
    return state.getProperty (IDs::loopEnabled, false);
}

void ProjectModel::setLoopEnabled (bool shouldLoop, juce::UndoManager* undoManagerToUse)
{
    state.setProperty (IDs::loopEnabled, shouldLoop, undoManagerToUse);
}

// 8.139：**保存されているのは拍のほう**（Phase 177）。
// テンポを変えても「2小節目から4小節目まで」のまま動きません

double ProjectModel::getLoopStartBeats() const
{
    return juce::jmax (0.0, (double) state.getProperty (IDs::loopStartBeats, 0.0));
}

double ProjectModel::getLoopEndBeats() const
{
    return juce::jmax (0.0, (double) state.getProperty (IDs::loopEndBeats, 0.0));
}

double ProjectModel::getLoopStartTime() const
{
    return getTimeForBeatPosition (getLoopStartBeats());
}

double ProjectModel::getLoopEndTime() const
{
    return getTimeForBeatPosition (getLoopEndBeats());
}

void ProjectModel::setLoopRangeBeats (double startBeats, double endBeats,
                                       juce::UndoManager* undoManagerToUse)
{
    // ドラッグは右から左へも行われる。向きを呼び出し側に気にさせない
    if (endBeats < startBeats)
        std::swap (startBeats, endBeats);

    startBeats = juce::jmax (0.0, startBeats);
    endBeats = juce::jmax (0.0, endBeats);

    if (endBeats <= startBeats)
        return;   // 長さ0のループは折り返しようがない

    state.setProperty (IDs::loopStartBeats, startBeats, undoManagerToUse);
    state.setProperty (IDs::loopEndBeats, endBeats, undoManagerToUse);
}

void ProjectModel::setLoopRange (double startSeconds, double endSeconds, juce::UndoManager* undoManagerToUse)
{
    setLoopRangeBeats (getBeatPositionAt (juce::jmax (0.0, startSeconds)),
                        getBeatPositionAt (juce::jmax (0.0, endSeconds)),
                        undoManagerToUse);
}

//==============================================================================
// 仕様書5.9：マーカー（Phase 49）

Marker::Marker (juce::ValueTree treeToWrap)
    : state (std::move (treeToWrap))
{
    jassert (! state.isValid() || state.hasType (IDs::MARKER));
}

Marker Marker::create (double timeBeats, const juce::String& name, juce::UndoManager* undoManager)
{
    // 8.139：**拍で作ります**（Phase 177）。`Note::create()`と同じ理由（8.138）
    juce::ValueTree m (IDs::MARKER);

    m.setProperty (IDs::markerId, juce::Uuid().toString(), undoManager);
    m.setProperty (IDs::markerBeats, juce::jmax (0.0, timeBeats), undoManager);
    m.setProperty (IDs::markerName, name, undoManager);

    return Marker (m);
}

juce::String Marker::getId() const   { return state[IDs::markerId]; }
juce::String Marker::getName() const { return state[IDs::markerName]; }

// 8.139：**保存されているのは拍のほう**（Phase 177）。ノートと同じ形（8.138）
double Marker::getTimeBeats() const
{
    return juce::jmax (0.0, (double) state.getProperty (IDs::markerBeats, 0.0));
}

void Marker::setTimeBeats (double newTimeBeats, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::markerBeats, juce::jmax (0.0, newTimeBeats), undoManager);
}

double Marker::getTime() const
{
    return MusicalTime::getSecondsFor (state, getTimeBeats());
}

void Marker::setTime (double newTimeSeconds, juce::UndoManager* undoManager)
{
    setTimeBeats (MusicalTime::getBeatsFor (state, juce::jmax (0.0, newTimeSeconds)), undoManager);
}

void Marker::setName (const juce::String& newName, juce::UndoManager* undoManager)
{
    state.setProperty (IDs::markerName, newName, undoManager);
}

//==============================================================================
namespace
{
    /** `<MARKERS>`ノードを返す。古いプロジェクトには無いため、必要なら作る。 */
    juce::ValueTree getOrCreateMarkersNode (juce::ValueTree& projectState, juce::UndoManager* undoManager)
    {
        auto node = projectState.getChildWithName (IDs::MARKERS);

        if (! node.isValid())
        {
            node = juce::ValueTree (IDs::MARKERS);
            projectState.addChild (node, -1, undoManager);
        }

        return node;
    }
}

int ProjectModel::getNumMarkers() const
{
    auto markersNode = state.getChildWithName (IDs::MARKERS);
    int count = 0;

    for (int i = 0; i < markersNode.getNumChildren(); ++i)
        if (markersNode.getChild (i).hasType (IDs::MARKER))
            ++count;

    return count;
}

Marker ProjectModel::getMarker (int index) const
{
    auto markersNode = state.getChildWithName (IDs::MARKERS);
    int count = 0;

    for (int i = 0; i < markersNode.getNumChildren(); ++i)
    {
        auto child = markersNode.getChild (i);

        if (child.hasType (IDs::MARKER))
        {
            if (count == index)
                return Marker (child);

            ++count;
        }
    }

    return Marker (juce::ValueTree());
}

Marker ProjectModel::addMarker (double timeSeconds, const juce::String& name, juce::UndoManager* undoManagerToUse)
{
    // 8.139：換算は**プロジェクトの表**で（Phase 177）。ここは繋がっているので引けます
    return addMarkerBeats (getBeatPositionAt (juce::jmax (0.0, timeSeconds)), name, undoManagerToUse);
}

Marker ProjectModel::addMarkerBeats (double timeBeats, const juce::String& name,
                                      juce::UndoManager* undoManagerToUse)
{
    timeBeats = juce::jmax (0.0, timeBeats);

    // 同じ位置に重ねない。**連打で見えない重なりができる**と、
    // 「次のマーカーへ」が同じ場所に止まり続ける
    const int numMarkers = getNumMarkers();

    for (int i = 0; i < numMarkers; ++i)
    {
        auto existing = getMarker (i);

        if (juce::approximatelyEqual (existing.getTimeBeats(), timeBeats))
            return existing;
    }

    auto marker = Marker::create (timeBeats, name, undoManagerToUse);
    auto markersNode = getOrCreateMarkersNode (state, undoManagerToUse);

    // 時刻の順を保った位置へ挿す（前後への移動が前から見ていくだけで済むように）
    int insertIndex = markersNode.getNumChildren();

    for (int i = 0; i < markersNode.getNumChildren(); ++i)
    {
        auto child = markersNode.getChild (i);

        if (child.hasType (IDs::MARKER)
            && (double) child.getProperty (IDs::markerBeats, 0.0) > marker.getTimeBeats())
        {
            insertIndex = i;
            break;
        }
    }

    markersNode.addChild (marker.state, insertIndex, undoManagerToUse);
    return marker;
}

void ProjectModel::removeMarker (const Marker& marker, juce::UndoManager* undoManagerToUse)
{
    state.getChildWithName (IDs::MARKERS).removeChild (marker.state, undoManagerToUse);
}

Marker ProjectModel::findMarkerAfter (double timeSeconds) const
{
    const int numMarkers = getNumMarkers();

    // 時刻順に並んでいるので、条件を満たした最初のものが「いちばん近い後」。
    // 同じ位置のものは「後」に含めない（押しても動かないため）
    for (int i = 0; i < numMarkers; ++i)
    {
        auto marker = getMarker (i);

        if (marker.getTime() > timeSeconds + 1.0e-6)
            return marker;
    }

    return Marker (juce::ValueTree());
}

Marker ProjectModel::findMarkerBefore (double timeSeconds) const
{
    Marker found { juce::ValueTree() };   // 丸括弧だと関数宣言に化ける（3.1のところと同じ）
    const int numMarkers = getNumMarkers();

    for (int i = 0; i < numMarkers; ++i)
    {
        auto marker = getMarker (i);

        if (marker.getTime() >= timeSeconds - 1.0e-6)
            break;

        found = marker;
    }

    return found;
}

void ProjectModel::sortMarkers (juce::UndoManager* undoManagerToUse)
{
    auto markersNode = state.getChildWithName (IDs::MARKERS);

    if (! markersNode.isValid())
        return;

    struct ByTime
    {
        static int compareElements (const juce::ValueTree& a, const juce::ValueTree& b)
        {
            // 8.139：**拍で並べる**（Phase 177）。秒と同じ向きに並びます
            const double timeA = a.getProperty (IDs::markerBeats, 0.0);
            const double timeB = b.getProperty (IDs::markerBeats, 0.0);

            return timeA < timeB ? -1 : (timeA > timeB ? 1 : 0);
        }
    };

    ByTime comparator;
    markersNode.sort (comparator, undoManagerToUse, true);
}

juce::String ProjectModel::getNextMarkerName() const
{
    // 使われていない一番小さい番号を探す。消してから足したときに
    // 番号が飛び続けないようにするため
    for (int number = 1; ; ++number)
    {
        const juce::String candidate = "Marker " + juce::String (number);
        bool taken = false;

        for (int i = 0; i < getNumMarkers(); ++i)
            if (getMarker (i).getName() == candidate)
            {
                taken = true;
                break;
            }

        if (! taken)
            return candidate;
    }
}

double ProjectModel::getTempo() const
{
    return state.getProperty (IDs::tempo, 120.0);
}

void ProjectModel::setTempo (double newTempo, juce::UndoManager* undoManagerToUse)
{
    state.setProperty (IDs::tempo, newTempo, undoManagerToUse);
}

juce::String ProjectModel::getTimeSignature() const
{
    return state.getProperty (IDs::timeSignature, "4/4").toString();
}

bool ProjectModel::isValidTimeSignature (const juce::String& text)
{
    // "分子/分母" の形だけを受け付ける。ここを緩くすると、ルーラーの小節線や
    // グルーヴのパターン長が黙って4拍子扱いへ落ちる（getBeatsPerBar参照）。
    // **判定はこの1箇所**（Phase 145）——フッターもレーンもここを通します
    const int numerator = text.upToFirstOccurrenceOf ("/", false, false).trim().getIntValue();
    const int denominator = text.fromLastOccurrenceOf ("/", false, false).trim().getIntValue();

    if (numerator <= 0 || numerator > 32 || denominator <= 0)
        return false;

    // 分母は2の累乗（2/4/8/16…）のみ。譜面として意味を持たない値を弾く
    return (denominator & (denominator - 1)) == 0 && denominator <= 32;
}

bool ProjectModel::setTimeSignature (const juce::String& newTimeSignature, juce::UndoManager* undoManagerToUse)
{
    if (! isValidTimeSignature (newTimeSignature))
        return false;

    const int numerator = newTimeSignature.upToFirstOccurrenceOf ("/", false, false).trim().getIntValue();
    const int denominator = newTimeSignature.fromLastOccurrenceOf ("/", false, false).trim().getIntValue();

    state.setProperty (IDs::timeSignature,
                        juce::String (numerator) + "/" + juce::String (denominator),
                        undoManagerToUse);
    return true;
}

SnapGrid ProjectModel::getSnapGrid() const
{
    // 既定は拍。**古いプロジェクト（Phase 53以前）にはこのプロパティがありません。**
    // そのときも寄る側にしておく（読み取れない値の扱いはsnapGridFromString()を参照）
    return snapGridFromString (state.getProperty (IDs::snapGrid, "1/4").toString());
}

void ProjectModel::setSnapGrid (SnapGrid newGrid)
{
    // **UndoManagerを渡さない。** 刻みは「何を作ったか」ではなく「どう作るか」なので、
    // Undoを1回押したときに戻ってほしいのは編集内容のほうです
    state.setProperty (IDs::snapGrid, snapGridToString (newGrid), nullptr);
}

double ProjectModel::getSnapSecondsAt (double atTime) const
{
    // 「マス1つの大きさ」を秒で欲しいとき用（ノートの既定の長さ、ループの最短）。
    // **寄せるのには使いません**（`snapTime()`は拍の座標で決める。8.98／Phase 139）
    return snapSecondsFor (getSnapGrid(), getTempoAt (atTime), getBeatsPerBarAt (atTime));
}

//==============================================================================
// 8.98：**寄せ先は「拍の座標」で決める**（Phase 139）
//
// Phase 138まで、寄せ先は `floor (秒 / 目盛りの秒数 + 0.5) * 目盛りの秒数` でした。
// **テンポが曲の途中で変わると、その先で拍から外れます**——目盛りの秒数は
// 「いまのテンポ」1つで作った値なので、変化点の向こうでは拍と合いません。
//
// 拍で刻んで最後に秒へ戻せば、**どこでも拍の上に乗ります**。
// **小節だけは別扱い**です（拍で表せない。`snapGridBeats()`の説明）。
//==============================================================================

/** 寄せる実体。`roundDown`がtrueなら手前の目盛りへ（切り捨て）。 */
static double snapToGridPosition (const ProjectModel& project, double seconds, bool roundDown)
{
    const double time = juce::jmax (0.0, seconds);
    const auto grid = project.getSnapGrid();

    if (grid == SnapGrid::off)
        return time;

    // **目盛りちょうどの位置で1つ手前へ落ちないように、わずかに甘く見る。**
    // 拍 → 秒 → 拍 と往復すると、ちょうどの値が 3.9999999999 になることがあり、
    // 切り捨てでマス1つぶん戻ります（画面では「たまに1つずれる」と見える）
    constexpr double tolerance = 1.0e-9;

    if (grid == SnapGrid::bar)
    {
        // 小節線へ。**「小節の長さで割る」と書かないこと**——小節ごとに
        // 長さが違う形にした瞬間に、後ろの小節が全部ずれます（8.98）
        const int bar = project.getBarIndexAt (time + tolerance);
        const double thisBarStart = project.getBarStartTime (bar);

        if (roundDown)
            return thisBarStart;

        const double nextBarStart = project.getBarStartTime (bar + 1);

        return ((time - thisBarStart) <= (nextBarStart - time)) ? thisBarStart : nextBarStart;
    }

    const double gridBeats = snapGridBeats (grid);

    if (gridBeats <= 0.0)
        return time;

    const double beats = project.getBeatPositionAt (time) / gridBeats;
    const double index = roundDown ? std::floor (beats + tolerance)
                                   : std::floor (beats + 0.5);

    return juce::jmax (0.0, project.getTimeForBeatPosition (index * gridBeats));
}

double ProjectModel::snapTime (double seconds) const
{
    return snapToGridPosition (*this, seconds, false);
}

double ProjectModel::snapTimeDown (double seconds) const
{
    return snapToGridPosition (*this, seconds, true);
}

int ProjectModel::getBeatsPerBar() const
{
    // "4/4"の分子を読む。壊れた値や古いプロジェクトでも止まらないよう、
    // 読めなければ4拍子として扱う（ルーラーの目盛りが崩れるだけで済ませる）。
    const int beats = getTimeSignature().upToFirstOccurrenceOf ("/", false, false).getIntValue();

    return beats > 0 ? beats : 4;
}

//==============================================================================
// 8.98：「時刻 <-> 小節・拍」の換算（Phase 138・139で入口を作り、Phase 140で中身を入れ替え）
//
// **どの質問も`TempoMap`（`TempoMap.h`）へ落とします。** ここでValueTreeを
// 辿ったり割り算を書いたりしないこと——**表を作る場所と、表に訊く場所を分ける**のが、
// オーディオスレッドから読めるようにするための形です（1.12）。
//
// 座標は3つ：**小節 --(拍子)--> 拍 --(テンポ)--> 秒**。
// 拍子は「小節 <-> 拍」だけ、テンポは「拍 <-> 秒」だけを決めます。
//==============================================================================

const TempoMap& ProjectModel::getTempoMap() const
{
    if (! tempoMapDirty)
        return tempoMap;

    // 8.138：**表を組む式は`MusicalTime`の1箇所だけ**（Phase 176／1.27）。
    // 読み込み時の変換（`migrateTimesToBeats()`）は`setState()`より前に走るので、
    // まだこのモデルに属していないツリーから同じ表を組む必要があります
    tempoMap = MusicalTime::buildTempoMapFrom (state);

    tempoMapDirty = false;
    return tempoMap;
}

double ProjectModel::getTempoAt (double timeSeconds) const
{
    const auto& map = getTempoMap();

    return map.getTempoAtBeat (map.getBeatAtTime (timeSeconds));
}

juce::String ProjectModel::getTimeSignatureAt (double timeSeconds) const
{
    // **分子も分母も表から引くこと**（Phase 145）。分母を「曲の既定値」から
    // 組み直していたので、"6/8"の変化点が"6/4"と表示され、
    // **その文字列を書き戻すドラッグで本当に書き換わって**いました
    const auto& map = getTempoMap();
    const int bar = getBarIndexAt (timeSeconds);

    return juce::String (map.getBeatsPerBarAtBar (bar)) + "/"
             + juce::String (map.getDenominatorAtBar (bar));
}

int ProjectModel::getBeatsPerBarAt (double timeSeconds) const
{
    const auto& map = getTempoMap();
    const auto position = map.getBarPositionAtBeat (map.getBeatAtTime (timeSeconds));

    return map.getBeatsPerBarAtBar (position.bar);
}

double ProjectModel::getBeatSecondsAt (double timeSeconds) const
{
    // **1未満のテンポへ落とさない**（0除算と、拡大率が無限大になるのを防ぐ）
    return 60.0 / juce::jmax (1.0, getTempoAt (timeSeconds));
}

double ProjectModel::getBarSecondsAt (double timeSeconds) const
{
    // **「1拍の長さ×拍数」ではなく、その小節の実際の長さ**（Phase 140）。
    // 小節の途中でテンポが変わることがあるので、掛け算では合いません
    const int bar = getBarIndexAt (timeSeconds);
    const double length = getBarStartTime (bar + 1) - getBarStartTime (bar);

    return juce::jmax (0.0, length);
}

ProjectModel::BarBeat ProjectModel::getBarBeatAt (double timeSeconds) const
{
    const auto& map = getTempoMap();
    const auto position = map.getBarPositionAtBeat (map.getBeatAtTime (timeSeconds));

    BarBeat result;
    result.bar = position.bar;
    result.beat = (int) std::floor (position.beatsIntoBar);
    result.beatFraction = position.beatsIntoBar - (double) result.beat;

    // 丸め誤差で拍数ぶんの値になることがある（0.9999…が1拍に届く形）。
    // **はみ出したら次の小節の頭**として扱う——「5拍目」を返さないこと
    const int beatsPerBar = juce::jmax (1, map.getBeatsPerBarAtBar (result.bar));

    if (result.beat >= beatsPerBar)
    {
        ++result.bar;
        result.beat = 0;
        result.beatFraction = 0.0;
    }

    return result;
}

int ProjectModel::getBarIndexAt (double timeSeconds) const
{
    return getBarBeatAt (timeSeconds).bar;
}

double ProjectModel::getBarStartTime (int barIndex) const
{
    if (barIndex <= 0)
        return 0.0;

    const auto& map = getTempoMap();

    return map.getTimeForBeat (map.getBeatForBarStart (barIndex));
}

double ProjectModel::getBeatStartTime (int barIndex, int beatIndex) const
{
    const auto& map = getTempoMap();

    return map.getTimeForBeat (map.getBeatForBarStart (juce::jmax (0, barIndex))
                                 + (double) juce::jmax (0, beatIndex));
}

double ProjectModel::getNoteBlockGapSeconds() const
{
    // 8.91：塊の切れ目は**1小節ぶんの空き**。**ここでしか決めません**——
    // アレンジ画面とピアノロールが別々に計算すると、同じ曲が違う塊に見えます（8.2）。
    // **曲の頭の小節で測ります**（切れ目の長さが場所によって変わると、
    // 「なぜここで分かれるのか」が説明できなくなるため。8.102）
    return getBarSecondsAt (0.0);
}

//==============================================================================
// 8.98：拍の座標（Phase 139）
//
// **この2つは対です。** getBeatPositionAt()で拍へ直し、刻んでから
// getTimeForBeatPosition()で秒へ戻す——という往復が、寄せる操作の形になります。
//==============================================================================

double ProjectModel::getBeatPositionAt (double timeSeconds) const
{
    return getTempoMap().getBeatAtTime (timeSeconds);
}

double ProjectModel::getTimeForBeatPosition (double beatPosition) const
{
    return getTempoMap().getTimeForBeat (beatPosition);
}

//==============================================================================
// 仕様書5.1・5.9：変化点の出し入れ（Phase 140）
//
// **どちらも「同じ位置には1つだけ」**です。2つ置けると、どちらが効いているか
// 画面から読めない状態ができます（8.91の「表現できない状態は、作らせない」）。
//==============================================================================

/** TEMPOMAPノード。無ければ作る（古いプロジェクトには無い）。 */
static juce::ValueTree getOrCreateTempoMapNode (juce::ValueTree& state, juce::UndoManager* undoManager)
{
    auto node = state.getChildWithName (IDs::TEMPOMAP);

    if (! node.isValid())
    {
        node = juce::ValueTree (IDs::TEMPOMAP);
        state.addChild (node, -1, undoManager);
    }

    return node;
}

void ProjectModel::setTempoChange (double beatPosition, double bpm, juce::UndoManager* undoManagerToUse)
{
    // **0拍目は置けません。** そこはsetTempo()（曲の頭の値）の担当で、
    // 両方が同じ場所を持つと、どちらが勝つかを説明できなくなります
    if (! (beatPosition > 0.0) || bpm < 1.0)
        return;

    auto node = getOrCreateTempoMapNode (state, undoManagerToUse);

    for (int i = 0; i < node.getNumChildren(); ++i)
    {
        auto child = node.getChild (i);

        if (child.hasType (IDs::TEMPOCHANGE)
             && juce::approximatelyEqual ((double) child.getProperty (IDs::tempoChangeBeat, -1.0), beatPosition))
        {
            child.setProperty (IDs::tempo, bpm, undoManagerToUse);
            return;
        }
    }

    juce::ValueTree change (IDs::TEMPOCHANGE);
    change.setProperty (IDs::tempoChangeBeat, beatPosition, nullptr);
    change.setProperty (IDs::tempo, bpm, nullptr);
    node.addChild (change, -1, undoManagerToUse);
}

void ProjectModel::removeTempoChange (double beatPosition, juce::UndoManager* undoManagerToUse)
{
    auto node = state.getChildWithName (IDs::TEMPOMAP);

    if (! node.isValid())
        return;

    for (int i = node.getNumChildren(); --i >= 0;)
    {
        auto child = node.getChild (i);

        if (child.hasType (IDs::TEMPOCHANGE)
             && juce::approximatelyEqual ((double) child.getProperty (IDs::tempoChangeBeat, -1.0), beatPosition))
            node.removeChild (i, undoManagerToUse);
    }
}

bool ProjectModel::setTimeSignatureChange (int bar, const juce::String& newTimeSignature,
                                            juce::UndoManager* undoManagerToUse)
{
    // **0小節目は置けません**（setTimeSignature()の担当）
    if (bar <= 0)
        return false;

    // **判定は`isValidTimeSignature()`の1箇所**（Phase 145）。ここに書き写していたので、
    // 片方だけ緩くなる余地が残っていました（8.12の「入口が2つ」）
    if (! isValidTimeSignature (newTimeSignature))
        return false;

    const int numerator = newTimeSignature.upToFirstOccurrenceOf ("/", false, false).trim().getIntValue();
    const int denominator = newTimeSignature.fromLastOccurrenceOf ("/", false, false).trim().getIntValue();

    const juce::String normalised = juce::String (numerator) + "/" + juce::String (denominator);

    auto node = getOrCreateTempoMapNode (state, undoManagerToUse);

    for (int i = 0; i < node.getNumChildren(); ++i)
    {
        auto child = node.getChild (i);

        if (child.hasType (IDs::TIMESIGCHANGE) && (int) child.getProperty (IDs::timeSigChangeBar, -1) == bar)
        {
            child.setProperty (IDs::timeSignature, normalised, undoManagerToUse);
            return true;
        }
    }

    juce::ValueTree change (IDs::TIMESIGCHANGE);
    change.setProperty (IDs::timeSigChangeBar, bar, nullptr);
    change.setProperty (IDs::timeSignature, normalised, nullptr);
    node.addChild (change, -1, undoManagerToUse);

    return true;
}

void ProjectModel::removeTimeSignatureChange (int bar, juce::UndoManager* undoManagerToUse)
{
    auto node = state.getChildWithName (IDs::TEMPOMAP);

    if (! node.isValid())
        return;

    for (int i = node.getNumChildren(); --i >= 0;)
    {
        auto child = node.getChild (i);

        if (child.hasType (IDs::TIMESIGCHANGE) && (int) child.getProperty (IDs::timeSigChangeBar, -1) == bar)
            node.removeChild (i, undoManagerToUse);
    }
}

Track ProjectModel::addTrack (const juce::String& name, TrackType type,
                               const juce::String& afterTrackId)
{
    beginAction (utf8 ("トラックの追加"));
    auto newTrack = Track::create (name, type, &undoManager);

    // 8.61：**作るたびに色を変える**（Phase 99／改善案⑫）。
    // 全部が同じ灰色だと、クリップやヘッダーに色を反映しても見分けが付かない。
    // **何本目かで決める**ので、消して足しても色が偏りにくい
    newTrack.state.setProperty (IDs::trackColor, Track::getDefaultColourForIndex (getNumTracks()),
                                 &undoManager);

    // 8.60：**選んだトラックの真下へ**（Phase 97／改善案⑱）。
    //
    // それまでは必ず末尾でした。10本も並ぶと、足したトラックが画面の外に出てしまい、
    // **足すたびにスクロールして探して、そこまで運ぶ**ことになっていました。
    //
    // コードトラックは常に先頭なので、この計算に混ぜません（下で引き上げます）
    int insertIndex = -1;
    juce::String parentFolderId;

    if (type != TrackType::Chord && afterTrackId.isNotEmpty())
    {
        for (int i = 0; i < getNumTracks(); ++i)
        {
            auto anchor = getTrack (i);

            if (anchor.getId() != afterTrackId)
                continue;

            // **フォルダを指したら、その中の先頭へ。** それ以外は指したトラックの隣
            // （＝同じフォルダの中）。どちらも「真下」であることに変わりはない
            parentFolderId = (anchor.getType() == TrackType::Folder) ? anchor.getId()
                                                                     : anchor.getParentFolderId();
            insertIndex = i + 1;
            break;
        }
    }

    if (parentFolderId.isNotEmpty())
        newTrack.setParentFolderId (parentFolderId, &undoManager);

    tracksNode.addChild (newTrack.state, insertIndex, &undoManager);

    // 8.60：**畳んでいるフォルダは開く**（Phase 97）。
    // 畳んだままだと行の高さが0になり（8.50）、足したのに何も起きていないように見える。
    // 入れ子になっている場合もあるので、上まで辿って全部開く
    for (auto folderId = parentFolderId; folderId.isNotEmpty();)
    {
        auto folder = findTrackById (folderId);

        if (! folder.state.getParent().isValid())
            break;

        if (folder.isCollapsed())
            folder.setCollapsed (false);

        folderId = folder.getParentFolderId();
    }

    // 仕様書5.2.3：コードトラックは一番上に固定（Phase 60／8.20）。
    // 末尾に足してから引き上げているのは、**追加の手順を種別ごとに分けないため**
    if (type == TrackType::Chord)
        ensureChordTrackIsFirst();

    return newTrack;
}

Track ProjectModel::duplicateTrack (const Track& source)
{
    if (! source.state.isValid() || ! source.state.getParent().isValid())
        return Track (juce::ValueTree());

    // 仕様書5.2.3：コードトラックは1本だけ（ルーラー直下の固定行。8.20）。
    // **押せる項目として出さないほうがよい**が、モデル側でも守っておく
    if (source.getType() == TrackType::Chord)
        return Track (juce::ValueTree());

    beginAction (utf8 ("トラックの複製"));

    auto copy = source.state.createCopy();

    // **IDは必ず振り直すこと。** 同じIDのトラックが2本あると、
    // エンジンへの問い合わせ（`trackId`で引く）が先に見つかった1本に当たり続け、
    // 音量もメーターも片方にしか効かない（1.32と同じ「指す先」の話）。
    copy.setProperty (IDs::trackId, juce::Uuid().toString(), nullptr);
    copy.setProperty (IDs::trackName, source.getName() + utf8 (" のコピー"), nullptr);

    // **録音待機は引き継がない。** 録音先は1本に限られているので、
    // 複製した瞬間に2本が待機している状態を作らない（`mouseDown`の録音待機の扱いと同じ）
    copy.setProperty (IDs::armed, false, nullptr);

    // クリップとコード区間のIDも振り直す。**ここを忘れると、
    // 片方のクリップを消したつもりが両方から消える**（同じIDで引かれるため）
    if (auto clipsNode = copy.getChildWithName (IDs::CLIPS); clipsNode.isValid())
        for (int i = 0; i < clipsNode.getNumChildren(); ++i)
            clipsNode.getChild (i).setProperty (IDs::clipId, juce::Uuid().toString(), nullptr);

    if (auto regionsNode = copy.getChildWithName (IDs::CHORDREGIONS); regionsNode.isValid())
        for (int i = 0; i < regionsNode.getNumChildren(); ++i)
            regionsNode.getChild (i).setProperty (IDs::chordRegionId, juce::Uuid().toString(), nullptr);

    // **元のトラックのすぐ下へ入れる**（末尾だと、どれを複製したのか分からなくなる）
    const int sourceIndex = tracksNode.indexOf (source.state);
    tracksNode.addChild (copy, sourceIndex + 1, &undoManager);

    // 音源・インサートは`<INSTRUMENT>`/`<INSERTS>`ごとコピーされ、
    // エンジンが作り直すときにモデルから読み直す（設計書3.8）。
    // **ここでプラグインを触る必要はない**（`AudioEngine::rebuildTrackNodes()`が拾う）

    return Track (copy);
}

void ProjectModel::removeTrack (const Track& track)
{
    const auto removedId = track.getId();

    beginAction (utf8 ("トラックの削除"));

    // Phase 33：**消す前に、他のトラックからの参照を外す。**
    // 1つのUndoステップにまとめたいので、削除より先にここで行う。
    for (int t = 0; t < getNumTracks(); ++t)
    {
        auto other = getTrack (t);

        if (other.getId() == removedId)
            continue;

        // 仕様書5.2.2：このトラックを送り先にしているセンド。
        // **後ろから消すこと**（前から消すと以降の番号がずれる）。
        for (int s = other.getNumSends(); --s >= 0;)
            if (other.getSend (s).getTargetTrackId() == removedId)
                other.removeSend (s, &undoManager);

        // 仕様書5.2.4：VCAのリンク
        if (other.getType() == TrackType::VCA && other.isTrackLinkedToVca (removedId))
            other.unlinkTrackFromVca (removedId, &undoManager);

        // 仕様書5.7.2：サイドチェインのソース
        for (int i = 0; i < other.getNumInserts(); ++i)
            if (other.getInsert (i).getSidechainSourceTrackId() == removedId)
                other.getInsert (i).setSidechainSourceTrackId ({}, &undoManager);

        // 8.50：**消えたフォルダの中身は、外へ出す**（Phase 89／D2）。
        // 親のIDだけ残ると、深さも「隠れているか」も答えが出ない迷子になる
        if (other.getParentFolderId() == removedId)
            other.setParentFolderId ({}, &undoManager);
    }

    tracksNode.removeChild (track.state, &undoManager);
}

bool ProjectModel::hasPinnedChordTrack() const
{
    return getNumTracks() > 0 && getTrack (0).getType() == TrackType::Chord;
}

void ProjectModel::ensureChordTrackIsFirst()
{
    for (int i = 1; i < getNumTracks(); ++i)
    {
        if (getTrack (i).getType() != TrackType::Chord)
            continue;

        // **Undoには積まない。** 並びを直したこと自体をUndoで戻せても意味が無く、
        // 戻した先は「決まりを満たしていない状態」になる
        tracksNode.moveChild (i, 0, nullptr);
        break;   // 2本目以降のコードトラックは動かさない（先頭の1本だけを固定行に使う）
    }
}

//==============================================================================
// 8.50：フォルダトラック（Phase 89／D2。仕様書5.2・設計書1.3）
//==============================================================================

int ProjectModel::getTrackFolderDepth (const Track& track) const
{
    if (! track.state.getParent().isValid())
        return 0;

    int depth = 0;
    auto parentId = track.getParentFolderId();

    // **回数を区切ること。** 万一輪になっていても、ここで止まれば画面は固まらない
    // （輪を作らせない見張りは`canMoveTrackIntoFolder()`にある）
    for (int guard = 0; guard < maxFolderDepth && parentId.isNotEmpty(); ++guard)
    {
        auto parent = findTrackById (parentId);

        if (! parent.state.getParent().isValid())
            break;   // 親が消えている（削除された）ので、そこで打ち切る

        ++depth;
        parentId = parent.getParentFolderId();
    }

    return depth;
}

bool ProjectModel::isTrackHiddenByCollapsedFolder (const Track& track) const
{
    if (! track.state.getParent().isValid())
        return false;

    auto parentId = track.getParentFolderId();

    for (int guard = 0; guard < maxFolderDepth && parentId.isNotEmpty(); ++guard)
    {
        auto parent = findTrackById (parentId);

        if (! parent.state.getParent().isValid())
            break;

        // **1つでも畳んでいる親があれば隠れる**（孫まで効く）
        if (parent.isCollapsed())
            return true;

        parentId = parent.getParentFolderId();
    }

    return false;
}

bool ProjectModel::canMoveTrackIntoFolder (const Track& track, const juce::String& folderId) const
{
    if (! track.state.getParent().isValid() || folderId.isEmpty())
        return false;

    // 自分自身の中には入れない
    if (track.getId() == folderId)
        return false;

    auto folder = findTrackById (folderId);

    if (! folder.state.getParent().isValid() || folder.getType() != TrackType::Folder)
        return false;

    // **自分の子孫の中には入れない**（輪になって、たどると戻ってこなくなる）。
    // 行き先の親をたどって、自分に行き当たらないかを見る
    auto parentId = folder.getParentFolderId();

    for (int guard = 0; guard < maxFolderDepth && parentId.isNotEmpty(); ++guard)
    {
        if (parentId == track.getId())
            return false;

        auto parent = findTrackById (parentId);

        if (! parent.state.getParent().isValid())
            break;

        parentId = parent.getParentFolderId();
    }

    return true;
}

int ProjectModel::getLastRowOfFolder (const juce::String& folderId) const
{
    int folderIndex = -1;

    for (int i = 0; i < getNumTracks(); ++i)
        if (getTrack (i).getId() == folderId)
        {
            folderIndex = i;
            break;
        }

    if (folderIndex < 0)
        return -1;

    // **中身は必ずフォルダの真下に並んでいる**（`moveTrackIntoFolder()`がそう置く）。
    // 深さが浅くなったところで、そのフォルダの範囲は終わり
    const int folderDepth = getTrackFolderDepth (getTrack (folderIndex));
    int last = folderIndex;

    for (int i = folderIndex + 1; i < getNumTracks(); ++i)
    {
        if (getTrackFolderDepth (getTrack (i)) <= folderDepth)
            break;

        last = i;
    }

    return last;
}

void ProjectModel::moveTrackIntoFolder (const Track& track, const juce::String& folderId,
                                         bool startNewAction)
{
    if (! track.state.getParent().isValid())
        return;

    // フォルダから出すだけなら、並びは触らない（その場に残す）
    if (folderId.isEmpty())
    {
        if (startNewAction)
            beginAction (utf8 ("フォルダから出す"));   // 8.203（Phase 237）
        Track (track.state).setParentFolderId ({}, &undoManager);
        return;
    }

    if (! canMoveTrackIntoFolder (track, folderId))
        return;

    if (startNewAction)
        beginAction (utf8 ("フォルダへ入れる"));   // 8.203（Phase 237）

    Track (track.state).setParentFolderId (folderId, &undoManager);

    // **中身はフォルダの真下へ並べる。** 離れた場所に居ると、
    // 「入っているのに隣に無い」状態になり、畳んだときの見え方も決まらない
    int fromIndex = -1;

    for (int i = 0; i < getNumTracks(); ++i)
        if (getTrack (i).getId() == track.getId())
        {
            fromIndex = i;
            break;
        }

    const int lastRow = getLastRowOfFolder (folderId);

    if (fromIndex < 0 || lastRow < 0)
        return;

    // 既に最後尾に居るなら動かさない（`moveChild`は同じ位置でも履歴を作る）
    const int toIndex = (fromIndex > lastRow) ? lastRow + 1 : lastRow;

    if (fromIndex != toIndex)
        tracksNode.moveChild (fromIndex, toIndex, &undoManager);
}
juce::StringArray ProjectModel::getFolderDescendantIds (const juce::String& folderId) const
{
    juce::StringArray ids;

    if (folderId.isEmpty())
        return ids;

    // **並び順のまま集める。** 動かした後も同じ順で並べ直したいため
    for (int i = 0; i < getNumTracks(); ++i)
    {
        auto candidate = getTrack (i);
        auto parentId = candidate.getParentFolderId();

        for (int guard = 0; guard < maxFolderDepth && parentId.isNotEmpty(); ++guard)
        {
            if (parentId == folderId)
            {
                ids.add (candidate.getId());
                break;
            }

            auto parent = findTrackById (parentId);

            if (! parent.state.getParent().isValid())
                break;

            parentId = parent.getParentFolderId();
        }
    }

    return ids;
}

void ProjectModel::moveTrackToSlot (const Track& track, int toIndex,
                                     const juce::String& newParentFolderId,
                                     bool startNewAction)
{
    if (! track.state.getParent().isValid())
        return;

    int fromIndex = -1;

    for (int i = 0; i < getNumTracks(); ++i)
        if (getTrack (i).getId() == track.getId())
        {
            fromIndex = i;
            break;
        }

    if (fromIndex < 0)
        return;

    // 仕様書5.2.3：コードトラックは一番上に固定（8.20）。**moveTrack()と同じ決まり**
    if (hasPinnedChordTrack())
    {
        if (fromIndex == 0)
            return;

        toIndex = juce::jmax (1, toIndex);
    }

    toIndex = juce::jlimit (0, getNumTracks() - 1, toIndex);

    const auto currentParent = track.getParentFolderId();
    const bool parentChanged = (currentParent != newParentFolderId);

    if (fromIndex == toIndex && ! parentChanged)
        return;   // 何も変わらない（`moveChild`は同じ位置でも履歴を作る）

    // 8.51：**動かすのと入れ先を変えるのは1回の操作**（Phase 90／D2。3.1）
    // 8.203：**まとめて動かすときは、区切りを開かない**（Phase 237/本人の報告）。
    // ここで毎回`beginAction()`を呼ぶので、**5本まとめて入れるとCtrl+Zが5回**要りました。
    // 呼ぶ側が先に区切りを開いているときは、そちらへ相乗りします
    if (startNewAction)
        beginAction (utf8 ("トラックの並べ替え"));

    if (parentChanged)
    {
        // 行き先が自分の子孫なら入れない（輪になる）
        if (newParentFolderId.isEmpty() || canMoveTrackIntoFolder (track, newParentFolderId))
            Track (track.state).setParentFolderId (newParentFolderId, &undoManager);
    }

    // **フォルダを動かすときは、中身も連れていく。** 置いていくと、
    // 「入っているのに離れた場所にいる」状態になり、畳んだときの見え方も決まらない
    const auto descendants = (track.getType() == TrackType::Folder)
                                 ? getFolderDescendantIds (track.getId())
                                 : juce::StringArray();

    if (fromIndex != toIndex)
        tracksNode.moveChild (fromIndex, toIndex, &undoManager);

    // 連れていく中身は、フォルダの真下へ順番に並べ直す
    for (int i = 0; i < descendants.size(); ++i)
    {
        int folderIndex = -1;
        int childIndex = -1;

        for (int t = 0; t < getNumTracks(); ++t)
        {
            const auto id = getTrack (t).getId();

            if (id == track.getId())
                folderIndex = t;
            else if (id == descendants[i])
                childIndex = t;
        }

        if (folderIndex < 0 || childIndex < 0)
            continue;

        const int wanted = folderIndex + 1 + i;

        if (childIndex != wanted && juce::isPositiveAndBelow (wanted, getNumTracks()))
            tracksNode.moveChild (childIndex, wanted, &undoManager);
    }
}
void ProjectModel::moveTrack (int fromIndex, int toIndex)
{
    if (fromIndex == toIndex
         || ! juce::isPositiveAndBelow (fromIndex, getNumTracks())
         || ! juce::isPositiveAndBelow (toIndex, getNumTracks()))
        return;

    // 仕様書5.2.3：コードトラックは一番上に固定（Phase 60／8.20）。
    //
    // **モデル側で守ること。** 並べ替えの入口はアレンジ画面のドラッグと
    // インスペクタの「上へ／下へ」の2つあり、片方だけに書くと必ず食い違う（1.27）。
    if (hasPinnedChordTrack())
    {
        if (fromIndex == 0)
            return;   // コードトラック自身は動かせない

        toIndex = juce::jmax (1, toIndex);   // 他のトラックはその上へ行けない

        if (fromIndex == toIndex)
            return;
    }

    beginAction (utf8 ("トラックの並べ替え"));
    tracksNode.moveChild (fromIndex, toIndex, &undoManager);
}

int ProjectModel::getNumTracks() const
{
    return tracksNode.getNumChildren();
}

Track ProjectModel::getTrack (int index) const
{
    return Track (tracksNode.getChild (index));
}

bool ProjectModel::writeCopyToFile (const juce::File& file) const
{
    // 「今の内容をファイルへ書く」だけを行う。currentFileも未保存フラグも変えないので、
    // オートセーブ（仕様書5.1）のように、裏で別ファイルへ書き出す用途に使える。
    auto xml = state.createXml();

    return xml != nullptr && xml->writeTo (file);
}

bool ProjectModel::saveToFile (const juce::File& file)
{
    // 仕様書5.1：上書き保存の前に、直前の内容をバックアップとして退避する。
    // 保存操作そのものが失敗した場合や、誤った内容で上書きしてしまった場合の保険。
    if (file.existsAsFile())
    {
        auto backupFile = file.getSiblingFile (file.getFileName() + ".bak");
        backupFile.deleteFile();
        file.copyFileTo (backupFile); // 失敗しても保存自体は続行する（保険が無いだけ）
    }

    if (! writeCopyToFile (file))
        return false;

    currentFile = file;
    markAsSaved();
    return true;
}

namespace
{
    /**
        仕様書7章「プロジェクトファイルの後方互換性」：Phase 13b以前の形式からの移行。

        以前は音源をプロジェクト直下の`<PLUGINS>`へ`role="instrument"`で**1台だけ**持ち、
        全MIDIトラックのノートをそこへまとめて送っていた。Phase 14でトラックごとの
        音源割り当てへ変えたため、古い形式の音源は最初のMIDIトラックへ移す。

        setState()より**前**に呼ぶこと。後から書き換えると、リスナーが付いた状態で
        変更が走り「開いた直後なのに未保存」になってしまう。
    */
    void migrateLegacyInstrumentToTrack (juce::ValueTree projectState)
    {
        auto pluginsNode = projectState.getChildWithName (IDs::PLUGINS);

        if (! pluginsNode.isValid())
            return;

        auto legacyInstrument = pluginsNode.getChildWithProperty (IDs::pluginRole,
                                                                   ProjectModel::getInstrumentRole());

        if (! legacyInstrument.isValid())
            return;

        auto tracksNode = projectState.getChildWithName (IDs::TRACKS);

        for (auto trackState : tracksNode)
        {
            if (trackTypeFromString (trackState[IDs::trackType]) != TrackType::Midi)
                continue;

            if (trackState.getChildWithName (IDs::INSTRUMENT).isValid())
                break; // 既に新形式で音源を持っている（＝移行済み）

            // addChild()は親を持つツリーを受け付けないため、先に外してから移す
            pluginsNode.removeChild (legacyInstrument, nullptr);
            legacyInstrument.removeProperty (IDs::pluginRole, nullptr); // 役割はもう使わない

            juce::ValueTree instrumentNode (IDs::INSTRUMENT);
            instrumentNode.addChild (legacyInstrument, -1, nullptr);
            trackState.addChild (instrumentNode, -1, nullptr);
            return;
        }

        // MIDIトラックが1本も無い場合は、移し先が無いのでそのまま残す
        // （消してしまうと、後からトラックを足しても音源情報が戻らない）。
    }

    /**
        仕様書7章：8.56／Phase 94（D3）で、開いているレーンの持ち方を変えた。

        Phase 93までは**トラックが`automationView`で1つだけ選ぶ**形で、
        その対象をトラック行に重ねて描いていた。専用の行になったので、
        **見せるかどうかはレーン自身が`laneVisible`で持つ**。

        古いプロジェクトを開いたときに「開いていたレーンが閉じている」と
        壊れて見えるので、ここで移し替える。`migrateLegacyInstrumentToTrack()`と同じく
        **setState()より前**に呼ぶこと（後から書き換えると「開いた直後なのに未保存」になる）。
    */
    void migrateAutomationViewToLanes (juce::ValueTree projectState)
    {
        auto migrateOne = [] (juce::ValueTree owner)
        {
            const juce::String target = owner[IDs::automationView].toString();

            // **プロパティは必ず消す。** 残しておくと、次に開いたときに
            // 「閉じたはずのレーンがまた開く」ことになる
            owner.removeProperty (IDs::automationView, nullptr);

            if (target.isEmpty())
                return;

            auto automationNode = owner.getChildWithName (IDs::AUTOMATION);

            if (! automationNode.isValid())
            {
                automationNode = juce::ValueTree (IDs::AUTOMATION);
                owner.addChild (automationNode, -1, nullptr);
            }

            auto lane = automationNode.getChildWithProperty (IDs::laneTargetId, target);

            if (! lane.isValid())
            {
                // 点がまだ無いまま開いていた場合。**行だけ復元する**
                lane = juce::ValueTree (IDs::LANE);
                lane.setProperty (IDs::laneTargetId, target, nullptr);
                automationNode.addChild (lane, -1, nullptr);
            }

            lane.setProperty (IDs::laneVisible, true, nullptr);
        };

        for (auto trackState : projectState.getChildWithName (IDs::TRACKS))
            migrateOne (trackState);

        auto masterBus = projectState.getChildWithName (IDs::MASTERBUS);

        if (masterBus.isValid())
            migrateOne (masterBus);
    }

    /**
        仕様書7章：Phase 15より前は、ピアノロールがクリップの長さを伸ばさなかったため、
        **クリップの外にはみ出したノート**を持つプロジェクトが存在する。
        そのままアレンジ画面に描くと「クリップは短いのに音は続く」表示になるため、
        読み込み時に長さを合わせておく（縮めることはしないので、内容は失われない）。

        migrateLegacyInstrumentToTrack()と同じく、setState()より前に呼ぶこと。
    */
    void growMidiClipsToFitNotes (juce::ValueTree projectState)
    {
        for (auto trackState : projectState.getChildWithName (IDs::TRACKS))
        {
            if (trackTypeFromString (trackState[IDs::trackType]) != TrackType::Midi)
                continue;

            auto clipsNode = trackState.getChildWithName (IDs::CLIPS);

            for (auto clipState : clipsNode)
                if (clipState.hasType (IDs::MIDICLIP))
                    MidiClip (clipState).growToFitNotes (nullptr);
        }
    }

    /**
        8.91：**MIDIクリップを廃止して、トラック直下のノート／CCへ移す**（Phase 131）。

        `growMidiClipsToFitNotes()`の**後に**呼ぶこと。あちらが「右へはみ出した
        ノートが収まるようにクリップを伸ばす」ので、**先に通せば、いま鳴っている音は
        1つも落ちません**。落ちるのは**左端をトリムして隠したぶんだけ**です
        （伸ばしても救えないもの＝今も鳴っていないもの）。

        **一方通行です。** 変換したものを保存すると、古い版では開けません。

        `migrateLegacyInstrumentToTrack()`と同じく、`setState()`より前に呼ぶこと。
    */
    void migrateMidiClipsToTrackNotes (juce::ValueTree projectState)
    {
        for (auto trackState : projectState.getChildWithName (IDs::TRACKS))
        {
            auto clipsNode = trackState.getChildWithName (IDs::CLIPS);

            if (! clipsNode.isValid())
                continue;

            // MIDIクリップだけを集める（オーディオクリップはそのまま残す）
            juce::Array<juce::ValueTree> midiClips;

            for (auto clipState : clipsNode)
                if (clipState.hasType (IDs::MIDICLIP))
                    midiClips.add (clipState);

            if (midiClips.isEmpty())
                continue;

            auto notesNode = trackState.getChildWithName (IDs::NOTES);
            auto ccNode = trackState.getChildWithName (IDs::CCEVENTS);

            if (! notesNode.isValid())
            {
                notesNode = juce::ValueTree (IDs::NOTES);
                trackState.addChild (notesNode, -1, nullptr);
            }

            if (! ccNode.isValid())
            {
                ccNode = juce::ValueTree (IDs::CCEVENTS);
                trackState.addChild (ccNode, -1, nullptr);
            }

            for (auto clipState : midiClips)
            {
                MidiClip clip (clipState);

                // **窓の判定はモデルの1箇所を通す**（1.14）。ここで
                // `offset <= t < offset + length`と書き直すと、
                // 「変換前に鳴っていたもの」との食い違いに気づけない
                // 8.138：**時刻は古いプロパティから直に読むこと**（Phase 176）。
                // クリップの中身はまだ秒で、しかも「クリップの先頭からの相対値」です。
                // `Note::getStartTime()`は拍から換算するようになったので、
                // ここを通すと0が返ります（`MidiClip::growToFitNotes()`の説明）
                auto clipNotes = clip.state.getChildWithName (IDs::NOTES);

                for (int n = 0; n < clipNotes.getNumChildren(); ++n)
                {
                    auto noteState = clipNotes.getChild (n);
                    double timelineStart = 0.0;

                    if (! clip.contentTimeToTimeline (noteState.getProperty (IDs::noteStartTime, 0.0),
                                                       timelineStart))
                        continue;   // 左端のトリムで隠してあったぶん（今も鳴っていない）

                    auto copy = noteState.createCopy();
                    copy.setProperty (IDs::noteStartTime, timelineStart, nullptr);
                    notesNode.addChild (copy, -1, nullptr);
                }

                auto clipEvents = clip.state.getChildWithName (IDs::CCEVENTS);

                for (int c = 0; c < clipEvents.getNumChildren(); ++c)
                {
                    auto eventState = clipEvents.getChild (c);
                    double timelineTime = 0.0;

                    if (! clip.contentTimeToTimeline (eventState.getProperty (IDs::ccTime, 0.0),
                                                       timelineTime))
                        continue;

                    auto copy = eventState.createCopy();
                    copy.setProperty (IDs::ccTime, timelineTime, nullptr);
                    ccNode.addChild (copy, -1, nullptr);
                }

                clipsNode.removeChild (clipState, nullptr);
            }

            // 時刻順に並べ直す（複数のクリップから集めたので、順番が混ざっている）。
            // **CCは昇順であることを前提に読む**ので、ここを飛ばすと値がおかしくなる
            struct TimeOrder
            {
                explicit TimeOrder (const juce::Identifier& p) : property (p) {}

                int compareElements (const juce::ValueTree& a, const juce::ValueTree& b) const
                {
                    const double timeA = a[property];
                    const double timeB = b[property];

                    return timeA < timeB ? -1 : (timeA > timeB ? 1 : 0);
                }

                juce::Identifier property;
            };

            TimeOrder noteOrder (IDs::noteStartTime);
            TimeOrder ccOrder (IDs::ccTime);

            notesNode.sort (noteOrder, nullptr, true);
            ccNode.sort (ccOrder, nullptr, true);
        }
    }

    /**
        8.138：**ノートとCCの時刻を、秒から拍へ**（Phase 176／8.105の宿題3）。

        `formatVersion`が2未満のプロジェクトだけが対象です。
        変換したらプロパティの版を2に上げ、**古い秒のプロパティは消します**
        ——残しておくと、どちらが本当か分からないものが2つ並ぶことになります（1.27）。

        ### **一番最後に呼ぶこと**

        `migrateMidiClipsToTrackNotes()`より後です。あちらはクリップの中身を
        **曲の時刻へ均す**ところまでやるので、拍へ直せるのはその後だけです
        （クリップの中の時刻は「クリップの先頭から」の座標で、曲の拍とは別もの）。

        ### 変換で音はずれません

        `beats = getBeatAtTime(秒)`。**同じ表で秒へ戻せば元の値になります**（8.101）。
        テンポの変化点を持ったプロジェクトでも、変化点はもともと拍で保存されているので
        （8.102）、その位置関係ごと保たれます。

        ### 戻せません

        **これは一方通行です。** 保存すると、Phase 175以前の版では開けません。
        呼び出し側（`loadFromFile()`）が、変換したときだけ**1度きりの控え**を残します。

        変換したときにtrueを返します。
    */
    bool migrateTimesToBeats (juce::ValueTree projectState)
    {
        const int version = (int) projectState.getProperty (IDs::formatVersion, 1);

        if (version >= ProjectModel::currentFormatVersion)
            return false;

        const auto tempoMap = MusicalTime::buildTempoMapFrom (projectState);

        // **位置と長さを一度に直す小物**（8.139）。同じ式を4種類ぶん書き写さないため（1.27）。
        // 長さは**引き算**であることに注意（8.137）
        auto convertStart = [&tempoMap] (juce::ValueTree& node,
                                          const juce::Identifier& oldTime,
                                          const juce::Identifier& newBeats)
        {
            const double seconds = node.getProperty (oldTime, 0.0);

            node.setProperty (newBeats, tempoMap.getBeatAtTime (seconds), nullptr);
            node.removeProperty (oldTime, nullptr);

            return seconds;
        };

        auto convertLength = [&tempoMap] (juce::ValueTree& node, double startSeconds,
                                           const juce::Identifier& oldLength,
                                           const juce::Identifier& newLengthBeats)
        {
            const double length = juce::jmax (0.0, (double) node.getProperty (oldLength, 0.0));
            const double lengthBeats = tempoMap.getBeatAtTime (startSeconds + length)
                                        - tempoMap.getBeatAtTime (startSeconds);

            node.setProperty (newLengthBeats, juce::jmax (0.0, lengthBeats), nullptr);
            node.removeProperty (oldLength, nullptr);
        };

        if (version < 3)
        {
            // 8.139：ループ範囲（Phase 177）。**プロジェクト直下**にあるので、
            // トラックを回る前にここで
            if (projectState.hasProperty (IDs::loopStartTime)
                 || projectState.hasProperty (IDs::loopEndTime))
            {
                convertStart (projectState, IDs::loopStartTime, IDs::loopStartBeats);
                convertStart (projectState, IDs::loopEndTime, IDs::loopEndBeats);
            }

            for (auto markerState : projectState.getChildWithName (IDs::MARKERS))
                convertStart (markerState, IDs::markerTime, IDs::markerBeats);
        }

        for (auto trackState : projectState.getChildWithName (IDs::TRACKS))
        {
            if (version < 3)
            {
                for (auto regionState : trackState.getChildWithName (IDs::CHORDREGIONS))
                {
                    const double start = convertStart (regionState, IDs::chordRegionStartTime,
                                                        IDs::chordRegionStartBeats);

                    convertLength (regionState, start, IDs::chordRegionLength,
                                    IDs::chordRegionLengthBeats);
                }

                // オートメーションの点は`<AUTOMATION>`の下の`<LANE>`ごとに並んでいる
                for (auto laneState : trackState.getChildWithName (IDs::AUTOMATION))
                    for (auto pointState : laneState)
                        convertStart (pointState, IDs::pointTime, IDs::pointBeats);
            }

            if (version >= 2)
                continue;   // ノートとCCはPhase 176で済んでいる

            for (auto noteState : trackState.getChildWithName (IDs::NOTES))
            {
                const double start = noteState.getProperty (IDs::noteStartTime, 0.0);
                const double length = noteState.getProperty (IDs::noteLength, 0.0);

                const double startBeats = tempoMap.getBeatAtTime (start);

                // **長さは引き算**（8.137）。秒数をそのままテンポで割ると、
                // 途中に変化点をまたぐノートで合わなくなります
                const double lengthBeats = tempoMap.getBeatAtTime (start + juce::jmax (0.0, length))
                                             - startBeats;

                noteState.setProperty (IDs::noteStartBeats, startBeats, nullptr);
                noteState.setProperty (IDs::noteLengthBeats, juce::jmax (0.0, lengthBeats), nullptr);

                noteState.removeProperty (IDs::noteStartTime, nullptr);
                noteState.removeProperty (IDs::noteLength, nullptr);
            }

            for (auto eventState : trackState.getChildWithName (IDs::CCEVENTS))
            {
                const double time = eventState.getProperty (IDs::ccTime, 0.0);

                eventState.setProperty (IDs::ccTimeBeats, tempoMap.getBeatAtTime (time), nullptr);
                eventState.removeProperty (IDs::ccTime, nullptr);
            }
        }

        projectState.setProperty (IDs::formatVersion, ProjectModel::currentFormatVersion, nullptr);
        return true;
    }

    /** 8.139：変換前の版を控えるファイル名（Phase 177）。

        **置いていく版の番号を付けます**（v1のファイルなら`.v1-backup`）。
        版が上がっても、**古い控えが上書きされません**。 */
    juce::File getFormatBackupFile (const juce::File& file, int leavingVersion)
    {
        return file.getSiblingFile (file.getFileName()
                                     + ".v" + juce::String (leavingVersion) + "-backup");
    }
}

bool ProjectModel::loadFromFile (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto newState = juce::ValueTree::fromXml (*xml);

        if (newState.isValid() && newState.hasType (IDs::PROJECT))
        {
            migrateLegacyInstrumentToTrack (newState);
            migrateAutomationViewToLanes (newState);   // 8.56（Phase 94／D3）

            // **順番が意味を持ちます**（8.91）。先に伸ばしてから移すと、
            // 右へはみ出していたノートも拾えるので、いま鳴っている音は落ちません
            growMidiClipsToFitNotes (newState);
            migrateMidiClipsToTrackNotes (newState);   // 8.91（Phase 131）

            // 8.138：**一番最後に**（Phase 176／8.105の宿題3）。
            // クリップの中身が曲の時刻へ均されてからでないと、拍へ直せません
            const int loadedVersion = (int) newState.getProperty (IDs::formatVersion, 1);

            if (migrateTimesToBeats (newState))
            {
                // **一方通行なので、控えを1つだけ残します。**
                //
                // 保存のたびに作る`.bak`とは別ものです（あちらは毎回上書きされるので、
                // 2回保存したら古い形は消えます）。**既にあれば触りません**——
                // 開き直すたびに上書きしたら、控えとして意味がありません。
                //
                // 8.139：**置いていく版の番号を付けます**（Phase 177）。
                // v2をv3へ上げたときに、v1のときの控えを潰さないため
                auto backup = getFormatBackupFile (file, loadedVersion);

                if (! backup.exists())
                    file.copyFileTo (backup);   // 失敗しても読み込みは続ける
            }

            setState (newState);

            currentFile = file;
            undoManager.clearUndoHistory();
            markAsSaved();
            return true;
        }
    }

    return false;
}
