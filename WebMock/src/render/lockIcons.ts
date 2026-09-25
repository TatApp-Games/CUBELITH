// ピースの固定（ロック）を示す南京錠アイコン。
// 銀 = 人が固定したもの（手動固定）、金 = ヒントで正解位置へ送ったもの。固定中のピースの
// **各ボクセルの中心**に 1 枚ずつ出す（そのピースの内部発光コアの代わり。glowCores.setHidden）。
//
// ドローコール: 種類ごとに全ボクセル分のスロットを持つ InstancedMesh を 1 つずつ作り、
// 固定していないスロットはスケール 0 で潰して描かない。増えるのは最大 2 ドローコールで、
// その種類の固定が 1 つも無ければ描かない（SPEC.md 4 章の「ピース数 + α」の α に収まる）。
// ボクセルごとに Sprite を作ると固定中のボクセル数だけドローコールが増えるので使わない。

import * as THREE from 'three';
import type { Piece } from '../core/piece';

/** 固定の種類。core の LockKind と同じ並びだが、描画側は色の選択にしか使わない。 */
export type LockIconKind = 'manual' | 'hint';

/** 描画する種類の並び。種類ごとに InstancedMesh を 1 つ持つ。 */
const KINDS: readonly LockIconKind[] = ['manual', 'hint'];

/**
 * アイコンの大きさ（ワールド単位 = ボクセル 1 個ぶんが 1）。
 * 解釈: ボクセルの間隔は 1 なので、大きいと斜めから見たときに隣のアイコンと重なって
 * ピースの形が読みにくい。半マスにして、ブロックごとの位置が分かる程度にする。
 */
const ICON_SIZE = 0.5;

/** テクスチャの 1 辺（px）。拡大しても縁がぼやけない程度で、かつ軽い大きさ。 */
const TEXTURE_PIXELS = 128;

/** 南京錠の配色。body = 錠前の胴、shackle = つる、edge = 縁取り、hole = 鍵穴。 */
type Palette = {
  readonly body: string;
  readonly bodyShade: string;
  readonly shackle: string;
  readonly edge: string;
  readonly hole: string;
};

/**
 * 解釈: 手動固定の色は要望に指定が無い。ヒントの金と一目で見分けられるよう銀（灰白）にする。
 * 金はヒント用。銀と並んでも一目で分かるよう、彩度の高い山吹寄りの金にしている。
 */
const PALETTES: Readonly<Record<LockIconKind, Palette>> = {
  manual: {
    body: '#f2f5fa',
    bodyShade: '#a9b6cb',
    shackle: '#cdd8e8',
    edge: '#5d6b84',
    hole: '#3a465c',
  },
  hint: {
    body: '#ffd23f',
    bodyShade: '#b87a10',
    shackle: '#ffbe1a',
    edge: '#6b4708',
    hole: '#4a3106',
  },
};

/** 角丸の矩形パスを引く。CanvasRenderingContext2D.roundRect は環境差があるので自前で描く。 */
function roundRectPath(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  width: number,
  height: number,
  radius: number,
): void {
  const r = Math.min(radius, width / 2, height / 2);
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + width, y, x + width, y + height, r);
  ctx.arcTo(x + width, y + height, x, y + height, r);
  ctx.arcTo(x, y + height, x, y, r);
  ctx.arcTo(x, y, x + width, y, r);
  ctx.closePath();
}

/** 南京錠を 1 枚描いた CanvasTexture を作る。背景は透明のまま残す。 */
function createPadlockTexture(palette: Palette): THREE.CanvasTexture {
  const canvas = document.createElement('canvas');
  canvas.width = TEXTURE_PIXELS;
  canvas.height = TEXTURE_PIXELS;
  const ctx = canvas.getContext('2d');
  if (ctx === null) throw new Error('2D コンテキストを取得できない');

  // つる（U 字を上下逆にした半円）。胴に少し潜り込ませて継ぎ目を隠す
  ctx.lineCap = 'round';
  ctx.lineWidth = 15;
  ctx.strokeStyle = palette.edge;
  ctx.beginPath();
  ctx.arc(64, 56, 22, Math.PI, 2 * Math.PI);
  ctx.stroke();
  ctx.lineWidth = 10;
  ctx.strokeStyle = palette.shackle;
  ctx.beginPath();
  ctx.arc(64, 56, 22, Math.PI, 2 * Math.PI);
  ctx.stroke();

  // 胴。上から下へグラデーションを掛けて金属らしい陰影にする
  const gradient = ctx.createLinearGradient(0, 56, 0, 116);
  gradient.addColorStop(0, palette.body);
  gradient.addColorStop(1, palette.bodyShade);
  roundRectPath(ctx, 26, 56, 76, 58, 12);
  ctx.fillStyle = gradient;
  ctx.fill();
  ctx.lineWidth = 5;
  ctx.strokeStyle = palette.edge;
  ctx.stroke();

  // 鍵穴（丸 + 下へ伸びる台形）
  ctx.fillStyle = palette.hole;
  ctx.beginPath();
  ctx.arc(64, 79, 9, 0, 2 * Math.PI);
  ctx.fill();
  ctx.beginPath();
  ctx.moveTo(59, 82);
  ctx.lineTo(69, 82);
  ctx.lineTo(72, 101);
  ctx.lineTo(56, 101);
  ctx.closePath();
  ctx.fill();

  const texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.SRGBColorSpace;
  texture.needsUpdate = true;
  return texture;
}

/**
 * 画面に正対する板（ビルボード）のマテリアル。
 * インスタンス行列からは平行移動（位置）とスケール（0 = 描かない）だけを使い、回転は捨てる。
 * 板の広がりは視点空間で足すので、どの向きから見てもカメラを向き、遠いほど小さく見える。
 *
 * 解釈: 固定はピースに隠れていても分からないと困るので depthTest を切って常に見えるようにする
 * （アイコンはボクセルの内側にあるので、深度テストを残すと自分のボクセルに隠れてしまう）。
 */
function createBillboardMaterial(texture: THREE.Texture): THREE.MeshBasicMaterial {
  const material = new THREE.MeshBasicMaterial({
    map: texture,
    transparent: true,
    depthTest: false,
    depthWrite: false,
  });
  material.onBeforeCompile = (shader): void => {
    shader.vertexShader = shader.vertexShader.replace(
      '#include <project_vertex>',
      [
        'vec4 mvPosition = modelViewMatrix * vec4( instanceMatrix[3].xyz, 1.0 );',
        'mvPosition.xy += position.xy * length( instanceMatrix[0].xyz );',
        'gl_Position = projectionMatrix * mvPosition;',
      ].join('\n'),
    );
  };
  return material;
}

/** 固定中のピースに出すアイコン群。位置は PieceViews が配置更新のたびに書き込む。 */
export type LockIcons = {
  /** ピースのルート（PieceViews.object）に add する。 */
  readonly object: THREE.Object3D;
  /** ピースのアイコンの種類を差し替える。null で消す。 */
  set(pieceId: number, kind: LockIconKind | null): void;
  /**
   * ピース pieceId の index 番目のボクセル中心を書き込む。アイコンが出ていないピースでも覚えておく。
   * 書き終えたら flush() を呼ぶ。
   */
  setPosition(pieceId: number, index: number, x: number, y: number, z: number): void;
  /** setPosition の結果を GPU へ反映する。 */
  flush(): void;
  /** ジオメトリ / マテリアル / テクスチャを解放する。 */
  dispose(): void;
};

/** ピース 1 つ分のインスタンス区間。 */
type Range = {
  readonly start: number;
  readonly count: number;
};

/**
 * 全ピースのボクセル数の合計だけスロットを持つロックアイコンのセットを作る。
 * テクスチャ・マテリアル・InstancedMesh は種類ごとに 1 つで、全ピースで共有する。
 */
export function createLockIcons(pieces: readonly Piece[]): LockIcons {
  const total = pieces.reduce((sum, piece): number => sum + piece.voxels.length, 0);
  const capacity = Math.max(total, 1);

  const object = new THREE.Group();
  object.name = 'lock-icons';
  // ピース本体・発光コアより後ろに描く（depthTest も切ってあるので常に手前に見える）
  object.renderOrder = 2;

  const ranges = new Map<number, Range>();
  let cursor = 0;
  for (const piece of pieces) {
    ranges.set(piece.id, { start: cursor, count: piece.voxels.length });
    cursor += piece.voxels.length;
  }

  // 使っていないスロットの行列。スケール 0 なので板が潰れて何も描かれない
  const collapsed = new THREE.Matrix4().makeScale(0, 0, 0);
  const geometry = new THREE.PlaneGeometry(ICON_SIZE, ICON_SIZE);
  const textures: THREE.Texture[] = [];
  const materials: THREE.Material[] = [];
  const meshes = new Map<LockIconKind, THREE.InstancedMesh>();
  for (const kind of KINDS) {
    const texture = createPadlockTexture(PALETTES[kind]);
    const material = createBillboardMaterial(texture);
    textures.push(texture);
    materials.push(material);
    const mesh = new THREE.InstancedMesh(geometry, material, capacity);
    mesh.name = `lock-icons-${kind}`;
    mesh.frustumCulled = false;
    mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
    mesh.renderOrder = 2;
    // ピース選択のレイキャストに拾わせない（userData.pieceId も付けない）
    mesh.raycast = (): void => {};
    for (let i = 0; i < capacity; i += 1) mesh.setMatrixAt(i, collapsed);
    // その種類の固定が 1 つも無い間は描かない（ドローコールを増やさない）
    mesh.visible = false;
    meshes.set(kind, mesh);
    object.add(mesh);
  }

  // ボクセル中心の位置（スロットごとに xyz）。固定していないピースの分も覚えておく
  const positions = new Float32Array(capacity * 3);
  // いまアイコンを出しているピースとその種類
  const kinds = new Map<number, LockIconKind>();
  const matrix = new THREE.Matrix4();
  const point = new THREE.Vector3();

  return {
    object,

    set(pieceId, kind): void {
      const range = ranges.get(pieceId);
      if (!range || (kinds.get(pieceId) ?? null) === kind) return;
      if (kind === null) kinds.delete(pieceId);
      else kinds.set(pieceId, kind);
      // そのピースのスロットを、出す種類のメッシュでは位置へ、それ以外では潰した行列にする
      for (const [meshKind, mesh] of meshes) {
        for (let i = range.start; i < range.start + range.count; i += 1) {
          if (meshKind === kind) {
            point.fromArray(positions, i * 3);
            matrix.makeTranslation(point.x, point.y, point.z);
            mesh.setMatrixAt(i, matrix);
          } else {
            mesh.setMatrixAt(i, collapsed);
          }
        }
        mesh.instanceMatrix.needsUpdate = true;
      }
      const used = new Set(kinds.values());
      for (const [meshKind, mesh] of meshes) mesh.visible = used.has(meshKind);
    },

    setPosition(pieceId, index, x, y, z): void {
      const range = ranges.get(pieceId);
      if (!range || index < 0 || index >= range.count) return;
      const slot = range.start + index;
      point.set(x, y, z).toArray(positions, slot * 3);
      const kind = kinds.get(pieceId);
      if (kind === undefined) return;
      matrix.makeTranslation(x, y, z);
      meshes.get(kind)?.setMatrixAt(slot, matrix);
    },

    flush(): void {
      // 描いていない種類は set() で出すときに書き直すので、送らなくてよい
      for (const mesh of meshes.values()) {
        if (mesh.visible) mesh.instanceMatrix.needsUpdate = true;
      }
    },

    dispose(): void {
      kinds.clear();
      object.removeFromParent();
      for (const mesh of meshes.values()) mesh.dispose();
      meshes.clear();
      object.clear();
      geometry.dispose();
      for (const material of materials) material.dispose();
      for (const texture of textures) texture.dispose();
    },
  };
}
