# Hardware Setup (NUCLEO-H723ZG)

## Prerequisites

### Install OpenOCD

```bash
sudo apt install openocd
```

### USB Permissions (udev Rules)

Create udev rules to allow non-root access to the ST-LINK debugger:

```bash
sudo tee /etc/udev/rules.d/99-stlink.rules << 'EOF'
# ST-LINK V2
ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3748", MODE="0666", GROUP="plugdev"
# ST-LINK V2-1
ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", MODE="0666", GROUP="plugdev"
# ST-LINK V3
ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374e", MODE="0666", GROUP="plugdev"
ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374f", MODE="0666", GROUP="plugdev"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

### WSL Users

If using WSL, you need to pass the USB device through using `usbipd`:

```powershell
# In Windows PowerShell (Admin)
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

After setting up udev rules, you may need to re-attach the device for the rules to take effect.

Verify the device is visible:
```bash
lsusb | grep STMicroelectronics
# Should show: STMicroelectronics STLINK-V3
```

## Building for Hardware

```bash
source ~/zephyrproject/.venv/bin/activate
export ZEPHYR_BASE=~/zephyrproject/zephyr

cd ~/code/zephyr-nucleo-h723zg-example
west build -b nucleo_h723zg app --pristine
```

**Note:** If you encounter Kconfig errors about `HAS_CMSIS_CORE`, ensure you have the CMSIS modules installed:
```bash
cd ~/zephyrproject && west update cmsis cmsis_6
```

## Flashing

Flash using OpenOCD (recommended):

```bash
west flash --runner openocd
```

**Troubleshooting:**

| Error | Solution |
|-------|----------|
| `LIBUSB_ERROR_ACCESS` | Set up udev rules (see above) and re-attach USB device |
| `no runners.yaml found` | Rebuild with `--pristine` for the hardware target |
| `STM32_Programmer_CLI not found` | Use `--runner openocd` instead |

## ADC Configuration

The firmware supports up to 15 ADC channels when used with the CD74HC4067 mux.

Configure in `targets/hw/adc_backend.c`:

1. Set `ADC_CONFIGURED=1`
2. Define the correct ADC node label (e.g., `adc1`)
3. Configure channel mappings per the wiring table below

Build with 15 channels:
```bash
west build -b nucleo_h723zg app --pristine -- -DCONFIG_APP_NUM_CH=15
```

## 15-Channel Mux Wiring Guide

### Equipment
- **Power Supply:** Rigol DP832 (Channel 3, 0-5V output)
- **Mux:** CD74HC4067 16-channel analog multiplexer on KB2040
- **DUT:** NUCLEO-H723ZG

### Wiring Diagram

```
Rigol DP832 CH3 ──────► CD74HC4067 Common (SIG)
                              │
                    ┌─────────┴─────────┐
                    │   16:1 MUX        │
                    │   Outputs C0-C14  │
                    └─────────┬─────────┘
                              │
              ┌───────────────┼───────────────┐
              │               │               │
              ▼               ▼               ▼
           CN9 (A0-A5)    CN7 (D8,D12,    CN10 (A6-A8,
                              D13,D24)        D32,D33)
```

### Complete Wiring Table

| Mux Output | Nucleo Pin | ADC Input | Connector Pin | Wire Color (suggested) |
|------------|------------|-----------|---------------|------------------------|
| C0 | PA3 | ADC1_INP15 | CN9-1 (A0) | Brown |
| C1 | PC0 | ADC1_INP10 | CN9-3 (A1) | Red |
| C2 | PC3 | **ADC3_INP1** | CN9-5 (A2) | Orange |
| C3 | PB1 | ADC1_INP5 | CN9-7 (A3) | Yellow |
| C4 | PC2 | **ADC3_INP0** | CN9-9 (A4) | Green |
| C5 | PF10 | ADC3_INP6 | CN9-11 (A5) | Blue |
| C6 | PA5 | ADC1_INP19 | CN7-10 (D13) | Purple |
| C7 | PA6 | ADC1_INP3 | CN7-12 (D12) | Gray |
| C8 | PA4 | ADC1_INP18 | CN7-17 (D24) | White |
| C9 | PF3 | ADC3_INP5 | CN7-20 (D8) | Black |
| C10 | PF4 | ADC3_INP9 | CN10-7 (A6) | Brown/White |
| C11 | PF5 | ADC3_INP4 | CN10-9 (A7) | Red/White |
| C12 | PF6 | ADC3_INP8 | CN10-11 (A8) | Orange/White |
| C13 | PA0 | ADC1_INP16 | CN10-27 (D32) | Yellow/White |
| C14 | PB0 | ADC1_INP9 | CN10-29 (D33) | Green/White |

**Note:** Mux outputs C0-C15 match software channels 0-15. We use C0-C14 (15 channels).

**Important:** PC2 and PC3 are `PC2_C` and `PC3_C` pins that only connect to ADC3, not ADC1!

### Ground Connections

Connect GND between all devices:
- Rigol DP832 GND
- KB2040 Mux GND
- NUCLEO-H723ZG GND (CN7-8, CN10-17, or any GND pin)

### ADC Channel Summary by Connector

| Connector | Pins Used | ADC Channels |
|-----------|-----------|--------------|
| CN9 | 6 pins | A0-A5 (INP15, INP10, INP13, INP5, INP12, ADC3_INP6) |
| CN7 | 4 pins | D8, D12, D13, D24 (ADC3_INP5, INP3, INP19, INP18) |
| CN10 | 5 pins | A6-A8, D32, D33 (ADC3_INP9/4/8, INP16, INP9) |

### ADC Peripheral Mapping

| ADC | Channels Used | Pins |
|-----|---------------|------|
| ADC1 | INP3, INP5, INP9, INP10, INP15, INP16, INP18, INP19 | PA0, PA3, PA4, PA5, PA6, PB0, PB1, PC0 |
| ADC3 | INP0, INP1, INP4, INP5, INP6, INP8, INP9 | PC2, PC3, PF3, PF4, PF5, PF6, PF10 |

**Note:** PC2 and PC3 are special `PC2_C`/`PC3_C` pins that only connect to ADC3 (not ADC1).

## NUCLEO-H723ZG Connector Pinout Reference

### CN9 - Arduino Analog Header
| Pin | MCU Pin | Function |
|-----|---------|----------|
| 1 | PA3 | A0 / ADC1_INP15 |
| 3 | PC0 | A1 / ADC1_INP10 |
| 5 | PC3 | A2 / ADC1_INP13 |
| 7 | PB1 | A3 / ADC1_INP5 |
| - | PC2 | A4 / ADC1_INP12 |
| - | PF10 | A5 / ADC3_INP6 |

### CN7 - Morpho Left (ADC pins only)
| Pin | MCU Pin | Function |
|-----|---------|----------|
| 10 | PA5 | D13 / ADC1_INP19 |
| 12 | PA6 | D12 / ADC1_INP3 |
| 17 | PA4 | D24 / ADC1_INP18 |
| 20 | PF3 | D8 / ADC3_INP5 |

### CN10 - Morpho Right (ADC pins only)
| Pin | MCU Pin | Function |
|-----|---------|----------|
| 7 | PF4 | A6 / ADC3_INP9 |
| 9 | PF5 | A7 / ADC3_INP4 |
| 11 | PF6 | A8 / ADC3_INP8 |
| 27 | PA0 | D32 / ADC1_INP16 |
| 29 | PB0 | D33 / ADC1_INP9 |

## Connecting via Serial

The NUCLEO board exposes a USB serial port. Connect and use:

```bash
# Find the port
ls /dev/ttyACM*

# Connect with screen, minicom, or similar
screen /dev/ttyACM0 115200
```

## Differences from Simulator

- `adcset` command does **not exist** on hardware builds
- ADC values come from real analog inputs
- Sampling happens at the same configurable rate

## Known Limitations

### CD74HC4067 Mux On-Resistance

The CD74HC4067 analog multiplexer has significant on-resistance (Ron) that causes voltage drop between the power supply and ADC input:

| Supply Voltage | Typical Ron | Expected Error |
|----------------|-------------|----------------|
| 5.0V | 50-70Ω | ~50-70mV |
| **3.3V** | **~100Ω** | **~80-120mV** |
| 2.0V | ~200Ω | ~150-200mV |

**This is a known limitation of the CD74HC4067 family.** At 3.3V supply, expect ~100mV measurement error due to the mux on-resistance.

#### Mitigation Options

1. **Accept the tolerance** - Tests use 150mV tolerance to account for this
2. **Software calibration** - Apply per-channel offset correction in firmware
3. **Buffer amplifier** - Add unity-gain op-amp between mux output and ADC input
4. **Lower-Ron mux** - Use ADG1606/ADG1607 (~4Ω Ron) for higher accuracy

#### Ground Connections Are Critical

Ensure a proper star ground connection between:
- Rigol DP832 GND (Channel 3 negative)
- KB2040 Mux board GND
- Nucleo GND (preferably CN9 pin 14, near analog inputs)

Poor grounding can add additional 30-50mV of error.

## Running Integration Tests

### Prerequisites

```bash
cd tests/integration
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

# For physical hardware tests, also install:
pip install -r requirements-physical.txt
```

### Physical Hardware Tests

Run tests against real hardware with Rigol DP832 + KB2040 mux:

```bash
cd tests/integration
pytest test_adc.py --config=configs/physical.yaml -v
```

### Virtual Tests (QEMU)

Run tests against QEMU simulator:

```bash
# First, start QEMU in another terminal
west build -t run

# Then run tests
cd tests/integration
pytest test_adc.py --config=configs/virtual.yaml -v
```

