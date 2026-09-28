#include "MantaAnalyzerEditor.h"

#include "../MantaTheme.h"
#include "../MantaPluginToolbar.h"

namespace
{
    //==========================================================================
    /**
        8.329：設定のポップオーバー（仕様書3章「歯車から開くポップオーバーに集約」）。

        **変えたらすぐ効きます**（「適用」ボタンは無い）。変えた設定は`onChange`で
        エディタへ渡し、エディタが`applySettings()`で配ります。
    */
    class SettingsPanel : public juce::Component
    {
    public:
        SettingsPanel (const AnalyzerSettings& initial, bool monoInput,
                       std::function<void (const AnalyzerSettings&)> changed)
            : settings (initial), onChange (std::move (changed))
        {
            using S = AnalyzerSettings;

            // 8.330：レンジを出さないあいだは、レンジの設定も出さない（`showsTargetRange`）
            if (MantaAnalyzerEditor::showsTargetRange)
            {
                addCombo (align, "Range position", { "Auto follow", "Calibrated", "Manual offset" },
                          (int) settings.align, [this] (int i) { settings.align = (S::Align) i; });

                addSlider (calDbfs, "Calibration level", -60, 0, 1, settings.calibrationDbfs, " dBFS",
                           [this] (double v) { settings.calibrationDbfs = (float) v; });
                addSlider (calSpl, "...equals", 40, 120, 1, settings.calibrationSpl, " dB SPL",
                           [this] (double v) { settings.calibrationSpl = (float) v; });
                addSlider (manual, "Manual offset", -12, 12, 0.5, settings.manualOffsetDb, " dB",
                           [this] (double v) { settings.manualOffsetDb = (float) v; });
                addSlider (opacity, "Range fill", 0, 60, 1, settings.rangeOpacity * 100.0, " %",
                           [this] (double v) { settings.rangeOpacity = (float) v / 100.0f; });
                addToggle (highlight, "Highlight excess", settings.highlightDeviation,
                           [this] (bool b) { settings.highlightDeviation = b; });
            }
            addToggle (realtime, "Realtime curve", settings.realtimeVisible,
                       [this] (bool b) { settings.realtimeVisible = b; });

            addCombo (response, "Response", { "Fast (0.3 s)", "Medium (1 s)", "Slow (3 s)", "Integrate" },
                      (int) settings.response, [this] (int i) { settings.response = (S::Response) i; });
            addSlider (hold, "Peak hold", 0.5, 10, 0.5, settings.peakHoldSeconds, " s",
                       [this] (double v) { settings.peakHoldSeconds = (float) v; });
            addToggle (holdForever, "Hold peaks forever", settings.peakHoldInfinite,
                       [this] (bool b) { settings.peakHoldInfinite = b; });

            {
                juce::StringArray names { "Off", "1/24 oct", "1/12 oct", "1/6 oct", "1/3 oct", "1 oct" };
                const int index = S::smoothingChoices().indexOf (settings.smoothingOctaves);
                addCombo (smoothing, "Smoothing", names, juce::jmax (0, index),
                          [this] (int i) { settings.smoothingOctaves = S::smoothingChoices()[i]; });
            }

            addSlider (slope, "Slope", 0, 6, 0.5, settings.slopeDbPerOctave, " dB/oct",
                       [this] (double v) { settings.slopeDbPerOctave = (float) v; });

            {
                juce::StringArray names;
                for (int size : S::fftSizeChoices())
                    names.add (juce::String (size));

                addCombo (fftSize, "FFT size", names, juce::jmax (0, S::fftSizeChoices().indexOf (settings.fftSize)),
                          [this] (int i) { settings.fftSize = S::fftSizeChoices()[i]; });
            }

            addCombo (window, "Window", { "Hann", "Blackman-Harris" }, (int) settings.window,
                      [this] (int i) { settings.window = (S::Window) i; });
            addCombo (overlap, "Overlap", { "50 %", "75 %" }, settings.overlap < 0.6f ? 0 : 1,
                      [this] (int i) { settings.overlap = i == 0 ? 0.5f : 0.75f; });
            addCombo (channel, "Channel", { "L+R", "L", "R", "Mid", "Side" }, (int) settings.channel,
                      [this] (int i) { settings.channel = (S::Channel) i; });

            // 仕様書7.1：**モノのときは解析チャンネルを選ばせない**
            channel.setEnabled (! monoInput);

            addSlider (freqMin, "Lowest frequency", 10, 100, 1, settings.frequencyMin, " Hz",
                       [this] (double v) { settings.frequencyMin = (float) v; });
            addSlider (freqMax, "Highest frequency", 5000, 22000, 100, settings.frequencyMax, " Hz",
                       [this] (double v) { settings.frequencyMax = (float) v; });
            addSlider (levelTop, "Top of scale", -24, 12, 1, settings.levelTopDb, " dB",
                       [this] (double v) { settings.levelTopDb = (float) v; });

            {
                juce::StringArray names;
                for (float r : S::levelRangeChoices())
                    names.add (juce::String ((int) r) + " dB");

                addCombo (levelRange, "Scale height", names,
                          juce::jmax (0, S::levelRangeChoices().indexOf (settings.levelRangeDb)),
                          [this] (int i) { settings.levelRangeDb = S::levelRangeChoices()[i]; });
            }

            addToggle (grid, "Grid", settings.gridVisible, [this] (bool b) { settings.gridVisible = b; });

            // **係数の版を隠さない**（仕様書は2023年版。いまは2003年版。TargetRange.h）
            note.setText (juce::String ("Target curves: ") + Iso226::edition + " coefficients",
                          juce::dontSendNotification);
            note.setFont (juce::Font (juce::FontOptions (11.0f)));
            note.setColour (juce::Label::textColourId, MantaTheme::textDim());
            if (MantaAnalyzerEditor::showsTargetRange)   // レンジが無いなら、係数の版も要らない
                addAndMakeVisible (note);

            refreshEnabled();

            const int rowsPerColumn = (rows.size() + 1) / 2;
            setSize (2 * columnWidth + 3 * margin,
                     rowsPerColumn * rowHeight + 2 * margin + (MantaAnalyzerEditor::showsTargetRange ? 20 : 0));
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (MantaTheme::panelBackground());
        }

        void resized() override
        {
            const int rowsPerColumn = (rows.size() + 1) / 2;

            for (int i = 0; i < rows.size(); ++i)
            {
                const int column = i / rowsPerColumn;
                const int row = i % rowsPerColumn;

                juce::Rectangle<int> area (margin + column * (columnWidth + margin),
                                           margin + row * rowHeight, columnWidth, rowHeight - 4);

                auto& r = rows.getReference (i);

                if (r.label != nullptr)
                    r.label->setBounds (area.removeFromLeft (labelWidth));

                r.control->setBounds (area);
            }

            note.setBounds (margin, getHeight() - margin - 18, getWidth() - 2 * margin, 18);
        }

    private:
        struct Row
        {
            juce::Label* label = nullptr;
            juce::Component* control = nullptr;
        };

        void changed()
        {
            settings.constrain();
            refreshEnabled();

            if (onChange != nullptr)
                onChange (settings);
        }

        /** 使わない欄は薄くする（合わせ方で、校正値と手動オフセットのどちらが効くかが変わる）。 */
        void refreshEnabled()
        {
            using A = AnalyzerSettings::Align;
            calDbfs.setEnabled (settings.align == A::calibrated);
            calSpl.setEnabled (settings.align == A::calibrated);
            manual.setEnabled (settings.align == A::manualOffset);
            hold.setEnabled (! settings.peakHoldInfinite);
        }

        juce::Label& makeLabel (const juce::String& text)
        {
            auto* label = labels.add (new juce::Label ({}, text));
            label->setFont (juce::Font (juce::FontOptions (12.0f)));
            label->setColour (juce::Label::textColourId, MantaTheme::text());
            addAndMakeVisible (label);
            return *label;
        }

        void addCombo (juce::ComboBox& box, const juce::String& text, const juce::StringArray& items,
                       int selected, std::function<void (int)> apply)
        {
            box.addItemList (items, 1);
            box.setSelectedItemIndex (selected, juce::dontSendNotification);
            box.onChange = [this, &box, apply]
            {
                apply (box.getSelectedItemIndex());
                changed();
            };

            addAndMakeVisible (box);
            rows.add ({ &makeLabel (text), &box });
        }

        void addSlider (juce::Slider& slider, const juce::String& text, double min, double max, double step,
                        double value, const juce::String& suffix, std::function<void (double)> apply)
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 20);
            slider.setRange (min, max, step);
            slider.setTextValueSuffix (suffix);
            slider.setValue (value, juce::dontSendNotification);
            slider.onValueChange = [this, &slider, apply]
            {
                apply (slider.getValue());
                changed();
            };

            addAndMakeVisible (slider);
            rows.add ({ &makeLabel (text), &slider });
        }

        void addToggle (juce::ToggleButton& toggle, const juce::String& text, bool value, std::function<void (bool)> apply)
        {
            toggle.setButtonText (text);
            toggle.setToggleState (value, juce::dontSendNotification);
            toggle.setColour (juce::ToggleButton::textColourId, MantaTheme::text());
            toggle.onClick = [this, &toggle, apply]
            {
                apply (toggle.getToggleState());
                changed();
            };

            addAndMakeVisible (toggle);
            rows.add ({ nullptr, &toggle });
        }

        static constexpr int margin = 12;
        static constexpr int columnWidth = 330;
        static constexpr int labelWidth = 120;
        static constexpr int rowHeight = 28;

        AnalyzerSettings settings;
        std::function<void (const AnalyzerSettings&)> onChange;

        juce::OwnedArray<juce::Label> labels;
        juce::Array<Row> rows;

        juce::ComboBox align, response, smoothing, fftSize, window, overlap, channel, levelRange;
        juce::Slider calDbfs, calSpl, manual, opacity, hold, slope, freqMin, freqMax, levelTop;
        juce::ToggleButton highlight, realtime, holdForever, grid;
        juce::Label note;
    };
}

//==============================================================================

MantaAnalyzerEditor::MantaAnalyzerEditor (MantaAnalyzerProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      settings (processorToUse.getSettings())
{
    addAndMakeVisible (view);

    view.onResetPeaks = [this] { resetPeaks(); };
    view.onLevelViewChanged = [this] (float top, float rangeDb)
    {
        auto s = settings;
        s.levelTopDb = top;
        s.levelRangeDb = rangeDb;
        applySettings (s);
    };

    // フッター（仕様書3.6：左から フリーズ・ピークリセット・ラウドネスレベル・表示切替・歯車）
    MantaPluginToolbar::styleButton (freezeButton, "Freeze");
    freezeButton.setClickingTogglesState (true);
    freezeButton.onClick = [this]
    {
        frozen = freezeButton.getToggleState();

        // 止めていたあいだの音は捨てる（**再開したとき、古い音から数え始めない**）
        if (! frozen)
            processor.getFifo().discardAll();
    };

    MantaPluginToolbar::styleButton (resetPeakButton, "Reset Peak");
    resetPeakButton.onClick = [this] { resetPeaks(); };

    for (auto* slider : { &lowPhonSlider, &highPhonSlider })
    {
        slider->setSliderStyle (juce::Slider::LinearBar);
        slider->setRange (20.0, 90.0, 1.0);

        if (showsTargetRange)   // 8.330：レンジを出さないあいだは、phonも出さない
            addAndMakeVisible (*slider);
    }

    lowPhonSlider.textFromValueFunction = [] (double v) { return "Low " + juce::String ((int) v) + " phon"; };
    highPhonSlider.textFromValueFunction = [] (double v) { return "High " + juce::String ((int) v) + " phon"; };

    lowPhonSlider.onValueChange = [this]
    {
        auto s = settings;
        s.lowPhon = (float) lowPhonSlider.getValue();

        // **上限は下限＋3以上**（仕様書5.1）。下限を上げたら、上限を押し上げる
        s.highPhon = juce::jmax (s.highPhon, s.lowPhon + 3.0f);
        applySettings (s);
    };

    highPhonSlider.onValueChange = [this]
    {
        auto s = settings;
        s.highPhon = (float) highPhonSlider.getValue();
        s.lowPhon = juce::jmin (s.lowPhon, s.highPhon - 3.0f);
        applySettings (s);
    };

    auto setupToggle = [this] (juce::TextButton& button, const char* text, bool AnalyzerSettings::* member)
    {
        MantaPluginToolbar::styleButton (button, text);
        button.setClickingTogglesState (true);
        button.onClick = [this, &button, member]
        {
            auto s = settings;
            s.*member = button.getToggleState();
            applySettings (s);
        };
        addAndMakeVisible (button);
    };

    setupToggle (curveButton, "Curve", &AnalyzerSettings::curveVisible);
    setupToggle (peakButton, "Peak", &AnalyzerSettings::peakVisible);
    setupToggle (rangeButton, "Range", &AnalyzerSettings::rangeVisible);
    rangeButton.setVisible (showsTargetRange);

    MantaPluginToolbar::styleButton (settingsButton, "Settings");
    settingsButton.onClick = [this] { showSettingsPanel(); };

    for (auto* button : { &freezeButton, &resetPeakButton, &settingsButton })
        addAndMakeVisible (*button);

    // 読み込みで設定が変わったら、こちらも取り直す
    processor.onSettingsLoaded = [this] { applySettings (processor.getSettings()); };

    applyFooterColours();
    refreshFooter();
    view.setSettings (getDisplaySettings());
    view.setFrame (frame);

    // 8.330：**大きさは固定**（本人の指定。ほかの内蔵プラグインと同じ。9.5）。
    // `setResizable()`を呼ばず、大きさも覚えません
    setSize (fixedWidth, fixedHeight);

    // 仕様書4.4：**開いているあいだだけ書き写させる**。開いたら解析・ピーク・位置をやり直す
    processor.getFifo().discardAll();
    processor.setEditorOpen (true);
    range.snapOnNextInput();

    startTimerHz (currentRate);
}

MantaAnalyzerEditor::~MantaAnalyzerEditor()
{
    stopTimer();
    processor.setEditorOpen (false);
    processor.onSettingsLoaded = nullptr;
}

void MantaAnalyzerEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());
}

void MantaAnalyzerEditor::resized()
{
    auto area = getLocalBounds();
    auto footer = area.removeFromBottom (footerHeight).reduced (6, 5);

    view.setBounds (area);

    auto place = [&footer] (juce::Component& c, int width)
    {
        c.setBounds (footer.removeFromLeft (width));
        footer.removeFromLeft (6);
    };

    place (freezeButton, 64);
    place (resetPeakButton, 86);
    footer.removeFromLeft (8);

    if (showsTargetRange)
    {
        place (lowPhonSlider, 104);
        place (highPhonSlider, 108);
        footer.removeFromLeft (8);
    }

    place (curveButton, 56);
    place (peakButton, 50);

    if (showsTargetRange)
        place (rangeButton, 58);

    settingsButton.setBounds (footer.removeFromRight (74));
}

void MantaAnalyzerEditor::applySettings (const AnalyzerSettings& newSettings)
{
    settings = newSettings;
    settings.constrain();

    processor.setSettings (settings);
    view.setSettings (getDisplaySettings());
    refreshFooter();
    rebuildPipelineIfNeeded();
}

void MantaAnalyzerEditor::applyFooterColours()
{
    // **本体のテーマに合わせて塗り直す**（8.330からはグラフの地もテーマに従う）。
    // 作ったときに1回塗るだけだと、開いたままテーマを変えたとき**明るい地に明るい字**になります
    for (auto* button : { &freezeButton, &resetPeakButton, &curveButton, &peakButton, &rangeButton, &settingsButton })
        MantaPluginToolbar::styleButton (*button, button->getButtonText());

    for (auto* slider : { &lowPhonSlider, &highPhonSlider })
    {
        slider->setColour (juce::Slider::trackColourId, MantaTheme::accent().withAlpha (0.35f));
        slider->setColour (juce::Slider::textBoxTextColourId, MantaTheme::text());
        slider->setColour (juce::Slider::textBoxOutlineColourId, MantaTheme::border());
    }

    repaint();
}

AnalyzerSettings MantaAnalyzerEditor::getDisplaySettings() const
{
    // 8.330：レンジを出さないあいだは、**保存されている設定がオンでも描かない**
    // （旗を戻したとき、本人が前に選んでいた状態のまま出るように、設定そのものは書き換えない）
    auto s = settings;
    s.rangeVisible = s.rangeVisible && showsTargetRange;
    return s;
}

void MantaAnalyzerEditor::refreshFooter()
{
    lowPhonSlider.setValue (settings.lowPhon, juce::dontSendNotification);
    highPhonSlider.setValue (settings.highPhon, juce::dontSendNotification);
    curveButton.setToggleState (settings.curveVisible, juce::dontSendNotification);
    peakButton.setToggleState (settings.peakVisible, juce::dontSendNotification);
    rangeButton.setToggleState (settings.rangeVisible, juce::dontSendNotification);

    lowPhonSlider.setEnabled (settings.rangeVisible);
    highPhonSlider.setEnabled (settings.rangeVisible);
}

void MantaAnalyzerEditor::resetPeaks()
{
    peaks.reset (analyzer.getNumBins());
    view.repaint();
}

void MantaAnalyzerEditor::rebuildPipelineIfNeeded()
{
    // 仕様書7.1：**モノのときは解析チャンネルを使わない**（Rへ Lと同じものが入るので、
    // Sideを選ぶと何も出なくなる）
    auto effective = settings;

    if (processor.isMono())
        effective.channel = AnalyzerSettings::Channel::sum;

    const double sampleRate = processor.getCurrentSampleRate();

    if (analyzer.needsPrepare (sampleRate, effective))
    {
        analyzer.prepare (sampleRate, effective);
        peaks.reset (analyzer.getNumBins());
        range.snapOnNextInput();
    }
    else
    {
        analyzer.applySettings (effective);
    }

    range.configure (effective);

    const auto plot = view.getPlotBounds();
    curves.setAxis (settings.frequencyMin, settings.frequencyMax, juce::jmax (0, plot.getWidth()),
                    analyzer.getBinHz(), analyzer.getNumBins());
}

std::unique_ptr<juce::Component> MantaAnalyzerEditor::createSettingsPanel (const AnalyzerSettings& initial, bool monoInput,
                                                                        std::function<void (const AnalyzerSettings&)> changed)
{
    return std::make_unique<SettingsPanel> (initial, monoInput, std::move (changed));
}

void MantaAnalyzerEditor::showSettingsPanel()
{
    auto panel = createSettingsPanel (settings, processor.isMono(),
                                                  [safe = juce::Component::SafePointer<MantaAnalyzerEditor> (this)]
                                                  (const AnalyzerSettings& s)
                                                  {
                                                      if (safe != nullptr)
                                                          safe->applySettings (s);
                                                  });

    juce::CallOutBox::launchAsynchronously (std::move (panel), settingsButton.getScreenBounds(), nullptr);
}

void MantaAnalyzerEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = lastTickMs > 0.0 ? juce::jlimit (0.0, 0.5, (now - lastTickMs) / 1000.0) : 1.0 / 60.0;
    lastTickMs = now;

    // テーマが変わったら色を作り直す（アクセントを毎回見るだけ。設計書6.5）
    if (const auto accent = MantaTheme::accent(); accent != lastAccent || AppColours::getTheme() != lastTheme)
    {
        lastAccent = accent;
        lastTheme = AppColours::getTheme();
        // 8.330：**グラフの地も本体のテーマに合わせる**（本人の指定。ライトは白系、ダークは黒系）
        view.setColours (AnalyzerThemeColors::fromAccent (accent, lastTheme == AppColours::Theme::Dark));
        applyFooterColours();
    }

    rebuildPipelineIfNeeded();

    auto& fifo = processor.getFifo();

    // 止めているあいだは読むだけ読んで捨てる（FIFOをあふれさせない）
    if (frozen)
    {
        fifo.discardAll();
        return;
    }

    const int frames = analyzer.process (fifo);

    if (frames > 0)
        peaks.update (analyzer.getFrameDb(), frames * analyzer.getFrameSeconds(),
                      settings.peakHoldSeconds, settings.peakHoldInfinite);

    // **窓が埋まるまでは位置を決めない**（`SpectrumAnalyzer::isWarmedUp()`）
    if (showsTargetRange && analyzer.isWarmedUp())
        range.update (analyzer.getReferenceBandDb(), analyzer.getLatestReferenceBandDb(), dt);

    //--------------------------------------------------------------------------
    // ピクセルの列へ

    const int width = curves.getWidth();

    curves.build (analyzer.getAveragedDb(), frame.average);
    curves.build (peaks.getPeakDb(), frame.peak);

    if (settings.realtimeVisible)
        curves.build (analyzer.getFrameDb(), frame.realtime);
    else
        frame.realtime.assign ((size_t) width, SpectrumAnalyzer::floorDb);

    frame.lower.resize ((size_t) width);
    frame.upper.resize ((size_t) width);
    frame.outside.resize ((size_t) width);

    for (int x = 0; showsTargetRange && x < width; ++x)
    {
        const double hz = curves.frequencyAtX (x);
        frame.lower[(size_t) x] = range.lowerDb (hz);
        frame.upper[(size_t) x] = range.upperDb (hz);
        frame.outside[(size_t) x] = range.outsideStandard (hz) ? 1 : 0;
    }

    frame.frequencyMin = settings.frequencyMin;
    frame.frequencyMax = settings.frequencyMax;
    frame.hasData = width > 0;

    view.repaint();

    //--------------------------------------------------------------------------
    // 設計書6.1：**描き直しが間に合わなければ30 Hzへ**（直近の平均で見る）

    paintAverageMs = 0.9 * paintAverageMs + 0.1 * view.getLastPaintMilliseconds();

    if (currentRate == 60 && paintAverageMs > 12.0)
    {
        currentRate = 30;
        startTimerHz (currentRate);
    }
    else if (currentRate == 30 && paintAverageMs < 6.0)
    {
        currentRate = 60;
        startTimerHz (currentRate);
    }
}
