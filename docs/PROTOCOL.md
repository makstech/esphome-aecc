# Protocol notes

For anyone writing their own tooling against these units. The component already handles
all of it; none of this is needed to use it.

## Modbus RTU, and its quirks

Slave 1 at 9600 8N1. Only function codes 3 and 6 exist — 1, 2 and 4 get no reply at any
address. A read returns at most 32 registers.

**Leave about 12 ms between frames.** Back to back, the unit answers only every other
request. The loss alternates cleanly, so it does not look like noise — it invents
plausible structure, and a scan will tell you things like "only even addresses are valid".

**Check the length of a reply, not just its CRC.** A truncated reply parses as real values
at shifted addresses, which is indistinguishable from good data.

**An invalid address returns an exception, not silence.** That makes it a definite answer:
do not retry it.

**Do not sweep the address space.** A long run of illegal-address reads can stop the
inverter responding at all, and it may not come back. Reads are enough to do it. Probe
addresses you have a reason to believe in.

## The setpoint only modulates a running command

`0xFE16` is the battery power setpoint, signed, obeyed in both directions to within
conversion loss. But it does nothing on its own: the energy manager has to already be
executing a non-zero command. With the schedule slot at zero the write is accepted, the
register reverts within a second or two, and the battery never moves.

Stop writing and the unit falls back to the slot after about three seconds. That makes the
slot, not the last command, the state an unattended unit runs — which is why the component
keeps it set to a small charging value.

## The energy manager

Not on Modbus. It lives behind newline-delimited JSON on TCP 8080 on the unit's own WiFi
module:

```json
{"Get":"Energycontrolparameters","RegControlAddr":[3000,3003],"SerialNumber":1,"CommandSource":"HA"}
```

It serves **one client at a time** — a Home Assistant integration polling the same unit
will starve everything else, silently. A malformed request produces the same silence, so
check your envelope before blaming contention.

Registers worth knowing:

| Register | Meaning |
|---|---|
| `3000` | Energy manager enable. At zero everything reads back correctly and nothing happens. |
| `3003`–`3018` | Sixteen schedule slots, CSV. Field four is a signed setpoint; negative charges. |
| `3020` | Schedule mode. 3 = AI, 6 = custom. |
| `3021` / `3022` | AI charge / discharge. Both zero for manual control. |
| `3026` / `3029` | Base discharge power and its enable. **Positive values export unconditionally.** |
| `3030` | Custom mode. |
| `3039` | Maximum feed power. |

Slot format:

```
enable,start,end,<signed W>,?,mode,?,?,?,maxSOC,minSOC
```

**The vendor app's AI mode rewrites these underneath you** — the slot, the SOC limits and
the bias. Anything holding a configuration has to reassert it, not set it once.

## The RJ45 carries more than one bus

On the unit this was mapped from, three independent RS485 buses share the connector: the
host port, and two BMS links. One of those is the live connection between the inverter and
the pack.

The bundled CT meter bridges each net across two pins, so a straight eight-wire patch
cable between it and the inverter shorts the two buses together. Break out only the two
conductors you need at your end.

Passive listening on the pack's link yields full per-cell telemetry for free, richer than
the JSON API exposes — but never transmit there while the inverter is polling.
