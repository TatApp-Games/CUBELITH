// CUBELITH モジュール（描画・入力・UI）のログカテゴリ
// ゲームロジック（CUBELITHCore）は Core だけに依存する決まり（Docs/SPEC_UE.md 7.1）なのでログを持たない。
// 生成したパズルの条件やシードのように「人が後から再現するために要る情報」はここから出す

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

CUBELITH_API DECLARE_LOG_CATEGORY_EXTERN(LogCubelith, Log, All);
