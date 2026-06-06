export interface ITempSensor {
  id: string; // js doesn't support uint64_t, so we convert to string
  color: string;
  name: string;
  show: boolean;
  useForControl: boolean;
  connected: boolean;
  compensateAbsolute: number;
  compensateRelative: number;
  lastTemp: number;
  distillIndex: number; // 0=Cube, 1=Column, 2=TSA, 255=not used
}
