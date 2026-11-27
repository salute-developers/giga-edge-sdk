import type { HouseEvent, Value } from './types';

async function request(path: string, body?: unknown) {
  const reply = await fetch(path, body === undefined ? undefined : { method: 'POST', body: JSON.stringify(body) });
  const data = await reply.json().catch(() => ({}));
  if (!reply.ok) throw new Error(data.error ?? `HTTP ${reply.status}`);
  return data;
}
export const editDevice = (id: string, param: string, value: Value) => request(`api/devices/${id}`, { param, value });
export const ask = (text: string) => request('api/chat', { text });
export const report = (comment: string) => request('api/report', { comment });
export const resetHouse = () => request('api/reset', {});
export const switchHouse = (id: string) => request('api/house', { id });
/** Subscribes to the events of the house, the first of which is the house itself; `online` reports the connection.
 * Returns the unsubscribe function. */
export function listen(on: (event: HouseEvent) => void, online: (up: boolean) => void) {
  const source = new EventSource('api/events');
  source.onmessage = message => on(JSON.parse(message.data));
  source.onopen = () => online(true);
  source.onerror = () => online(false);
  return () => source.close();
}
