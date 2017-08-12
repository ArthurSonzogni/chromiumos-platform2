#!/usr/bin/python2

# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is executed at build time to generate udev rules. The
# resulting rules file is installed on the device, the script itself
# is not.

from __future__ import print_function
import string

# List of USB devices (vendorid:productid) for which it is safe to enable
# autosuspend.
USB_IDS = []

# Host Controllers and internal hubs
USB_IDS += [
  # Linux Host Controller (UHCI) (most older x86 boards)
  "1d6b:0001",
  # Linux Host Controller (EHCI) (all boards)
  "1d6b:0002",
  # Linux Host Controller (XHCI) (most newer boards)
  "1d6b:0003",
  # SMSC (Internal HSIC Hub) (most Exynos boards)
  "0424:3503",
  # Intel (Rate Matching Hub) (all x86 boards)
  "05e3:0610",
  # Intel (Internal Hub?) (peppy, falco)
  "8087:0024",
  # Genesys Logic (Internal Hub) (rambi)
  "8087:8000",
]

# Webcams
USB_IDS += [
  # Chicony (zgb)
  "04f2:b1d8",
  # Chicony (mario)
  "04f2:b262",
  # Chicony (stout)
  "04f2:b2fe",
  # Chicony (butterfly)
  "04f2:b35f",
  # Chicony (rambi)
  "04f2:b443",
  # Chicony (glados)
  "04f2:b552",
  # LiteOn (spring)
  "058f:b001",
  # Foxlink? (butterfly)
  "05c8:0351",
  # Foxlink? (butterfly)
  "05c8:0355",
  # Cheng Uei? (falco)
  "05c8:036e",
  # SuYin (parrot)
  "064e:d251",
  # Realtek (falco)
  "0bda:571c",
  # Sunplus (parrot)
  "1bcf:2c17",
  # (C-13HDO10B39N) (alex)
  "2232:1013",
  # (C-10HDP11538N) (lumpy)
  "2232:1017",
  # (Namuga) (link)
  "2232:1033",
  # (C-03FFM12339N) (daisy)
  "2232:1037",
  # (C-10HDO13531N) (peach)
  "2232:1056",
  # (NCM-G102) (samus)
  "2232:6001",
  # Acer (stout)
  "5986:0299",
]

# Bluetooth Host Controller
USB_IDS += [
  # Hon-hai (parrot)
  "0489:e04e",
  # Hon-hai (peppy)
  "0489:e056",
  # LiteOn (parrot)
  "04ca:3006",
  # Atheros (stumpy, stout)
  "0cf3:3004",
  # Atheros (AR3011) (mario, alex, zgb)
  "0cf3:3005",
  # Atheros (stumyp)
  "0cf3:3007",
  # Atheros (butterfly)
  "0cf3:311e",
  # Marvell (rambi)
  "1286:2046",
  # Marvell (gru)
  "1286:204e",
  # Intel (rambi, samus)
  "8087:07dc",
  # Intel (strago, glados)
  "8087:0a2a",
]

# WWAN (3G/LTE)
USB_IDS += [
  # Samsung (Y3300) (alex, lumpy)
  "04e8:6872",
  # Samsung (Y3400) (alex, lumpy)
  "04e8:6906",
  # Qualcomm (Gobi 2000) (zgb)
  "05c6:9214",
  # Qualcomm (Gobi 2000) (zgb)
  "05c6:9215",
  # Qualcomm (Gobi 2000) (alex)
  "05c6:9244",
  # Qualcomm (Gobi 2000) (alex)
  "05c6:9245",
  # GCT (WiMax) (daisy)
  "1076:7e0*",
  # GCT (WiMax) (daisy)
  "1076:7f0*",
  # Sierra (Gobi 3000 MC8355) (stout)
  "1199:9012",
  # Sierra (Gobi 3000 MC8355) (stout)
  "1199:9013",
  # Huawei (ME936) (kip, nyan_blaze)
  "12d1:15bb",
  # Novatel (Gobi 3000) (link)
  "1410:9010",
  # Novatel (Gobi 2000) (mario)
  "1410:a010",
  # Novatel (Gobi 2000) (mario)
  "1410:a014",
  # Novatel (Gobi 3000) (alex, lumpy)
  "1410:a020",
  # Novatel (Gobi 3000 E396) (alex, lumpy, daisy)
  "1410:a021",
  # Novatel (Gobi 3000 E396U) (daisy)
  "1410:a023",
  # Altair (LTE) (spring)
  "216f:0047",
]

# Mass Storage
USB_IDS += [
  # Genesys (SD card reader) (lumpy, link, peppy)
  "05e3:0727",
  # Realtek (SD card reader) (mario, alex)
  "0bda:0138",
  # Realtek (SD card reader) (falco)
  "0bda:0177",
]

# Security Key
USB_IDS += [
  # Yubico.com
  "1050:0211",
  # Yubico.com (HID firmware)
  "1050:0200",
  # Google Cr50 (HID)
  "18d1:5014",
]

# List of PCI devices (vendorid:deviceid) for which it is safe to enable
# autosuspend.
PCI_IDS = []

# Intel
PCI_IDS += [
  # Host bridge
  "8086:590c",
  # i915
  "8086:591e",
  # proc_thermal
  "8086:1903",
  # xhci_hcd
  "8086:9d2f",
  # intel_pmc_core
  "8086:9d21",
  # i801_smbus
  "8086:9d23",
  # iwlwifi
  "8086:095a",
  # GMM
  "8086:1911",
  # Thermal
  "8086:9d31",
  # MME
  "8086:9d3a",
  # CrOS EC
  "8086:9d4b",
  # PCH SPI
  "8086:9d24",
]

# Samsung
PCI_IDS += [
  # NVMe KUS030205M-B001
  "144d:a806",
]

################################################################################

UDEV_RULE = """\
ACTION!="add", GOTO="autosuspend_end"
SUBSYSTEM!="i2c|pci|usb", GOTO="autosuspend_end"

SUBSYSTEM=="i2c", GOTO="autosuspend_i2c"
SUBSYSTEM=="pci", GOTO="autosuspend_pci"
SUBSYSTEM=="usb", GOTO="autosuspend_usb"

# I2C rules
LABEL="autosuspend_i2c"
ATTR{name}=="cyapa", ATTR{power/control}="on", GOTO="autosuspend_end"
GOTO="autosuspend_end"

# PCI rules
LABEL="autosuspend_pci"
%(pci_rules)s\
GOTO="autosuspend_end"

# USB rules
LABEL="autosuspend_usb"
%(usb_rules)s\
GOTO="autosuspend_end"

# Enable autosuspend
LABEL="autosuspend_enable"
TEST=="power/control", ATTR{power/control}="auto", GOTO="autosuspend_end"

LABEL="autosuspend_end"
"""


def main():
  pci_rules = ''
  for dev_ids in PCI_IDS:
    vendor, device = dev_ids.split(":")
    pci_rules += ('ATTR{vendor}=="0x%s", ATTR{device}=="0x%s", '
                  'GOTO="autosuspend_enable"\n' % (vendor, device))

  usb_rules = ''
  for dev_ids in USB_IDS:
    vid, pid = dev_ids.split(':')
    usb_rules += ('ATTR{idVendor}=="%s", ATTR{idProduct}=="%s", '
                  'GOTO="autosuspend_enable"\n' % (vid, pid))

  print(UDEV_RULE % {'pci_rules': pci_rules, 'usb_rules': usb_rules})

if __name__ == '__main__':
  main()
