# Walkthrough 2 — Fault injection: make the twin tell the truth

Cut a trace on a running board, pull a chip out, strap one to the wrong address —
and check the firmware tells you which readings are real. Then break the board
28 different ways in about 23 seconds, without rebuilding anything.

Every command below is copy-pasteable, and every block of output is real —
captured from an actual run of this repository.

**Time:** about 15 minutes.
**You need:** [Walkthrough 1](01-virtual-pcb.md) done, or at least its build
steps. No hardware.

---

## The question

A fuel gauge reads empty. Is the tank empty, or did the wire to the sender break?
The needle looks the same either way.

Firmware has the same problem. A channel reading `0 mV` might be a channel at
zero volts — or a trace that's been cut, a chip that's missing, a pin that's
floating. If nothing in the stack records the difference, your software acts on
a number that means nothing: a control loop pushes harder, a logger records a
clean zero forever, a test goes green.

This walkthrough breaks the virtual board on purpose and watches what the
firmware says about it.

---

## Step 1 — Build both halves

Same as Walkthrough 1. If you've already built, this is quick.

```bash
source ~/zephyrproject/.venv/bin/activate && export ZEPHYR_BASE=~/zephyrproject/zephyr && cd ~/code/zephyr-nucleo-h723zg-example
```

```bash
cmake -S vpcb -B build-vpcb && cmake --build build-vpcb && west build -b native_sim app -d build-vpcb-fw
```

## Step 2 — Give the board its own copy of the netlist

We're going to edit the wiring while everything runs. Work on a copy, so the
file in the repository stays intact:

```bash
cp vpcb/netlists/adc_loopback.txt /tmp/board.netlist
```

Start the board on the copy, in its own terminal:

```bash
./build-vpcb/vpcb-board --sock /tmp/vpcb.sock --netlist /tmp/board.netlist
```

Then the two chips:

```bash
./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x48 -q &
```

```bash
./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x4c -q &
```

And the firmware, in a third terminal:

```bash
./build-vpcb-fw/zephyr/zephyr.exe --vpcb-sock=/tmp/vpcb.sock
```

## Step 3 — Drive three neighbouring channels

```
dacset 2 1500
dacset 3 1500
dacset 4 1500
adcregs
```

```
dacset ch2 -> dacx578@48 ch2 code=1861 OK
dacset ch3 -> dacx578@48 ch3 code=1861 OK
dacset ch4 -> dacx578@48 ch4 code=1861 OK
    ...
    ch[2]: 1498 mV
    ch[3]: 1498 mV
    ch[4]: 1498 mV
```

Channels 2 and 4 are about to be our control group. They prove the chip, the bus
and the firmware are healthy, so whatever happens to channel 3 can only be the
copper.

## Step 4 — Cut a trace, live

Delete the one line that wires U1's channel 3 to board net 3, then tell the
board to re-read its netlist:

```bash
sed -i '/^0x48 3 3$/d' /tmp/board.netlist && pkill -HUP -x vpcb-board
```

The board takes the new wiring immediately — no restart:

```
[board 1226986775.049] netlist reloaded from /tmp/board.netlist (14 links)
```

And the firmware notices on its next sample:

```
W: net 3 unreadable (-61)
```

Now do exactly what worked a moment ago:

```
dacset 3 1500
```

```
dacset ch3 -> dacx578@48 ch3 code=1861 OK
```

**It reports OK.** And it's right to: the write reached the chip and the chip
drove its pin. The board log shows where the signal went:

```
NET_SET dac7578@48 ch3 -> not in netlist, dropped
```

Read the channels back:

```
adcregs
```

```
    ch[2]: 1498 mV
    ch[3]: ---
    ch[4]: 1498 mV
```

`---` is not a number. It's the firmware saying *there is no reading here* —
instead of the `0 mV` it would once have printed, which looked exactly like a
channel genuinely sitting at zero volts.

`-61` is `-ENODATA`: the net exists, but nothing is driving it. It's a different
fault from the `-5` you met in Walkthrough 1, where nothing answered on the bus.

## Step 5 — Restore the trace

```bash
cp vpcb/netlists/adc_loopback.txt /tmp/board.netlist && pkill -HUP -x vpcb-board
```

```
I: net 3 readable again
```

```
adcregs
```

```
    ch[2]: 1498 mV
    ch[3]: 1498 mV
    ch[4]: 1498 mV
```

Back to 1498 mV without re-sending anything. The chip never stopped driving its
pin — only the copper between it and the ADC was missing. The board works out
every net from whatever is wired to it right now, so reconnecting the trace
reconnects the voltage.

## Step 6 — Where the truth used to die

Before this change, the answer to "is channel 3 real?" was destroyed in three
separate places. All three had to change, bottom up:

| Layer | File | Used to | Now |
|---|---|---|---|
| Board model | `vpcb/board/main.c` | Reported a floating net as `0 µV`, status OK | Reports `VPCB_UNDRIVEN` and invents no voltage |
| ADC backend | `app/targets/*/adc_backend.c` | Logged a failed channel, wrote `0`, returned success | Returns a per-channel validity flag |
| Register file | `app/src/regs.h` | Had `mv[]`, `seq`, timestamp — nowhere to record it | Has `valid[]` beside `mv[]`, and `adcregs` prints `---` |

Fixing any one layer alone wouldn't have been enough. The board could report the
truth, and the backend would throw it away; the backend could keep it, and the
register file had nowhere to put it.

## Step 7 — Pull a chip

Drive a channel on U1, then kill the chip:

```
dacset 0 2000
```

```bash
pkill -f 'vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x48'
```

The board releases everything that chip was driving:

```
[board 1226992578.017] peer dac7578@48 disconnected (its address now NAKs)
[board 1226992578.037]   net0 released - dac7578@48 no longer drives it
...
[board 1226992578.046]   net7 released - dac7578@48 no longer drives it
```

And the firmware notices on its own:

```
W: net 0 unreadable (-61)
W: net 1 unreadable (-61)
...
W: net 7 unreadable (-61)
E: addr 0x48 NAK - no device on the virtual bus
W: dacx578@48 not answering (-5) - its channels read ---
```

```
adcregs
```

```
    ch[0]: ---
    ch[1]: ---
    ...
    ch[7]: ---
    ch[8]: 0 mV
```

All eight of U1's channels go to `---`. Channel 8 belongs to U2, which is still
fitted and still driving 0 V, so it reads a real 0 mV. Same number on the
screen, completely different meaning — and now you can tell them apart.

Two things put U1's channels at `---`: the board says their nets float, and the
firmware's own check says U1 stopped answering. On a real board only the second
can happen, and it's enough.

Put the chip back:

```bash
./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x48 -q &
```

```
I: net 0 readable again
...
I: net 7 readable again
I: dacx578@48 answering again
```

```
    ch[0]: 0 mV
    ch[1]: 0 mV
    ...
```

Valid again — at **0 mV**, not 2000. A DAC7578 powers up at zero-scale, and a
chip that has just been fitted has just been powered. The model behaves the way
the part does.

## Step 8 — Strap a chip to the wrong address

The DAC7578 picks its I²C address from how two pins are strapped — the table is
in [docs/hardware.md](../docs/hardware.md#addresses). Get the strap wrong and the
chip answers somewhere else. Swap U2 for one strapped to `0x4d` instead of
`0x4c`:

```bash
pkill -f 'vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x4c'; ./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x4d -q &
```

The strap moves the chip's *address*, not its *pins* — it is still soldered to
the same traces. So the netlist has to follow it:

```bash
sed -i 's/^0x4c/0x4d/' /tmp/board.netlist && pkill -HUP -x vpcb-board
```

```
W: dacx578@4c not answering (-5) - its channels read ---
I: net 8 readable again
...
I: net 14 readable again
```

The nets are driven again — by the chip at `0x4d`. But nothing answers at
`0x4c`, where the firmware expects U2. Now write to U2 the way the firmware
expects to:

```
dacset 8 1500
```

```
dacset ch8: write to dacx578@4c FAILED (-5) - nothing answered on the bus
```

```
adcregs
```

```
    ch[8]: ---
    ch[9]: ---
    ...
    ch[14]: ---
```

The pins really are at 0 V: the chip is fitted, powered and driving them at
zero-scale. But the firmware drives them through `0x4c`, and nothing answers
there, so it can't vouch for them and says so. A real board behaves exactly the
same way: `-5` on the write, `---` on the channels.

Put everything back before moving on:

```bash
pkill -f vpcb-dac7578; ./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x48 -q & ./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x4c -q &
```

```bash
cp vpcb/netlists/adc_loopback.txt /tmp/board.netlist && pkill -HUP -x vpcb-board
```

---

## Step 9 — 28 faults, one firmware, no rebuilds

Everything above, plus more, is automated. The fault matrix starts its own stack
and breaks it 28 different ways, putting the board back after each one:

```bash
PYTHONPATH=tests/integration pytest tests/integration/test_faults.py --config=tests/integration/configs/vpcb.yaml -v
```

```
test_faults.py::TestFaultMatrix::test_cut_trace[ch00-twin-only] PASSED
test_faults.py::TestFaultMatrix::test_cut_trace[ch01-twin-only] PASSED
...
test_faults.py::TestFaultMatrix::test_swapped_wires[ch07-ch08-bench] PASSED
test_faults.py::TestFaultMatrix::test_chip_missing[0x48-bench] PASSED
test_faults.py::TestFaultMatrix::test_wrong_strap[0x4c-as-0x4d-bench] PASSED
test_faults.py::TestFaultMatrix::test_part_at_datasheet_limit[gain-ppm1500-bench] PASSED
test_faults.py::TestFaultMatrix::test_intermittent_chip PASSED
test_faults.py::TestFaultMatrix::test_intermittent_trace PASSED

============================= 28 passed in 22.46s ==============================
```

| Cases | Fault | Could real hardware detect it? |
|---:|---|---|
| 15 | One trace cut, on each channel | **No** — `twin-only` |
| 3 | Two wires crossed (one pair crosses between chips) | **Yes** — by driving distinct values |
| 2 | A chip pulled | **Yes** — `-5` on the write, `---` on its channels |
| 2 | A chip strapped to the wrong address | **Yes**, identically |
| 4 | A DAC at each of its datasheet offset and gain limits | **Yes** — real parts land in here |
| 2 | A chip dropping out; a trace opening and closing | **Yes** / no |

Every case enforces the same rule: **`adcregs` shows the right number or `---`,
never a confident wrong number.** The one exception is crossed wires — each
reading is a real voltage, just on the wrong channel — and that's caught the only
way it can be, by giving every channel a different value and checking where each
one lands.

The datasheet cases aren't faults at all. The DAC7578's datasheet allows ±4 mV of
offset and ±0.15 % of gain error, and the chip model can be told to be exactly
that bad (`--offset-uv`, `--gain-ppm`). Those cases check your test tolerances
can absorb a part you're allowed to be shipped.

Doing this on the bench would mean physically breaking a board 28 different
ways — including cutting traces you'd never get back.

## Step 10 — What the twin can't promise

Look at that last column again. The cases marked `twin-only` pass here **because
the virtual board knows something real copper doesn't**: that a net is floating.

On a real board, a cut trace leaves the ADC pin floating, and the ADC converts
whatever it happens to pick up — some number, with nothing to say it's
meaningless. No firmware change can fix that from the ADC side, because the
information doesn't exist there.

So here's what carries over to hardware, and what doesn't:

- **Carries over:** `-5` and `---` for a missing, mis-strapped or dropped-out
  chip (the firmware's own check decides that, so copper gives the same answer),
  crossed wires caught by distinct values, datasheet error inside your
  tolerances, and every layer of the firmware passing along "not a real reading"
  when a read genuinely fails.
- **Twin-only:** knowing a net is floating. On the bench you'd need extra
  hardware for that — a pull resistor that drags a floating pin to a known rail,
  or a test that checks a channel moves when you expect it to.
  [Walkthrough 4](04-real-hardware.md) does the second on a real board.

That's the honest boundary of a digital twin. It's worth more than a mock because
it can be broken the way hardware breaks — and worth stating plainly where it
sees more than the bench can.

## Cleanup

```bash
pkill -f 'vpcb-board|vpcb-dac7578'; rm -f /tmp/vpcb.sock /tmp/board.netlist
```

---

## What to take away

**A reading you can't trust is worse than no reading.** Software acts on numbers.
`---` stops it acting on one that means nothing.

**Information dies in layers.** The board, the backend and the register file each
threw the truth away on their own. Fixing one wasn't enough.

**We didn't add a feature.** Every layer already had the information. We stopped
throwing it away.

## Where to go next

- [`tests/integration/test_faults.py`](../tests/integration/test_faults.py) —
  every case, with what it proves and whether the bench could see it
- [docs/hardware.md](../docs/hardware.md) — the physical rig, and the address
  strap table the wrong-strap case is about
- [Walkthrough 1](01-virtual-pcb.md) — how the virtual PCB is put together
