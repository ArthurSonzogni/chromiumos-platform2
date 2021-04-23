# Host utilities build and use

## Building

To build the host utilities:
```bash
sudo emerge hps-tool
```

## Running the host utilities

To run the utilities:

```bash
hps [ --ftdi | --test | --bus <i2c-bus> ] [ --addr <i2c-addr> ] <command> <command arguments>
```
```--ftdi``` selects a FTDI USB connection to the device.
```--test``` selects an internal test device.
```--bus``` selects direct I2C via this I2C bus.
```--addr``` sets the I2C peripheral address to use.

The following commands are supported:

```bash
hps status   # Read the common status registers and display them
hps cmd <value> # Send a command to the module.
hps dl <bank> file # Download the file to the bank selected
hps readtest [iterations] # Read all of the registers and verify their value
```
