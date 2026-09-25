// クリア演出（RULES.md 5.2）。クリア判定が真になった瞬間から 2〜3 秒で
// 「発光 → 融合 → パーティクル → カメラ旋回」をこの順に再生する。
//
// 進行はすべて経過時間（秒）で持つので、フレームレートが変わっても同じ長さで再生される。
// 解釈: SPEC.md 10 章の「重いポストプロセスは初版のスコープ外」に従い、ブルームは入れず
// 材質の emissive と背景色の持ち上げだけで「画面全体が明るくなる」を表現する。

import * as THREE from 'three';

/** 発光が最大に達するまで（秒）。 */
const GLOW_END = 0.8;
/** 融合（単一立方体へのクロスフェード）の開始と終了（秒）。 */
const MERGE_START = 0.7;
const MERGE_END = 1.5;
/** パーティクルを撒く時刻（秒）。融合の直前から広がり始める。 */
const PARTICLE_AT = 1.35;
/** カメラの自動旋回を始める時刻（秒）。 */
const CAMERA_AT = 1.7;
/** 演出全体の長さ（秒）。RULES.md 5.2 の「2〜3 秒」。 */
const TOTAL = 2.6;

/** カメラの自動旋回の速さ（rad/s）。1 周およそ 18 秒の「ゆっくり」。 */
const AUTO_ROTATE_SPEED = 0.35;

/** 粒子の数（RULES.md 5.2-3 の「数百〜千粒」）。 */
const PARTICLE_COUNT = 700;
/** 粒子の寿命（秒）。 */
const PARTICLE_LIFE = 1.4;
/** 粒子の初速（立方体の 1 辺 N に対する倍率 / 秒）。 */
const PARTICLE_SPEED_MIN = 0.5;
const PARTICLE_SPEED_MAX = 1.4;
/** 粒子の減速（1 秒あたりに残る速度の割合）。 */
const PARTICLE_DRAG = 0.35;

/** 背景をどれだけ明るい側へ寄せるか（0 = 元のまま、1 = 完全に BACKGROUND_PEAK）。 */
const BACKGROUND_LIFT = 0.45;
/** 明るくなったときの背景の寄せ先。 */
const BACKGROUND_PEAK = new THREE.Color(0x9fc4ff);

/** 演出から外へ出す操作。描画層のほかの部品（PieceViews / OrbitCamera）につなぐ。 */
export type ClearEffectHooks = {
  /** 内部コアとピース本体の発光を 0..1 で持ち上げる。 */
  readonly setGlow: (amount: number) => void;
  /** ピース群（個別の InstancedMesh とコア）の表示。融合が終わると false になる。 */
  readonly setPiecesVisible: (visible: boolean) => void;
  /** カメラの自動旋回速度（rad/s）。0 で停止。 */
  readonly setAutoRotate: (speed: number) => void;
};

export type ClearEffectOptions = ClearEffectHooks & {
  readonly scene: THREE.Scene;
  /** 解答空間のサイズ N。融合後の立方体の 1 辺になる。 */
  readonly n: number;
  /** 融合後の立方体の色。ピース色を混ぜたものを渡す想定。 */
  readonly color: THREE.Color;
};

export type ClearEffect = {
  /** 毎フレーム呼ぶ。delta は秒。 */
  update(deltaSeconds: number): void;
  /** 発光〜パーティクルまでが終わったか（カメラ旋回は解除まで続く）。 */
  finished(): boolean;
  /** 演出とカメラ旋回を解除し、GPU 資源を解放する。 */
  dispose(): void;
};

/**
 * 融合後の巨大クリスタル。
 * 解釈: transmission と transparent の併用は three では推奨されないが、RULES.md 5.2-2 の
 * 「差し替えの瞬間が飛ばないようクロスフェードする」には opacity が要る。opacity を使うのは
 * 融合の 0.8 秒だけで、その間はピース本体も出ているため破綻しても目立たない。
 */
function createCrystalMaterial(color: THREE.Color, n: number): THREE.MeshPhysicalMaterial {
  return new THREE.MeshPhysicalMaterial({
    color,
    roughness: 0.3,
    metalness: 0,
    transmission: 0.9,
    thickness: n * 0.8,
    ior: 1.5,
    clearcoat: 0.8,
    clearcoatRoughness: 0.15,
    envMapIntensity: 1.6,
    emissive: color.clone().multiplyScalar(0.55),
    transparent: true,
    opacity: 0,
  });
}

/** 立方体中心から放射状に飛ぶ粒子。速度と寿命はここで自前に更新する。 */
type Particles = {
  readonly points: THREE.Points;
  /** 進める。寿命が尽きたら false を返す。 */
  advance(delta: number): boolean;
  dispose(): void;
};

function createParticles(color: THREE.Color, n: number): Particles {
  const positions = new Float32Array(PARTICLE_COUNT * 3);
  const colors = new Float32Array(PARTICLE_COUNT * 3);
  const velocities = new Float32Array(PARTICLE_COUNT * 3);
  const tint = color.clone().lerp(new THREE.Color(0xffffff), 0.6);

  for (let i = 0; i < PARTICLE_COUNT; i += 1) {
    // 球面上の一様分布。中心から放射状に散らす（RULES.md 5.2-3）
    const u = Math.random() * 2 - 1;
    const theta = Math.random() * Math.PI * 2;
    const r = Math.sqrt(Math.max(1 - u * u, 0));
    const speed =
      n * (PARTICLE_SPEED_MIN + Math.random() * (PARTICLE_SPEED_MAX - PARTICLE_SPEED_MIN));
    const vx = r * Math.cos(theta) * speed;
    const vy = u * speed;
    const vz = r * Math.sin(theta) * speed;
    velocities[i * 3] = vx;
    velocities[i * 3 + 1] = vy;
    velocities[i * 3 + 2] = vz;
    // 立方体の中心から、少しだけばらけた位置で生まれる
    positions[i * 3] = vx * 0.05;
    positions[i * 3 + 1] = vy * 0.05;
    positions[i * 3 + 2] = vz * 0.05;
    colors[i * 3] = tint.r;
    colors[i * 3 + 1] = tint.g;
    colors[i * 3 + 2] = tint.b;
  }

  const geometry = new THREE.BufferGeometry();
  const positionAttribute = new THREE.BufferAttribute(positions, 3);
  positionAttribute.setUsage(THREE.DynamicDrawUsage);
  const colorAttribute = new THREE.BufferAttribute(colors, 3);
  colorAttribute.setUsage(THREE.DynamicDrawUsage);
  geometry.setAttribute('position', positionAttribute);
  geometry.setAttribute('color', colorAttribute);
  // frustumCulled を切るので境界球は使わないが、three が要求する場合に備えて入れておく
  geometry.boundingSphere = new THREE.Sphere(new THREE.Vector3(), n * 4);

  const material = new THREE.PointsMaterial({
    size: Math.max(n * 0.05, 0.12),
    vertexColors: true,
    transparent: true,
    depthWrite: false,
    blending: THREE.AdditiveBlending,
    sizeAttenuation: true,
  });

  const points = new THREE.Points(geometry, material);
  points.name = 'clear-particles';
  points.frustumCulled = false;
  points.renderOrder = 2;

  let age = 0;

  return {
    points,
    advance(delta: number): boolean {
      age += delta;
      const life = THREE.MathUtils.clamp(1 - age / PARTICLE_LIFE, 0, 1);
      // 終わり際にすっと消えるよう、寿命の 2 乗で暗くする
      const fade = life * life;
      const decay = PARTICLE_DRAG ** delta;
      for (let i = 0; i < PARTICLE_COUNT; i += 1) {
        const b = i * 3;
        positions[b] = (positions[b] ?? 0) + (velocities[b] ?? 0) * delta;
        positions[b + 1] = (positions[b + 1] ?? 0) + (velocities[b + 1] ?? 0) * delta;
        positions[b + 2] = (positions[b + 2] ?? 0) + (velocities[b + 2] ?? 0) * delta;
        velocities[b] = (velocities[b] ?? 0) * decay;
        velocities[b + 1] = (velocities[b + 1] ?? 0) * decay;
        velocities[b + 2] = (velocities[b + 2] ?? 0) * decay;
        colors[b] = tint.r * fade;
        colors[b + 1] = tint.g * fade;
        colors[b + 2] = tint.b * fade;
      }
      positionAttribute.needsUpdate = true;
      colorAttribute.needsUpdate = true;
      return age < PARTICLE_LIFE;
    },
    dispose(): void {
      points.removeFromParent();
      geometry.dispose();
      material.dispose();
    },
  };
}

/**
 * クリア演出を開始する。作った時点で 0 秒目から始まり、あとは update(delta) で進む。
 * 立方体は原点中心に置く（PieceViews / SolutionFrame と同じで、解答空間の中心が原点にある）。
 */
export function createClearEffect(options: ClearEffectOptions): ClearEffect {
  const { scene, n, color } = options;

  const geometry = new THREE.BoxGeometry(n, n, n);
  const material = createCrystalMaterial(color, n);
  const cube = new THREE.Mesh(geometry, material);
  cube.name = 'clear-crystal';
  cube.visible = false;
  scene.add(cube);

  // 背景の持ち上げ用に元の色を控える（Color 以外の背景には手を出さない）
  const background = scene.background instanceof THREE.Color ? scene.background : null;
  const originalBackground = background?.clone() ?? null;

  let elapsed = 0;
  let piecesHidden = false;
  let rotating = false;
  let particles: Particles | null = null;
  let particlesSpawned = false;
  let disposed = false;

  const setBackgroundLift = (amount: number): void => {
    if (!background || !originalBackground) return;
    background.copy(originalBackground).lerp(BACKGROUND_PEAK, BACKGROUND_LIFT * amount);
  };

  return {
    update(deltaSeconds: number): void {
      if (disposed) return;
      // タブ復帰などで delta が跳ねても演出が飛ばないよう上限を掛ける
      const delta = THREE.MathUtils.clamp(deltaSeconds, 0, 0.1);
      elapsed += delta;

      // 1. 発光: 内部コアとピース本体の emissive を最大まで上げ、背景も明るくする
      const glow = THREE.MathUtils.smoothstep(elapsed, 0, GLOW_END);
      options.setGlow(glow);
      setBackgroundLift(glow);

      // 2. 融合: 単一の立方体をクロスフェードで出し、出し切ったらピース群を隠す
      if (elapsed >= MERGE_START) {
        const t = THREE.MathUtils.smoothstep(elapsed, MERGE_START, MERGE_END);
        cube.visible = true;
        material.opacity = t;
        material.emissive.copy(color).multiplyScalar(0.55 + 0.9 * t);
        if (t >= 1 && !piecesHidden) {
          piecesHidden = true;
          options.setPiecesVisible(false);
        }
      }

      // 3. パーティクル: 立方体の中心から放射状に 1 回だけ撒く
      if (!particlesSpawned && elapsed >= PARTICLE_AT) {
        particlesSpawned = true;
        particles = createParticles(color, n);
        scene.add(particles.points);
      }
      if (particles && !particles.advance(delta)) {
        particles.dispose();
        particles = null;
      }

      // 4. カメラ: 立方体の周囲をゆっくり自動旋回する（手動操作は main 側で止めてある）
      if (!rotating && elapsed >= CAMERA_AT) {
        rotating = true;
        options.setAutoRotate(AUTO_ROTATE_SPEED);
      }
    },

    finished(): boolean {
      return elapsed >= TOTAL;
    },

    dispose(): void {
      if (disposed) return;
      disposed = true;
      options.setAutoRotate(0);
      options.setGlow(0);
      options.setPiecesVisible(true);
      if (background && originalBackground) background.copy(originalBackground);
      particles?.dispose();
      particles = null;
      cube.removeFromParent();
      geometry.dispose();
      material.dispose();
    },
  };
}
