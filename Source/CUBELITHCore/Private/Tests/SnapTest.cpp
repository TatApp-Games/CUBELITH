// WebMock/tests/snap.test.ts の snapCandidate（手で組んだケース・生成したパズル）の正常系の移植
// SnapCandidate は Solve.h（solve.ts）の公開 API だが、テストは移植元と 1 対 1 にするためこのファイルに分けてある
// 移植しないテスト: 「入力が壊れていれば例外を投げる」（checkf で停止するため）

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Generate.h"
#include "Grid.h"
#include "Piece.h"
#include "Solve.h"

namespace CubelithCoreTests
{
	// SnapTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace SnapTestDetail
	{
		/** 6 近傍のオフセット（TS の STEPS）。参照実装の「接する」判定に使う */
		TArray<Cubelith::FVec3> Steps()
		{
			return TArray<Cubelith::FVec3>{
				Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(-1, 0, 0), Cubelith::Vec3(0, 1, 0),
				Cubelith::Vec3(0, -1, 0), Cubelith::Vec3(0, 0, 1), Cubelith::Vec3(0, 0, -1) };
		}

		/** 配置を組み立てる小さな入口（TS の place）。向きは既定で恒等 */
		Cubelith::FPlacement Place(int32 PieceId, const Cubelith::FVec3& Position, int32 Orientation = Cubelith::IdentityOrientation)
		{
			Cubelith::FPlacement Placement;
			Placement.PieceId = PieceId;
			Placement.Orientation = Orientation;
			Placement.Position = Position;
			return Placement;
		}

		/** x 方向に 2 マス続くドミノ（正規化後は (0,0,0) と (1,0,0)）。TS の domino */
		Cubelith::FPiece Domino(int32 Id)
		{
			const TArray<Cubelith::FVec3> Voxels{ Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(1, 0, 0) };
			return Cubelith::CreatePiece(Id, Voxels);
		}

		/** x 方向に 3 マス続く棒（正規化後は (-1,0,0)・(0,0,0)・(1,0,0)）。TS の bar3 */
		Cubelith::FPiece Bar3(int32 Id)
		{
			const TArray<Cubelith::FVec3> Voxels{
				Cubelith::Vec3(0, 0, 0), Cubelith::Vec3(1, 0, 0), Cubelith::Vec3(2, 0, 0) };
			return Cubelith::CreatePiece(Id, Voxels);
		}

		/** 単独ボクセル。TS の unit */
		Cubelith::FPiece Unit(int32 Id)
		{
			const TArray<Cubelith::FVec3> Voxels{ Cubelith::Vec3(0, 0, 0) };
			return Cubelith::CreatePiece(Id, Voxels);
		}

		/** id からピースを引く（TS の findPiece）。テストの組み方が壊れていたら checkf */
		const Cubelith::FPiece& FindPiece(TArrayView<const Cubelith::FPiece> Pieces, int32 Id)
		{
			for (const Cubelith::FPiece& Piece : Pieces)
			{
				if (Piece.Id == Id)
				{
					return Piece;
				}
			}
			checkf(false, TEXT("テスト: ピース %d が無い"), Id);
			return Pieces[0];
		}

		/** id から配置を引く（TS の findPlacement） */
		const Cubelith::FPlacement& FindPlacement(TArrayView<const Cubelith::FPlacement> Placements, int32 Id)
		{
			for (const Cubelith::FPlacement& Placement : Placements)
			{
				if (Placement.PieceId == Id)
				{
					return Placement;
				}
			}
			checkf(false, TEXT("テスト: ピース %d の配置が無い"), Id);
			return Placements[0];
		}

		/** アクティブ以外の全ピースのワールドボクセル（TS の otherVoxels） */
		TArray<Cubelith::FVec3> OtherVoxels(
			TArrayView<const Cubelith::FPiece> Pieces, TArrayView<const Cubelith::FPlacement> Placements, int32 ActivePieceId)
		{
			TArray<Cubelith::FVec3> Voxels;
			for (const Cubelith::FPlacement& Placement : Placements)
			{
				if (Placement.PieceId == ActivePieceId)
				{
					continue;
				}
				Voxels.Append(Cubelith::PlacedVoxels(FindPiece(Pieces, Placement.PieceId), Placement));
			}
			return Voxels;
		}

		/**
		 * 実装とは独立に RULES.md 3.5 の 3 条件を素朴に判定する（TS の isValidPosition。テスト側の参照実装）。
		 * 接する・重ならない・外接立方体が N×N×N に収まる。
		 */
		bool IsValidPosition(
			TArrayView<const Cubelith::FPiece> Pieces, TArrayView<const Cubelith::FPlacement> Placements,
			int32 ActivePieceId, int32 N, const Cubelith::FVec3& Position)
		{
			const Cubelith::FPlacement& Active = FindPlacement(Placements, ActivePieceId);
			const TArray<Cubelith::FVec3> Others = OtherVoxels(Pieces, Placements, ActivePieceId);
			if (Others.Num() == 0)
			{
				return false;
			}

			const TArray<Cubelith::FVec3> Moved = Cubelith::PlacedVoxels(
				FindPiece(Pieces, ActivePieceId), Place(ActivePieceId, Position, Active.Orientation));

			const TSet<Cubelith::FVec3> Occupied(Others);
			for (const Cubelith::FVec3& V : Moved)
			{
				if (Occupied.Contains(V))
				{
					return false;
				}
			}

			bool bTouches = false;
			for (const Cubelith::FVec3& V : Moved)
			{
				for (const Cubelith::FVec3& Step : Steps())
				{
					if (Occupied.Contains(Cubelith::AddVec3(V, Step)))
					{
						bTouches = true;
					}
				}
			}
			if (!bTouches)
			{
				return false;
			}

			TArray<Cubelith::FVec3> All = Others;
			All.Append(Moved);
			const Cubelith::FBoundingBox Box = Cubelith::BoundingBox(All);
			return Box.Size.X <= N && Box.Size.Y <= N && Box.Size.Z <= N;
		}

		/** 原点からのマンハッタン距離（TS の manhattan） */
		int32 Manhattan(const Cubelith::FVec3& V)
		{
			return FMath::Abs(V.X) + FMath::Abs(V.Y) + FMath::Abs(V.Z);
		}

		/** 27 通りの平行移動を総当たりして「最も近い有効な位置」を求める参照実装（TS の bruteForceCandidate） */
		TOptional<Cubelith::FPlacement> BruteForceCandidate(
			TArrayView<const Cubelith::FPiece> Pieces, TArrayView<const Cubelith::FPlacement> Placements,
			int32 ActivePieceId, int32 N)
		{
			const Cubelith::FPlacement& Active = FindPlacement(Placements, ActivePieceId);
			TOptional<Cubelith::FVec3> Best;
			for (int32 X = -1; X <= 1; ++X)
			{
				for (int32 Y = -1; Y <= 1; ++Y)
				{
					for (int32 Z = -1; Z <= 1; ++Z)
					{
						const Cubelith::FVec3 Offset = Cubelith::Vec3(X, Y, Z);
						const Cubelith::FVec3 Position = Cubelith::AddVec3(Active.Position, Offset);
						if (!IsValidPosition(Pieces, Placements, ActivePieceId, N, Position))
						{
							continue;
						}
						if (!Best.IsSet())
						{
							Best = Offset;
							continue;
						}
						const int32 Closer = Manhattan(Offset) - Manhattan(Best.GetValue());
						if (Closer < 0 || (Closer == 0 && Cubelith::CompareVec3(Offset, Best.GetValue()) < 0))
						{
							Best = Offset;
						}
					}
				}
			}
			if (!Best.IsSet())
			{
				return TOptional<Cubelith::FPlacement>();
			}
			return TOptional<Cubelith::FPlacement>(
				Place(ActivePieceId, Cubelith::AddVec3(Active.Position, Best.GetValue()), Active.Orientation));
		}

		/** 期待する座標と一致するか。成分を直に比べるので EqualsVec3 の実装には依存しない */
		bool CheckVec3(FAutomationTestBase& Test, const FString& What, const Cubelith::FVec3& Actual, const Cubelith::FVec3& Expected)
		{
			if (Actual.X == Expected.X && Actual.Y == Expected.Y && Actual.Z == Expected.Z)
			{
				return true;
			}
			Test.AddError(FString::Printf(TEXT("%s: 期待 (%d,%d,%d) / 実際 (%d,%d,%d)"),
				*What, Expected.X, Expected.Y, Expected.Z, Actual.X, Actual.Y, Actual.Z));
			return false;
		}

		/** 2 つの候補（未設定を含む）が一致するか */
		bool CheckCandidateEquals(FAutomationTestBase& Test, const FString& What,
			const TOptional<Cubelith::FPlacement>& Actual, const TOptional<Cubelith::FPlacement>& Expected)
		{
			if (Actual.IsSet() != Expected.IsSet())
			{
				Test.AddError(FString::Printf(TEXT("%s: 候補の有無が違う 期待 %s / 実際 %s"),
					*What, Expected.IsSet() ? TEXT("あり") : TEXT("なし"), Actual.IsSet() ? TEXT("あり") : TEXT("なし")));
				return false;
			}
			if (!Actual.IsSet())
			{
				return true;
			}
			if (Actual.GetValue() != Expected.GetValue())
			{
				const Cubelith::FPlacement& A = Actual.GetValue();
				const Cubelith::FPlacement& E = Expected.GetValue();
				Test.AddError(FString::Printf(TEXT("%s: 候補が違う 期待 {id %d, 向き %d, (%d,%d,%d)} / 実際 {id %d, 向き %d, (%d,%d,%d)}"),
					*What, E.PieceId, E.Orientation, E.Position.X, E.Position.Y, E.Position.Z,
					A.PieceId, A.Orientation, A.Position.X, A.Position.Y, A.Position.Z));
				return false;
			}
			return true;
		}

		/** ドミノ 2 枚（id 0 / 1）。TS の dominoes */
		TArray<Cubelith::FPiece> Dominoes()
		{
			return TArray<Cubelith::FPiece>{ Domino(0), Domino(1) };
		}

		/** 単独ボクセル 2 つ（id 0 / 1）。TS の units */
		TArray<Cubelith::FPiece> Units()
		{
			return TArray<Cubelith::FPiece>{ Unit(0), Unit(1) };
		}

		/** 生成したパズルで試す組み合わせ（TS の CASES） */
		struct FGeneratedCase
		{
			int32 N = 0;
			int32 M = 0;
			uint32 Seed = 0;
		};

		const TArray<FGeneratedCase>& GeneratedCases()
		{
			static const TArray<FGeneratedCase> Cases{
				{ 3, 2, 1 }, { 3, 4, 7 }, { 4, 5, 12345 }, { 5, 8, 99 }, { 7, 27, 2026 } };
			return Cases;
		}

		/** 1 つの配置だけを平行移動した配列を返す（TS の solution.map(...)） */
		TArray<Cubelith::FPlacement> TranslateOne(
			TArrayView<const Cubelith::FPlacement> Placements, int32 PieceId, const Cubelith::FVec3& Step)
		{
			TArray<Cubelith::FPlacement> Result;
			Result.Reserve(Placements.Num());
			for (const Cubelith::FPlacement& Placement : Placements)
			{
				Result.Add(Placement.PieceId == PieceId
					? Place(Placement.PieceId, Cubelith::AddVec3(Placement.Position, Step), Placement.Orientation)
					: Placement);
			}
			return Result;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapOneStepOffReturnsSolutionTest, "CUBELITH.Core.Snap.OneStepOffReturnsSolution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapOneStepOffReturnsSolutionTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// ピース 0 は (0,0,0)-(1,0,0)。ピース 1 の正解は (0,1,0)-(1,1,0) だが 1 マス上にずれている
	const TArray<Cubelith::FPiece> Dominoes = SnapTestDetail::Dominoes();
	const TArray<Cubelith::FPlacement> Placements{
		SnapTestDetail::Place(0, Cubelith::Vec3(0, 0, 0)), SnapTestDetail::Place(1, Cubelith::Vec3(0, 2, 0)) };

	const TOptional<Cubelith::FPlacement> Candidate = Cubelith::SnapCandidate(Dominoes, Placements, 1, 3);
	if (!Candidate.IsSet())
	{
		AddError(TEXT("候補が見つからなかった"));
		return false;
	}

	TestEqual(TEXT("候補のピース id"), Candidate.GetValue().PieceId, 1);
	SnapTestDetail::CheckVec3(*this, TEXT("候補の位置"), Candidate.GetValue().Position, Cubelith::Vec3(0, 1, 0));
	// 向きは変えない（スナップは平行移動だけ）
	TestEqual(TEXT("候補の向き"), Candidate.GetValue().Orientation, Cubelith::IdentityOrientation);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapTwoStepsAwayNoCandidateTest, "CUBELITH.Core.Snap.TwoStepsAwayNoCandidate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapTwoStepsAwayNoCandidateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 2 マス以上離れていれば候補なし（TS の null）
	const TArray<Cubelith::FPiece> Dominoes = SnapTestDetail::Dominoes();
	const TArray<Cubelith::FPlacement> Placements{
		SnapTestDetail::Place(0, Cubelith::Vec3(0, 0, 0)), SnapTestDetail::Place(1, Cubelith::Vec3(0, 4, 0)) };

	TestFalse(TEXT("2 マス以上離れていれば候補なし"), Cubelith::SnapCandidate(Dominoes, Placements, 1, 3).IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapNotTouchingNoCandidateTest, "CUBELITH.Core.Snap.NotTouchingNoCandidate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapNotTouchingNoCandidateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// どのピースとも接しない位置は候補にならない
	// N=5 なので外接立方体には余裕がある。斜めに離れていて 1 マス動かしても面が接しない
	const TArray<Cubelith::FPiece> Units = SnapTestDetail::Units();
	const TArray<Cubelith::FPlacement> Placements{
		SnapTestDetail::Place(0, Cubelith::Vec3(0, 0, 0)), SnapTestDetail::Place(1, Cubelith::Vec3(2, 2, 0)) };

	TestFalse(TEXT("接しない位置は候補にならない"), Cubelith::SnapCandidate(Units, Placements, 1, 5).IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapOverlappingNotCandidateTest, "CUBELITH.Core.Snap.OverlappingNotCandidate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapOverlappingNotCandidateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 現在位置（移動量 0）はピース 0 と 1 マス重なっている。重ならない別の位置が選ばれる
	const TArray<Cubelith::FPiece> Dominoes = SnapTestDetail::Dominoes();
	const TArray<Cubelith::FPlacement> Placements{
		SnapTestDetail::Place(0, Cubelith::Vec3(0, 0, 0)), SnapTestDetail::Place(1, Cubelith::Vec3(1, 0, 0)) };

	const TOptional<Cubelith::FPlacement> Candidate = Cubelith::SnapCandidate(Dominoes, Placements, 1, 3);
	if (!Candidate.IsSet())
	{
		AddError(TEXT("候補が見つからなかった"));
		return false;
	}
	TestFalse(TEXT("現在位置は選ばれない"), Candidate.GetValue().Position == Cubelith::Vec3(1, 0, 0));

	const TSet<Cubelith::FVec3> Occupied(SnapTestDetail::OtherVoxels(Dominoes, Placements, 1));
	for (const Cubelith::FVec3& V : Cubelith::PlacedVoxels(SnapTestDetail::FindPiece(Dominoes, 1), Candidate.GetValue()))
	{
		TestFalse(FString::Printf(TEXT("候補のボクセル (%d,%d,%d) が重なっていない"), V.X, V.Y, V.Z), Occupied.Contains(V));
	}

	SnapTestDetail::CheckCandidateEquals(*this, TEXT("総当たりの参照実装との一致"),
		Candidate, SnapTestDetail::BruteForceCandidate(Dominoes, Placements, 1, 3));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapExceedingCubeNotCandidateTest, "CUBELITH.Core.Snap.ExceedingCubeNotCandidate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapExceedingCubeNotCandidateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 3 マスの棒 2 本。現在位置のままだと x が 4 マスに広がるので、x を 1 縮める位置が選ばれる
	const TArray<Cubelith::FPiece> Bars{ SnapTestDetail::Bar3(0), SnapTestDetail::Bar3(1) };
	const TArray<Cubelith::FPlacement> Placements{
		SnapTestDetail::Place(0, Cubelith::Vec3(1, 0, 0)), SnapTestDetail::Place(1, Cubelith::Vec3(2, 1, 0)) };

	TestFalse(TEXT("現在位置は条件を満たさない"),
		SnapTestDetail::IsValidPosition(Bars, Placements, 1, 3, Cubelith::Vec3(2, 1, 0)));

	const TOptional<Cubelith::FPlacement> Candidate = Cubelith::SnapCandidate(Bars, Placements, 1, 3);
	if (!Candidate.IsSet())
	{
		AddError(TEXT("候補が見つからなかった"));
		return false;
	}
	SnapTestDetail::CheckVec3(*this, TEXT("候補の位置"), Candidate.GetValue().Position, Cubelith::Vec3(1, 1, 0));

	TArray<Cubelith::FVec3> All = SnapTestDetail::OtherVoxels(Bars, Placements, 1);
	All.Append(Cubelith::PlacedVoxels(SnapTestDetail::FindPiece(Bars, 1), Candidate.GetValue()));
	SnapTestDetail::CheckVec3(*this, TEXT("全ボクセルの外接ボックスの Size"),
		Cubelith::BoundingBox(All).Size, Cubelith::Vec3(3, 2, 1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapZeroOffsetWhenAlreadyValidTest, "CUBELITH.Core.Snap.ZeroOffsetWhenAlreadyValid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapZeroOffsetWhenAlreadyValidTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 現在位置が条件を満たしていれば移動量 0 の候補を返す
	const TArray<Cubelith::FPiece> Dominoes = SnapTestDetail::Dominoes();
	const TArray<Cubelith::FPlacement> Placements{
		SnapTestDetail::Place(0, Cubelith::Vec3(0, 0, 0)), SnapTestDetail::Place(1, Cubelith::Vec3(0, 1, 0)) };

	const TOptional<Cubelith::FPlacement> Candidate = Cubelith::SnapCandidate(Dominoes, Placements, 1, 3);
	if (!Candidate.IsSet())
	{
		AddError(TEXT("候補が見つからなかった"));
		return false;
	}
	SnapTestDetail::CheckVec3(*this, TEXT("候補の位置"), Candidate.GetValue().Position, Cubelith::Vec3(0, 1, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapNoOtherPieceNoCandidateTest, "CUBELITH.Core.Snap.NoOtherPieceNoCandidate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapNoOtherPieceNoCandidateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 接する相手がいなければ候補なし
	const TArray<Cubelith::FPiece> Single{ SnapTestDetail::Domino(0) };
	const TArray<Cubelith::FPlacement> Placements{ SnapTestDetail::Place(0, Cubelith::Vec3(0, 0, 0)) };

	TestFalse(TEXT("接する相手がいなければ候補なし"), Cubelith::SnapCandidate(Single, Placements, 0, 3).IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapTieBrokenDeterministicallyTest, "CUBELITH.Core.Snap.TieBrokenDeterministically",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapTieBrokenDeterministicallyTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// ピース 1 は斜め位置。(-1,0,0) と (0,-1,0) がどちらも有効で距離は同じ。
	// 同点は座標の辞書順（x → y → z）なので (-1,0,0) が選ばれる
	const TArray<Cubelith::FPiece> Units = SnapTestDetail::Units();
	const TArray<Cubelith::FPlacement> Placements{
		SnapTestDetail::Place(0, Cubelith::Vec3(0, 0, 0)), SnapTestDetail::Place(1, Cubelith::Vec3(1, 1, 0)) };

	TestTrue(TEXT("(0,1,0) は有効"), SnapTestDetail::IsValidPosition(Units, Placements, 1, 3, Cubelith::Vec3(0, 1, 0)));
	TestTrue(TEXT("(1,0,0) は有効"), SnapTestDetail::IsValidPosition(Units, Placements, 1, 3, Cubelith::Vec3(1, 0, 0)));

	const TOptional<Cubelith::FPlacement> Candidate = Cubelith::SnapCandidate(Units, Placements, 1, 3);
	if (!Candidate.IsSet())
	{
		AddError(TEXT("候補が見つからなかった"));
		return false;
	}
	SnapTestDetail::CheckVec3(*this, TEXT("候補の位置"), Candidate.GetValue().Position, Cubelith::Vec3(0, 1, 0));

	// 同じ入力なら何度呼んでも同じ。ピースと配置の並びを変えても変わらない
	SnapTestDetail::CheckCandidateEquals(*this, TEXT("2 度目の呼び出し"),
		Cubelith::SnapCandidate(Units, Placements, 1, 3), Candidate);

	TArray<Cubelith::FPlacement> ReversedPlacements = Placements;
	Algo::Reverse(ReversedPlacements);
	SnapTestDetail::CheckCandidateEquals(*this, TEXT("配置の並びを逆にした結果"),
		Cubelith::SnapCandidate(Units, ReversedPlacements, 1, 3), Candidate);

	TArray<Cubelith::FPiece> ReversedPieces = Units;
	Algo::Reverse(ReversedPieces);
	SnapTestDetail::CheckCandidateEquals(*this, TEXT("ピースの並びを逆にした結果"),
		Cubelith::SnapCandidate(ReversedPieces, Placements, 1, 3), Candidate);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapPrefersNearerCandidateTest, "CUBELITH.Core.Snap.PrefersNearerCandidate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapPrefersNearerCandidateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// (0,-1,0) は距離 1、(0,-1,±1) や (±1,-1,0) は距離 2。どれも有効だが近い方が返る
	const TArray<Cubelith::FPiece> Units = SnapTestDetail::Units();
	const TArray<Cubelith::FPlacement> Placements{
		SnapTestDetail::Place(0, Cubelith::Vec3(0, 0, 0)), SnapTestDetail::Place(1, Cubelith::Vec3(0, 2, 0)) };

	TestTrue(TEXT("(0,1,0) は有効"), SnapTestDetail::IsValidPosition(Units, Placements, 1, 3, Cubelith::Vec3(0, 1, 0)));
	TestFalse(TEXT("(1,1,0) は無効"), SnapTestDetail::IsValidPosition(Units, Placements, 1, 3, Cubelith::Vec3(1, 1, 0)));
	TestTrue(TEXT("(0,0,1) は有効"), SnapTestDetail::IsValidPosition(Units, Placements, 1, 3, Cubelith::Vec3(0, 0, 1)));

	const TOptional<Cubelith::FPlacement> Candidate = Cubelith::SnapCandidate(Units, Placements, 1, 3);
	if (!Candidate.IsSet())
	{
		AddError(TEXT("候補が見つからなかった"));
		return false;
	}
	SnapTestDetail::CheckVec3(*this, TEXT("候補の位置"), Candidate.GetValue().Position, Cubelith::Vec3(0, 1, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapGeneratedPuzzleMatchesBruteForceTest, "CUBELITH.Core.Snap.GeneratedPuzzleMatchesBruteForce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapGeneratedPuzzleMatchesBruteForceTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 生成したパズルの解答配置から 1 ピースを 1 マスずらしても、総当たりの参照実装と同じ答えになる。
	// 件数が多いので、食い違ったらそのケースを打ち切る（記録を増やさない）
	for (const SnapTestDetail::FGeneratedCase& Case : SnapTestDetail::GeneratedCases())
	{
		const FString What = FString::Printf(TEXT("N=%d / M=%d / seed=%u"), Case.N, Case.M, Case.Seed);
		const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(Case.N, Case.M, Case.Seed);

		bool bFailed = false;
		for (const Cubelith::FVec3& Step : SnapTestDetail::Steps())
		{
			for (const Cubelith::FPlacement& Target : Puzzle.Solution)
			{
				const FString Where = FString::Printf(TEXT("%s: ピース %d を (%d,%d,%d) ずらしたとき"),
					*What, Target.PieceId, Step.X, Step.Y, Step.Z);
				const TArray<Cubelith::FPlacement> Placements =
					SnapTestDetail::TranslateOne(Puzzle.Solution, Target.PieceId, Step);

				const TOptional<Cubelith::FPlacement> Candidate =
					Cubelith::SnapCandidate(Puzzle.Pieces, Placements, Target.PieceId, Case.N);

				// 総当たりの参照実装と完全に一致する
				if (!SnapTestDetail::CheckCandidateEquals(*this, Where, Candidate,
					SnapTestDetail::BruteForceCandidate(Puzzle.Pieces, Placements, Target.PieceId, Case.N)))
				{
					bFailed = true;
					break;
				}
				if (!Candidate.IsSet())
				{
					AddError(FString::Printf(TEXT("%s: 候補が見つからなかった"), *Where));
					bFailed = true;
					break;
				}

				// 他のピースが N×N×N を張っているなら、収まる位置は解答位置しか無い
				const Cubelith::FBoundingBox Box = Cubelith::BoundingBox(
					SnapTestDetail::OtherVoxels(Puzzle.Pieces, Placements, Target.PieceId));
				if (Box.Size.X == Case.N && Box.Size.Y == Case.N && Box.Size.Z == Case.N)
				{
					if (!SnapTestDetail::CheckVec3(*this, FString::Printf(TEXT("%s の候補の位置"), *Where),
						Candidate.GetValue().Position, Target.Position))
					{
						bFailed = true;
						break;
					}
				}
			}
			if (bFailed)
			{
				break;
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithSnapFarFromCubeNoCandidateTest, "CUBELITH.Core.Snap.FarFromCubeNoCandidate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithSnapFarFromCubeNoCandidateTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	// 立方体から遠く離れた配置では候補が出ない
	const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(4, 5, 3);
	const TArray<Cubelith::FPlacement> Placements =
		SnapTestDetail::TranslateOne(Puzzle.Solution, 0, Cubelith::Vec3(20, 20, 20));

	TestFalse(TEXT("立方体から遠く離れた配置では候補が出ない"),
		Cubelith::SnapCandidate(Puzzle.Pieces, Placements, 0, 4).IsSet());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
