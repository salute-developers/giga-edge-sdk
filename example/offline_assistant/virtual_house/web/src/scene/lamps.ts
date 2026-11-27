import * as THREE from 'three';

/** A lamp lights like a point light with a decay of 2 and no shadow; `color` and `intensity` are those of the light. */
export type Lamp = { color: THREE.Color; intensity: number };
type Source = Lamp & { index: number; parent: THREE.Object3D; at: THREE.Vector3; range: number; position: THREE.Vector3; shone: THREE.Color };
/** How much of one lamp the vertices of a mesh get, from vertex `start` on; the rest get none. */
type Share = { start: number; values: Float32Array };
type Lit = { mesh: THREE.Mesh; light: THREE.BufferAttribute; matrix: THREE.Matrix4; shares: (Share | undefined)[] };

function addLampLight(shader: THREE.WebGLProgramParametersWithUniforms) {
  shader.vertexShader = shader.vertexShader
    .replace('#include <common>', '#include <common>\nattribute vec3 lampLight;\nvarying vec3 vLampLight;')
    .replace('#include <begin_vertex>', '#include <begin_vertex>\nvLampLight = lampLight;');
  shader.fragmentShader = shader.fragmentShader
    .replace('#include <common>', '#include <common>\nvarying vec3 vLampLight;')
    .replace('#include <lights_fragment_end>', '#include <lights_fragment_end>\nreflectedLight.directDiffuse += max(vLampLight, 0.0) * BRDF_Lambert(diffuseColor.rgb);');
}

/** A Lambert material that also takes the light of the lamps, which its mesh carries per vertex. */
export function lampLit(material: THREE.MeshLambertMaterial) {
  material.onBeforeCompile = addLampLight;
  return material;
}

/** The lamps of a house. The light they cast on every vertex is computed once, when the house is built; a change of
 * a lamp adds the difference of its own light only, and a mesh that moves gets its light again. */
export function makeLamps() {
  const sources: Source[] = [], lit: Lit[] = [];
  const point = new THREE.Vector3(), normal = new THREE.Vector3(), toLamp = new THREE.Vector3();
  const normalMatrix = new THREE.Matrix3(), sphere = new THREE.Sphere(), shown = new THREE.Color();

  function bake(entry: Lit) {
    const { mesh, matrix } = entry, geometry = mesh.geometry;
    const positions = geometry.getAttribute('position'), normals = geometry.getAttribute('normal'), sum = entry.light.array as Float32Array;
    matrix.copy(mesh.matrixWorld); normalMatrix.getNormalMatrix(matrix);
    if (!geometry.boundingSphere) geometry.computeBoundingSphere();
    sphere.copy(geometry.boundingSphere!).applyMatrix4(matrix);
    sum.fill(0);
    entry.shares = sources.map(source => {
      if (source.position.distanceTo(sphere.center) >= source.range + sphere.radius) return undefined;
      const values = new Float32Array(positions.count);
      let start = positions.count, end = 0;
      for (let i = 0; i < positions.count; i++) {
        toLamp.subVectors(source.position, point.fromBufferAttribute(positions, i).applyMatrix4(matrix));
        const distance = toLamp.length();
        if (distance >= source.range) continue;
        const facing = normal.fromBufferAttribute(normals, i).applyMatrix3(normalMatrix).normalize().dot(toLamp) / distance;
        if (facing <= 0) continue;
        // The falloff of a point light with a cutoff distance, as the renderer computes it.
        values[i] = facing / Math.max(distance * distance, .01) * (1 - (distance / source.range) ** 4) ** 2;
        start = Math.min(start, i); end = i + 1;
      }
      if (start >= end) return undefined;
      const share = { start, values: values.slice(start, end) };
      add(sum, share, source.shone.r, source.shone.g, source.shone.b);
      return share;
    });
  }
  function add(sum: Float32Array, { start, values }: Share, r: number, g: number, b: number) {
    for (let i = 0, j = start * 3; i < values.length; i++, j += 3) {
      sum[j] += values[i] * r; sum[j + 1] += values[i] * g; sum[j + 2] += values[i] * b;
    }
  }

  return {
    /** A lamp at `at` in `parent` that reaches as far as `range`; it starts dark. */
    add(parent: THREE.Object3D, at: THREE.Vector3, range: number, color: THREE.ColorRepresentation): Lamp {
      const source = { index: sources.length, parent, at, range, color: new THREE.Color(color), intensity: 0, position: new THREE.Vector3(), shone: new THREE.Color(0, 0, 0) };
      sources.push(source);
      return source;
    },
    /** Lights the meshes of `world` with the lamps added so far. */
    light(world: THREE.Object3D) {
      world.updateMatrixWorld(true);
      for (const source of sources) source.position.copy(source.at).applyMatrix4(source.parent.matrixWorld);
      world.traverse(mesh => {
        if (!(mesh instanceof THREE.Mesh) || !(mesh.material instanceof THREE.MeshLambertMaterial) || mesh.material.onBeforeCompile !== addLampLight) return;
        const light = new THREE.BufferAttribute(new Float32Array(mesh.geometry.getAttribute('position').count * 3), 3);
        mesh.geometry.setAttribute('lampLight', light);
        const entry: Lit = { mesh, light, matrix: new THREE.Matrix4(), shares: [] };
        bake(entry); lit.push(entry);
      });
    },
    /** Shows the current color and intensity of a lamp. */
    shine(lamp: Lamp) {
      const source = lamp as Source;
      shown.copy(source.color).multiplyScalar(source.intensity);
      const r = shown.r - source.shone.r, g = shown.g - source.shone.g, b = shown.b - source.shone.b;
      if (Math.max(Math.abs(r), Math.abs(g), Math.abs(b)) < 1e-4) return;
      source.shone.copy(shown);
      for (const entry of lit) {
        const share = entry.shares[source.index];
        if (!share) continue;
        add(entry.light.array as Float32Array, share, r, g, b);
        entry.light.addUpdateRange(share.start * 3, share.values.length * 3); entry.light.needsUpdate = true;
      }
    },
    /** Lights again the meshes that moved since they were lit; call it after the world matrices are updated. */
    follow() {
      for (const entry of lit) {
        if (entry.mesh.matrixWorld.equals(entry.matrix)) continue;
        bake(entry);
        entry.light.clearUpdateRanges(); entry.light.needsUpdate = true;
      }
    },
  };
}
