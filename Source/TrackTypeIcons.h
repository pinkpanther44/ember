#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppColours.h"
#include "IconAssets.h"
#include "ProjectModel.h"

//==============================================================================
/**
    8.295：**トラックの種類を絵で示す**（Phase 288／改善案1）。

    アレンジのトラックヘッダーと、Consoleのストリップの**両方に同じ絵**を出します。
    置き場所はそれぞれ違いますが、**どの種類がどの絵か**は
    ここ1箇所だけが決めます（片方だけ直す、が起きないように。1.27）。

    | 種類 | 絵 | 元のファイル |
    |---|---|---|
    | MIDI | `track_midi_svg` | MIDIアイコン.svg |
    | Audio | `track_audio_svg` | Audioアイコン.svg |
    | VCA | `track_vca_svg` | VCAアイコン.svg |
    | Send | `track_send_svg` | SendIcon.svg |
    | フォルダ | `browser_folder_svg` | **ブラウザと同じ絵**（本人の指定） |
    | **パラアウト（DrumOut）** | `track_midi_svg` | **MIDIと同じ絵**（本人の指定） |
    | コード | **出しません** | —— |

    ### コードトラックには絵がありません

    本人の判断です——**「基本的に1本しか作らないからアイコンは必要ない」**。
    行の高さが半分なので、並びの中でも取り違えようがありません。

    ### パラアウトは、MIDIと同じ絵

    本人の指定です。**出所がMIDIトラックの音源**（ドラム音源の出力バス）なので、
    同じ絵にすると**どこから来た音か**が並びで読めます。
    **押せるのはMIDIのほうだけ**です（受け皿には音源がありません）。

    ### 色は**アクセント**（8.266の決まりのまま）

    ブラウザのフォルダ／ファイルと同じで、**絵はアクセントの色で塗ります**
    （Manta＝パープル、Ember＝ワインレッド）。絵の中の色を
    `Drawable::replaceColour()`で差し替えるので、**絵は1枚で足ります**。

    **地も枠も描きません**（本人の指定「全体的な見た目を揃えたい」）。
    押せるのはMIDI（音源のGUI）とフォルダ（開閉）だけですが、
    **見た目では区別しません**——押せることは、
    MIDIの絵が**GUIの出ているあいだオレンジに変わる**ことで伝わります
    （`draw()`の説明）。

    ### MIDIは、**GUIが出ているあいだオレンジ**（本人の指定）

    Emberではゴールドになります（`AppColours::orange`＝`accentSecondary`）。
    「いま開いている音源はどれか」が、**窓を探さなくても並びで分かります**。
*/
namespace TrackTypeIcons
{
    /** その種類の絵の名前（`BinaryData`の名前）。無ければ`nullptr`。 */
    inline const char* getResourceFor (TrackType type)
    {
        switch (type)
        {
            case TrackType::Midi:   return "track_midi_svg";
            case TrackType::Audio:  return "track_audio_svg";
            case TrackType::VCA:    return "track_vca_svg";
            case TrackType::Send:   return "track_send_svg";
            case TrackType::Folder: return "browser_folder_svg";   // ブラウザと同じ絵
            case TrackType::DrumOut: return "track_midi_svg";      // MIDIと同じ絵（本人の指定）

            case TrackType::Chord:
            default:                return nullptr;                 // 本人の判断で絵を作らない
        }
    }

    /** そのトラックの絵が**押せるか**。

        **MIDI＝音源のGUIを出す／しまう、フォルダ＝畳む／開く**（本人の指定）。
        他（Audio・VCA・Send・パラアウト）は種類を示しているだけなので、
        押しても何も起きません。

        **見た目では区別しません**（本人の指定。`draw()`の説明）ので、
        ここは**当たり判定を分けるためだけ**に使います。 */
    inline bool isClickable (TrackType type)
    {
        return type == TrackType::Midi || type == TrackType::Folder;
    }

    /** 絵の色。`isActive`はMIDIの音源GUIが出ているとき（本人の指定でオレンジ／ゴールド）。 */
    inline juce::Colour getColourFor (TrackType type, bool isActive)
    {
        juce::ignoreUnused (type);

        return isActive ? AppColours::orange : AppColours::purple;
    }

    //==========================================================================
    /** 絵を1つ描く。**絵は読んで持っておくこと**——行ごと・描き直しごとに
        読むと、スクロールのたびにSVGを解析することになります（`BrowserPanel.h`と同じ話）。

        持っておく側のための入れ物。`draw()`は**色が変わったときだけ**読み直します。 */
    class Cached
    {
    public:
        /** その種類・その色で描く。種類に絵が無ければ**何もしません**。

            8.295：**地も枠も描きません**（Phase 288／本人の指定で2段階）。

            はじめは`drawHeaderChip()`にならって`AppColours::background`で
            地を塗っていました。**白い札に見える**というのが本人の指摘で、
            そのとおりでした——ヘッダーは**トラックの色を薄く敷いてある**ので
            （`headerTintAlpha`）、そこを塗ると**その絵のところだけ色帯が消えます**。
            選択中の行では、選択のパープルまでそこだけ抜けていました。

            次に「押せるものには枠」だけ残しましたが、本人の指定は
            **「全体的な見た目を揃えたい」**——**枠も外しました**。

            > **押せることは、絵そのものが伝えます。** MIDIの絵は
            > **GUIが出ているあいだオレンジに変わる**ので、一度押せば分かります。
            > 枠で「押せそう」を作るより、**押した結果が目に見える**ほうが確かです。
            > 押せない絵（Audio・VCA・Send・パラアウト）は、押しても何も起きません
            > ——**何も起きないことが分かりにくい**のは確かなので、
            > 揃った見た目を採るという本人の判断です。 */
        void draw (juce::Graphics& g, juce::Rectangle<int> bounds,
                    TrackType type, bool isActive, float inset = 1.5f)
        {
            if (bounds.isEmpty())
                return;

            const auto* resource = getResourceFor (type);

            if (resource == nullptr)
                return;

            if (auto* drawable = getOrLoad (resource, getColourFor (type, isActive)))
                drawable->drawWithin (g, bounds.toFloat().reduced (inset),
                                       juce::RectanglePlacement::centred, 1.0f);
        }

    private:
        /** **1枚ずつではなく、組み合わせごとに持ちます。**

            `IconAssets::SvgButton`は「色が変わったら作り直す」1枚持ちですが、
            それはボタン1つに1枚だからです。ここは**行ごとに種類が違う**ので、
            1枚持ちだと**MIDIとオーディオが交互に並んだだけで毎行読み直し**になります
            （スクロールのたびにSVGを解析することになる）。

            組み合わせは**多くて8つ**（絵4種×色2つ）なので、線形に探して足ります。 */
        struct Entry
        {
            const char* resource = nullptr;
            juce::Colour colour;
            std::unique_ptr<juce::Drawable> drawable;
        };

        juce::Drawable* getOrLoad (const char* resource, juce::Colour colour)
        {
            for (auto& entry : entries)
                if (entry.resource == resource && entry.colour == colour)
                    return entry.drawable.get();

            Entry entry;
            entry.resource = resource;
            entry.colour = colour;
            entry.drawable = IconAssets::loadTinted (resource, colour);

            auto* result = entry.drawable.get();
            entries.push_back (std::move (entry));

            return result;
        }

        std::vector<Entry> entries;
    };
}
