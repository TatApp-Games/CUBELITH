import { describe, expect, it } from 'vitest';
import {
  addVec3,
  IDENTITY_ORIENTATION,
  ORIENTATION_COUNT,
  rotateOrientation,
  rotateVoxel,
  vec3,
  vec3Key,
  type Vec3,
} from '../src/core/grid';
import {
  boundingBox,
  createPiece,
  localOrigin,
  normalizePiece,
  placedVoxels,
  type Piece,
  type Placement,
} from '../src/core/piece';

const ALL_ORIENTATIONS: readonly number[] = Array.from({ length: ORIENTATION_COUNT }, (_, i) => i);

/** L 字のテトロミノ。重心に最も近いボクセルが一意に決まる。 */
const L_TETROMINO: readonly Vec3[] = [vec3(0, 0, 0), vec3(1, 0, 0), vec3(2, 0, 0), vec3(2, 1, 0)];

describe('localOrigin', () => {
  it('重心に最も近いボクセルを返す（直線の 3 連）', () => {
    expect(localOrigin([vec3(0, 0, 0), vec3(1, 0, 0), vec3(2, 0, 0)])).toEqual(vec3(1, 0, 0));
  });

  it('重心に最も近いボクセルを返す（L 字の 4 連）', () => {
    // 重心は (1.25, 0.25, 0)。距離の 2 乗は 1.625 / 0.125 / 0.625 / 1.125 で (1,0,0) が最小
    expect(localOrigin(L_TETROMINO)).toEqual(vec3(1, 0, 0));
  });

  it('同点のときは辞書順で最小のボクセルを選ぶ', () => {
    expect(localOrigin([vec3(1, 0, 0), vec3(0, 0, 0)])).toEqual(vec3(0, 0, 0));
    expect(localOrigin([vec3(0, 0, 6), vec3(0, 0, 5)])).toEqual(vec3(0, 0, 5));
    // 2x2 の正方形は 4 つとも重心から等距離
    const square = [vec3(1, 1, 0), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 0)];
    expect(localOrigin(square)).toEqual(vec3(0, 0, 0));
  });

  it('平行移動しても同じボクセル（を平行移動したもの）を選ぶ', () => {
    const shift = vec3(-3, 7, 2);
    const moved = L_TETROMINO.map((v) => addVec3(v, shift));
    expect(localOrigin(moved)).toEqual(addVec3(localOrigin(L_TETROMINO), shift));
  });

  it('入力の順番を変えても結果が変わらない', () => {
    const reversed = [...L_TETROMINO].reverse();
    expect(localOrigin(reversed)).toEqual(localOrigin(L_TETROMINO));
  });

  it('空のボクセル集合は RangeError', () => {
    expect(() => localOrigin([])).toThrow(RangeError);
  });
});

describe('normalizePiece / createPiece', () => {
  it('局所原点が (0,0,0) に来るよう平行移動する', () => {
    const piece = normalizePiece({ id: 3, voxels: L_TETROMINO });
    expect(piece.id).toBe(3);
    expect(piece.voxels).toEqual([vec3(-1, 0, 0), vec3(0, 0, 0), vec3(1, 0, 0), vec3(1, 1, 0)]);
  });

  it('正規化後は必ず原点のボクセルを含み、局所原点も原点になる', () => {
    const inputs: readonly Vec3[][] = [
      [...L_TETROMINO],
      [vec3(5, 5, 5)],
      [vec3(0, 0, 0), vec3(0, 1, 0), vec3(0, 2, 0), vec3(0, 2, 1), vec3(1, 2, 1)],
      [vec3(-4, -4, -4), vec3(-3, -4, -4)],
    ];
    for (const voxels of inputs) {
      const piece = createPiece(1, voxels);
      expect(piece.voxels.map(vec3Key)).toContain(vec3Key(vec3(0, 0, 0)));
      expect(localOrigin(piece.voxels)).toEqual(vec3(0, 0, 0));
    }
  });

  it('同じ入力に対して常に同じ結果を返し、二度かけても変わらない', () => {
    const once = createPiece(0, L_TETROMINO);
    const again = createPiece(0, L_TETROMINO);
    expect(again).toEqual(once);
    expect(normalizePiece(once)).toEqual(once);
  });

  it('平行移動しただけの入力は同じ正規化結果になる', () => {
    const base = createPiece(2, L_TETROMINO);
    const moved = createPiece(2, L_TETROMINO.map((v) => addVec3(v, vec3(10, -20, 30))));
    expect(moved).toEqual(base);
  });

  it('ボクセルの並びと個数、相対位置を保つ', () => {
    const piece = createPiece(9, L_TETROMINO);
    expect(piece.voxels).toHaveLength(L_TETROMINO.length);
    const origin = localOrigin(L_TETROMINO);
    piece.voxels.forEach((v, i) => {
      const source = L_TETROMINO[i];
      expect(source).toBeDefined();
      if (source === undefined) return;
      expect(v).toEqual(vec3(source.x - origin.x, source.y - origin.y, source.z - origin.z));
    });
  });

  it('空のボクセル集合は RangeError', () => {
    expect(() => normalizePiece({ id: 0, voxels: [] })).toThrow(RangeError);
    expect(() => createPiece(0, [])).toThrow(RangeError);
  });
});

describe('placedVoxels', () => {
  const piece: Piece = createPiece(7, [vec3(0, 0, 0), vec3(1, 0, 0)]);

  it('恒等の向きと原点では局所座標のまま', () => {
    const placement: Placement = {
      pieceId: 7,
      orientation: IDENTITY_ORIENTATION,
      position: vec3(0, 0, 0),
    };
    expect(placedVoxels(piece, placement)).toEqual([vec3(0, 0, 0), vec3(1, 0, 0)]);
  });

  it('恒等の向きでは position をそのまま加算する', () => {
    const placement: Placement = {
      pieceId: 7,
      orientation: IDENTITY_ORIENTATION,
      position: vec3(2, 3, 4),
    };
    expect(placedVoxels(piece, placement)).toEqual([vec3(2, 3, 4), vec3(3, 3, 4)]);
  });

  it('向きを適用してから position を加算する（手で並べた期待値）', () => {
    // Rz(+90): (x, y, z) -> (-y, x, z) なので (1,0,0) は (0,1,0) に写る
    const placement: Placement = {
      pieceId: 7,
      orientation: rotateOrientation(IDENTITY_ORIENTATION, 'z', 1),
      position: vec3(2, 3, 4),
    };
    expect(placedVoxels(piece, placement)).toEqual([vec3(2, 3, 4), vec3(2, 4, 4)]);
  });

  it('L 字を Y 軸に 90 度回した結果が期待どおり', () => {
    // Ry(+90): (x, y, z) -> (z, y, -x)。正規化済みの L 字は (-1,0,0) (0,0,0) (1,0,0) (1,1,0)
    const l = createPiece(0, L_TETROMINO);
    const placement: Placement = {
      pieceId: 0,
      orientation: rotateOrientation(IDENTITY_ORIENTATION, 'y', 1),
      position: vec3(1, 1, 1),
    };
    expect(placedVoxels(l, placement)).toEqual([
      vec3(1, 1, 2),
      vec3(1, 1, 1),
      vec3(1, 1, 0),
      vec3(1, 2, 0),
    ]);
  });

  it('position をずらすと結果も同じだけ平行移動する', () => {
    const delta = vec3(-5, 2, 8);
    for (const orientation of ALL_ORIENTATIONS) {
      const base = placedVoxels(piece, { pieceId: 7, orientation, position: vec3(0, 0, 0) });
      const moved = placedVoxels(piece, { pieceId: 7, orientation, position: delta });
      expect(moved).toEqual(base.map((v) => addVec3(v, delta)));
    }
  });

  it('どの向きでもボクセル数は変わらず、rotateVoxel + 加算と一致する', () => {
    const l = createPiece(4, L_TETROMINO);
    const position = vec3(3, -1, 2);
    for (const orientation of ALL_ORIENTATIONS) {
      const placed = placedVoxels(l, { pieceId: 4, orientation, position });
      expect(placed).toHaveLength(l.voxels.length);
      expect(new Set(placed.map(vec3Key)).size).toBe(l.voxels.length);
      expect(placed).toEqual(l.voxels.map((v) => addVec3(rotateVoxel(v, orientation), position)));
    }
  });

  it('ピース id と配置の id が食い違ったら例外', () => {
    expect(() =>
      placedVoxels(piece, { pieceId: 8, orientation: IDENTITY_ORIENTATION, position: vec3(0, 0, 0) }),
    ).toThrow();
  });
});

describe('boundingBox', () => {
  it('min / max / size を返す', () => {
    const box = boundingBox([vec3(-1, 0, 0), vec3(0, 0, 0), vec3(1, 0, 0), vec3(1, 1, 0)]);
    expect(box.min).toEqual(vec3(-1, 0, 0));
    expect(box.max).toEqual(vec3(1, 1, 0));
    expect(box.size).toEqual(vec3(3, 2, 1));
  });

  it('ボクセル 1 つなら size は (1,1,1)', () => {
    const box = boundingBox([vec3(4, -2, 7)]);
    expect(box.min).toEqual(vec3(4, -2, 7));
    expect(box.max).toEqual(vec3(4, -2, 7));
    expect(box.size).toEqual(vec3(1, 1, 1));
  });

  it('回転しても size の 3 成分は入れ替わるだけ', () => {
    const base = boundingBox(L_TETROMINO);
    const baseSorted = [base.size.x, base.size.y, base.size.z].sort((a, b) => a - b);
    for (const orientation of ALL_ORIENTATIONS) {
      const rotated = boundingBox(L_TETROMINO.map((v) => rotateVoxel(v, orientation)));
      expect([rotated.size.x, rotated.size.y, rotated.size.z].sort((a, b) => a - b)).toEqual(
        baseSorted,
      );
    }
  });

  it('空のボクセル集合は RangeError', () => {
    expect(() => boundingBox([])).toThrow(RangeError);
  });
});
