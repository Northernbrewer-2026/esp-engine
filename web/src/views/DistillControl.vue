<script lang="ts" setup>
import WebConn from "@/helpers/webConn";
import { inject, onBeforeUnmount, onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";
const { t } = useI18n({ useScope: "global" });

const webConn = inject<WebConn>("webConn");

// ── State ─────────────────────────────────────────────────────────────────
const running     = ref<boolean>(false);
const status      = ref<string>("Idle");
const mode        = ref<number>(0);
const elapsedSec  = ref<number>(0);
const heaterPct   = ref<number>(0);
const productPWM  = ref<number>(0);
const intervalId  = ref<any>();

interface ValveState { id: number; name: string; role: number; isOpen: boolean }
const valves = ref<ValveState[]>([]);

const selectedMode = ref<number>(1);

const alert     = ref<string>("");
const alertType = ref<"error" | "success" | "warning" | "info">("info");

// ── Mode options ──────────────────────────────────────────────────────────
const distillModes = [
  { title: t("distill.mode_simple1"),        value: 1 },
  { title: t("distill.mode_simple2"),        value: 2 },
  { title: t("distill.mode_simple3"),        value: 3 },
  { title: t("distill.mode_heads"),          value: 4 },
  { title: t("distill.mode_rectification"),  value: 5 },
  { title: t("distill.mode_defl"),           value: 6 },
  { title: t("distill.mode_ndrf"),           value: 7 },
  { title: t("distill.mode_nbk"),            value: 8 },
  { title: t("distill.mode_test_valves"),    value: 9 },
];

// ── Helpers ───────────────────────────────────────────────────────────────
const formatElapsed = (sec: number): string => {
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  return `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
};

const modeLabel = (m: number): string => distillModes.find((d) => d.value === m)?.title ?? "Unknown";

// ── API calls ─────────────────────────────────────────────────────────────
const getData = async () => {
  const result = await webConn?.doPostRequest({ command: "DistillData", data: null });
  if (!result?.success) return;

  running.value    = result.data.running;
  status.value     = result.data.status;
  mode.value       = result.data.mode;
  elapsedSec.value = result.data.elapsedSec;
  heaterPct.value  = result.data.heaterPct;
  productPWM.value = result.data.productPWM;
  valves.value     = result.data.valves ?? [];
};

const start = async () => {
  const result = await webConn?.doPostRequest({
    command: "DistillStart",
    data: { mode: selectedMode.value },
  });
  if (result?.message) {
    alert.value     = result.message;
    alertType.value = result.success ? "success" : "error";
  }
  await getData();
};

const stop = async () => {
  const result = await webConn?.doPostRequest({ command: "DistillStop", data: null });
  if (result?.message) {
    alert.value     = result.message;
    alertType.value = result.success ? "success" : "warning";
  }
  await getData();
};

const startPump = async () => {
  const result = await webConn?.doPostRequest({ command: "DistillStartPump", data: null });
  if (result?.message) {
    alert.value     = result.message;
    alertType.value = result.success ? "success" : "error";
  }
};

const manualValve = async (role: number, open: boolean) => {
  const cmd = open ? "DistillOpenValve" : "DistillCloseValve";
  await webConn?.doPostRequest({ command: cmd, data: { role } });
  await getData();
};

// ── Lifecycle ─────────────────────────────────────────────────────────────
onMounted(() => {
  getData();
  intervalId.value = setInterval(getData, 2000);
});

onBeforeUnmount(() => {
  clearInterval(intervalId.value);
});
</script>

<template>
  <v-container class="pa-4" fluid>
    <v-alert :type="alertType" v-if="alert" closable @click:close="alert = ''" class="mb-4">
      {{ alert }}
    </v-alert>

    <!-- Status banner -->
    <v-row>
      <v-col cols="12">
        <v-card :color="running ? 'primary' : 'surface-variant'" variant="tonal" class="mb-2">
          <v-card-text>
            <div class="d-flex align-center flex-wrap ga-4">
              <div>
                <div class="text-overline">{{ t("distill.mode_label") }}</div>
                <div class="text-h6">{{ running ? modeLabel(mode) : t("distill.idle") }}</div>
              </div>
              <v-divider vertical />
              <div>
                <div class="text-overline">{{ t("distill.status") }}</div>
                <div class="text-h6">{{ status }}</div>
              </div>
              <v-divider vertical />
              <div>
                <div class="text-overline">{{ t("distill.elapsed") }}</div>
                <div class="text-h6 font-weight-bold">{{ formatElapsed(elapsedSec) }}</div>
              </div>
              <v-divider vertical />
              <div>
                <div class="text-overline">{{ t("distill.heater_pct") }}</div>
                <div class="text-h6">{{ heaterPct }}%</div>
              </div>
              <v-spacer />
              <v-chip v-if="productPWM > 0" color="green" label>
                {{ t("distill.product_pwm") }}: {{ productPWM }}%
              </v-chip>
            </div>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Controls -->
    <v-row align="center">
      <v-col cols="12" md="4">
        <v-select
          v-model="selectedMode"
          :items="distillModes"
          :label="t('distill.select_mode')"
          :disabled="running"
          variant="outlined"
          density="comfortable"
        />
      </v-col>
      <v-col cols="auto">
        <v-btn
          color="success"
          :disabled="running"
          size="large"
          prepend-icon="mdi-play"
          @click="start"
        >
          {{ t("general.start") }}
        </v-btn>
      </v-col>
      <v-col cols="auto">
        <v-btn
          color="error"
          :disabled="!running"
          size="large"
          prepend-icon="mdi-stop"
          @click="stop"
        >
          {{ t("general.stop") }}
        </v-btn>
      </v-col>
      <!-- NBK-specific pump start button -->
      <v-col cols="auto" v-if="running && mode === 8">
        <v-btn color="warning" size="large" @click="startPump">
          {{ t("distill.start_pump") }}
        </v-btn>
      </v-col>
    </v-row>

    <!-- Valve states -->
    <v-row class="mt-2">
      <v-col cols="12">
        <div class="text-subtitle-1 mb-2">{{ t("distill.valves") }}</div>
        <div class="d-flex flex-wrap ga-3">
          <v-card
            v-for="valve in valves"
            :key="valve.id"
            :color="valve.isOpen ? 'green' : 'grey'"
            variant="tonal"
            min-width="140"
          >
            <v-card-text class="pa-3 text-center">
              <div class="text-caption text-medium-emphasis">{{ valve.name }}</div>
              <div class="text-body-1 font-weight-bold">
                {{ valve.isOpen ? t("distill.open") : t("distill.closed") }}
              </div>
              <!-- Manual override buttons (only when not in an active run) -->
              <div class="mt-2 d-flex justify-center ga-1" v-if="!running">
                <v-btn size="x-small" color="green" @click="manualValve(valve.role, true)">
                  {{ t("distill.open") }}
                </v-btn>
                <v-btn size="x-small" color="red" @click="manualValve(valve.role, false)">
                  {{ t("distill.close") }}
                </v-btn>
              </div>
            </v-card-text>
          </v-card>
        </div>
      </v-col>
    </v-row>
  </v-container>
</template>
