"""The battery's own settings, as schema defaults so that naming one is enough."""

from esphome.const import (
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_VOLTAGE,
    UNIT_AMPERE,
    UNIT_HERTZ,
    UNIT_MINUTE,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_VOLT,
    UNIT_WATT,
)

# scale multiplies the register, the same way the sensor platform's does: 552 at scale
# 0.1 is 55.2 V.
_V = dict(unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE, scale=0.1, step=0.1)
_A = dict(unit_of_measurement=UNIT_AMPERE, device_class=DEVICE_CLASS_CURRENT, scale=0.1, step=0.5)
_PCT = dict(unit_of_measurement=UNIT_PERCENT, scale=1, step=1, min_value=0, max_value=100)
# Pack voltages are all within a cell chemistry's window; a wider range invites a typo
# that a 16S LiFePO4 pack will not survive.
_PACK_V = dict(_V, min_value=40.0, max_value=60.0)

NUMBERS = {
    "grid_export_limit": dict(
        address=0xA03F, min_value=0, max_value=15000, step=100, scale=1,
        unit_of_measurement=UNIT_WATT,
        # Output tops out at the lowest copy, and the vendor app reads this one.
        mirror=0x9ACE,
    ),
    "output_voltage": dict(address=0xA029, min_value=200.0, max_value=250.0, step=1.0, **{k: v for k, v in _V.items() if k != "step"}),
    "output_frequency": dict(
        address=0xA02A, min_value=45.0, max_value=65.0, step=0.1, scale=0.01,
        unit_of_measurement=UNIT_HERTZ,
    ),
    "max_charge_current": dict(address=0xA02F, min_value=0, max_value=100.0, **_A),
    "pv_max_charge_current": dict(address=0xA030, min_value=0, max_value=100.0, **_A),
    # Reads 35.0 A where the app shows 50 A, so the mapping is unconfirmed.
    "mains_max_charge_current": dict(address=0xA031, min_value=0, max_value=100.0, **_A),
    "saturation_current": dict(address=0xA035, min_value=0, max_value=50.0, **_A),
    "cv_voltage": dict(address=0xA08D, **_PACK_V),
    "float_voltage": dict(address=0xA08E, **_PACK_V),
    "cv_charge_time": dict(
        address=0xA090, min_value=0, max_value=600, step=5, scale=1,
        unit_of_measurement=UNIT_MINUTE,
    ),
    "cv_return_voltage": dict(address=0xA091, **_PACK_V),
    "battery_to_mains_voltage": dict(address=0xA092, **_PACK_V),
    "mains_to_battery_voltage": dict(address=0xA093, **_PACK_V),
    "undervoltage_alarm": dict(address=0xA094, **_PACK_V),
    "low_voltage_shutdown": dict(address=0xA095, **_PACK_V),
    "eod_voltage": dict(address=0xA096, **_PACK_V),
    "shutdown_delay": dict(
        address=0xA097, min_value=0, max_value=300, step=1, scale=1,
        unit_of_measurement=UNIT_SECOND,
    ),
    "eod_clear_voltage": dict(address=0xA098, **_PACK_V),
    "soc_low_alarm": dict(address=0xA09B, **_PCT),
    "soc_shutdown": dict(address=0xA09C, **_PCT),
    "soc_full": dict(address=0xA09D, **_PCT),
    "soc_inverter_to_mains": dict(address=0xA09E, **_PCT),
    "soc_mains_to_inverter": dict(address=0xA09F, **_PCT),
}

# Some registers have no key on purpose - unconfirmed mappings and enums whose value
# lists were never established. docs/REGISTERS.md says which and why.
SWITCHES = {
    "device_power": 0x9C40,
    "eco_mode": 0xA02D,
    "buzzer_mute": 0xA033,
    "battery_activation": 0xA03E,
    "mixing_priority": 0xA041,
    "external_ct_host": 0xA047,
    "anti_islanding": 0xA069,
    "bms_function": 0xA099,
    "independent_pack": 0xA0AF,
}

# Only where the value list is actually known.
# Labels are the app's own, so the two agree when read side by side.
SELECTS = {
    "utility_range": dict(
        address=0xA02B,
        options={0: "UPS mode", 1: "APL mode", 2: "GEN mode"},
    ),
    "inverter_mode": dict(
        address=0xA028,
        options={
            0: "Photovoltaic priority (PV)",
            1: "Utility power priority (GID)",
            2: "PV Battery Priority (BAT)",
            3: "Hybrid Priority (HBD)",
        },
    ),
    "battery_type": dict(
        address=0xA08C,
        options={
            0: "Custom Type",
            1: "SLD-Sealed Lead Acid",
            2: "FLD-Flooded Lead Acid",
            3: "GEL-Gel Lead Acid",
            4: "LiFePO4 14S",
            5: "LiFePO4 15S",
            6: "LiFePO4 16S",
            7: "LiFePO4 7S",
            8: "LiFePO4 8S",
            9: "LiFePO4 9S",
            10: "Ternary Lithium 7S",
            11: "Ternary Lithium 8S",
            12: "Ternary Lithium 13S",
            13: "Ternary Lithium 14S",
        },
    ),
    "parallel_mode": dict(
        address=0xA02C,
        options={
            0: "Stand-alone",
            1: "Parallel operation",
            2: "Split phase reference phase",
            3: "Split phase with 120° difference from reference phase",
            4: "Phase with 180° difference between split phase and reference phase",
            5: "First of three phases",
            6: "Second of three phases",
            7: "Third phase of three phases",
        },
    ),
    "bms_protocol": dict(
        address=0xA09A,
        options={
            0: "Voltronic", 1: "Pylon", 2: "Aoguan", 3: "Oulite", 4: "Gotion",
            5: "Sunwoda", 6: "CF", 7: "Dyuness", 8: "Pace", 9: "BST",
            10: "Foxess", 11: "AEC", 12: "Pylon-V3.5", 13: "MeZic-V3.5",
            14: "Tentek", 15: "Rvtran", 16: "YUZE", 17: "EVT",
        },
    ),
    "charge_priority": dict(
        address=0xA02E,
        options={
            0: "Photovoltaic priority (OSO)",
            1: "Utility Priority (OUO)",
            2: "Mixed mode (SNU)",
            3: "PV only (NUC)",
        },
    ),
    "grid_standard": dict(
        address=0xA043,
        options={
            0: "GNL",
            1: "VDE4105",
            2: "IEEE1547",
            3: "CEI-021",
            4: "VDE0126",
            5: "EN50549 (General)",
            6: "EN50549-SE (Switzerland)",
            7: "EN50549-DK1 (Denmark)",
            8: "SI4777 (Israel)",
            9: "TOR-Z1 (Austria)",
            10: "EN50549-PL (Poland)",
        },
    ),
    "on_grid_mode": dict(
        address=0xA034,
        options={
            0: "PV to load",
            1: "PV to grid",
            2: "Prevention of power flow into the grid (external CT sensor)",
            3: "Power flow prevention (external smart device)",
        },
    ),
}
