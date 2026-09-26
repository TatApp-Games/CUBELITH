// CubelithTitleState.h の実装。文言は移植元 WebMock/src/ui/titleScreen.ts と同じ形に揃える

#include "CubelithTitleState.h"

namespace Cubelith
{
	const TCHAR* RotationLabel(bool bAllowRotation)
	{
		return bAllowRotation ? TEXT("回転あり") : TEXT("回転なし");
	}

	FTitleResume MakeTitleResume(const FCubelithSavedProgress& Progress)
	{
		FTitleResume Resume;
		Resume.SpaceSize = Progress.Difficulty.SpaceSize;
		Resume.PieceCount = Progress.Difficulty.PieceCount;
		Resume.bAllowRotation = Progress.Difficulty.bAllowRotation;
		Resume.Remaining = Progress.Remaining;
		return Resume;
	}

	FString TitleResumeText(const FTitleResume& Resume)
	{
		return FString::Printf(TEXT("N = %d / M = %d / %s・残り %d ピース"),
			Resume.SpaceSize, Resume.PieceCount, RotationLabel(Resume.bAllowRotation), Resume.Remaining);
	}

	FString TitleClearCountText(int32 Total, int32 CountOfSelected)
	{
		return FString::Printf(TEXT("クリア 合計 %d 回 / この難易度 %d 回"), Total, CountOfSelected);
	}

	FString TitleSummaryText(int32 SpaceSize, int32 PieceCount, bool bAllowRotation)
	{
		return FString::Printf(TEXT("N = %d / M = %d / %s（%d ボクセルを %d 個に分割）"),
			SpaceSize, PieceCount, RotationLabel(bAllowRotation),
			SpaceSize * SpaceSize * SpaceSize, PieceCount);
	}
}
