// エントリ。core でパズルを生成・散らし、render に渡して描画する（SPEC.md 8 章 M2）。
// 難易度選択画面は M4、ピース操作は M3 で作る。ここでは既定値か URL クエリで N / M / seed を決める。
import * as THREE from 'three';
import {
  generatePuzzle,
  MAX_SPACE_SIZE,
  maxPieces,
  MIN_PIECE_COUNT,
  MIN_SPACE_SIZE,
  scatterPlacements,
} from './core/generate';
import { placedVoxels, type Piece, type Placement } from './core/piece';
import { createOrbitCamera } from './render/camera';
import { createPieceViews, createSolutionFrame } from './render/pieces';
import { createRenderContext } from './render/scene';

/** SPEC.md 3.1 の既定（N=3 / M=4）。 */
const DEFAULT_SPACE_SIZE = 3;
const DEFAULT_PIECE_COUNT = 4;

/** URL クエリから整数を読む。無い / 数値でないときは undefined。 */
function readInt(params: URLSearchParams, key: string): number | undefined {
  const raw = params.get(key);
  if (raw === null) return undefined;
  const value = Number.parseInt(raw, 10);
  return Number.isFinite(value) ? value : undefined;
}

/** N / M / seed を決める。範囲外の指定は core が受け付ける範囲へ丸める。 */
function readParams(search: string): { n: number; m: number; seed: number } {
  const params = new URLSearchParams(search);
  const n = THREE.MathUtils.clamp(
    readInt(params, 'n') ?? DEFAULT_SPACE_SIZE,
    MIN_SPACE_SIZE,
    MAX_SPACE_SIZE,
  );
  const m = THREE.MathUtils.clamp(
    readInt(params, 'm') ?? DEFAULT_PIECE_COUNT,
    MIN_PIECE_COUNT,
    maxPieces(n),
  );
  // シードは SPEC.md 3.1 のとおり ?seed= で指定できる。無ければ毎回ランダム
  const seed = readInt(params, 'seed') ?? Math.floor(Math.random() * 0x7fffffff);
  return { n, m, seed };
}

/** 散らした全ボクセルが収まる球の半径（中心は解答立方体の中心）。カメラの初期距離に使う。 */
function scatterRadius(
  pieces: readonly Piece[],
  placements: readonly Placement[],
  n: number,
): number {
  const byId = new Map(pieces.map((piece): [number, Piece] => [piece.id, piece]));
  const center = (n - 1) / 2;
  let maxSquared = (n / 2) ** 2 * 3;
  for (const placement of placements) {
    const piece = byId.get(placement.pieceId);
    if (!piece) continue;
    for (const voxel of placedVoxels(piece, placement)) {
      const squared =
        (voxel.x - center) ** 2 + (voxel.y - center) ** 2 + (voxel.z - center) ** 2;
      if (squared > maxSquared) maxSquared = squared;
    }
  }
  return Math.sqrt(maxSquared) + 1;
}

const container = document.getElementById('app');
if (!container) throw new Error('#app が見つからない');

const { n, m, seed } = readParams(window.location.search);
const puzzle = generatePuzzle(n, m, seed);
const placements = scatterPlacements(puzzle.pieces, n, seed);

const context = createRenderContext(container);
const pieceViews = createPieceViews(puzzle.pieces, n);
pieceViews.updatePlacements(placements);
context.scene.add(pieceViews.object, createSolutionFrame(n));

const orbit = createOrbitCamera(context.camera, context.renderer.domElement);
orbit.frame(scatterRadius(puzzle.pieces, placements, n));

// シードの表示（SPEC.md 6 章。HUD 本体は M4 で作る）
const ui = document.getElementById('ui');
if (ui) {
  const info = document.createElement('div');
  info.id = 'info';
  info.textContent = `N=${n}  M=${m}  seed=${seed}`;
  ui.appendChild(info);
}

context.start((): void => {
  orbit.update();
});
