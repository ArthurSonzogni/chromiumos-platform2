# cros_camera_app

A command line tool for controlling the ChromeOS Camera App (CCA)
programmatically.

This tool provides both a command line interface and a Python API for automating
camera operations on ChromeOS devices.

## Development

To deploy the code directly for faster development iteration:

```shell
# Deploy the code
(host) $ rsync -avp . rex:~/cros_camera_app

# Run the code
(dut) $ python -m cros_camera_app.cli.main
```

## Related docs

- Original design doc:
  [go/cros-camera:dd:cca-cli](http://goto.google.com/cros-camera:dd:cca-cli)
