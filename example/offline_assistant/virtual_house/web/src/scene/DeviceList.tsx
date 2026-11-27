import { useEffect, useRef, useState } from 'react';
import type { Device, Param, Room, Value } from '../types';
import { lampState, rgb, summary, value } from './devices';

function DeviceIcon({ kind }: { kind: string }) {
  const paths = kind === 'light' ? ['M9 18h6M10 21h4M8 13a6 6 0 1 1 8 0c-1 1-1 2-1 3H9c0-1 0-2-1-3Z']
    : kind === 'led_strip' ? ['M4 5h16v5H4zM4 14h16v5H4zM8 5v5m4-5v5m4-5v5M8 14v5m4-5v5m4-5v5']
    : kind === 'tv' ? ['M3 5h18v13H3zM8 22h8m-4-4v4']
    : kind === 'sensor_door' || kind === 'gate' ? ['M5 21V3h14v18M9 21V6l7-2v17M12 12h1']
    : kind === 'sensor_water_leak' || kind === 'valve' ? ['M12 3c-2 4-7 8-7 12a7 7 0 0 0 14 0c0-4-5-8-7-12ZM9 16c0 2 1 3 3 3']
    : kind === 'curtain' || kind === 'window_blind' ? ['M3 4h18M5 4v16l5-3V4m9 0v16l-5-3V4M10 11h4']
    : kind === 'sensor_temp' || kind.startsWith('hvac') ? ['M9 14V5a3 3 0 0 1 6 0v9a5 5 0 1 1-6 0ZM12 9v9M19 7h2m-2 4h2']
    : kind === 'vacuum_cleaner' ? ['M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18ZM8 8h8M12 13a2 2 0 1 0 0 4 2 2 0 0 0 0-4']
    : kind === 'kettle' ? ['M5 9h11v8a4 4 0 0 1-4 4H9a4 4 0 0 1-4-4ZM16 10h2a3 3 0 0 1 0 6h-2M8 5h5m-2-3v3']
    : kind === 'socket' || kind === 'relay' ? ['M5 3h14v18H5zM9 9v3m6-3v3M10 16h4']
    : ['M12 10a2 2 0 1 0 0 4 2 2 0 0 0 0-4M7 7a7 7 0 0 0 0 10m10-10a7 7 0 0 1 0 10M4 4a11 11 0 0 0 0 16M20 4a11 11 0 0 1 0 16'];
  return <svg width="19" height="19" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">{paths.map(p => <path key={p} d={p} />)}</svg>;
}
const labels: Record<string, string> = {
  on_off: 'Питание', light_brightness: 'Яркость', light_colour_temp: 'Белый: тёплый → холодный', light_colour: 'Цвет',
  temperature: 'Температура', humidity: 'Влажность', hvac_temp_set: 'Целевая температура', hvac_humidity_set: 'Целевая влажность',
  volume_int: 'Громкость', mute: 'Без звука', open_percentage: 'Открыто', open_set: 'Положение',
  doorcontact_state: 'Дверь', water_leak_state: 'Протечка', gas_leak_state: 'Утечка газа', smoke_state: 'Задымление', pir: 'Движение', battery_percentage: 'Заряд',
};
const choices: Record<string, string> = { open: 'Открыт', close: 'Закрыт' };
const booleanLabel = (name: string, on: boolean) => name === 'doorcontact_state' ? (on ? 'Открыта' : 'Закрыта')
  : name === 'water_leak_state' ? (on ? 'Протечка' : 'Сухо')
  : name === 'on_off' ? (on ? 'Включено' : 'Выключено') : on ? 'Да' : 'Нет';

/** The devices by room and the manual controls of the selected one. */
export function DeviceList({ rooms, devices, selected, changed, onSelect, onEdit }: {
  rooms: Room[]; devices: Device[]; selected: string; changed: Set<string>;
  onSelect: (id: string) => void; onEdit: (id: string, param: string, next: Value) => void;
}) {
  const selectedDevice = devices.find(d => d.device_id === selected);
  const [lastRoom, setLastRoom] = useState(rooms[0].name);
  useEffect(() => { if (selectedDevice) setLastRoom(selectedDevice.room); }, [selectedDevice?.room]);
  const room = selectedDevice?.room ?? lastRoom;
  return <section className="devices" aria-label="Устройства дома">
    <nav className="devices-rooms" aria-label="Комнаты">{rooms.map(r => <button key={r.name} aria-pressed={room === r.name} onClick={() => { setLastRoom(r.name); onSelect(''); }}>{r.name}</button>)}</nav>
    <div className="devices-workspace">
      <div className="devices-list" aria-label={`Устройства: ${room}`}>
        {devices.filter(d => d.room === room).map(d => <button key={d.device_id} className={`device-row ${selected === d.device_id ? 'selected' : ''}`} aria-pressed={selected === d.device_id} onClick={() => onSelect(selected === d.device_id ? '' : d.device_id)}>
          <span className={`device-icon ${value(d, 'on_off') === true ? 'on' : ''}`}><DeviceIcon kind={d.kind} /></span>
          <span className="device-name"><strong>{d.name}</strong><code>{d.device_id}</code></span>
          <span className="device-status">{d.type === 'light' && <i className="device-color" style={{ background: lampState(d).color }} />}{summary(d)}{changed.has(d.device_id) && <i className="device-changed" title="Изменено ассистентом" />}</span>
        </button>)}
      </div>
      <div className="devices-inspector" aria-label="Управление устройством">
        {selectedDevice ? <>
          <div className="inspector-title"><strong>{selectedDevice.name}</strong><span>{selectedDevice.room}</span></div>
          <div className="parameters">{selectedDevice.parameters.map(p => <ParameterControl key={`${selectedDevice.device_id}/${p.name}`} parameter={p} onCommit={next => onEdit(selectedDevice.device_id, p.name, next)} />)}</div>
        </> : <p className="inspector-hint">Выберите устройство в списке или на сцене: его можно переключить вручную, а показания датчиков задать самому.</p>}
      </div>
    </div>
  </section>;
}

function ParameterControl({ parameter: p, onCommit }: { parameter: Param; onCommit: (next: Value) => void }) {
  const current = String(p.value ?? '');
  const [draft, setDraft] = useState(current);
  useEffect(() => { setDraft(current); }, [current]);
  const label = labels[p.name] ?? p.name;
  const slider = p.data_type === 'INTEGER';
  const commit = (raw: string) => { if (raw.trim() && raw !== current && Number.isFinite(Number(raw))) onCommit(Number(raw)); else setDraft(current); };
  if (p.name === 'light_colour') return <ColourControl parameter={p} onCommit={onCommit} />;
  return <div className="parameter">
    <span className="parameter-label" title={`${p.name}${p.read_only ? ' · показание: ассистент его только читает' : ''}`}>{label}{p.unit && !slider ? `, ${p.unit}` : ''}</span>
    {p.data_type === 'BOOL' ? <button type="button" className="switch" role="switch" aria-label={label} aria-checked={p.value === true} onClick={() => onCommit(p.value !== true)}><span>{booleanLabel(p.name, p.value === true)}</span><i /></button>
      : p.data_type === 'ENUM' ? <select aria-label={label} value={current} onChange={e => onCommit(e.target.value)}>{(p.values ?? []).map(v => <option key={v} value={v}>{choices[v] ?? v}</option>)}</select>
      : slider ? <div className="slider"><input type="range" aria-label={label} aria-valuetext={`${draft}${p.unit ?? ''}`} min={p.range?.min} max={p.range?.max} step={p.range?.step} value={draft} onChange={e => { setDraft(e.target.value); onCommit(Number(e.target.value)); }} /><output>{draft}{p.unit}</output></div>
      : <input aria-label={label} type="number" min={p.range?.min} max={p.range?.max} step={p.range?.step} value={draft} onChange={e => setDraft(e.target.value)} onBlur={() => commit(draft)} onKeyDown={e => { if (e.key === 'Enter') e.currentTarget.blur(); }} />}
  </div>;
}

/** The colour of a light, "R;G;B"; while it has none, the picker shows white and a pick gives it one. */
function ColourControl({ parameter: p, onCommit }: { parameter: Param; onCommit: (next: Value) => void }) {
  const current = `#${(rgb(p.value) ?? [255, 255, 255]).map(c => c.toString(16).padStart(2, '0')).join('')}`;
  const [draft, setDraft] = useState(current);
  useEffect(() => { setDraft(current); }, [current]);
  // The picker reports every move of the pointer as an input and the end of the choice as a change: only that is sent.
  const input = useRef<HTMLInputElement>(null), commit = useRef(onCommit);
  commit.current = onCommit;
  useEffect(() => {
    const element = input.current!;
    const send = () => { if (element.value !== current) commit.current([1, 3, 5].map(i => parseInt(element.value.slice(i, i + 2), 16)).join(';')); };
    element.addEventListener('change', send);
    return () => element.removeEventListener('change', send);
  }, [current]);
  return <div className="parameter">
    <span className="parameter-label">{labels[p.name]}{p.value == null ? ' · белый' : ''}</span>
    <input ref={input} type="color" aria-label={labels[p.name]} value={draft} onChange={e => setDraft(e.target.value)} />
  </div>;
}
