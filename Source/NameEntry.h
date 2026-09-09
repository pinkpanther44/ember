#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    「名前をひとつ入れてもらう」だけのダイアログ（Phase 72／8.33）。

    マーカー名・トラック名・ドラムのパート名で**同じものを3回書いていた**ので、
    1つにまとめました。増やすときはここを呼ぶこと。

    **非同期で出します。** モーダルループを回すとオーディオのタイマーごと止まります
    （JUCEの`AlertWindow::runModalLoop()`を使わない理由。1.16）。

    `onAccepted`は**OKが押されて、かつ中身が空でないとき**だけ呼ばれます。
    呼ばれるのはメッセージスレッドで、その時点で対象が消えている可能性があるので、
    **受け取る側でValueTreeの生死を確かめること**（1.32）。
*/
namespace NameEntry
{
    void show (const juce::String& title,
               const juce::String& message,
               const juce::String& initialText,
               std::function<void (const juce::String& newName)> onAccepted,
               const juce::String& fieldLabel = {});
}
