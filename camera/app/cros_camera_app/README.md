# cros_camera_app

A command-line tool for programmatically controlling the ChromeOS Camera App
(CCA).

This tool provides both a command-line interface and a Python API for automating
camera operations on ChromeOS devices.

## Setup

The tool is installed in test image as `cros_camera_app` under
`/usr/local/bin/`, with a short alias `cca` pointing to the same script.

### One-Time Setup

Before using the tool, run the following command to configure the device for
remote control:

```shell
(dut) $ cca setup
```

This command updates `/etc/chrome_dev.conf` with the necessary flags to enable
remote control.

## Usage

### Capturing a Photo

To take a photo and save it as `photo.jpg`:

```shell
(dut) $ cca take-photo --output=photo.jpg
```

### Recording a Video

To record a 5-second video and save it as `video.mp4`:

```shell
(dut) $ cca record-video --duration=5 --output=video.mp4
```

### Getting Help

For a complete list of commands and options, run:

```shell
(dut) $ cca --help
(dut) $ cca take-photo --help  # or other subcommands
```

## Development

For faster iteration during development, deploy the code directly:

```shell
# Deploy the code
(host) $ rsync -avp . rex:~/cros_camera_app

# Run the code
(dut) $ python -m cros_camera_app.cli.main
```

### Ebuild Reference

The corresponding ebuild for this tool can be found at
[cros-camera-app-9999.ebuild](https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/main/media-libs/cros-camera-app/cros-camera-app-9999.ebuild).

## Additional Documentation

-   Original design doc:
    [go/cros-camera:dd:cca-cli](http://goto.google.com/cros-camera:dd:cca-cli)
