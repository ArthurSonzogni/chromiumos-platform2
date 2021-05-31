# HPS Hardware Abstraction Layer

The HPS code uses a HAL (Hardware Abstraction Layer)
interface to access the HPS hardware device.
The ```dev.h``` interface defines the HAL API.
This HAL library implements the various access
methods that are used.

## I2C

The i2c implementation uses a i2c device to connect to
the hardware module.

## FTDI

The ftdi implementation uses the libftdi1 library to
remotely interface a minimal I2C connection via
FTDI USB interface devices such as FT4232H.

## FakeDev

The FakeDev class implements an internal s/w simulator
of the HPS hardware for testing and development.

## UART

The uart implementation uses a serial protocol
to communicate with a remote device.

## Retry

The retry class is a shim proxy layer that allows
calls to be retried upon error.
