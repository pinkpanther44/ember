#include "Localise.h"
#include "AppSettings.h"
#include "Branding.h"   // 8.175：既定の言語（Phase 216）

#include <string_view>
#include <unordered_map>

namespace Localise
{
    namespace
    {
        const juce::String languageKey { "language" };

        // **起動時に1度だけ決める。** `utf8()`は`paint()`からも呼ばれるので、
        // ここで毎回`AppSettings`（ファイルに載った値）を引くわけにはいかない
        Mode currentMode = Mode::japanese;

        /** 見出し → 行。**中身はリテラルを指すだけ**なので、表そのものは複写しない。

            `std::string_view`を見出しにしてあるのが要点です：
            `utf8()`がもらうのは`const char*`なので、**引くために`juce::String`を
            作る必要がありません**（1文字ずつの走査も、確保も走らない）。 */
        using Lookup = std::unordered_map<std::string_view, const Entry*>;

        const Lookup& getLookup()
        {
            // **初回に1度だけ組む。** 関数内のstaticなので、
            // 起動の順番（グローバルの初期化順）に依存しない
            static const Lookup lookup = []
            {
                int numEntries = 0;
                const auto* table = getTable (numEntries);

                Lookup map;
                map.reserve ((size_t) numEntries * 2);

                for (int i = 0; i < numEntries; ++i)
                    map.emplace (std::string_view (table[i].japanese), &table[i]);

                return map;
            }();

            return lookup;
        }
    }

    Mode getMode()
    {
        return currentMode;
    }

    void setMode (Mode newMode)
    {
        currentMode = newMode;
        AppSettings::setInt (languageKey, (int) newMode);
    }

    void loadFromSettings()
    {
        // **範囲の外は日本語に寄せる。** 設定ファイルは手で書き換えられるので、
        // 知らない数字が入っていても画面が空にならないようにしておく
        // 8.175：**既定はブランドで決まります**（Phase 216）。
        // Emberは英語、Manta Studioは日本語。どちらも設定で変えられます
        const auto fallback = Branding::defaultsToEnglish ? Mode::english : Mode::japanese;

        const int stored = AppSettings::getInt (languageKey, (int) fallback);

        currentMode = (stored == (int) Mode::english)                 ? Mode::english
                    : (stored == (int) Mode::englishWithJapaneseHelp) ? Mode::englishWithJapaneseHelp
                                                                      : Mode::japanese;
    }

    juce::String getModeName (Mode mode)
    {
        // **どの言語で見ていても、選べる言語の名前はその言語で出す。**
        // 英語の画面で「日本語」が英語（Japanese）になっていると、
        // 日本語へ戻したい人が自分の言語を探せない
        switch (mode)
        {
            case Mode::english:                 return "English";
            case Mode::englishWithJapaneseHelp: return juce::String (juce::CharPointer_UTF8 (
                                                           "English + \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"
                                                           "\xe3\x81\xae\xe8\xaa\xac\xe6\x98\x8e"));
            case Mode::japanese:
            default:                            return juce::String (juce::CharPointer_UTF8 (
                                                           "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
        }
    }

    juce::String translate (const char* japaneseUtf8)
    {
        // **日本語のときは何も引かない。** 既定の設定では、
        // Phase 200までとまったく同じ経路・同じ速さになる
        if (currentMode == Mode::japanese || japaneseUtf8 == nullptr)
            return juce::String (juce::CharPointer_UTF8 (japaneseUtf8));

        const auto& lookup = getLookup();
        const auto found = lookup.find (std::string_view (japaneseUtf8));

        // **訳が無ければ日本語のまま。** 表に載せ忘れても、
        // 画面が空になったり見出しの文字列が出たりはしない
        if (found == lookup.end())
            return juce::String (juce::CharPointer_UTF8 (japaneseUtf8));

        const auto* entry = found->second;

        // 3つめの言語：**説明は日本語のまま**（本人の指定）
        if (currentMode == Mode::englishWithJapaneseHelp && entry->isHelp)
            return juce::String (juce::CharPointer_UTF8 (japaneseUtf8));

        // **訳が空の行も「訳なし」と同じ扱い。**
        // `Tools/Update-Translations.ps1 -Write`は、新しく見つかった文字列を
        // **英語を空のまま**足します。埋める前にビルドしても、
        // そこだけ空白になる（何のボタンか分からない）ことはありません
        if (entry->english == nullptr || *entry->english == '\0')
            return juce::String (juce::CharPointer_UTF8 (japaneseUtf8));

        // 英語はASCIIなので、`CharPointer_UTF8`を通す必要はない。
        // **それでも通しておく**——訳に「°」「–」などが混じったときに
        // ここだけ壊れる、という状態を作らないため（1.30）
        return juce::String (juce::CharPointer_UTF8 (entry->english));
    }
}
