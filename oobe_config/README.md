# OOBE Config Save and Restore Utilities

Provides utility executables to save and restore system state that can
be applied during OOBE.

## Testing Data Save and Restore

This will powerwash your device.

```
touch /mnt/stateful_partition/.save_rollback_data
echo "fast safe keepimg" > /mnt/stateful_partition/factory_install_reset
reboot
```
