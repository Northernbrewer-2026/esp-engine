import type TemperatureScale from "@/enums/TemperatureScale";

export interface IDistillPins {
  triac: number;
  pump: number;
  mixer: number;
  alarmWater: number;
  alarmLevel: number;
  alarmGas: number;
  npgLevel: number;
  pressureSensor: number;
}

export interface ISystemSettings {
  onewirePin: number;
  stirPin: number;
  buzzerPin: number;
  buzzerTime: number;
  invertOutputs: boolean;
  mqttUri: string;
  temperatureScale: TemperatureScale;
  distill: IDistillPins;
}
