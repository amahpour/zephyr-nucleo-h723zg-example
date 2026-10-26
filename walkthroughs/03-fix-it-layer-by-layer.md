# Walkthrough 3 — Fix the board that lies, one layer at a time

Go back to before the fix. Cut a trace and watch the firmware report a
confident 0 mV with no warning at all. Then apply the real fix one commit at a
time and watch what changes at each step — until a broken channel finally
says it's broken.

Every file path, line number and code snippet below is taken from the actual
commits, and every block of output was captured from a real run of each stage.

**Time:** about 20 minutes.
**You need:** [Walkthrough 1](01-virtual-pcb.md)'s toolchain. No hardware.
[Walkthrough 2](02-fault-injection.md) shows the finished result; this one shows
how it got there.

---

## Step 1 — Go back to before the fix

The repository has a tag marking the moment just before the fix. Make your own
branch from it:

```bash
source ~/zephyrproject/.venv/bin/activate && export ZEPHYR_BASE=~/zephyrproject/zephyr && cd ~/code/zephyr-nucleo-h723zg-example
```

```bash
git checkout -b fix-it-myself before-fault-model
```

Build both halves:

```bash
cmake -S vpcb -B build-vpcb && cmake --build build-vpcb
```

```bash
west build -b native_sim app -d build-vpcb-fw --pristine
```

Make a copy of the wiring file with one wire removed — U1 channel 3 no longer
reaches ADC channel 3. That's a cut trace:

```bash
grep -v '^0x48 3 3$' vpcb/netlists/adc_loopback.txt > /tmp/cut_trace.txt
```

---

## Step 2 — See the problem

Start the board on the cut wiring, the two chips, and the firmware — three
terminals:

```bash
./build-vpcb/vpcb-board --sock /tmp/vpcb.sock --netlist /tmp/cut_trace.txt
```

```bash
./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x48 -q & ./build-vpcb/vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x4c -q &
```

```bash
./build-vpcb-fw/zephyr/zephyr.exe --vpcb-sock=/tmp/vpcb.sock
```

Drive three neighbouring channels and read them back:

```
dacset 2 1500
dacset 3 1500
dacset 4 1500
adcregs
```

```
dacset ch3 -> dacx578@48 ch3 code=1861 OK
    ch[2]: 1498 mV
    ch[3]: 0 mV
    ch[4]: 1498 mV
```

**The write to channel 3 said OK. Channel 3 reads zero. And there is not one
warning anywhere in the session.** Channels 2 and 4 prove the chip, the bus and
the firmware are healthy — only the wire is broken, and nothing says so.

Is channel 3 at zero volts, or is it broken? You can't tell. Neither can the
firmware.

### A second symptom: the reading that outlives its chip

Stop everything (`Ctrl-C` the firmware, then
`pkill -f 'vpcb-board|vpcb-dac7578'`), and restart the three processes on the
**intact** wiring, `vpcb/netlists/adc_loopback.txt`. Then:

```
dacset 0 2000
```

Remove the chip at `0x48`:

```bash
pkill -f 'vpcb-dac7578 --sock /tmp/vpcb.sock --addr 0x48'
```

```
adcregs
```

```
    ch[0]: 1998 mV
```

The chip is gone. Its old reading isn't.

### It isn't a polling problem

**`app/src/main.c`** — lines 44–53

```c
    while (1) {
        ret = adc_backend_sample_all(samples);
        if (ret == 0) {
            regs_update(samples);
        } else {
            LOG_ERR("ADC sample failed: %d", ret);
        }

        k_msleep(SAMPLE_PERIOD_MS);
    }
```

The firmware already checks every channel ten times a second. What's missing
isn't polling — it's what happens when a check fails.

---

## Step 3 — Find where the information dies

The answer to *"is this reading real?"* exists at the bottom of the stack. It's
thrown away three separate times on the way up.

### Layer 1 — the board

**`vpcb/board/main.c`** — lines 299–310

```c
case VPCB_MSG_NET_GET: {
    struct vpcb_net_get *g = (struct vpcb_net_get *)pl;
    struct vpcb_net_value v = { .net = g->net, .microvolts = 0,
                                .status = VPCB_ENONET };
    if (g->net < MAX_NETS && net_driven[g->net]) {
        v.microvolts = nets[g->net];
        v.status = VPCB_OK;
    } else if (g->net < MAX_NETS) {
        /* Undriven net: floating. Model as 0 V for now. */
        v.microvolts = 0;
        v.status = VPCB_OK;
    }
```

Asked about a wire connected to nothing, the board answers **"0 volts, status
OK."** Note the *"for now"* — a shortcut that was never revisited.

### Layer 2 — reading the sensor

**`app/drivers/i2c_vpcb.c`** — lines 162–163

```c
if (st < 0)  { return -EIO; }
if (st != VPCB_ST_OK) { return -ENODEV; }
```

Every kind of failure becomes the same "no device" error.

**`app/targets/vpcb/adc_backend.c`** — lines 47–58

```c
ret = vpcb_net_read_mv(chan, &mv);
if (ret != 0) {
    /* Net unreadable - report 0 V but say so, loudly. Once per change
     * of state, not once per sample: a board that is down for a few
     * seconds would otherwise print every channel ten times a second. */
    if (chan < NUM_CH && !net_down[chan]) {
        LOG_WRN("net %u unreadable (%d)", chan, ret);
        net_down[chan] = true;
    }
    *result = 0;
    return 0;
}
```

**`app/targets/hw/adc_backend.c`** — lines 166–170. This isn't a simulation
quirk — the real hardware build does exactly the same thing:

```c
ret = adc_read(*channel_mappings[i].dev, &sequence);
if (ret < 0) {
    LOG_ERR("ADC read failed for channel %d (ADC channel %d): %d",
            i, channel_cfgs[i].channel_id, ret);
    out_mv[i] = 0;
```

Log it, write a zero, carry on as if nothing happened.

### Layer 3 — where readings are stored

**`app/src/regs.h`** — lines 25–29

```c
struct adc_regs {
    int32_t mv[NUM_CH];             /* Latest mV per channel */
    uint32_t seq;                   /* Sequence number, increments each update */
    int64_t last_sample_uptime_ms;  /* Timestamp of last update (k_uptime_get()) */
};
```

There's no field for "is this real?" Even if every layer below told the truth,
there would be nowhere to put it.

---

## Step 4 — Fix layer 1: the board tells the truth

Each fix is the real commit from `main`. Apply it to your branch:

```bash
git cherry-pick d0efde4
```

```bash
cmake --build build-vpcb
```

**`vpcb/include/vpcb/proto.h`** — line 44. A new answer the board can give:

```c
VPCB_UNDRIVEN = 5, /* net exists but nothing drives it: floating */
```

**`vpcb/board/main.c`** — lines 321–325

```c
} else if (g->net < MAX_NETS) {
    /* Undriven net: floating. There is no voltage to
     * report, so say that instead of inventing 0 V. */
    v.status = VPCB_UNDRIVEN;
}
```

**`vpcb/board/main.c`** — lines 145–154. A chip that leaves stops driving its
wires:

```c
static void release_nets(struct peer *p)
{
    for (int i = 0; i < n_links; i++) {
        if (links[i].addr == p->addr && links[i].net < MAX_NETS &&
            net_driven[links[i].net]) {
            net_driven[links[i].net] = 0;
            logd("  net%u released - %s no longer drives it", links[i].net, p->name);
        }
    }
}
```

Restart the three processes on `/tmp/cut_trace.txt`, then `dacset 3 1500` and
`adcregs`:

```
W: net 3 unreadable (-19)
    ch[3]: 0 mV
```

**The truth arrives — and gets thrown away.** The firmware even prints a
warning, then shows zero anyway.

The `-19` is wrong too: it means "no device," which is layer 2's catch-all from
Step 3. That's next.

---

## Step 5 — Fix layer 2: the sensor code keeps the answer

```bash
git cherry-pick 7f2a6d6
```

```bash
west build -b native_sim app -d build-vpcb-fw
```

**`app/src/adc_backend.h`** — line 37. Every reading now comes with a flag
saying whether it's real:

```c
int adc_backend_sample_all(int32_t out_mv[NUM_CH], bool out_valid[NUM_CH]);
```

**`app/drivers/i2c_vpcb.c`** — lines 164–167. A floating wire gets its own
error:

```c
/* Floating is not the same fault as "not in the netlist": the net
 * exists, nothing is driving it. */
if (st == VPCB_ST_UNDRIVEN) { return -ENODATA; }
if (st != VPCB_ST_OK) { return -ENODEV; }
```

**`app/targets/vpcb/adc_backend.c`** — lines 119–122

```c
    out_mv[i] = 0;
    out_valid[i] = false;
} else {
    out_valid[i] = (net_err[i] == 0);
```

**`app/targets/hw/adc_backend.c`** — lines 170–173. The same fix on real
hardware:

```c
    out_mv[i] = 0;
    out_valid[i] = false;
} else {
    out_valid[i] = true;
```

**`app/src/main.c`** — lines 46–50. The flag arrives here — and stops:

```c
ret = adc_backend_sample_all(samples, valid);
if (ret == 0) {
    /* valid[] has nowhere to go yet: struct adc_regs has no field
     * for it. */
    regs_update(samples);
```

Restart and try again:

```
W: net 3 unreadable (-61)
    ch[3]: 0 mV
```

`-61` means "nothing drives this wire" — the right error now. **The firmware
knows. It has nowhere to put it.**

---

## Step 6 — Fix layer 3: somewhere to put the truth

```bash
git cherry-pick c407a2f
```

```bash
west build -b native_sim app -d build-vpcb-fw
```

**`app/src/regs.h`** — lines 26–31

```c
struct adc_regs {
    int32_t mv[NUM_CH];             /* Latest mV per channel */
    bool valid[NUM_CH];             /* Is mv[i] a real reading? */
    uint32_t seq;                   /* Sequence number, increments each update */
    int64_t last_sample_uptime_ms;  /* Timestamp of last update (k_uptime_get()) */
};
```

**`app/src/main.c`** — lines 46–48

```c
ret = adc_backend_sample_all(samples, valid);
if (ret == 0) {
    regs_update(samples, valid);
```

**`app/src/cmd_read_regs.c`** — lines 24–31

```c
for (int i = 0; i < NUM_CH; i++) {
    if (snapshot.valid[i]) {
        shell_print(sh, "    ch[%d]: %d mV", i, snapshot.mv[i]);
    } else {
        /* Not a number: there is no reading to show. */
        shell_print(sh, "    ch[%d]: ---", i);
    }
}
```

Restart and try again:

```
    ch[2]: 1498 mV
    ch[3]: ---
    ch[4]: 1498 mV
```

**Not zero. Broken. And now you can tell.**

### The reading that outlived its chip

Same build, intact wiring. `dacset 0 2000`, remove the `0x48` chip, `adcregs`:

```
    ch[0]: ---
    ch[1]: ---
    ...
    ch[7]: ---
    ch[8]: 1498 mV
```

All eight of that chip's channels say they have no reading. The other chip's
channels are untouched. Start the chip again and its channels come back at
**0 mV** — a DAC7578 powers up at zero.

---

## Step 7 — Break it 28 ways, rebuild nothing

```bash
git cherry-pick dc1da2a 406eeb1
```

```bash
cmake --build build-vpcb
```

**`vpcb/board/main.c`** — lines 160–171. The board now works out each wire's
voltage from the wiring file whenever it's asked, so the wiring can change
while everything runs:

```c
static int net_value(uint32_t net, int32_t *uv)
{
    for (int i = 0; i < n_links; i++) {
        if (links[i].net != net || links[i].chan >= MAX_PINS) { continue; }
        struct peer *ic = find_ic(links[i].addr);
        if (ic && ic->pin_driven[links[i].chan]) {
            *uv = ic->pin_uv[links[i].chan];
            return 0;
        }
    }
    return -1;
}
```

**`tests/integration/test_faults.py`** — lines 128–140. One case of the 28:

```python
def test_cut_trace(self, board, ch):
    """
    One trace cut. The DAC still accepts the write; nothing drives the pin.

    twin-only: on copper the pin floats and the ADC converts whatever it
    picks up, with nothing to say it is meaningless. Here the board knows
    the net is undriven, and the reading comes back as --- instead.
    """
    dut, _ = board
    dut.rewire(without(dut.netlist_lines(), ch))
    regs = read_channels(dut)
    assert regs[ch] is None, f"cut trace on ch[{ch}] still reads {regs[ch]} mV"
    assert_others_healthy(regs, skip={ch})
```

Run all of them:

```bash
PYTHONPATH=tests/integration pytest tests/integration/test_faults.py --config=tests/integration/configs/vpcb.yaml -v
```

```
============================= 28 passed in 22.26s ==============================
```

| Cases | Fault | Could a real board catch it? |
|---:|---|---|
| 15 | One cut wire, per channel | **No** — only the virtual board knows a wire is floating |
| 3 | Two wires crossed | Yes |
| 2 | A chip pulled | Partly |
| 2 | A chip set to the wrong address | Yes |
| 4 | A chip as sloppy as its datasheet allows | Yes |
| 2 | A chip dropping out; a wire opening and closing | Partly / No |

Run the same tests against the firmware from Step 1 and **19 fail, 9 pass**.
Every failure is a test that needs the virtual board's extra knowledge — the
honest limit of what a virtual board can promise about real hardware.

That table is as it stood at this fix. The first bench run later moved two rows:
the firmware now checks every cycle that each DAC still answers, so a pulled or
dropping-out chip reads `---` on a real board too. See
[Walkthrough 4](04-real-hardware.md), step 10.

---

## Cleanup

```bash
pkill -f 'vpcb-board|vpcb-dac7578'; rm -f /tmp/vpcb.sock /tmp/cut_trace.txt
```

```bash
git checkout main && git branch -D fix-it-myself
```

---

## What to take away

**A reading you can't trust is worse than no reading.** Software acts on
numbers. `---` stops it acting on one that means nothing.

**Information dies in layers.** Fixing the board alone, or the sensor code
alone, changed nothing on screen. All three had to change.

**Polling with error handling.** The firmware was already checking ten times a
second. It just never asked whether the answer was real.
