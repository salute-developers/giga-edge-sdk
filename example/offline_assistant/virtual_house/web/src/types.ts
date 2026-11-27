export type Value = string | number | boolean | null;
export type Param = {
  name: string; value: Value; data_type: 'BOOL' | 'INTEGER' | 'FLOAT' | 'ENUM';
  range?: { min: number; max: number; step: number }; unit?: string; values?: string[];
  /** A reading: the assistant cannot set it, the page can. */
  read_only?: boolean;
};
/** A device of a house file plus its current parameters. `type` is the category of the model, `kind` the 3D model. */
export type Device = {
  device_id: string; type: string; kind: string; name: string; room: string; parameters: Param[];
  pos: [number, number, number]; facing?: '+x' | '-x' | '+z' | '-z';
  length?: number; size?: [number, number]; mount?: 'ceiling'; base?: number; slide?: number;
};
export type Room = { name: string; x: number; z: number; w: number; d: number; floor: string };
/** `area` and `pos`: the room of the assistant and where its speaker stands, when the house file tells. */
export type Assistant = { mode: 'text' | 'voice' | null; ready: boolean; area?: string; pos?: [number, number, number] };
/** The current house and the list of houses to switch to; `id` is the file name in houses/. */
export type House = {
  id: string; name: string; rooms: Room[]; devices: Device[]; assistant: Assistant;
  /** The scenes and the scripts of the house: the assistant runs one by «включи сценарий <name>». */
  scenarios: { name: string; domain: 'scene' | 'script' }[];
  houses: { id: string; name: string; devices: number }[];
};
export type Values = Record<string, Record<string, Value>>;
/** What house.py sends over /api/events. */
export type HouseEvent =
  | ({ type: 'house' } & House)
  | { type: 'values'; values: Values }
  | ({ type: 'assistant' } & Assistant)
  // `title`: the name of a script, which is called by a tool of its own, without arguments; `background`: a read of the
  // whole house the assistant makes by itself, not for a request.
  | { type: 'call'; tool: string; title?: string; arguments: Record<string, unknown>; changed: string[]; error?: string; is_error?: boolean; background?: boolean }
  | { type: 'request' | 'answer'; text: string }
  // A problem report of the assistant: the file it is saved to, or why it is not.
  | { type: 'report'; path?: string; error?: string }
  | { type: 'listening' };
