#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "ProjectModel.h"

#include <atomic>
#include <cmath>
#include <functional>
#include <vector>

//==============================================================================
/**
    8.147：**トランスポーズ済みのファイルを、裏で作る**（Phase 185／改善案㉞。仕様書5.5）。

    ### なぜ「押されたとき」ではなく「見て回る」のか

    半音の値が入る道はいくつもあります——クリップのメニュー、インスペクタ、
    **Undo／Redo**、コピー＆ペースト、クリップの複製、**プロジェクトを開いたとき**。

    **入口ごとに「作れ」と言う形にすると、必ずどれかを忘れます**（1.15）。
    ここは代わりに**プロジェクトを見て回って、足りないものを作ります**。
    誰がどう値を入れても勝手に効きます。

    `requestScan()`はモデルを読むので**メッセージスレッドから**呼びます。
    実際に作るのは、ずっと寝ている低優先度のスレッドが1本です。

    ### 出来るまでは素の音が鳴ります

    `ClipPlayerProcessor`は**キャッシュが在ればそれを開き、無ければ素のファイル**を
    開きます。だから作っている間も音は途切れません（音程がまだ動いていないだけ）。
    出来上がったら`onFinished`が呼ばれるので、そこで読み直させます。

    ### スレッドを作り直さない

    `startThread()`を呼び直す形にすると、**前の回が終わりかけている瞬間に
    次を頼まれたとき**の始末が面倒です（開始が黙って失敗する）。
    **1本を寝かせておいて叩き起こす**形にしてあります。
*/
class ClipAudioRenderer : private juce::Thread,
                           private juce::AsyncUpdater
{
public:
    explicit ClipAudioRenderer (ProjectModel& projectToUse);
    ~ClipAudioRenderer() override;

    /** プロジェクトを見て回って、足りないものがあれば裏で作り始める。

        **メッセージスレッドから呼ぶこと**（モデルを読みます）。
        何度呼んでも構いません——**同じ組み合わせは1回しか作りません**。

        ### 呼ぶたびに、要るものだけへ置き換わります

        8.148：**溜めずに入れ替えます**（Phase 186／本人の報告）。

        +1を4回押すと、途中の値（+1・+2・+3）も1回ずつ要求されます。
        積んでいくと**もう誰も要らない3つを作り終えるまで、
        本命の+4が始まりません**——「反映に時間がかかる」の正体でした。

        **作りかけも捨てます。** 長いファイルだと1本に何秒もかかるので、
        終わるのを待っていては同じことになります。 */
    void requestScan();

    /** これから作るものがあるか（画面に「作っています」と出すのに使う）。

        `requestScan()`を呼んだ**直後に**訊けます（作り始めるのを待ちません）。 */
    bool hasPendingWork() const;

    /** いま作っている最中か（画面に「作っています」と出すのに使う）。 */
    bool isRendering() const { return rendering.load(); }

    /** 1回ぶんの作業が終わったときに、**メッセージスレッドで**呼ばれる。

        `message`は画面に出す文面（何も作らなかったときは呼ばれません）。
        **ここで再生側に読み直させること**——出来たファイルは、
        読み直すまで使われません。 */
    std::function<void (const juce::String& message)> onFinished;

private:
    void run() override;

    /** 作り終わったことをメッセージスレッドへ知らせる口。

        **`juce::MessageManager::callAsync()`ではなく`AsyncUpdater`**を使うのは、
        あちらは**このオブジェクトが消えた後でも飛んでくる**ためです
        （`onFinished`が捕まえている画面はもう無い）。
        `AsyncUpdater`は**破棄のときに待っている呼び出しを取り消します**。 */
    void handleAsyncUpdate() override;

    struct Job
    {
        juce::File source;
        int semitones = 0;
        /** 8.150：**折れ線ごと持ちます**（Phase 188／8.48）。
            倍率1つでは、マーカーを動かしたことが伝わりません。 */
        WarpMap map;

        bool operator== (const Job& other) const
        {
            if (semitones != other.semitones || source != other.source)
                return false;

            if (map.points.size() != other.map.points.size())
                return false;

            for (size_t i = 0; i < map.points.size(); ++i)
                if (std::abs (map.points[i].sourceSeconds - other.map.points[i].sourceSeconds) > 1.0e-9
                     || std::abs (map.points[i].warpedSeconds - other.map.points[i].warpedSeconds) > 1.0e-9)
                    return false;

            return true;
        }
    };

    /** いま足りていない組み合わせ。**メッセージスレッドから**呼ぶこと。 */
    std::vector<Job> collectJobs() const;

    ProjectModel& project;

    juce::CriticalSection jobLock;
    std::vector<Job> jobs;           // jobLockで守る
    juce::String finishedMessage;    // jobLockで守る（作業スレッドが書き、メッセージスレッドが読む）
    juce::WaitableEvent wakeUp;

    std::atomic<bool> rendering { false };

    /** 8.148：**見て回った回数**（Phase 186／本人の報告）。

        `requestScan()`のたびに1つ増えます。作業スレッドは取り出したときの値を覚えておき、
        **違う値になっていたら作りかけを捨てます**——その時点で、
        いま作っているものはもう要求と食い違っているからです。 */
    std::atomic<int> scanGeneration { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipAudioRenderer)
};
