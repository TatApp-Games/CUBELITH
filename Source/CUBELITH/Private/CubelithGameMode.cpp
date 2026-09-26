#include "CubelithGameMode.h"

#include "CubelithOrbitPawn.h"

ACubelithGameMode::ACubelithGameMode()
{
	// マップ /Game/Maps/Main に PlayerStart が無ければ Pawn はワールド原点に湧く。軌道カメラの注視点は
	// 立方体の中心（= 原点。003 で合わせる）なのでそれでよく、マップには手を入れない
	DefaultPawnClass = ACubelithOrbitPawn::StaticClass();
}
