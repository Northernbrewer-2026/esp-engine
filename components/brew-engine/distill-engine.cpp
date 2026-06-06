/*
 * esp-brew-engine - Distilling Extension
 * Copyright (C) 2024
 *
 * Implements DistillEngine: all distilling state machines translated from
 * the original Russian Arduino sketch into ESP32/FreeRTOS C++.
 *
 * Design notes:
 *  - All state machines run inside a single 1-second-tick FreeRTOS task
 *    (distillLoop).  The ESP32 has ample headroom for this.
 *  - Temperature values are floats in °C (converted from tenths-of-degree
 *    integers at the boundary with UpdateTemperatures()).
 *  - Valve PWM is approximated in seconds rather than 50 Hz half-periods
 *    (the ESP32 doesn't share a 50 Hz zero-cross with the Arduino), but the
 *    ratio is preserved.  One half-period ≈ 10 ms → 100 half-periods ≈ 1 s.
 *  - The triac / heater output is controlled by BrewEngine's existing PID
 *    loop during brewing.  During distilling we override it via
 *    SetHeaterOutput() which lets BrewEngine's outputLoop set the actual GPIO.
 */

#include "distill-engine.h"
#include "esp_log.h"

static const char *TAG = "DistillEngine";

// ── Convert half-periods → seconds (100 half-periods ≈ 1 s at 50 Hz) ─────
static inline uint16_t hp2sec(uint16_t hp) { return (hp + 50) / 100; }
static inline uint16_t sec2hp(uint16_t s)  { return s * 100; }

// ── Helpers ────────────────────────────────────────────────────────────────

DistillEngine::DistillEngine(SettingsManager *sm)
    : settingsManager(sm)
{
}

void DistillEngine::Init(bool inv)
{
    invertOutputs = inv;
    gpioHigh = inv ? 0 : 1;
    gpioLow  = inv ? 1 : 0;

    readPins();
    readSettings();
    readValves();
    initValveGPIO();

    ESP_LOGI(TAG, "DistillEngine initialised");
}

void DistillEngine::UpdateTemperatures(const vector<float> &t)
{
    temps = t;
}

void DistillEngine::SetHeaterOutput(uint8_t pct)
{
    heaterOutputPct = pct;
}

// ── Pin helpers ────────────────────────────────────────────────────────────

void DistillEngine::initPin(gpio_num_t pin)
{
    if (pin == GPIO_NUM_NC) return;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, gpioLow);
}

void DistillEngine::pinHigh(gpio_num_t pin)
{
    if (pin != GPIO_NUM_NC) gpio_set_level(pin, gpioHigh);
}

void DistillEngine::pinLow(gpio_num_t pin)
{
    if (pin != GPIO_NUM_NC) gpio_set_level(pin, gpioLow);
}

// ── Valve helpers ──────────────────────────────────────────────────────────

Valve *DistillEngine::valveByRole(ValveRole role)
{
    for (auto *v : valves)
        if (v->role == role) return v;
    return nullptr;
}

void DistillEngine::openValve(ValveRole role)
{
    auto *v = valveByRole(role);
    if (!v) return;
    v->pwmOpenSec  = sec2hp(1); // effectively always open
    v->pwmCloseSec = 0;
}

void DistillEngine::closeValve(ValveRole role)
{
    auto *v = valveByRole(role);
    if (!v) return;
    v->pwmOpenSec  = 0;
    v->pwmCloseSec = sec2hp(1);
}

void DistillEngine::setValvePWM(ValveRole role, uint16_t openHp, uint16_t closeHp)
{
    auto *v = valveByRole(role);
    if (!v) return;
    v->pwmOpenSec  = openHp;
    v->pwmCloseSec = closeHp;
}

void DistillEngine::closeAllValves()
{
    for (auto *v : valves)
    {
        v->pwmOpenSec  = 0;
        v->pwmCloseSec = 100;
        v->isOpen      = false;
        if (v->pinNr != GPIO_NUM_NC)
            gpio_set_level(v->pinNr, v->activeHigh ? gpioLow : gpioHigh);
    }
}

void DistillEngine::tickValvePWM()
{
    // Called every second.  Each valve's counter advances; when it exceeds
    // the open or close threshold we toggle the physical output.
    for (auto *v : valves)
    {
        if (v->pinNr == GPIO_NUM_NC) continue;

        uint16_t openSec  = hp2sec(v->pwmOpenSec);
        uint16_t closeSec = hp2sec(v->pwmCloseSec);

        if (openSec == 0)
        {
            // Always closed
            if (v->isOpen)
            {
                v->isOpen = false;
                gpio_set_level(v->pinNr, v->activeHigh ? gpioLow : gpioHigh);
            }
            v->pwmCounter = 0;
            continue;
        }
        if (closeSec == 0)
        {
            // Always open
            if (!v->isOpen)
            {
                v->isOpen = true;
                gpio_set_level(v->pinNr, v->activeHigh ? gpioHigh : gpioLow);
            }
            v->pwmCounter = 0;
            continue;
        }

        v->pwmCounter++;
        uint16_t cycle = openSec + closeSec;
        if (v->pwmCounter >= cycle) v->pwmCounter = 0;

        bool shouldOpen = (v->pwmCounter < openSec);
        if (shouldOpen != v->isOpen)
        {
            v->isOpen = shouldOpen;
            gpio_set_level(v->pinNr, (shouldOpen ^ !v->activeHigh) ? gpioHigh : gpioLow);
        }
    }
}

// ── Safe stop (all outputs off) ────────────────────────────────────────────

void DistillEngine::safeStop()
{
    closeAllValves();
    pinLow(pins.pump);
    pinLow(pins.mixer);
    // Signal zero output to heater (BrewEngine will pick this up)
    heaterOutputPct = 0;
    running  = false;
    mode     = DISTILL_NONE;
    state    = DS_IDLE;
    statusText = "Idle";
    ESP_LOGI(TAG, "Distill safe stop");
}

// ── Column stabilisation check (shared by Rectification & NDRF) ────────────

bool DistillEngine::columnStabilised()
{
    if (temps.size() <= DISTILL_TEMP_COLUMN) return false;
    float col = temps[DISTILL_TEMP_COLUMN];

    // Positive stabilisation time: relative to last temp change
    if (settings.timeColumnStabiliseSec >= 0)
    {
        if (abs(col - tempColumnPrev) >= 0.2f)
        {
            tempColumnPrev = col;
            secTempPrev    = elapsedSecInternal;
            return false;
        }
        return (elapsedSecInternal - secTempPrev) > settings.timeColumnStabiliseSec;
    }
    else
    {
        // Negative: absolute time from state entry
        return (elapsedSecInternal - secTempPrev) > (-settings.timeColumnStabiliseSec);
    }
}

// ── Product PWM calculator (auto-adjust based on temp) ────────────────────

uint8_t DistillEngine::calcProductPWM()
{
    // Simple linear: current PWM, adjusted by auto-increment interval
    // (mirrors GetCHIMOtbor() in the original sketch but simplified)
    return productPWMpct;
}

// ─────────────────────────────────────────────────────────────────────────
//  STATE MACHINES
// ─────────────────────────────────────────────────────────────────────────

// ── Simple Distillation (modes 1/2/3) ─────────────────────────────────────
//
// Mirrors ProcessSimpleDistill() from the original sketch.
// Runs at full power until the cube temperature exceeds the configured
// end-temperature for this distillation number.

void DistillEngine::runSimpleDistill()
{
    if (temps.empty()) return;

    float tCube   = (temps.size() > DISTILL_TEMP_CUBE)   ? temps[DISTILL_TEMP_CUBE]   : 0;
    float tColumn = (temps.size() > DISTILL_TEMP_COLUMN) ? temps[DISTILL_TEMP_COLUMN] : 0;

    uint16_t endTempTenths = settings.tempSimpleDistill1;
    if (mode == DISTILL_SIMPLE2) endTempTenths = settings.tempSimpleDistill2;
    if (mode == DISTILL_SIMPLE3) endTempTenths = settings.tempSimpleDistill3;
    float endTemp = endTempTenths / 10.0f;

    // TSA safety check
    if (temps.size() > DISTILL_TEMP_TSA && temps[DISTILL_TEMP_TSA] > 65.0f)
    {
        state = DS_ALARM_TSA;
        statusText = "Alarm: TSA over-temp!";
        safeStop();
        return;
    }

    switch (state)
    {
    case DS_IDLE:
        break;

    case DS_WARMUP:
        // Full power; open water supply; wait for deflegmator sensor if present
        statusText = "Warming up";
        heaterOutputPct = 100;
        openValve(VALVE_ROLE_WATER);
        {
            float deflegStart = settings.tempDeflegmatorStart / 10.0f;
            bool deflegReady  = (tColumn >= deflegStart) || (temps.size() <= DISTILL_TEMP_COLUMN);
            if (deflegReady)
            {
                openValve(VALVE_ROLE_COOLER);
                state = DS_PRODUCT;
                statusText = "Distilling";
            }
        }
        break;

    case DS_PRODUCT:
        // Reduced power; collect until end temp
        heaterOutputPct = (uint8_t)((settings.powerDistillSimple * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        if (tCube >= endTemp)
        {
            statusText = "Cooling down";
            state      = DS_COOLDOWN;
            secTempPrev = elapsedSecInternal;
        }
        break;

    case DS_COOLDOWN:
        heaterOutputPct = 0;
        if (elapsedSecInternal - secTempPrev >= 120) // 2-minute cooldown
        {
            state = DS_DONE;
            statusText = "Done";
            safeStop();
        }
        break;

    default:
        break;
    }
}

// ── Heads Collection ───────────────────────────────────────────────────────
//
// Mirrors ProcessSimpleGlv().  Waits for the deflegmator to warm up, then
// opens the heads valve at configured PWM until cube temp exceeds dist3 end.

void DistillEngine::runHeadsCollection()
{
    if (temps.empty()) return;

    float tCube   = temps.size() > DISTILL_TEMP_CUBE   ? temps[DISTILL_TEMP_CUBE]   : 0;
    float tColumn = temps.size() > DISTILL_TEMP_COLUMN ? temps[DISTILL_TEMP_COLUMN] : 0;

    float deflegStart = settings.tempDeflegmatorStart / 10.0f;
    float endTemp     = settings.tempSimpleDistill3 / 10.0f;

    if (temps.size() > DISTILL_TEMP_TSA && temps[DISTILL_TEMP_TSA] > 65.0f)
    {
        state = DS_ALARM_TSA; statusText = "Alarm: TSA over-temp!"; safeStop(); return;
    }

    switch (state)
    {
    case DS_WARMUP:
        statusText = "Warming up";
        heaterOutputPct = 100;
        openValve(VALVE_ROLE_WATER);
        if (tColumn >= deflegStart || temps.size() <= DISTILL_TEMP_COLUMN)
        {
            state = DS_HEADS;
            statusText = "Collecting heads";
        }
        break;

    case DS_HEADS:
    {
        heaterOutputPct = (uint8_t)((settings.powerHeadsDistill * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        // Wait until cube temp clears deflegStart, then open cooler
        if (tCube >= deflegStart) openValve(VALVE_ROLE_COOLER);

        // Heads valve PWM
        uint16_t openHp  = (settings.pwmHeadsCycleHalfperiods / 10) * settings.pwmHeadsPercent;
        uint16_t closeHp = settings.pwmHeadsCycleHalfperiods - openHp;
        setValvePWM(VALVE_ROLE_HEADS, openHp, closeHp);

        if (tCube >= endTemp)
        {
            closeValve(VALVE_ROLE_HEADS);
            state = DS_COOLDOWN;
            statusText = "Cooling down";
            secTempPrev = elapsedSecInternal;
        }
        break;
    }

    case DS_COOLDOWN:
        heaterOutputPct = 0;
        if (elapsedSecInternal - secTempPrev >= 60)
        {
            state = DS_DONE; statusText = "Done"; safeStop();
        }
        break;

    default:
        break;
    }
}

// ── Rectification ─────────────────────────────────────────────────────────
//
// Mirrors ProcessRectif().
// Phases: Accelerate → Stabilise → Heads → HeadsWait → Product → Tails → Cooldown

void DistillEngine::runRectification()
{
    if (temps.empty()) return;

    float tCube   = temps.size() > DISTILL_TEMP_CUBE   ? temps[DISTILL_TEMP_CUBE]   : 0;
    float tColumn = temps.size() > DISTILL_TEMP_COLUMN ? temps[DISTILL_TEMP_COLUMN] : 0;

    float tAccelEnd    = settings.tempEndRectAccelerate / 10.0f;
    float tHeadsEnd    = settings.tempEndHeadsColl      / 10.0f;
    float tProductEnd  = settings.tempEndProductColl    / 10.0f;
    float tRectEnd     = settings.tempEndRectification  / 10.0f;

    if (temps.size() > DISTILL_TEMP_TSA && temps[DISTILL_TEMP_TSA] > 65.0f)
    {
        state = DS_ALARM_TSA; statusText = "Alarm: TSA over-temp!"; safeStop(); return;
    }

    switch (state)
    {
    case DS_ACCELERATE:
        statusText = "Accelerating";
        heaterOutputPct = 100;
        openValve(VALVE_ROLE_WATER);
        if (tCube >= tAccelEnd)
        {
            openValve(VALVE_ROLE_DEFLEGM);
            tempColumnPrev = tColumn;
            secTempPrev    = elapsedSecInternal;
            state          = DS_STABILISE;
            statusText     = "Stabilising column";
        }
        break;

    case DS_STABILISE:
        heaterOutputPct = (uint8_t)((settings.powerRectification * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        if (tColumn > 50.0f && columnStabilised())
        {
            tempProductStab  = tColumn;
            productPWMpct    = settings.pwmProductStartPercent;
            secTempPrev      = elapsedSecInternal;
            state            = DS_HEADS;
            statusText       = "Collecting heads";
        }
        break;

    case DS_HEADS:
    {
        heaterOutputPct = (uint8_t)((settings.powerRectification * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        closeValve(VALVE_ROLE_PRODUCT);

        uint16_t openHp  = (settings.pwmHeadsCycleHalfperiods / 10) * settings.pwmHeadsPercent;
        uint16_t closeHp = settings.pwmHeadsCycleHalfperiods - openHp;
        setValvePWM(VALVE_ROLE_HEADS, openHp, closeHp);

        tempProductStab = tColumn;

        if (tCube >= tHeadsEnd)
        {
            closeValve(VALVE_ROLE_HEADS);
            productPWMpct = settings.pwmProductStartPercent;
            state         = DS_HEADS_WAIT;
            statusText    = "Heads done – waiting";
        }
        break;
    }

    case DS_HEADS_WAIT:
    {
        closeValve(VALVE_ROLE_PRODUCT);
        if (tCube >= tProductEnd)
        {
            state = DS_TAILS; statusText = "Collecting tails"; break;
        }
        // Wait until column cools back to stabilisation temp
        if (tColumn <= tempProductStab)
        {
            secTempPrev = elapsedSecInternal;
            state       = DS_PRODUCT;
            statusText  = "Collecting product";
        }
        // If restabilisation timeout exceeded, update stab temp
        if (settings.timeColumnRestabiliseSec > 0 &&
            (elapsedSecInternal - secTempPrev) > settings.timeColumnRestabiliseSec)
        {
            tempProductStab = tColumn;
        }
        break;
    }

    case DS_PRODUCT:
    {
        heaterOutputPct = (uint8_t)((settings.powerRectification * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        closeValve(VALVE_ROLE_HEADS);

        productPWMpct  = calcProductPWM();
        uint16_t openHp  = ((settings.pwmProductCycleHalfperiods / 10) * productPWMpct);
        uint16_t closeHp = settings.pwmProductCycleHalfperiods - openHp;
        setValvePWM(VALVE_ROLE_PRODUCT, openHp, closeHp);

        float delta = settings.pwmProductDeltaTenths / 10.0f;
        if (tColumn >= tempProductStab + delta)
        {
            // Column rose: stop, reduce PWM
            if (productPWMpct > settings.pwmProductMinPercent)
                productPWMpct -= 10;
            if (productPWMpct < settings.pwmProductMinPercent)
                productPWMpct = settings.pwmProductMinPercent;
            timeRestab  = elapsedSecInternal;
            state       = DS_HEADS_WAIT;
            statusText  = "Stop – column rising";
        }
        else
        {
            // Auto-increment if stable long enough
            if (settings.timeAutoIncProductPWMSec > 0 &&
                (elapsedSecInternal - secTempPrev) > (int32_t)settings.timeAutoIncProductPWMSec)
            {
                if (productPWMpct > settings.pwmProductMinPercent)
                    productPWMpct += 5;
                if (productPWMpct > 95) productPWMpct = 95;
                secTempPrev = elapsedSecInternal;
            }
        }

        if (tCube >= tProductEnd)
        {
            closeValve(VALVE_ROLE_PRODUCT);
            state = DS_TAILS; statusText = "Collecting tails";
        }
        break;
    }

    case DS_TAILS:
    {
        heaterOutputPct = (uint8_t)((settings.powerRectification * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        uint16_t openHp  = (settings.pwmHeadsCycleHalfperiods / 100) * 90;
        uint16_t closeHp = settings.pwmHeadsCycleHalfperiods - openHp;
        setValvePWM(VALVE_ROLE_HEADS, openHp, closeHp);

        if (tCube >= tRectEnd)
        {
            closeValve(VALVE_ROLE_HEADS);
            secTempPrev = elapsedSecInternal;
            state       = DS_COOLDOWN;
            statusText  = "Cooling down";
        }
        break;
    }

    case DS_COOLDOWN:
        heaterOutputPct = 0;
        if (elapsedSecInternal - secTempPrev >= 180)
        {
            state = DS_DONE; statusText = "Done"; safeStop();
        }
        break;

    default:
        break;
    }
}

// ── Distillation with Deflegmator ──────────────────────────────────────────
//
// Mirrors ProcessDistilDefl().  Keeps deflegmator temperature in a band by
// switching between 0 %, 50 %, and 100 % deflegmator water flow.

void DistillEngine::runDistillDefl()
{
    if (temps.empty()) return;

    float tCube   = temps.size() > DISTILL_TEMP_CUBE   ? temps[DISTILL_TEMP_CUBE]   : 0;
    float tColumn = temps.size() > DISTILL_TEMP_COLUMN ? temps[DISTILL_TEMP_COLUMN] : 0;

    float tDeflTarget = settings.tempDeflegmator       / 10.0f;
    float tDeflStart  = settings.tempDeflegmatorStart  / 10.0f;
    float tDeflDelta  = settings.deflegmatorDeltaTenths / 10.0f;
    float tEndDefl    = settings.tempDeflegmatorEnd    / 10.0f;

    if (temps.size() > DISTILL_TEMP_TSA && temps[DISTILL_TEMP_TSA] > 65.0f)
    {
        state = DS_ALARM_TSA; statusText = "Alarm: TSA over-temp!"; safeStop(); return;
    }

    if (tCube >= tEndDefl && state < DS_COOLDOWN)
    {
        state = DS_COOLDOWN; secTempPrev = elapsedSecInternal; statusText = "Cooling down";
    }

    switch (state)
    {
    case DS_WARMUP:
        statusText = "Warming up";
        heaterOutputPct = 100;
        openValve(VALVE_ROLE_WATER);
        if (tColumn >= tDeflStart || temps.size() <= DISTILL_TEMP_COLUMN)
        {
            openValve(VALVE_ROLE_COOLER);
            state = DS_STABILISE; // re-use Stabilise as "no deflegmator" phase
            statusText = "Distilling (no deflegmator)";
        }
        break;

    case DS_STABILISE: // no-deflegmator phase
        heaterOutputPct = (uint8_t)((settings.powerDistillSimple * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        if (tColumn > tDeflTarget)
        {
            openValve(VALVE_ROLE_DEFLEGM);
            state = DS_HEADS; // 50 % deflegmator phase
            statusText = "50% deflegmator";
        }
        break;

    case DS_HEADS: // 50% deflegmator
        heaterOutputPct = (uint8_t)((settings.powerDistillSimple * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        if (tColumn > tDeflTarget + tDeflDelta)
        {
            closeValve(VALVE_ROLE_COOLER);
            state = DS_PRODUCT; // 100 % deflegmator
            statusText = "100% deflegmator";
        }
        else if (tColumn < tDeflTarget - tDeflDelta)
        {
            closeValve(VALVE_ROLE_DEFLEGM);
            openValve(VALVE_ROLE_COOLER);
            state = DS_STABILISE; statusText = "Distilling (no deflegmator)";
        }
        break;

    case DS_PRODUCT: // 100% deflegmator
        heaterOutputPct = (uint8_t)((settings.powerDistillSimple * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        if (tColumn < tDeflTarget)
        {
            openValve(VALVE_ROLE_COOLER);
            state = DS_HEADS; statusText = "50% deflegmator";
        }
        break;

    case DS_COOLDOWN:
        heaterOutputPct = 0;
        if (elapsedSecInternal - secTempPrev >= 120)
        {
            state = DS_DONE; statusText = "Done"; safeStop();
        }
        break;

    default:
        break;
    }
}

// ── NDRF (Non-Return-Flow Rectification) ──────────────────────────────────
//
// Mirrors ProcessNDRF().  Similar to Rectification but stabilisation is
// strictly time-based (absolute seconds) rather than temperature-based.

void DistillEngine::runNDRF()
{
    if (temps.empty()) return;

    float tCube  = temps.size() > DISTILL_TEMP_CUBE   ? temps[DISTILL_TEMP_CUBE]   : 0;
    float tCol   = temps.size() > DISTILL_TEMP_COLUMN ? temps[DISTILL_TEMP_COLUMN] : 0;

    float tAccelEnd   = settings.tempEndRectAccelerate / 10.0f;
    float tHeadsEnd   = settings.tempEndHeadsColl      / 10.0f;
    float tProductEnd = settings.tempEndProductColl    / 10.0f;
    float tRectEnd    = settings.tempEndRectification  / 10.0f;

    if (temps.size() > DISTILL_TEMP_TSA && temps[DISTILL_TEMP_TSA] > 65.0f)
    {
        state = DS_ALARM_TSA; statusText = "Alarm: TSA over-temp!"; safeStop(); return;
    }

    switch (state)
    {
    case DS_ACCELERATE:
        statusText = "Accelerating";
        heaterOutputPct = 100;
        openValve(VALVE_ROLE_WATER);
        if (tCube >= tAccelEnd)
        {
            openValve(VALVE_ROLE_DEFLEGM);
            secTempPrev = elapsedSecInternal;
            // Default 30-minute absolute stabilisation
            settings.timeColumnStabiliseSec = -1800;
            state = DS_STABILISE;
            statusText = "Stabilising (timed)";
        }
        break;

    case DS_STABILISE:
    {
        heaterOutputPct = (uint8_t)((settings.powerRectification * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        int32_t elapsed = elapsedSecInternal - secTempPrev;
        int32_t remaining = (-settings.timeColumnStabiliseSec) - elapsed;
        char buf[64];
        snprintf(buf, sizeof(buf), "Stabilising – %lds remaining", (long)remaining);
        statusText = buf;
        if (remaining <= 0)
        {
            tempProductStab = tCol;
            productPWMpct   = settings.pwmProductStartPercent;
            secTempPrev     = elapsedSecInternal;
            state           = DS_HEADS;
            statusText      = "Collecting heads";
        }
        break;
    }

    // Heads, Product, Tails, Cooldown identical to Rectification
    case DS_HEADS:
    {
        heaterOutputPct = (uint8_t)((settings.powerRectification * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        closeValve(VALVE_ROLE_PRODUCT);
        uint16_t openHp  = (settings.pwmHeadsCycleHalfperiods / 10) * settings.pwmHeadsPercent;
        setValvePWM(VALVE_ROLE_HEADS, openHp, settings.pwmHeadsCycleHalfperiods - openHp);
        tempProductStab = tCol;
        if (tCube >= tHeadsEnd)
        {
            closeValve(VALVE_ROLE_HEADS);
            productPWMpct = settings.pwmProductStartPercent;
            state = DS_HEADS_WAIT; statusText = "Heads done – waiting";
        }
        break;
    }
    case DS_HEADS_WAIT:
        closeValve(VALVE_ROLE_PRODUCT);
        if (tCube >= tProductEnd)  { state = DS_TAILS; statusText = "Collecting tails"; break; }
        if (tCol <= tempProductStab) { secTempPrev = elapsedSecInternal; state = DS_PRODUCT; statusText = "Collecting product"; }
        break;

    case DS_PRODUCT:
    {
        heaterOutputPct = (uint8_t)((settings.powerRectification * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        uint16_t openHp  = ((settings.pwmProductCycleHalfperiods / 10) * productPWMpct);
        setValvePWM(VALVE_ROLE_PRODUCT, openHp, settings.pwmProductCycleHalfperiods - openHp);
        float delta = settings.pwmProductDeltaTenths / 10.0f;
        if (tCol >= tempProductStab + delta) { state = DS_HEADS_WAIT; statusText = "Stop – column rising"; }
        if (tCube >= tProductEnd)            { closeValve(VALVE_ROLE_PRODUCT); state = DS_TAILS; statusText = "Collecting tails"; }
        break;
    }

    case DS_TAILS:
    {
        heaterOutputPct = (uint8_t)((settings.powerRectification * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        uint16_t openHp  = (settings.pwmHeadsCycleHalfperiods / 100) * 90;
        setValvePWM(VALVE_ROLE_HEADS, openHp, settings.pwmHeadsCycleHalfperiods - openHp);
        if (tCube >= tRectEnd) { closeValve(VALVE_ROLE_HEADS); secTempPrev = elapsedSecInternal; state = DS_COOLDOWN; statusText = "Cooling down"; }
        break;
    }

    case DS_COOLDOWN:
        heaterOutputPct = 0;
        if (elapsedSecInternal - secTempPrev >= 180) { state = DS_DONE; statusText = "Done"; safeStop(); }
        break;

    default:
        break;
    }
}

// ── NBK Pump-Still ────────────────────────────────────────────────────────
//
// Mirrors ProcessNBK().  The "still" is distilled by pumping the wash through
// a cooler; the deflegmator sensor monitors over-temperature.

void DistillEngine::runNBK()
{
    if (temps.empty()) return;

    float tColumn = temps.size() > DISTILL_TEMP_COLUMN ? temps[DISTILL_TEMP_COLUMN] : 0;
    float tDeflStart = settings.tempDeflegmatorStart / 10.0f;

    switch (state)
    {
    case DS_WARMUP:
        statusText = "Heating – waiting for column";
        heaterOutputPct = 100;
        openValve(VALVE_ROLE_WATER);
        if (tColumn >= tDeflStart || temps.size() <= DISTILL_TEMP_COLUMN)
        {
            openValve(VALVE_ROLE_COOLER);
            state = DS_STABILISE; // re-use as "waiting for user start"
            statusText = "Ready – press Start to run pump";
        }
        break;

    case DS_STABILISE: // waiting for user to confirm pump start
        heaterOutputPct = (uint8_t)((settings.powerNBK * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        // User must call DistillStartPump command to advance to DS_PRODUCT
        break;

    case DS_PRODUCT: // pump running
        heaterOutputPct = (uint8_t)((settings.powerNBK * 100) /
                                    max((uint16_t)1, settings.powerTotalWatt));
        pumpSpeed = settings.speedNBK;
        if (pins.pump != GPIO_NUM_NC)
            gpio_set_level(pins.pump, pumpSpeed > 0 ? gpioHigh : gpioLow);

        // Over-temp detection: > 98 °C at column → hold
        if (tColumn >= 98.0f && elapsedSecInternal > 60)
        {
            secTempPrev = elapsedSecInternal;
            state = DS_TAILS;
            statusText = "Column too hot – holding";
        }
        break;

    case DS_TAILS: // column cooling hold
        if (tColumn < 97.0f) { state = DS_PRODUCT; statusText = "Pump running"; }
        if (elapsedSecInternal - secTempPrev >= 300) { state = DS_DONE; safeStop(); }
        break;

    case DS_DONE:
        safeStop();
        break;

    default:
        break;
    }
}

// ── Valve Test ────────────────────────────────────────────────────────────

void DistillEngine::runTestValves()
{
    static uint8_t testIdx = 0;
    static int32_t testSecStart = 0;

    ValveRole roles[] = {
        VALVE_ROLE_WATER, VALVE_ROLE_DEFLEGM, VALVE_ROLE_HEADS,
        VALVE_ROLE_PRODUCT, VALVE_ROLE_COOLER, VALVE_ROLE_NPG
    };
    const int nRoles = sizeof(roles) / sizeof(roles[0]);

    switch (state)
    {
    case DS_WARMUP:
        closeAllValves();
        testIdx      = 0;
        testSecStart = elapsedSecInternal;
        state        = DS_PRODUCT; // use DS_PRODUCT as "test active"
        statusText   = "Testing valve 0";
        break;

    case DS_PRODUCT:
        if (testIdx < nRoles)
            openValve(roles[testIdx]);

        if (elapsedSecInternal - testSecStart >= 10) // 10s per valve
        {
            if (testIdx < nRoles) closeValve(roles[testIdx]);
            testIdx++;
            testSecStart = elapsedSecInternal;

            if (testIdx >= nRoles)
            {
                state = DS_DONE; statusText = "Valve test done"; safeStop();
            }
            else
            {
                char buf[40]; snprintf(buf, sizeof(buf), "Testing valve %d", testIdx);
                statusText = buf;
            }
        }
        break;

    default:
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────
//  MAIN LOOP TASK
// ─────────────────────────────────────────────────────────────────────────

void DistillEngine::distillLoop(void *arg)
{
    DistillEngine *inst = (DistillEngine *)arg;

    while (inst->running)
    {
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1-second tick
        inst->elapsedSecInternal++;
        inst->elapsedSec = (uint32_t)inst->elapsedSecInternal;

        inst->tickValvePWM();

        switch (inst->mode)
        {
        case DISTILL_SIMPLE1:
        case DISTILL_SIMPLE2:
        case DISTILL_SIMPLE3:
            inst->runSimpleDistill();
            break;
        case DISTILL_HEADS:
            inst->runHeadsCollection();
            break;
        case DISTILL_RECTIFICATION:
            inst->runRectification();
            break;
        case DISTILL_DEFL:
            inst->runDistillDefl();
            break;
        case DISTILL_NDRF:
            inst->runNDRF();
            break;
        case DISTILL_NBK:
            inst->runNBK();
            break;
        case DISTILL_TEST_VALVES:
            inst->runTestValves();
            break;
        default:
            break;
        }
    }

    vTaskDelete(NULL);
}

// ─────────────────────────────────────────────────────────────────────────
//  START / STOP
// ─────────────────────────────────────────────────────────────────────────

void DistillEngine::start(DistillMode m)
{
    if (running) stop();

    mode               = m;
    elapsedSecInternal = 0;
    elapsedSec         = 0;
    secTempPrev        = 0;
    tempColumnPrev     = 0;
    productPWMpct      = settings.pwmProductStartPercent;
    running            = true;

    // NDRF stabilisation uses negative = absolute time
    if (m == DISTILL_NDRF)
        settings.timeColumnStabiliseSec = -1800;

    // Initial state: most modes start with warmup
    state = (m == DISTILL_RECTIFICATION || m == DISTILL_NDRF)
            ? DS_ACCELERATE
            : DS_WARMUP;

    ESP_LOGI(TAG, "Starting distill mode %d", (int)m);

    xTaskCreate(&DistillEngine::distillLoop, "distill_task", 4096, this, 5, &loopHandle);
}

void DistillEngine::stop()
{
    safeStop();
    if (loopHandle)
    {
        // running=false causes the loop to exit; wait briefly
        vTaskDelay(pdMS_TO_TICKS(1500));
        loopHandle = NULL;
    }
}

// ─────────────────────────────────────────────────────────────────────────
//  PERSISTENCE
// ─────────────────────────────────────────────────────────────────────────

void DistillEngine::readSettings()
{
    vector<uint8_t> empty = json::to_msgpack(json::object());
    vector<uint8_t> ser   = settingsManager->Read("distillCfg", empty);

    if (!ser.empty())
    {
        json j = json::from_msgpack(ser);
        if (!j.empty()) settings.from_json(j);
    }
    ESP_LOGI(TAG, "Distill settings loaded");
}

void DistillEngine::saveSettings()
{
    json j = settings.to_json();
    vector<uint8_t> ser = json::to_msgpack(j);
    settingsManager->Write("distillCfg", ser);
    ESP_LOGI(TAG, "Distill settings saved");
}

void DistillEngine::readPins()
{
    pins.triac          = (gpio_num_t)settingsManager->Read("dPinTriac",     (uint16_t)GPIO_NUM_NC);
    pins.pump           = (gpio_num_t)settingsManager->Read("dPinPump",      (uint16_t)GPIO_NUM_NC);
    pins.mixer          = (gpio_num_t)settingsManager->Read("dPinMixer",     (uint16_t)GPIO_NUM_NC);
    pins.alarmWater     = (gpio_num_t)settingsManager->Read("dPinAlmWater",  (uint16_t)GPIO_NUM_NC);
    pins.alarmLevel     = (gpio_num_t)settingsManager->Read("dPinAlmLevel",  (uint16_t)GPIO_NUM_NC);
    pins.alarmGas       = (gpio_num_t)settingsManager->Read("dPinAlmGas",    (uint16_t)GPIO_NUM_NC);
    pins.npgLevel       = (gpio_num_t)settingsManager->Read("dPinNPG",       (uint16_t)GPIO_NUM_NC);
    pins.pressureSensor = (gpio_num_t)settingsManager->Read("dPinPressure",  (uint16_t)GPIO_NUM_NC);
    ESP_LOGI(TAG, "Distill pins loaded");
}

void DistillEngine::savePins()
{
    settingsManager->Write("dPinTriac",    (uint16_t)pins.triac);
    settingsManager->Write("dPinPump",     (uint16_t)pins.pump);
    settingsManager->Write("dPinMixer",    (uint16_t)pins.mixer);
    settingsManager->Write("dPinAlmWater", (uint16_t)pins.alarmWater);
    settingsManager->Write("dPinAlmLevel", (uint16_t)pins.alarmLevel);
    settingsManager->Write("dPinAlmGas",   (uint16_t)pins.alarmGas);
    settingsManager->Write("dPinNPG",      (uint16_t)pins.npgLevel);
    settingsManager->Write("dPinPressure", (uint16_t)pins.pressureSensor);
    ESP_LOGI(TAG, "Distill pins saved");
}

void DistillEngine::readValves()
{
    vector<uint8_t> empty = json::to_msgpack(json::array());
    vector<uint8_t> ser   = settingsManager->Read("distillValves", empty);
    json jValves = json::from_msgpack(ser);

    for (auto *v : valves) delete v;
    valves.clear();

    if (jValves.empty())
    {
        addDefaultValves();
        return;
    }

    for (auto &jv : jValves)
    {
        auto *v = new Valve();
        v->from_json(jv);
        valves.push_back(v);
    }
    ESP_LOGI(TAG, "Loaded %d valves", (int)valves.size());
}

void DistillEngine::saveValves(const json &jValves)
{
    for (auto *v : valves) delete v;
    valves.clear();

    uint8_t id = 1;
    for (auto &jv : jValves)
    {
        auto *v = new Valve();
        json jvCopy = jv;
        jvCopy["id"] = id++;
        v->from_json(jvCopy);
        valves.push_back(v);
    }

    vector<uint8_t> ser = json::to_msgpack(jValves);
    settingsManager->Write("distillValves", ser);
    initValveGPIO();
    ESP_LOGI(TAG, "Saved %d valves", (int)valves.size());
}

void DistillEngine::addDefaultValves()
{
    struct DefaultValve { uint8_t id; const char *name; ValveRole role; };
    DefaultValve defs[] = {
        {1, "Water Supply",   VALVE_ROLE_WATER  },
        {2, "Deflegmator",    VALVE_ROLE_DEFLEGM},
        {3, "Heads Output",   VALVE_ROLE_HEADS  },
        {4, "Product Output", VALVE_ROLE_PRODUCT},
        {5, "Cooler",         VALVE_ROLE_COOLER },
        {6, "NPG",            VALVE_ROLE_NPG    },
    };
    for (auto &d : defs)
    {
        auto *v = new Valve();
        v->id          = d.id;
        v->name        = d.name;
        v->role        = d.role;
        v->pinNr       = GPIO_NUM_NC;
        v->activeHigh  = true;
        v->useForBrewing = false;
        valves.push_back(v);
    }
    ESP_LOGI(TAG, "Added %d default valves", (int)valves.size());
}

void DistillEngine::initValveGPIO()
{
    for (auto *v : valves)
    {
        if (v->pinNr == GPIO_NUM_NC) continue;
        gpio_reset_pin(v->pinNr);
        gpio_set_direction(v->pinNr, GPIO_MODE_OUTPUT);
        gpio_set_level(v->pinNr, v->activeHigh ? gpioLow : gpioHigh); // start closed
    }
    initPin(pins.pump);
    initPin(pins.mixer);
}

// ─────────────────────────────────────────────────────────────────────────
//  JSON API COMMAND ROUTER
// ─────────────────────────────────────────────────────────────────────────

string DistillEngine::processDistillCommand(const string &command, const json &data)
{
    json resultData = {};
    string message  = "";
    bool   success  = true;

    // ── Status / data ──────────────────────────────────────────────────
    if (command == "DistillData")
    {
        json jValveStates = json::array();
        for (auto *v : valves)
        {
            json jv;
            jv["id"]     = v->id;
            jv["name"]   = v->name;
            jv["role"]   = (int)v->role;
            jv["isOpen"] = v->isOpen;
            jValveStates.push_back(jv);
        }
        resultData = {
            {"mode",        (int)mode},
            {"state",       (int)state},
            {"status",      statusText},
            {"running",     running},
            {"elapsedSec",  elapsedSec},
            {"valves",      jValveStates},
            {"heaterPct",   heaterOutputPct},
            {"productPWM",  productPWMpct},
        };
    }

    // ── Start / stop ───────────────────────────────────────────────────
    else if (command == "DistillStart")
    {
        if (running)
        {
            message = "Already running – stop first"; success = false;
        }
        else
        {
            int m = data.value("mode", (int)DISTILL_NONE);
            if (m <= DISTILL_NONE || m > DISTILL_TEST_VALVES)
            {
                message = "Invalid distill mode"; success = false;
            }
            else
            {
                start((DistillMode)m);
                message = "Distillation started";
            }
        }
    }
    else if (command == "DistillStop")
    {
        stop();
        message = "Distillation stopped";
    }

    // ── NBK pump manual control ────────────────────────────────────────
    else if (command == "DistillStartPump")
    {
        if (mode == DISTILL_NBK && state == DS_STABILISE)
        {
            state = DS_PRODUCT;
            statusText = "Pump running";
        }
        else { message = "NBK mode not in waiting state"; success = false; }
    }

    // ── Settings ───────────────────────────────────────────────────────
    else if (command == "GetDistillSettings")
    {
        resultData = settings.to_json();
    }
    else if (command == "SaveDistillSettings")
    {
        settings.from_json(data);
        saveSettings();
        message = "Distill settings saved";
    }

    // ── Pin settings ───────────────────────────────────────────────────
    else if (command == "GetDistillPins")
    {
        resultData = pins.to_json();
    }
    else if (command == "SaveDistillPins")
    {
        pins.from_json(data);
        savePins();
        initValveGPIO();
        message = "Distill pin settings saved – restart recommended";
    }

    // ── Valve settings ─────────────────────────────────────────────────
    else if (command == "GetValveSettings")
    {
        json jValves = json::array();
        for (auto *v : valves) jValves.push_back(v->to_json());
        resultData = jValves;
    }
    else if (command == "SaveValveSettings")
    {
        if (running) { message = "Cannot save valve settings while running"; success = false; }
        else         { saveValves(data); message = "Valve settings saved"; }
    }

    // ── Manual valve control ───────────────────────────────────────────
    else if (command == "DistillOpenValve")
    {
        int role = data.value("role", -1);
        if (role >= 0) openValve((ValveRole)role);
        else { message = "Missing role"; success = false; }
    }
    else if (command == "DistillCloseValve")
    {
        int role = data.value("role", -1);
        if (role >= 0) closeValve((ValveRole)role);
        else { message = "Missing role"; success = false; }
    }

    else
    {
        message = "Unknown distill command: " + command;
        success = false;
    }

    json out;
    out["data"]    = resultData;
    out["success"] = success;
    if (!message.empty()) out["message"] = message;
    return out.dump();
}
