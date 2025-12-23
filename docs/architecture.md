# Architecture

## Project Structure

```
app/
  src/                    # Shared, target-agnostic code
    main.c                # Application entry + sampling thread
    regs.h/c              # Thread-safe register file
    adc_backend.h         # ADC interface (no implementation)
    cmd_read_regs.c       # adcregs shell command

  targets/
    sim/                  # Simulator-only (CONFIG_APP_TARGET_SIM)
      adc_backend.c       # ADC-emul driver + injection support
      cmd_inject_adc.c    # adcset shell command
    hw/                   # Hardware-only (CONFIG_APP_TARGET_HW)
      adc_backend.c       # Real ADC driver

  boards/
    qemu_x86.conf         # Simulator Kconfig
    qemu_x86.overlay      # ADC emulator devicetree
    nucleo_h723zg.conf    # Hardware Kconfig
```

## Target Separation

SIM-only code is **never compiled** into hardware builds. Selection is done via
Kconfig at build time:

| Target | Board | Config Symbol |
|--------|-------|---------------|
| Simulator | `qemu_x86` | `CONFIG_APP_TARGET_SIM=y` |
| Hardware | `nucleo_h723zg` | `CONFIG_APP_TARGET_HW=y` |

## Configuration Options

| Option | Board Default | Description |
|--------|---------------|-------------|
| `CONFIG_APP_NUM_CH` | 15 | Number of ADC channels |
| `CONFIG_APP_SAMPLE_PERIOD_MS` | 100 | Sampling interval (ms) |

## Sampling Behavior

A dedicated thread:
1. Calls `adc_backend_sample_all()` to read all channels
2. Updates the register file with new values
3. Sleeps for `CONFIG_APP_SAMPLE_PERIOD_MS`
4. Repeats

The `seq` field increments with each sample.

## ADC Emulator (Simulator)

Uses Zephyr's `adc-emul` driver:
- 15 channels (matches hardware configuration)
- 3.3V reference
- 12-bit resolution
- Initial value: 0 mV

