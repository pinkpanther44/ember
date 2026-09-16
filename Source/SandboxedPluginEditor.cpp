#include "SandboxedPluginEditor.h"

#include "AppColours.h"
#include "AppSettings.h"
#include "SandboxedPluginProcessor.h"
#include "Utf8.h"

namespace
{
    /** 返事が来ないまま諦めるまで（ミリ秒）。

        **ロードより長く待つ意味はありません。** ここまで無言なら、
        子のメッセージスレッドが詰まっているということです。 */
    constexpr int giveUpAfterMs = 8000;

    /** まだ本来の大きさを知らないときの、仮の大きさ。 */
    constexpr int provisionalWidth = 560;
    constexpr int provisionalHeight = 360;
}

//==============================================================================

SandboxedPluginEditor::SandboxedPluginEditor (SandboxedPluginProcessor& owner,
                                               PluginSandboxHost& hostToUse)
    : juce::AudioProcessorEditor (owner), sandboxProcessor (owner), host (hostToUse)
{
    message.setJustificationType (juce::Justification::centred);
    message.setColour (juce::Label::textColourId, AppColours::textSecondary);
    message.setText (utf8 ("プラグインの画面を開いています…"), juce::dontSendNotification);
    addAndMakeVisible (message);

    // **前に開いたときの大きさから始めます。** 知らなければ仮の大きさで出し、
    // 届いたら合わせます（一度だけ跳ねます）
    setSize (AppSettings::getInt (rememberedSizeKey() + "W", provisionalWidth),
              AppSettings::getInt (rememberedSizeKey() + "H", provisionalHeight));

    host.onEditorOpened = [this] (bool success, const PluginSandboxHost::EditorInfo& info)
    {
        editorOpened (success, info);
    };

    host.onEditorResized = [this] (int width, int height)
    {
        if (width <= 0 || height <= 0 || crashed)
            return;

        const juce::ScopedValueSetter<bool> guard (resizingFromWorker, true);

        rememberSize (width, height);
        setSize (width, height);
    };

    // 別窓の「×」が押された（はめ込めなかったときだけ起こります）
    host.onEditorClosed = [this]
    {
        if (! crashed)
            message.setText (utf8 ("このプラグインは別プロセスで動いています"),
                              juce::dontSendNotification);
    };

    // **はめ込めるのはWindowsだけ**（クラスの説明）。
    // ほかの環境では、子が自分の窓として出します
   #if JUCE_WINDOWS
    constexpr bool wantEmbedding = true;
   #else
    constexpr bool wantEmbedding = false;
   #endif

    if (host.requestEditor (wantEmbedding))
        startTimer (giveUpAfterMs);
    else
        showParameterList (utf8 ("このプラグインは別プロセスで動いています。本来の画面は開けませんでした"));
}

SandboxedPluginEditor::~SandboxedPluginEditor()
{
    sandboxProcessor.setActiveSandboxEditor (nullptr, false);

    host.onEditorOpened = nullptr;
    host.onEditorResized = nullptr;
    host.onEditorClosed = nullptr;

    // **先にこちらの手を離すこと。** `HWNDComponent`は片づけのときに
    // `DestroyWindow()`を呼びますが、**他プロセスの窓は壊せません**（何も起きません）。
    // 窓を閉じるのは子の仕事なので、そちらへ頼みます
   #if JUCE_WINDOWS
    if (embeddedView != nullptr)
        embeddedView->setHWND (nullptr);
   #endif

    if (! crashed)
        host.closeEditor();
}

//==============================================================================

void SandboxedPluginEditor::editorOpened (bool success, const PluginSandboxHost::EditorInfo& info)
{
    stopTimer();
    settled = true;

    if (crashed)
        return;

   #if JUCE_WINDOWS
    if (success && info.nativeHandle != nullptr)
    {
        message.setVisible (false);

        embeddedView = std::make_unique<juce::HWNDComponent>();
        addAndMakeVisible (*embeddedView);

        // `HWNDComponent`は自分の`ComponentPeer`が付くのを待ってから`SetParent`します。
        // **この時点で窓に載っていなくても構いません**
        embeddedView->setHWND (info.nativeHandle);

        setResizable (info.resizable, false);

        {
            const juce::ScopedValueSetter<bool> guard (resizingFromWorker, true);
            setSize (juce::jmax (120, info.width), juce::jmax (80, info.height));
        }

        rememberSize (info.width, info.height);
        sandboxProcessor.setActiveSandboxEditor (this, true);

        resized();
        return;
    }
   #endif

    juce::ignoreUnused (info);

    showParameterList (success
        ? utf8 ("このプラグインは別プロセスで動いています。本来の画面は別の窓で開いています")
        : utf8 ("このプラグインは別プロセスで動いています。本来の画面は開けませんでした"));
}

void SandboxedPluginEditor::showParameterList (const juce::String& explanation)
{
    stopTimer();
    settled = true;

    fallbackView = std::make_unique<juce::GenericAudioProcessorEditor> (sandboxProcessor);
    addAndMakeVisible (*fallbackView);

    message.setText (explanation, juce::dontSendNotification);
    message.setVisible (true);

    sandboxProcessor.setActiveSandboxEditor (this, false);

    setSize (460, 520);
    resized();
}

void SandboxedPluginEditor::timerCallback()
{
    stopTimer();

    if (settled || crashed)
        return;

    // **無言のまま時間切れ**。子のメッセージスレッドが詰まっています——
    // 空白で待たせ続けるより、触れるものを出します
    showParameterList (utf8 ("このプラグインは別プロセスで動いています。本来の画面は開けませんでした"));
}

//==============================================================================

juce::String SandboxedPluginEditor::rememberedSizeKey() const
{
    juce::PluginDescription description;
    sandboxProcessor.fillInPluginDescription (description);

    return "sandboxEditor_" + juce::String::toHexString ((juce::int64) description.uniqueId) + "_";
}

void SandboxedPluginEditor::rememberSize (int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    AppSettings::setInt (rememberedSizeKey() + "W", width);
    AppSettings::setInt (rememberedSizeKey() + "H", height);
}

//==============================================================================

void SandboxedPluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);
}

void SandboxedPluginEditor::resized()
{
    auto area = getLocalBounds();

    if (crashed)
    {
        message.setBounds (area);
        return;
    }

   #if JUCE_WINDOWS
    if (embeddedView != nullptr)
    {
        embeddedView->setBounds (area);

        // 引き伸ばされたぶんを子へ伝えます（**自分が言われて直した直後は送らない**。
        // 送り返すと、丸め誤差のぶんだけ大きさが行ったり来たりします）
        if (! resizingFromWorker)
        {
            rememberSize (area.getWidth(), area.getHeight());
            host.setEditorSize (area.getWidth(), area.getHeight());
        }

        return;
    }
   #endif

    if (message.isVisible())
        message.setBounds (area.removeFromTop (44).reduced (10, 4));

    if (fallbackView != nullptr)
        fallbackView->setBounds (area);
}

//==============================================================================

void SandboxedPluginEditor::sandboxCrashed()
{
    if (crashed)
        return;

    crashed = true;
    stopTimer();

   #if JUCE_WINDOWS
    if (embeddedView != nullptr)
    {
        // **はめ込んだ窓はもう無い**（子と一緒に消えました）
        embeddedView->setHWND (nullptr);
        removeChildComponent (embeddedView.get());
        embeddedView = nullptr;
    }
   #endif

    fallbackView = nullptr;

    // **はめ込んでいたことを取り消します**（窓はもうありません）
    sandboxProcessor.setActiveSandboxEditor (this, false);

    message.setText (utf8 ("このプラグインは落ちました。音は素通しになっています"),
                      juce::dontSendNotification);
    message.setColour (juce::Label::textColourId, AppColours::orange);
    message.setVisible (true);

    // **空白のまま残さないこと。** 何も出ていないと「固まった」と見分けがつきません
    setSize (juce::jmax (getWidth(), 360), juce::jmax (getHeight(), 120));
    resized();
}
