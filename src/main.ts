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
import { pickHintPiece } from './core/hint';
import { placedVoxels, type Piece, type Placement } from './core/piece';
import { snappedOrientation } from './input/freeRotation';
import { createPieceInput, type PieceInput } from './input/pieceInput';
import { createSnapControl } from './input/snapControl';
import { createSnapAudio } from './render/audio';
import { createOrbitCamera } from './render/camera';
import { createClearEffect, type ClearEffect } from './render/clearEffect';
import { createPieceViews, createSolutionFrame } from './render/pieces';
import { preferLiteMode } from './render/quality';
import { createRotationGizmo, type GizmoAxis } from './render/rotationGizmo';
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

/**
 * 回転ギズモの輪をピースの外へ出すための余白（ボクセル）。ボクセル半分 + 少し。
 * 最小半径はボクセル 1.5 個ぶん（1 ボクセルのピースでも指で輪を掴める大きさ）。
 */
const GIZMO_MARGIN = 0.9;
const MIN_GIZMO_RADIUS = 1.5;

/**
 * 回転ギズモの中心（ワールド座標を center に書く）と半径を、ピースの配置後ボクセルから決める。
 * 中心は重心（render/pieces.ts の自由回転の回転中心と同じ）なので、回しても中心は動かない。
 * 半径は外接球 + 余白なので、どのピースでも輪がピースの外に出る（解釈: 指示書のとおり
 * 画面上で一定の大きさにはせず、ピースの大きさに合わせる）。
 */
function measureGizmo(
  piece: Piece,
  placement: Placement,
  n: number,
  center: THREE.Vector3,
): number {
  const voxels = placedVoxels(piece, placement);
  let sumX = 0;
  let sumY = 0;
  let sumZ = 0;
  for (const voxel of voxels) {
    sumX += voxel.x;
    sumY += voxel.y;
    sumZ += voxel.z;
  }
  const count = Math.max(voxels.length, 1);
  center.set(sumX / count, sumY / count, sumZ / count);
  let maxSquared = 0;
  for (const voxel of voxels) {
    const squared =
      (voxel.x - center.x) ** 2 + (voxel.y - center.y) ** 2 + (voxel.z - center.z) ** 2;
    if (squared > maxSquared) maxSquared = squared;
  }
  // ピースのルートは立方体 [0, n-1]³ の中心が原点に来るようずらしてある（render/pieces.ts）
  center.addScalar(-(n - 1) / 2);
  return Math.max(Math.sqrt(maxSquared) + GIZMO_MARGIN, MIN_GIZMO_RADIUS);
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
  /** 回転モードで表示だけをねじっているピース。ねじれたまま残さないよう必ずここで覚えておく。 */
  let freeRotated: number | null = null;

  // 回転モード中だけ出す回転ギズモ（SPEC.md 3.3 の回転操作の見せ方）。
  // 中身は 4 本の輪だけなので、セッション中は作りっぱなしにして表示だけを切り替える
  const gizmo = createRotationGizmo();
  context.scene.add(gizmo.object);
  const gizmoCenter = new THREE.Vector3();
  // ピース id から実体を引く（ギズモの大きさを測るのにボクセル形状が要る）
  const pieceById = new Map(puzzle.pieces.map((piece): [number, Piece] => [piece.id, piece]));

  /** 表示だけの自由回転を解く（論理上の配置は触らない）。 */
  const clearFreeRotation = (): void => {
    if (freeRotated === null) return;
    pieceViews.setFreeRotation(freeRotated, null);
    freeRotated = null;
  };

  /** ギズモを消す（回転モードを抜ける / 選択が外れる / クリア / 破棄）。 */
  const hideGizmo = (): void => {
    gizmo.setActive(null);
    gizmo.setVisible(false);
  };

  /**
   * ギズモを選択中のピースに合わせて置き直す。回転モードでなければ消す。
   * 回転モードに入ったときと、ピースが動いたとき（game の onChange）に呼ぶ。
   */
  const refreshGizmo = (): void => {
    const pieceId = input?.rotateMode() === true ? input.selectedPieceId() : null;
    const piece = pieceId === null ? undefined : pieceById.get(pieceId);
    const placement = pieceId === null ? undefined : game.placementOf(pieceId);
    if (piece === undefined || placement === undefined) {
      hideGizmo();
      return;
    }
    const radius = measureGizmo(piece, placement, n, gizmoCenter);
    gizmo.place(gizmoCenter, radius);
    gizmo.setVisible(true);
    // 出した最初のフレームから外周の白い輪をカメラへ向けておく
    gizmo.update(context.camera);
  };

  /** 回転モードを抜けて、HUD のラベルと表示のねじれ・ギズモを元へ戻す。 */
  const exitRotateMode = (): void => {
    input?.setRotateMode(false);
    clearFreeRotation();
    hideGizmo();
    hud?.setRotateMode(false);
  };

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
    // 回転モードのねじれを解いてから片付ける（クリアした形が歪んで見えないように）
    exitRotateMode();
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
    // ピースが動けばギズモの中心も動く（回転モードでなければ消えたまま）
    refreshGizmo();
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
    // 回転モードの pointerdown で輪を拾う（ピース本体のピックより先に呼ばれる）
    pickGizmo: (raycaster): GizmoAxis | null => gizmo.pick(raycaster),
    gizmoCenter: (target): THREE.Vector3 | null =>
      gizmo.isVisible() ? gizmo.center(target) : null,
    onGizmoAxisChange: (axis): void => {
      gizmo.setActive(axis);
    },
    onSelectionChange: (pieceId): void => {
      // 選択が変わると入力側が回転モードを解除する。表示のねじれとギズモもここで戻す
      clearFreeRotation();
      hideGizmo();
      pieceViews.setHighlighted(pieceId);
      // setSelected は回転モードの表示も 'off' に戻す（Hud.setSelected の注記）
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
    // 回転モードのドラッグ中。論理上の配置は変えず、表示だけを連続的に回す
    onFreeRotate: (pieceId, quaternion): void => {
      freeRotated = pieceId;
      pieceViews.setFreeRotation(pieceId, quaternion);
    },
    // 指を離したら最寄りの向き（24 通り）へスナップして確定させる。
    // 解釈: ドラッグ中の表示は重心まわりに回すが、確定は SPEC.md 3.3 のとおり局所原点まわりの
    // 回転（位置はそのまま向き id だけを差し替える）なので、確定の瞬間にピースが半マス前後
    // ずれて見えることがある。HUD の 90 度回転ボタンや 2 本指回転と同じ動きに揃えている
    onFreeRotateEnd: (pieceId, quaternion): void => {
      const placement = game.placementOf(pieceId);
      if (placement !== undefined) {
        // 位置は変えない。向きだけを「今の向きに自由回転を重ねた姿勢」の最寄りへ置き換える。
        // 45 度も回さずに離したときは向きが変わらないので、クリア判定も回さない
        const next = snappedOrientation(placement.orientation, quaternion);
        if (next !== placement.orientation) game.place(pieceId, next, placement.position);
      }
      // 確定した向きで描き直したいので、表示だけのねじれはここで解く
      clearFreeRotation();
      // 向きが変わればスナップ候補も変わる（クリアして入力が外れていれば候補は出さない）
      const active = input?.selectedPieceId() ?? null;
      snap.refresh(active !== null && game.lockKindOf(active) !== null ? null : active);
    },
  });

  session = {
    update(delta, elapsed): void {
      snapMotion.update();
      // 外周の白い輪は常にカメラを向く（表示中だけ働く）
      gizmo.update(context.camera);
      // 内部発光コアの呼吸（SPEC.md 4 章）。ピースごとに位相がずれている
      pieceViews.updateGlow(elapsed);
      clearEffect?.update(delta);
    },
    dispose(): void {
      finished = true;
      // 表示だけのねじれを解いてから捨てる（破棄の順で状態が残らないように）
      clearFreeRotation();
      gizmo.dispose();
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
        exitRotateMode();
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
          // 固定すると回転もできなくなるので、走っている回転を先に確定させてから固定する
          exitRotateMode();
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
      onToggleRotateMode: (): void => {
        if (input === null) return;
        if (input.rotateMode()) {
          // 抜けるときは表示のねじれを戻す（確定は指を離した時点で済んでいる）
          exitRotateMode();
          return;
        }
        // 入れたかどうかは入力側が決める（選択が無い / 固定中なら入らない）
        input.setRotateMode(true);
        hud?.setRotateMode(input.rotateMode());
        // 入れたときだけギズモが出る（入れなかったなら refreshGizmo が消したままにする）
        refreshGizmo();
      },
      onHint: (): void => {
        const pieceId = pickHintPiece(game.placements(), puzzle.solution, game.lockedIds());
        // 未固定が 1 個以下なら null。ボタンも無効なはずだが念のため何もしない
        if (pieceId === null) return;
        const answer = puzzle.solution.find((p): boolean => p.pieceId === pieceId);
        if (answer === undefined) return;
        // ヒントで置くピースが回転モード中なら、そのねじれを先に片付ける
        if (freeRotated === pieceId) exitRotateMode();
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
