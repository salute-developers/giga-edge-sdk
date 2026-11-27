import { useEffect, useRef, useState } from 'react';
import { ask, editDevice, listen, report, resetHouse, switchHouse } from './api';
import { Chat, type Message } from './Chat';
import { DeviceList } from './scene/DeviceList';
import { HouseView } from './scene/HouseView';
import type { Voice } from './scene/speaker';
import type { Assistant, House, HouseEvent, Value } from './types';

// offline_assistant_demo gives up on a turn after 15 s without printing an answer.
const ANSWER_TIMEOUT_MS = 20000;
// How long the speaker shows that it listens, if nothing is heard, and that it answers, by the length of the answer.
const LISTEN_MS = 10000, ANSWER_MS_PER_CHAR = 70;
// The assistant reads the whole house by itself from time to time: such a read says nothing about the dialogue.
const background = (m: Message) => m.type === 'call' && !!m.background;
/** An event of the dialogue in the shape the page draws, whatever the house sent. */
function tidy(event: Message): Message {
  if (event.type === 'call') {
    const { tool, arguments: args, changed } = event as { tool?: unknown; arguments?: unknown; changed?: unknown };
    return { ...event, tool: String(tool ?? ''), arguments: args && typeof args === 'object' && !Array.isArray(args) ? args as Record<string, unknown> : {},
      changed: Array.isArray(changed) ? changed.map(String) : [] };
  }
  return 'text' in event ? { ...event, text: String(event.text ?? '') } : event;
}

export function App() {
  const [house, setHouse] = useState<House>();
  const [assistant, setAssistant] = useState<Assistant>({ mode: null, ready: false });
  const [messages, setMessages] = useState<Message[]>([]);
  const [selected, setSelected] = useState('');
  const [changed, setChanged] = useState(new Set<string>());
  const [pending, setPending] = useState(false);
  const [online, setOnline] = useState(true);
  const [error, setError] = useState('');
  const fail = (e: unknown) => setError(e instanceof Error ? e.message : String(e));
  const shown = useRef('');
  const since = useRef(0);

  useEffect(() => listen((event: HouseEvent) => {
    if (event.type === 'house') {
      // The house also comes again after a reconnect; only another house starts the page afresh.
      if (shown.current !== event.id) { shown.current = event.id; setMessages([]); setSelected(''); setChanged(new Set()); setPending(false); }
      setHouse(event); setAssistant(event.assistant);
    } else if (event.type === 'values') {
      setHouse(h => h && { ...h, devices: h.devices.map(d => event.values[d.device_id] ? { ...d, parameters: d.parameters.map(p => ({ ...p, value: p.name in event.values[d.device_id] ? event.values[d.device_id][p.name] : p.value })) } : d) });
    } else if (event.type === 'assistant') {
      setAssistant({ mode: event.mode, ready: event.ready });
    } else {
      const message = tidy(event);
      setMessages(all => [...all.slice(-300), message]);
      if (message.type === 'listening' || message.type === 'request' || message.type === 'answer') since.current = performance.now();
      if (message.type === 'request') { setPending(true); setChanged(new Set()); }
      if (message.type === 'answer') setPending(false);
      if (message.type === 'call') setChanged(ids => new Set([...ids, ...message.changed]));
    }
  }, setOnline), []);
  let last: Message | undefined;
  for (let i = messages.length - 1; i >= 0 && !last; i--) if (!background(messages[i])) last = messages[i];
  useEffect(() => {
    if (!pending) return;
    const timer = setTimeout(() => { setPending(false); setMessages(all => [...all, { type: 'note', text: 'Ассистент не ответил: подробности в его журнале.' }]); }, ANSWER_TIMEOUT_MS);
    return () => clearTimeout(timer);
  }, [pending, last]);

  if (!house) return <main className="page-message" role={online ? 'status' : 'alert'}>{online ? 'Загружаем дом…' : 'Нет связи с домом: запущен ли house.py?'}</main>;
  const run = (action: Promise<unknown>) => { setError(''); action.catch(fail); };
  const voice: Voice = !assistant.mode || !assistant.ready ? { state: 'off', until: 0 }
    : pending ? { state: 'thinking', until: Infinity }
    : last?.type === 'listening' ? { state: 'listening', until: since.current + LISTEN_MS }
    : last?.type === 'answer' ? { state: 'answering', until: since.current + Math.min(10000, Math.max(2000, last.text.length * ANSWER_MS_PER_CHAR)) }
    : { state: 'idle', until: 0 };
  const reset = () => { setMessages([]); setChanged(new Set()); setSelected(''); run(resetHouse()); };
  return <main className="page">
    <header className="toolbar">
      <div className="toolbar-title">
        <select aria-label="Дом" value={house.id} disabled={pending} onChange={e => run(switchHouse(e.target.value))}>
          {house.houses.map(h => <option key={h.id} value={h.id}>{h.name} · {h.devices} устройств</option>)}
        </select>
        <span>{house.rooms.length} комнат{assistant.mode && !assistant.ready ? ' · ассистент запускается…' : ''}</span>
      </div>
      {!online && <span className="toolbar-alert" role="alert">Нет связи с домом</span>}
      {error && <span className="toolbar-alert" role="alert">{error}</span>}
      <button onClick={reset}>Начать заново</button>
    </header>
    <div className="layout">
      <section className="world" aria-label="Дом">
        <HouseView key={`view-${house.id}`} house={house} voice={voice} selected={selected} onSelect={setSelected} />
        <DeviceList key={`devices-${house.id}`} rooms={house.rooms} devices={house.devices} selected={selected} changed={changed} onSelect={setSelected} onEdit={(id: string, param: string, value: Value) => run(editDevice(id, param, value))} />
      </section>
      <Chat messages={messages} assistant={assistant} scenarios={house.scenarios.map(s => s.name)} pending={pending} onSelect={setSelected} onSend={text => run(ask(text))} onReport={comment => run(report(comment))} />
    </div>
  </main>;
}
