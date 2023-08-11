# cros_healthd: Telemetry and Diagnostics Daemon

`cros_healthd` provides the universal one-stop telemetry and diagnostics API
support for the ChromeOS system.

## Design doc

[go/cros_healthd](https://goto.google.com/cros_healthd)

## Development

### Set up Device Under Test (DUT)

*   Make sure that the DUT is running a recent ChromeOS image, preferably by
    running:
    ```bash
    (cros-sdk) cros flash ${DUT_IP} xbuddy://remote/${BOARD}/latest-dev/test
    ```

### Build packages (once each board)

*   Build the `diagnostics` package:
    ```bash
    (cros-sdk) USE="-cros-debug" cros build-packages --board=${BOARD}
    (cros-sdk) cros_workon-${BOARD} start diagnostics
    ```
    Note that `cros build-packages` is necessary to rebuild all dependencies
    with the correct USE flags.

### Emerge and deploy to DUT

*   `emerge` the package. This is needed whenever you make code changes.
    ```bash
    (cros-sdk) USE="-cros-debug" emerge-${BOARD} diagnostics
    ```

*   Deploy the package to DUT:
    ```bash
    (cros-sdk) cros deploy ${DUT_IP} diagnostics
    ```

*   Restart the `cros_healthd` daemon:
    ```bash
    (DUT) restart cros_healthd
    ```

### Run command line tools and inspect the output
*   Run `cros-health-tool` for telemetry, diagnostic routines or events. For
    examples:
    ```bash
    (DUT) cros-health-tool telem --category=system
    ```
    ```bash
    (DUT) cros-health-tool diag disk_read
    ```
    ```bash
    (DUT) cros-health-tool event --category=touchpad
    ```
