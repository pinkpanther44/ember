#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "ProjectModel.h"

//==============================================================================
/**
    仕様書5.1「オートセーブ・バックアップファイル生成」・7章「クラッシュリカバリ」に
    対応する自動保存の管理クラス。

    方針：**ユーザーのプロジェクトファイルを勝手に上書きしない**。
    オートセーブはAppData配下の専用ファイルへ書き出し、元のファイルには触れない。
    「気付かないうちに保存されていて、Undoでは戻せない」という事故を避けるため
    （多くのDAWも、自動保存はリカバリ用の別ファイルとして持つ方式を採っている）。

    クラッシュ検知の考え方は、プラグインのクラッシュ検知（設計書3.5）と同じ「マーカー方式」：
    - 正常終了時にはオートセーブファイルを削除する
    - 次回起動時にファイルが残っていれば、前回は正常終了しなかったと判断して復元を提案する
*/
class AutoSaveManager : private juce::Timer
{
public:
    explicit AutoSaveManager (ProjectModel& projectToUse);
    ~AutoSaveManager() override;

    /** 定期的なオートセーブを開始する。 */
    void start();
    void stop();

    /** 今すぐオートセーブする（未保存の変更が無ければ何もしない）。 */
    void saveNow();

    /** オートセーブファイルを削除する。手動保存に成功したときと、正常終了時に呼ぶ。 */
    void clearAutoSave();

    /** 前回のセッションのオートセーブファイルが残っているか（＝正常終了しなかったか）。 */
    bool hasRecoverableAutoSave() const;

    /** オートセーブから復元する。成功したらtrue。 */
    bool restoreFromAutoSave();

    /** オートセーブ直前に呼ばれる。プラグインの内部状態の取り込み（設計書3.8）など、
        「保存前にやるべきこと」をMainComponentから差し込むために使う。 */
    std::function<void()> onBeforeAutoSave;

    /** オートセーブを実行した直後に呼ばれる（UIへの通知用）。 */
    std::function<void (const juce::File&)> onAutoSaved;

    static juce::File getAutoSaveFile();

private:
    void timerCallback() override;

    // 保存間隔。短すぎると大きなプロジェクトで引っかかりを感じ、長すぎると
    // クラッシュ時に失う作業が増えるため、まずは3分から始める。
    // 環境設定（仕様書6.2）を作る際に、ここを設定項目として公開する想定。
    static constexpr int autoSaveIntervalMilliseconds = 3 * 60 * 1000;

    ProjectModel& project;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoSaveManager)
};
