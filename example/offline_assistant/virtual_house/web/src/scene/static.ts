import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import type { Room } from '../types';
import type { Kit } from './geometry';
import { lampLit } from './lamps';

/** Batch the fixed decoration before devices are added, keeping room-sized bounds for culling. */
export function mergeStatic(world: THREE.Scene, kit: Kit, rooms: Room[]) {
  const batches = new Map<string, THREE.Mesh<THREE.BufferGeometry, THREE.MeshLambertMaterial>[]>();
  for (const object of world.children) {
    if (!(object instanceof THREE.Mesh) || !(object.material instanceof THREE.MeshLambertMaterial)) continue;
    const room = rooms.find(r => Math.abs(object.position.x - r.x) <= r.w / 2 && Math.abs(object.position.z - r.z) <= r.d / 2);
    const key = `${room?.name ?? ''}:${object.castShadow}:${object.receiveShadow}`;
    const batch = batches.get(key) ?? [];
    batch.push(object); batches.set(key, batch);
  }
  for (const objects of batches.values()) {
    if (objects.length < 2) continue;
    const parts = objects.map(object => {
      object.updateMatrix();
      const geometry = object.geometry.clone().applyMatrix4(object.matrix);
      const color = object.material.color, colors = new Float32Array(geometry.getAttribute('position').count * 3);
      for (let i = 0; i < colors.length; i += 3) { colors[i] = color.r; colors[i + 1] = color.g; colors[i + 2] = color.b; }
      geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
      return geometry;
    });
    const geometry = mergeGeometries(parts)!;
    parts.forEach(part => part.dispose());
    const material = lampLit(new THREE.MeshLambertMaterial({ vertexColors: true }));
    const mesh = new THREE.Mesh(geometry, material);
    mesh.castShadow = objects[0].castShadow; mesh.receiveShadow = objects[0].receiveShadow;
    geometry.computeBoundingBox(); geometry.computeBoundingSphere();
    mesh.updateMatrix(); mesh.matrixAutoUpdate = false;
    mesh.userData.collisionBounds = objects.map(object => new THREE.Box3().setFromObject(object));
    world.add(mesh); kit.geometries.push(geometry); kit.materials.push(material);
    for (const object of objects) {
      world.remove(object);
      object.geometry.dispose(); object.material.dispose();
      kit.geometries.splice(kit.geometries.indexOf(object.geometry), 1);
      kit.materials.splice(kit.materials.indexOf(object.material), 1);
    }
  }
}
