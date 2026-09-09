#include "AudioEditorView.h"

#include "AppColours.h"
#include "HitPointDetector.h"   // 仕様書5.5.1：ヒットポイント検出（Phase 85）
#include "NameEntry.h"   // 8.40：ゲインの数値入力（Phase 80）
#include "Utf8.h"

//==============================================================================
AudioEditorView::AudioEditorView (ProjectModel& projectToUse, WaveformCache& waveformCacheToUse)
    : project (projectToUse), waveformCache (waveformCacheToUse)
{
    clipNameLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
    clipNameLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (clipNameLabel);

    auto setUpButton = [this] (juce::TextButton& button, const juce::String& text,
                                std::function<void()> action)
    {
        button.setButtonText (text);
        button.onClick = std::move (action);
        addAndMakeVisible (button);
    };

    setUpButton (zoomOutButton, "-",   [this] { zoomOut(); });
    setUpButton (zoomInButton,  "+",   [this] { zoomIn(); });
    setUpButton (zoomFitButton, "Fit", [this] { zoomToFit(); });

    // D1のメモ：オーディオデータが無いときに出す2つ。
    // **押した先はここでは決めません**（取り込み先のトラックを知っているのは
    // 呼んだ側なので、要求だけ返す。8.11と同じ形）
    setUpButton (importButton, utf8 ("オーディオファイルを取り込む..."),
                  [this] { if (onImportRequested != nullptr) onImportRequested(); });

    setUpButton (recordButton, utf8 ("録音する"),
                  [this] { if (onRecordRequested != nullptr) onRecordRequested(); });

    // 仕様書5.5.1：ヒットポイントの自動検出（Phase 85）
    setUpButton (detectButton, utf8 ("ヒットポイント検出"), [this] { detectHitPoints(); });

    horizontalScrollBar.setAutoHide (false);
    horizontalScrollBar.addListener (this);
    addAndMakeVisible (horizontalScrollBar);

    updateButtonVisibility();
}

AudioEditorView::~AudioEditorView()
{
    horizontalScrollBar.removeListener (this);

    // **見ているサムネイルからは必ず外れること。** 付けっぱなしで死ぬと、
    // 波形が届いたときに居ないものへ通知が飛ぶ
    if (thumbnailFilePath.isNotEmpty())
        waveformCache.getThumbnail (thumbnailFilePath).removeChangeListener (this);
}

//==============================================================================
void AudioEditorView::setClip (AudioClip clipToEdit)
{
    const bool clipChanged = (clipToEdit.state != clip.state);

    clip = clipToEdit;
    clipIsSet = clip.state.isValid() && clip.state.getParent().isValid();

    // 波形の届き先を付け替える。**同じファイルなら何もしない**
    const juce::String newPath = clipIsSet ? clip.getSourceFilePath() : juce::String();

    if (newPath != thumbnailFilePath)
    {
        if (thumbnailFilePath.isNotEmpty())
            waveformCache.getThumbnail (thumbnailFilePath).removeChangeListener (this);

        thumbnailFilePath = newPath;

        if (thumbnailFilePath.isNotEmpty())
            waveformCache.getThumbnail (thumbnailFilePath, this);
    }

    // 8.46：**逆再生中はそれと分かるように**（Phase 86）。
    // 波形はソースのままなので（この画面はソースの時間で並んでいる）、
    // 見ただけでは向きが分からない。名前の後ろに印を足しておく
    juce::String title = clipIsSet ? juce::File (clip.getSourceFilePath()).getFileName()
                                    : juce::String();

    if (clipIsSet && clip.isReversed())
        title += utf8 ("   （逆再生）");   // **記号は使わない**（フォントに無いことがある。1.30）

    clipNameLabel.setText (title, juce::dontSendNotification);

    // **別のクリップへ移ったときだけ見え方を作り直す**（ピアノロールと同じ条件。8.29）。
    // 同じクリップで呼び直されただけでスクロールが戻ると、
    // トリムを直すたびに見ていた場所を見失う
    if (clipChanged)
    {
        scrollStartSeconds = 0.0;
        zoomToFitPending = true;
        zoomToFit();   // 幅と長さが分かっていれば、ここで済む
    }

    updateButtonVisibility();
    updateScrollBar();
    repaint();
}

void AudioEditorView::refreshFromModel()
{
    // 掴んでいたクリップが消えている（Undo・削除）ことがある
    if (clipIsSet && ! clip.state.getParent().isValid())
        setClip (AudioClip (juce::ValueTree()));

    updateButtonVisibility();
    updateScrollBar();
    repaint();
}

void AudioEditorView::refreshAfterProjectChanged (bool keepSelection)
{
    // 8.41：**Undo/Redoでは捨てない**（1.33）。消えた（Undoで無くなった）ときは
    // `refreshFromModel()`が気づいて外すので、ここで先回りしなくてよい
    if (! keepSelection)
        setClip (AudioClip (juce::ValueTree()));

    refreshFromModel();
}

void AudioEditorView::setPlayheadSeconds (double timelineSeconds)
{
    if (playheadSeconds == timelineSeconds)
        return;

    playheadSeconds = timelineSeconds;
    repaint();
}

void AudioEditorView::visibilityChanged()
{
    if (isVisible())
        refreshFromModel();
}

void AudioEditorView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // **波形が届いて初めてファイルの長さが分かる。** それまでは全体表示にできないので、
    // ここでやり直す（読み込みは別スレッドで進む。1.15の「モデルを見て追う」と同じ形）
    if (zoomToFitPending)
        zoomToFit();

    updateScrollBar();
    repaint();
}

//==============================================================================
void AudioEditorView::updateButtonVisibility()
{
    // **「まだ何も無い」ときだけ2つのボタンを出す。** 波形が出ているのに
    // 取り込みボタンが真ん中に居座ると、編集の邪魔になる
    importButton.setVisible (! clipIsSet);
    recordButton.setVisible (! clipIsSet);

    zoomInButton.setVisible (clipIsSet);
    zoomOutButton.setVisible (clipIsSet);
    zoomFitButton.setVisible (clipIsSet);
    detectButton.setVisible (clipIsSet);        // Phase 85
    horizontalScrollBar.setVisible (clipIsSet);
}

juce::Rectangle<int> AudioEditorView::getToolbarBounds() const
{
    return getLocalBounds().removeFromTop (toolbarHeight);
}

juce::Rectangle<int> AudioEditorView::getRulerBounds() const
{
    auto area = getLocalBounds();
    area.removeFromTop (toolbarHeight);
    return area.removeFromTop (rulerHeight);
}

juce::Rectangle<int> AudioEditorView::getWaveformBounds() const
{
    auto area = getLocalBounds();
    area.removeFromTop (toolbarHeight + rulerHeight);
    area.removeFromBottom (scrollBarHeight);
    return area;
}

void AudioEditorView::resized()
{
    auto toolbar = getToolbarBounds().reduced (6, 4);

    zoomFitButton.setBounds (toolbar.removeFromRight (40));
    toolbar.removeFromRight (4);
    zoomInButton.setBounds (toolbar.removeFromRight (26));
    toolbar.removeFromRight (2);
    zoomOutButton.setBounds (toolbar.removeFromRight (26));
    toolbar.removeFromRight (8);

    // 仕様書5.5.1：ヒットポイントの検出ボタン（Phase 85）
    detectButton.setBounds (toolbar.removeFromRight (juce::jmin (140, toolbar.getWidth())));
    toolbar.removeFromRight (8);

    clipNameLabel.setBounds (toolbar);

    horizontalScrollBar.setBounds (getLocalBounds().removeFromBottom (scrollBarHeight).reduced (2, 1));

    // 「まだ何も無い」ときの2つは、波形の場所の中央へ縦に並べる
    auto centre = getWaveformBounds().withSizeKeepingCentre (240, 68);

    importButton.setBounds (centre.removeFromTop (30));
    centre.removeFromTop (8);
    recordButton.setBounds (centre.removeFromTop (30));

    // **幅が決まってはじめて全体表示にできる**（クリップを渡された時点では0のことがある）
    if (zoomToFitPending)
        zoomToFit();

    updateScrollBar();
}

//==============================================================================
double AudioEditorView::getSourceLengthSeconds() const
{
    if (! clipIsSet)
        return 0.0;

    // **サムネイルが知っている長さを優先する。** ファイルを開き直さずに済み、
    // 波形と同じ範囲になる（描画とズレない）
    if (thumbnailFilePath.isNotEmpty())
    {
        auto& thumbnail = waveformCache.getThumbnail (thumbnailFilePath);
        const double length = thumbnail.getTotalLength();

        if (length > 0.0)
            return length;
    }

    // まだ読み込めていないときは、クリップが使っている範囲だけでも出す
    return clip.getSourceEnd();
}

double AudioEditorView::getVisibleSeconds() const
{
    return (double) juce::jmax (1, getWaveformBounds().getWidth()) / pixelsPerSecond;
}

int AudioEditorView::sourceTimeToX (double sourceSeconds) const
{
    return getWaveformBounds().getX()
             + (int) std::round ((sourceSeconds - scrollStartSeconds) * pixelsPerSecond);
}

double AudioEditorView::xToSourceTime (int x) const
{
    return scrollStartSeconds + (double) (x - getWaveformBounds().getX()) / pixelsPerSecond;
}


bool AudioEditorView::isShowingReversed() const
{
    return clipIsSet && clip.isReversed();
}

int AudioEditorView::sourceTimeToDisplayX (double sourceSeconds) const
{
    if (! isShowingReversed())
        return sourceTimeToX (sourceSeconds);

    const double windowStart = clip.getOffset();
    const double windowEnd = clip.getSourceEnd();

    // 窓の外はそのまま（鳴らない場所なので、ひっくり返す意味がない）
    if (sourceSeconds < windowStart || sourceSeconds > windowEnd)
        return sourceTimeToX (sourceSeconds);

    // 窓の中は、窓の中央を軸にして折り返す
    return sourceTimeToX (windowStart + windowEnd - sourceSeconds);
}

double AudioEditorView::displayXToSourceTime (int x) const
{
    const double sourceSeconds = xToSourceTime (x);

    if (! isShowingReversed())
        return sourceSeconds;

    const double windowStart = clip.getOffset();
    const double windowEnd = clip.getSourceEnd();

    if (sourceSeconds < windowStart || sourceSeconds > windowEnd)
        return sourceSeconds;

    return windowStart + windowEnd - sourceSeconds;
}
double AudioEditorView::sourceTimeToTimeline (double sourceSeconds) const
{
    if (! clipIsSet)
        return sourceSeconds;

    // 8.149：**換算はモデルが持っています**（Phase 187）。伸縮を掛けるので、
    // ここで素の引き算を書くと、伸ばした瞬間にずれます（1.27）
    return clip.sourceTimeToTimeline (sourceSeconds);
}

double AudioEditorView::timelineTimeToSource (double timelineSeconds) const
{
    if (! clipIsSet)
        return timelineSeconds;

    return clip.timelineToSourceTime (timelineSeconds);
}

//==============================================================================
void AudioEditorView::updateScrollBar()
{
    const double contentSeconds = juce::jmax (0.001, getSourceLengthSeconds());
    const double visible = juce::jmin (contentSeconds, getVisibleSeconds());

    // はみ出したままにしない（広げたときに、右端から先の何も無い場所が出たままになる）
    scrollStartSeconds = juce::jlimit (0.0, juce::jmax (0.0, contentSeconds - visible), scrollStartSeconds);

    horizontalScrollBar.setRangeLimits ({ 0.0, contentSeconds }, juce::dontSendNotification);
    horizontalScrollBar.setCurrentRange ({ scrollStartSeconds, scrollStartSeconds + visible },
                                          juce::dontSendNotification);
}

void AudioEditorView::scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved != &horizontalScrollBar)
        return;

    scrollStartSeconds = juce::jmax (0.0, newRangeStart);
    repaint();
}

void AudioEditorView::setZoom (double newPixelsPerSecond, int anchorX)
{
    const double limited = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond, newPixelsPerSecond);

    if (limited == pixelsPerSecond)
        return;

    // **掴んでいる場所の時刻を動かさない**（ピアノロールのズームと同じ。8.28）
    const double anchorTime = xToSourceTime (anchorX);

    pixelsPerSecond = limited;
    scrollStartSeconds = juce::jmax (0.0, anchorTime
                                            - (double) (anchorX - getWaveformBounds().getX()) / pixelsPerSecond);

    updateScrollBar();
    repaint();
}

void AudioEditorView::zoomIn()
{
    setZoom (pixelsPerSecond * zoomStepFactor, getWaveformBounds().getCentreX());
}

void AudioEditorView::zoomOut()
{
    setZoom (pixelsPerSecond / zoomStepFactor, getWaveformBounds().getCentreX());
}

void AudioEditorView::zoomToFit()
{
    const int width = getWaveformBounds().getWidth();
    const double contentSeconds = getSourceLengthSeconds();

    if (width <= 0 || contentSeconds <= 0.0)
        return;

    pixelsPerSecond = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond,
                                     (double) width / contentSeconds);
    scrollStartSeconds = 0.0;
    zoomToFitPending = false;

    updateScrollBar();
    repaint();
}

//==============================================================================
void AudioEditorView::mouseDown (const juce::MouseEvent& e)
{
    dragMode = DragMode::None;

    if (! clipIsSet)
        return;

    // ルーラーのクリックでシーク（アレンジ画面・ピアノロールと同じ。8.33）。
    // **返すのは曲の時刻**：再生位置はソースの時刻ではない
    if (getRulerBounds().contains (e.getPosition()))
    {
        if (onSeekRequested != nullptr)
            onSeekRequested (juce::jmax (0.0, sourceTimeToTimeline (xToSourceTime (e.x))));

        return;
    }

    if (! getWaveformBounds().contains (e.getPosition()))
        return;

    // 8.40：右クリックはメニュー（Phase 80。アレンジ画面のクリップと同じ形。8.29）
    if (e.mods.isPopupMenu())
    {
        showClipMenu (e.getScreenPosition());
        return;
    }

    dragStartPosition = e.getPosition();

    // **当たり判定の順番：フェードハンドル → ゲインの線。**
    // ハンドルは波形の上端にあり、ゲインの線は上下に動くので、
    // 0dB付近まで上げると重なる。**掴めるものが2つあるときは小さいほうを優先する**
    if (e.getPosition().toFloat().getDistanceFrom (getFadeHandlePosition (true).toFloat())
          <= fadeHandleHitRadius)
    {
        project.beginAction (utf8 ("フェードインの変更"));
        dragMode = DragMode::FadeIn;
        dragOriginalFadeSeconds = clip.getFadeInSeconds();
        return;
    }

    if (e.getPosition().toFloat().getDistanceFrom (getFadeHandlePosition (false).toFloat())
          <= fadeHandleHitRadius)
    {
        project.beginAction (utf8 ("フェードアウトの変更"));
        dragMode = DragMode::FadeOut;
        dragOriginalFadeSeconds = clip.getFadeOutSeconds();
        return;
    }

    // **線が描かれている範囲だけ掴める。** 伏せている（クリップの外の）ところで
    // 掴めてしまうと、見えていない線を動かすことになる
    const bool insideWindow = (e.x >= sourceTimeToX (clip.getOffset())
                                && e.x <= sourceTimeToX (clip.getSourceEnd()));

    if (insideWindow && std::abs (e.getPosition().y - gainDbToY (clip.getGainDb())) <= gainLineHitRadius)
    {
        // ドラッグ1回ぶんをUndoの1ステップにする（3.1）。
        // 値はドラッグ中に直接モデルへ書くので、区切りはここで作っておく
        project.beginAction (utf8 ("クリップゲインの変更"));
        dragMode = DragMode::Gain;
        dragOriginalGainDb = clip.getGainDb();
        return;
    }

    // 8.45：ヒットポイント（Phase 85。設計書2.3.4）。
    // **線の上なら掴んで動かす。空いているところなら、そこへ1つ置く。**
    // どちらも「掴めるものが無かったとき」に来るので、判定はいちばん最後
    const int hitPoint = findHitPointAt (e.getPosition());

    if (hitPoint >= 0)
    {
        const auto points = clip.getHitPoints();

        project.beginAction (utf8 ("ヒットポイントの移動"));
        dragMode = DragMode::HitPoint;
        draggedHitPoint = hitPoint;
        dragOriginalHitTime = points[hitPoint];
        repaint();
        return;
    }

    project.beginAction (utf8 ("ヒットポイントの追加"));
    clip.addHitPoint (juce::jmax (0.0, displayXToSourceTime (e.x)), &project.getUndoManager());
    repaint();
}

void AudioEditorView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! clipIsSet)
        return;

    // Ctrl＋ホイールで拡大縮小、それ以外は横スクロール（ピアノロールと同じ。8.28）
    if (e.mods.isCommandDown())
    {
        setZoom (pixelsPerSecond * (wheel.deltaY > 0 ? zoomStepFactor : 1.0 / zoomStepFactor), e.x);
        return;
    }

    const double delta = (wheel.deltaY != 0.0f ? -wheel.deltaY : wheel.deltaX) * getVisibleSeconds() * 0.25;

    scrollStartSeconds = juce::jmax (0.0, scrollStartSeconds + delta);
    updateScrollBar();
    repaint();
}

//==============================================================================
void AudioEditorView::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);

    g.setColour (AppColours::panel);
    g.fillRect (getToolbarBounds());
    g.setColour (AppColours::border);
    g.drawLine (0.0f, (float) toolbarHeight, (float) getWidth(), (float) toolbarHeight, 1.0f);

    if (! clipIsSet)
    {
        drawEmptyState (g, getWaveformBounds());
        return;
    }

    drawRuler (g, getRulerBounds());
    drawWaveform (g, getWaveformBounds());

    // 8.45：ヒットポイント（Phase 85）。**フェードより先に描く**
    // （フェードの黒い三角の下に潜らせて、端の線がうるさくならないように）
    drawHitPoints (g, getWaveformBounds());

    // 8.40：フェードとゲイン（Phase 80）。**波形の後に重ねる**
    drawFades (g, getWaveformBounds());
    drawGainLine (g, getWaveformBounds());
}

void AudioEditorView::drawEmptyState (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (AppColours::textSecondary);
    g.setFont (juce::FontOptions (12.0f));

    // ボタンの上へ出す（ボタンは`resized()`が中央へ置いている）
    g.drawText (utf8 ("このトラックにはまだオーディオがありません"),
                 area.withTrimmedBottom (area.getHeight() / 2 + 44),
                 juce::Justification::centredBottom, false);
}

double AudioEditorView::getRulerStepSeconds() const
{
    // **目盛りが詰まらない間隔を選ぶ。** 拡大率が変わっても、
    // 1目盛りがおよそ60px以上になるところまで粗くする
    static const double steps[] = { 0.01, 0.05, 0.1, 0.5, 1.0, 5.0, 10.0, 30.0, 60.0 };

    for (auto step : steps)
        if (step * pixelsPerSecond >= 60.0)
            return step;

    return 120.0;
}

void AudioEditorView::drawRuler (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (AppColours::panel);
    g.fillRect (area);
    g.setColour (AppColours::border);
    g.drawLine ((float) area.getX(), (float) area.getBottom(),
                 (float) area.getRight(), (float) area.getBottom(), 1.0f);

    const double step = getRulerStepSeconds();
    const double endSeconds = scrollStartSeconds + getVisibleSeconds();

    g.setFont (juce::FontOptions (9.0f));

    for (double t = std::floor (scrollStartSeconds / step) * step; t <= endSeconds; t += step)
    {
        if (t < 0.0)
            continue;

        const int x = sourceTimeToX (t);

        if (x < area.getX() || x > area.getRight())
            continue;

        g.setColour (AppColours::border);
        g.drawLine ((float) x, (float) area.getY(), (float) x, (float) area.getBottom(), 1.0f);

        // 目盛りの数字は「分:秒」（長いファイルでも読める形）
        const int totalTenths = (int) std::round (t * 10.0);
        const int minutes = totalTenths / 600;
        const int seconds = (totalTenths / 10) % 60;
        const int tenths = totalTenths % 10;

        juce::String text = juce::String (minutes) + ":" + juce::String (seconds).paddedLeft ('0', 2);

        if (step < 1.0)
            text += "." + juce::String (tenths);

        g.setColour (AppColours::textSecondary);
        g.drawText (text, x + 3, area.getY(), 60, area.getHeight(), juce::Justification::centredLeft, false);
    }
}

void AudioEditorView::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (AppColours::canvas);
    g.fillRect (area);

    const double sourceLength = getSourceLengthSeconds();
    const double windowStart = clip.getOffset();
    const double windowEnd = clip.getSourceEnd();

    // **見えている範囲だけを渡す。** 拡大していても読み込み・描画の量が増えない
    const double fromSeconds = juce::jmax (0.0, scrollStartSeconds);
    const double toSeconds = juce::jmin (sourceLength, scrollStartSeconds + getVisibleSeconds());

    if (toSeconds > fromSeconds && thumbnailFilePath.isNotEmpty())
    {
        auto& thumbnail = waveformCache.getThumbnail (thumbnailFilePath);

        const int fromX = sourceTimeToX (fromSeconds);
        const int toX = sourceTimeToX (toSeconds);

        // 8.40：**クリップゲインぶん縦に伸ばして描く**（Phase 80）。
        // 見えている大きさと鳴る音量が一致するので、上げ下げの結果が目で分かる
        g.setColour (AppColours::purple);

        if (! isShowingReversed())
        {
            thumbnail.drawChannels (g, { fromX, area.getY(), juce::jmax (1, toX - fromX), area.getHeight() },
                                     fromSeconds, toSeconds, clip.getGainLinear());
        }
        else
        {
            // 8.47：**逆再生のときは、窓の中だけ左右反転して描く**（Phase 87）。
            //
            // **窓の外はそのまま。** 鳴らない場所なので、ひっくり返す意味がありません。
            // 3回に分けて描くことになりますが、**それぞれの範囲は意味が違う**ので
            // まとめないほうが読めます。
            const int windowStartX = sourceTimeToX (windowStart);
            const int windowEndX = sourceTimeToX (windowEnd);

            auto drawRange = [&] (int x1, int x2, double t1, double t2)
            {
                if (x2 <= x1 || t2 <= t1)
                    return;

                thumbnail.drawChannels (g, { x1, area.getY(), x2 - x1, area.getHeight() },
                                         t1, t2, clip.getGainLinear());
            };

            // 窓より前（そのまま）
            if (fromSeconds < windowStart)
                drawRange (fromX, juce::jmin (toX, windowStartX),
                            fromSeconds, juce::jmin (toSeconds, windowStart));

            // 窓の中（**左右反転**）。折り返しの軸は窓の中央
            {
                juce::Graphics::ScopedSaveState saved (g);

                const float centreX = (float) (windowStartX + windowEndX) * 0.5f;

                g.addTransform (juce::AffineTransform::translation (-centreX, 0.0f)
                                    .scaled (-1.0f, 1.0f)
                                    .translated (centreX, 0.0f));

                drawRange (windowStartX, windowEndX, windowStart, windowEnd);
            }

            // 窓より後ろ（そのまま）
            if (toSeconds > windowEnd)
                drawRange (juce::jmax (fromX, windowEndX), toX,
                            juce::jmax (fromSeconds, windowEnd), toSeconds);
        }
    }

    // **クリップが使っていない範囲を伏せる**（トリムで隠れているところ。8.35と同じ考え方）。
    // ここを描かないと、「ファイルのどこを切り出したのか」が分からない
    g.setColour (AppColours::background.withAlpha (0.72f));

    const int windowStartX = sourceTimeToX (windowStart);
    const int windowEndX = sourceTimeToX (windowEnd);

    if (windowStartX > area.getX())
        g.fillRect (area.getX(), area.getY(), windowStartX - area.getX(), area.getHeight());

    if (windowEndX < area.getRight())
        g.fillRect (windowEndX, area.getY(), area.getRight() - windowEndX, area.getHeight());

    // 使っている範囲の境目（トリムの位置）を線で出す
    g.setColour (AppColours::orange.withAlpha (0.8f));

    if (windowStartX >= area.getX() && windowStartX <= area.getRight())
        g.drawLine ((float) windowStartX, (float) area.getY(),
                     (float) windowStartX, (float) area.getBottom(), 1.5f);

    if (windowEndX >= area.getX() && windowEndX <= area.getRight())
        g.drawLine ((float) windowEndX, (float) area.getY(),
                     (float) windowEndX, (float) area.getBottom(), 1.5f);

    // 再生位置。**曲の時刻をソースの時刻へ直してから**描く（基準が違う。8.28）
    const double playheadSource = timelineTimeToSource (playheadSeconds);

    if (playheadSource >= windowStart && playheadSource <= windowEnd)
    {
        const int x = sourceTimeToX (playheadSource);

        if (x >= area.getX() && x <= area.getRight())
        {
            g.setColour (AppColours::orange);
            g.drawLine ((float) x, (float) area.getY(), (float) x, (float) area.getBottom(), 1.5f);
        }
    }
}
//==============================================================================
// 仕様書5.5：フェードとクリップゲインの編集（Phase 80／8.40）
//==============================================================================

juce::Point<int> AudioEditorView::getFadeHandlePosition (bool fadeIn) const
{
    auto area = getWaveformBounds();

    // **アレンジ画面と同じ形**：波形の上端に置き、横へ動かすと長さが変わる（8.29）
    // 8.149：**フェードはタイムラインの秒**（Phase 187）。この軸はソースの秒なので、
    // 必ず`timelineToSourceTime()`を通すこと——伸ばすと持ち手だけ取り残されます
    if (fadeIn)
        return { sourceTimeToX (clip.timelineToSourceTime (clip.getStartTime()
                                                             + clip.getFadeInSeconds())), area.getY() };

    return { sourceTimeToX (clip.timelineToSourceTime (clip.getStartTime() + clip.getLength()
                                                         - clip.getFadeOutSeconds())),
             area.getY() };
}

int AudioEditorView::gainDbToY (float gainDb) const
{
    auto area = getWaveformBounds();
    const float t = juce::jlimit (0.0f, 1.0f,
                                   (gainDb - minClipGainDb) / (maxClipGainDb - minClipGainDb));

    // 上が大きい（Y座標は下向きが正なので反転する）
    return area.getBottom() - (int) std::round (t * (float) area.getHeight());
}

float AudioEditorView::yToGainDb (int y) const
{
    auto area = getWaveformBounds();

    if (area.getHeight() <= 0)
        return 0.0f;

    const float t = juce::jlimit (0.0f, 1.0f,
                                   (float) (area.getBottom() - y) / (float) area.getHeight());

    return minClipGainDb + t * (maxClipGainDb - minClipGainDb);
}

//==============================================================================
void AudioEditorView::drawFades (juce::Graphics& g, juce::Rectangle<int> area)
{
    const double windowStart = clip.getOffset();
    const double windowEnd = clip.getSourceEnd();

    const int windowStartX = sourceTimeToX (windowStart);
    const int windowEndX = sourceTimeToX (windowEnd);

    // 8.149：**この軸はソースの秒**なので、伸縮ぶんで割ってから掛ける（Phase 187）
    const double sourcePerTimeline = 1.0 / clip.getStretch();

    const int fadeInPixels = juce::jlimit (0, juce::jmax (0, windowEndX - windowStartX),
                                            (int) (clip.getFadeInSeconds() * sourcePerTimeline * pixelsPerSecond));
    const int fadeOutPixels = juce::jlimit (0, juce::jmax (0, windowEndX - windowStartX),
                                             (int) (clip.getFadeOutSeconds() * sourcePerTimeline * pixelsPerSecond));

    // 8.41：**アレンジ画面のクリップとまったく同じ描き方**（Phase 81）。
    //
    // Phase 80では「鳴るときと同じカーブ」で塗っていましたが、
    // **同じものが画面ごとに違って見える**ほうが困ります（8.37でも同じ判断をしている）。
    // 黒い三角を半透明で重ねる、`TimelineComponent::drawClip()`と揃えた形にしました。
    if (fadeInPixels > 0)
    {
        juce::Path fadePath;
        fadePath.startNewSubPath ((float) windowStartX, (float) area.getBottom());
        fadePath.lineTo ((float) windowStartX, (float) area.getY());
        fadePath.lineTo ((float) (windowStartX + fadeInPixels), (float) area.getY());
        fadePath.closeSubPath();

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (fadePath);
    }

    if (fadeOutPixels > 0)
    {
        juce::Path fadePath;
        fadePath.startNewSubPath ((float) windowEndX, (float) area.getBottom());
        fadePath.lineTo ((float) windowEndX, (float) area.getY());
        fadePath.lineTo ((float) (windowEndX - fadeOutPixels), (float) area.getY());
        fadePath.closeSubPath();

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (fadePath);
    }

    // 掴む場所（丸）。**アレンジ画面と同じ色・同じ形**にしてある。
    // あちらは選択中のクリップにだけ出すが、ここは**編集中の1本しか出していない**ので常に出す
    auto drawHandle = [&] (juce::Point<int> position, bool isDragged)
    {
        if (position.x < area.getX() - 8 || position.x > area.getRight() + 8)
            return;

        g.setColour (isDragged ? AppColours::orange.brighter (0.3f) : AppColours::orange);
        g.fillEllipse ((float) position.x - fadeHandleRadius, (float) position.y - fadeHandleRadius,
                        fadeHandleRadius * 2.0f, fadeHandleRadius * 2.0f);
    };

    drawHandle (getFadeHandlePosition (true),  dragMode == DragMode::FadeIn);
    drawHandle (getFadeHandlePosition (false), dragMode == DragMode::FadeOut);
}

void AudioEditorView::drawGainLine (juce::Graphics& g, juce::Rectangle<int> area)
{
    const float gainDb = clip.getGainDb();
    const int y = gainDbToY (gainDb);

    const int fromX = juce::jmax (area.getX(), sourceTimeToX (clip.getOffset()));
    const int toX = juce::jmin (area.getRight(), sourceTimeToX (clip.getSourceEnd()));

    if (toX <= fromX)
        return;

    const bool isDragged = (dragMode == DragMode::Gain);

    g.setColour (AppColours::purple.withAlpha (isDragged ? 1.0f : 0.7f));
    g.drawLine ((float) fromX, (float) y, (float) toX, (float) y, isDragged ? 2.0f : 1.5f);

    // 数値も出す。**線の位置だけでは狙った値に合わせられない**（8.35と同じ理由）
    const juce::String text = (gainDb >= 0.0f ? "+" : "") + juce::String (gainDb, 1) + " dB";

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (AppColours::textPrimary);
    g.drawText (text, fromX + 6, y - 14, 80, 13, juce::Justification::centredLeft, false);
}

//==============================================================================
void AudioEditorView::mouseDrag (const juce::MouseEvent& e)
{
    if (! clipIsSet || dragMode == DragMode::None)
        return;

    auto& undoManager = project.getUndoManager();

    // 8.45：ヒットポイントを動かす（Phase 85）。**時刻の配列ごと書き直す**
    // （並びは時刻順なので、動かしたら並べ直す。番号は引き直す）
    if (dragMode == DragMode::HitPoint)
    {
        auto points = clip.getHitPoints();

        if (! juce::isPositiveAndBelow (draggedHitPoint, points.size()))
            return;

        const double newTime = juce::jmax (0.0, displayXToSourceTime (e.getPosition().x));

        points.set (draggedHitPoint, newTime);
        points.sort();
        clip.setHitPoints (points, &undoManager);

        // **並べ替えで番号がずれる**ので、動かした時刻から引き直す（1.32と同じ話）
        draggedHitPoint = points.indexOf (newTime);

        repaint();
        return;
    }

    if (dragMode == DragMode::Gain)
    {
        // **上へドラッグすると上がる。** 掴んだ時点の値からの差で書く
        const float newGainDb = dragOriginalGainDb
                                  + (yToGainDb (e.getPosition().y) - yToGainDb (dragStartPosition.y));

        clip.setGainDb (newGainDb, &undoManager);
        repaint();
        return;
    }

    // フェードの長さ。**掴んだ時点の長さ＋動かした秒数**で書く
    // 8.149：**この軸はソースの秒、フェードはタイムラインの秒**（Phase 187）。
    // 掛けずに書くと、伸ばしたクリップでは持ち手より速く/遅く伸びます
    const double deltaSeconds = (double) (e.getPosition().x - dragStartPosition.x)
                                    / pixelsPerSecond * clip.getStretch();
    const double signedDelta = (dragMode == DragMode::FadeIn) ? deltaSeconds : -deltaSeconds;

    // **クリップの長さを超えさせない**（フェードイン＋アウトが重なると鳴り方が壊れる）
    const double limit = juce::jmax (0.0, clip.getLength()
                                            - (dragMode == DragMode::FadeIn ? clip.getFadeOutSeconds()
                                                                             : clip.getFadeInSeconds()));

    const double newLength = juce::jlimit (0.0, limit, dragOriginalFadeSeconds + signedDelta);

    if (dragMode == DragMode::FadeIn)
        clip.setFadeInSeconds (newLength, &undoManager);
    else
        clip.setFadeOutSeconds (newLength, &undoManager);

    repaint();
}

void AudioEditorView::mouseUp (const juce::MouseEvent&)
{
    // モデルへはドラッグ中に書いてあるので、ここでは掴んだ状態を畳むだけ（8.37と同じ形）
    if (dragMode == DragMode::None)
        return;

    dragMode = DragMode::None;
    draggedHitPoint = -1;
    repaint();
}

void AudioEditorView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! clipIsSet || ! getWaveformBounds().contains (e.getPosition()))
        return;

    auto& undoManager = project.getUndoManager();

    // **掴めるものは、ダブルクリックで既定値へ戻る**（8.37のつまみと同じ）
    if (e.getPosition().toFloat().getDistanceFrom (getFadeHandlePosition (true).toFloat())
          <= fadeHandleHitRadius)
    {
        project.beginAction (utf8 ("フェードインを消す"));
        clip.setFadeInSeconds (0.0, &undoManager);
        repaint();
        return;
    }

    if (e.getPosition().toFloat().getDistanceFrom (getFadeHandlePosition (false).toFloat())
          <= fadeHandleHitRadius)
    {
        project.beginAction (utf8 ("フェードアウトを消す"));
        clip.setFadeOutSeconds (0.0, &undoManager);
        repaint();
        return;
    }

    if (std::abs (e.getPosition().y - gainDbToY (clip.getGainDb())) <= gainLineHitRadius)
    {
        project.beginAction (utf8 ("クリップゲインを0dBへ"));
        clip.setGainDb (0.0f, &undoManager);
        repaint();
        return;
    }

    // 8.45：**ヒットポイントのダブルクリックで削除**（Phase 85。設計書2.3.4）。
    // 1回目のmouseDownで置いた点がここで消えるので、**線の上でなければ何も残らない**
    const int hitPoint = findHitPointAt (e.getPosition());

    if (hitPoint >= 0)
    {
        auto points = clip.getHitPoints();

        project.beginAction (utf8 ("ヒットポイントの削除"));
        points.remove (hitPoint);
        clip.setHitPoints (points, &undoManager);

        draggedHitPoint = -1;
        dragMode = DragMode::None;
        repaint();
    }
}

//==============================================================================
void AudioEditorView::showClipMenu (juce::Point<int> screenPosition)
{
    if (! clipIsSet)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::File (clip.getSourceFilePath()).getFileName());
    menu.addItem (1, utf8 ("ゲインを数値で入力..."));
    menu.addItem (2, utf8 ("ゲインを0dBへ戻す"), clip.getGainDb() != 0.0f);
    menu.addSeparator();
    menu.addItem (3, utf8 ("フェードインを消す"), clip.getFadeInSeconds() > 0.0);
    menu.addItem (4, utf8 ("フェードアウトを消す"), clip.getFadeOutSeconds() > 0.0);
    menu.addSeparator();

    // 仕様書5.5.1：ヒットポイント（Phase 85）
    menu.addItem (5, utf8 ("ヒットポイントを検出"), ! isDetectingHitPoints);
    menu.addItem (6, utf8 ("ヒットポイントを全部消す"), ! clip.getHitPoints().isEmpty());
    menu.addSeparator();

    // 8.46：反転とオートフェード（Phase 86）。**アレンジ画面のクリップメニューと同じ項目**
    menu.addItem (7, utf8 ("逆再生にする"), true, clip.isReversed());
    menu.addItem (8, utf8 ("オートフェードをかける"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this] (int result)
        {
            // メニューを開いているあいだにクリップが消えている可能性がある（1.32）
            if (result <= 0 || ! clip.state.getParent().isValid())
                return;

            if (result == 1)
            {
                showGainEntry();
                return;
            }

            auto& undoManager = project.getUndoManager();

            if (result == 2)
            {
                project.beginAction (utf8 ("クリップゲインを0dBへ"));
                clip.setGainDb (0.0f, &undoManager);
            }
            else if (result == 3)
            {
                project.beginAction (utf8 ("フェードインを消す"));
                clip.setFadeInSeconds (0.0, &undoManager);
            }
            else if (result == 4)
            {
                project.beginAction (utf8 ("フェードアウトを消す"));
                clip.setFadeOutSeconds (0.0, &undoManager);
            }
            else if (result == 5)
            {
                detectHitPoints();
                return;   // 別スレッドで走るので、ここでは描き直さない
            }
            else if (result == 6)
            {
                project.beginAction (utf8 ("ヒットポイントを全部消す"));
                clip.setHitPoints ({}, &undoManager);
            }
            else if (result == 7)
            {
                project.beginAction (utf8 ("逆再生の切り替え"));
                clip.setReversed (! clip.isReversed(), &undoManager);
                setClip (clip);   // 見出しの印を出し直す
            }
            else if (result == 8)
            {
                project.beginAction (utf8 ("オートフェード"));
                clip.applyAutoFade (&undoManager);
            }

            repaint();
        });
}

void AudioEditorView::showGainEntry()
{
    NameEntry::show (utf8 ("クリップゲイン"),
                      utf8 ("dBで入力してください（-24〜+24）。"),
                      juce::String (clip.getGainDb(), 1),
                      [this] (const juce::String& text)
                      {
                          // 入力欄を開いているあいだにクリップが消えている可能性がある
                          if (! clip.state.getParent().isValid())
                              return;

                          // **数字でないものは受け取らない。** `getDoubleValue()`は
                          // 読めない文字列を0として返すので、打ち間違いが「0dBにする」になる
                          const auto trimmed = text.trim();

                          if (trimmed.isEmpty() || ! trimmed.containsOnly ("0123456789+-."))
                              return;

                          project.beginAction (utf8 ("クリップゲインの変更"));
                          clip.setGainDb ((float) trimmed.getDoubleValue(), &project.getUndoManager());
                          repaint();
                      });
}
//==============================================================================
// 仕様書5.5.1：ヒットポイント（Phase 85／8.45）
//==============================================================================

void AudioEditorView::detectHitPoints()
{
    if (! clipIsSet || isDetectingHitPoints)
        return;

    const juce::File file (clip.getSourceFilePath());

    if (! file.existsAsFile())
        return;

    isDetectingHitPoints = true;
    detectButton.setButtonText (utf8 ("検出中..."));
    detectButton.setEnabled (false);

    // **解析は別スレッドで。** `HitPointDetector`はファイルを読み直すので、
    // 画面のスレッドで回すと、長いファイルではその間ずっと固まります
    // （クラスの説明にも「呼び出し側の責務」と書いてあるとおり）。
    //
    // **戻ってきたときに自分が生きているとは限らない**（別のクリップへ移った・
    // パネルを閉じた）ので、`SafePointer`で確かめてから書きます。
    juce::Component::SafePointer<AudioEditorView> safeThis (this);
    auto clipState = clip.state;
    auto& formatManager = waveformCache.getFormatManager();

    detectionPool.addJob ([safeThis, file, clipState, &formatManager]
    {
        auto detected = HitPointDetector::detect (file, formatManager);

        juce::MessageManager::callAsync ([safeThis, clipState, detected]
        {
            auto* view = safeThis.getComponent();

            if (view == nullptr)
                return;

            view->isDetectingHitPoints = false;
            view->detectButton.setButtonText (utf8 ("ヒットポイント検出"));
            view->detectButton.setEnabled (true);

            // **掴んでいるクリップが入れ替わっていたら書かない**（別の音へ書き込む）
            if (! clipState.isValid() || ! clipState.getParent().isValid()
                 || clipState != view->clip.state)
                return;

            // 8.47：**見つからなかったときは、そう伝えること**（Phase 87）。
            // 黙って何も起きないと、ボタンが壊れているようにしか見えません（1.9）
            if (detected.isEmpty())
            {
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::InfoIcon)
                        .withTitle (utf8 ("ヒットポイント"))
                        .withMessage (utf8 ("音の立ち上がりが見つかりませんでした。")
                                          + juce::String ("\n\n")
                                          + utf8 ("音が滑らかに変わる素材では検出できないことがあります。")
                                          + utf8 ("波形をクリックすると、手で置けます。"))
                        .withButton (utf8 ("OK")),
                    nullptr);
                return;
            }

            view->project.beginAction (utf8 ("ヒットポイントの検出"));
            AudioClip (clipState).setHitPoints (detected, &view->project.getUndoManager());
            view->repaint();
        });
    });
}

int AudioEditorView::findHitPointAt (juce::Point<int> position) const
{
    if (! clipIsSet || ! getWaveformBounds().contains (position))
        return -1;

    const auto points = clip.getHitPoints();

    int closest = -1;
    float closestDistance = hitPointHitRadius;

    for (int i = 0; i < points.size(); ++i)
    {
        // 8.47：**逆再生では見せている位置で当てる**（Phase 87）
        const float distance = std::abs ((float) (sourceTimeToDisplayX (points[i]) - position.x));

        if (distance <= closestDistance)
        {
            closestDistance = distance;
            closest = i;
        }
    }

    return closest;
}

void AudioEditorView::drawHitPoints (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto points = clip.getHitPoints();

    if (points.isEmpty())
        return;

    const double windowStart = clip.getOffset();
    const double windowEnd = clip.getSourceEnd();

    for (int i = 0; i < points.size(); ++i)
    {
        // 8.47：**逆再生では反転した位置に描く**（Phase 87）。音の形と線が離れないように
        const int x = sourceTimeToDisplayX (points[i]);

        if (x < area.getX() || x > area.getRight())
            continue;

        // **クリップが使っていない範囲のものは薄く**（伏せ表示と揃える。8.39）。
        // 消してしまわないのは、トリムを戻せばまた使うため
        const bool inWindow = (points[i] >= windowStart && points[i] <= windowEnd);
        const bool isDragged = (i == draggedHitPoint);

        // 8.47：**地の色に合わせた色を使うこと**（Phase 87）。
        // Phase 85は黒で描いていましたが、**ダークテーマでは地も黒に近い**ので
        // 線がまったく見えませんでした（1.43と同じ形の見落とし）
        g.setColour (isDragged ? AppColours::orange
                               : AppColours::textPrimary.withAlpha (inWindow ? 0.85f : 0.30f));

        g.drawLine ((float) x, (float) area.getY(), (float) x, (float) area.getBottom(),
                     isDragged ? 2.0f : 1.0f);
    }
}
