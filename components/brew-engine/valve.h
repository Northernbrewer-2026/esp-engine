#ifndef _Valve_H_
#define _Valve_H_

/*
 * esp-brew-engine - Distilling Extension
 * Valve output: shared output object used by both brewing and distilling.
 * Each valve has a PWM-style open/close cycle (counts of zero-crossings or
 * timer ticks on ESP32, implemented as on/off seconds here for simplicity).
 */

#include "nlohmann_json.hpp"
#include "driver/gpio.h"

using namespace std;
using json = nlohmann::json;

// Which distilling modes this valve participates in
enum ValveRole
{
    VALVE_ROLE_NONE       = 0,
    VALVE_ROLE_WATER      = 1,  // Main water supply (KLP_VODA)
    VALVE_ROLE_DEFLEGM    = 2,  // Deflegmator (KLP_DEFL)
    VALVE_ROLE_HEADS      = 3,  // Heads / tails output (KLP_GLV_HVS)
    VALVE_ROLE_PRODUCT    = 4,  // Spirit / product output (KLP_SR)
    VALVE_ROLE_COOLER     = 5,  // Cooler / condenser (KLP_HLD)
    VALVE_ROLE_NPG        = 6,  // NPG (flooded-column protection) (KLP_NPG)
    VALVE_ROLE_DRAIN      = 7,  // Drain / stillage (KLP_BARDA)
};

class Valve
{
public:
    uint8_t    id;
    string     name;
    gpio_num_t pinNr;
    ValveRole  role;          // logical role in distilling
    bool       activeHigh;    // true = HIGH = open
    bool       useForBrewing; // make valve available in brewing mode too

    // runtime PWM state (seconds-based, updated by distillLoop)
    uint16_t pwmOpenSec  = 0;   // seconds to stay open per cycle
    uint16_t pwmCloseSec = 10;  // seconds to stay closed per cycle
    uint16_t pwmCounter  = 0;
    bool     isOpen      = false;

    json to_json() const
    {
        json j;
        j["id"]           = this->id;
        j["name"]         = this->name;
        j["pinNr"]        = (int)this->pinNr;
        j["role"]         = (int)this->role;
        j["activeHigh"]   = this->activeHigh;
        j["useForBrewing"]= this->useForBrewing;
        return j;
    }

    void from_json(const json &j)
    {
        this->id           = j["id"].get<uint8_t>();
        this->name         = j["name"].get<string>();
        this->pinNr        = (gpio_num_t)j["pinNr"].get<int>();
        this->role         = (ValveRole)j.value("role", (int)VALVE_ROLE_NONE);
        this->activeHigh   = j.value("activeHigh", true);
        this->useForBrewing= j.value("useForBrewing", false);
        this->pwmOpenSec   = 0;
        this->pwmCloseSec  = 10;
        this->pwmCounter   = 0;
        this->isOpen       = false;
    }
};

#endif /* _Valve_H_ */
