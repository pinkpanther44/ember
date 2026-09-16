#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginSandboxHost.h"

class SandboxedPluginProcessor;

//==============================================================================
/**
    8.262：**別プロセスで動いているプラグインの、本来のGUI**（Phase 269）。

    ```
    ┌ 親（Manta Studio）の窓 ─────────────┐
    │  SandboxedPluginEditor               │
    │   └ HWNDComponent ── SetParent ──────┼──▶ 子プロセスの枠なし窓
    │                                      │      └ プラグイン本来のエディタ
    └──────────────────────────────────────┘
    ```

    **窓を作るのは子、飾るのは親**です。子が枠も題字も無い窓を作り、
    そのハンドル（Windowsなら`HWND`）を親へ渡します。親は`HWNDComponent`で
    それを自分の窓の子にします——**利用者からは1つの窓に見えます**。

    ### 開くのは非同期です

    **頼んで、先に画面を出し、届いたらはめ込みます。** 待ってはいけません——
    理由は`PluginSandboxHost::requestEditor()`に書いてあります
    （待つ形にしたら、2度目に開くときだけ5秒固まりました）。

    そのぶん、**窓は小さく出てから本来の大きさへ変わります**。
    2度目からは前回の大きさを覚えているので、ほとんど動きません。

    ### 4.3の「保留」を解いたときに効いたこと

    Phase 5b-3では3度失敗しました（ヒープ不整合・子の自主終了）。今回効いたのは：

    | | |
    |---|---|
    | **窓はメッセージスレッドで作る** | 制御チャンネルの返事は専用スレッドで届きます。そこで窓を作っていたのが、いちばん確からしい原因（`SandboxWorker.cpp`） |
    | **子の自主終了はpingだった** | 8.260で判明済み。落ちたのではなく、忙しくてpingに答えられていませんでした |
    | **親が待たない** | `SetParent`で入力キューが繋がるので、親が待つと子の`CreateWindowEx`が止まります |
    | **エディタの寿命を1か所に寄せた** | 窓は`setContentNonOwned`。**消す順番は`closeEditorWindow()`だけが決めます** |

    ### はめ込めないとき

    **Windows以外では、子が自分の窓として出します**（`HWNDComponent`はWindows専用。
    Linuxの`XEmbedComponent`はXEMBEDの作法を子側にも要求するので、
    ここでは踏み込んでいません）。そのときこの画面は**パラメータ一覧**になり、
    プラグイン本来のGUIは別窓として並びます。

    ### 子が落ちたら

    はめ込んだ窓ごと消えます。ここは**そのことを書いた面**に変わります——
    空白のまま残すと「固まった」と見分けがつきません。
*/
class SandboxedPluginEditor final : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    SandboxedPluginEditor (SandboxedPluginProcessor& owner, PluginSandboxHost& hostToUse);
    ~SandboxedPluginEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 子が落ちた（`SandboxedPluginProcessor`が呼びます）。 */
    void sandboxCrashed();

private:
    void editorOpened (bool success, const PluginSandboxHost::EditorInfo& info);
    void showParameterList (const juce::String& explanation);
    void timerCallback() override;   ///< 返事が来ないまま時間切れ

    /** 前に開いたときの大きさ（**次に開くときの初手**。`AppSettings`に残します）。 */
    juce::String rememberedSizeKey() const;
    void rememberSize (int width, int height);

    //==========================================================================
    SandboxedPluginProcessor& sandboxProcessor;
    PluginSandboxHost& host;

    /** 子の窓をはめ込んだところ。**はめ込めたときだけ**中身が入ります。 */
   #if JUCE_WINDOWS
    std::unique_ptr<juce::HWNDComponent> embeddedView;
   #endif

    /** はめ込めないとき（と落ちたあと）に出すもの。 */
    std::unique_ptr<juce::Component> fallbackView;

    juce::Label message;

    bool crashed = false;
    bool settled = false;   ///< 返事が届いた（成功・失敗どちらでも）

    /** 子から「大きさが変わった」と言われて直している最中か（**呼び戻しよけ**）。 */
    bool resizingFromWorker = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SandboxedPluginEditor)
};
