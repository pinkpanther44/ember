cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# 8.259：**内蔵プラグインを、窓を出さずに確かめる道具**（Phase 267／本人の要望）。
#
#   cmake -S Tools/PluginPreview -B out-preview
#   cmake --build out-preview --config Release
#   out-preview\PluginPreview_artefacts\Release\PluginPreview.exe --all
#
# **本体のビルドとは別**です（`PersonalDAW`のターゲットには入っていません）。
# 絵を撮って音を測るだけのものを、配るexeへ混ぜる理由がないためです。
#
# ---------------------------------------------------------------------------
# JUCEは**本体が落としたものを使い回します**
#
# `out/_deps/juce-src`（`cmake -S . -B out`を1度でも通していれば居ます）。
# もう一度FetchContentで落とすと、**同じものを2回持つ**ことになります。
# 別の場所にあるなら `-DJUCE_SOURCE_DIR=...` で渡してください。
# ---------------------------------------------------------------------------

project(PluginPreview VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(DAW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

if (NOT DEFINED JUCE_SOURCE_DIR)
    set(JUCE_SOURCE_DIR "${DAW_ROOT}/out/_deps/juce-src")
endif()

if (NOT EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${JUCE_SOURCE_DIR}.\n"
        "Configure the app once first (cmake -S . -B out), or pass -DJUCE_SOURCE_DIR=<path>.")
endif()

add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

juce_add_gui_app(PluginPreview PRODUCT_NAME "PluginPreview")

# 埋め込みの絵。**本体と同じファイル**を指しています（写しを持たない）
juce_add_binary_data(PluginPreviewAssets
    SOURCES
        "${DAW_ROOT}/Resources/Plugins/racco_guitar_field.jpg"
        "${DAW_ROOT}/Resources/Plugins/java_rhino_bass_field.jpg"
        "${DAW_ROOT}/Resources/Icons/app_icon.png"
        "${DAW_ROOT}/Resources/Icons/plugin_bypass.svg"
        "${DAW_ROOT}/Resources/Icons/plugin_pin.svg"

        # 8.266：ブラウザの絵（`--icons`が色の差し替えを確かめます。Phase 269）
        "${DAW_ROOT}/Resources/Icons/browser_folder.svg"
        "${DAW_ROOT}/Resources/Icons/browser_file.svg")

target_sources(PluginPreview
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp"

        # 画面と音が使う、本体側の部品
        "${DAW_ROOT}/Source/AppColours.cpp"
        "${DAW_ROOT}/Source/AppSettings.cpp"
        "${DAW_ROOT}/Source/Localise.cpp"
        "${DAW_ROOT}/Source/LocaliseTable.cpp"
        "${DAW_ROOT}/Source/MixerLookAndFeel.cpp"
        "${DAW_ROOT}/Source/NameEntry.cpp"
        "${DAW_ROOT}/Source/StorageLocations.cpp"
        "${DAW_ROOT}/Source/ValueEntrySlider.cpp"

        # 内蔵プラグイン（**表ごと本物を使います**——識別子の食い違いもここで出ます）
        "${DAW_ROOT}/Source/Plugins/MantaPluginFormat.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaKnobLookAndFeel.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaPluginToolbar.cpp"

        "${DAW_ROOT}/Source/Plugins/MantaEQ/MantaEQParameters.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaEQ/EQFilterDesign.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaEQ/EQAnalyser.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaEQ/LinearPhaseEngine.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaEQ/MantaEQProcessor.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaEQ/EQCurveComponent.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaEQ/MantaEQEditor.cpp"

        "${DAW_ROOT}/Source/Plugins/MantaComp/MantaCompParameters.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaComp/CompressorEngine.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaComp/MantaCompProcessor.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaComp/CompressorDisplay.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaComp/MantaCompEditor.cpp"

        "${DAW_ROOT}/Source/Plugins/MantaSynth/MantaSynthParameters.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaSynth/MantaSynthProcessor.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaSynth/MantaSynthEditor.cpp"

        "${DAW_ROOT}/Source/Plugins/MantaDelay/MantaDelayParameters.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaDelay/MantaDelayProcessor.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaDelay/MantaDelayEditor.cpp"

        "${DAW_ROOT}/Source/Plugins/MantaReverb/MantaReverbParameters.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaReverb/MantaReverbProcessor.cpp"
        "${DAW_ROOT}/Source/Plugins/MantaReverb/MantaReverbEditor.cpp"

        "${DAW_ROOT}/Source/Plugins/RaccoGuitar/RaccoGuitarParameters.cpp"
        "${DAW_ROOT}/Source/Plugins/RaccoGuitar/RaccoGuitarProcessor.cpp"
        "${DAW_ROOT}/Source/Plugins/RaccoGuitar/RaccoGuitarEditor.cpp"

        "${DAW_ROOT}/Source/Plugins/JavaRhinoBass/JavaRhinoBassParameters.cpp"
        "${DAW_ROOT}/Source/Plugins/JavaRhinoBass/JavaRhinoBassProcessor.cpp"
        "${DAW_ROOT}/Source/Plugins/JavaRhinoBass/JavaRhinoBassEditor.cpp")

target_include_directories(PluginPreview PRIVATE "${DAW_ROOT}/Source")

target_compile_definitions(PluginPreview
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="PluginPreview"
        JUCE_APPLICATION_VERSION_STRING="1.0.0"
        JUCE_PLUGINHOST_VST3=1

        # **この道具でだけ**メッセージループを回します（`--presets`が
        # `AsyncUpdater`の届き先まで見るため）。本体では使っていません
        JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(PluginPreview
    PRIVATE
        PluginPreviewAssets
        juce::juce_gui_extra
        juce::juce_data_structures
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)

if (MSVC)
    target_compile_options(PluginPreview PRIVATE /utf-8)
endif()
