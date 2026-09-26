cmake_minimum_required(VERSION 3.22)

project(PersonalDAW VERSION 1.0.1)

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
    # 8.182：**exeのプロパティに出る作者名**（Phase 221／ライセンス確定）。
    # 仮のままだと、配ったexeの「詳細」タブに"YourName"と表示されます
    COMPANY_NAME "pinkpanther44"
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

    # 8.278：自動スクロールのボタン（Phase 275／本人が用意）。
    # 元の名前：自動スクロール.svg（**ファイル名はASCII**）
    Resources/Icons/auto_scroll.svg

    # 8.295：トラックの種類を示す絵と、オートメーションのボタン（Phase 288／本人が用意）。
    # 元の名前：MIDIアイコン.svg／Audioアイコン.svg／VCAアイコン.svg／
    # オートメーション.svg（**ファイル名はASCII**）
    #
    # **フォルダトラックの絵は増やしていません**——ブラウザと同じ
    # `browser_folder.svg`をそのまま使います。フォルダはどちらの画面でも
    # 同じものなので、**絵を2枚持つと形を直すときに片方だけ直す**ことになります（1.27）。
    #
    # **切り抜きは要りませんでした。** どれも1500x1500の枠に描いてありますが、
    # `Drawable::drawWithin()`が見るのは**中身の外接矩形**（`getDrawableBounds()`）
    # なので、まわりの余白は勝手に落ちます——オートメーションの絵は
    # 1338x1090ぶんしか使っていませんが、そのまま置けば枠いっぱいに出ます
    Resources/Icons/track_midi.svg
    Resources/Icons/track_audio.svg
    Resources/Icons/track_vca.svg
    Resources/Icons/header_automation.svg

    # 8.295：センドトラックの絵（Phase 288。**あとから本人が用意**）。
    # 元の名前：SendIcon.svg（**ファイル名はASCII**）。
    #
    # **コードトラックの絵はありません**——本人の判断で「基本的に1本しか作らない
    # からアイコンは必要ない」。**パラアウトの受け皿はMIDIの絵**を使います
    # （出所がMIDIトラックの音源なので、同じ絵でつながりが読めます）
    Resources/Icons/track_send.svg


    # 8.256：Racco Guitarの画面のまん中に敷く絵（Phase 264／本人が用意）。
    #
    # **1600x900のJPEGにしてあります**（元は1920x1080のPNGで1.8MB）。
    # 滑らかな階調だけの絵なのでJPEGで劣化が見えず、**85KB**まで落ちます——
    # exeへ埋め込むものなので、使われない画素を持ち歩かないこと（ICON_BIGと同じ話）
    Resources/Plugins/racco_guitar_field.jpg

    # 8.257：Java Rhino Bassの画面のまん中に敷く絵（Phase 265／本人が用意）。
    #
    # 8.274：**淡い絵へ差し替えました**（Phase 273／本人の指定。文字が見づらいため）。
    # ラメの粒が無くなったぶんJPEGがよく効くので、**品質92でも168KB**です
    # （前の絵は粒を残すために品質82で469KBありました）。
    # **滑らかな階調だけの絵は、品質を落とすと縞が出ます**——ギター（85KB）と同じ性質で、
    # 小さくするより縞を出さないほうを採っています
    Resources/Plugins/java_rhino_bass_field.jpg

    # 8.288：Orangutan Drumsの画面のまん中に敷く絵（Phase 281／本人が用意）。
    #
    # 元は1920x1080のWebP。**置き場所の形に切ってから**縮めてあります
    # （伸ばすより切るほうが、銅の流れが崩れません）。
    #
    # 8.289：**切り直しました**（Phase 282／窓が縦長になったため）。
    # Phase 281は1600x543（約3:1）でしたが、置き場所が正方形に近くなりました
    # ——3:1の絵を敷くと、真ん中の帯しか映りません。
    #
    # 8.290：**740x949**（Phase 283／絵をタブとマスターまで広げたため）。
    # 置き場所は368x472＝**縦長**になり、切る形もそれに合わせてあります
    # （`RectanglePlacement::fillDestination`で敷くので、**形が合っていないと端が落ちます**）
    #
    # **ガンマ0.72で明るく起こしてあります。** 銅の絵はギター・ベースの絵より
    # 3割ほど暗く（平均の明るさ119に対して187・191）、そのままでは
    # **濃い文字が読めません**。白のベールを厚くすると銅が灰色に寄るので、
    # 絵そのものを起こしてベールは0.20に留めてあります（8.274の経緯と同じ話）
    Resources/Plugins/orangutan_drums_field.jpg

    # 8.292：Kakapoの画面のまん中に敷く絵（Phase 285／本人が用意）。
    #
    # 苔の緑（カカポは緑のオウム）。元は1920x1080のWebPで、3:1の帯に切って1600x543へ。
    # **ガンマ0.62で少しだけ起こしてあります**——この絵は平均の明るさが79しかなく
    # （銅は119、ギター・ベースは187・191）、そのままでは何も読めません。
    # ただし**起こしすぎると苔が灰色に寄る**ので、読ませるものは
    # 絵の上に敷いた薄い板へ載せてあります（`KakapoTheme.h`）
    Resources/Plugins/kakapo_field.jpg

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
        Source/AboutDialog.cpp
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
        Source/SandboxedPluginEditor.cpp
        # 8.260：サンドボックスの往復を窓なしで測る（Phase 268）
        Source/SandboxSelfTest.cpp
        # 8.268：フッターの値と書き換え先を窓なしで確かめる（Phase 270）
        Source/FooterValuesSelfTest.cpp
        # 8.271：刻み（3連符を含む）の寄せ先を窓なしで確かめる（Phase 272）
        Source/SnapGridSelfTest.cpp
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
        Source/CrashLog.cpp     # 8.316：落ちたときに番地を残す（Phase 309）
        Source/AppMessageBox.cpp   # 8.322：メッセージボックスの入口（Phase 312）
        Source/AudioDeviceSelfTest.cpp  # 8.316：デバイスの付け替え（Phase 309）
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
        # 8.284：ドラムマップのプリセット（Phase 277／仕様書10.7）
        Source/DrumMapPresets.cpp
        # 8.284：そのプリセットを窓なしで確かめる（Phase 277）
        Source/DrumMapPresetSelfTest.cpp
        # 8.286：プロジェクトの置き場所を窓なしで確かめる（Phase 279）
        Source/StorageSelfTest.cpp
        # 8.295：トラックヘッダーの並びを窓なしで確かめる（Phase 288）
        Source/TrackHeaderLayoutSelfTest.cpp
        # 8.325：MIDIの時間範囲を窓なしで確かめる（Phase 314）
        Source/TimeRangeSelfTest.cpp
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

        # 8.204：4つめ「Manta Delay」（Phase 238/ディレイ設計書8章のPhase 1）。
        # **エンジン・表示・色はヘッダだけ**（DSPはJUCEのdspモジュールを使うだけなので薄い）
        Source/Plugins/MantaDelay/MantaDelayParameters.cpp
        Source/Plugins/MantaDelay/MantaDelayProcessor.cpp
        Source/Plugins/MantaDelay/MantaDelayEditor.cpp

        # 8.240：5つめ「Manta Reverb」（Phase 254/リバーブ設計書8章のPhase 1）。
        # **エンジン（初期反射・FDN・オールパス・ダンピング）はヘッダだけ**——
        # どれも状態を持つ小さな部品で、`.cpp`へ分けても読む場所が増えるだけです
        Source/Plugins/MantaReverb/MantaReverbParameters.cpp
        Source/Plugins/MantaReverb/MantaReverbProcessor.cpp
        Source/Plugins/MantaReverb/MantaReverbEditor.cpp

        # 8.256：6つめ「Racco Guitar」＝**2つめの音源**（Phase 264／本人の要望）。
        # 本人が別に作った`KSGuitar`（Karplus-Strong拡張版）を移したもので、
        # **アンプ段は持って来ていません**（本人の指定）。
        # **弦とボイスはヘッダだけ**——JUCEに依存しない小さな部品なので、
        # `.cpp`へ分けても読む場所が増えるだけです（リバーブと同じ扱い）
        Source/Plugins/RaccoGuitar/RaccoGuitarParameters.cpp
        Source/Plugins/RaccoGuitar/RaccoGuitarProcessor.cpp
        Source/Plugins/RaccoGuitar/RaccoGuitarEditor.cpp

        # 8.257：7つめ「Java Rhino Bass」＝**3つめの音源**（Phase 265／本人の要望）。
        # 本人が別に作った`JBass5`（5弦ジャズベースの物理モデリング）を移したもので、
        # **アンプ段は持って来ていません**（本人の指定。ギターと同じ切り分け）
        Source/Plugins/JavaRhinoBass/JavaRhinoBassParameters.cpp
        Source/Plugins/JavaRhinoBass/JavaRhinoBassProcessor.cpp
        Source/Plugins/JavaRhinoBass/JavaRhinoBassEditor.cpp

        # 8.288：8つめ「Orangutan Drums」＝**4つめの音源**（Phase 281／本人の要望）。
        # 本人が別に作った`MAGAZINE`（16パッドのドラムシンセ）を移したもので、
        # **シークエンサは持って来ていません**（本人の指定。本体にピアノロールと
        # ドラムエディタがあります）。
        # **音とマスター段はヘッダだけ**——JUCEに依存しないので、そのまま数値で測れます
        Source/Plugins/OrangutanDrums/OrangutanDrumsParameters.cpp
        Source/Plugins/OrangutanDrums/OrangutanDrumsProcessor.cpp
        Source/Plugins/OrangutanDrums/OrangutanDrumsEditor.cpp

        # 8.292：9つめ「Kakapo」＝**5つめの音源**（Phase 285／本人の仕様書・設計書）。
        # 弾いた音からスケールを当てて出すもので、**内蔵の単音リードで音も鳴ります**。
        # **判定・控え・音はヘッダだけ**——JUCEに依存しないので、そのまま数値で測れます
        # （`Tools/PluginPreview --scale`が仕様書18章の試験を回します）
        Source/Plugins/Kakapo/KakapoParameters.cpp
        Source/Plugins/Kakapo/KakapoProcessor.cpp
        Source/Plugins/Kakapo/KakapoEditor.cpp
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

        # 8.189：**LV2も読み込めるようにする**（Phase 227／本人の要望）。
        # JUCEが`lilv`/`serd`/`sord`/`sratom`/`lv2`を**自分の中に持っています**
        # （`LV2_SDK/`。どれもISCライセンスで、AGPLv3と問題なく組み合わせられます）。
        # **外部ライブラリは要りません**——依存もビルド手順も増えません。
        #
        # **WindowsでもLinuxでも有効です。** LV2はLinuxに多い形式ですが、
        # 片方だけ有効にすると「同じアプリなのにOSで挿せるものが違う」ことになります
        JUCE_PLUGINHOST_LV2=1

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
# **`target_compile_options`では効きませんでした**（Phase 220で1度踏んだ）。
# ターゲット自身のオプションは、リンクした相手のINTERFACEオプションより**先**に並ぶので、
# `-Wno-switch-enum ... -Wswitch-enum`の順になり、**後ろ側が勝ちます。**
#
# INTERFACEライブラリにして、`juce_recommended_warning_flags`より**後ろでリンク**すること。
add_library(MantaQuietWarnings INTERFACE)

if (NOT MSVC)
    target_compile_options(MantaQuietWarnings INTERFACE
        -Wno-switch-enum
        -Wno-float-equal
        -Wno-overloaded-virtual
    )
endif()

target_link_libraries(PersonalDAW PUBLIC MantaQuietWarnings)

# ---------------------------------------------------------------------------
# ソースファイル内の日本語コメント（UTF-8）を正しく扱うための設定。
# これがないと、日本語Windows環境（コードページ932）でビルドした際に
# 「表示できない文字を含んでいます」という警告が大量に出ることがあります。
# ---------------------------------------------------------------------------
if (MSVC)
    target_compile_options(PersonalDAW PRIVATE /utf-8)
endif()
