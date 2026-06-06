#ifndef _DistillEngine_H_
#define _DistillEngine_H_

/*
 * esp-brew-engine - Distilling Extension
 *
 * DistillEngine implements the distilling modes from the original
 * Arduino/Russian firmware as an ESP32 FreeRTOS task that runs alongside
 * (but independently of) the existing BrewEngine mash/boil logic.
 *
 * Supported modes (mirror IspReg values from original sketch):
 *   SimpleDistill1   – 1st plain pot-still run         (was IspReg 104)
 *   SimpleDistill2   – 2nd fractional run               (was IspReg 106)
 *   SimpleDistill3   – 3rd fractional run               (was IspReg 107)
 *   HeadsCollection  – Heads-only run w/ deflegmator    (was IspReg 105)
 *   Rectification    – Full column rectification        (was IspReg 109)
 *   DistillDefl      – Distillation with deflegmator    (was IspReg 110)
 *   NDRF             – Non-return-flow rectification    (was IspReg 111)
 *   NBK              – NBK pump-still                   (was IspReg 112)
 *   TestValves       – Valve test cycle                 (was IspReg 129)
 *
 * The engine exposes a processDistillCommand() method that is called from
 * BrewEngine::processCommand() when the command namespace is "Distill*".
 */

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <optional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nlohmann_json.hpp"

#include "valve.h"
#include "distill-settings.h"
#include "settings-manager.h"

using namespace std;
using json = nlohmann::json;

// ── Distilling process modes ──────────────────────────────────────────────
enum DistillMode
{
    DISTILL_NONE         = 0,
    DISTILL_SIMPLE1      = 1,  // 1st plain run
    DISTILL_SIMPLE2      = 2,  // 2nd fractional
    DISTILL_SIMPLE3      = 3,  // 3rd fractional
    DISTILL_HEADS        = 4,  // Heads collection
    DISTILL_RECTIFICATION= 5,  // Full rectification
    DISTILL_DEFL         = 6,  // Distillation w/ deflegmator
    DISTILL_NDRF         = 7,  // Non-return-flow rectification
    DISTILL_NBK          = 8,  // NBK pump-still
    DISTILL_TEST_VALVES  = 9,  // Valve test
};

// ── State machine steps per mode ─────────────────────────────────────────
enum DistillState
{
    DS_IDLE              = 0,
    DS_WARMUP            = 1,  // Heating up / waiting for deflegmator
    DS_ACCELERATE        = 2,  // Full-power acceleration (rectification)
    DS_STABILISE         = 3,  // Column stabilisation pause
    DS_HEADS             = 4,  // Collecting heads
    DS_HEADS_WAIT        = 5,  // Waiting for column to return after heads stop
    DS_PRODUCT           = 6,  // Collecting product (SR / spirit)
    DS_TAILS             = 7,  // Tails collection
    DS_COOLDOWN          = 8,  // Water cooldown before shutoff
    DS_DONE              = 100,
    DS_ALARM_TSA         = 101, // TSA (atmospheric connection) over-temp
    DS_ALARM_PRESSURE    = 102, // Pressure sensor alarm
};

// ── Alarm pin configuration (all stored in system settings) ──────────────
struct DistillPins
{
    gpio_num_t triac        = GPIO_NUM_NC; // Phase-control triac / SSR
    gpio_num_t pump         = GPIO_NUM_NC; // NBK pump PWM
    gpio_num_t mixer        = GPIO_NUM_NC; // Mixer/stirrer relay
    gpio_num_t alarmWater   = GPIO_NUM_NC; // Floor-flood sensor
    gpio_num_t alarmLevel   = GPIO_NUM_NC; // Receiver vessel level sensor
    gpio_num_t alarmGas     = GPIO_NUM_NC; // Vapour/gas sensor
    gpio_num_t npgLevel     = GPIO_NUM_NC; // NPG float-level sensor
    gpio_num_t pressureSensor = GPIO_NUM_NC; // MPX5010 pressure sensor

    json to_json() const
    {
        json j;
        j["triac"]          = (int)triac;
        j["pump"]           = (int)pump;
        j["mixer"]          = (int)mixer;
        j["alarmWater"]     = (int)alarmWater;
        j["alarmLevel"]     = (int)alarmLevel;
        j["alarmGas"]       = (int)alarmGas;
        j["npgLevel"]       = (int)npgLevel;
        j["pressureSensor"] = (int)pressureSensor;
        return j;
    }

    void from_json(const json &j)
    {
        auto gp = [&](const char *k, gpio_num_t &v){
            if (j.contains(k) && !j[k].is_null()) v = (gpio_num_t)j[k].get<int>();
        };
        gp("triac",         triac);
        gp("pump",          pump);
        gp("mixer",         mixer);
        gp("alarmWater",    alarmWater);
        gp("alarmLevel",    alarmLevel);
        gp("alarmGas",      alarmGas);
        gp("npgLevel",      npgLevel);
        gp("pressureSensor",pressureSensor);
    }
};

// ── Temperature sensor roles for distilling ───────────────────────────────
// Index into the readings array provided by BrewEngine via callback
#define DISTILL_TEMP_CUBE   0   // Still cube / boiler
#define DISTILL_TEMP_COLUMN 1   // Column / deflegmator (20 cm from packing)
#define DISTILL_TEMP_TSA    2   // Atmospheric connection tube (safety)

class DistillEngine
{
public:
    DistillEngine(SettingsManager *settingsManager);

    // Called once from BrewEngine::Init() after sensors are initialised
    void Init(bool invertOutputs);

    // BrewEngine calls this every time it has fresh temperature readings.
    // temps: tenths of a degree, indexed by DISTILL_TEMP_* constants.
    // Must be set before Start() is called.
    void UpdateTemperatures(const vector<float> &temps);

    // BrewEngine calls this to forward heater output % (0-100) so we can
    // also drive the triac when not using the main PID loop.
    void SetHeaterOutput(uint8_t pct);

    // JSON API – called from BrewEngine::processCommand()
    string processDistillCommand(const string &command, const json &data);

    // Exposed so BrewEngine::SaveSystemSettings can persist pin changes
    void savePins();
    void readPins();

    // Shared valve list – owned here, exposed to BrewEngine for GPIO init
    vector<Valve *> valves;

    // Status strings for the Data command
    string statusText   = "Idle";
    DistillMode  mode   = DISTILL_NONE;
    DistillState state  = DS_IDLE;
    bool running        = false;
    uint32_t elapsedSec = 0;  // seconds since start

    DistillPins     pins;
    DistillSettings settings;

private:
    SettingsManager *settingsManager;
    bool invertOutputs = false;
    uint8_t gpioHigh   = 1;
    uint8_t gpioLow    = 0;

    // Live temperature readings (tenths °C)
    vector<float> temps;
    uint8_t heaterOutputPct = 0;

    // Runtime variables (mirror original sketch globals)
    float   tempColumnPrev      = 0;
    int32_t secTempPrev         = 0;
    int32_t secStart            = 0;
    int32_t elapsedSecInternal  = 0;
    float   tempProductStab     = 0;
    uint8_t productPWMpct       = 0;
    int32_t timeRestab          = 0;

    // NBK pump
    uint8_t pumpSpeed = 0;

    // Valve helpers
    Valve *valveByRole(ValveRole role);
    void   openValve(ValveRole role);
    void   closeValve(ValveRole role);
    void   setValvePWM(ValveRole role, uint16_t openSec, uint16_t closeSec);
    void   closeAllValves();
    void   tickValvePWM();  // called every second by distillLoop

    // GPIO helpers
    void pinHigh(gpio_num_t pin);
    void pinLow(gpio_num_t pin);
    void initPin(gpio_num_t pin);

    // State machine runners
    void runSimpleDistill();
    void runHeadsCollection();
    void runRectification();
    void runDistillDefl();
    void runNDRF();
    void runNBK();
    void runTestValves();
    void safeStop();

    // Column stabilisation logic (shared by Rectification & NDRF)
    bool columnStabilised();

    // Product PWM auto-tune (returns new % value)
    uint8_t calcProductPWM();

    // Persistence (remaining private helpers)
    void readSettings();
    void saveSettings();
    void readValves();
    void saveValves(const json &jValves);
    void addDefaultValves();
    void initValveGPIO();

    // FreeRTOS task
    static void distillLoop(void *arg);
    TaskHandle_t loopHandle = NULL;

    void start(DistillMode m);
    void stop();
};

#endif /* _DistillEngine_H_ */
