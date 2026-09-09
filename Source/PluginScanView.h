#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AudioEngine.h"
#include "PluginSandboxHost.h"

//==============================================================================
/**
    仕様書5.8「プラグイン管理」のスキャン画面。

    **Phase 26で「スキャンと一覧」だけに絞った。** それ以前はここから
    テストトーンの経路へプラグインを挿して試聴できたが、Phase 12c-2（インサート）と
    Phase 21（ドラッグ&ドロップ）でミキサー側から挿せるようになり、
    試聴専用の経路は二重の入り口になっていた（HANDOVER 6.3の「二重化」）。

    現在の役割は次の3つだけ：
    - VST3のスキャンと、追加フォルダの指定
    - 見つかったプラグインの一覧表示（クラッシュ履歴つき。設計書3.5）
    - クラッシュ履歴のリセット

    （将来）仕様書6.2の環境設定画面ができたら、この内容はそちらへ移すのが筋。
*/
class PluginScanView : public juce::Component
{
public:
    explicit PluginScanView (AudioEngine& engineToUse);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 仕様書5.1・設計書3.8：プロジェクト読み込み後に、挿入中プラグインの表示を更新する。 */
    void refreshAfterProjectChanged();

    /** スキャンが終わったときに呼ばれる。同じ一覧を出しているブラウザパネル（Phase 17）へ
        知らせるために使う。 */
    std::function<void()> onPluginsScanned;

private:
    void scanClicked();
    void chooseFolderClicked();
    void resetCrashClicked();
    void updateStatus();

    AudioEngine& engine;

    juce::TextButton scanButton        { "Scan Default VST3 Folders" };
    juce::TextButton chooseFolderButton{ "Add Folder..." };
    juce::TextButton resetCrashButton  { "Reset Crash History" };

    /** 8.157：ブラックリストを空にする（Phase 195/E2）。

        スキャン中に落ちたプラグインは**次から飛ばします**が、
        **戻す道が無いと二度と出てきません**。原因が直った（更新した）ときに
        もう一度試せるよう、ここから空にできるようにしてあります。 */
    juce::TextButton clearBlacklistButton { "Retry Skipped Plugins" };
    juce::ListBox pluginListBox;
    juce::Label statusLabel;

    /** 8.52：スキャンを走らせるスレッド（Phase 91）。

        **`juce::Thread::launch`ではなくここに置く**：投げっぱなしだと、
        スキャン中に画面を閉じたときに壊れたあとのものを触りに来ます
        （ヒットポイントの検出と同じ形。8.45）。 */
    juce::ThreadPool scanPool { 1 };

    /** スキャン中はボタンを止める（二重に走らせない）。 */
    bool isScanning = false;

    /** スキャンが終わったときに呼ばれる（メッセージスレッド）。 */
    void finishScan (const juce::Array<juce::PluginDescription>& found);

    juce::Array<juce::PluginDescription> foundPlugins;
    juce::FileSearchPath extraFolders;
    std::unique_ptr<juce::FileChooser> fileChooser;

    //==========================================================================
    class PluginListModel : public juce::ListBoxModel
    {
    public:
        explicit PluginListModel (PluginScanView& ownerIn) : owner (ownerIn) {}

        int getNumRows() override { return owner.foundPlugins.size(); }

        void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    private:
        PluginScanView& owner;
    };

    PluginListModel listModel { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginScanView)
};
