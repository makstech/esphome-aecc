"""What the component can report, as schema defaults so that nothing has to be listed.

Pure data: the hub and the sensor platforms both read this, so it must not import either.
"""

from esphome.const import (
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_POWER,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_HERTZ,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_WATT,
)

_WATTS = dict(
    unit_of_measurement=UNIT_WATT,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_POWER,
    state_class=STATE_CLASS_MEASUREMENT,
)

# Sign conventions are the inverter's own: battery positive discharges, grid positive
# exports. `needs` is what has to be configured before the reading means anything.
PRESETS = {
    "soc": dict(
        address=0xFE06, signed=False, name="Battery SOC",
        unit_of_measurement=UNIT_PERCENT, accuracy_decimals=0,
        device_class=DEVICE_CLASS_BATTERY, state_class=STATE_CLASS_MEASUREMENT,
    ),
    "battery_power": dict(address=0xFE07, signed=True, name="Battery power", **_WATTS),
    "grid_power": dict(address=0xFE0A, signed=True, name="Grid power", **_WATTS),
    "backup_load": dict(address=0xFE10, signed=False, name="Backup load", **_WATTS),
    "setpoint": dict(address=0xFE16, signed=True, name="Setpoint", **_WATTS),
    "losses": dict(address=0xFE08, signed=False, name="Losses", **_WATTS),
}

# Not registers: what the control loop itself is doing. The meter is reached through
# the controller, so its readings need one too.
DIAGNOSTICS = {
    "meter_power": dict(metric="GRID_W", name="Meter grid power", interval="5s",
                        needs=("meter", "control"), **_WATTS),
    "meter_power_filtered": dict(
        metric="GRID_FILTERED_W", name="Meter grid power, filtered", interval="5s",
        needs=("meter", "control"), **_WATTS
    ),
    "commanded_power": dict(metric="COMMAND_W", name="Commanded power", interval="5s",
                            needs=("control",), **_WATTS),
    "meter_age": dict(
        metric="METER_AGE_S", name="Meter sample age", interval="5s",
        needs=("meter", "control"),
        unit_of_measurement=UNIT_SECOND, accuracy_decimals=2,
        device_class=DEVICE_CLASS_DURATION, state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    "loop_rate": dict(
        metric="LOOP_HZ", name="Control loop rate", interval="5s", needs=("control",),
        unit_of_measurement=UNIT_HERTZ, accuracy_decimals=1,
        state_class=STATE_CLASS_MEASUREMENT, entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}

# True when healthy, which is the inverse of every health-shaped device class Home
# Assistant offers, so none is set.
HEALTH = {
    "control_effective": dict(metric="CONTROL_EFFECTIVE", name="Setpoint honoured", needs=("control",)),
    "meter_ok": dict(metric="METER_OK", name="Meter OK", needs=("meter", "control")),
    "ems_ready": dict(metric="EMS_READY", name="Scheduler ready",
                      needs=("datalogger", "control")),
}
