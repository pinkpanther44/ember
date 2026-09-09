#include "ClipAudioRenderer.h"

#include "AudioTransform.h"
#include <algorithm>
#include <cmath>
#include "Utf8.h"

ClipAudioRenderer::ClipAudioRenderer (ProjectModel& projectToUse)
    : juce::Thread ("ClipAudioRenderer"), project (projectToUse)
{
    // 8.148：**普通の優先度で回す**（Phase 186／本人の報告）。
    //
    // Phase 185では`low`にしていました。オーディオを邪魔しないつもりでしたが、
    // **オーディオはもっと高い優先度で回っている**ので、ここを下げても
    // 得られるものはありません。代わりに、他のアプリが動いているだけで
    // **同じ長さのファイルが何倍も遅くなり**、「速いときと遅いときがある」
    // という形になっていました。
    //
    // これは裏の雑用ではなく、**押した人が結果を待っている作業**です
    startThread (juce::Thread::Priority::normal);
}

ClipAudioRenderer::~ClipAudioRenderer()
{
    signalThreadShouldExit();
    wakeUp.signal();          // 寝ているところを起こさないと、待ち続けます
    stopThread (5000);
}

void ClipAudioRenderer::requestScan()
{
    // 8.148：**いま要るものを数え直して、積んであるものと入れ替える**（Phase 186）。
    //
    // 足していく形だと、+1を4回押したときに**もう誰も要らない途中の値**
    // （+1・+2・+3）を作り終えるまで本命が始まりません。
    // `collectJobs()`は「いま足りていないもの」を返すので、
    // **そのまま置き換えるのが正しい姿**です
    auto found = collectJobs();

    {
        const juce::ScopedLock lock (jobLock);
        jobs = std::move (found);
    }

    // **作りかけも捨てさせる**（`run()`が世代を見ています）。
    // 長いファイルは1本に何秒もかかるので、終わるのを待っていては同じことです
    ++scanGeneration;

    wakeUp.signal();
}

bool ClipAudioRenderer::hasPendingWork() const
{
    if (rendering.load())
        return true;

    const juce::ScopedLock lock (jobLock);
    return ! jobs.empty();
}

std::vector<ClipAudioRenderer::Job> ClipAudioRenderer::collectJobs() const
{
    std::vector<Job> found;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() != TrackType::Audio)
            continue;

        for (int c = 0; c < track.getNumClips(); ++c)
        {
            auto clip = track.getClip (c);
            const int semitones = clip.getTranspose();
            const auto map = clip.getRenderWarpMap();

            // 8.149／8.150：**音程・長さ・折れ線のどれかが動いていれば作る**（Phase 187・188）
            if (AudioTransform::isIdentity (semitones, map))
                continue;

            const juce::File source (clip.getSourceFilePath());

            if (! source.existsAsFile())
                continue;   // 行方不明のファイルは、ここでは何も言わない（8.30の担当）

            // **既に在るものは積まない。** Undoで前の値へ戻したときは、
            // キャッシュがそのまま使えます（作り直しになりません）
            if (AudioTransform::getRenderedFileFor (source, semitones, map).existsAsFile())
                continue;

            const Job job { source, semitones, map };

            if (std::find (found.begin(), found.end(), job) == found.end())
                found.push_back (job);
        }
    }

    return found;
}

void ClipAudioRenderer::run()
{
    while (! threadShouldExit())
    {
        std::vector<Job> current;
        int generation = 0;

        {
            const juce::ScopedLock lock (jobLock);
            current.swap (jobs);
            generation = scanGeneration.load();
        }

        if (current.empty())
        {
            wakeUp.wait (-1);   // 頼まれるまで寝る（`requestScan()`が起こす）
            continue;
        }

        rendering.store (true);

        int numRendered = 0;
        juce::StringArray errors;
        bool cancelled = false;

        // 8.148：**要求が変わったら、作りかけを捨てる**（Phase 186／本人の報告）。
        // `requestScan()`が世代を1つ進めるので、ここが食い違ったら要らなくなった証拠です
        const auto shouldAbort = [this, generation]
        {
            return threadShouldExit() || scanGeneration.load() != generation;
        };

        for (const auto& job : current)
        {
            if (shouldAbort())
            {
                cancelled = true;
                break;
            }

            auto error = AudioTransform::renderToFile (job.source, job.semitones, job.map, shouldAbort);

            // **止めた回は「出来た」と数えないこと。** `renderToFile()`は
            // 途中で止めても空（＝成功）を返します（作りかけは書かずに帰るだけ）ので、
            // ここで訊き直さないと**作っていないのに作ったと出ます**
            if (shouldAbort())
            {
                cancelled = true;
                break;
            }

            if (error.isNotEmpty())
                errors.addIfNotAlreadyThere (error);
            else
                ++numRendered;
        }

        rendering.store (false);

        if (threadShouldExit())
            return;   // 終了中は何も知らせない（画面がもう無い）

        // **捨てたときは何も言わないこと。** 次の要求がすぐ来ているので、
        // そちらが終わったときにまとめて知らせます
        if (cancelled)
            continue;

        // **知らせるのはメッセージスレッドで。** 画面もモデルもここからは触れません（1.15）
        juce::String message;

        if (numRendered > 0)
            message << utf8 ("音を作り直しました（") << numRendered << utf8 ("件）。");

        if (! errors.isEmpty())
            message << (message.isEmpty() ? "" : " ") << errors.joinIntoString (" / ");

        if (message.isNotEmpty())
        {
            {
                const juce::ScopedLock lock (jobLock);
                finishedMessage = message;
            }

            triggerAsyncUpdate();
        }
    }
}

void ClipAudioRenderer::handleAsyncUpdate()
{
    juce::String message;

    {
        const juce::ScopedLock lock (jobLock);
        message = finishedMessage;
        finishedMessage.clear();
    }

    if (message.isNotEmpty() && onFinished != nullptr)
        onFinished (message);
}
