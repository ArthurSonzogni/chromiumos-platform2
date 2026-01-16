# Reference VM

The Reference VM is a Debian-based VM image used for testing ChromeOS VM
integrations.

## Dependencies

Debian:

```sh
sudo apt install eatmydata fai-setup-storage lvm2 ovmf python3-jinja2 \
  python3-requests python3-yaml
```

For creating the UEFI variables image:

```sh
pip3 install --user virt-firmware
```

## Build instructions

```
sudo ./build.py
```

`sudo` is required for loopback device use.

## UEFI variables preparation

```
virt-fw-vars --input /usr/share/OVMF/OVMF_VARS_4M.fd \
  --output refvm_VARS.fd \
  --enroll-generate "reference VM PK/KEK" \
  --secure-boot \
  --add-mok "$(uuidgen -r)" ./data/var/lib/dkms/mok.pub
```

### Secure Boot

The built image and firmware support UEFI Secure Boot. To load out-of-tree modules
(currently `virtio_wl` and `tpm_virtio`), a signing key is included in the built image at
`/var/lib/dkms/mok.key`. The variables image generated using the above command
includes the public key in the MOK.

## Booting in crosvm directly

A build of OVMF for crosvm (`CROSVM_CODE.fd`) is required. To run a basic VM
with a serial console:

```
crosvm run --cpus 4 --mem 4096 --disable-sandbox --bios CROSVM_CODE.fd \
  --pflash refvm_VARS.fd --block path=/mnt/refvm.img \
  --serial type=stdout,console,stdin,earlycon
```

To enable networking, refer to the
[crosvm guide](https://crosvm.dev/book/running_crosvm/example_usage.html#add-networking-support)
.

## Booting on ChromeOS

Refer to [go/bruschetta-installer](https://goto.google.com/bruschetta-installer) on how to boot on
ChromeOS.

## Uprev

Uprev script will update the copy of reference_vm image to the latest one built
in kokoro.

Install dependencies

```
sudo apt install brotli
```

Run the uprev script and commit the result.

```
repo start refvm-uprev-xxxx src/platform/tast-tests
cd src/platform2/vm_tools/reference_vm/
./uprev.sh
cd -
cd src/platform/tast-tests
git commit -a
cros_sdk tast run $DUT bruschetta.Basic  # try running test locally.
```

Commit message can be something like:

```
bruschetta: uprev to refvm-xxx

BUG=b:366103608
TEST=cros_sdk tast run $DUT bruschetta.Basic
```
