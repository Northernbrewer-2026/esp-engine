#ifndef _DistillSettings_H_
#define _DistillSettings_H_

/*
 * esp-brew-engine - Distilling Extension
 *
 * DistillSettings holds every tunable parameter from the original Arduino
 * sketch, mapped cleanly to NVS-persistable types.  The web API reads /
 * writes these via GetDistillSettings / SaveDistillSettings.
 *
 * Temperature values are stored as integers in tenths of a degree Celsius
 * (same convention as the original sketch, e.g. 964 = 96.4 °C) so they
 * survive NVS uint16 storage without loss.
 */

#include "nlohmann_json.hpp"
#include <cstdint>

using json = nlohmann::json;

struct DistillSettings
{
    // ── Power settings ────────────────────────────────────────────────────
    uint16_t powerTotalWatt        = 3000;  // Nominal TEN wattage
    uint16_t powerRectification    = 1000;  // Rectification power (W)
    uint16_t powerDistillSimple    = 3000;  // Simple distillation power (W)
    uint16_t powerHeadsDistill     = 800;   // Heads collection power (W)
    uint16_t powerNBK              = 2400;  // NBK pump-still power (W)

    // ── Temperature thresholds (tenths °C) ───────────────────────────────
    uint16_t tempEndRectAccelerate = 830;   // End of acceleration phase in rect.
    uint16_t tempEndHeadsColl      = 854;   // End of heads collection (85.4 °C)
    uint16_t tempEndProductColl    = 965;   // End of product collection (96.5 °C)
    uint16_t tempEndRectification  = 995;   // End of full rectification (99.5 °C)
    uint16_t tempSimpleDistill1    = 992;   // End temp: 1st simple distillation
    uint16_t tempSimpleDistill2    = 964;   // End temp: 2nd fractional
    uint16_t tempSimpleDistill3    = 954;   // End temp: 3rd fractional
    uint16_t tempDeflegmatorStart  = 700;   // Deflegmator: start water supply
    uint16_t tempDeflegmator       = 820;   // Deflegmator: target temperature
    uint16_t tempDeflegmatorEnd    = 985;   // Cube temp to end deflegmator run

    // ── PWM / timing for heads & product valves ───────────────────────────
    uint16_t pwmHeadsCycleHalfperiods = 2000; // Cycle length for heads PWM
    uint8_t  pwmHeadsPercent          = 5;    // % open for heads valve
    uint16_t pwmProductCycleHalfperiods = 1000; // Cycle length for product PWM
    uint8_t  pwmProductMinPercent     = 20;   // Minimum % open for product
    uint8_t  pwmProductStartPercent   = 40;   // Starting % open for product
    uint8_t  pwmProductDeltaTenths    = 3;    // Delta for stop/start (0.3 °C)

    // ── Deflegmator delta ─────────────────────────────────────────────────
    uint8_t  deflegmatorDeltaTenths   = 20;   // Delta around deflegmator target

    // ── Column stabilisation ──────────────────────────────────────────────
    int16_t  timeColumnStabiliseSec   = 900;  // positive=relative, negative=absolute
    uint16_t timeAutoIncProductPWMSec = 600;  // Auto-increment product PWM interval
    uint16_t timeColumnRestabiliseSec = 1800; // Re-stabilisation timeout

    // ── NBK pump ─────────────────────────────────────────────────────────
    uint8_t  speedNBK                 = 0;    // NBK pump speed 0-254

    json to_json() const
    {
        json j;
        j["powerTotalWatt"]              = powerTotalWatt;
        j["powerRectification"]          = powerRectification;
        j["powerDistillSimple"]          = powerDistillSimple;
        j["powerHeadsDistill"]           = powerHeadsDistill;
        j["powerNBK"]                    = powerNBK;
        j["tempEndRectAccelerate"]       = tempEndRectAccelerate;
        j["tempEndHeadsColl"]            = tempEndHeadsColl;
        j["tempEndProductColl"]          = tempEndProductColl;
        j["tempEndRectification"]        = tempEndRectification;
        j["tempSimpleDistill1"]          = tempSimpleDistill1;
        j["tempSimpleDistill2"]          = tempSimpleDistill2;
        j["tempSimpleDistill3"]          = tempSimpleDistill3;
        j["tempDeflegmatorStart"]        = tempDeflegmatorStart;
        j["tempDeflegmator"]             = tempDeflegmator;
        j["tempDeflegmatorEnd"]          = tempDeflegmatorEnd;
        j["pwmHeadsCycleHalfperiods"]    = pwmHeadsCycleHalfperiods;
        j["pwmHeadsPercent"]             = pwmHeadsPercent;
        j["pwmProductCycleHalfperiods"]  = pwmProductCycleHalfperiods;
        j["pwmProductMinPercent"]        = pwmProductMinPercent;
        j["pwmProductStartPercent"]      = pwmProductStartPercent;
        j["pwmProductDeltaTenths"]       = pwmProductDeltaTenths;
        j["deflegmatorDeltaTenths"]      = deflegmatorDeltaTenths;
        j["timeColumnStabiliseSec"]      = timeColumnStabiliseSec;
        j["timeAutoIncProductPWMSec"]    = timeAutoIncProductPWMSec;
        j["timeColumnRestabiliseSec"]    = timeColumnRestabiliseSec;
        j["speedNBK"]                    = speedNBK;
        return j;
    }

    void from_json(const json &j)
    {
        auto get = [&](const char *k, auto &v) {
            if (j.contains(k) && !j[k].is_null()) v = j[k];
        };
        get("powerTotalWatt",             powerTotalWatt);
        get("powerRectification",         powerRectification);
        get("powerDistillSimple",         powerDistillSimple);
        get("powerHeadsDistill",          powerHeadsDistill);
        get("powerNBK",                   powerNBK);
        get("tempEndRectAccelerate",      tempEndRectAccelerate);
        get("tempEndHeadsColl",           tempEndHeadsColl);
        get("tempEndProductColl",         tempEndProductColl);
        get("tempEndRectification",       tempEndRectification);
        get("tempSimpleDistill1",         tempSimpleDistill1);
        get("tempSimpleDistill2",         tempSimpleDistill2);
        get("tempSimpleDistill3",         tempSimpleDistill3);
        get("tempDeflegmatorStart",       tempDeflegmatorStart);
        get("tempDeflegmator",            tempDeflegmator);
        get("tempDeflegmatorEnd",         tempDeflegmatorEnd);
        get("pwmHeadsCycleHalfperiods",   pwmHeadsCycleHalfperiods);
        get("pwmHeadsPercent",            pwmHeadsPercent);
        get("pwmProductCycleHalfperiods", pwmProductCycleHalfperiods);
        get("pwmProductMinPercent",       pwmProductMinPercent);
        get("pwmProductStartPercent",     pwmProductStartPercent);
        get("pwmProductDeltaTenths",      pwmProductDeltaTenths);
        get("deflegmatorDeltaTenths",     deflegmatorDeltaTenths);
        get("timeColumnStabiliseSec",     timeColumnStabiliseSec);
        get("timeAutoIncProductPWMSec",   timeAutoIncProductPWMSec);
        get("timeColumnRestabiliseSec",   timeColumnRestabiliseSec);
        get("speedNBK",                   speedNBK);
    }
};

#endif /* _DistillSettings_H_ */
