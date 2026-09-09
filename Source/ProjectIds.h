#pragma once

#include <juce_data_structures/juce_data_structures.h>

//==============================================================================
// ValueTreeで使うノード種別・プロパティ名。
// 設計書1.4「ValueTreeスキーマ」に対応する識別子群です。
//==============================================================================
namespace IDs
{
    // ノード種別
    extern const juce::Identifier PROJECT;
    extern const juce::Identifier TRACKS;
    extern const juce::Identifier TRACK;
    extern const juce::Identifier CLIPS;
    extern const juce::Identifier AUDIOCLIP;
    extern const juce::Identifier MIDICLIP;
    extern const juce::Identifier NOTES;
    extern const juce::Identifier NOTE;

    // プロジェクトのプロパティ
    extern const juce::Identifier projectName;

    /** 8.138：**保存形式の版**（Phase 176／8.105の宿題3）。

        入っていなければ**1**（＝時刻が秒で入っている、Phase 175までの形）。
        **2**から、ノートとCCの時刻は**拍**（`startBeats`・`lengthBeats`・`timeBeats`）です。

        **読み込み時に一方通行で変換します**（`migrateTimesToBeats()`）。
        変換したものを保存すると古い版では開けないので、**そのとき1度だけ
        `.v1-backup`を残します**。 */
    extern const juce::Identifier formatVersion;

    extern const juce::Identifier sampleRate;
    extern const juce::Identifier bitDepth;
    extern const juce::Identifier tempo;
    extern const juce::Identifier timeSignature;

    // 仕様書5.5・5.9：編集の刻み（スナップ、Phase 54）
    extern const juce::Identifier snapGrid;

    // 仕様書5.9：ループ再生（Phase 48）
    extern const juce::Identifier loopEnabled;

    // 8.139：Phase 176までの、秒で持っていた範囲（Phase 177／8.105の宿題3）。
    // **読み込み時の変換だけが読みます**
    extern const juce::Identifier loopStartTime;
    extern const juce::Identifier loopEndTime;

    /** 8.139：ループ範囲を**拍**で（Phase 177／`formatVersion` 3から）。
        テンポを変えても「2小節目から4小節目まで」のまま動きません。 */
    extern const juce::Identifier loopStartBeats;
    extern const juce::Identifier loopEndBeats;

    // 仕様書5.9：マーカー（Phase 49）
    extern const juce::Identifier MARKERS;
    extern const juce::Identifier MARKER;

    extern const juce::Identifier markerId;
    extern const juce::Identifier markerName;

    /** Phase 176までの、秒で持っていた位置。**読み込み時の変換だけが読みます**（8.139）。 */
    extern const juce::Identifier markerTime;

    /** 8.139：マーカーの位置を**拍**で（Phase 177／`formatVersion` 3から）。 */
    extern const juce::Identifier markerBeats;

    // 仕様書5.1・5.9：テンポと拍子の変化点（Phase 140／8.98の3）。
    // **`tempo`と`timeSignature`はプロジェクトの既定値**（0拍目／0小節目）として
    // そのまま残し、変化点だけをこのノードに足す。**古いファイルはこのノードが
    // 無いだけ**なので、読み替えは要らない
    extern const juce::Identifier TEMPOMAP;
    extern const juce::Identifier TEMPOCHANGE;
    extern const juce::Identifier TIMESIGCHANGE;

    extern const juce::Identifier tempoChangeBeat;   // テンポは**拍**で位置を持つ
    extern const juce::Identifier timeSigChangeBar;  // 拍子は**小節**でしか置けない

    // 仕様書5.11.1：キーの変化点（Phase 143／改善案㉔㉕）。
    // **`chordKeyRoot`／`chordKeyMinor`を使い回します**（コードトラックの既定値と同じ形）。
    // 曲の頭のキーはコードトラックが持ったままで、変化点だけがこのノードに載る
    extern const juce::Identifier KEYMAP;
    extern const juce::Identifier KEYCHANGE;
    extern const juce::Identifier keyChangeBar;      // キーも**小節**でしか置けない

    // トラックのプロパティ
    extern const juce::Identifier trackId;
    extern const juce::Identifier trackName;
    extern const juce::Identifier trackColor;

    /** 8.62：**トラックの行の高さ**（Phase 100）。ヘッダーの下端をドラッグして変える。

        入っていなければ種別ごとの既定（`TimelineComponent::getTrackAreaHeight()`）。
        **プロジェクトに保存します**：波形を大きく見たいトラックと、
        場所を取りたくないトラックの区別は曲ごとの話なので、開き直したときに残ってほしい。 */
    extern const juce::Identifier trackHeight;
    extern const juce::Identifier trackType;

    /** 8.142：**このトラックの時間の基準**（Phase 180／8.105の宿題3）。

        `"musical"`（拍で持つ＝テンポに追従）か`"linear"`（秒で持つ＝動かない）。
        **入っていなければ種別ごとの既定**（`Track::getDefaultTimeBase()`）で、
        オーディオだけが`linear`です。

        **効くのはオーディオクリップの開始位置だけ**です。ノート・CC・コード区間・
        マーカーは常に拍で、オーディオの**長さは常に秒**（伸び縮みできないため）。 */
    extern const juce::Identifier trackTimeBase;

    /** 8.143：**この行が音を受け取る音源トラックのID**（Phase 181／改善案⑮）。
        `TrackType::DrumOut`のときだけ意味を持ちます。空なら音は入ってきません。 */
    extern const juce::Identifier drumOutSourceTrackId;

    /** 8.143：受け取る出力バスの番号（Phase 181）。**1から**——
        **0番は音源トラック自身が受けます**（従来どおりの「メイン出力」）。 */
    extern const juce::Identifier drumOutSourceBus;
    extern const juce::Identifier mute;
    extern const juce::Identifier solo;
    extern const juce::Identifier armed;

    //=========================================================================
    // 8.84：MIDIトラックの入力設定（Phase 124/改善案⑯。仕様書5.4）

    /** 受け取るMIDI入力デバイスの**名前**。空なら「すべて」。

        **識別子ではなく名前で持ちます。** 識別子は機械ごとに違うので、
        別のPCでプロジェクトを開くと必ず外れます。名前なら
        「同じ鍵盤を挿してあれば効く」ようになります
        （同名のデバイスが2台あると区別できませんが、そちらのほうが稀です）。 */
    extern const juce::Identifier midiInputDevice;

    /** 受け取るMIDIチャンネル（1〜16）。0または未設定なら「すべて」。 */
    extern const juce::Identifier midiInputChannel;

    /** 音源へ送るときのMIDIチャンネル（1〜16）。0または未設定なら「そのまま」。 */
    extern const juce::Identifier midiOutputChannel;
    extern const juce::Identifier volume;
    extern const juce::Identifier pan;

    // VCAトラックのプロパティ（仕様書5.2.4・設計書1.4）
    extern const juce::Identifier vcaLinkedTrackIds;

    // オーディオクリップのプロパティ（設計書1.3）
    extern const juce::Identifier clipId;

    /** クリップの開始位置（秒）。**`linear`のトラックではこちらが本体**（8.142）。 */
    extern const juce::Identifier clipStartTime;

    /** 8.142：クリップの開始位置を**拍**で（Phase 180）。
        **`musical`のトラックではこちらが本体**で、`clipStartTime`は入っていません。

        **長さ（`clipLength`）は常に秒です。** オーディオは伸び縮みできないので、
        テンポを変えたときに動くのは**開始位置だけ**です（`trackTimeBase`）。 */
    extern const juce::Identifier clipStartBeats;

    extern const juce::Identifier clipLength;
    extern const juce::Identifier clipSourceFilePath;
    extern const juce::Identifier clipOffset;
    extern const juce::Identifier clipFadeIn;
    extern const juce::Identifier clipFadeOut;
    extern const juce::Identifier clipHitPoints;

    /** 8.78：**クリップのグループ**（Phase 118／改善案㊱。仕様書5.5）。

        同じ文字列を持つクリップは**まとめて選ばれます**（＝まとめて動く・消える）。
        空なら、どのグループにも入っていません。

        **オーディオとMIDIで同じプロパティ**です。どちらも`<CLIPS>`の子なので、
        **種別をまたいでまとめられます**（ドラムのループとそのMIDIを一組で動かす等）。 */
    extern const juce::Identifier clipGroupId;

    /** 仕様書5.5：**クリップ単位のゲイン**（Phase 80／8.40）。dBで持つ。
        入っていなければ0dB（Phase 79以前のプロジェクトは今までどおりの音量）。 */
    extern const juce::Identifier clipGainDb;

    /** 仕様書5.5：**逆再生**（Phase 86／8.46）。
        ファイルには手を加えず、鳴らすときに後ろから読む。 */
    extern const juce::Identifier clipReversed;

    /** 8.147：**オーディオのトランスポーズ**（Phase 185／改善案㉞。仕様書5.5）。
        半音の数（`-24`〜`+24`）。入っていなければ0＝素のファイルをそのまま鳴らす。

        **ファイルには手を加えません。** 音程を動かしたものは`AudioTransform`が
        キャッシュへ作り、再生側はそちらを開きます（`AudioTransform.h`）。 */
    extern const juce::Identifier clipTranspose;

    /** 8.149：**クリップの伸縮**（Phase 187／8.48。仕様書5.5.1）。

        **タイムラインの秒 ÷ ソースの秒**です。1.5なら「ソースの1秒がタイムラインの
        1.5秒になる」＝**遅く・長く**なります。入っていなければ1.0＝そのまま。

        **`clipLength`はタイムラインの長さのまま**です。使っているソースの長さは
        `clipLength ÷ clipStretch`で、`AudioClip::getSourceLength()`が答えます
        ——**素で割り算を書かないこと**（1.27）。

        **ファイルには手を加えません。** 伸ばしたものは`AudioTransform`が
        キャッシュへ作り、再生側はそちらを開きます。 */
    extern const juce::Identifier clipStretch;

    //=========================================================================
    // 8.150：**ワープマーカー**（Phase 188／8.48。仕様書5.5.1）

    /** クリップの子に付く、マーカーの入れ物。無ければ**ただの伸縮**（`clipStretch`）。 */
    extern const juce::Identifier WARP;
    extern const juce::Identifier WARPMARKER;

    /** そのマーカーが指している**ソースの中の時刻**（秒）。 */
    extern const juce::Identifier warpSourceTime;

    /** その音が**クリップの中のどこで鳴るか**（秒。クリップの頭から）。

        **タイムラインの時刻ではありません。** クリップを動かしても
        マーカーが付いてくるように、クリップの中での位置で持ちます。 */
    extern const juce::Identifier warpClipTime;

    // プラグイン（設計書1.3のPluginInstance、3.8の状態保存）
    // オートメーション（仕様書5.6・設計書1.3のAutomationLane）
    extern const juce::Identifier AUTOMATION;
    extern const juce::Identifier LANE;
    extern const juce::Identifier POINT;

    extern const juce::Identifier laneTargetId;

    /** 8.56：**このレーンをアレンジ画面に行として出すか**（Phase 94／D3）。

        Phase 93までは「トラックごとに1つだけ、`automationView`で選んだものを
        トラック行に重ねて描く」形だった。専用の行を持つようになったので、
        **見せるかどうかはレーン自身が持つ**（複数を同時に開ける）。

        入っていなければ非表示。古いプロジェクトは`automationView`から移し替える
        （`ProjectModel.cpp`の`migrateAutomationViewToLanes()`）。 */
    extern const juce::Identifier laneVisible;

    /** 8.56：レーンの行の高さ（Phase 94／D3）。入っていなければ既定値。
        **行ごとに持つ**ので、細かく見たいレーンだけ広げられる。 */
    extern const juce::Identifier laneHeight;

    /** 8.59：**このレーンを鳴らさない**（Phase 96）。入っていなければ有効。

        **点は消さずに、効かせるのをやめる**（インサートのバイパスと同じ考え方）。
        音の側（`AudioEngine`）とフェーダーの表示（8.54）の両方が見る。 */
    extern const juce::Identifier laneBypassed;

    /** 8.59：レーンの行の色（Phase 96）。ARGBのhex文字列。
        **入っていなければ親トラックの色**（`Track::getColourString()`）を使う。 */
    extern const juce::Identifier laneColour;

    extern const juce::Identifier automationMode;

    /** Phase 93まで使っていた「トラックごとに1つだけ開くレーン」の対象（Phase 26）。
        **Phase 94（D3）で`laneVisible`へ移した。** 読み込み時の移し替えだけが読む。 */
    extern const juce::Identifier automationView;
    /** Phase 176までの、秒で持っていた位置。**読み込み時の変換だけが読みます**（8.139）。 */
    extern const juce::Identifier pointTime;

    /** 8.139：オートメーションの点の位置を**拍**で（Phase 177／`formatVersion` 3から）。 */
    extern const juce::Identifier pointBeats;

    extern const juce::Identifier pointValue;
    extern const juce::Identifier pointCurve;

    /** 仕様書5.6：**曲がり具合**（Phase 77）。-1.0〜+1.0で、0が直線。
        `pointCurve`が`linear`のときだけ意味を持つ（S字とステップは形が決まっている）。 */
    extern const juce::Identifier pointCurveAmount;

    extern const juce::Identifier MASTERBUS;
    extern const juce::Identifier INSERTS;
    extern const juce::Identifier INSTRUMENT;
    extern const juce::Identifier SENDS;
    extern const juce::Identifier SEND;

    extern const juce::Identifier sendTargetTrackId;
    extern const juce::Identifier sendLevel;
    extern const juce::Identifier sendPrePost;
    extern const juce::Identifier PLUGINS;
    extern const juce::Identifier PLUGININSTANCE;
    extern const juce::Identifier PLUGINDESC;

    extern const juce::Identifier pluginRole;
    extern const juce::Identifier pluginFormat;
    extern const juce::Identifier pluginUid;
    extern const juce::Identifier pluginDisplayName;
    extern const juce::Identifier pluginState;

    // サイドチェイン（仕様書5.7.2・設計書1.3のPluginInstance）
    extern const juce::Identifier sidechainSourceTrackId;

    /** 8.63：**このインサートを通さない**（Phase 101／改善案㉘。仕様書5.7）。

        外すのではなく通さないだけなので、設定も内部状態も残る。
        音の側は`AudioEngine::rebuildTrackNodes()`が**配線から外す**ことで実現する。 */
    extern const juce::Identifier pluginBypassed;

    // ドラムエディター（仕様書5.3.2・設計書1.3のDrumMap）
    extern const juce::Identifier DRUMMAPS;
    extern const juce::Identifier DRUMMAP;
    extern const juce::Identifier DRUMENTRY;

    extern const juce::Identifier drumMapId;
    extern const juce::Identifier drumMapName;
    extern const juce::Identifier drumMidiNote;
    extern const juce::Identifier drumPartName;
    extern const juce::Identifier drumMuteGroup;
    extern const juce::Identifier drumRowMuted;
    extern const juce::Identifier trackDrumMapId;
    extern const juce::Identifier trackDrumEditor;

    /** 8.1のD5：ピアノロールで、このトラックのノートを透かしとして出すか（Phase 74）。 */
    extern const juce::Identifier trackWatermark;

    /** 8.44：**行を畳んでいるか**（Phase 84／C12）。
        画面の見え方なので、Undoの対象にしない（`trackWatermark`と同じ扱い）。 */
    extern const juce::Identifier trackCollapsed;

    /** 8.50：**このトラックが入っているフォルダのID**（Phase 89／D2。設計書1.3）。

        設計書はフォルダ側に`childTrackIds`を持たせる案でしたが、**子が親を指す形**に
        しました。トラックの増減・並べ替えのたびに一覧を直す必要がなく、
        **食い違いようがない**ためです（1.32と同じ考え方）。 */
    extern const juce::Identifier trackParentFolder;

    // グルーヴクオンタイズ（仕様書5.3.4・設計書1.3のGrooveTemplate）
    extern const juce::Identifier GROOVETEMPLATES;
    extern const juce::Identifier GROOVETEMPLATE;
    extern const juce::Identifier GROOVEPOINT;

    extern const juce::Identifier grooveId;
    extern const juce::Identifier grooveName;
    extern const juce::Identifier grooveGridDivision;
    extern const juce::Identifier grooveSourceTempo;
    extern const juce::Identifier groovePatternLength;
    extern const juce::Identifier groovePosition;
    extern const juce::Identifier grooveTimingOffsetMs;

    /** 8.135：ズレを「マス何個ぶん」で持つ（Phase 173/8.105の宿題2）。
        これが入っていなければ、`grooveTimingOffsetMs`から換算して読む。 */
    extern const juce::Identifier grooveTimingOffsetGrids;
    extern const juce::Identifier grooveVelocityScale;
    extern const juce::Identifier clipGrooveTemplateId;

    // CCレーン（仕様書5.3.3・設計書1.3のCCEvent）
    extern const juce::Identifier CCEVENTS;
    extern const juce::Identifier CC;
    extern const juce::Identifier CCLANES;
    extern const juce::Identifier CCLANE;

    extern const juce::Identifier ccController;
    extern const juce::Identifier ccValue;

    /** 仕様書5.3.3：この点から次の点までの繋ぎ方（Phase 76）。
        **オートメーションの`pointCurve`と同じ値**を入れる（読み替えずに済むように）。 */
    extern const juce::Identifier ccCurve;

    /** 曲がり具合（Phase 77）。**オートメーションの`pointCurveAmount`と同じ扱い**。 */
    extern const juce::Identifier ccCurveAmount;
    /** Phase 175までの、秒で持っていた時刻。**読み込み時の変換だけが読みます**（8.138）。 */
    extern const juce::Identifier ccTime;

    /** 8.138：CCの時刻を**拍**で（Phase 176／`formatVersion` 2から）。 */
    extern const juce::Identifier ccTimeBeats;

    extern const juce::Identifier ccLaneVisible;

    // コードトラック（仕様書5.2.3・5.11、設計書1.3のChordTrack／ChordRegion）
    extern const juce::Identifier CHORDREGIONS;
    extern const juce::Identifier CHORDREGION;

    extern const juce::Identifier chordKeyRoot;
    extern const juce::Identifier chordKeyMinor;

    extern const juce::Identifier chordRegionId;

    // Phase 176までの、秒で持っていた位置と長さ。
    // **読み込み時の変換だけが読みます**（8.139）
    extern const juce::Identifier chordRegionStartTime;
    extern const juce::Identifier chordRegionLength;

    /** 8.139：コード区間の位置を**拍**で（Phase 177／`formatVersion` 3から）。 */
    extern const juce::Identifier chordRegionStartBeats;

    /** 8.139：コード区間の長さを**拍数**で（Phase 177）。**位置ではなく長さ**なので、
        秒へ直すときは「終わりの拍 - 始まりの拍」で求めること。 */
    extern const juce::Identifier chordRegionLengthBeats;

    extern const juce::Identifier chordRoot;
    extern const juce::Identifier chordType;
    extern const juce::Identifier chordTensions;
    extern const juce::Identifier chordBass;

    // MIDIノートのプロパティ（設計書1.3のNote）
    extern const juce::Identifier notePitch;
    extern const juce::Identifier noteVelocity;

    // 8.138：Phase 175までの、秒で持っていた時刻（Phase 176／8.105の宿題3）。
    //
    // **読み込み時の変換と、MIDIクリップの後始末だけが読みます。**
    // 新しく書かれることはありません——`<MIDICLIP>`の中のノートは
    // 変換の途中でしか存在しないので、そこだけ秒のまま残ります
    extern const juce::Identifier noteStartTime;
    extern const juce::Identifier noteLength;

    /** 8.138：ノートの開始位置を**拍**で（Phase 176／`formatVersion` 2から）。

        曲の頭から数えた拍数（小数）。**テンポを変えると、音は一緒に動きます**——
        「3小節目の2拍目にある」という事実そのものを持つようになったためです。 */
    extern const juce::Identifier noteStartBeats;

    /** 8.138：ノートの長さを**拍数**で（Phase 176）。**位置ではなく長さ**なので、
        秒へ直すときは「終わりの拍 - 始まりの拍」で求めること。 */
    extern const juce::Identifier noteLengthBeats;
}

//==============================================================================
/** そのノードが「小節バーのレーン」に載るものか（テンポ・拍子・キーの変化点）。

    **画面はこれで購読の可否を判断します**（Phase 145）。

    ### なぜここに置いてあるか

    **同じ判定を3箇所が要るからです**——コードパッド（挿入位置の小節・拍と
    パッドの中身が変わる）、ピアノロール（ルーラーの目盛りとグリッドが変わる）、
    そしてこれから足す画面。**書き写すと、必ずどれかが古いまま残ります**（8.2）。

    ### プロパティだけ見ていると足りません

    変化点は「足す・消す」が主な操作なので、**子の出し入れでも呼ぶこと。**
    実際にPhase 142ではコードパッドが、Phase 144ではピアノロールが
    取りこぼしていました（8.106・8.109）。

    ### ノード種別まで見ること（1.42）

    `<TEMPOCHANGE>`の`tempo`はプロジェクトの`tempo`と**同じ識別子**なので、
    プロパティ名だけで判定すると偶然拾えたり拾えなかったりします。 */
inline bool isSignatureLaneNode (const juce::ValueTree& tree)
{
    return tree.hasType (IDs::TEMPOMAP)  || tree.hasType (IDs::TEMPOCHANGE)
        || tree.hasType (IDs::TIMESIGCHANGE)
        || tree.hasType (IDs::KEYMAP)    || tree.hasType (IDs::KEYCHANGE);
}

/** テンポ・拍子の表に効く「値の変更」かどうか（Phase 141／8.103）。

    **子の出し入れは`isSignatureLaneNode()`のほうで見ること。**
    こちらは`valueTreePropertyChanged`用です。

    ### なぜここへ上がってきたか

    Phase 199まで`MainComponent.cpp`の中だけにありました（メトロノーム用）。
    Phase 200で**オーディオエンジンも同じ判定を要る**ようになった
    （再生中のテンポ変更に音のほうも追いつかせるため。8.162）ので、
    **書き写さずにここへ移してあります**（8.2）。

    ### ノード種別まで見ること（1.42）

    `<TEMPOCHANGE>`の`tempo`はプロジェクトの`tempo`と**同じ識別子**なので、
    プロパティ名だけで判定すると偶然拾えたり拾えなかったりします。 */
inline bool isTempoMapChange (const juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType (IDs::PROJECT))
        return property == IDs::tempo || property == IDs::timeSignature;

    return tree.hasType (IDs::TEMPOCHANGE) || tree.hasType (IDs::TIMESIGCHANGE);
}
