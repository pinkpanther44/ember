cmake_minimum_required(VERSION 3.22)

project(PersonalDAW VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# 8.176：**Visual C++ ランタイムを実行ファイルへ入れる**（Phase 217／配布のため）。
#
# 既定（/MD）だと、配布先のPCに「Microsoft Visual C++ 再頒布可能パッケージ」が
# 入っていないと **`VCRUNTIME140.dll` が見つかりません** と出て起動しません。
# 静的リンク（/MT）にすると実行ファイル側に入るので、そのまま動きます。
#
# **本人のEAGLE type0で実際に踏んで、同じ手で直した問題**です。
#
# **Manta Studio側にも掛けています。** ここだけEmberと違えると、
# /MT でしか出ない問題を**配布版で初めて踏む**ことになります
# （普段動かして確かめているのはManta Studioのほうなので）。
#
# **ターゲットを作るより前に書くこと**（FetchContentでJUCEを取り込む前）。
# `cmake_minimum_required`が3.22なのでCMP0091=NEWとなり、この変数が効きます。
# ---------------------------------------------------------------------------
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# 8.175：**同じコードから2つのアプリを出す**（Phase 216／本人の要望）。
#
#   cmake -S . -B out       -DMANTA_BRAND=manta   （既定。自分用）
#   cmake -S . -B out-ember -DMANTA_BRAND=ember   （配布用）
#
# **ビルドフォルダを分けること。** 同じ out/ へ両方作ると、成果物が上書きし合って
# 「いまどちらを起動したのか分からない」になります（1.1）。
#
# 違うところは全部 `Source/Branding.h` に集めてあります。ここでやるのは
# **コンパイル時にどちらか教えること**と、**exeの名前・アイコン**だけです。
# ---------------------------------------------------------------------------
set(MANTA_BRAND "manta" CACHE STRING "Which product to build: manta or ember")
set_property(CACHE MANTA_BRAND PROPERTY STRINGS manta ember)

if (MANTA_BRAND STREQUAL "ember")
    set(MANTA_PRODUCT_NAME "Ember")
    set(MANTA_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icons/ember_icon.png")
    message(STATUS "Brand: Ember (distribution build - no ASIO, English by default)")
else()
    set(MANTA_PRODUCT_NAME "Manta Studio")
    set(MANTA_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icons/app_icon.png")
    message(STATUS "Brand: Manta Studio")
endif()

# ---------------------------------------------------------------------------
# JUCEをFetchContentで取得する。
# 初回のcmake実行時にGitHubからJUCE本体をダウンロード・ビルドするため、
# インターネット接続が必要で、多少時間がかかります。
# GIT_TAGは最新の安定版に合わせて更新してください（リリース一覧：
# https://github.com/juce-framework/JUCE/releases ）。
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# アプリケーション定義
# ---------------------------------------------------------------------------
# PRODUCT_NAME が exe 名とウィンドウタイトルになります（Phase 56で改名。HANDOVER 8.1のB2）。
#
# **ターゲット名（PersonalDAW）はそのままにしてあります。** これを変えると
#   - ビルド成果物の場所が out/PersonalDAW_artefacts/ から変わり、HANDOVER 1.1の
#     「ビルドフォルダを取り違えない」という手順書が全部ずれる
#   - 設定・オートセーブ・録音の保存先（%APPDATA%\PersonalDAW 等）は据え置きにしたので、
#     内部の識別子だけ変えると、かえって対応が分かりにくくなる
# ためです。表に出る名前と内部の名前が違うのは意図的です。
juce_add_gui_app(PersonalDAW
    PRODUCT_NAME "${MANTA_PRODUCT_NAME}"
    COMPANY_NAME "YourName"          # 好きな名前に変更してください
    VERSION ${PROJECT_VERSION}

    # 8.133：アプリのアイコン（Phase 169／改善案リスト3の44）。
    # **exeとウィンドウのアイコンになります。** JUCEが各サイズへ縮小するので、
    # 元は大きい1枚で足ります。
    #
    # 8.164：**本人が作り直した絵に差し替え**（Phase 202）。
    # 2000x2000でもらったものを**1024x1024**へ縮めてあります——
    # Windowsのexeアイコンは最大256px、起動画面でも大きくて300px程度なので、
    # 1920のままだと**使われない画素を毎回exeへ埋め込む**ことになります
    # （`juce_add_binary_data`で絵としても埋めているため、そのぶん実行ファイルが太る）。
    ICON_BIG "${MANTA_APP_ICON}"
)

# ---------------------------------------------------------------------------
# 8.165：**絵を差し替えたら、ビルドし直すだけで反映させる**（Phase 203／本人の報告）。
#
# JUCEはexe用の`icon.ico`を**構成（configure）のときに1度だけ**作ります
# （`_juce_execute_juceaide(winicon ...)`は`execute_process`——ビルド規則ではない）。
#
# そのため`app_icon.png`を置き換えて`cmake --build`しただけでは、
# **exeのアイコンは古いまま**でした。`cmake -S . -B out`を打ち直すまで変わらない、
# という**気づきようのない**状態です——ビルドは成功し、アプリの中の絵
# （`juce_add_binary_data`で埋めているほう）は新しくなるので、
# **起動画面だけ新しくて、exeとショートカットは古い**という食い違いになります。
#
# **絵を構成の依存に入れておけば、変わったときにCMakeが勝手に走り直します**
# （Visual Studioの生成器では`ZERO_CHECK`がこれを見ています）。
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MANTA_APP_ICON}")

# ---------------------------------------------------------------------------
# 8.133：ボタンのアイコン（Phase 169／改善案リスト3の44）
#
# **exeへ埋め込みます。** 外のファイルを読む形にすると、実行ファイルだけ渡したときに
# アイコンが出ません（アプリの見た目が配り方で変わるのは避ける）。
#
# **ファイル名はASCIIにすること。** `juce_add_binary_data`はファイル名から
# C++の識別子（`BinaryData::tool_select_svg`）を作るので、日本語名だと壊れます。
# 元の名前（「選択ツールボタン.svg」など）との対応はHANDOVER 8.133の表にあります。
# ---------------------------------------------------------------------------
set(PERSONALDAW_EMBEDDED_ASSETS
    Resources/Icons/tool_select.svg
    Resources/Icons/tool_pen.svg
    Resources/Icons/tool_cut.svg
    Resources/Icons/tool_eraser.svg
    Resources/Icons/transport_loop.svg
    Resources/Icons/transport_metronome.svg
    Resources/Icons/browser_folder.svg
    Resources/Icons/browser_file.svg

    # 8.171：内蔵プラグインの帯のボタン（Phase 210／本人が用意）。
    # 元の名前：バイパスボタン.svg／画面固定ボタン.svg（**ファイル名はASCII**）
    Resources/Icons/plugin_bypass.svg
    Resources/Icons/plugin_pin.svg

    # 8.151：起動画面に出すアプリのアイコン（Phase 189／改善案⑰）。
    # exeのアイコン（ICON_BIG）と**同じファイル**を、絵としても読めるように埋め込む。
    #
    # 8.175：**ブランドのぶんだけ入れます**（Phase 216）。`SplashWindow`は
    # 名前で引いているので、`Branding::isEmber`で引く名前を変えるだけで済みます
)

if (MANTA_BRAND STREQUAL "ember")
    list(APPEND PERSONALDAW_EMBEDDED_ASSETS Resources/Icons/ember_icon.png)
else()
    list(APPEND PERSONALDAW_EMBEDDED_ASSETS Resources/Icons/app_icon.png)
endif()

# ---------------------------------------------------------------------------
# 8.151：起動画面の絵（Phase 189／改善案⑰）
#
# `Resources/Splash/splash.png` を置いてビルドすると、起動画面の全面がその絵になります。
# **無くてもビルドは通ります**——そのときはアプリのアイコンと名前を描きます
# （`SplashWindow.cpp`）。ASIOの探し方（下）と同じで、あるときだけ拾う形です。
#
# **ファイル名は splash.png のままにすること。** `juce_add_binary_data`は
# ファイル名からC++の識別子（`splash_png`）を作り、`SplashWindow`はその名前で引きます。
# ---------------------------------------------------------------------------
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Splash/splash.png")
    message(STATUS "Splash image found - using Resources/Splash/splash.png")
    list(APPEND PERSONALDAW_EMBEDDED_ASSETS Resources/Splash/splash.png)
else()
    message(STATUS "No splash image (Resources/Splash/splash.png) - drawing the default startup screen")
endif()

juce_add_binary_data(PersonalDAWIcons
    SOURCES ${PERSONALDAW_EMBEDDED_ASSETS}
)

target_sources(PersonalDAW
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        # 8.163：表示の言語（Phase 201）。表は Tools/ExtractStrings が作り直す
        Source/Localise.cpp
        Source/LocaliseTable.cpp
        Source/ProjectModel.cpp
        Source/ProjectIds.cpp
        Source/MusicalTime.cpp
        Source/MusicalTimeBench.cpp
        Source/TransportBarComponent.cpp
        Source/ArrangeView.cpp
        Source/ConsoleView.cpp
        Source/AudioEngine.cpp
        Source/PluginManager.cpp
        Source/PluginScanView.cpp
        Source/PreferencesDialog.cpp
        Source/WaveformCache.cpp
        Source/ExportOptions.cpp
        Source/MixerLookAndFeel.cpp
        Source/TrackHeaderControls.cpp
        Source/TimelineComponent.cpp
        Source/ClipPlayerProcessor.cpp
        Source/HitPointDetector.cpp
        Source/PluginCrashTracker.cpp
        Source/SandboxWorker.cpp
        Source/PluginSandboxHost.cpp
        Source/SandboxedPluginProcessor.cpp
        Source/PianoRollComponent.cpp
        Source/PianoRollView.cpp
        Source/PianoRollHeaderComponent.cpp
        Source/EditClipboard.cpp
        Source/NameEntry.cpp
        Source/AutomationCurveUI.cpp
        Source/AudioEditorView.cpp
        Source/PianoRollTrackList.cpp
        Source/MidiPlayerProcessor.cpp
        Source/RecorderProcessor.cpp
        Source/LevelMeterComponent.cpp
        Source/AutoSaveManager.cpp
        Source/ChannelStripComponent.cpp
        Source/TrackRackComponent.cpp
        Source/MasterStripComponent.cpp
        Source/MasterChannelProcessor.cpp
        Source/MetronomeProcessor.cpp
        Source/TrackChannelProcessor.cpp
        Source/SendGainProcessor.cpp
        Source/MidiFileExporter.cpp
        Source/MidiRecording.cpp
        Source/AudioTransform.cpp
        Source/ClipAudioRenderer.cpp
        Source/GrooveQuantise.cpp
        Source/EditorPanel.cpp
        Source/ChordPadPanel.cpp
        Source/AppSettings.cpp
        Source/AppColours.cpp
        Source/PanelResizerBar.cpp
        Source/SelectionState.cpp
        Source/SnapGridSelector.cpp
        Source/StorageLocations.cpp
        Source/ValueEntrySlider.cpp
        Source/BrowserPanel.cpp
        Source/InspectorPanel.cpp
        Source/DragAndDropIds.cpp
        # 8.153：MP3の書き出し（Phase 191/8.1のD9b）
        Source/Mp3Writer.cpp
        # 8.151：起動画面とテンプレート（Phase 189／8.1のD8・改善案⑰）
        Source/SplashWindow.cpp
        Source/ProjectTemplates.cpp
        Source/ProjectChooser.cpp

        # 9.5：**Manta Studio内部専用のプラグイン**（Phase 204）。
        #
        # `MantaPluginFormat`を`AudioPluginFormatManager`へ載せることで、
        # 挿す・保存する・開き直す・オートメーションが**外のVST3とまったく同じ道**を
        # 通ります（`Source/Plugins/MantaPluginFormat.h`）。
        #
        # **フラットな`Source/`ではなく`Source/Plugins/`に置いてあります**——
        # プラグイン1つで6ファイル前後になるので、増やすたびに`Source/`の一覧が
        # 埋まってしまうためです（EQ設計書2章のディレクトリ構成案に合わせてあります）。
        Source/Plugins/MantaPluginFormat.cpp
        Source/Plugins/MantaEQ/MantaEQParameters.cpp
        Source/Plugins/MantaEQ/EQFilterDesign.cpp
        Source/Plugins/MantaEQ/EQAnalyser.cpp
        # 仕様書4.7・設計書4.2：Linear Phaseモード（Phase 205）
        Source/Plugins/MantaEQ/LinearPhaseEngine.cpp
        Source/Plugins/MantaEQ/MantaEQProcessor.cpp
        Source/Plugins/MantaKnobLookAndFeel.cpp
        Source/Plugins/MantaPluginToolbar.cpp
        Source/Plugins/MantaEQ/EQCurveComponent.cpp
        Source/Plugins/MantaEQ/MantaEQEditor.cpp

        # コンプ仕様書・設計書：2つめの内蔵プラグイン（Phase 208）
        Source/Plugins/MantaComp/MantaCompParameters.cpp
        Source/Plugins/MantaComp/CompressorEngine.cpp
        Source/Plugins/MantaComp/MantaCompProcessor.cpp
        Source/Plugins/MantaComp/CompressorDisplay.cpp
        Source/Plugins/MantaComp/MantaCompEditor.cpp

        # 3つめの内蔵プラグイン＝**最初の音源**（Phase 213）。
        # 本人が以前に作ったEAGLE type0（別リポジトリ）が元。
        # **DSPとエフェクトはヘッダだけ**（JUCEに依存させていないので、
        # そのままg++で測れる。`MantaSynthDSP.h`）
        Source/Plugins/MantaSynth/MantaSynthParameters.cpp
        Source/Plugins/MantaSynth/MantaSynthProcessor.cpp
        Source/Plugins/MantaSynth/MantaSynthEditor.cpp
)

target_compile_definitions(PersonalDAW
    PRIVATE
        # 8.175：**どちらのアプリを作っているか**（Phase 216／`Source/Branding.h`）。
        # Manta Studioでは定義しません（`#if MANTA_BRAND_EMBER`は0として扱われます）
        $<$<STREQUAL:${MANTA_BRAND},ember>:MANTA_BRAND_EMBER=1>

        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PersonalDAW,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PersonalDAW,JUCE_VERSION>"
        # VST3ホスティングを有効化する（設計書3.1）。これが無いと
        # AudioPluginFormatManager::addDefaultFormats() 自体が「削除された関数」になり、
        # 呼び出し箇所でC2280エラーが出る。AUと違いjuce_add_gui_appの専用引数が無いため、
        # ここで直接定義する必要がある。
        JUCE_PLUGINHOST_VST3=1

        # 8.153：MP3の**読み込み**（Phase 191/D9b）。
        #
        # 書き出せるのに読み込めないのは、使う側から見て理屈が通りません。
        # JUCEにデコーダは入っていて、既定で切ってあるだけです
        # （MP3の特許は2017年に切れているので、有効にして構いません）。
        # これでブラウザからもドラッグからもMP3が取り込めます
        JUCE_USE_MP3AUDIOFORMAT=1
)

# ---------------------------------------------------------------------------
# ASIO（仕様書5.4／HANDOVER 8.88）。**SDKが置いてあるときだけ有効になります。**
#
# SteinbergのASIO SDKは再配布できないので、このリポジトリには入れていません。
# 使う場合は https://www.steinberg.net/developers/ から取得して、
# 中の `common` フォルダを次のどちらかに置いてください：
#
#   1. Tools/ASIOSDK/common            （既定の探し場所）
#   2. cmake の -DASIO_SDK_DIR=<パス>  （別の場所に置いた場合）
#
# **JUCEが要るのはヘッダだけ**です（iasiodrv.h ほか。juce_audio_devicesが取り込む）。
# 見つからなければ黙って無効のまま進みます——**警告を出さないこと**：
# ASIOを使わない環境でビルドするたびに出ると、本当の警告が埋もれます。
# ---------------------------------------------------------------------------
# 8.175：**Ember版にはASIOを入れません**（Phase 216／本人の判断）。
#
# ASIO SDKは**再配布できず、しかもプロプライエタリ**なので、
# オープンソース（GPL/AGPL）で配るものへリンクできません。
# SDKを置かなければ勝手に無効になりますが、
# **置いてある環境で誤って混ざらないよう**、ここで明示的に外します。
# 代わりにWindows Audio（WASAPI共有）が既定になります——
# 他のアプリと音が共存し、実測では往復20.0ms（ASIOの1024サンプルより速い。8.174）
if (WIN32 AND NOT MANTA_BRAND STREQUAL "ember")
    if (NOT DEFINED ASIO_SDK_DIR)
        set(ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tools/ASIOSDK/common")
    endif()

    if (EXISTS "${ASIO_SDK_DIR}/iasiodrv.h")
        message(STATUS "ASIO SDK found at ${ASIO_SDK_DIR} - ASIO support enabled")
        target_include_directories(PersonalDAW PRIVATE "${ASIO_SDK_DIR}")
        target_compile_definitions(PersonalDAW PRIVATE JUCE_ASIO=1)
    else()
        message(STATUS "ASIO SDK not found (looked in ${ASIO_SDK_DIR}) - building without ASIO")
    endif()
endif()

# ---------------------------------------------------------------------------
# 8.153：MP3の書き出し（Phase 191／8.1のD9b）。
#
# **Windowsが最初から持っているエンコーダを使います**（Media Foundation）。
# LAME等を抱えないので、配布物もライセンスの確認も増えません（`Mp3Writer.h`）。
#
# **`Mp3Writer.cpp`はWindows以外では中身が空**になり、
# 「使えません」と答えるだけの関数が残ります。
# ---------------------------------------------------------------------------
if (WIN32)
    target_link_libraries(PersonalDAW PRIVATE mfplat mf mfuuid mfreadwrite ole32)
endif()

target_link_libraries(PersonalDAW
    PRIVATE
        PersonalDAWIcons          # 8.133：ボタンのアイコン（Phase 169）
        juce::juce_gui_extra
        juce::juce_data_structures
        juce::juce_audio_utils
        # 8.147：オーディオのトランスポーズのFFT（Phase 185／改善案㉞）。
        # **要るのは`juce::dsp::FFT`だけ**です（自前の位相ボコーダー。`AudioTransform.h`）
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# 8.179：**GCC/Clangの口うるさい警告を3つだけ止める**（Phase 220／Linux版のため）。
#
# `juce_recommended_warning_flags`はGCCではMSVCよりずっと多くを指摘します。
# 最初のLinuxビルドでは**ログ901行のうち800行以上が警告**になり、
# **本当のエラーが埋もれて読めませんでした。**
#
# 止めるのはこの3つだけです。**どれもこのコードでは意図してやっていること**で、
# MSVCでは1つも出ません：
#
#   -Wswitch-enum      … switchに全部の列挙値を書け。`default:`で受けている場所が多く、
#                         足すと「増やしたときに気づかない」ほうへ倒れます
#   -Wfloat-equal      … floatを==で比べるな。ドラッグの前後で
#                         「値が動いたか」を見るところは、これでよい
#   -Woverloaded-virtual … `processBlock(float)`を書くと`processBlock(double)`が隠れる。
#                         **JUCEの設計どおり**で、全プラグイン・全プロセッサで出ます
#
# **他の警告は止めていません。** 本当の指摘は今までどおり出ます。
# ---------------------------------------------------------------------------
if (NOT MSVC)
    target_compile_options(PersonalDAW PRIVATE
        -Wno-switch-enum
        -Wno-float-equal
        -Wno-overloaded-virtual
    )
endif()

# ---------------------------------------------------------------------------
# ソースファイル内の日本語コメント（UTF-8）を正しく扱うための設定。
# これがないと、日本語Windows環境（コードページ932）でビルドした際に
# 「表示できない文字を含んでいます」という警告が大量に出ることがあります。
# ---------------------------------------------------------------------------
if (MSVC)
    target_compile_options(PersonalDAW PRIVATE /utf-8)
endif()
