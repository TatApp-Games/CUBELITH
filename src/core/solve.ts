// クリア判定（SPEC.md 3.4）。物理判定は使わず、配列のインデックス計算だけで行う。
// Three.js に依存しない純粋なロジック。

import { addVec3, compareVec3, vec3, type Vec3 } from './grid';
import { boundingBox, placedVoxels, type Piece, type Placement } from './piece';

/**
 * 全ピースが N×N×N の立方体にぴったり収まっているか。
 *
 * 変換後のボクセル総数が N³、重複が無く、外接ボックスの各辺がちょうど N ならクリア。
 * 立方体の位置（原点）は問わない。ピース群がどこにあってもよい。
 *
 * pieces と placements はピース id で対応付ける（並び順は問わない）。
 * id の重複・未知の id・配置漏れ / 二重配置は呼び出し側のバグなので Error を投げる。
 */
export function isSolved(
  pieces: readonly Piece[],
  placements: readonly Placement[],
  n: number,
): boolean {
  if (!Number.isInteger(n) || n < 1) {
    throw new RangeError(`空間サイズ N は 1 以上の整数 (got ${String(n)})`);
  }

  const byId = new Map<number, Piece>();
  for (const piece of pieces) {
    if (byId.has(piece.id)) throw new Error(`クリア判定: ピース id ${piece.id} が重複している`);
    byId.set(piece.id, piece);
  }
  if (placements.length !== byId.size) {
    throw new Error(
      `クリア判定: ピース数 ${byId.size} と配置数 ${placements.length} が一致しない`,
    );
  }

  // ボクセル総数が N³ でなければ、置き方によらずクリアにはなり得ない
  const total = n * n * n;
  let count = 0;
  for (const piece of pieces) count += piece.voxels.length;
  if (count !== total) return false;

  // 各配置をワールドのグリッド座標に変換して集める
  const world: Vec3[] = [];
  const placed = new Set<number>();
  for (const placement of placements) {
    const piece = byId.get(placement.pieceId);
    if (piece === undefined) {
      throw new Error(`クリア判定: 未知のピース id ${placement.pieceId}`);
    }
    if (placed.has(placement.pieceId)) {
      throw new Error(`クリア判定: ピース id ${placement.pieceId} の配置が重複している`);
    }
    placed.add(placement.pieceId);
    for (const v of placedVoxels(piece, placement)) world.push(v);
  }

  // 外接ボックスが N×N×N でなければ、はみ出しか隙間がある
  const box = boundingBox(world);
  if (box.size.x !== n || box.size.y !== n || box.size.z !== n) return false;

  // ここまで来れば全ボクセルは箱の中。重複が 1 つも無ければ N³ マスをちょうど埋めている
  const seen: boolean[] = new Array<boolean>(total).fill(false);
  for (const v of world) {
    const index = ((v.x - box.min.x) * n + (v.y - box.min.y)) * n + (v.z - box.min.z);
    if (seen[index] === true) return false;
    seen[index] = true;
  }
  return true;
}

// ---- マグネット・スナップの候補（SPEC.md 3.5） ----

/**
 * 候補にする平行移動の範囲（各軸 -1 / 0 / +1 の 27 通り）。
 * SPEC.md 3.5 の「半マス〜1 マス以内」を、グリッド単位の実装では 1 マス以内と読む。
 */
const SNAP_RANGE = 1;

/** 6 近傍のオフセット。「少なくとも 1 面で接する」の判定に使う。 */
const FACE_NEIGHBORS: readonly Vec3[] = [
  vec3(1, 0, 0),
  vec3(-1, 0, 0),
  vec3(0, 1, 0),
  vec3(0, -1, 0),
  vec3(0, 0, 1),
  vec3(0, 0, -1),
];

/** ボクセル座標の Set キー。grid の vec3Key と同じ形式だが、成分を直接渡せるようにしてある。 */
function voxelKey(x: number, y: number, z: number): string {
  return `${x},${y},${z}`;
}

/** 原点からのマンハッタン距離。候補の「近さ」の尺度。 */
function manhattan(v: Vec3): number {
  return Math.abs(v.x) + Math.abs(v.y) + Math.abs(v.z);
}

/**
 * 候補の平行移動を「近い順」に並べた表。
 * マンハッタン距離の小さい順、同点は座標の辞書順（x → y → z）。
 * 先頭から見て最初に条件を満たしたものを採れば「最も近いもの」を決定的に選べる。
 */
const SNAP_OFFSETS: readonly Vec3[] = ((): readonly Vec3[] => {
  const offsets: Vec3[] = [];
  for (let x = -SNAP_RANGE; x <= SNAP_RANGE; x += 1) {
    for (let y = -SNAP_RANGE; y <= SNAP_RANGE; y += 1) {
      for (let z = -SNAP_RANGE; z <= SNAP_RANGE; z += 1) offsets.push(vec3(x, y, z));
    }
  }
  offsets.sort((a, b): number => {
    const distance = manhattan(a) - manhattan(b);
    return distance !== 0 ? distance : compareVec3(a, b);
  });
  return offsets;
})();

/**
 * アクティブなピースの吸着先を返す（SPEC.md 3.5）。無ければ null。
 *
 * 現在位置から各軸 -1 / 0 / +1 の平行移動を試し、次をすべて満たす位置を候補にする。
 *   1. 他のピースと少なくとも 1 面で接する（6 近傍で隣り合うボクセルの組が 1 つ以上ある）
 *   2. 他のピースと重ならない
 *   3. 全ピースのボクセルの外接立方体が N×N×N に収まる（各辺が N 以下。原点は問わない）
 * 複数あればマンハッタン距離が最小のもの、同点は座標の辞書順で決定的に選ぶ。
 *
 * 向きは変えない。解釈: SPEC.md 3.5 は位置についてのみ述べており、回転を伴う吸着は書かれていない。
 * 解釈: 「既に置かれている他のピース」を区別する状態は持たないので、アクティブ以外の全ピースを相手にする。
 * 現在位置がそのまま条件を満たすときは移動量 0 の候補（＝現在の配置と同じ位置）を返す。
 *
 * placements に含まれるピースだけを対象にする。id の重複・未知の id・
 * アクティブなピースの配置漏れは呼び出し側のバグなので Error を投げる。
 */
export function snapCandidate(
  pieces: readonly Piece[],
  placements: readonly Placement[],
  activePieceId: number,
  n: number,
): Placement | null {
  if (!Number.isInteger(n) || n < 1) {
    throw new RangeError(`空間サイズ N は 1 以上の整数 (got ${String(n)})`);
  }

  const byId = new Map<number, Piece>();
  for (const piece of pieces) {
    if (byId.has(piece.id)) throw new Error(`スナップ候補: ピース id ${piece.id} が重複している`);
    byId.set(piece.id, piece);
  }

  // アクティブなピースの配置と、それ以外のピースのワールドボクセルを 1 度の走査で集める
  let active: Placement | undefined;
  const others: Vec3[] = [];
  const seen = new Set<number>();
  for (const placement of placements) {
    const piece = byId.get(placement.pieceId);
    if (piece === undefined) {
      throw new Error(`スナップ候補: 未知のピース id ${placement.pieceId}`);
    }
    if (seen.has(placement.pieceId)) {
      throw new Error(`スナップ候補: ピース id ${placement.pieceId} の配置が重複している`);
    }
    seen.add(placement.pieceId);
    if (placement.pieceId === activePieceId) {
      active = placement;
      continue;
    }
    for (const v of placedVoxels(piece, placement)) others.push(v);
  }
  if (active === undefined) {
    throw new Error(`スナップ候補: アクティブなピース id ${activePieceId} の配置が無い`);
  }
  const activePiece = byId.get(activePieceId);
  if (activePiece === undefined) {
    throw new Error(`スナップ候補: 未知のピース id ${activePieceId}`);
  }
  // 接する相手がいなければ条件 1 を満たしようがない
  if (others.length === 0) return null;

  const activeVoxels = placedVoxels(activePiece, active);
  if (activeVoxels.length === 0) return null;

  // 相手のボクセルは候補ごとに変わらないので、集合と外接ボックスは 1 度だけ作って使い回す
  // （N=7 / M=40 でドラッグ中に毎回呼ばれても重くならないように）
  const occupied = new Set<string>();
  for (const v of others) occupied.add(voxelKey(v.x, v.y, v.z));
  const otherBox = boundingBox(others);
  // アクティブなピースの外接ボックスは平行移動で丸ごと動くだけなので、これも先に求めておく
  const activeBox = boundingBox(activeVoxels);

  /** 条件 3: 相手とアクティブを合わせた外接立方体が N×N×N に収まるか。 */
  const fitsInSpace = (offset: Vec3): boolean => {
    const spanX =
      Math.max(otherBox.max.x, activeBox.max.x + offset.x) -
      Math.min(otherBox.min.x, activeBox.min.x + offset.x);
    const spanY =
      Math.max(otherBox.max.y, activeBox.max.y + offset.y) -
      Math.min(otherBox.min.y, activeBox.min.y + offset.y);
    const spanZ =
      Math.max(otherBox.max.z, activeBox.max.z + offset.z) -
      Math.min(otherBox.min.z, activeBox.min.z + offset.z);
    return spanX + 1 <= n && spanY + 1 <= n && spanZ + 1 <= n;
  };

  /** 条件 1 と 2: 重ならず、かつどこかで 1 面接するか。 */
  const fitsAgainstOthers = (offset: Vec3): boolean => {
    let touches = false;
    for (const v of activeVoxels) {
      const x = v.x + offset.x;
      const y = v.y + offset.y;
      const z = v.z + offset.z;
      if (occupied.has(voxelKey(x, y, z))) return false;
      if (touches) continue;
      for (const d of FACE_NEIGHBORS) {
        if (occupied.has(voxelKey(x + d.x, y + d.y, z + d.z))) {
          touches = true;
          break;
        }
      }
    }
    return touches;
  };

  for (const offset of SNAP_OFFSETS) {
    // 判定は軽い順に。外接立方体だけなら比較 6 回で済む
    if (!fitsInSpace(offset)) continue;
    if (!fitsAgainstOthers(offset)) continue;
    return {
      pieceId: activePieceId,
      orientation: active.orientation,
      position: addVec3(active.position, offset),
    };
  }
  return null;
}
