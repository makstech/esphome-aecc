# 🔋 AECC Balcony Battery ESPHome Component

**Control an AECC-platform balcony battery from Home Assistant, locally, over RS485.**

These all-in-one batteries are sold under many names — Sunpura, Lunergy, AEG Solarcube,
Voltdeer, AFERIY, AccuMate, JET GreenARK, Oscal, Fossibot, Humsienk. Underneath they run
the same platform.

Normally you configure them through the vendor's phone app, which talks to their cloud.
This component talks to the battery directly instead.

## ✨ What you get

- **Live readings** in Home Assistant with no configuration: state of charge, battery
  power, grid power, load
- **Settings you can change**, including ones the app asks for a password
- **Zero-export control** — hold your grid connection at a target, for places where
  feeding in is not allowed
- **A configuration backup** you can download and put back

## 🔌 Wiring

You need an ESP32 and one RS485 adapter for the inverter. Add a second adapter only if you
want zero-export control, which needs a meter on its own bus. A plain ESP32 is the safe
choice: it has three serial ports.

Buy **auto-direction** adapters, the kind with only VCC, GND, TXD and RXD. The component
has no transmit-enable pin, so a breakout that brings out DE/RE will not transmit.

Use a normal patch cable into the battery, then break out only two conductors at your
end, with a keystone jack or by cutting the cable:

```
RJ45 pin 7  →  A
RJ45 pin 8  →  B
```

Do not connect the other six. The socket carries more than one RS485 bus, and one of them
is the live link between the inverter and its battery pack.

Power the adapters from **3.3 V**, not 5 V. Many of them pass their supply voltage
straight through to the TTL side, and a 5 V RXD output is above what an ESP32 pin is rated
for. If you are not sure, measure RXD against ground before connecting it: RS485 idles
high, so it will read either 3.3 V or 5 V and tell you.

> ⚠️ **Check the pinout in your own manual.** The pins above are from a Humsienk
> `HSBPSS2K5W7K68WHEU`. Other brands on this platform are probably the same, but a wrong
> guess here lands on the battery's own BMS link.

The bundled CT meter is a second bus, on its own adapter:

```
RJ45 pin 2 or 4  →  A
RJ45 pin 3 or 5  →  B
```

It bridges each net across two pins internally, which is why a straight patch cable
between the meter and the battery shorts the two together.

> ⚠️ **If you wire a meter, isolate one of the two buses.** The meter and the battery
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
```

That is the whole configuration. It gives you the battery's readings, and its settings
once you name the ones you want.

## 📊 What you get in Home Assistant

These appear on their own. A reading is created when the thing it describes is
configured, so a setup without a meter has no meter readings.

**Readings**

| Key | Name | What it is |
|---|---|---|
| `soc_sensor` | Battery SOC | State of charge, % |
| `battery_power_sensor` | Battery power | Positive when discharging |
| `grid_power_sensor` | Grid power | At the battery's own connection, positive when exporting |
| `backup_load_sensor` | Backup load | Load on the battery's own socket |
| `setpoint_sensor` | Setpoint | What the battery has been told to do |
| `losses_sensor` | Losses | Standby and conversion losses |

**Control diagnostics** — with `control:`, and the meter ones also with `meter:`

| Key | Name | What it is |
|---|---|---|
| `meter_power_sensor` | Meter grid power | What your meter is reading right now |
| `meter_power_filtered_sensor` | Meter grid power, filtered | The same after smoothing, which is what the loop acts on |
| `commanded_power_sensor` | Commanded power | What the loop is asking the battery for |
| `meter_age_sensor` | Meter sample age | Seconds since the last good meter reading |
| `loop_rate_sensor` | Control loop rate | How often the loop is actually running, Hz |

**Health**

| Key | Name | On means | Needs |
|---|---|---|---|
| `control_effective_sensor` | Setpoint honoured | The battery is obeying the setpoint | `control:` |
| `meter_ok_sensor` | Meter OK | The meter is answering with fresh readings | `meter:` + `control:` |
| `ems_ready_sensor` | Scheduler ready | The battery's scheduler is set up for local control | `datalogger:` + `control:` |

Setpoint honoured and Meter OK both go off while the mode is Off.

To rename or reconfigure one, name its key under `aecc:`:

```yaml
aecc:
  soc_sensor: Charge level
  meter_age_sensor:
    name: Meter age
    interval: 30s
```

[`tests/all-keys.yaml`](tests/all-keys.yaml) is generated from the component and names
every key it accepts, so it is the exhaustive list if this one has drifted.

**Battery settings** — also under `aecc:`, see [below](#-battery-settings)

**Other** — a **Back up configuration** button appears with `backup:`, and the
`aecc.write_register` action writes any address.

## ⚖️ Modes

A `control:` block gives you a **Battery mode** dropdown in Home Assistant, and it
remembers your choice across reboots:

| Mode | What it does |
|---|---|
| Off | Nothing. The battery answers to the vendor app, the cloud, or anything else on the bus |
| Zero export | Holds your grid connection at a target, charging rather than exporting |
| Manual | Holds the battery at a power you set, positive discharging |

Off is the default, and it is genuinely off: the component stops reasserting the battery's
own scheduler too, so it will not fight you while you drive the battery some other way.
Zero export only appears in the list when you have given the component a `meter:`.

### Handing the battery back

Off stops the component writing, but it leaves the battery's own scheduler wherever the
component last put it — parked, doing nothing. A `datalogger:` block therefore also gives
you a **Work mode** dropdown, for handing the battery back to its built-in
self-consumption automation. Name it with `work_mode_select:` to change the label.

| Battery mode | Work mode | What runs the battery |
|---|---|---|
| Off | Self-consumption | The battery's own automation |
| Off | Custom | Nothing; it idles on the resting slot |
| Zero export or Manual | Custom | This component |

Self-consumption can only be selected while the battery mode is Off, because the other two
modes need Custom and put it back. Note that the battery's automation rewrites the
schedule slot and the SOC limits to suit itself, and it is free to export — so it is not a
substitute for zero export where feeding in is not allowed.

```yaml
uart:
  - id: bus_inverter
    tx_pin: GPIO32
    rx_pin: GPIO33
    baud_rate: 9600
  - id: bus_meter          # only needed for Zero export
    tx_pin: GPIO25
    rx_pin: GPIO26
    baud_rate: 9600

aecc:
  id: nova
  uart_id: bus_inverter
  meter:
    type: rs071        # the CT meter that came in the box
    uart_id: bus_meter
  datalogger:
    host: 192.0.2.10   # the battery's own WiFi module, on your network
  control:
    setpoint_number: Battery power   # the Manual target; negative charges
    grid_target: 60    # watts to keep importing, in Zero export
    max_discharge: 600
    max_charge: 2400
    min_soc: 15%
    max_soc: 90%
```

`min_soc` and `max_soc` apply in Manual as well, so a bad setpoint cannot discharge past
your reserve, and so do `max_discharge` and `max_charge` — a Manual setpoint beyond them
is held at the limit and logged.

The `datalogger:` block is not optional in practice. Without it the battery ignores the
control loop and keeps running whatever the vendor app last scheduled, while every
control entity still appears in Home Assistant. [Commissioning](#-commissioning)
explains why.

Picking a mode is what starts any of this; a freshly flashed node sits idle.

Set `max_discharge` near your usual household draw. If the load suddenly drops, the
battery takes about a second to wind down, and how much it pushes out during that second
is roughly the difference between this number and your baseline.

`grid_target` is how much you keep importing rather than sitting exactly on zero. The
Home Assistant control stops at zero, so an optimiser cannot ask the battery to export by
accident. Give `grid_target_number` a negative `min_value` where exporting is wanted and
permitted.

### Letting Home Assistant steer it

Name the loop's own parameters and they become controls too, starting from the values you
set above. The mode dropdown is there whether you name it or not; naming it only changes
the label:

```yaml
  control:
    grid_target: 60
    max_discharge: 600
    mode_select: Battery mode
    grid_target_number: Grid target
    max_discharge_number: Max discharge
    max_charge_number: Max charge
    min_soc_number: Reserve
    max_soc_number: Charge ceiling
```

These remember their last value across a reboot, so something like EMHASS can change them
by the hour:

| What you want | grid target | max discharge | max charge |
|---|---|---|---|
| Soak up your own solar | 0 | 0 | full |
| Charge from cheap grid at 2 kW | 2000 | 0 | full |
| Run the house off the battery | 0 | full | 0 |
| Save the battery for later | 0 | 0 | 0 |

## 🧰 Battery settings

Name a setting inside `aecc:` and it becomes a control in Home Assistant. That is the
whole configuration — the register, the range, the step and the unit are already known:

```yaml
aecc:
  id: nova
  uart_id: bus_inverter
  max_charge_current_number: Max charge current
  soc_full_number: Full at
  buzzer_mute_switch: Buzzer mute
  on_grid_mode_select: On-grid mode
```

Any of those defaults can be replaced by writing the longer form instead of a name:

```yaml
  soc_shutdown_number:
    name: Shut down at
    min_value: 5
    max_value: 50
    interval: 5min
```

The suffix says what kind of control you get: `_number` for a value, `_switch` for an
on/off setting, `_select` for a list of choices.

| Suffix | Available for |
|---|---|
| `_number` | `grid_export_limit`, `output_voltage`, `output_frequency`, `max_charge_current`, `pv_max_charge_current`, `mains_max_charge_current`, `saturation_current`, `cv_voltage`, `float_voltage`, `cv_charge_time`, `cv_return_voltage`, `battery_to_mains_voltage`, `mains_to_battery_voltage`, `undervoltage_alarm`, `low_voltage_shutdown`, `eod_voltage`, `shutdown_delay`, `eod_clear_voltage`, `soc_low_alarm`, `soc_shutdown`, `soc_full`, `soc_inverter_to_mains`, `soc_mains_to_inverter` |
| `_switch` | `device_power`, `eco_mode`, `buzzer_mute`, `battery_activation`, `mixing_priority`, `external_ct_host`, `anti_islanding`, `bms_function`, `independent_pack` |
| `_select` | `utility_range`, `on_grid_mode`, `grid_standard`, `inverter_mode`, `charge_priority`, `battery_type`, `parallel_mode`, `bms_protocol` |

Some do more than they sound like. `device_power` switches the inverter off.
`anti_islanding` off stops it disconnecting from a dead grid. `bms_function` off drops the
pack's BMS link. `grid_export_limit` caps on-grid *output*, which includes power serving
your own load, so setting it to zero stops the battery supplying the house rather than
stopping export. And `grid_standard` swaps the inverter's whole set of grid voltage
and frequency trip limits in one write.

These are the battery's own settings, so they are read back from it at boot rather than
remembered here — change one in the vendor app and the entity follows.

A few settings have no key on purpose — the enums whose value lists were never worked out,
where a bare index would be a worse control than none. Those and anything else are still
reachable by address, and [`docs/REGISTERS.md`](docs/REGISTERS.md) lists every register
with the key that reaches it, where there is one:

```yaml
  registers:
    - name: Parallel mode
      address: 0xA02C
      max_value: 7
```

Give it a `name:` for it to appear in Home Assistant.

## 🔧 Commissioning

The battery ignores the component until its own scheduler is set up, and the vendor app
undoes that whenever its AI mode is on. Give the component the battery's address and it
handles this for you, checking every minute and putting it back if it drifts:

```yaml
aecc:
  datalogger:
    host: 192.0.2.10     # the battery's own WiFi module, on your network
    resting_power: -300
    resting_power_number: Resting power   # optional, to change it from Home Assistant
```

`host:` also takes a hostname or an mDNS name, so a battery on DHCP does not need a
reservation:

```yaml
  datalogger:
    host: humsienk-control.local
```

A **Datalogger address** text and a **Datalogger** switch come with the block. The first
changes the address without reflashing; the second stops the component using the
datalogger, for when the vendor app or another integration needs it. Name them with
`host_text:` and `enable_switch:` to change the labels.

`host:` can be left out entirely, which is the point of the text entity — flash the node,
then type the address into Home Assistant once you know it:

```yaml
aecc:
  id: nova
  uart_id: bus_inverter
  datalogger:
```

### Why `resting_power` cannot be zero

The battery only accepts commands while its own scheduler is running a non-zero value.
Set that value to zero and it ignores the ESP32 completely.

That same value is also what the battery falls back to a few seconds after the ESP32 goes
quiet. So one number does two jobs, and zero breaks both of them.

A small charging value is the quietest non-zero option, and charging can never push
anything into the grid. If you want the battery to do as close to nothing as possible when
the ESP32 stops, use something small like `-10` rather than looking for a zero.

The **Setpoint honoured** sensor turns off if the battery stops obeying. Put it on a
dashboard or an automation to know when that happens. It reports on the control loop, so
it needs `control:` as well.

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
curl http://your-node.local/aecc/backup                 # starts it
sleep 60
curl http://your-node.local/aecc/backup -o backup.txt   # again, for the file
```

The first call only starts the walk; at 9600 baud it takes the best part of a minute.

Or press the **Back up configuration** button and fetch it afterwards. Putting it back:

```bash
curl --data-binary @backup.txt http://your-node.local/aecc/restore
curl -X POST -d '' http://your-node.local/aecc/restore     # for the report
```

Restore writes the settings and the energy manager's own values, skips anything already
correct, and stops if the battery starts refusing. It does not switch the battery on, so
on a completely blank unit that last step is still yours.

## 📖 More

- [`docs/REGISTERS.md`](docs/REGISTERS.md) — every address the component knows about
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — for writing your own tooling

## ⚠️ Notes

Writing settings on a grid-connected battery can make it export, which needs permission in
many places. Know your local rules.

Not affiliated with any of the brands listed.
