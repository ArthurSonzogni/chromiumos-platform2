# Chrome OS Flex Bluetooth

`process_flex_bluetooth_overrides` is an executable that runs before Floss
starts (see https://source.chromium.org/chromiumos/chromiumos/codesearch/+/HEAD:src/third_party/chromiumos-overlay/net-wireless/floss/files/upstart/btmanagerd.conf) and applies overrides for Floss on Flex. It looks at
the Bluetooth adapters on the system and writes the overrides found (for the
Bluetooth adapter) to the file `/var/lib/bluetooth/sysprops.conf.d/floss_reven_overrides.conf`,
 which is read by Floss.
