// 照合データ（Docs/FIXTURES.md の puzzle_n{N}_m{M}.json）と生成結果の一致を確かめる（RULES.md 3.6）
// 25 ファイル × 6 ケース（allowRotation が false → true、その中で puzzleSeeds の順）を全部回す
// このファイルで比べるのは pieces / solution / scatter / reshuffleWithoutHints。hintPieceIds / reshuffleWithHints は 007 で足す
// WebMock/src/core のファイルとの 1 対 1 の対象外（照合データのファイルに合わせてテスト側の都合で切ったファイル）
// ケース数が多いので、一致しているときは記録を増やさず、食い違ったときだけ AddError してそのケースの残りを打ち切る

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FixtureHelpers.h"
#include "Generate.h"
#include "Grid.h"
#include "Piece.h"

namespace CubelithCoreTests
{
	// PuzzleFixturesTest.cpp 専用のヘルパ。unity ビルドでは他のテストファイルと同じ翻訳単位に入るので、名前をこの中に閉じる
	namespace PuzzleFixturesDetail
	{
		/** 照合データの 1 ケース（puzzle_n{N}_m{M}.json の cases[CaseIndex]） */
		struct FPuzzleCase
		{
			int32 N = 0;
			int32 M = 0;
			uint32 Seed = 0;
			bool bAllowRotation = false;
			FString FileName;
			int32 CaseIndex = 0;
			/** ケースのオブジェクトそのもの。pieces / solution / scatter（007 は hintPieceIds なども）をここから読む */
			TSharedPtr<FJsonObject> Json;
		};

		/** ずれたときに「どのファイルのどのケースか」が分かる文字列 */
		FString Describe(const FPuzzleCase& Case)
		{
			return FString::Printf(TEXT("%s の cases[%d]（N=%d / M=%d / seed=%u / allowRotation=%s）"),
				*Case.FileName, Case.CaseIndex, Case.N, Case.M, Case.Seed,
				Case.bAllowRotation ? TEXT("true") : TEXT("false"));
		}

		/**
		 * index.json の combinations（25 件）をたどって全ケース（25 × 6 = 150 件）を読む。
		 * 読めなければ false を返し、理由を OutError に入れる。
		 */
		bool LoadPuzzleCases(TArray<FPuzzleCase>& OutCases, FString& OutError)
		{
			OutCases.Reset();

			const TSharedPtr<FJsonObject> Index = LoadFixtureIndex(OutError);
			if (!Index.IsValid())
			{
				return false;
			}

			TArray<uint32> PuzzleSeeds;
			if (!ReadUint32Array(Index, TEXT("puzzleSeeds"), PuzzleSeeds, OutError))
			{
				return false;
			}
			if (PuzzleSeeds.Num() == 0)
			{
				OutError = TEXT("index.json の puzzleSeeds が空");
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* Combinations = nullptr;
			if (!Index->TryGetArrayField(TEXT("combinations"), Combinations) || Combinations == nullptr)
			{
				OutError = TEXT("index.json に combinations が無い");
				return false;
			}

			for (int32 ComboIndex = 0; ComboIndex < Combinations->Num(); ++ComboIndex)
			{
				const TSharedPtr<FJsonObject>* Combo = nullptr;
				if (!(*Combinations)[ComboIndex].IsValid() || !(*Combinations)[ComboIndex]->TryGetObject(Combo) || Combo == nullptr || !Combo->IsValid())
				{
					OutError = FString::Printf(TEXT("combinations[%d] がオブジェクトとして読めない"), ComboIndex);
					return false;
				}

				int32 N = 0;
				int32 M = 0;
				FString FileName;
				if (!(*Combo)->TryGetNumberField(TEXT("n"), N) || !(*Combo)->TryGetNumberField(TEXT("m"), M) ||
					!(*Combo)->TryGetStringField(TEXT("file"), FileName))
				{
					OutError = FString::Printf(TEXT("combinations[%d] に n / m / file が揃っていない"), ComboIndex);
					return false;
				}

				const TSharedPtr<FJsonObject> Puzzle = LoadFixtureJson(FileName, OutError);
				if (!Puzzle.IsValid())
				{
					return false;
				}

				// n / m はファイル側にも入っている（Docs/FIXTURES.md）。食い違うならファイルの取り違え
				int32 FileN = 0;
				int32 FileM = 0;
				if (!Puzzle->TryGetNumberField(TEXT("n"), FileN) || !Puzzle->TryGetNumberField(TEXT("m"), FileM))
				{
					OutError = FString::Printf(TEXT("%s に n / m が無い"), *FileName);
					return false;
				}
				if (FileN != N || FileM != M)
				{
					OutError = FString::Printf(TEXT("%s の n / m が index.json と違う: 期待 (%d, %d) / 実際 (%d, %d)"),
						*FileName, N, M, FileN, FileM);
					return false;
				}

				const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
				if (!Puzzle->TryGetArrayField(TEXT("cases"), Cases) || Cases == nullptr)
				{
					OutError = FString::Printf(TEXT("%s に cases が無い"), *FileName);
					return false;
				}

				// ケースは allowRotation の 2 通り × puzzleSeeds の件数（Docs/FIXTURES.md）
				const int32 ExpectedCaseNum = PuzzleSeeds.Num() * 2;
				if (Cases->Num() != ExpectedCaseNum)
				{
					OutError = FString::Printf(TEXT("%s の cases の件数が %d でない: %d"), *FileName, ExpectedCaseNum, Cases->Num());
					return false;
				}

				for (int32 CaseIndex = 0; CaseIndex < Cases->Num(); ++CaseIndex)
				{
					const TSharedPtr<FJsonObject>* CaseObject = nullptr;
					if (!(*Cases)[CaseIndex].IsValid() || !(*Cases)[CaseIndex]->TryGetObject(CaseObject) || CaseObject == nullptr || !CaseObject->IsValid())
					{
						OutError = FString::Printf(TEXT("%s の cases[%d] がオブジェクトとして読めない"), *FileName, CaseIndex);
						return false;
					}

					FPuzzleCase Case;
					Case.N = N;
					Case.M = M;
					Case.FileName = FileName;
					Case.CaseIndex = CaseIndex;
					Case.Json = *CaseObject;

					if (!ReadUint32Field(*CaseObject, TEXT("seed"), Case.Seed, OutError))
					{
						OutError = FString::Printf(TEXT("%s の cases[%d] の %s"), *FileName, CaseIndex, *OutError);
						return false;
					}
					if (!(*CaseObject)->TryGetBoolField(TEXT("allowRotation"), Case.bAllowRotation))
					{
						OutError = FString::Printf(TEXT("%s の cases[%d] に allowRotation が無い"), *FileName, CaseIndex);
						return false;
					}

					// 並びは allowRotation が false → true、その中で puzzleSeeds の順（Docs/FIXTURES.md）。
					// ここがずれていると比べる相手を取り違えるので、読んだ時点で確かめる
					const bool bExpectedAllowRotation = CaseIndex >= PuzzleSeeds.Num();
					const uint32 ExpectedSeed = PuzzleSeeds[CaseIndex % PuzzleSeeds.Num()];
					if (Case.bAllowRotation != bExpectedAllowRotation || Case.Seed != ExpectedSeed)
					{
						OutError = FString::Printf(
							TEXT("%s の cases[%d] の並びが想定と違う: 期待 (seed=%u, allowRotation=%s) / 実際 (seed=%u, allowRotation=%s)"),
							*FileName, CaseIndex, ExpectedSeed, bExpectedAllowRotation ? TEXT("true") : TEXT("false"),
							Case.Seed, Case.bAllowRotation ? TEXT("true") : TEXT("false"));
						return false;
					}

					OutCases.Add(MoveTemp(Case));
				}
			}

			return true;
		}

		/** 1 ケース分の pieces と solution を比べる。食い違ったら AddError して false（そのケースの残りは見ない） */
		bool CheckPiecesAndSolution(FAutomationTestBase& Test, const FPuzzleCase& Case)
		{
			FString Error;

			TArray<Cubelith::FPiece> ExpectedPieces;
			if (!ReadPieceArray(Case.Json, TEXT("pieces"), ExpectedPieces, Error))
			{
				Test.AddError(FString::Printf(TEXT("%s: %s"), *Describe(Case), *Error));
				return false;
			}

			TArray<Cubelith::FPlacement> ExpectedSolution;
			if (!ReadPlacementArray(Case.Json, TEXT("solution"), ExpectedSolution, Error))
			{
				Test.AddError(FString::Printf(TEXT("%s: %s"), *Describe(Case), *Error));
				return false;
			}

			const Cubelith::FGeneratedPuzzle Puzzle = Cubelith::GeneratePuzzle(Case.N, Case.M, Case.Seed);

			if (Puzzle.Pieces.Num() != Case.M || Puzzle.Solution.Num() != Case.M)
			{
				Test.AddError(FString::Printf(TEXT("%s: 生成結果の件数が M=%d でない（pieces %d / solution %d）"),
					*Describe(Case), Case.M, Puzzle.Pieces.Num(), Puzzle.Solution.Num()));
				return false;
			}
			if (ExpectedPieces.Num() != Case.M || ExpectedSolution.Num() != Case.M)
			{
				Test.AddError(FString::Printf(TEXT("%s: 照合データの件数が M=%d でない（pieces %d / solution %d）"),
					*Describe(Case), Case.M, ExpectedPieces.Num(), ExpectedSolution.Num()));
				return false;
			}

			// pieces: id と、局所座標のボクセルの並び（座標の昇順 x → y → z。Docs/FIXTURES.md）
			for (int32 Index = 0; Index < Case.M; ++Index)
			{
				const Cubelith::FPiece& Expected = ExpectedPieces[Index];
				const Cubelith::FPiece& Actual = Puzzle.Pieces[Index];

				if (Expected.Id != Index || Actual.Id != Index)
				{
					Test.AddError(FString::Printf(TEXT("%s: pieces[%d].id が %d でない（照合データ %d / 生成結果 %d）"),
						*Describe(Case), Index, Index, Expected.Id, Actual.Id));
					return false;
				}
				if (Expected.Voxels.Num() != Actual.Voxels.Num())
				{
					Test.AddError(FString::Printf(TEXT("%s: pieces[%d].voxels の件数が違う（照合データ %d / 生成結果 %d）"),
						*Describe(Case), Index, Expected.Voxels.Num(), Actual.Voxels.Num()));
					return false;
				}
				for (int32 VoxelIndex = 0; VoxelIndex < Expected.Voxels.Num(); ++VoxelIndex)
				{
					const Cubelith::FVec3& E = Expected.Voxels[VoxelIndex];
					const Cubelith::FVec3& A = Actual.Voxels[VoxelIndex];
					if (A != E)
					{
						Test.AddError(FString::Printf(TEXT("%s: pieces[%d].voxels[%d] が違う（照合データ (%d,%d,%d) / 生成結果 (%d,%d,%d)）"),
							*Describe(Case), Index, VoxelIndex, E.X, E.Y, E.Z, A.X, A.Y, A.Z));
						return false;
					}
				}
			}

			// solution: pieceId・orientation（全ピース 0）・position
			for (int32 Index = 0; Index < Case.M; ++Index)
			{
				const Cubelith::FPlacement& Expected = ExpectedSolution[Index];
				const Cubelith::FPlacement& Actual = Puzzle.Solution[Index];
				if (Actual != Expected)
				{
					Test.AddError(FString::Printf(
						TEXT("%s: solution[%d] が違う（照合データ {id %d, 向き %d, (%d,%d,%d)} / 生成結果 {id %d, 向き %d, (%d,%d,%d)}）"),
						*Describe(Case), Index,
						Expected.PieceId, Expected.Orientation, Expected.Position.X, Expected.Position.Y, Expected.Position.Z,
						Actual.PieceId, Actual.Orientation, Actual.Position.X, Actual.Position.Y, Actual.Position.Z));
					return false;
				}
				if (Expected.PieceId != Index || Expected.Orientation != Cubelith::IdentityOrientation)
				{
					Test.AddError(FString::Printf(TEXT("%s: 照合データの solution[%d] が id %d / 向き 0 でない（id %d / 向き %d）"),
						*Describe(Case), Index, Index, Expected.PieceId, Expected.Orientation));
					return false;
				}
			}

			// Docs/FIXTURES.md「解答の絶対座標はここには無い」の性質:
			// 照合データの voxel + position（向きは恒等）が立方体 [0, N-1]³ を隙間なく覆う
			const int32 Total = Case.N * Case.N * Case.N;
			TSet<Cubelith::FVec3> Covered;
			Covered.Reserve(Total);
			int32 VoxelCount = 0;
			for (int32 Index = 0; Index < Case.M; ++Index)
			{
				for (const Cubelith::FVec3& Voxel : ExpectedPieces[Index].Voxels)
				{
					const Cubelith::FVec3 World = Cubelith::AddVec3(Voxel, ExpectedSolution[Index].Position);
					if (World.X < 0 || World.X >= Case.N || World.Y < 0 || World.Y >= Case.N || World.Z < 0 || World.Z >= Case.N)
					{
						Test.AddError(FString::Printf(TEXT("%s: pieces[%d] の解答の絶対座標 (%d,%d,%d) が [0, %d]³ の外"),
							*Describe(Case), Index, World.X, World.Y, World.Z, Case.N - 1));
						return false;
					}
					Covered.Add(World);
					++VoxelCount;
				}
			}
			if (VoxelCount != Total || Covered.Num() != Total)
			{
				Test.AddError(FString::Printf(TEXT("%s: 解答の絶対座標が立方体を隙間なく覆っていない（ボクセル %d 個 / 相異なる %d 個 / 期待 %d 個）"),
					*Describe(Case), VoxelCount, Covered.Num(), Total));
				return false;
			}

			return true;
		}

		/**
		 * 配置の配列（M 件）を照合データの値と 1 件ずつ比べる。
		 * 食い違ったら AddError して false（そのケースの残りは見ない）
		 */
		bool ComparePlacements(FAutomationTestBase& Test, const FPuzzleCase& Case, const TCHAR* FieldName,
			const TArray<Cubelith::FPlacement>& Expected, const TArray<Cubelith::FPlacement>& Actual)
		{
			if (Expected.Num() != Case.M || Actual.Num() != Case.M)
			{
				Test.AddError(FString::Printf(TEXT("%s: %s の件数が M=%d でない（照合データ %d / 生成結果 %d）"),
					*Describe(Case), FieldName, Case.M, Expected.Num(), Actual.Num()));
				return false;
			}

			for (int32 Index = 0; Index < Case.M; ++Index)
			{
				const Cubelith::FPlacement& E = Expected[Index];
				const Cubelith::FPlacement& A = Actual[Index];

				// pieceId / orientation / position を並びも含めて比べる
				if (A != E)
				{
					Test.AddError(FString::Printf(
						TEXT("%s: %s[%d] が違う（照合データ {id %d, 向き %d, (%d,%d,%d)} / 生成結果 {id %d, 向き %d, (%d,%d,%d)}）"),
						*Describe(Case), FieldName, Index,
						E.PieceId, E.Orientation, E.Position.X, E.Position.Y, E.Position.Z,
						A.PieceId, A.Orientation, A.Position.X, A.Position.Y, A.Position.Z));
					return false;
				}
				// 並びは Pieces の並び（id の昇順。Docs/FIXTURES.md「共通の表し方」）
				if (E.PieceId != Index)
				{
					Test.AddError(FString::Printf(TEXT("%s: 照合データの %s[%d].pieceId が %d（期待 %d）"),
						*Describe(Case), FieldName, Index, E.PieceId, Index));
					return false;
				}
				// allowRotation が false のケースは全ピースの向きが 0（Docs/FIXTURES.md）
				if (!Case.bAllowRotation && E.Orientation != Cubelith::IdentityOrientation)
				{
					Test.AddError(FString::Printf(TEXT("%s: allowRotation が false なのに %s[%d] の向きが %d"),
						*Describe(Case), FieldName, Index, E.Orientation));
					return false;
				}
			}

			return true;
		}

		/**
		 * 1 ケース分の scatter と reshuffleWithoutHints を比べる（Docs/FIXTURES.md「各ケースの作り方」の 2 と 3）。
		 * 食い違ったら AddError して false（そのケースの残りは見ない）
		 */
		bool CheckScatter(FAutomationTestBase& Test, const FPuzzleCase& Case)
		{
			FString Error;

			TArray<Cubelith::FPlacement> ExpectedScatter;
			if (!ReadPlacementArray(Case.Json, TEXT("scatter"), ExpectedScatter, Error))
			{
				Test.AddError(FString::Printf(TEXT("%s: %s"), *Describe(Case), *Error));
				return false;
			}

			TArray<Cubelith::FPlacement> ExpectedReshuffle;
			if (!ReadPlacementArray(Case.Json, TEXT("reshuffleWithoutHints"), ExpectedReshuffle, Error))
			{
				Test.AddError(FString::Printf(TEXT("%s: %s"), *Describe(Case), *Error));
				return false;
			}

			// 1: generatePuzzle(n, m, seed) の pieces
			const TArray<Cubelith::FPiece> Pieces = Cubelith::GeneratePieces(Case.N, Case.M, Case.Seed);

			// 2: scatterPlacements(pieces, n, seed, { allowRotation })
			Cubelith::FScatterOptions ScatterOptions;
			ScatterOptions.bAllowRotation = Case.bAllowRotation;
			const TArray<Cubelith::FPlacement> Scatter =
				Cubelith::ScatterPlacements(Pieces, Case.N, Case.Seed, ScatterOptions);

			// 3: scatterPlacements(pieces, n, seed, { allowRotation, keep: [] })。
			// keep の空配列は省略と同じ意味だが、TS の呼び出しの形を写すために別のオプションを作って渡す
			Cubelith::FScatterOptions ReshuffleOptions;
			ReshuffleOptions.bAllowRotation = Case.bAllowRotation;
			ReshuffleOptions.Keep.Reset();
			const TArray<Cubelith::FPlacement> Reshuffle =
				Cubelith::ScatterPlacements(Pieces, Case.N, Case.Seed, ReshuffleOptions);

			if (!ComparePlacements(Test, Case, TEXT("scatter"), ExpectedScatter, Scatter))
			{
				return false;
			}
			// RULES.md 3.3「やり直し」のとおり scatter と同じ配置になるが、
			// scatter と等しいことを見るだけにせず JSON の値と比べる（Docs/FIXTURES.md）
			if (!ComparePlacements(Test, Case, TEXT("reshuffleWithoutHints"), ExpectedReshuffle, Reshuffle))
			{
				return false;
			}

			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPuzzleFixturesPiecesAndSolutionTest, "CUBELITH.Core.Fixtures.PuzzlePiecesAndSolution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPuzzleFixturesPiecesAndSolutionTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	FString Error;
	TArray<PuzzleFixturesDetail::FPuzzleCase> Cases;
	if (!PuzzleFixturesDetail::LoadPuzzleCases(Cases, Error))
	{
		AddError(Error);
		return false;
	}

	// 25 ファイル（N と M のプリセットの組み合わせ）× 6 ケース（Docs/FIXTURES.md）
	TestEqual(TEXT("ケースの総数"), Cases.Num(), 150);

	// pieces と solution は allowRotation に依らないが、手を抜かず 6 ケースそれぞれで JSON の値と比べる
	for (const PuzzleFixturesDetail::FPuzzleCase& Case : Cases)
	{
		PuzzleFixturesDetail::CheckPiecesAndSolution(*this, Case);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCubelithPuzzleFixturesScatterTest, "CUBELITH.Core.Fixtures.PuzzleScatter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCubelithPuzzleFixturesScatterTest::RunTest(const FString& Parameters)
{
	using namespace CubelithCoreTests;

	FString Error;
	TArray<PuzzleFixturesDetail::FPuzzleCase> Cases;
	if (!PuzzleFixturesDetail::LoadPuzzleCases(Cases, Error))
	{
		AddError(Error);
		return false;
	}

	// 25 ファイル（N と M のプリセットの組み合わせ）× 6 ケース（Docs/FIXTURES.md）
	TestEqual(TEXT("ケースの総数"), Cases.Num(), 150);

	// scatter は allowRotation とシードで変わるので、6 ケースそれぞれで JSON の値と比べる
	for (const PuzzleFixturesDetail::FPuzzleCase& Case : Cases)
	{
		PuzzleFixturesDetail::CheckScatter(*this, Case);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
