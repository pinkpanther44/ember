#include "MetronomeProcessor.h"

MetronomeProcessor::MetronomeProcessor (Transport& transportToUse)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      transport (transportToUse)
{
}

void MetronomeProcessor::prepareToPlay (double sampleRate, int)
{
    if (sampleRate > 0.0)
        currentSampleRate = sampleRate;

    clickSamplesRemaining = 0;
}

void MetronomeProcessor::releaseResources()
{
    clickSamplesRemaining = 0;
}

void MetronomeProcessor::startClick (bool isAccent)
{
    clickSamplesRemaining = juce::jmax (1, (int) (clickLengthSeconds * currentSampleRate));
    clickPhase = 0.0;
    clickPhaseIncrement = juce::MathConstants<double>::twoPi
                            * (isAccent ? accentFrequency : beatFrequency) / currentSampleRate;

    // 小節の頭は少し大きくして、拍子の頭が分かるようにする
    clickLevel = gain.load() * (isAccent ? 1.0f : 0.7f);
}

void MetronomeProcessor::setTempoMap (const TempoMap& newMap)
{
    // **写しはロックの外で作り、入れ替えだけをロックの中で行う**
    // （MidiPlayerProcessor::rebuildNoteList()と同じ形）。
    // ロックの中でvectorを組み立てると、その間オーディオスレッドが待たされます
    TempoMap copy = newMap;

    const juce::SpinLock::ScopedLockType lock (tempoMapLock);
    tempoMap = std::move (copy);
}

void MetronomeProcessor::startCountIn (int bars, juce::int64 transportStartPosition,
                                        juce::int64 transportEndPosition)
{
    // Phase 141：**数える間隔は「演奏を始める位置」の値で決める。**
    // カウントインは「これから始まる場所の拍」を数えるものなので、
    // 曲の頭のテンポではなく、始める場所のテンポで数えないと噛み合いません
    {
        const juce::SpinLock::ScopedLockType lock (tempoMapLock);

        const double startSeconds = (currentSampleRate > 0.0)
                                      ? (double) transportStartPosition / currentSampleRate
                                      : 0.0;
        const double startBeat = tempoMap.getBeatAtTime (startSeconds);

        countInSamplesPerBeat = currentSampleRate * 60.0
                                  / juce::jmax (1.0, tempoMap.getTempoAtBeat (startBeat));
        countInBeatsPerBar = juce::jmax (1, tempoMap.getBeatsPerBarAtBar (
                                              tempoMap.getBarPositionAtBeat (startBeat).bar));
    }

    const int clicks = juce::jmax (1, bars) * countInBeatsPerBar;

    countInTransportStart.store (transportStartPosition);
    countInTransportEnd.store (transportEndPosition);

    // カウンタはオーディオスレッドが触るものだが、まだカウントインが始まっていない
    // （countInBeatsRemainingが0）ので、ここで初期化しても競合しない。
    // **残り拍数は最後に入れること**：これがカウントインの開始スイッチになる。
    countInSamplesToNextBeat = 0.0; // 最初の1拍はすぐ鳴らす
    countInBeatIndex = 0;

    // **クリックの数より1つ多く数える。** 最後の1つはクリックを鳴らさず、
    // そこが「演奏の頭」＝トランスポートを開始する位置になる。
    // 1小節4拍なら「1・2・3・4」と鳴らして、次の1拍目から演奏が始まる。
    countInBeatsRemaining.store (clicks + 1);
}

void MetronomeProcessor::processCountIn (int numSamples, int& clickOffsetOut, bool& isAccentOut)
{
    const int remaining = countInBeatsRemaining.load();

    if (remaining <= 0)
        return;

    // Phase 141：**始めたときに写し取った間隔を使う**（ヘッダの説明を参照）
    const double samplesPerBeat = countInSamplesPerBeat;

    if (samplesPerBeat < 1.0)
        return;

    if (countInSamplesToNextBeat < (double) numSamples)
    {
        if (remaining > 1)
        {
            // まだカウントの途中。クリックを鳴らして次の拍へ
            clickOffsetOut = juce::jmax (0, (int) countInSamplesToNextBeat);

            const int beatsInBar = juce::jmax (1, countInBeatsPerBar);
            isAccentOut = (countInBeatIndex % beatsInBar) == 0;

            ++countInBeatIndex;
            countInSamplesToNextBeat += samplesPerBeat;
            countInBeatsRemaining.store (remaining - 1);
        }
        else
        {
            // 数え終わり＝演奏の頭。ここではクリックを鳴らさず、トランスポートを始める。
            // **オーディオスレッドから開始してよい**：Transportはatomicだけで出来ている。
            // メッセージスレッドのタイマーで判断すると、ブロック数個ぶん遅れて
            // 演奏の頭がその量だけ後ろにずれて録れることになる。
            transport.start (countInTransportEnd.load(), countInTransportStart.load());
            countInBeatsRemaining.store (0);
            return;
        }
    }

    countInSamplesToNextBeat -= (double) numSamples;
}

void MetronomeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // このノードは音を作るだけで、入力は受け取らない。
    // **必ず先に消すこと**：グラフのバッファは使い回されるので、
    // 消さないと他のノードの音がそのまま出てしまう。
    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    int clickStartOffset = -1;
    bool clickIsAccent = false;

    //==========================================================================
    // 仕様書5.4：録音のカウントイン（Phase 39）。
    // **メトロノームが切ってあっても鳴らす**（鳴らないとカウントインの意味が無い）ので、
    // enabledの判定より先に処理する。
    const bool countingIn = isCountingIn();

    if (countingIn)
    {
        processCountIn (numSamples, clickStartOffset, clickIsAccent);
    }
    else if (! enabled.load())
    {
        clickSamplesRemaining = 0; // 切った瞬間に鳴り止ませる
        return;
    }

    //==========================================================================
    // 拍の境目がこのブロックに入っているかを調べる。
    // 位置はTransportが持っている（設計書1.5）。停止中は進まないので鳴らない。
    //
    // **ブロックの先頭で鳴らしてしまわないこと。** 48kHz・512サンプルなら
    // 1ブロック＝約10.7msあり、そこへ丸めるとクリックが最大10msずれる。
    // 拍がブロックのどこに来るかを覚えておいて、その位置から鳴らし始める。
    //
    // Phase 141：**位置はTempoMapに訊く**（掛け算1つでは、途中でテンポが
    // 変わった先が合わない）。ルーラーの小節線と同じ計算から出るので、
    // 「線とクリックがずれる」ということが起こりません。
    //
    // カウントイン中はそちらが位置を決めているので、ここは通さない。
    if (! countingIn && enabled.load() && transport.isPlaying() && currentSampleRate > 0.0)
    {
        // **取れなければ、その回は諦める**（オーディオスレッドを待たせないこと）。
        // 落とすのはクリック1回ぶんで、次のブロックで拾い直されます
        const juce::SpinLock::ScopedTryLockType tryLock (tempoMapLock);

        if (tryLock.isLocked())
        {
            const auto blockStart = transport.getPositionSamples();
            const auto blockEnd = blockStart + (juce::int64) numSamples;

            const double blockStartSeconds = (double) blockStart / currentSampleRate;

            // このブロック内で最初に来る拍の番号。
            // ぴったり境目から始まる場合も鳴らしたいので、切り上げは「以上」で取る
            // （丸め誤差でちょうどの拍を1つ飛ばさないよう、わずかに甘く見る）。
            const double beatAtBlockStart = tempoMap.getBeatAtTime (blockStartSeconds);
            const auto beatIndex = (juce::int64) std::ceil (beatAtBlockStart - 1.0e-9);

            const double beatSeconds = tempoMap.getTimeForBeat ((double) beatIndex);
            const auto beatPosition = (juce::int64) (beatSeconds * currentSampleRate);

            // 1ブロックに2拍入るのは、テンポの上限（300BPM）ではあり得ないので
            // 最初の1つだけ見ればよい（512サンプルに2拍＝5600BPM相当）。
            if (beatPosition >= blockStart && beatPosition < blockEnd)
            {
                clickStartOffset = (int) (beatPosition - blockStart);

                // 小節の頭かどうか。**わずかに先へずらしてから訊く**——
                // 3.9999…拍が「前の小節の4拍目」に見えると、アクセントが落ちます
                const auto barPosition = tempoMap.getBarPositionAtBeat ((double) beatIndex + 1.0e-6);
                clickIsAccent = (barPosition.beatsIntoBar < 0.5);
            }
        }
    }

    //==========================================================================
    // クリックを書き込む。前のブロックから続いている音もここで鳴らし切る。
    if (clickStartOffset < 0 && clickSamplesRemaining <= 0)
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    const double totalSamples = juce::jmax (1.0, clickLengthSeconds * currentSampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        if (i == clickStartOffset)
            startClick (clickIsAccent);

        if (clickSamplesRemaining <= 0)
            continue;

        // 直線的に減衰させる。短い音なので、これで十分「コッ」と聞こえる
        const float envelope = (float) (clickSamplesRemaining / totalSamples);
        const float sample = clickLevel * envelope * (float) std::sin (clickPhase);

        left[i] += sample;

        if (right != nullptr)
            right[i] += sample;

        clickPhase += clickPhaseIncrement;
        --clickSamplesRemaining;
    }
}
