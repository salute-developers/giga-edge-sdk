import type { Device } from '../types';

export function value(d: Device | undefined, name: string) { return d?.parameters.find(p => p.name === name)?.value; }
const number = (d: Device | undefined, name: string, fallback = 0) => Number(value(d, name) ?? fallback);
/** How open a cover is, 0..1; a valve is open or closed. */
export function opening(d: Device | undefined) {
  return value(d, 'open_set') != null ? Number(value(d, 'open_set') === 'open') : number(d, 'open_percentage') / 100;
}
/** The colour of a white light by its colour temperature in percent (0 is 2700 K, 100 is 6500 K): a visual
 * approximation. */
export function whiteColor(percent: number) {
  const t = Math.max(0, Math.min(100, percent)) / 100;
  const rgb = t < .5 ? [255, 180 + (244 - 180) * t * 2, 105 + (229 - 105) * t * 2] : [255 + (195 - 255) * (t - .5) * 2, 244 + (221 - 244) * (t - .5) * 2, 229 + (255 - 229) * (t - .5) * 2];
  return `rgb(${rgb.map(Math.round).join(',')})`;
}
/** A colour written as "R;G;B", each 0..255, as numbers; undefined for anything else. */
export function rgb(text: unknown) {
  const channels = /^(\d{1,3});(\d{1,3});(\d{1,3})$/.exec(String(text ?? ''))?.slice(1).map(Number);
  return channels?.every(c => c <= 255) ? channels : undefined;
}
/** The colour of a light: the hue of its own colour at full strength, when it has one (how bright it shines is
 * `light_brightness`), otherwise white by its colour temperature. */
function lightColor(d: Device | undefined) {
  const own = rgb(value(d, 'light_colour')), top = own ? Math.max(...own) : 0;
  return own && top ? `rgb(${own.map(c => Math.round(c * 255 / top)).join(',')})` : whiteColor(number(d, 'light_colour_temp', 50));
}
export function lampState(d: Device | undefined) {
  return {
    on: value(d, 'on_off') === true,
    brightness: Math.max(0, Math.min(1, number(d, 'light_brightness', 65) / 100)),
    color: lightColor(d),
  };
}
/** The state of a device in a few words, for the list. */
export function summary(d: Device): string {
  const get = (p: string) => value(d, p);
  const power = get('on_off') ? 'вкл' : 'выкл';
  if (d.type === 'light') return get('on_off') ? `вкл · ${get('light_brightness')}%` : 'выкл';
  if (d.type === 'sensor_door') return get('doorcontact_state') ? 'открыта' : 'закрыта';
  if (d.type === 'sensor_water_leak') return get('water_leak_state') ? 'протечка' : 'сухо';
  if (d.type === 'sensor_smoke') return get('smoke_state') ? 'дым' : 'чисто';
  if (d.type === 'sensor_gas') return get('gas_leak_state') ? 'утечка газа' : 'норма';
  if (d.type === 'sensor_pir') return get('pir') ? 'движение' : 'тихо';
  if (d.type === 'sensor_temp') return get('temperature') != null ? `${get('temperature')} °C` : `${get('humidity')}%`;
  if (d.type === 'valve') return opening(d) ? 'открыт' : 'закрыт';
  if (['curtain', 'window_blind', 'gate'].includes(d.type)) return opening(d) === 0 ? 'закрыты' : opening(d) === 1 ? 'открыты' : `открыты на ${get('open_percentage')}%`;
  if (d.type === 'tv') return get('on_off') ? `вкл · громкость ${get('volume_int')}${get('mute') ? ' · без звука' : ''}` : 'выкл';
  if (d.type === 'vacuum_cleaner') return `${get('on_off') ? 'убирает' : 'на базе'}${get('battery_percentage') != null ? ` · ${get('battery_percentage')}%` : ''}`;
  if (d.type === 'hvac_humidifier') return `${power} · цель ${get('hvac_humidity_set')}%`;
  return get('hvac_temp_set') != null ? `${power} · ${get('temperature')} → ${get('hvac_temp_set')} °C` : power;
}
