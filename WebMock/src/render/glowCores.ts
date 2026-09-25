// 内部発光コア（SPEC.md 4 章）。各ボクセルの中心に置く小さな発光メッシュを、
// **全ピース分まとめて 1 つの InstancedMesh** で描く（ドローコールを増やさない）。
// 平常時はピースごとに位相をずらしたサイン波で呼吸させ、クリア演出（SPEC.md 5.2-1）では
// 位相差を消して全ボクセルを連動させたまま最大まで上げる。

import * as THREE from 'three';
import type { Piece } from '../core/piece';

/**
 * コアの半径。
 * 解釈: SPEC.md に大きさの指定は無い。ボクセル（1 辺 VOXEL_SIZE = 0.96）の中に完全に埋まり、
 * かつすりガラス越しに「芯が光っている」と分かる程度として 1/6 マス弱にする。
 */
const CORE_RADIUS = 0.17;

/** 呼吸の角速度（rad/s）。1 往復およそ 5 秒の「ゆっくり」した明滅。 */
const BREATH_SPEED = 1.25;

/** 平常時の輝度（0.5 + 0.5 sin t にこれを掛ける）。加算合成なので 1 未満でも十分光る。 */
const BASE_GLOW = 0.55;

/** クリア演出で最大まで上げたときの輝度。 */
const BURST_GLOW = 3.0;

/** コアの色をピース色から作るときの白寄せ量。芯なので彩度より明るさを優先する。 */
const CORE_WHITENESS = 0.45;

/** 内部発光コアのセット。位置は PieceViews が毎回の配置更新で書き込む。 */
export type GlowCores = {
  /** シーン（ピースのルート）に追加する InstancedMesh。1 セットで全ピース分。 */
  readonly object: THREE.InstancedMesh;
  /** ピース pieceId の index 番目のボクセル中心を書き込む。書き終えたら flush() を呼ぶ。 */
  setPosition(pieceId: number, index: number, x: number, y: number, z: number): void;
  /** setPosition の結果を GPU へ反映する。 */
  flush(): void;
  /** 経過時間（秒）で明滅を進める。フレームレートに依存しない。 */
  update(elapsedSeconds: number): void;
  /** クリア演出の発光ブースト（0 = 平常、1 = 最大）。 */
  setBoost(amount: number): void;
  /**
   * ピースのコアを隠す / 戻す。固定中のピースはコアの代わりにロックアイコン（lockIcons）を出す。
   * 隠したコアもクリア演出のブーストに合わせて光る（SPEC.md 5.2-1「全ボクセルが連動」を保つ）。
   */
  setHidden(pieceId: number, hidden: boolean): void;
  /** ジオメトリ / マテリアルを解放する。 */
  dispose(): void;
};

/** ピース 1 つ分のインスタンス区間と明滅の位相。 */
type Range = {
  readonly start: number;
  readonly count: number;
  readonly phase: number;
  readonly color: THREE.Color;
};

/**
 * 全ピースのボクセル数の合計だけインスタンスを持つコアのセットを作る。
 * colorOf はピース色を返す関数（PieceViews が持っている色をそのまま渡す）。
 */
export function createGlowCores(
  pieces: readonly Piece[],
  colorOf: (pieceId: number) => THREE.Color,
): GlowCores {
  const total = pieces.reduce((sum, piece): number => sum + piece.voxels.length, 0);
  const geometry = new THREE.SphereGeometry(CORE_RADIUS, 8, 6);

  /**
   * 解釈: 加算合成の芯なので深度は書かない。さらに depthTest も切ってある。
   * すりガラス（transmission）のピース本体は不透明パスで深度を書くため、深度テストを残すと
   * 自分を包むボクセルに隠れてコアがまったく見えなくなる。クリスタル内部の発光が
   * 手前のピース越しにうっすら透ける絵になるが、すりガラスの見た目としてはむしろ自然。
   */
  const material = new THREE.MeshBasicMaterial({
    transparent: true,
    depthWrite: false,
    depthTest: false,
    blending: THREE.AdditiveBlending,
  });

  const object = new THREE.InstancedMesh(geometry, material, Math.max(total, 1));
  object.name = 'glow-cores';
  object.frustumCulled = false;
  object.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  // ピース本体のあとに描く。ピース選択のレイキャスト（src/input）には拾わせない
  object.renderOrder = 1;
  object.raycast = (): void => {};

  const ranges = new Map<number, Range>();
  let cursor = 0;
  pieces.forEach((piece, index): void => {
    ranges.set(piece.id, {
      start: cursor,
      count: piece.voxels.length,
      // ピースごとに位相を等間隔でずらす（SPEC.md 4 章）
      phase: (index / Math.max(pieces.length, 1)) * Math.PI * 2,
      color: colorOf(piece.id).clone().lerp(new THREE.Color(0xffffff), CORE_WHITENESS),
    });
    cursor += piece.voxels.length;
  });

  const matrix = new THREE.Matrix4();
  const scratch = new THREE.Color();
  let boost = 0;
  // コアを隠しているピース（固定中のピース）
  const hiddenPieces = new Set<number>();

  // instanceColor を確保しておく（setColorAt の初回呼び出しで作られる）
  for (let i = 0; i < object.count; i += 1) object.setColorAt(i, scratch.setScalar(0));
  // 位置が入るまでは原点に重なるので、いったん見えない位置へ逃がす
  matrix.makeTranslation(0, 0, 0);
  for (let i = 0; i < object.count; i += 1) object.setMatrixAt(i, matrix);

  return {
    object,

    setPosition(pieceId, index, x, y, z): void {
      const range = ranges.get(pieceId);
      if (!range || index < 0 || index >= range.count) return;
      matrix.makeTranslation(x, y, z);
      object.setMatrixAt(range.start + index, matrix);
    },

    flush(): void {
      object.instanceMatrix.needsUpdate = true;
    },

    update(elapsedSeconds: number): void {
      for (const [pieceId, range] of ranges) {
        const breath = 0.5 + 0.5 * Math.sin(elapsedSeconds * BREATH_SPEED + range.phase);
        // ブーストが上がるほど位相差が消え、全ボクセルが連動して最大へ向かう（SPEC.md 5.2-1）
        const level = THREE.MathUtils.lerp(breath, 1, boost);
        // 隠したコアは平常時は 0（加算合成なので黒は見えない）で、ブーストに合わせて他へ追いつく
        const visibility = hiddenPieces.has(pieceId) ? boost : 1;
        const intensity =
          level * visibility * THREE.MathUtils.lerp(BASE_GLOW, BURST_GLOW, boost);
        scratch.copy(range.color).multiplyScalar(intensity);
        for (let i = 0; i < range.count; i += 1) {
          object.setColorAt(range.start + i, scratch);
        }
      }
      if (object.instanceColor) object.instanceColor.needsUpdate = true;
    },

    setBoost(amount: number): void {
      boost = THREE.MathUtils.clamp(amount, 0, 1);
    },

    setHidden(pieceId: number, hidden: boolean): void {
      if (hidden) hiddenPieces.add(pieceId);
      else hiddenPieces.delete(pieceId);
    },

    dispose(): void {
      hiddenPieces.clear();
      object.removeFromParent();
      object.dispose();
      geometry.dispose();
      material.dispose();
    },
  };
}
