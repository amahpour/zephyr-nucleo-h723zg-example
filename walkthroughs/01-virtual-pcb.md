# Walkthrough 1 — Run the virtual PCB

Build a board out of Linux processes, watch firmware drive a DAC across a socket
and read the voltage back on its own ADC, then unplug a chip mid-session and
watch the driver report it.

Every command below is copy-pasteable, and every block of output is real — it
was captured from an actual run of this repository, not transcribed by hand.

**Time:** about 20 minutes, most of it the first Zephyr build.
**You need:** Linux (or WSL), and the Zephyr toolchain from the
[README prerequisites](../README.md#prerequisites). No hardware at all.

---

## What you're about to build

Most "simulated hardware" is a function call. Something in the firmware asks for
a voltage, and something else in the same binary hands one back. That proves the
code above the driver works, and nothing else.

Here the peripherals are separate programs:

| Process | What it is |
|---|---|
| `zephyr.exe` | Your firmware, compiled for `native_sim`. A normal Linux executable. |
| `vpcb-board` | The PCB. Owns the netlist, routes I²C by address, holds net voltages. |
| `vpcb-dac7578` ×2 | One process per DAC chip, at I²C addresses `0x48` and `0x4c`. |

They talk over a Unix domain socket. The firmware's I²C transfers genuinely leave
its address space, which means the DACx578 driver, the Zephyr DAC API and the
`dacset` command all run exactly as they do on the real board — the netlist file
is standing in for copper.

---

## Step 1 — Set up your shell

Every later step assumes these three things. Run it once per terminal.

```bash
source ~/zephyrproject/.venv/bin/activate && export ZEPHYR_BASE=~/zephyrproject/zephyr && cd ~/code/zephyr-nucleo-h723zg-example
```

Adjust the last path if you cloned somewhere else.

## Step 2 — Build the board and the chips

These are plain C programs with no Zephyr involvement. They build in seconds.

```bash
cmake -S vpcb -B build-vpcb && cmake --build build-vpcb
```

You get three executables:

```
build-vpcb/vpcb-board
build-vpcb/vpcb-dac7578
build-vpcb/vpcb-fake-mcu
```

`vpcb-fake-mcu` is a test client that lets you poke the board without building
firmware. We won't need it here.

## Step 3 — Build the firmware

Same application source as the Nucleo build, different board target.

```bash
west build -b native_sim app -d build-vpcb-fw --pristine
```

The result, `build-vpcb-fw/zephyr/zephyr.exe`, is an ordinary x86 Linux binary.
You can run it under `gdb`, `strace`, `perf` or Valgrind like anything else.

## Step 4 — Look at the netlist

This file is the circuit. It is the entire schematic of the test rig.

```bash
cat vpcb/netlists/adc_loopback.txt
```

```
# Virtual PCB netlist:  <ic_i2c_addr> <ic_channel> <board_net>
# The board net index is what the MCU's ADC samples.
# Two DAC7578s give 16 channels; the app uses 15.
0x48 0 0
0x48 1 1
...
0x4c 6 14
```

Each line is one wire: *this DAC channel is connected to this board net.* The
MCU's ADC samples nets by index, so line `0x48 0 0` means "U1 channel 0 drives
the net that ADC channel 0 reads." That's the loopback.

The physical rig in [docs/hardware.md](../docs/hardware.md) is wired to match,
one line to one jumper.

## Step 5 — Start the board

The board owns the netlist and the socket, so it has to be listening before
anything else starts. Give it its own terminal — it prints a live bus trace, and
that trace is half the fun.

```bash
./build-vpcb/vpcb-board --sock /tmp/vpcb.sock --netlist vpcb/netlists/adc_loopback.txt
```

```
[board 598011120.479] netlist loaded from .../vpcb/netlists/adc_loopback.txt (15 links)
[board 598011120.518] listening on /tmp/vpcb.sock
```

## Step 6 — Plug in the chips

Two more terminals, or append `&` to background them. Each one is a DAC7578
answering at its strapped address.

```bash
./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x48 &
```

```bash
./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x4c &
```

The board terminal reacts as each part powers up and drives its outputs to zero:

```
[board 598011221.112] HELLO role=IC addr=0x48 name=dac7578@48 v1
[board 598011221.176] NET_SET dac7578@48 ch0 -> net0 = 0 uV
[board 598011221.178] NET_SET dac7578@48 ch1 -> net1 = 0 uV
...
[board 598011221.306] HELLO role=IC addr=0x4C name=dac7578@4C v1
...
[board 598011221.469] NET_SET dac7578@4C ch7 -> not in netlist, dropped
```

That last line is worth a pause. U2 channel 7 exists on the chip but has no wire
in the netlist, so the board drops the write — exactly like a real DAC output
with no trace leaving the pad. The model doesn't know or care that it's
unconnected; the board does, because the board is the copper.

Confirm your board is three separate programs:

```bash
ps -o pid,rss,comm -C vpcb-board -C vpcb-dac7578
```

## Step 7 — Boot the firmware and close the loop

```bash
./build-vpcb-fw/zephyr/zephyr.exe --vpcb-sock=/tmp/vpcb.sock
```

```
i2c_vpcb: attached to virtual PCB at /tmp/vpcb.sock
I: attached to virtual PCB at /tmp/vpcb.sock
I: DACx578 at 0x48, 12-bit
I: DACx578 at 0x4c, 12-bit
*** Booting Zephyr OS build v4.4.2 ***
I: ADC Sampler application started
I: ADC backend (VPCB) initialized with 15 channels
ADC Sampler ready. Type 'help' for available commands.
I: Sampling thread started (period=100 ms)

uart:~$
```

That's the Zephyr shell, on your terminal's stdin. Drive channel 0 to 2 V:

```
dacset 0 2000
```

```
dacset ch0 -> dacx578@48 ch0 code=2481 OK
```

Now look at the board terminal, which saw the whole transaction:

```
[board 598013724.373] I2C addr=0x48 wlen=3 rlen=0 -> dac7578@48
[board 598013724.390]   W [3]: 30 9B 10
[board 598013724.545] NET_SET dac7578@48 ch0 -> net0 = 1999340 uV
[board 598013724.552]   reply status=OK rlen=0
```

Three bytes — `30 9B 10` — are the real DAC7578 command: write-and-update
channel 0, then the 12-bit code `0x9B1` left-justified in 16 bits. That is the
same byte sequence an oscilloscope would capture on the physical rig.

Read it back through the ADC:

```
adcregs
```

```
ADC Register File:
  seq:       37
  timestamp: 3960 ms
  channels:
    ch[0]: 1998 mV
    ch[1]: 0 mV
    ...
```

**1998 mV, not 2000.** Nothing is lying to you: 2000 mV became DAC code 2481,
which is 1999340 µV on the net, which the 12-bit ADC quantised to 1998. The two
millivolts you lost are the two conversions a real signal chain would cost you.
A mocked ADC would have handed back exactly 2000.

Try the other chip, to see the address routing pick a different part:

```
dacset 8 1500
```

```
[board 598990066.983] I2C addr=0x4C wlen=3 rlen=0 -> dac7578@4C
[board 598990066.009]   W [3]: 30 74 50
[board 598990066.075] NET_SET dac7578@4C ch0 -> net8 = 1499706 uV
dacset ch8 -> dacx578@4c ch0 code=1861 OK
```

Board channel 8 is U2's channel 0. The firmware knows which chip owns which
channel; the netlist knows which net each one lands on.

---

## Step 8 — Unplug a chip

This is the part that a function-call mock cannot do.

Leave the firmware running. In another terminal, kill the DAC at `0x48`:

```bash
pkill -f 'vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x48'
```

The board notices immediately:

```
[board 597984959.492] peer dac7578@48 disconnected (its address now NAKs)
```

Now ask the firmware to do the exact same thing that worked a minute ago:

```
dacset 0 2000
```

```
[board 597985565.896] I2C addr=0x48 wlen=3 rlen=0 -> NAK (no IC at this address)
E: addr 0x48 NAK - no device on the virtual bus
dacset ch0: write to dacx578@48 FAILED (-19) - device did not ACK
```

`-19` is `-ENODEV`, and it arrived by the honest route: the driver issued a real
transfer, nothing acknowledged the address, and the error propagated up through
the Zephyr I²C API. There was no rebuild, no `--inject-failure` flag, no mock
object and no `#ifdef`. A process stopped existing.

On the bench this is an unpopulated footprint, a cold solder joint, a part held
in reset or a chip strapped to the wrong address. The firmware cannot tell the
difference, which is the whole point.

Two more things worth checking while it's broken:

```
adcregs
```

Channel 0 still reads `1998 mV`. The DAC is gone, but its last output is still
sitting on the net — the board holds net state, not the chip. Charge on a trace
doesn't vanish because a part died.

```
dacset 8 1500
```

Still `OK`. Only the address that lost its process NAKs. The rest of the bus is
unaffected, exactly as it would be on real copper.

## Step 9 — Wedge the board itself

There are two very different failures hiding here, and the firmware
distinguishes them. Freeze the board process — don't kill it, just stop it
responding:

```bash
pkill -STOP -f vpcb-board
```

```
dacset 8 1000
```

```
E: no reply from the virtual PCB within 1000 ms of host time (net 0) - board process not responding
W: net 0 unreadable (-5)
```

`-5` is `-EIO`, and the message names the board process, not the bus. That
distinction is deliberate. A frozen simulator is *your test rig breaking*, not a
device misbehaving, and reporting it as an I²C timeout would send you debugging
firmware that is working fine. Errors from a test harness should never be able
to impersonate errors from the thing under test.

Let it go again:

```bash
pkill -CONT -f vpcb-board
```

---

## Step 10 — Run the real test suite against it

Everything above was manual. The integration suite does the whole dance for you —
it starts the board, the chips and the firmware, runs the tests, and tears the
constellation down.

```bash
cd tests/integration && python -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt
```

```bash
cd ~/code/zephyr-nucleo-h723zg-example && PYTHONPATH=tests/integration pytest tests/integration/ --config=tests/integration/configs/vpcb.yaml -v
```

Now open the two config files side by side:

```bash
diff tests/integration/configs/vpcb.yaml tests/integration/configs/physical.yaml
```

The `instrument:` block is identical — `type: dac` in both. Only the `dut:`
block differs: processes and a socket on one side, `/dev/ttyACM0` on the other.
The test code doesn't branch on which one it's talking to, because `dacset` on
the virtual PCB and `dacset` on the Nucleo run the same firmware through the
same driver.

That's the payoff. These tests run on every pull request with no hardware in the
loop, and when you do wire up the board, the passing suite means something.

```bash
PYTHONPATH=tests/integration pytest tests/integration/ --config=tests/integration/configs/physical.yaml -v
```

---

## Step 11 — It's just a Linux program

Because the firmware is a native executable, host tooling works on it directly.
Build it under AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
west build -b native_sim app -d build-asan --pristine -- -DCONFIG_ASAN=y -DCONFIG_UBSAN=y
```

The tree is clean, so to see them fire you have to break something on purpose.
In [`app/src/regs.c:31`](../app/src/regs.c#L31), change `i < NUM_CH` to
`i <= NUM_CH`, rebuild, and run it. Both sanitizers catch the same line, each
seeing a different half of the bug:

- **UBSAN** flags the write: `index 15 out of bounds for type 'int32_t [15]'`.
  It lands on the next member of the struct, which ASAN can't see — it's the
  same allocation.
- **ASAN** flags the read: a stack buffer overflow past the sampling thread's
  `samples[]` array.

The stack trace walks straight from `regs_update` through the Zephyr thread
entry into the native simulator. Undo it when you're done:

```bash
git checkout app/src/regs.c
```

Coverage works the same way:

```bash
west build -b native_sim app -d build-cov --pristine -- -DCONFIG_COVERAGE=y -DCONFIG_COVERAGE_DUMP=y
```

## Cleanup

```bash
pkill -f 'vpcb-board|vpcb-dac7578'; rm -f /tmp/vpcb.sock
```

---

## What to take away

**Something outside the firmware has to own the connections.** Firmware bugs are
the ones everyone tests for. The part at the address nobody talks to is the kind
that eats a bring-up week, and it only shows up when the wiring lives somewhere
the firmware cannot see — here, a netlist file held by a separate process.

**Absence has to be modelled, not flagged.** A missing chip that you simulate
with a config option is a chip that's still there. A missing chip that's a
process you killed produces a NAK because there is genuinely nothing on the
other end of the socket.

**A broken harness must not look like a broken device.** `-19` means the part
didn't answer. `-5` means the simulator didn't. Collapsing those into one error
code costs somebody a day.

## Where to go next

- [docs/architecture.md](../docs/architecture.md) — how the firmware's target
  layer selects a backend at build time
- [docs/hardware.md](../docs/hardware.md) — the physical loopback rig this
  netlist mirrors, and its wiring
- `vpcb/include/vpcb/proto.h` — the wire protocol, shared verbatim by the
  firmware and the models
