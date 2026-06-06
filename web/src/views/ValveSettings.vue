<script lang="ts" setup>
import WebConn from "@/helpers/webConn";
import { inject, onMounted, ref } from "vue";
import { useI18n } from "vue-i18n";
const { t } = useI18n({ useScope: "global" });

const webConn = inject<WebConn>("webConn");

interface IValve {
  id: number;
  name: string;
  pinNr: number;
  role: number;
  activeHigh: boolean;
  useForBrewing: boolean;
}

const valves = ref<IValve[]>([]);

const alert     = ref<string>("");
const alertType = ref<"error" | "success" | "warning" | "info">("info");

const roleOptions = [
  { title: t("valveSettings.role_none"),    value: 0 },
  { title: t("valveSettings.role_water"),   value: 1 },
  { title: t("valveSettings.role_deflegm"), value: 2 },
  { title: t("valveSettings.role_heads"),   value: 3 },
  { title: t("valveSettings.role_product"), value: 4 },
  { title: t("valveSettings.role_cooler"),  value: 5 },
  { title: t("valveSettings.role_npg"),     value: 6 },
  { title: t("valveSettings.role_drain"),   value: 7 },
];

const getData = async () => {
  const result = await webConn?.doPostRequest({ command: "GetValveSettings", data: null });
  if (result?.success && Array.isArray(result.data)) valves.value = result.data;
};

const save = async () => {
  const result = await webConn?.doPostRequest({ command: "SaveValveSettings", data: valves.value });
  alert.value     = result?.message ?? (result?.success ? t("general.saved") : t("general.error"));
  alertType.value = result?.success ? "success" : "error";
  window.scrollTo(0, 0);
};

const addValve = () => {
  const nextId = valves.value.length > 0 ? Math.max(...valves.value.map((v) => v.id)) + 1 : 1;
  valves.value.push({
    id: nextId,
    name: `Valve ${nextId}`,
    pinNr: -1,
    role: 0,
    activeHigh: true,
    useForBrewing: false,
  });
};

const removeValve = (idx: number) => {
  valves.value.splice(idx, 1);
};

onMounted(() => getData());
</script>

<template>
  <v-container class="pa-6" fluid>
    <v-alert :type="alertType" v-if="alert" closable @click:close="alert = ''" class="mb-4">{{ alert }}</v-alert>

    <v-card variant="outlined">
      <v-card-title>{{ t("valveSettings.title") }}</v-card-title>
      <v-card-subtitle>{{ t("valveSettings.subtitle") }}</v-card-subtitle>
      <v-card-text>

        <v-row class="font-weight-bold text-caption text-medium-emphasis d-none d-md-flex">
          <v-col md="2">{{ t("valveSettings.name") }}</v-col>
          <v-col md="1">{{ t("valveSettings.pin") }}</v-col>
          <v-col md="3">{{ t("valveSettings.role") }}</v-col>
          <v-col md="2">{{ t("valveSettings.active_high") }}</v-col>
          <v-col md="2">{{ t("valveSettings.use_for_brewing") }}</v-col>
          <v-col md="1" />
        </v-row>

        <v-row v-for="(valve, idx) in valves" :key="valve.id" align="center" class="mb-1">
          <v-col cols="12" md="2">
            <v-text-field v-model="valve.name" :label="t('valveSettings.name')" density="compact" variant="outlined" />
          </v-col>
          <v-col cols="6" md="1">
            <v-text-field
              v-model.number="valve.pinNr"
              :label="t('valveSettings.pin')"
              type="number"
              density="compact"
              variant="outlined"
              :hint="t('valveSettings.pin_hint')"
              persistent-hint
            />
          </v-col>
          <v-col cols="12" md="3">
            <v-select v-model="valve.role" :items="roleOptions" :label="t('valveSettings.role')" density="compact" variant="outlined" />
          </v-col>
          <v-col cols="6" md="2">
            <v-checkbox v-model="valve.activeHigh" :label="t('valveSettings.active_high')" density="compact" />
          </v-col>
          <v-col cols="6" md="2">
            <v-checkbox v-model="valve.useForBrewing" :label="t('valveSettings.use_for_brewing')" density="compact" />
          </v-col>
          <v-col cols="auto" md="1">
            <v-btn icon size="small" color="error" variant="text" @click="removeValve(idx)">
              <v-icon>mdi-delete</v-icon>
            </v-btn>
          </v-col>
        </v-row>

        <v-row class="mt-2">
          <v-col cols="auto">
            <v-btn color="secondary" variant="outlined" prepend-icon="mdi-plus" @click="addValve">
              {{ t("valveSettings.add") }}
            </v-btn>
          </v-col>
        </v-row>

      </v-card-text>
    </v-card>

    <v-row class="mt-4">
      <v-col cols="auto">
        <v-btn color="success" size="large" @click="save">{{ t("general.save") }}</v-btn>
      </v-col>
    </v-row>
  </v-container>
</template>
