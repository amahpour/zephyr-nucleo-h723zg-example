# RCA: the virtual board said `-19`, the real board says `-5`

**Found:** 2026-10-25, on the first bench run
([walkthrough 4](../../walkthroughs/04-real-hardware.md), step 10).
**Fixed:** the same day. The virtual board now returns `-5`, like the real one.

## What happened

On the real NUCLEO-H723ZG, with the DAC's SDA wire pulled, every `dacset`
failed with:

```
dacset ch0: write to dacx578@4c FAILED (-5)
```

The virtual board had always reported the same kind of fault, a chip that
doesn't answer, as:

```
dacset ch0: write to dacx578@48 FAILED (-19) - device did not ACK
```

Same firmware, same driver, same fault, different number. The virtual board
exists to behave like the real one, so this is a bug in the virtual board.

The readings disagreed too, which the second half of this write-up covers.
With SDA pulled the real DAC keeps driving its last value, so `adcregs` showed a
true voltage — but from a chip the firmware could no longer reach. The virtual
board showed `---`.

## Why `-19` was chosen

**2026-09-02.** The virtual board's I²C driver, `app/drivers/i2c_vpcb.c`, was
written before any hardware existed. When the board process answered NAK
(nothing acknowledged the address), the driver returned `-ENODEV`, which is
`-19`. The comment beside it said:

> No device acknowledged: exactly what a real bus reports when the chip is
> absent, unpowered or mis-strapped.

`-ENODEV` means "no such device". It is accurate, specific, and the kind of
code many drivers use for missing hardware. It looked right, so it was never
checked.

**2026-09-03.** A related fix gave a stalled simulator its own code, `-5`, so a
broken harness couldn't be mistaken for a missing chip. That was a good goal,
and it made the separate `-19` feel even more deliberate.

**After that, the claim spread:**

- the `dacset` hint "device did not ACK" was keyed to `-19`
- the fault matrix labelled the missing and mis-strapped chip cases
  "bench: yes, identically — `-19` on the write"
- walkthroughs 1 and 2, `docs/hardware.md` and the project site all repeated it

None of those had been run on a bench. They were predictions written as facts.

## Root cause

**The virtual board's return codes were designed from what a failure *means*,
not from what the real driver *returns*.**

What the real driver returns was there to read:

- Zephyr's I²C API promises only `-EIO` for a failed transfer
  (`include/zephyr/drivers/i2c.h`: `@retval -EIO General input / output error`).
- The STM32 driver, `drivers/i2c/i2c_stm32_v2.c`, folds a NACK, lost
  arbitration and a timeout into one result: `ret = -EIO`.

Nobody read either, and there was no board to try it on.

## Contributing factors

1. **No hardware yet.** The claim couldn't be tested, and it wasn't marked as
   untested.
2. **An assumption written as fact.** The comment said "exactly what a real bus
   reports", so later work built on it instead of questioning it.
3. **A prediction labelled as a measurement.** "bench: yes, identically" had
   never been on a bench.
4. **Two good goals collided.** Distinct codes for distinct failures make
   debugging easier. Identical codes on both boards are the point of a virtual
   board. With no hardware to compare against, the first goal won by default.

## Fix

- `i2c_vpcb.c` returns `-EIO` for every bus failure, as the real driver does:
  NAK, a modelled bus timeout, and a stalled simulator.
- The distinction lives in the log line instead. `E: addr 0x48 NAK - no device
  on the virtual bus` still says which failure it was, once, when it starts.
- `dacset` prints `- nothing answered on the bus` after a `-5`, on both boards.
- The fault matrix asserts `-5`. Walkthroughs 1 and 2, `docs/hardware.md` and
  the site say `-5`.

## Verified

| Where | Fault | Result |
|---|---|---|
| Real board, before the fixes | SDA pulled | `FAILED (-5)`; readings unchanged, the DAC still drives |
| Real board, after | SDA pulled | `FAILED (-5) - nothing answered on the bus`; its 8 channels read `---` |
| Real board, after | SDA plugged back in | `answering again`; readings return within one cycle |
| Virtual board | chip process killed | `FAILED (-5) - nothing answered on the bus` |
| Virtual board | chip strapped to the wrong address | `FAILED (-5) - nothing answered on the bus` |
| Virtual board | chip killed or mis-strapped | its channels read `---` |
| Virtual board | fault matrix, all 28 cases | passed |

**Not yet measured on the bench:** a true address NACK (a chip that is present
on the bus but at a different address). The SDA test reaches the same `-EIO` in
the same driver function, and the driver source returns `-EIO` for a NACK, but a
mis-strapped breakout would confirm it.

## What stays as it was

- Videos already published show `-19`. That was the code at the time.
- Tags such as `before-fault-model` and `before-sda-pull` show `-19` where it
  was. Walkthrough 3 replays from the first, walkthrough 4 step 10 from the
  second.
- Walkthrough 3's own `-19` lines are a different path (reading a net the
  virtual board doesn't know), which has no real-hardware counterpart and is
  unchanged.

## The second finding: confident readings from a chip that doesn't answer

When the virtual board loses a chip, its channels read `---`. That came from
the board, not the firmware: the board knows the chip's pins stopped being
driven and reports the nets as floating. The firmware itself never asked
whether the chip was still there.

On the bench nothing tells it the pins stopped, because they didn't. With SDA
pulled the DAC keeps driving its last value, the ADC reads it, and `adcregs`
showed a confident 2000 mV from a chip the firmware could not reach.

**Root cause:** the virtual board's `---` for a missing chip depended on
knowledge real hardware doesn't have, so it passed for a firmware feature it
wasn't. Nothing checked that the firmware itself noticed.

**Fix:** every sample cycle the firmware reads back a register from each DAC
(`app/src/dac_health.c`). A DAC that doesn't answer has its channels marked "no
reading" until it answers again. Both boards now reach `---` the same way, from
the same I²C answer:

```
W: dacx578@4c not answering (-5) - its channels read ---
I: dacx578@4c answering again
```

Checked on the bench with SDA pulled and plugged back in. The virtual DAC model
needed one fix to support it: it executed a lone command byte, which the real
part treats as the start of a read-back.

## The rule, now

The virtual board must give the firmware the same answers the real board does:
the same return codes, and readings that mean the same thing. It may *log* more,
because it can see more. The one documented exception is a floating pin: the
virtual board knows a net is undriven and a real ADC can't, so a cut wire reads
`---` there and a number on copper. That one isn't hidden; it's tested for, by
checking a channel follows its DAC ([walkthrough 4](../../walkthroughs/04-real-hardware.md), step 9).

## Lesson

A virtual board is only as faithful as the last time someone checked it
against the real one. Test on the virtual board first, then run the same tests
on hardware. Where the two disagree, the hardware wins and the virtual board
gets fixed. Then keep running the bench, because that is the only way to find
the next disagreement.
