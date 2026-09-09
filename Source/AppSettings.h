#pragma once

#include <juce_data_structures/juce_data_structures.h>

//==============================================================================
/**
    設計書2.5：**プロジェクトではなく「アプリ全体」に紐づく設定**の保存先。

    ウィンドウの位置・サイズのような「どのプロジェクトを開いても同じでいてほしい」値は、
    プロジェクトファイル（`.ms1`）へ入れると、プロジェクトを開くたびに
    配置がリセットされてしまう。そのため保存先を分けている。

    保存先は`%APPDATA%\PersonalDAW\settings.xml`（クラッシュ検知やオートセーブと同じ場所）。

    実装は**読むたび・書くたびにファイルを開き直す**単純な方式にしてある。
    扱う値は数個・触るのも1セッションに数回なので速度は問題にならず、
    常駐オブジェクトを持たないぶん、終了時の破棄順序を気にしなくて済む。

    Phase 16で新設。仕様書6.2の環境設定（テーマ／ショートカット／サンドボックス方針）も、
    ここへ載せていく想定。
*/
class AppSettings
{
public:
    /** 保存先フォルダ（`%APPDATA%\PersonalDAW`）。無ければ作る。 */
    static juce::File getDataFolder();

    static juce::String getString (const juce::String& key, const juce::String& defaultValue = {});
    static void setString (const juce::String& key, const juce::String& value);

    static int getInt (const juce::String& key, int defaultValue);
    static void setInt (const juce::String& key, int value);

    /** 小数を持つ設定（Phase 53：コードパッドのストロークのずらし幅など）。

        **intへ丸めて持たないこと。** 刻みが0.01のスライダーを整数で保存すると、
        開き直すたびに値が変わる（保存した瞬間に丸め、読むときにさらに丸める）。 */
    static double getDouble (const juce::String& key, double defaultValue);
    static void setDouble (const juce::String& key, double value);

private:
    static juce::File getSettingsFile();
    static juce::ValueTree load();
    static void save (const juce::ValueTree& settings);
};
