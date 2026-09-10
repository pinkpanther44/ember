#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    8.175：**同じコードから2つのアプリを出すための、たった1つの表**（Phase 216）。

    ```
                    ┌──────────────┐
    Source/  ──────▶│  Branding.h  │──┬──▶ Manta Studio（自分用。ASIOあり・日本語）
                    └──────────────┘  └──▶ Ember       （配布用。ASIOなし・英語）
    ```

    ### なぜコードを2本に分けないのか

    本人の方針は「**機能面は同じ、別々のアプリ**」です。
    リポジトリを分けると、**Manta Studioの改善をEmberへ写す作業が毎回発生**し、
    写し忘れたぶんだけ2つが離れていきます（8.2と同じ話）。

    1本のコードにして、**違うところだけをこのファイルへ集めて**あります。
    Manta側の改善は、Emberをビルドし直すだけで乗ります。

    ### 切り替え方

    ```
    cmake -S . -B out        -DMANTA_BRAND=manta   （既定）
    cmake -S . -B out-ember  -DMANTA_BRAND=ember
    ```

    **ビルドフォルダを分けること。** 同じ`out/`へ両方作ると、
    成果物が上書きし合って「どちらを起動したか分からない」になります（1.1）。

    ### 分けなければいけないもの（**オプションではありません**）

    | | なぜ |
    |---|---|
    | `%APPDATA%`のフォルダ名 | 同じ`plugin_list.xml`を2つのexeが共有すると、**Ember版が書いた「Ember EQ」をManta Studioが作れず**、一覧には出るのに挿せなくなります（8.167でまさにこれを踏みました） |
    | プロジェクトの拡張子 | 内蔵プラグインの識別子が違うので、**互いのプロジェクトを開くとプラグインを見失います。** 拡張子が同じだと、それに気づかないまま開いてしまいます |

    ### 内蔵プラグインの名前を変えると識別子も変わる

    `PluginDescription::createIdentifierString()`には`name`が混ざります（8.166）。
    つまり**Manta EQ → Ember EQ にした時点で別のプラグイン**です。

    - Ember利用者にとっては、最初からそれしか無いので**問題になりません**
    - **本人の曲（Manta Studioで作ったもの）はEmberでは開けません。** 意図どおりです
*/
namespace Branding
{
   #if MANTA_BRAND_EMBER
    inline constexpr bool isEmber = true;
   #else
    inline constexpr bool isEmber = false;
   #endif

    //==========================================================================
    // 表に出る名前

    /** ウィンドウのタイトルとexeの名前。CMakeの`PRODUCT_NAME`と**同じ文字列**であること。 */
    inline constexpr const char* productName = isEmber ? "Ember" : "Manta Studio";

    //==========================================================================
    // 保存先

    /** `%APPDATA%\<これ>`。設定・オートセーブ・プラグイン一覧の置き場所。

        **ブランドごとに必ず変えること**（このファイルの冒頭の表）。 */
    inline constexpr const char* dataFolderName = isEmber ? "Ember" : "PersonalDAW";

    /** `ドキュメント\<これ>`。録音したWAVの既定の置き場所。 */
    inline constexpr const char* recordingsFolderName = isEmber ? "Ember Recordings"
                                                                 : "PersonalDAW Recordings";

    /** `ドキュメント<これ>`。プロジェクトテンプレートの既定の置き場所。 */
    inline constexpr const char* templatesFolderName = isEmber ? "Ember Templates"
                                                                : "PersonalDAW Templates";

    //==========================================================================
    // プロジェクトファイル

    inline constexpr const char* projectExtension = isEmber ? ".em1" : ".ms1";

    /** ファイル選択ダイアログのフィルタ。

        **Manta Studioだけ古い拡張子も出します**（Phase 56より前に保存したもの）。
        Emberには過去がないので、増やす理由がありません。 */
    inline constexpr const char* projectWildcard = isEmber ? "*.em1" : "*.ms1;*.pdawproj";

    //==========================================================================
    // 内蔵プラグイン（**名前は識別子の一部**。8.166）

    inline constexpr const char* pluginFormatName = isEmber ? "Ember" : "Manta";
    inline constexpr const char* pluginManufacturer = isEmber ? "Ember" : "Manta Studio";

    inline constexpr const char* eqPluginName = isEmber ? "Ember EQ" : "Manta EQ";
    inline constexpr const char* compPluginName = isEmber ? "Ember Comp" : "Manta Comp";

    /** シンセだけ名前が揃っていないのは**本人の指定**です（`Red Panda`）。 */
    inline constexpr const char* synthPluginName = isEmber ? "Red Panda" : "Manta Synthesizer";

    /** プリセットの置き場所（`%APPDATA%\<data>\Presets\<これ>\`）。

        **プラグイン名とは別に持ちます。** 名前を変えたときに
        保存済みのプリセットが行方不明にならないよう、フォルダ名は据え置きです。 */
    inline constexpr const char* eqPresetFolder = "MantaEQ";
    inline constexpr const char* compPresetFolder = "MantaComp";
    inline constexpr const char* synthPresetFolder = "MantaSynth";

    //==========================================================================
    // 既定値

    //==========================================================================
    // 8.188：**バージョン情報に出すもの**（Phase 226）。
    //
    // **ここに集めること。** 名前を直に書いて「Emberでだけ間違っていた」のを
    // 3件やっています（8.186）。

    /** 配っているかどうか。**Manta Studioは本人専用**で公開していません。

        偽のときは、バージョン情報からライセンスとソースの節を丸ごと外します——
        **配っていないものに配布の条件を書いても、読む人を混乱させるだけ**です。 */
    inline constexpr bool isPublished = isEmber;

    /** 著作権者。`Packaging/Ember/README.md`と`LICENSE`の表記と**揃えること**。 */
    inline constexpr const char* copyrightHolder = "pinkpanther44";

    inline constexpr const char* copyrightYear = "2026";

    /** **AGPLv3が求める「ソースの入手先」**（8.182）。空なら出しません。 */
    inline constexpr const char* sourceUrl = isPublished
                                                ? "https://github.com/pinkpanther44/ember"
                                                : "";

    inline constexpr const char* licenceName = "GNU Affero General Public License, version 3 or later";

    /** 8.186：**ASIOが使えるかどうか**（Phase 225）。

        Ember版にASIOは入っていません（8.174／本人の判断）。ASIO SDKは
        **再配布できず**、プロプライエタリなので**AGPLv3のものへリンクできない**ためです。

        CMake側でも`if (WIN32 AND NOT MANTA_BRAND STREQUAL "ember")`で外していますが、
        **画面に出す文章はそれを知りません**。「ASIOを選べる環境では〜」という案内が
        Ember版でも出ていたので、ここで判断できるようにしました。 */
    inline constexpr bool hasAsioSupport = ! isEmber;

    /** 8.186：**1本目のトラックの色**（Phase 225／本人の指定）。

        `Track::getColourPalette()`の2段目（既定色を配る段）から数えた、
        **配り始めの位置**です。0ならパープル、5ならレッド。

        段の並びは グレー／パープル／ブルー／グリーン／イエロー／オレンジ／レッド／ピンク で、
        グレーは飛ばすので、**パープルが0番**になります。

        Emberはワインレッドが基調なので、**1本目を赤系から始めます**。
        2本目以降はそこから順に回るだけなので、**色の総数も並びも変わりません**。 */
    inline constexpr int defaultTrackColourOffset = isEmber ? 5 : 0;

    /** 8.177：**レトロな見せ方**（Phase 218／本人の要望）。

        いまのところ**7セグメントの数字表示**です（`SegmentDisplay.h`）。
        ドットフォントは**保留**——多言語に対応しているものが少ないため、
        本人の判断で見送っています。**数字だけならフォントは要りません。** */
    inline constexpr bool retroUI = isEmber;

    /** Emberの既定の言語は**英語**（本人の指定）。

        訳の表は919行すべて英訳済みなので、既定値を変えるだけで足ります。
        **設定で日本語にもできます**——固定にはしていません。 */
    inline constexpr bool defaultsToEnglish = isEmber;

    //==========================================================================
    // アクセント色
    //
    // 本人の指定は「**ワインレッド・レッド・ゴールド**、プラスでホワイトグレー・ブラック」。
    //
    // `AppColours`のアクセント枠は2つ（`purple`＝選ばれているもの／
    // `orange`＝いま起きていること・注意）なので、そこへ割り当てています。
    //
    // | 枠 | Manta Studio | Ember | その枠の役割 |
    // |---|---|---|---|
    // | `purple` | パープル | **ワインレッド** | 選択中・アクティブ |
    // | `orange` | オレンジ | **ゴールド** | 録音・警告・いま効いているもの |
    //
    // **レッドは既に画面に出ています**——メーターの頭2段が`0xffe5484d`で、
    // 「もう上げられない」を示しています（`SynthLevelMeter`・`SegmentMeter`）。
    // 3色ともそろっているので、ここで新しい枠を作っていません。
    //
    // ホワイトグレーとブラックは、**ダークテーマの地と文字がすでにそれ**です。

    /** 選択中・アクティブの色。`dark`はダークテーマかどうか。 */
    inline constexpr juce::uint32 accentPrimary (bool dark)
    {
        if (isEmber)
            return dark ? 0xffb03a52 : 0xff8e2b3f;   // ワインレッド

        return dark ? 0xff8f74ff : 0xff7c5cff;       // パープル（Phase 34からの値）
    }

    /** 録音・警告・いま効いているものの色。 */
    inline constexpr juce::uint32 accentSecondary (bool dark)
    {
        if (isEmber)
            return dark ? 0xffdbb43f : 0xffc8a02e;   // ゴールド

        return dark ? 0xffff9a55 : 0xffff8a3d;       // オレンジ（Phase 34からの値）
    }
}
