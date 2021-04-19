# Host utilities build and use

## Building

To build the host utilities:
```bash
sudo emerge hps-tool
```

## Running the host utilities

To run the utilities:

```bash
hps [ -f --bus <i2c-bus> --addr <i2c-addr> ] <command> <command arguments>
```
```-f``` selects a FTDI USB connection to the device, otherwise direct I2C is assumed.
```--bus``` defines which I2C bus to use.
```--addr``` sets the I2C peripheral address to use.

The following commands are supported:

```bash
hps -f status   # Read the common status registers and display them
hps -f cmd <value> # Send a command to the module.
hps -f dl <bank> file # Download the file to the bank selected
hps -f readtest [iterations] # Read all of the registers and verify their value
```
