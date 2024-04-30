# Fingerprint Study Tool

The fingerprint study tool allows you to capture raw fingerprint samples from
study participants in order to analyze the performance of a fingerprint system.

<!-- mdformat off(b/139308852) -->
*** note
See [Typography conventions] to understand what `(outside)`, `(inside)`,
`(in/out)`, and `(device)` mean.
***
<!-- mdformat on -->

[Typography conventions]: https://www.chromium.org/chromium-os/developer-library/guides/development/developer-guide/#typography-conventions

## Install/Run Fingerprint Study

1.  You can install the fingerprint_study package on a Chromebook in dev mode
    using `cros deploy` (**Option 1**), install manually with Python virtual
    environments (**Options 2**), or build+flash a custom ChromeOS image with
    the fingerprint_study package preinstalled (**Options 3**).

    On the host, run the following commands:

    -   **Option 1**

        ```bash
        (inside) $ BOARD=hatch
        (inside) $ DUT=dut1
        (inside) $ emerge-$BOARD fingerprint_study
        (inside) $ cros deploy $DUT fingerprint_study
        ```

    -   **Option 2**

        Follow the [FPSTUDY_VENV_INSTALL.md](FPSTUDY_VENV_INSTALL.md) tutorial.

    -   **Option 3**

        ```bash
        (inside) $ BOARD=hatch
        (inside) $ USE=fpstudy ./build_packages --board=$BOARD
        (inside) $ ./build_image --board=$BOARD --noenable_rootfs_verification \
                   base
        (inside) $ cros flash usb:// $BOARD/latest
        ```

        Insert the USB flash drive into the chromebook
        [boot from USB][boot-from-usb] and then
        [install the image][install-from-usb].

2.  Configure `FINGER_COUNT`, `ENROLLMENT_COUNT`, and `VERIFICATION_COUNT` in
    [/etc/init/fingerprint_study.conf](init/fingerprint_study.conf) with the
    proper fingerprint study parameters.

3.  Reboot the device.

4.  Navigate to http://127.0.0.1:9000 in a web browser.

5.  Output fingerprint captures are stored by default in `/var/lib/fingers`. See
    [/etc/init/fingerprint_study.conf](init/fingerprint_study.conf).

[boot-from-usb]:
https://www.chromium.org/chromium-os/developer-library/guides/development/developer-guide/#boot-from-your-usb-disk
[install-from-usb]:
https://www.chromium.org/chromium-os/developer-library/guides/development/developer-guide/#installing-your-chromiumos-image-to-your-hard-disk

## Test on Host Using Mock ectool

We will use a python virtual environment to ensure proper dependency versions
and a mock `ectool` in [mock-bin](mock-bin). Note, the mock ectool will
effectively emulate an immediate finger press when the study tool requests a
finger press. This does not make use of the FPC python library.

1.  Run the following command:

    ```bash
    (in/out) $ ./host-run.sh
    ```

2.  Finally, navigate to http://127.0.0.1:9000 in a web browser.

## Setup GPG Encryption

The tool supports encryption of the collected samples. See
[FPSTUDY_ENCRYPTION.md](FPSTUDY_ENCRYPTION.md).

## VSCode Python Completion

1.  Run `python-venv-setup.sh` to create the Python virtual environment.
2.  Open the platform2/biod directory in VS Code. You must explicitly open this
    directory for the [`biod/pyproject.toml`](../pyproject.toml) to
    automatically configure VS Code for Python.
