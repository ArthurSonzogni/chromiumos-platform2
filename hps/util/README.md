# Host utilities build and use

## Building

The host utilities are part of the ChromeOS source, and
the [developer guide](https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md)
should be followed to create a chroot where building can take place.

To build the host utilities, ensure the chroot SDK has been entered,
and use emerge to build the tool:
```bash
cros_sdk
...
sudo emerge hps-tool
```

## Running the host utilities

The host utilities are contained with a single ```hps``` binary.

To run the different commands:

```bash
hps [ --mcp | --ftdi | --test | --bus <i2c-bus> ] [ --addr <i2c-addr> ] <command> <command arguments>
```

The following arguments configure ```hps``` to connect to the
physical HPS hardware. Typically for Proto2 hardware, the ```--mcp``` argument
should be used, so that a USB cable connection from a host is used
to connect to the module (the MCP2221A connection at the module uses a I2C
interface to the hardware).
The default I2C address of 0x30 is used, or a
different address may be selected using ```--addr=N```.
More details about the hardware layers can be found in the [README](../hal/README.md)

The ```--test``` argument selects an internal s/w simulator that does not
require any hardware to run.

```--mcp``` selects a MCP2221A USB connection to the device.
```--ftdi``` selects a FTDI USB connection to the device.
```--test``` selects an internal test device.
```--bus``` selects direct I2C via this I2C bus device.
```--addr``` sets the I2C peripheral address to use.

The following commands are supported:

```bash
hps status   # Read the common status registers and display them
hps cmd [ reset | launch ] # Send the selected command to the module.
hps dl <bank> file # Download the file to the bank selected
hps readtest [iterations] # Read all of the registers and verify their value
hps boot <version> <mcu-file> <spi-file> # Boot the module and update flash if necessary
hps enable <feature-id> # Enable feature ID (0, 1).
hps disable <feature-id> # Disable feature ID (0, 1).
hps watch <feature-id> # Enable feature and watch for results
```

Some commands assume that the module is already
initialised/started and running the application
stage (such as the feature enable/disable/watch commands).
Depending on what firmware has been flashed to the MCU, the
module may be already running in stage 1 or be enabled for
application processing.

The ```hps``` utility may be used for a range of actions, depending on
the state of the hardware and what firmware is running on it.

For a full description of the interface presented for
the different stages, the
[Common Host Interface](https://docs.google.com/document/d/19RBB24DLq8DqQqLh3qfAumYQaAyNiHQx9ZqL4SxGwfQ/edit?usp=sharing)
document, the
[Stage 1 Host interface](https://docs.google.com/document/d/1cG7yyLvlsszud33i-qw_UB7T0jpHfJtnt1dtQG104b0/edit?usp=sharing)
document, and the
[Application Host interface](https://docs.google.com/document/d/1rXH4jzS1kLUby-CkSLjQxJQx2w_qiAxcyKsyRTonHns/edit?usp=sharing)
document may be referred to.

Typical actions at various stages may be (assumes FTDI connection):

## Stage 0 (RO boot loader running)

In stage 0, the hardware is running a RO boot loader that
is used to verify the stage 1 RW firmware, allow updating of the RW firmware,
and launching to the stage 1 firmware.

With the module in stage 0, the```boot``` command can be
used to take the module through its startup sequence:
 - verify the stage 1 RW code.
 - check the application version.
 - if necessary update both the MCU flash and the SPI flash.
 - enable the application processing of the module

```bash
hps --ftdi boot 11 appl-firmware spi-blob
...
hps --ftdi status
...
reg 2 = 0225  # 0x0200 is application running
...
```

The sequence to start the module can be performed manually
by sending the appropriate commands to launch to stage 1, and
then to enable application processing (and if necessary at each
stage, the MCU firmware and SPI firmware can be downloaded).

```bash
hps --ftdi status
...
reg 2 = 0005  # 0x0001 is OK, 0x0004 is appl f/w verified
...
reg 4 = 000A  # Application version (10)
...
# Update RW MCU firmware
hps --ftdi dl 0 appl-firmware  # Download version 11
# Reset module to allow new firmware to be verified
hps --ftdi cmd reset
# Optionally, check status and verify that f/w is verified
hps --ftdi status
...
reg 2 = 0025  # 0x0001 is OK, 0x0004 is appl f/w verified
...
reg 4 = 000B  # Application version (11)
...
# Launch stage 1 RW firmware
hps --ftdi cmd launch
# Verify stage 1 launched
hps --ftdi status
...
reg 2 = 0525  # 0x0100 is Stage 1 running, 0x0400 is SPI verified
...
```

## Stage 1 (RW running)

In stage 1, the hardware is running a launched RW application that
is used to verify the FPGA SPI blob, allow updating of the SPI blob,
and enabling the application firmware.

```bash
# Update FPGA SPI
hps --ftdi dl 1 spi-blob
# Reset module and re-launch to stage 1
hps --ftdi cmd reset
hps --ftdi cmd launch
# Check status and verify that stage 1 is running and SPI is verified
hps --ftdi status
...
reg 2 = 0525  # 0x0100 is Stage 1 running, 0x0400 is SPI verified
...
# Enable/launch application
hps --ftdi cmd appl
# Verify application stage is running
hps --ftdi status
...
reg 2 = 0225  # 0x0200 is application running
...
```

## Application running

At this stage, the module is ready for feature enabling
and processing.


```bash
# Enable feature 0
hps --ftdi enable 0
# Verify feature is enabled
hps --ftdi status 7
reg 7 = 0001  # 0x0001 is feature 1.
hps --ftdi status 8
reg 8 = 8024  # Result for feature 1; 0x8000 indicates valid result.
# Disable feature 0
hps --ftdi disable 0
# Verify feature is disabled
hps --ftdi status 7
reg 7 = 0000  # 0x0001 is feature 1.
# Enable feature 0 and watch for results.
hps --ftdi watch 0
Result = 42
Result = 45
...
```

The ```watch``` command will enable the selected feature and
wait in a polling loop for any changes from the result register.
