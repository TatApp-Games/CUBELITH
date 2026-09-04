// エントリ（SPEC.md 8 章 M4）。画面遷移（src/ui）とパズル 1 回分のセッション（core / render / input）を
// つなぐだけの層。ゲームのルールは src/core、描画は src/render、操作は src/input が持つ。
//
// レンダラ・カメラ・SE はアプリ全体で 1 つを使い回し、パズルごとに作り直すもの（ピースの
// InstancedMesh・枠・ゲーム状態・入力・スナップ）だけをセッションとしてまとめて破棄する。

import * as THREE from 'three';
import { createGame } from './core/game';
import {
  generatePuzzle,
  MAX_SPACE_SIZE,
  maxPieces,
  MIN_PIECE_COUNT,
  MIN_SPACE_SIZE,
  scatterPlacements,
} from './core/generate';
import { subVec3 } from './core/grid';
import { placedVoxels, type Piece, type Placement } from './core/piece';
import { createPieceInput, type PieceInput } from './input/pieceInput';
import { createSnapControl } from './input/snapControl';
import { createSnapAudio } from './render/audio';
import { createOrbitCamera } from './render/camera';
import { createPieceViews, createSolutionFrame } from './render/pieces';
import { createRenderContext } from './render/scene';
import { createSnapMotion } from './render/snapMotion';
import { createClearScreen } from './ui/clearScreen';
import { DEFAULT_PIECE_COUNT, DEFAULT_SPACE_SIZE, randomSeed } from './ui/difficulty';
import { createHud, type Hud } from './ui/hud';
import { unsettledPieceCount } from './ui/progress';
import { createScreenManager } from './ui/screens';
import { createTitleScreen } from './ui/titleScreen';
import type { Screen } from './ui/screens';

/** パズル 1 回分の条件（SPEC.md 3.1）。 */
type Settings = { readonly n: number; readonly m: number; readonly seed: number };

/** パズル 1 回分。作り直しのたびに dispose して GPU 資源とリスナを手放す。 */
type Session = {
  /** 毎フレーム呼ぶ（スナップの補間を進める）。 */
  update(): void;
  dispose(): void;
};

/** URL クエリから整数を読む。無い / 数値でないときは undefined。 */
function readInt(params: URLSearchParams, key: string): number | undefined {
  const raw = params.get(key);
  if (raw === null) return undefined;
  const value = Number.parseInt(raw, 10);
  return Number.isFinite(value) ? value : undefined;
}

/** タイトル画面の初期値。範囲外の指定は core が受け付ける範囲へ丸める。 */
function readParams(search: string): Settings {
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
  // シードは SPEC.md 3.1 のとおり ?seed= で指定できる。無ければ毎回引き直す
  const seed = readInt(params, 'seed') ?? randomSeed();
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

const context = createRenderContext(container);
const orbit = createOrbitCamera(context.camera, context.renderer.domElement);
// マグネットスナップの SE（SPEC.md 3.5）。WebAudio の合成音なのでアプリ全体で 1 つでよい
const snapAudio = createSnapAudio();
const screens = createScreenManager(ui);

// 自動再生制限があるので、最初のユーザー操作で AudioContext を作って resume する
const unlockAudio = (): void => {
  snapAudio.unlock();
};
window.addEventListener('pointerdown', unlockAudio, { capture: true });
window.addEventListener('keydown', unlockAudio, { capture: true });

let session: Session | null = null;

/** 走っているセッションを片付ける。 */
function disposeSession(): void {
  if (session === null) return;
  session.dispose();
  session = null;
}

/** タイトル（難易度選択）へ。セッションが走っていれば破棄する。 */
function showTitle(settings: Settings): void {
  disposeSession();
  screens.show('title', (host): Screen =>
    createTitleScreen(host, {
      n: settings.n,
      m: settings.m,
      seed: settings.seed,
      onStart: (n, m, seed): void => {
        startSession({ n, m, seed });
      },
    }),
  );
}

/** 生成 → 散らし → プレイ（SPEC.md 2 章 2 / 3）。 */
function startSession(settings: Settings): void {
  disposeSession();
  const { n, m, seed } = settings;

  const puzzle = generatePuzzle(n, m, seed);
  const initial = scatterPlacements(puzzle.pieces, n, seed);
  const total = puzzle.pieces.length;

  const pieceViews = createPieceViews(puzzle.pieces, n);
  const frame = createSolutionFrame(n);
  context.scene.add(pieceViews.object, frame.object);
  orbit.frame(scatterRadius(puzzle.pieces, initial, n));

  const snapMotion = createSnapMotion((pieceId, offset): void => {
    pieceViews.setOffset(pieceId, offset);
  });

  // 互いを参照し合うので実体が揃うまでは null を入れておく（参照するのは操作が起きた後）
  let hud: Hud | null = null;
  let input: PieceInput | null = null;
  let finished = false;

  const snap = createSnapControl({
    pieces: puzzle.pieces,
    n,
    placements: (): readonly Placement[] => game.placements(),
    onHintChange: (pieceId): void => {
      pieceViews.setSnapHint(pieceId);
    },
    onSnap: (from, to): void => {
      // 論理上の配置は整数座標のまま即座に確定させ、見た目だけを 100〜150 ms かけて追いつかせる
      game.move(to.pieceId, subVec3(to.position, from.position));
      snapMotion.start(to.pieceId, subVec3(from.position, to.position));
      snapAudio.playSnap();
    },
  });

  /** クリア画面へ。以降ピースを動かせないよう入力を外す（SPEC.md 2 章 4 / 5）。 */
  const showClear = (): void => {
    if (finished) return;
    finished = true;
    // 選択を解いてからカメラ操作を戻す（select(null) が orbit.enabled を true にする）
    input?.select(null);
    input?.dispose();
    input = null;
    hud = null;
    screens.show('clear', (host): Screen =>
      createClearScreen(host, {
        n,
        m,
        seed,
        // 「もう一度」は同じ条件で再生成し、シードだけ引き直す（SPEC.md 2 章 5）
        onRetry: (): void => {
          startSession({ n, m, seed: randomSeed() });
        },
        onBackToTitle: (): void => {
          showTitle({ n, m, seed: randomSeed() });
        },
      }),
    );
  };

  // 配置が変わるたびに描画・HUD・スナップ候補へ反映する（SPEC.md 3.4）
  const game = createGame(puzzle.pieces, n, initial, (placements, solved): void => {
    pieceViews.updatePlacements(placements);
    hud?.setRemaining(unsettledPieceCount(puzzle.pieces, placements, n), total);
    // 位置が変わったこのタイミングだけで候補を計算し直す（毎フレームは回さない）
    snap.refresh(input?.selectedPieceId() ?? null);
    if (solved) showClear();
  });

  input = createPieceInput({
    domElement: context.renderer.domElement,
    camera: context.camera,
    root: pieceViews.object,
    orbit,
    gridPositionOf: (pieceId): Placement['position'] | undefined =>
      game.placementOf(pieceId)?.position,
    onSelectionChange: (pieceId): void => {
      pieceViews.setHighlighted(pieceId);
      hud?.setSelected(pieceId);
      snap.refresh(pieceId);
    },
    onMove: (pieceId, delta): void => {
      // 手で動かしたら前のスナップの補間は用済み
      snapMotion.cancel(pieceId);
      game.move(pieceId, delta);
    },
    onRelease: (pieceId): void => {
      // 手を離した瞬間に吸い付かせる（SPEC.md 3.5）
      snap.release(pieceId);
    },
  });

  session = {
    update(): void {
      snapMotion.update();
    },
    dispose(): void {
      finished = true;
      input?.dispose();
      input = null;
      hud = null;
      snapMotion.dispose();
      snap.refresh(null);
      context.scene.remove(pieceViews.object);
      pieceViews.dispose();
      frame.dispose();
    },
  };

  hud = screens.show('play', (host): Hud =>
    createHud(host, {
      onRotate: (axis, dir): void => {
        const pieceId = input?.selectedPieceId() ?? null;
        if (pieceId === null) return;
        // 回した先は別の場所なので、走っているスナップの補間は打ち切る
        snapMotion.cancel(pieceId);
        game.rotate(pieceId, axis, dir);
      },
      onDepth: (dir): void => {
        input?.moveDepth(dir);
      },
      onReset: (): void => {
        // 同じ seed の散らし配置に戻す（SPEC.md 3.3「やり直し」）
        input?.select(null);
        for (const piece of puzzle.pieces) snapMotion.cancel(piece.id);
        game.reset(scatterPlacements(puzzle.pieces, n, seed));
      },
      onBackToTitle: (): void => {
        showTitle({ n, m, seed: randomSeed() });
      },
    }),
  );

  pieceViews.updatePlacements(game.placements());
  hud.setSelected(null);
  hud.setRemaining(unsettledPieceCount(puzzle.pieces, game.placements(), n), total);
  // 散らした直後はクリアにならないはずだが、念のため初期状態も見る
  if (game.solved()) showClear();
}

context.start((): void => {
  orbit.update();
  session?.update();
});

showTitle(readParams(window.location.search));
