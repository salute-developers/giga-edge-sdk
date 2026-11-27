import * as THREE from 'three';
import type { Kit } from './geometry';

/** What the assistant is doing, as its speaker shows it; a live state lasts until `until` (performance.now()). */
export type Voice = { state: 'off' | 'idle' | 'listening' | 'thinking' | 'answering'; until: number };
/** The id the pointer and the selection know the speaker by. */
export const SPEAKER = 'speaker';
const words: Record<Voice['state'], string> = { off: 'выключена', idle: 'ждёт команды', listening: 'слушает', thinking: 'думает', answering: 'отвечает' };
/** The state shown at `now`: a live one ends at its `until`. */
const stateAt = (voice: Voice, now: number) => voice.state !== 'off' && now >= voice.until ? 'idle' : voice.state;
export const voiceText = (voice: Voice, now: number) => words[stateAt(voice, now)];
// SIZE: twice life size, so that it reads from the whole house.
const SEGMENTS = 16, SIZE = 2;

/** A soft round glow, white in the middle, for a sprite tinted by its material. */
function glowTexture() {
  const canvas = document.createElement('canvas'); canvas.width = canvas.height = 128;
  const ctx = canvas.getContext('2d')!, gradient = ctx.createRadialGradient(64, 64, 0, 64, 64, 64);
  gradient.addColorStop(0, 'rgba(255,255,255,.9)'); gradient.addColorStop(.35, 'rgba(255,255,255,.45)'); gradient.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = gradient; ctx.fillRect(0, 0, 128, 128);
  return new THREE.CanvasTexture(canvas);
}

/** The speaker of the assistant standing at `[x, y, z]`: a light fabric column with a ring of lights on a dark top, a
 * halo over it and a light that falls on what it stands on. The ring glows steadily while the assistant waits, the halo
 * pulses while it listens, the light runs round while it thinks and flickers while it answers. Returns the speaker and
 * its update, which tells whether it needs a frame: while it is animated, and when what it shows changes. */
export function buildSpeaker(kit: Kit, world: THREE.Object3D, [x, y, z]: number[]) {
  const g = new THREE.Group(); g.position.set(x, y, z); g.scale.setScalar(SIZE); world.add(g);
  kit.cylinder(0, .008, 0, .1, .016, '#2b3439', g);
  kit.cylinder(0, .158, 0, .112, .284, '#d9dfe3', g);
  kit.cylinder(0, .304, 0, .104, .008, '#1f272c', g);
  const display = kit.box(0, .2, .108, .09, .05, .012, '#3a454c', g);
  const ring = Array.from({ length: SEGMENTS }, (_, i) => {
    const angle = i / SEGMENTS * 2 * Math.PI, light = kit.box(Math.cos(angle) * .08, .311, Math.sin(angle) * .08, .014, .006, .026, '#3d4a52', g);
    light.rotation.y = -angle;
    return light;
  });
  g.traverse(part => { part.userData.deviceId = SPEAKER; });
  // Each state has its own tint, which reads even when the speaker is small.
  const tints = { off: new THREE.Color('#000000'), idle: new THREE.Color('#7fdcff'), listening: new THREE.Color('#5fd0ff'), thinking: new THREE.Color('#a98bff'), answering: new THREE.Color('#c8f4ff') };
  const texture = glowTexture();
  const haloMaterial = new THREE.SpriteMaterial({ map: texture, color: tints.idle, blending: THREE.AdditiveBlending, transparent: true, depthWrite: false });
  kit.textures.push(texture); kit.materials.push(haloMaterial);
  const halo = new THREE.Sprite(haloMaterial); halo.position.y = .32; g.add(halo);
  const lamp = kit.lamps.add(g, new THREE.Vector3(0, .36, 0), 1.6, tints.idle);
  let shown: Voice['state'] | undefined;
  const update = (voice: Voice, now: number, animate: boolean) => {
    const state = stateAt(voice, now), t = animate ? now / 1000 : 0, changed = state !== shown;
    shown = state;
    const pulse = Math.sin(t * 4) ** 2, flicker = Math.abs(Math.sin(t * 11) * Math.sin(t * 3.1)), tint = tints[state];
    ring.forEach((light, i) => {
      const level = state === 'off' ? 0 : state === 'idle' ? .8
        : state === 'listening' ? .7 + .8 * pulse
        : state === 'thinking' ? .15 + 1.4 * Math.max(0, Math.cos(t * 5 - i / SEGMENTS * 2 * Math.PI)) ** 6
        : .7 + .8 * Math.abs(Math.sin(t * 11 + i * .7) * Math.sin(t * 3.1));
      light.material.emissive.copy(tint); light.material.emissiveIntensity = level * 2;
    });
    const strength = state === 'off' ? 0 : state === 'idle' ? .55 : state === 'listening' ? .45 + .55 * pulse : state === 'thinking' ? .6 : .55 + .45 * flicker;
    haloMaterial.color.copy(tint); haloMaterial.opacity = strength; halo.scale.setScalar(.32 + (state === 'listening' ? .18 * pulse : 0));
    lamp.color.copy(tint); lamp.intensity = strength * 2.2; kit.lamps.shine(lamp);
    display.material.emissive.copy(tint); display.material.emissiveIntensity = state === 'answering' ? .35 + .4 * flicker : 0;
    return changed || animate && state !== 'off' && state !== 'idle';
  };
  return { object: g, update };
}
