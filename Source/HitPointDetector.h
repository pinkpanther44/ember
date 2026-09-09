#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <vector>

//==============================================================================
/**
    仕様書5.5.1「ヒットポイント（トランジェント）自動検出」の実装。

    音の立ち上がり（オンセット）位置を検出する。設計書5.5.1の方針どおり、
    まずは振幅ベースのシンプルな検出から始めている
    （精度が必要になれば、スペクトルフラックス等の高度な手法へ置き換える想定）。

    アルゴリズムの概要：
    1. 音声を短いフレーム（約10ms）に区切り、各フレームのRMS（音量）を求める
    2. 直前フレームからの音量の増加量が閾値を超えた点を「立ち上がり」とみなす
    3. 一定時間内（デバウンス時間）に連続して検出された場合は最初の1つだけ残す
       （1つの打撃音が複数のヒットポイントとして検出されるのを防ぐ）

    解析はファイル読み込みを伴うため、UIスレッドから直接呼ばず、
    バックグラウンドスレッドで実行すること（呼び出し側の責務）。
*/
class HitPointDetector
{
public:
    struct Parameters
    {
        double frameSizeSeconds = 0.01;      // 解析フレーム長（約10ms）
        double debounceSeconds = 0.05;       // 連続検出を1つにまとめる時間

        /** 8.47：**しきい値は素材のいちばん大きいところからの相対で決める**（Phase 87）。

            固定値だけにすると、**録音レベルが低い素材では1つも見つかりません**。
            下の2つは「これ以下にはしない」という床で、実際のしきい値は
            ピーク×相対値と、この床の大きいほうになります。 */
        float relativeThresholdIncrease = 0.15f;
        float relativeMinimumRms = 0.05f;

        float minimumThresholdIncrease = 0.01f;
        float minimumRms = 0.004f;
    };

    /** 指定ファイルを解析し、ヒットポイント位置（ファイル先頭からの秒数）を返す。
        読み込めない場合は空の配列を返す。 */
    static juce::Array<double> detect (const juce::File& file,
                                        juce::AudioFormatManager& formatManager,
                                        const Parameters& params = {});
};
