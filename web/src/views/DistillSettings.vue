<script lang="ts" setup>
import WebConn from "@/helpers/webConn";
import { inject, onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";
const { t } = useI18n({ useScope: "global" });

const webConn = inject<WebConn>("webConn");

interface IDistillSettings {
  powerTotalWatt: number;
  powerRectification: number;
  powerDistillSimple: number;
  powerHeadsDistill: number;
  powerNBK: number;
  tempEndRectAccelerate: number;
  tempEndHeadsColl: number;
  tempEndProductColl: number;
  tempEndRectification: number;
  tempSimpleDistill1: number;
  tempSimpleDistill2: number;
  tempSimpleDistill3: number;
  tempDeflegmatorStart: number;
  tempDeflegmator: number;
  tempDeflegmatorEnd: number;
  pwmHeadsCycleHalfperiods: number;
  pwmHeadsPercent: number;
  pwmProductCycleHalfperiods: number;
  pwmProductMinPercent: number;
  pwmProductStartPercent: number;
  pwmProductDeltaTenths: number;
  deflegmatorDeltaTenths: number;
  timeColumnStabiliseSec: number;
  timeAutoIncProductPWMSec: number;
  timeColumnRestabiliseSec: number;
  speedNBK: number;
}

const settings = ref<IDistillSettings>({
  powerTotalWatt: 3000,
  powerRectification: 1000,
  powerDistillSimple: 3000,
  powerHeadsDistill: 800,
  powerNBK: 2400,
  tempEndRectAccelerate: 830,
  tempEndHeadsColl: 854,
  tempEndProductColl: 965,
  tempEndRectification: 995,
  tempSimpleDistill1: 992,
  tempSimpleDistill2: 964,
  tempSimpleDistill3: 954,
  tempDeflegmatorStart: 700,
  tempDeflegmator: 820,
  tempDeflegmatorEnd: 985,
  pwmHeadsCycleHalfperiods: 2000,
  pwmHeadsPercent: 5,
  pwmProductCycleHalfperiods: 1000,
  pwmProductMinPercent: 20,
  pwmProductStartPercent: 40,
  pwmProductDeltaTenths: 3,
  deflegmatorDeltaTenths: 20,
  timeColumnStabiliseSec: 900,
  timeAutoIncProductPWMSec: 600,
  timeColumnRestabiliseSec: 1800,
  speedNBK: 0,
});

const alert     = ref<string>("");
const alertType = ref<"error" | "success" | "warning" | "info">("info");

// Temperature fields are stored as tenths of a degree; display as x.x °C
const toDisplay  = (tenths: number) => (tenths / 10).toFixed(1);
const fromDisplay = (v: string)     => Math.round(parseFloat(v) * 10);

// Reactive display models that convert to/from tenths automatically
const tempFields: { key: keyof IDistillSettings; label: string; tooltip: string }[] = [
  { key: "tempEndRectAccelerate", label: t("distillSettings.temp_rect_accel"),   tooltip: t("distillSettings.temp_rect_accel_tt") },
  { key: "tempEndHeadsColl",      label: t("distillSettings.temp_heads_end"),    tooltip: t("distillSettings.temp_heads_end_tt") },
  { key: "tempEndProductColl",    label: t("distillSettings.temp_product_end"),  tooltip: t("distillSettings.temp_product_end_tt") },
  { key: "tempEndRectification",  label: t("distillSettings.temp_rect_end"),     tooltip: t("distillSettings.temp_rect_end_tt") },
  { key: "tempSimpleDistill1",    label: t("distillSettings.temp_simple1"),      tooltip: t("distillSettings.temp_simple1_tt") },
  { key: "tempSimpleDistill2",    label: t("distillSettings.temp_simple2"),      tooltip: t("distillSettings.temp_simple2_tt") },
  { key: "tempSimpleDistill3",    label: t("distillSettings.temp_simple3"),      tooltip: t("distillSettings.temp_simple3_tt") },
  { key: "tempDeflegmatorStart",  label: t("distillSettings.temp_defl_start"),   tooltip: t("distillSettings.temp_defl_start_tt") },
  { key: "tempDeflegmator",       label: t("distillSettings.temp_defl"),         tooltip: t("distillSettings.temp_defl_tt") },
  { key: "tempDeflegmatorEnd",    label: t("distillSettings.temp_defl_end"),     tooltip: t("distillSettings.temp_defl_end_tt") },
];

const getData = async () => {
  const result = await webConn?.doPostRequest({ command: "GetDistillSettings", data: null });
  if (result?.success && result.data) settings.value = result.data;
};

const save = async () => {
  const result = await webConn?.doPostRequest({ command: "SaveDistillSettings", data: settings.value });
  alert.value     = result?.message ?? (result?.success ? t("general.saved") : t("general.error"));
  alertType.value = result?.success ? "success" : "error";
  window.scrollTo(0, 0);
};

onMounted(() => getData());
</script>

<template>
  <v-container class="pa-6" fluid>
    <v-alert :type="alertType" v-if="alert" closable @click:close="alert = ''" class="mb-4">{{ alert }}</v-alert>

    <!-- Power settings -->
    <v-card class="mb-4" variant="outlined">
      <v-card-title>{{ t("distillSettings.power_title") }}</v-card-title>
      <v-card-text>
        <v-row>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.powerTotalWatt"     :label="t('distillSettings.power_total')"  type="number" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.powerRectification" :label="t('distillSettings.power_rect')"   type="number" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.powerDistillSimple" :label="t('distillSettings.power_simple')" type="number" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.powerHeadsDistill"  :label="t('distillSettings.power_heads')"  type="number" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.powerNBK"           :label="t('distillSettings.power_nbk')"    type="number" />
          </v-col>
        </v-row>
      </v-card-text>
    </v-card>

    <!-- Temperature thresholds (displayed as °C, stored as tenths) -->
    <v-card class="mb-4" variant="outlined">
      <v-card-title>{{ t("distillSettings.temp_title") }}</v-card-title>
      <v-card-subtitle>{{ t("distillSettings.temp_subtitle") }}</v-card-subtitle>
      <v-card-text>
        <v-row>
          <v-col cols="12" md="3" v-for="tf in tempFields" :key="tf.key">
            <v-text-field
              :model-value="toDisplay(settings[tf.key] as number)"
              @update:model-value="(v: string) => { (settings as any)[tf.key] = fromDisplay(v); }"
              :label="tf.label"
              type="number"
              step="0.1"
              suffix="°C"
            >
              <template v-slot:append>
                <v-tooltip :text="tf.tooltip">
                  <template v-slot:activator="{ props }">
                    <v-icon size="small" v-bind="props">mdi-help-circle-outline</v-icon>
                  </template>
                </v-tooltip>
              </template>
            </v-text-field>
          </v-col>
        </v-row>
      </v-card-text>
    </v-card>

    <!-- PWM / timing -->
    <v-card class="mb-4" variant="outlined">
      <v-card-title>{{ t("distillSettings.pwm_title") }}</v-card-title>
      <v-card-text>
        <v-row>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.pwmHeadsCycleHalfperiods"   :label="t('distillSettings.pwm_heads_cycle')"   type="number" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.pwmHeadsPercent"            :label="t('distillSettings.pwm_heads_pct')"     type="number" suffix="%" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.pwmProductCycleHalfperiods" :label="t('distillSettings.pwm_product_cycle')" type="number" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.pwmProductMinPercent"       :label="t('distillSettings.pwm_product_min')"   type="number" suffix="%" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.pwmProductStartPercent"     :label="t('distillSettings.pwm_product_start')" type="number" suffix="%" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.pwmProductDeltaTenths"      :label="t('distillSettings.pwm_product_delta')" type="number" suffix="×0.1°C" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.deflegmatorDeltaTenths"     :label="t('distillSettings.defl_delta')"        type="number" suffix="×0.1°C" />
          </v-col>
        </v-row>
      </v-card-text>
    </v-card>

    <!-- Timing -->
    <v-card class="mb-4" variant="outlined">
      <v-card-title>{{ t("distillSettings.timing_title") }}</v-card-title>
      <v-card-text>
        <v-row>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.timeColumnStabiliseSec"    :label="t('distillSettings.time_stabilise')"  type="number" suffix="s"
              :hint="t('distillSettings.time_stabilise_hint')" persistent-hint />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.timeAutoIncProductPWMSec"  :label="t('distillSettings.time_autoinc')"    type="number" suffix="s" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.timeColumnRestabiliseSec"  :label="t('distillSettings.time_restab')"     type="number" suffix="s" />
          </v-col>
          <v-col cols="12" md="3">
            <v-text-field v-model.number="settings.speedNBK"                  :label="t('distillSettings.speed_nbk')"       type="number" suffix="0-254" />
          </v-col>
        </v-row>
      </v-card-text>
    </v-card>

    <v-row>
      <v-col cols="auto">
        <v-btn color="success" size="large" @click="save">{{ t("general.save") }}</v-btn>
      </v-col>
    </v-row>
  </v-container>
</template>
