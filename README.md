# 🔋 AECC Balcony Battery ESPHome Component

**Control an AECC-platform balcony battery from Home Assistant, locally, over RS485.**

These all-in-one batteries are sold under many names — Sunpura, Lunergy, AEG Solarcube,
Voltdeer, AFERIY, AccuMate, JET GreenARK, Oscal, Fossibot, Humsienk. Underneath they run
the same platform.

Normally you configure them through the vendor's phone app, which talks to their cloud.
This component talks to the battery directly instead.

## ✨ What you get

- **Live readings** in Home Assistant: state of charge, battery power, grid power, load
- **Settings you can change**, including ones the app asks for a password
- **Zero-export control** — hold your grid connection at a target, for places where
  feeding in is not allowed
- **A configuration backup** you can download and put back

## 🔌 Wiring

You need an ESP32 and one RS485 adapter per bus. A plain ESP32 is the safe choice: it has
three serial ports and you need two of them.

Use a normal patch cable into the battery, then break out only two conductors at your
end, with a keystone jack or by cutting the cable:

```
RJ45 pin 7  →  A
RJ45 pin 8  →  B
```

Leave the other six unconnected. The socket carries more than one RS485 bus, and one of
them is the live link between the inverter and its battery pack. So, make sure not to
short circuit them.

Power the adapters from **3.3 V**, not 5 V. Many of them pass their supply voltage
straight through to the TTL side, and a 5 V RXD output is above what an ESP32 pin is rated
for. If you are not sure, measure RXD against ground before connecting it: RS485 idles
high, so it will read either 3.3 V or 5 V and tell you.

> ⚠️ **Check the pinout in your own manual.** The pins above are from a Humsienk
> `HSBPSS2K5W7K68WHEU`. Other brands on this platform are probably the same, but a wrong
> guess here lands on the battery's own BMS link.

> ⚠️ **If you also wire a meter, isolate one of the two buses.** The meter and the battery
> are powered separately. Two plain adapters sharing the ESP32's ground tie their
> electrical references together, which RS485 will not tolerate for long.

## 🚀 Quick start

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/makstech/esphome-aecc
      ref: main

uart:
  - id: bus_inverter
    tx_pin: GPIO32
    rx_pin: GPIO33
    baud_rate: 9600

aecc:
  id: nova
  uart_id: bus_inverter

sensor:
  - platform: aecc
```

That gives you state of charge, battery power, grid power and backup load. Name any
sensor yourself and you get only the ones you name.

## 📊 What you can put in Home Assistant

**Readings** — `sensor:`

| Key | What it is |
|---|---|
| `soc` | State of charge, % |
| `battery_power` | Positive when discharging |
| `grid_power` | At the battery's own connection, positive when exporting |
| `backup_load` | Load on the battery's own socket |
| `setpoint` | What the battery has been told to do |
| `losses` | Standby and conversion losses |

**Zero-export diagnostics** — `sensor:`, only with `zero_export:` configured

| Key | What it is |
|---|---|
| `meter_power` | What your meter is reading right now |
| `meter_power_filtered` | The same after smoothing, which is what the loop acts on |
| `commanded_power` | What the loop is asking the battery for |
| `meter_age` | Seconds since the last good meter reading |
| `loop_rate` | How often the loop is actually running, Hz |

**Controls** — `number:`

| Key | What it does |
|---|---|
| `grid_target_w` | Watts to keep importing. Cannot go below zero unless you allow it |
| `max_discharge_w` | Most the battery may discharge |
| `max_charge_w` | Most the battery may charge |
| by `address:` | Any battery setting, see below |

**Health** — `binary_sensor:`

| Key | On means |
|---|---|
| `control_effective` | The battery is obeying the setpoint |
| `meter_ok` | The meter is answering with fresh readings |
| `ems_ready` | The battery's scheduler is set up for local control |

**Other** — a `button:` to take a backup, and the `aecc.write_register` action to write
any address.

Every address is listed in [`docs/REGISTERS.md`](docs/REGISTERS.md), so a setting without
a named entity is still reachable.

## ⚖️ Zero export

If feeding into the grid is not allowed where you live, give the component a meter and a
target. It charges the battery when you would otherwise export, and discharges it to cover
your load.

```yaml
aecc:
  id: nova
  uart_id: bus_inverter
  meter:
    type: rs071        # the CT meter that came in the box
    uart_id: bus_meter
  zero_export:
    grid_target: 60    # watts to keep importing
    max_discharge: 600
    max_charge: 2400
    min_soc: 15%
    max_soc: 90%
```

Set `max_discharge` near your usual household draw. If the load suddenly drops, the
battery takes about a second to wind down, and how much it pushes out during that second
is roughly the difference between this number and your baseline.

`grid_target` is how much you keep importing rather than sitting exactly on zero. It
cannot be negative unless you deliberately allow it, so an optimiser cannot ask the
battery to export by accident.

### Letting Home Assistant steer it

The target and the two limits appear as numbers in Home Assistant, so something like
EMHASS can change them by the hour:

| What you want | grid target | max discharge | max charge |
|---|---|---|---|
| Soak up your own solar | 0 | 0 | full |
| Charge from cheap grid at 2 kW | 2000 | 0 | full |
| Run the house off the battery | 0 | full | 0 |
| Save the battery for later | 0 | 0 | 0 |

## 🔧 Commissioning

The battery ignores the component until its own scheduler is set up, and the vendor app
undoes that whenever its AI mode is on. Give the component the battery's IP address and it
handles this for you, checking every minute and putting it back if it drifts:

```yaml
aecc:
  datalogger:
    host: 192.0.2.10     # the battery's own WiFi module, on your network
    resting_power: -300
```

### Why `resting_power` cannot be zero

The battery only accepts commands while its own scheduler is running a non-zero value.
Set that value to zero and it ignores the ESP32 completely.

That same value is also what the battery falls back to a few seconds after the ESP32 goes
quiet. So one number does two jobs, and zero breaks both of them.

A small charging value is the quietest non-zero option, and charging can never push
anything into the grid. If you want the battery to do as close to nothing as possible when
the ESP32 stops, use something small like `-10` rather than looking for a zero.

The `control_effective` sensor turns off if the battery stops obeying. Put it on a
dashboard or an automation to know when that happens.

## 💾 Backup and restore

Take a copy before you change anything on a new battery.

```yaml
web_server:
  version: 3

aecc:
  backup:
    url: /aecc/backup
```

```bash
curl http://your-node.local/aecc/backup -o backup.txt   # starts it
curl http://your-node.local/aecc/backup -o backup.txt   # again, for the file
```

Or press the **Back up configuration** button and fetch it afterwards. Putting it back:

```bash
curl --data-binary @backup.txt http://your-node.local/aecc/restore
curl -X POST -d '' http://your-node.local/aecc/restore     # for the report
```

Restore only writes settings, skips anything already correct, and stops if the battery
starts refusing. It does not switch the battery on, so on a completely blank unit that
last step is still yours.

## 📖 More

- [`docs/REGISTERS.md`](docs/REGISTERS.md) — every address the component knows about
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — for writing your own tooling

## ⚠️ Notes

Writing settings on a grid-connected battery can make it export, which needs permission in
many places. Know your local rules.

Not affiliated with any of the brands listed. Register names are the vendor app's own.
