import { useEffect, useRef, useState } from 'react';
import type { Assistant, HouseEvent } from './types';

/** What the house reports about the dialogue, and notes of the page itself. */
export type Message = Exclude<HouseEvent, { type: 'house' | 'values' | 'assistant' }> | { type: 'note'; text: string };
const examples = ['Включи свет в спальне', 'Закрой шторы в спальне наполовину', 'Какая температура в гостиной?', 'Сделай телевизор погромче', 'Выключи весь свет'];

type Call = Extract<Message, { type: 'call' }>;
const failed = (call: Call) => !!(call.error || call.is_error);
/** A call of the assistant to the house: the Home Assistant intent, its target and its values. */
function callParts(call: Call) {
  const { name, area, domain, ...rest } = call.arguments;
  const values = Object.entries(rest).map(([key, value]) => `${key}=${value}`).join(' ');
  const [tool, target] = call.title ? ['script', call.title] : [call.tool.split('__').pop() ?? call.tool, String(name ?? domain ?? '')];
  return { tool, target: `${target}${area ? ` [${area}]` : ''}`, values: values ? ` · ${values}` : '' };
}
function callText(call: Call) {
  const { tool, target, values } = callParts(call);
  return `${tool}${target ? ` → ${target}` : ''}${values}${failed(call) ? ` — ошибка${call.error ? `: ${call.error}` : ''}` : ''}`;
}
const callHint = (call: Call) => call.error ?? (failed(call) ? 'Ошибка' : call.background ? 'Чтение дома ассистентом' : call.changed.length ? 'Показать устройство' : 'Ничего не изменил');
const keys = new WeakMap<object, number>();
let lastKey = 0;
/** A key that stays with a message while the older ones are dropped. */
const keyOf = (m: object) => keys.get(m) ?? (keys.set(m, ++lastKey), lastKey);
const calls = (n: number) => `${n} ${n % 10 === 1 && n % 100 !== 11 ? 'вызов' : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14) ? 'вызова' : 'вызовов'}`;
/** The messages with each run of calls in a row as one item. */
function group(messages: Message[]) {
  const items: (Exclude<Message, Call> | { type: 'calls'; calls: Call[] })[] = [];
  for (const m of messages) {
    const last = items[items.length - 1];
    if (m.type === 'call' && last?.type === 'calls') last.calls.push(m);
    else items.push(m.type === 'call' ? { type: 'calls', calls: [m] } : m);
  }
  return items;
}

/** The dialogue with the assistant and what it did to the house. */
export function Chat({ messages, assistant, scenarios, pending, onSend, onReport, onSelect }: {
  messages: Message[]; assistant: Assistant; scenarios: string[]; pending: boolean; onSend: (text: string) => void; onReport: (comment: string) => void; onSelect: (id: string) => void;
}) {
  const [draft, setDraft] = useState('');
  const [problem, setProblem] = useState('');
  const end = useRef<HTMLDivElement>(null);
  useEffect(() => { end.current?.scrollIntoView({ block: 'end' }); }, [messages.length, pending]);
  const send = (text: string) => { if (text.trim() && !pending) { onSend(text); setDraft(''); } };
  return <section className="chat" aria-label="Диалог с ассистентом">
    <div className="chat-log" aria-live="polite">
      {messages.length === 0 && <p className="chat-note">{
        assistant.mode === null ? 'Дом запущен без ассистента: устройствами можно управлять вручную или из любого MCP-клиента.'
          : !assistant.ready ? 'Ассистент загружает модели…'
          : assistant.mode === 'voice' ? 'Скажите «Гига Ёж» и команду. Всё, что ассистент делает с домом, появится здесь.'
          : 'Напишите команду — ассистент выполнит её в доме.'}</p>}
      {group(messages).map(m => m.type === 'request' ? <p key={keyOf(m)} className="chat-bubble user">{m.text}</p>
        : m.type === 'answer' ? <p key={keyOf(m)} className="chat-bubble">{m.text}</p>
        : m.type === 'calls' ? <Calls key={keyOf(m.calls[0])} calls={m.calls} onSelect={onSelect} />
        : m.type === 'report' ? <p key={keyOf(m)} className="chat-note">{m.path ? `Отчёт сохранён: ${m.path}` : `Отчёт не сохранён: ${m.error}`}</p>
        : <p key={keyOf(m)} className="chat-note">{m.type === 'listening' ? 'слушаю…' : m.text}</p>)}
      {pending && <p className="chat-note">думает…</p>}
      <div ref={end} />
    </div>
    {assistant.mode === 'text' && <>
      <div className="chat-examples">{[...examples, ...scenarios.map(name => `Включи сценарий ${name.toLowerCase()}`)].map(text => <button key={text} disabled={pending || !assistant.ready} onClick={() => send(text)}>{text}</button>)}</div>
      <form className="chat-composer" onSubmit={e => { e.preventDefault(); send(draft); }}>
        <input aria-label="Запрос ассистенту" placeholder="Включи свет в спальне" value={draft} disabled={!assistant.ready} onChange={e => setDraft(e.target.value)} />
        <button type="submit" disabled={pending || !assistant.ready || !draft.trim()}>Отправить</button>
      </form>
    </>}
    {assistant.mode === 'voice' && <div className="chat-composer"><button disabled={!assistant.ready} onClick={() => onSend('')}>Слушать без «Гига Ёж»</button></div>}
    {assistant.mode && <form className="chat-composer chat-report" onSubmit={e => { e.preventDefault(); onReport(problem); setProblem(''); }}>
      <input aria-label="Что пошло не так" placeholder="Что пошло не так" value={problem} disabled={!assistant.ready} onChange={e => setProblem(e.target.value)} />
      <button type="submit" disabled={!assistant.ready}>Сообщить о проблеме</button>
    </form>}
  </section>;
}

/** One call; a click on a call that changed a device selects it. */
function CallLine({ call, onSelect }: { call: Call; onSelect: (id: string) => void }) {
  const kind = failed(call) ? 'failed' : call.background ? 'background' : call.changed.length ? '' : 'same';
  return <button className={`chat-call ${kind}`} disabled={!call.changed.length} onClick={() => onSelect(call.changed[0])} title={callHint(call)}>{callText(call)}</button>;
}
/** A few calls in a row one by one; more of them folded under a line that counts them by tool, open when one failed. */
function Calls({ calls: run, onSelect }: { calls: Call[]; onSelect: (id: string) => void }) {
  const lines = run.map((call, i) => <CallLine key={i} call={call} onSelect={onSelect} />);
  if (run.length <= 3) return <>{lines}</>;
  const tools = new Map<string, number>();
  for (const call of run) tools.set(callParts(call).tool, (tools.get(callParts(call).tool) ?? 0) + 1);
  const errors = run.filter(failed).length, changed = new Set(run.flatMap(call => call.changed)).size;
  return <details className="chat-calls" open={errors > 0}>
    <summary>{calls(run.length)}: {[...tools].map(([tool, n]) => `${tool} ×${n}`).join(', ')}{changed ? ` · изменено устройств: ${changed}` : ''}{errors ? <b> · ошибок: {errors}</b> : null}</summary>
    <div>{lines}</div>
  </details>;
}
