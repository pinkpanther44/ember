#pragma once

#include "ProjectModel.h"

#include <vector>

/**
    8.362：**MIDI ファイルをアレンジ画面へ落とす**（Phase 351／本人の要望）。

    > 「ブラウザや外のフォルダなどから、オーディオデータやMIDIデータをArrange画面に挿入する際に、
    > 挿入後どの位置に挿入されるかわかるように、ドラッグ中に範囲を表示できないかな？」

    ブラウザには`.mid`が並んでいたが、**アレンジ画面へ落としても何も起きなかった**（音声ファイルだけを受けていた）。
    範囲を出すのに合わせて、MIDI ファイルも置けるようにした。

    - 時刻は**拍で読む**（ティック ÷ 四分音符あたりのティック）。落とした位置の拍から並べるので、
      曲のテンポが違っても小節に沿って入る。SMPTE 形式（まれ）は秒で読み、120BPM として拍に直す
    - ファイルの中のトラックとチャンネルは**全部まとめて1本へ**入れる（ドラム・ベースなどを分けたいときは、
      あとで分ける。複数トラックの分け入れは別の話）
    - 長さ（範囲の表示に使う）は、最後のノートの終わりと最後のイベントの遅いほう
*/
namespace MidiFileImport
{
    struct ImportedNote
    {
        int pitch = 60;
        int velocity = 100;
        double startBeats = 0.0;
        double lengthBeats = 0.25;
    };

    struct Contents
    {
        bool ok = false;
        std::vector<ImportedNote> notes;
        double lengthBeats = 0.0;
    };

    /** 拡張子で見る（中身は開かない。ドラッグ中に何度も呼ばれる）。 */
    inline bool isMidiFile (const juce::File& file)
    {
        return file.hasFileExtension ("mid;midi");
    }

    /** 読む。読めなければ`ok`が false。 */
    Contents read (const juce::File& file);

    /** トラックへ、`startBeats`から入れる。入れたノートの数を返す。 */
    int addToTrack (Track track, const Contents& contents, double startBeats, juce::UndoManager* undoManager);
}
