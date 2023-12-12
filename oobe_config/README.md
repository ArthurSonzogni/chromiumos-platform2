# OOBE Config Save and Restore Utilities

Provides utility executables to save and restore system state that can be
applied during OOBE.

Currently only used by the enterprise rollback feature, which is described
below.

## Enterprise Rollback

Enterprise Rollback is a feature that allows device admins to roll back devices
to a previous version. Device-wide network configs and state of oobe are
preserved.

### Rollback Overview

- Admin pins to a certain device version and allows rollback
- On the next update check, the device sends the [rollback_allowed](https://source.chromium.org/chromium/aosp/platform/system/update_engine/+/HEAD:cros/omaha_request_builder_xml.cc;l=159;drc=bed15aeab709496288faa6ab288b7d0e6cde0630)
flag to Omaha
- If there's a rollback image available for the pinned version, it will be
downloaded and installed
- Once the update is ready, update_engine leaves the flag
[/mnt/stateful_partition/.save_rollback_data](https://source.chromium.org/chromium/aosp/platform/system/update_engine/+/HEAD:cros/hardware_chromeos.cc;l=262;drc=bed15aeab709496288faa6ab288b7d0e6cde0630)
and marks the device to be powerwashed
- The device will boot into the rollback image on the next reboot


- [oobe_config_save](http://cs/chromeos_public/src/platform2/oobe_config/etc/init/oobe_config_save.conf)
is triggered during shutdown. Because the `.save_rollback_data` flag is present
it will:
	- Collect information for [rollback_data.proto](./rollback_data.proto)
	by connecting to Chrome via mojo
	- (Option 1, devices produced with firmware that branched before 2023)
	Serialize and encrypt data with [openssl](./config/rollback_openssl_encryption.h)
	using pstore. The encryption key is randomly created by software and stays
	in `/var/lib/oobe_config_save/data_for_pstore`
	- (Option 2, devices produced with firmware that branched after 2022)
	Serialize and encrypt data with [libhwsec](../libhwsec/frontend/oobe_config/)
	using the TPM
	- Encrypted data is put into
	`/mnt/stateful_partition/unencrypted/preserve/rollback_data(_tpm)`

- Upon booting into the rollback image, the device
[powerwashes](/init/clobber_state.cc)
	- `/var/lib/oobe_config_save/data_for_pstore` is moved into pstore
	`/dev/pmsg0`
	- `/mnt/stateful_partition/unencrypted/preserve/rollback_data(_tpm)` is
	preserved by moving to `/tmp` during wiping and then moving back
	- Once the device is wiped, it is rebooted


- [oobe_config_restore](./etc/init/oobe_config_restore.conf)
service always runs when oobe is not finished
- Chrome requests oobe configuration from `oobe_config_restore`
	- Encrypted rollback data is loaded from
	`/mnt/stateful_partition/unencrypted/preserve/rollback_data`
	- If pstore was used, the key can be found under `/sys/fs/pstore/pmsg-ramoops-*`
	- Unencrypted data is sent to Chrome and stored in
	`/var/lib/oobe_config_restore`
	- Chrome steps through oobe and reconfigures networks using
	[rollback_network_config](https://source.chromium.org/chromium/chromium/src/+/main:chrome/browser/ash/net/rollback_network_config/)

Note:
- Data put into `/dev/pmsg0` only survives one reboot and does not survive a
	power cycle

Known Issues:
- Firmware version increments may break rollback because of firmware rollback
	protection
- Data save may fail on an unclean shutdown
- If the device loses power after powerwash, the encryption key in pstore is lost and
	rollback data cannot be decrypted

## Checking if a device is able to use TPM encryption (Option 2 above)

```
tpm_manager_client list_spaces
```
Check if you see a space `0x000100E`. That is the rollback space and indicates
the device is able to use the TPM for encryption.

## Testing Data Save and Restore for Rollback

This will powerwash your device.

```
touch /mnt/stateful_partition/.save_rollback_data
echo "fast safe keepimg" > /mnt/stateful_partition/factory_install_reset
reboot
```
