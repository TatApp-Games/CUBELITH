#include "Piece.h"

namespace Cubelith
{
	namespace
	{
		// 重心との距離を比べるときの許容誤差（TS の DISTANCE_EPSILON）。
		// 重心は有理数なので厳密な同点が浮動小数で僅かにずれる。
		constexpr double DistanceEpsilon = 1e-9;
	}

	FVec3 LocalOrigin(TArrayView<const FVec3> Voxels)
	{
		checkf(Voxels.Num() > 0, TEXT("LocalOrigin: ボクセル集合が空"));
		const FVec3 First = Voxels[0];

		// 重心は double で計算する（TS の number は倍精度。Docs/SPEC_UE.md 7.1）。
		// 整数のまま分母を払う形に書き換えると DistanceEpsilon の意味が変わるので、TS と同じ浮動小数の計算を保つ
		double SumX = 0.0;
		double SumY = 0.0;
		double SumZ = 0.0;
		for (const FVec3& V : Voxels)
		{
			SumX += static_cast<double>(V.X);
			SumY += static_cast<double>(V.Y);
			SumZ += static_cast<double>(V.Z);
		}
		const double Count = static_cast<double>(Voxels.Num());
		const double CenterX = SumX / Count;
		const double CenterY = SumY / Count;
		const double CenterZ = SumZ / Count;

		const auto SquaredDistance = [CenterX, CenterY, CenterZ](const FVec3& V) -> double
		{
			const double Dx = static_cast<double>(V.X) - CenterX;
			const double Dy = static_cast<double>(V.Y) - CenterY;
			const double Dz = static_cast<double>(V.Z) - CenterZ;
			return Dx * Dx + Dy * Dy + Dz * Dz;
		};

		// 比較の形は TS のまま（2 段の非対称な書き方が同点の選び方を決めているので、きれいに書き直さない）
		FVec3 Best = First;
		double BestDistance = SquaredDistance(First);
		for (const FVec3& V : Voxels)
		{
			const double Distance = SquaredDistance(V);
			if (Distance < BestDistance - DistanceEpsilon)
			{
				Best = V;
				BestDistance = Distance;
			}
			else if (Distance <= BestDistance + DistanceEpsilon && CompareVec3(V, Best) < 0)
			{
				Best = V;
				BestDistance = FMath::Min(BestDistance, Distance);
			}
		}

		return Vec3(Best.X, Best.Y, Best.Z);
	}

	FPiece NormalizePiece(const FPiece& Piece)
	{
		const FVec3 Origin = LocalOrigin(Piece.Voxels);

		FPiece Result;
		Result.Id = Piece.Id;
		Result.Voxels.Reserve(Piece.Voxels.Num());
		for (const FVec3& V : Piece.Voxels)
		{
			Result.Voxels.Add(SubVec3(V, Origin));
		}
		return Result;
	}

	FPiece CreatePiece(int32 Id, TArrayView<const FVec3> Voxels)
	{
		FPiece Piece;
		Piece.Id = Id;
		Piece.Voxels.Append(Voxels.GetData(), Voxels.Num());
		return NormalizePiece(Piece);
	}

	TArray<FVec3> PlacedVoxels(const FPiece& Piece, const FPlacement& Placement)
	{
		checkf(Piece.Id == Placement.PieceId, TEXT("PlacedVoxels: ピース id が一致しない (piece %d / placement %d)"),
			Piece.Id, Placement.PieceId);

		TArray<FVec3> Result;
		Result.Reserve(Piece.Voxels.Num());
		for (const FVec3& V : Piece.Voxels)
		{
			Result.Add(AddVec3(RotateVoxel(V, Placement.Orientation), Placement.Position));
		}
		return Result;
	}

	FBoundingBox BoundingBox(TArrayView<const FVec3> Voxels)
	{
		checkf(Voxels.Num() > 0, TEXT("BoundingBox: ボクセル集合が空"));
		const FVec3 First = Voxels[0];

		int32 MinX = First.X;
		int32 MinY = First.Y;
		int32 MinZ = First.Z;
		int32 MaxX = First.X;
		int32 MaxY = First.Y;
		int32 MaxZ = First.Z;
		for (const FVec3& V : Voxels)
		{
			if (V.X < MinX) { MinX = V.X; }
			if (V.Y < MinY) { MinY = V.Y; }
			if (V.Z < MinZ) { MinZ = V.Z; }
			if (V.X > MaxX) { MaxX = V.X; }
			if (V.Y > MaxY) { MaxY = V.Y; }
			if (V.Z > MaxZ) { MaxZ = V.Z; }
		}

		FBoundingBox Box;
		Box.Min = Vec3(MinX, MinY, MinZ);
		Box.Max = Vec3(MaxX, MaxY, MaxZ);
		Box.Size = Vec3(MaxX - MinX + 1, MaxY - MinY + 1, MaxZ - MinZ + 1);
		return Box;
	}
}
