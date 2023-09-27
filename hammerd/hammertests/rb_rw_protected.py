#!/usr/bin/env python3
# Copyright 2018 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Verify flash protection

RO must not allow to boot RW with either ROLLBACK or RW unprotected
"""

from __future__ import print_function

import sys
import time

import common
import hammerd_api  # pylint: disable=import-error


FLASH_PROTECT_INIT = (
    common.EC_FLASH_PROTECT_RO_AT_BOOT
    | common.EC_FLASH_PROTECT_RO_NOW
    | common.EC_FLASH_PROTECT_ALL_NOW
    | common.EC_FLASH_PROTECT_GPIO_ASSERTED
    | common.EC_FLASH_PROTECT_ALL_AT_BOOT
    | common.EC_FLASH_PROTECT_RW_AT_BOOT
    | common.EC_FLASH_PROTECT_RW_NOW
    | common.EC_FLASH_PROTECT_ROLLBACK_AT_BOOT
    | common.EC_FLASH_PROTECT_ROLLBACK_NOW
)

FLASH_PROTECT_NORW = (
    common.EC_FLASH_PROTECT_RO_AT_BOOT
    | common.EC_FLASH_PROTECT_RO_NOW
    | common.EC_FLASH_PROTECT_GPIO_ASSERTED
    | common.EC_FLASH_PROTECT_ROLLBACK_AT_BOOT
    | common.EC_FLASH_PROTECT_ROLLBACK_NOW
)

FLASH_PROTECT_NORB = (
    common.EC_FLASH_PROTECT_RO_AT_BOOT
    | common.EC_FLASH_PROTECT_RO_NOW
    | common.EC_FLASH_PROTECT_GPIO_ASSERTED
    | common.EC_FLASH_PROTECT_RW_AT_BOOT
    | common.EC_FLASH_PROTECT_RW_NOW
)


def main(argv):
    if argv:
        sys.exit("Test takes no args!")
    updater = hammerd_api.FirmwareUpdater(
        common.BASE_VENDOR_ID, common.BASE_PRODUCT_ID, common.BASE_USB_PATH
    )
    # Load EC image.
    with open(common.IMAGE, "rb") as f:
        ec_image = f.read()
    updater.LoadEcImage(ec_image)

    common.disable_hammerd()
    common.connect_usb(updater)
    print(f"PDU Response: {updater.GetFirstResponsePdu().contents}")
    print(f"Current running section: {updater.CurrentSection()}")

    protect = get_flash_protection(updater)
    print(f"Protection: {protect:04x} == {FLASH_PROTECT_INIT:04x}?")
    assert protect == FLASH_PROTECT_INIT, "Initial WP status error"
    unlock_rw(updater)
    reset(updater)
    updater.CloseUsb()
    time.sleep(0.5)
    # Catch it right after reset: RW is still unlocked and can be updated.
    common.connect_usb(updater)
    print(f"PDU Response: {updater.GetFirstResponsePdu().contents}")
    print(f"Current running section: {updater.CurrentSection()}")
    protect = get_flash_protection(updater)
    print(f"Protection: {protect:04x} == {FLASH_PROTECT_NORW:04x}?")
    assert protect == FLASH_PROTECT_NORW, "WP status after Unlock RW"
    updater.CloseUsb()
    time.sleep(common.WAIT_TIME_JUMP_FROM_RO_TO_RW)
    # By now, hammer will have jumped to RW and locked the flash again
    common.connect_usb(updater)
    assert (
        get_flash_protection(updater) == FLASH_PROTECT_INIT
    ), "WP status after jump RW"

    updater.SendSubcommand(hammerd_api.UpdateExtraCommand.UnlockRollback)
    reset(updater)
    updater.CloseUsb()
    time.sleep(0.5)
    common.connect_usb(updater)
    print(f"PDU Response: {updater.GetFirstResponsePdu().contents}")
    print(f"Current running section: {updater.CurrentSection()}")
    assert (
        get_flash_protection(updater) == FLASH_PROTECT_NORB
    ), "WP status after Unlock RB"
    updater.CloseUsb()
    time.sleep(common.WAIT_TIME_JUMP_FROM_RO_TO_RW)
    # By now, hammer will have jumped to RW and locked the flash again
    common.connect_usb(updater)
    assert (
        get_flash_protection(updater) == FLASH_PROTECT_INIT
    ), "WP status after jump RW"


def get_flash_protection(updater):
    pdu_resp = updater.GetFirstResponsePdu().contents
    return pdu_resp.flash_protection


def reset(updater):
    updater.SendSubcommand(hammerd_api.UpdateExtraCommand.ImmediateReset)


def unlock_rw(updater):
    # Check if RW is locked and unlock if needed
    wp_rw = (get_flash_protection(updater) & common.EC_FLASH_PROTECT_RW_NOW) > 0
    print(f"WP status:  {wp_rw}")
    if wp_rw:
        print("Need to unlock RW")
        unlocked = updater.UnlockRW()
        assert unlocked == 1, "Failed to unlock RW"


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
