#pragma once
// ChordModel.h / ChordEngine.h の値を単体で確かめるための、juce の最小スタブ。
//
// **このフォルダのインクルードパスを本体のビルドに足してはいけない。**
// `#include <juce_core/juce_core.h>` を横取りして本物のJUCEを隠すためのもので、
// テスト用の実行ファイルをその場でコンパイルするときにだけ使う（Run-ChordTests.ps1）。
//
// エンジン側が juce_core とSTLだけに依存している限り、これで足りる。
// 足りなくなったら、本物のJUCEと同じ挙動になるようにここへ足すこと。
#include <string>
#include <vector>

namespace juce
{
    template <typename T> T jmin (T a, T b) { return a < b ? a : b; }
    template <typename T> T jmax (T a, T b) { return a > b ? a : b; }
    template <typename T> T jlimit (T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }

    struct String
    {
        std::string s;
        String() = default;
        String (const char* t) : s (t ? t : "") {}
        String (const std::string& t) : s (t) {}
        String& operator+= (const String& o) { s += o.s; return *this; }
        const char* toRawUTF8() const { return s.c_str(); }
        bool operator== (const char* o) const { return s == std::string (o); }
    };

    inline String operator+ (const String& a, const String& b) { return String (a.s + b.s); }
    inline String operator+ (const char* a, const String& b)   { return String (std::string (a) + b.s); }
    inline String operator+ (const String& a, const char* b)   { return String (a.s + std::string (b)); }

    struct StringArray
    {
        std::vector<std::string> items;
        void add (const char* t) { items.push_back (t); }
        bool isEmpty() const { return items.empty(); }
        String joinIntoString (const char* sep) const
        {
            std::string out;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i > 0) out += sep;
                out += items[i];
            }
            return String (out);
        }
    };
}
