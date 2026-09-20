// ピースの固定（ロック）を示す南京錠アイコン。
// 銀 = 人が固定したもの（手動固定）、金 = ヒントで正解位置へ送ったもの。テクスチャは
// CanvasTexture で 2 枚だけ作って全ピースで共有し、Sprite だけを固定中のピースぶん生成する。
//
// ドローコール: 固定中のピース 1 個につき Sprite 1 個 = 1 ドローコール増える。
// 人が固定するのはたいてい数個なので、SPEC.md 4 章の「ピース数 + α」の α に収まる。
// （固定を解除した時点で Sprite は捨てるので、増えたままにはならない）

import * as THREE from 'three';

/** 固定の種類。core の LockKind と同じ並びだが、描画側は色の選択にしか使わない。 */
export type LockIconKind = 'manual' | 'hint';

/**
 * アイコンの大きさ（ワールド単位 = ボクセル 1 個ぶんが 1）。
 * 解釈: ボクセル 1 個より少し小さくして、ピースの形を隠さずに中心が分かる程度にする。
 */
const ICON_SIZE = 0.7;

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

/** 固定中のピースに出すアイコン群。位置は PieceViews が配置更新のたびに書き込む。 */
export type LockIcons = {
  /** ピースのルート（PieceViews.object）に add する。 */
  readonly object: THREE.Object3D;
  /** ピースのアイコンを差し替える。null で消す（Sprite は捨てる）。 */
  set(pieceId: number, kind: LockIconKind | null): void;
  /** アイコンを置く位置（ピースの重心）。アイコンが出ていないピースでも覚えておく。 */
  setPosition(pieceId: number, x: number, y: number, z: number): void;
  /** テクスチャ / マテリアル / Sprite を解放する。 */
  dispose(): void;
};

/**
 * ロックアイコンのセットを作る。
 * テクスチャとマテリアルは種類ごとに 1 つだけ作り、Sprite は必要になるまで作らない。
 */
export function createLockIcons(): LockIcons {
  const object = new THREE.Group();
  object.name = 'lock-icons';
  // ピース本体・発光コアより後ろに描く（depthTest も切ってあるので常に手前に見える）
  object.renderOrder = 2;

  const textures = new Map<LockIconKind, THREE.CanvasTexture>();
  const materials = new Map<LockIconKind, THREE.SpriteMaterial>();
  const sprites = new Map<number, THREE.Sprite>();
  const positions = new Map<number, THREE.Vector3>();

  /**
   * 解釈: 固定はピースに隠れていても分からないと困るので depthTest を切って常に見えるようにする。
   * sizeAttenuation は既定（true）のまま = 遠いピースのアイコンは小さく見える。
   */
  const materialFor = (kind: LockIconKind): THREE.SpriteMaterial => {
    const existing = materials.get(kind);
    if (existing) return existing;
    const texture = createPadlockTexture(PALETTES[kind]);
    textures.set(kind, texture);
    const material = new THREE.SpriteMaterial({
      map: texture,
      transparent: true,
      depthTest: false,
      depthWrite: false,
    });
    materials.set(kind, material);
    return material;
  };

  return {
    object,

    set(pieceId, kind): void {
      const existing = sprites.get(pieceId);
      if (kind === null) {
        if (!existing) return;
        existing.removeFromParent();
        sprites.delete(pieceId);
        return;
      }
      const material = materialFor(kind);
      if (existing) {
        // 種類だけの入れ替えなら Sprite は使い回す
        existing.material = material;
        return;
      }
      const sprite = new THREE.Sprite(material);
      sprite.name = `lock-icon-${pieceId}`;
      sprite.scale.setScalar(ICON_SIZE);
      sprite.renderOrder = 2;
      // ピース選択のレイキャストに拾わせない（userData.pieceId も付けない）
      sprite.raycast = (): void => {};
      const position = positions.get(pieceId);
      if (position) sprite.position.copy(position);
      sprites.set(pieceId, sprite);
      object.add(sprite);
    },

    setPosition(pieceId, x, y, z): void {
      const position = positions.get(pieceId);
      if (position) position.set(x, y, z);
      else positions.set(pieceId, new THREE.Vector3(x, y, z));
      sprites.get(pieceId)?.position.set(x, y, z);
    },

    dispose(): void {
      for (const sprite of sprites.values()) sprite.removeFromParent();
      sprites.clear();
      positions.clear();
      object.removeFromParent();
      object.clear();
      for (const material of materials.values()) material.dispose();
      materials.clear();
      for (const texture of textures.values()) texture.dispose();
      textures.clear();
    },
  };
}
