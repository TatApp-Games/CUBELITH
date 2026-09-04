// エントリ。core でパズルを生成・散らし、render で描き、input と ui で操作させる（SPEC.md 8 章 M3）。
// 難易度選択画面は M4、演出は M5 で作る。ここでは既定値か URL クエリで N / M / seed を決める。
import * as THREE from 'three';
import {
  generatePuzzle,
  MAX_SPACE_SIZE,
  maxPieces,
  MIN_PIECE_COUNT,
  MIN_SPACE_SIZE,
  scatterPlacements,
} from './core/generate';
import { createGame } from './core/game';
import { placedVoxels, type Piece, type Placement } from './core/piece';
import { createPieceInput } from './input/pieceInput';
import { createOrbitCamera } from './render/camera';
import { createPieceViews, createSolutionFrame } from './render/pieces';
import { createRenderContext } from './render/scene';
import { createHud } from './ui/hud';

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
const ui = document.getElementById('ui');
if (!ui) throw new Error('#ui が見つからない');

const { n, m, seed } = readParams(window.location.search);
const puzzle = generatePuzzle(n, m, seed);
const initialPlacements = scatterPlacements(puzzle.pieces, n, seed);

const context = createRenderContext(container);
const pieceViews = createPieceViews(puzzle.pieces, n);
context.scene.add(pieceViews.object, createSolutionFrame(n));

const orbit = createOrbitCamera(context.camera, context.renderer.domElement);
orbit.frame(scatterRadius(puzzle.pieces, initialPlacements, n));

// HUD → 操作。input / game はこの後で作るが、参照するのはボタンが押された時点なので問題ない
const hud = createHud(ui, {
  onRotate: (axis, dir): void => {
    const pieceId = input.selectedPieceId();
    if (pieceId !== null) game.rotate(pieceId, axis, dir);
  },
  onDepth: (dir): void => {
    input.moveDepth(dir);
  },
  onReset: (): void => {
    // 同じ seed の散らし配置に戻す（SPEC.md 3.3「やり直し」）
    input.select(null);
    game.reset(scatterPlacements(puzzle.pieces, n, seed));
  },
});

// 配置が変わるたびに描画へ反映し、クリア判定の結果を HUD へ渡す（SPEC.md 3.4）
const game = createGame(
  puzzle.pieces,
  n,
  initialPlacements,
  (placements, solved): void => {
    pieceViews.updatePlacements(placements);
    hud.setSolved(solved);
  },
);
pieceViews.updatePlacements(game.placements());
hud.setSolved(game.solved());

const input = createPieceInput({
  domElement: context.renderer.domElement,
  camera: context.camera,
  root: pieceViews.object,
  orbit,
  gridPositionOf: (pieceId): Placement['position'] | undefined =>
    game.placementOf(pieceId)?.position,
  onSelectionChange: (pieceId): void => {
    pieceViews.setHighlighted(pieceId);
    hud.setSelected(pieceId);
  },
  onMove: (pieceId, delta): void => {
    game.move(pieceId, delta);
  },
});

// シードの表示（SPEC.md 6 章。難易度選択画面は M4 で作る）
const info = document.createElement('div');
info.id = 'info';
info.textContent = `N=${n}  M=${m}  seed=${seed}`;
ui.appendChild(info);

context.start((): void => {
  orbit.update();
});
