// エントリ（SPEC.md 8 章 M4）。画面遷移（src/ui）とパズル 1 回分のセッション（core / render / input）を
// つなぐだけの層。ゲームのルールは src/core、描画は src/render、操作は src/input が持つ。
//
// レンダラ・カメラ・SE はアプリ全体で 1 つを使い回し、パズルごとに作り直すもの（ピースの
// InstancedMesh・枠・ゲーム状態・入力・スナップ）だけをセッションとしてまとめて破棄する。

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
import { pickHintPiece } from './core/hint';
import { placedVoxels, type Piece, type Placement } from './core/piece';
import { createPieceInput, type PieceInput } from './input/pieceInput';
import { createSnapControl } from './input/snapControl';
import { createSnapAudio } from './render/audio';
import { createOrbitCamera } from './render/camera';
import { createClearEffect, type ClearEffect } from './render/clearEffect';
import { createPieceViews, createSolutionFrame } from './render/pieces';
import { preferLiteMode } from './render/quality';
import { createRenderContext } from './render/scene';
import { createSnapMotion } from './render/snapMotion';
import { createClearScreen } from './ui/clearScreen';
import { DEFAULT_PIECE_COUNT, DEFAULT_SPACE_SIZE, randomSeed } from './ui/difficulty';
import { createFpsMeter, type FpsMeter } from './ui/fpsMeter';
import { createHud, type Hud } from './ui/hud';
import { clampInt, readAppParams, withSettings } from './ui/params';
import { unsettledPieceCount } from './ui/progress';
import { createScreenManager } from './ui/screens';
import { createTitleScreen } from './ui/titleScreen';
import type { Screen } from './ui/screens';

/** パズル 1 回分の条件（SPEC.md 3.1）。 */
type Settings = { readonly n: number; readonly m: number; readonly seed: number };

/** パズル 1 回分。作り直しのたびに dispose して GPU 資源とリスナを手放す。 */
type Session = {
  /**
   * 毎フレーム呼ぶ（スナップの補間・内部コアの明滅・クリア演出を進める）。
   * delta は前フレームからの秒数、elapsed は起動からの経過秒。
   */
  update(delta: number, elapsed: number): void;
  dispose(): void;
};

/**
 * タイトル画面の初期値。クエリの読み取りと不正値の扱いは src/ui/params.ts が持つ。
 * N / M は範囲外なら core が受け付ける範囲へ丸め、seed は不正なら乱数へフォールバックする。
 */
function initialSettings(query: ReturnType<typeof readAppParams>): Settings {
  const n = clampInt(query.n, MIN_SPACE_SIZE, MAX_SPACE_SIZE) ?? DEFAULT_SPACE_SIZE;
  const m = clampInt(query.m, MIN_PIECE_COUNT, maxPieces(n)) ?? DEFAULT_PIECE_COUNT;
  // シードは SPEC.md 3.1 のとおり ?seed= で指定できる。無ければ毎回引き直す
  return { n, m, seed: query.seed ?? randomSeed() };
}

/**
 * 遊んでいる盤面の条件を URL に映す（履歴は増やさない）。
 * 「もう一度」でシードが変わっても、URL をコピーすれば同じ盤面を再現できる。
 */
function syncLocation(settings: Settings): void {
  const search = withSettings(window.location.search, settings);
  window.history.replaceState(null, '', `${window.location.pathname}${search}`);
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

const query = readAppParams(window.location.search);
// 軽量モードは ?lite= が最優先、無指定なら端末判定（src/render/quality.ts）
const lite = query.lite ?? preferLiteMode();

const context = createRenderContext(container);
const orbit = createOrbitCamera(context.camera, context.renderer.domElement);
// マグネットスナップの SE（SPEC.md 3.5）。WebAudio の合成音なのでアプリ全体で 1 つでよい
const snapAudio = createSnapAudio();
const screens = createScreenManager(ui);
// 計測用（SPEC.md には無い開発用の道具）。?fps=1 のときだけ作る
const fpsMeter: FpsMeter | null = query.fps ? createFpsMeter(ui) : null;

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

  syncLocation(settings);

  const pieceViews = createPieceViews(puzzle.pieces, n, { lite });
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
  // クリア演出（SPEC.md 5.2）。クリアするまでは null
  let clearEffect: ClearEffect | null = null;

  /**
   * ヒントボタンの有効 / 無効を今の盤面から決め直す。
   * 未固定のピースが 2 個以上あるときだけ使える（最後の 1 ピースはヒントを使えない）。
   * 配置やロックが変わるたびに呼ぶ。
   */
  const refreshHintEnabled = (): void => {
    hud?.setHintEnabled(total - game.lockedIds().length >= 2);
  };

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
    // 演出中は操作を無効化する（SPEC.md 5.2-4）。カメラの自動旋回は enabled と独立に効く
    orbit.enabled = false;
    // 完成した立方体を主役にするので、解答空間の枠は引っ込める
    frame.object.visible = false;
    clearEffect = createClearEffect({
      scene: context.scene,
      n,
      color: pieceViews.blendedColor,
      setGlow: (amount): void => {
        pieceViews.setGlowBoost(amount);
      },
      setPiecesVisible: (visible): void => {
        pieceViews.setPiecesVisible(visible);
      },
      setAutoRotate: (speed): void => {
        orbit.setAutoRotate(speed);
      },
    });
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
    const active = input?.selectedPieceId() ?? null;
    snap.refresh(active !== null && game.lockKindOf(active) !== null ? null : active);
    refreshHintEnabled();
    if (solved) showClear();
  });

  input = createPieceInput({
    domElement: context.renderer.domElement,
    camera: context.camera,
    root: pieceViews.object,
    orbit,
    gridPositionOf: (pieceId): Placement['position'] | undefined =>
      game.placementOf(pieceId)?.position,
    // 固定中のピースは選べるが動かせない（ドラッグ・奥行き・2 本指回転・スナップを止める）
    isLocked: (pieceId): boolean => game.lockKindOf(pieceId) !== null,
    onSelectionChange: (pieceId): void => {
      pieceViews.setHighlighted(pieceId);
      hud?.setSelected(pieceId);
      hud?.setLock(pieceId === null ? 'none' : (game.lockKindOf(pieceId) ?? 'none'));
      // 固定中のピースは吸い付かないので候補も出さない
      snap.refresh(pieceId !== null && game.lockKindOf(pieceId) !== null ? null : pieceId);
    },
    onMove: (pieceId, delta): void => {
      // 手で動かしたら前のスナップの補間は用済み
      snapMotion.cancel(pieceId);
      game.move(pieceId, delta);
    },
    // 2 本指ジェスチャからの 90 度回転（HUD のボタンと同じ扱い）
    onRotate: (pieceId, axis, dir): void => {
      snapMotion.cancel(pieceId);
      game.rotate(pieceId, axis, dir);
    },
    onRelease: (pieceId): void => {
      // 手を離した瞬間に吸い付かせる（SPEC.md 3.5）
      snap.release(pieceId);
    },
  });

  session = {
    update(delta, elapsed): void {
      snapMotion.update();
      // 内部発光コアの呼吸（SPEC.md 4 章）。ピースごとに位相がずれている
      pieceViews.updateGlow(elapsed);
      clearEffect?.update(delta);
    },
    dispose(): void {
      finished = true;
      input?.dispose();
      input = null;
      hud = null;
      // 「もう一度」「難易度を変える」でここに来る。演出とカメラ旋回はここで解除する
      clearEffect?.dispose();
      clearEffect = null;
      orbit.setAutoRotate(0);
      orbit.enabled = true;
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
        // ヒントで固定したピース（金ロック）は正解位置に残し、それ以外だけを散らし直す
        const keep = game
          .placements()
          .filter((placement): boolean => game.lockKindOf(placement.pieceId) === 'hint');
        game.reset(scatterPlacements(puzzle.pieces, n, seed, { keep }));
        // reset は手動固定を解く（ヒントの固定は残る）。アイコンも実際の状態へ揃え直す
        for (const piece of puzzle.pieces) {
          pieceViews.setLockIcon(piece.id, game.lockKindOf(piece.id));
        }
        refreshHintEnabled();
      },
      onBackToTitle: (): void => {
        showTitle({ n, m, seed: randomSeed() });
      },
      onNext: (): void => {
        // 難易度はそのままシードだけ引き直す（クリア画面の「もう一度」と同じ扱い）
        startSession({ n, m, seed: randomSeed() });
      },
      onToggleLock: (): void => {
        const pieceId = input?.selectedPieceId() ?? null;
        if (pieceId === null) return;
        const kind = game.lockKindOf(pieceId);
        // ヒントで置いたピース（金ロック）は解除できない
        if (kind === 'hint') return;
        if (kind === null) {
          game.lock(pieceId, 'manual');
          // 固定した位置で止めるので、走っているスナップの補間は打ち切る
          snapMotion.cancel(pieceId);
        } else {
          game.unlock(pieceId);
        }
        const next = game.lockKindOf(pieceId);
        pieceViews.setLockIcon(pieceId, next);
        hud?.setLock(next ?? 'none');
        // 固定したら候補を消し、解除したら計算し直す
        snap.refresh(next === null ? pieceId : null);
        refreshHintEnabled();
      },
      onHint: (): void => {
        const pieceId = pickHintPiece(game.placements(), puzzle.solution, game.lockedIds());
        // 未固定が 1 個以下なら null。ボタンも無効なはずだが念のため何もしない
        if (pieceId === null) return;
        const answer = puzzle.solution.find((p): boolean => p.pieceId === pieceId);
        if (answer === undefined) return;
        // 正解位置へ送ってから固定する（place は固定済みのピースには効かないので順番が要る）
        game.place(pieceId, answer.orientation, answer.position);
        game.lock(pieceId, 'hint');
        pieceViews.setLockIcon(pieceId, 'hint');
        // 移動先で止めるので、走っているスナップの補間は打ち切る
        snapMotion.cancel(pieceId);
        // 選択は解除しない。選択中がそのピースなら「固定解除」を無効表示に切り替える
        const selected = input?.selectedPieceId() ?? null;
        if (selected === pieceId) hud?.setLock('hint');
        snap.refresh(
          selected !== null && game.lockKindOf(selected) !== null ? null : selected,
        );
        refreshHintEnabled();
      },
    }),
  );

  pieceViews.updatePlacements(game.placements());
  hud.setSelected(null);
  hud.setRemaining(unsettledPieceCount(puzzle.pieces, game.placements(), n), total);
  refreshHintEnabled();
  // 散らした直後はクリアにならないはずだが、念のため初期状態も見る
  if (game.solved()) showClear();
}

context.start((delta, elapsed): void => {
  orbit.update(delta);
  session?.update(delta, elapsed);
  // ドローコールは「前のフレームの描画結果」を読む（このフレームぶんはまだ積まれていない）
  fpsMeter?.update(delta, context.drawCalls());
});

showTitle(initialSettings(query));
