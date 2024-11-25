# Use Virtualenv to Run the Fingerprint Study Collection Tool

This is an **alternative method** to install the fingerprint study collection
tool onto a test device. It bypasses the Chrome OS/Gentoo dependencies and
allows using providing a clean virtualenv for the execution on the test device.

## 1) Build python3 virtual environment bundle

```bash
# Optionally, you can build the virtual environment in a Docker container.
# docker run -v$HOME/Downloads:/Downloads -it debian
# On Debian, ensure that git, python3, python3-pip, and virtualenv are installed.
(outside) $ sudo apt update && apt install git python3 python3-pip virtualenv
# Grab the fingerprint study tool source
(outside) $ git clone https://chromium.googlesource.com/chromiumos/platform2
# Create an isolated python3 environment
(outside) $ virtualenv -p python3 /tmp/fpstudy-virtualenv
(outside) $ . /tmp/fpstudy-virtualenv/bin/activate
# Install fingerprint study dependencies
(outside) $ pip3 install -r platform2/biod/study/requirements.txt
# Copy the fingerprint study source
(outside) $ cp -r platform2/biod/study /tmp/fpstudy-virtualenv
# Bundle the virtual environment with study source
(outside) $ tar -C /tmp -czvf /tmp/fpstudy-virtualenv.tar.gz fpstudy-virtualenv
# For Docker with Downloads volume shared, run the following command:
# cp /tmp/fpstudy-virtualenv.tar.gz /Downloads/
```

The output of these steps is the `fpstudy-virtualenv.tar.gz` archive.

*See [Typography conventions] to understand what `(outside)`, `(inside)`,
`(in/out)`, and `(device)` mean.*

## 2) Enable developer mode on the chromebook

See [Enable Developer Mode].

## 3) Install python3 virtual environment bundle

Transfer the `fpstudy-virtualenv.tar.gz` bundle to the test device.

One such method is to use scp, like in the following command:

```bash
(in/out) $ scp fpstudy-virtualenv.tar.gz root@$DUTIP:/root/
```

On the test device, extract the bundle into `/opt/google`, as shown in the
following command set:

```bash
(device) $ mkdir -p /opt/google
(device) $ tar -xzvf /root/fpstudy-virtualenv.tar.gz -C /opt/google
```

Enable the fingerprint study Upstart job.

```bash
(device) $ ln -s /opt/google/fpstudy-virtualenv/study/fingerprint_study_virtualenv.conf /etc/init
(device) $ start fingerprint_study_virtualenv
(device) $ sleep 2
(device) $ status fingerprint_study_virtualenv
```

## 4) Configure

To configure the number of fingers, enrollment taps, and verification taps
expected by the fingerprint study tool, please modify
`/opt/google/fpstudy-virtualenv/study/fingerprint_study_virtualenv.conf`.

## 5) Test

Navigate to http://127.0.0.1:9000 in a web browser.

[Enable Developer Mode]: https://www.chromium.org/chromium-os/developer-library/guides/device/developer-mode/
[Typography conventions]: https://www.chromium.org/chromium-os/developer-library/guides/development/developer-guide/#typography-conventions
