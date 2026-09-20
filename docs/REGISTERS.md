# Register map

Everything the component knows about, so you can reach a setting it does not model.

Read any of these by adding it to the `sensor:` platform by address, and write one with
the `aecc.write_register` action:

```yaml
sensor:
  - platform: aecc
    registers:
      - name: Grid standard
        address: 0xA043
        interval: 60s
```

Names are the vendor app's own identifiers. Verified on a Humsienk
`HSBPSS2K5W7K68WHEU`; other units on this platform use the same firmware but enum values
and defaults can differ between builds, so read a value back before trusting it.

## Live telemetry

| Address | Meaning | Notes |
|---|---|---|
| `0xFE06` | State of charge | % |
| `0xFE07` | Battery power | signed; positive discharging |
| `0xFE08` | Standby and conversion loss | W |
| `0xFE0A` | Grid power at the inverter's own port | signed; positive exporting |
| `0xFE10` | Backup (EPS) load | W |
| `0xFE16` | Active power setpoint | signed; positive discharging |

## Battery

| Address | Meaning | Scale |
|---|---|---|
| `0xA08C` | Battery type | 6 = LiFePO4 16S |
| `0xA08D` | Constant voltage | ×10 V |
| `0xA08E` | Float voltage | ×10 V |
| `0xA090` | Constant voltage charging time | minutes |
| `0xA091` | Return-to-CV voltage | ×10 V |
| `0xA092` | Battery → mains switchover voltage | ×10 V |
| `0xA093` | Mains → battery switchover voltage | ×10 V |
| `0xA094` | Undervoltage alarm | ×10 V |
| `0xA095` | Low-voltage shutdown | ×10 V |
| `0xA096` | End-of-discharge voltage | ×10 V |
| `0xA097` | Voltage shutdown delay | seconds |
| `0xA098` | End-of-discharge clear voltage | ×10 V |
| `0xA03E` | Battery activation | 0/1 |
| `0xA0AF` | Independent pack enable | 0/1 |

## Charging

| Address | Meaning | Scale |
|---|---|---|
| `0xA02E` | Charge priority | enum |
| `0xA02F` | Max charge current, stand-alone | ×10 A |
| `0xA030` | Max charge current from PV | ×10 A |
| `0xA031` | Max charge current from mains | ×10 A |
| `0xA035` | Saturation current | ×10 A |

## State of charge limits

| Address | Meaning |
|---|---|
| `0xA09B` | Low alarm, % |
| `0xA09C` | Shutdown point, % |
| `0xA09D` | Full judgement, % |
| `0xA09E` | Inverter → mains, % |
| `0xA09F` | Mains → inverter, % |

## Grid and output

| Address | Meaning | Notes |
|---|---|---|
| `0x9C40` | Device on/off | 0/1 |
| `0xA028` | Inverter operating mode | enum |
| `0xA029` | Output voltage | ×10 V |
| `0xA02A` | Output frequency | ×100 Hz |
| `0xA02B` | Utility range | 0 = UPS, 1 = APL, 2 = GEN |
| `0xA02C` | Parallel mode | 0 = stand-alone |
| `0xA02D` | Energy saving mode | 0/1 |
| `0xA033` | Buzzer mute | 0/1 |
| `0xA034` | On-grid mode | 1 = PV to grid, 2 = anti-backflow via CT |
| `0xA036` | Neutral–earth bond | 0/1, see below |
| `0xA03F` | Grid-tie power cap | W — **the app locks this**, see below |
| `0x9ACE` | Grid-tie power cap, second copy | W |
| `0xA041` | Mixing priority | 0/1 |
| `0xA043` | Grid standard | a preset; see below |
| `0xA047` | Take grid reading from the EMS host | 0/1 |
| `0xA069` | Anti-islanding | 0/1 |
| `0xA04D`–`0xA0F9` | Grid code trip thresholds | voltage ×10, frequency ×100 |

## The bundled CT meter

A separate Modbus device on its own bus. Slave 1, 9600 8N1, IEEE-754 float32 with
big-endian word order, so each value spans two registers.

| Address | Meaning |
|---|---|
| `0` | Line voltage, V |
| `12` | Active power, W — positive is importing |
| `18` | Apparent power, VA |

## The grid standard is a preset, not the limits

`0xA043` selects one of the firmware's built-in grid profiles. The numbering follows a
list that differs between firmware builds, so the value that means EN50549 on one unit may
not on another. On the unit this was mapped from, `5` was EN50549.

You do not need to identify it. Each profile is just a set of trip thresholds, and those
live at `0xA04D`-`0xA0F9` and are individually readable and writable. If your grid
operator requires particular voltage or frequency limits, set them directly and read them
back rather than hunting for the right preset number.

## Two things that surprise people

**The grid-tie cap lives in three places.** `0xA03F`, its second copy at `0x9ACE`, and the
energy manager's own limit. Output tops out at the lowest of them, so raising one and
seeing no change is the expected result rather than a fault.

**`0xA036` is named for the neutral–earth bond but may not be it.** Enabling it never
produced a referenced island on the unit this was mapped from. Treat the name as a label,
not a promise.

## Not on Modbus

The energy manager's own registers — the schedule slots, the enable flag, the SOC window —
are only reachable over the unit's JSON API. The component handles those itself when you
give it a `datalogger:` block.
