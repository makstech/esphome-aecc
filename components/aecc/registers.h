#pragma once

#include <cstdint>

namespace esphome {
namespace aecc {

/// Inverter holding registers, slave 1 at 9600 8N1 on the RJ45's pins 7/8.
/// Names are the vendor app's own identifiers.
namespace reg {

// Control
constexpr uint16_t ON_OFF_CTRL = 0x9C40;

// Settings
constexpr uint16_t OUT_PRIORITY = 0xA028;       // maps to the app's Inverter Operating Mode, unconfirmed
constexpr uint16_t OUT_VOLT = 0xA029;           // x10 V
constexpr uint16_t OUT_FREQ = 0xA02A;           // x100 Hz
constexpr uint16_t LINE_RANGE = 0xA02B;         // 0 UPS, 1 APL, 2 GEN
constexpr uint16_t PARA_MODE = 0xA02C;
constexpr uint16_t ECO_EN = 0xA02D;
constexpr uint16_t CHG_PRIORITY = 0xA02E;
constexpr uint16_t MAX_CHG_CURR = 0xA02F;       // x10 A
constexpr uint16_t CHG_CURR_BY_PV = 0xA030;     // x10 A
constexpr uint16_t CHG_CURR_BY_LINE = 0xA031;   // x10 A
constexpr uint16_t MUTE_EN = 0xA033;
constexpr uint16_t ON_GRID_SET = 0xA034;        // 2 = anti-backflow via CT, 3 = via smart device
constexpr uint16_t CHG_FULL_CURR = 0xA035;      // x10 A
constexpr uint16_t NG_FUNC_EN = 0xA036;         // named for the N-E bond, but setting it
                                                // never produced a referenced island
constexpr uint16_t BATT_ACTIVE = 0xA03E;
constexpr uint16_t ON_GRID_POWER = 0xA03F;      // W - the app locks this one
/// A second copy of the on-grid cap. Output tops out at the lowest of this, 0xA03F
/// and EMS 3039, so raising one alone changes nothing.
constexpr uint16_t ON_GRID_POWER_MIRROR = 0x9ACE;
constexpr uint16_t HYBRID_PRIORITY_EN = 0xA041;
constexpr uint16_t GRID_STANDARD = 0xA043;      // enum indexes the app's list in display order
constexpr uint16_t EXT_CT_GET_HOST_EN = 0xA047;
constexpr uint16_t ISLAND_EN = 0xA069;
constexpr uint16_t BAT_TYPE = 0xA08C;           // 6 = LiFePO4 16S
constexpr uint16_t CV_VOLT = 0xA08D;            // x10 V
constexpr uint16_t FLOAT_VOLT = 0xA08E;         // x10 V
constexpr uint16_t CV_CHG_TIME = 0xA090;        // min
constexpr uint16_t CV_CHG_BACK_VOLT = 0xA091;   // x10 V
constexpr uint16_t INV_TO_LINE_VOLT = 0xA092;   // x10 V
constexpr uint16_t LINE_BACK_TO_INV_VOLT = 0xA093;  // x10 V
constexpr uint16_t BATT_LOW_VOLT = 0xA094;      // x10 V
constexpr uint16_t BATT_DELAY_OFF_VOLT = 0xA095;    // x10 V
constexpr uint16_t BATT_EOD_VOLT = 0xA096;      // x10 V
constexpr uint16_t BATT_DELAY_OFF_TIME = 0xA097;    // s
constexpr uint16_t BATT_EOD_BACK_VOLT = 0xA098;     // x10 V
constexpr uint16_t BMS_SET = 0xA099;            // 1 = enable 485-BMS
constexpr uint16_t BMS_PROTOCOL = 0xA09A;       // 1 = Pylon
constexpr uint16_t SOC_LOW_ALARM = 0xA09B;      // %
constexpr uint16_t SOC_SHUTDOWN = 0xA09C;       // %
constexpr uint16_t SOC_FULL = 0xA09D;           // %
constexpr uint16_t SOC_INV_TO_LINE = 0xA09E;    // %
constexpr uint16_t SOC_LINE_TO_INV = 0xA09F;    // %
constexpr uint16_t BATT_PACK_NOT_UNION = 0xA0AF;

// Live telemetry
constexpr uint16_t SOC = 0xFE06;                // %
constexpr uint16_t BATTERY_POWER = 0xFE07;      // W, signed: + discharging
constexpr uint16_t LOSSES = 0xFE08;             // W, standby + conversion
constexpr uint16_t GRID_POWER = 0xFE0A;         // W, signed: + export
constexpr uint16_t BACKUP_LOAD = 0xFE10;        // W

/// The battery power setpoint, signed, negative charges. Obeyed literally in both
/// directions to within conversion loss - but only while the energy manager is already
/// running a non-zero command. With EMS slot 3003 at zero this register is inert:
/// writes revert within ~1.5 s and the battery never moves.
constexpr uint16_t SETPOINT = 0xFE16;

struct Island {
  uint16_t lo;
  uint16_t hi;  // exclusive
};

/// Configuration only. The log island at 0xCF08 is 512 registers of history, not settings,
/// and the address space is known to be incompletely mapped - never probe outside these.
constexpr Island CONFIG_ISLANDS[] = {
    {0x9AC0, 0x9B14},
    {0x9C40, 0x9C4C},
    {0xA028, 0xA0FA},
};


}  // namespace reg

/// Bundled SMeter-RS071, a plain Modbus RTU slave 1 at 9600 8N1, IEEE-754 float32 with
/// big-endian word order. It reaches the inverter over a proprietary radio link, so a
/// wired tap on its RJ45 is a passive third party.
namespace meter_reg {
constexpr uint16_t VOLTAGE = 0;        // V
constexpr uint16_t ACTIVE_POWER = 12;  // W, positive = importing
constexpr uint16_t APPARENT_POWER = 18;
}  // namespace meter_reg

}  // namespace aecc
}  // namespace esphome
