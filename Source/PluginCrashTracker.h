#pragma once

#include <juce_data_structures/juce_data_structures.h>

//==============================================================================
/**
    設計書3.5「クラッシュ検知とcrashCountの更新」の実装。

    ネイティブロード（同一プロセス）中のプラグインのクラッシュは、発生した時点で
    安全に検知・復旧するのが本質的に難しい（C++では、破損したメモリ状態から
    安全に処理を継続する保証がない）。そのため、以下の「実行前マーカー」方式を採る：

    1. プラグインへの危険な呼び出しの直前に、ディスク上のマーカーファイルへ
       「このプラグインを処理中」と書き込む
    2. 呼び出しが正常に完了したら、マーカーを削除する
    3. 次回アプリ起動時、マーカーが残っている（＝前回正常終了しなかった）
       プラグインを検出し、そのプラグインのcrashCountを+1する
    4. crashCount >= 2（仕様書9章のしきい値）になったプラグインは
       「サンドボックスすべき」と判定される

    Phase 5aではこの判定までを実装する。判定結果を使った実際のプロセス分離は
    Phase 5b（設計書3.4のIPC実装）で対応する。

    データはユーザーのアプリケーションデータフォルダに保存される：
      - crash_marker.txt   ： 処理中プラグインの識別子（正常終了時は削除される）
      - plugin_crashes.xml ： プラグインごとのcrashCount
*/
class PluginCrashTracker
{
public:
    PluginCrashTracker();

    /** アプリ起動時に1度だけ呼ぶ。前回のマーカーが残っていれば、そのプラグインの
        crashCountを加算し、そのプラグイン識別子を返す（無ければ空文字）。 */
    juce::String checkForPreviousCrash();

    /** プラグインのロード直前に呼ぶ。マーカーを書き込む。 */
    void beginPluginOperation (const juce::String& pluginIdentifier);

    /** プラグインのロードが正常に完了したら呼ぶ。マーカーを削除する。 */
    void endPluginOperation();

    int getCrashCount (const juce::String& pluginIdentifier) const;

    /** 仕様書9章のしきい値（連続2回）に達しているか。 */
    bool shouldSandbox (const juce::String& pluginIdentifier) const;

    /** 特定プラグインのクラッシュ履歴をリセットする（ユーザーが再挑戦したい場合用）。 */
    void resetCrashCount (const juce::String& pluginIdentifier);

    static constexpr int sandboxThreshold = 2;

private:
    juce::File getDataFolder() const;
    juce::File getMarkerFile() const;
    juce::File getCrashCountsFile() const;

    void loadCrashCounts();
    void saveCrashCounts();

    /** ValueTreeのプロパティ名に使えるよう、識別子を安全な文字列へ変換する。 */
    static juce::Identifier makeSafeKey (const juce::String& pluginIdentifier);

    juce::ValueTree crashCounts { "PLUGINCRASHES" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCrashTracker)
};
