# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""D-Bus utilities."""

import logging
from typing import List, Optional

# The dbus module works fine on the DUT, but the linter gives an "import-error".
# pylint: disable=import-error
import dbus


SHILL_DBUS_INTERFACE = "org.chromium.flimflam"


def get_connected_ethernet_interface() -> Optional[str]:
    """Get the name of the connected Ethernet interface.

    Returns:
        The name of the connected Ethernet interface. If no interface or >=2
        interfaces are connected, returns None.
    """
    bus = dbus.SystemBus()
    manager = dbus.Interface(
        bus.get_object(SHILL_DBUS_INTERFACE, "/"),
        SHILL_DBUS_INTERFACE + ".Manager",
    )
    try:
        device_paths = manager.GetProperties()["Devices"]
    except (dbus.DBusException, KeyError) as e:
        logging.error("Failed to get the device path list: %s", e)
        return None

    ret: List[str] = []

    for device_path in device_paths:
        device = dbus.Interface(
            bus.get_object(SHILL_DBUS_INTERFACE, device_path),
            SHILL_DBUS_INTERFACE + ".Device",
        )

        try:
            device_properties = device.GetProperties()
            device_type = str(device_properties["Type"])
            device_selected_service = str(device_properties["SelectedService"])
            interface = str(device_properties["Interface"])
        except (dbus.DBusException, KeyError) as e:
            logging.warning("Failed to get the device properties: %s", e)
            continue

        if device_type == "ethernet" and device_selected_service != "/":
            service = dbus.Interface(
                bus.get_object(SHILL_DBUS_INTERFACE, device_selected_service),
                SHILL_DBUS_INTERFACE + ".Service",
            )
            try:
                service_is_connected = bool(
                    service.GetProperties()["IsConnected"]
                )
            except (dbus.DBusException, KeyError) as e:
                logging.warning("Failed to get the service properties: %s", e)
                continue

            if service_is_connected:
                ret.append(interface)

    if len(ret) == 0:
        logging.error("Could not find any connected Ethernet interface")
        return None

    if len(ret) >= 2:
        logging.error(
            "Got %d connected Ethernet interfaces: %s, want exactly 1",
            len(ret),
            ret,
        )
        return None

    return ret[0]
