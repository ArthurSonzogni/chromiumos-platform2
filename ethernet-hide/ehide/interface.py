# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The Ethernet interface class."""

from typing import Optional


class Interface:
    """The Ethernet interface class.

    Attributes:
        name: Name of the interface.
        mac: The MAC address of the interface.
        static_ipv4_cidr: The static IPv4 CIDR used to set this interface. If it
            is None then the IPv4 address will be set dynamically using DHCP.
        has_ipv4_initially: Whether the interface has any IPv4 address before
            starting ehide.
        has_ipv6_initially: Whether the interface has any IPv6 address before
            starting ehide.
    """

    def __init__(self, name: str):
        """Initializes the Ethernet interface."""
        self.name = name
        self.mac: Optional[str] = None
        self.static_ipv4_cidr: Optional[str] = None
        self.has_ipv4_initially: bool = False
        self.has_ipv6_initially: bool = False

    def is_dhcp_used(self) -> bool:
        """Returns whether DHCP is used for IPv4 provisioning.

        Returns:
            A bool that indicates whether DHCP is used for IPv4 provisioning
            on this interface when ehide runs.
        """
        return self.has_ipv4_initially and self.static_ipv4_cidr is None

    def has_ipv4(self) -> bool:
        """Returns whether this interface has any IPv4 address when ehide runs.

        Returns:
            A bool that indicates whether this interface should have any IPv4
            address when ehide runs.
        """
        return self.has_ipv4_initially or self.static_ipv4_cidr is not None

    def has_ipv6(self) -> bool:
        """Returns whether this interface has any IPv6 address when ehide runs.

        Returns:
            A bool that indicates whether this interface should have any IPv6
            address when ehide runs.
        """
        return self.has_ipv6_initially
