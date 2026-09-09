#include "ProjectIds.h"

//==============================================================================
// 設計書1.4「ValueTreeスキーマ」の識別子の実体。
//
// Phase 26でProjectModel.cppから分けた。識別子はデータモデルのどの層からも参照され、
// 一覧として眺めたい場面が多いのに、2,500行のファイルの先頭に埋もれていたため。
//==============================================================================
namespace IDs
{
    const juce::Identifier PROJECT   { "PROJECT" };
    const juce::Identifier TRACKS    { "TRACKS" };
    const juce::Identifier TRACK     { "TRACK" };
    const juce::Identifier CLIPS     { "CLIPS" };
    const juce::Identifier AUDIOCLIP { "AUDIOCLIP" };
    const juce::Identifier MIDICLIP  { "MIDICLIP" };
    const juce::Identifier NOTES     { "NOTES" };
    const juce::Identifier NOTE      { "NOTE" };

    const juce::Identifier projectName  { "name" };
    const juce::Identifier formatVersion{ "formatVersion" };
    const juce::Identifier sampleRate   { "sampleRate" };
    const juce::Identifier bitDepth     { "bitDepth" };
    const juce::Identifier tempo        { "tempo" };
    const juce::Identifier timeSignature{ "timeSignature" };

    const juce::Identifier snapGrid     { "snapGrid" };

    const juce::Identifier loopEnabled   { "loopEnabled" };
    const juce::Identifier loopStartTime { "loopStart" };
    const juce::Identifier loopEndTime   { "loopEnd" };
    const juce::Identifier loopStartBeats { "loopStartBeats" };
    const juce::Identifier loopEndBeats   { "loopEndBeats" };

    const juce::Identifier MARKERS { "MARKERS" };
    const juce::Identifier MARKER  { "MARKER" };

    const juce::Identifier markerId   { "id" };
    const juce::Identifier markerName { "name" };
    const juce::Identifier markerTime  { "time" };
    const juce::Identifier markerBeats { "beats" };

    const juce::Identifier TEMPOMAP      { "TEMPOMAP" };
    const juce::Identifier TEMPOCHANGE   { "TEMPOCHANGE" };
    const juce::Identifier TIMESIGCHANGE { "TIMESIGCHANGE" };

    const juce::Identifier tempoChangeBeat  { "beat" };
    const juce::Identifier timeSigChangeBar { "bar" };

    const juce::Identifier KEYMAP    { "KEYMAP" };
    const juce::Identifier KEYCHANGE { "KEYCHANGE" };

    const juce::Identifier keyChangeBar { "bar" };

    const juce::Identifier trackId    { "id" };
    const juce::Identifier trackName  { "name" };
    const juce::Identifier trackColor { "color" };
    const juce::Identifier trackHeight { "trackHeight" };
    const juce::Identifier trackType  { "type" };
    const juce::Identifier trackTimeBase { "timeBase" };
    const juce::Identifier drumOutSourceTrackId { "drumOutSource" };
    const juce::Identifier drumOutSourceBus     { "drumOutBus" };
    const juce::Identifier mute       { "mute" };
    const juce::Identifier solo       { "solo" };
    const juce::Identifier armed      { "armed" };

    // 8.84：MIDIトラックの入力設定（Phase 124/改善案⑯）
    const juce::Identifier midiInputDevice   { "midiInputDevice" };
    const juce::Identifier midiInputChannel  { "midiInputChannel" };
    const juce::Identifier midiOutputChannel { "midiOutputChannel" };
    const juce::Identifier volume     { "volume" };
    const juce::Identifier pan        { "pan" };

    const juce::Identifier vcaLinkedTrackIds { "linkedTrackIds" };

    const juce::Identifier clipId             { "id" };
    const juce::Identifier clipStartTime      { "startTime" };
    const juce::Identifier clipStartBeats     { "startBeats" };
    const juce::Identifier clipLength         { "length" };
    const juce::Identifier clipSourceFilePath { "sourceFilePath" };
    const juce::Identifier clipOffset         { "offset" };
    const juce::Identifier clipFadeIn         { "fadeIn" };
    const juce::Identifier clipFadeOut        { "fadeOut" };
    const juce::Identifier clipHitPoints      { "hitPoints" };
    const juce::Identifier clipGainDb         { "gainDb" };
    const juce::Identifier clipReversed       { "reversed" };
    const juce::Identifier clipTranspose      { "transpose" };   // 8.147（Phase 185／改善案㉞）
    const juce::Identifier clipStretch        { "stretch" };     // 8.149（Phase 187／8.48）

    // 8.150：ワープマーカー（Phase 188／8.48）
    const juce::Identifier WARP               { "WARP" };
    const juce::Identifier WARPMARKER         { "WARPMARKER" };
    const juce::Identifier warpSourceTime     { "sourceTime" };
    const juce::Identifier warpClipTime       { "clipTime" };
    const juce::Identifier clipGroupId        { "groupId" };   // 8.78（Phase 118）

    // 仕様書5.6・設計書1.4：オートメーション
    const juce::Identifier AUTOMATION      { "AUTOMATION" };
    const juce::Identifier LANE            { "LANE" };
    const juce::Identifier POINT           { "POINT" };

    const juce::Identifier laneTargetId    { "targetId" };
    const juce::Identifier laneVisible     { "laneVisible" };
    const juce::Identifier laneHeight      { "laneHeight" };
    const juce::Identifier laneBypassed    { "laneBypassed" };
    const juce::Identifier laneColour      { "laneColour" };
    const juce::Identifier automationMode  { "automationMode" };
    const juce::Identifier automationView  { "automationViewTarget" };
    const juce::Identifier pointTime       { "time" };
    const juce::Identifier pointBeats      { "beats" };
    const juce::Identifier pointValue      { "value" };
    const juce::Identifier pointCurve      { "curve" };
    const juce::Identifier pointCurveAmount { "curveAmount" };

    const juce::Identifier MASTERBUS       { "MASTERBUS" };
    const juce::Identifier INSERTS         { "INSERTS" };
    const juce::Identifier INSTRUMENT      { "INSTRUMENT" };
    const juce::Identifier SENDS           { "SENDS" };
    const juce::Identifier SEND            { "SEND" };

    const juce::Identifier sendTargetTrackId { "targetTrackId" };
    const juce::Identifier sendLevel         { "level" };
    const juce::Identifier sendPrePost       { "prePost" };
    const juce::Identifier PLUGINS         { "PLUGINS" };
    const juce::Identifier PLUGININSTANCE  { "PLUGININSTANCE" };
    const juce::Identifier PLUGINDESC      { "PLUGIN" }; // PluginDescription::createXml()のタグ名

    const juce::Identifier pluginRole        { "role" };
    const juce::Identifier pluginFormat      { "format" };
    const juce::Identifier pluginUid         { "pluginUid" };
    const juce::Identifier pluginDisplayName { "name" };
    const juce::Identifier pluginState       { "state" };

    const juce::Identifier sidechainSourceTrackId { "sidechainSourceTrackId" };
    const juce::Identifier pluginBypassed { "bypassed" };

    const juce::Identifier DRUMMAPS  { "DRUMMAPS" };
    const juce::Identifier DRUMMAP   { "DRUMMAP" };
    const juce::Identifier DRUMENTRY { "DRUMENTRY" };

    const juce::Identifier drumMapId      { "id" };
    const juce::Identifier drumMapName    { "name" };
    const juce::Identifier drumMidiNote   { "midiNote" };
    const juce::Identifier drumPartName   { "partName" };
    const juce::Identifier drumMuteGroup  { "muteGroup" };
    const juce::Identifier drumRowMuted   { "muted" };
    const juce::Identifier trackDrumMapId { "drumMapId" };
    const juce::Identifier trackDrumEditor { "drumEditor" };
    const juce::Identifier trackWatermark  { "pianoRollWatermark" };
    const juce::Identifier trackCollapsed  { "collapsed" };
    const juce::Identifier trackParentFolder { "parentFolderId" };

    const juce::Identifier GROOVETEMPLATES { "GROOVETEMPLATES" };
    const juce::Identifier GROOVETEMPLATE  { "GROOVETEMPLATE" };
    const juce::Identifier GROOVEPOINT     { "GROOVEPOINT" };

    const juce::Identifier grooveId             { "id" };
    const juce::Identifier grooveName           { "name" };
    const juce::Identifier grooveGridDivision   { "gridDivision" };
    const juce::Identifier grooveSourceTempo    { "sourceTempo" };
    const juce::Identifier groovePatternLength  { "patternLength" };
    const juce::Identifier groovePosition       { "gridPosition" };
    const juce::Identifier grooveTimingOffsetGrids { "timingOffsetGrids" };   // 8.135（Phase 173）
    const juce::Identifier grooveTimingOffsetMs { "timingOffsetMs" };
    const juce::Identifier grooveVelocityScale  { "velocityScale" };
    const juce::Identifier clipGrooveTemplateId { "grooveTemplateId" };

    const juce::Identifier CCEVENTS { "CCEVENTS" };
    const juce::Identifier CC       { "CC" };
    const juce::Identifier CCLANES  { "CCLANES" };
    const juce::Identifier CCLANE   { "CCLANE" };

    const juce::Identifier ccController  { "controllerNumber" };
    const juce::Identifier ccValue       { "value" };
    const juce::Identifier ccCurve       { "curveType" };
    const juce::Identifier ccCurveAmount { "curveAmount" };
    const juce::Identifier ccTime        { "time" };
    const juce::Identifier ccTimeBeats   { "timeBeats" };
    const juce::Identifier ccLaneVisible { "visible" };

    const juce::Identifier CHORDREGIONS { "CHORDREGIONS" };
    const juce::Identifier CHORDREGION  { "CHORDREGION" };

    const juce::Identifier chordKeyRoot  { "keyRoot" };
    const juce::Identifier chordKeyMinor { "keyMinor" };

    const juce::Identifier chordRegionId        { "id" };
    const juce::Identifier chordRegionStartTime { "startTime" };
    const juce::Identifier chordRegionLength    { "length" };

    const juce::Identifier chordRegionStartBeats  { "startBeats" };
    const juce::Identifier chordRegionLengthBeats { "lengthBeats" };

    const juce::Identifier chordRoot     { "chordRoot" };
    const juce::Identifier chordType     { "chordType" };
    const juce::Identifier chordTensions { "tensions" };
    const juce::Identifier chordBass     { "bass" };

    const juce::Identifier notePitch     { "pitch" };
    const juce::Identifier noteVelocity  { "velocity" };
    const juce::Identifier noteStartTime { "startTime" };
    const juce::Identifier noteLength    { "length" };

    const juce::Identifier noteStartBeats  { "startBeats" };
    const juce::Identifier noteLengthBeats { "lengthBeats" };
}
