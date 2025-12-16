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

## ADC Configuration (TODO)

When hardware arrives, configure the ADC in `targets/hw/adc_backend.c`:

1. Set `ADC_CONFIGURED=1`
2. Define the correct ADC node label (e.g., `adc1`)
3. Configure channel mappings for your wiring

## NUCLEO-H723ZG Arduino Header ADC Pins

| Arduino Pin | STM32 Pin | ADC Channel |
|-------------|-----------|-------------|
| A0 | PA3 | ADC1_INP15 |
| A1 | PC0 | ADC1_INP10 |
| A2 | PC3 | ADC1_INP13 |
| A3 | PB1 | ADC1_INP5 |
| A4 | PC2 | ADC1_INP12 |
| A5 | PF10 | ADC3_INP6 |

You may need a devicetree overlay (`boards/nucleo_h723zg.overlay`) to enable
specific ADC channels.

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

