import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { House } from '../types';
import { summary } from './devices';
import { buildHouse } from './geometry';
import { SPEAKER, voiceText, type Voice } from './speaker';

/** The 3D house: built once from the rooms and the devices, then animated towards the current values. */
export function HouseView({ house, voice, selected, onSelect }: { house: House; voice: Voice; selected: string; onSelect: (id: string) => void }) {
  const host = useRef<HTMLDivElement>(null);
  const caption = useRef<HTMLDivElement>(null);
  const current = useRef({ voice, selected, onSelect, devices: new Map(house.devices.map(d => [d.device_id, d])) });
  current.current = { voice, selected, onSelect, devices: new Map(house.devices.map(d => [d.device_id, d])) };
  const shadowState = JSON.stringify(house.devices.filter(d => ['curtain', 'gate', 'window_blind', 'sensor_door', 'valve', 'hvac_humidifier'].includes(d.kind)).map(d => [d.device_id, d.parameters.map(p => p.value)]));
  const previousShadows = useRef(shadowState);
  const resetCamera = useRef<() => void>(() => {});
  const redraw = useRef<(moved: boolean) => void>(() => {});
  const [error, setError] = useState('');
  // The values change with every event; the geometry depends only on what the house file describes.
  const layout = useRef(house);
  useLayoutEffect(() => {
    const container = host.current!;
    let renderer: THREE.WebGLRenderer;
    // Multisampling is left to screens below a density of 2, where without it the edges show steps.
    try { renderer = new THREE.WebGLRenderer({ antialias: window.devicePixelRatio < 2, alpha: true }); }
    catch { setError('3D недоступно: браузеру нужен WebGL. Устройства доступны в списке ниже.'); return; }
    // The scene is drawn at the density of the screen, up to 2.
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.autoUpdate = false;
    renderer.shadowMap.type = THREE.PCFShadowMap;
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    container.appendChild(renderer.domElement);
    renderer.domElement.setAttribute('aria-label', `3D: ${layout.current.name}. Вращайте мышью, приближайте колесом. Устройства также доступны в списке.`);
    const world = new THREE.Scene();
    const { bindings, deviceObjects, plan, speaker, followLamps, geometries, materials, textures } = buildHouse(world, layout.current);
    const reach = Math.max(plan.w, plan.d * 1.75);
    const camera = new THREE.PerspectiveCamera(38, 1, .1, 200);
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.minDistance = 6;
    controls.maxDistance = reach * 2.5;
    controls.maxPolarAngle = Math.PI / 2.15;
    controls.minPolarAngle = .15;
    resetCamera.current = () => { camera.position.set(plan.x + reach * .16, reach * .72, plan.z + reach * .8); controls.target.set(plan.x, 0, plan.z + .3); controls.update(); };
    resetCamera.current();
    world.add(new THREE.HemisphereLight('#c7e5ff', '#756653', 1.5));
    const sun = new THREE.DirectionalLight('#ffd9b1', 2.4);
    sun.position.set(plan.x - 8, 15, plan.z + 9); sun.target.position.set(plan.x, 0, plan.z); world.add(sun.target);
    sun.castShadow = true; sun.shadow.mapSize.set(1024, 1024);
    Object.assign(sun.shadow.camera, { left: -reach / 2 - 3, right: reach / 2 + 3, top: reach / 2 + 3, bottom: -reach / 2 - 3, far: 60 });
    sun.shadow.bias = -.001;
    world.add(sun);
    const bounds = new THREE.Box3();
    const outline = new THREE.Box3Helper(bounds, new THREE.Color('#67d8ff'));
    const outlineMaterial = outline.material as THREE.LineBasicMaterial;
    geometries.push(outline.geometry); materials.push(outlineMaterial);
    outlineMaterial.depthTest = false; world.add(outline);
    const raycaster = new THREE.Raycaster();
    let hovered = '';
    let dragging = false;
    let down = { x: 0, y: 0 };
    const hitDevice = (e: PointerEvent): string => {
      const r = renderer.domElement.getBoundingClientRect();
      raycaster.setFromCamera(new THREE.Vector2((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1), camera);
      const hit = raycaster.intersectObjects(world.children, true).find(hit => hit.object instanceof THREE.Mesh);
      return hit?.object.userData.deviceId ?? '';
    };
    const hover = (id: string) => { if (id !== hovered) { hovered = id; redraw.current(false); } };
    const pointerDown = (e: PointerEvent) => { down = { x: e.clientX, y: e.clientY }; dragging = true; hover(''); };
    const pointerUp = (e: PointerEvent) => {
      dragging = false;
      // A drag turns the camera; only a click selects.
      if (Math.hypot(e.clientX - down.x, e.clientY - down.y) > 5) return;
      current.current.onSelect(hitDevice(e));
    };
    const pointerMove = (e: PointerEvent) => {
      if (dragging) return;
      hover(hitDevice(e)); renderer.domElement.style.cursor = hovered ? 'pointer' : 'grab';
    };
    const pointerLeave = () => { hover(''); dragging = false; };
    const onLost = (e: Event) => { e.preventDefault(); setError('3D-контекст потерян. Перезагрузите страницу, чтобы восстановить сцену.'); };
    const listeners: [string, (e: never) => void][] = [['pointerdown', pointerDown], ['pointerup', pointerUp], ['pointermove', pointerMove], ['pointerleave', pointerLeave], ['pointercancel', pointerLeave], ['webglcontextlost', onLost]];
    for (const [name, listener] of listeners) renderer.domElement.addEventListener(name, listener as EventListener);
    const resize = new ResizeObserver(() => { const { width, height } = container.getBoundingClientRect(); if (!width || !height) return; camera.aspect = width / height; camera.updateProjectionMatrix(); renderer.setSize(width, height); redraw.current(false); });
    resize.observe(container);
    // A frame is drawn only when something changes. The values move the devices, which takes up to a second, and
    // only they move the shadows; the camera, the pointer and the size need the frame alone.
    let drawUntil = 0, shadowsUntil = 0;
    redraw.current = moved => {
      const until = performance.now() + 1500;
      drawUntil = Math.max(drawUntil, until);
      if (moved) shadowsUntil = until;
    };
    redraw.current(true);
    // The density of the screen changes when the window moves to another display or the page is zoomed.
    let density: MediaQueryList;
    const densityChanged = () => { renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2)); redraw.current(false); watchDensity(); };
    const watchDensity = () => { density = matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`); density.addEventListener('change', densityChanged, { once: true }); };
    watchDensity();
    controls.addEventListener('change', () => redraw.current(false));
    const shown = () => { if (!document.hidden) redraw.current(true); };
    document.addEventListener('visibilitychange', shown);
    let previous = performance.now();
    const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
    const anchor = new THREE.Vector3();
    renderer.setAnimationLoop(now => {
      if (document.hidden) { previous = now; return; }
      const seconds = reducedMotion ? 0 : Math.min(.1, (now - previous) / 1000);
      const dt = reducedMotion ? 1 : Math.min(1, (now - previous) / 140); previous = now;
      let busy = false;
      for (const binding of bindings) { const d = current.current.devices.get(binding.id); if (d && binding.update(d, dt, seconds)) busy = true; }
      controls.update();
      // The speaker only glows: its light needs frames, not shadows.
      const lit = speaker?.(current.current.voice, now, !reducedMotion) ?? false;
      if (!busy && !lit && now > drawUntil) return;
      renderer.shadowMap.needsUpdate = now <= shadowsUntil;
      world.updateMatrixWorld(true);
      followLamps();
      const id = hovered || current.current.selected;
      const object = deviceObjects.get(id), device = current.current.devices.get(id);
      const text = id === SPEAKER ? ['Колонка ассистента', `${layout.current.assistant.area ?? ''} · ${voiceText(current.current.voice, now)}`] : device && [device.name, summary(device)];
      outline.visible = !!object;
      const card = caption.current!;
      if (object && text) {
        bounds.setFromObject(object);
        bounds.getCenter(anchor); anchor.y = bounds.max.y + .12;
        anchor.project(camera);
        const width = container.clientWidth, height = container.clientHeight;
        card.style.opacity = anchor.z >= -1 && anchor.z <= 1 && Math.abs(anchor.x) < 1 && Math.abs(anchor.y) < 1 ? '1' : '0';
        card.style.left = `${Math.max(100, Math.min(width - 100, (anchor.x + 1) * width / 2))}px`;
        card.style.top = `${Math.max(55, Math.min(height - 48, (1 - anchor.y) * height / 2))}px`;
        if (card.children[0].textContent !== text[0]) card.children[0].textContent = text[0];
        if (card.children[1].textContent !== text[1]) card.children[1].textContent = text[1];
      } else card.style.opacity = '0';
      renderer.render(world, camera);
    });
    return () => {
      renderer.setAnimationLoop(null); resize.disconnect(); controls.dispose(); document.removeEventListener('visibilitychange', shown);
      density.removeEventListener('change', densityChanged);
      for (const [name, listener] of listeners) renderer.domElement.removeEventListener(name, listener as EventListener);
      geometries.forEach(g => g.dispose()); materials.forEach(m => m.dispose()); textures.forEach(t => t.dispose()); sun.shadow.dispose(); renderer.dispose(); renderer.forceContextLoss(); renderer.domElement.remove();
    };
  }, []);
  useEffect(() => {
    redraw.current(previousShadows.current !== shadowState);
    previousShadows.current = shadowState;
  }, [house.devices, shadowState]);
  useEffect(() => redraw.current(false), [selected, voice.state, voice.until]);
  return <div className="house-stage">
    <div ref={host} className="house-canvas" />
    <div ref={caption} className="house-caption"><strong /><span /></div>
    {error && <p role="alert" className="house-error">{error}</p>}
    <div className="house-camera"><span>Клик по устройству · вращение мышью · масштаб колесом</span><button onClick={() => resetCamera.current()}>Вернуть ракурс</button></div>
  </div>;
}
