#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Ethernet-hide (ehide)

Ethernet-hide (ehide) is a tool that creates an environment that hides the
Ethernet interface on DUT but still allows SSH'ing to it through that Ethernet
interface. This tool can help CrOS developers test in a no-network or WiFi-
only environment with SSH connections unaffected. This tool can also help
protect SSH connection when developers' behavior, such as restarting or
deploying shill, can potentially affect it. Ehide only runs on test images.
"""

import argparse
import logging
import logging.handlers
import time
from typing import Dict, List

import ehide.ehide_daemon
import ehide.interface


def get_parser() -> argparse.ArgumentParser:
    """Gets the argument parser.

    Returns:
        The argument parser.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action",
        choices=("start", "stop", "state"),
        help="To start, stop, or show the current state of ehide.",
    )
    parser.add_argument(
        "-a",
        "--approach",
        default=ehide.ehide_daemon.Approach.FORWARDING.value,
        choices=(
            ehide.ehide_daemon.Approach.FORWARDING.value,
            ehide.ehide_daemon.Approach.SHELL.value,
        ),
        help=(
            "The approach to enabling SSH after hiding Ethernet. Must be "
            "'forwarding' or 'shell'. The forwarding approach uses socat to "
            "bidirectionally forward traffic between the port:22 in the ehide "
            "network namespace and the port:22 in the root network namespace. "
            "The shell approach starts an SSH server inside the created "
            "namespace and configures the SSH server to start a shell in the "
            "root network namespace using nsenter command in the ForceCommand "
            "option. The default value is 'forwarding'."
        ),
    )
    parser.add_argument(
        "-d",
        "--dhclient-dir",
        type=str,
        default="/run/dhclient",
        help=(
            "Directory to save dhclient lease file and pid file. The default "
            "value is '/run/dhclient'."
        ),
    )
    parser.add_argument(
        "-i",
        "--interface",
        type=str,
        action="append",
        help=(
            "Manually specify the name of the Ethernet interface to hide. Can "
            "appear multiple times to specify multiple interfaces. Two formats "
            "are allowed: <ifname> or <ifname>=<cidr>. If only <ifname> is "
            "specified then DHCP will be used for dynamic IPv4 provisioning. "
            "If the format is <ifname>=<cidr> then the IPv4 address will be "
            "statically set to <cidr>. If no interface is specified, ehide "
            "will automatically detect all the connected Ethernet interfaces "
            "to hide. "
        ),
    )
    parser.add_argument(
        "-l",
        "--log-path",
        type=str,
        help="File path for logging.",
    )
    parser.add_argument(
        "-n",
        "--netns-name",
        type=str,
        default="netns-ehide",
        help=(
            "Name of the network namespace to hide Ethernet interface. The "
            "default value is 'netns-ehide'."
        ),
    )
    parser.add_argument(
        "-s",
        "--static-ipv4-addr",
        type=str,
        help=(
            "Configure static IPv4 address (CIDR) for the Ethernet interface. "
            "If it is none, ehide will resort to DHCP."
        ),
    )
    return parser


def parse_interfaces(
    interfaces: List[str],
) -> Dict[str, ehide.interface.Interface]:
    """Parses interfaces from the command line arguments.

    Args:
        interfaces: A list of interfaces from the command line arguments. Each
        is in the format of either <ifname> or <ifname>=<cidr>.

    Returns:
        A dictionary of interfaces that maps Ethernet interface names to
        ehide.interface.Interface objects.
    """
    ret: Dict[str, ehide.interface.Interface] = {}
    for interface in interfaces:
        interface_split = interface.split("=")
        if len(interface_split) == 1:  # <ifname>
            ifname = interface_split[0]
            ret[ifname] = ehide.interface.Interface(ifname)
        elif len(interface_split) == 2:  # <ifname>=<cidr>
            ifname, cidr = interface_split
            ret[ifname] = ehide.interface.Interface(ifname)
            ret[ifname].static_ipv4_cidr = cidr
        else:
            raise ValueError(f"Invalid interface: {interface}.")
    return ret


def main():
    opts = get_parser().parse_args()
    action: str = opts.action

    # Set up logging.
    logging_handlers = [
        logging.handlers.SysLogHandler(
            "/dev/log", facility=logging.handlers.SysLogHandler.LOG_LOCAL1
        )
    ]
    if opts.log_path:
        logging_handlers.append(logging.FileHandler(opts.log_path))
    logging.basicConfig(
        format="ehide: %(asctime)s.%(msecs)03d %(levelname)s %(message)s",
        level=logging.INFO,
        datefmt="%Y-%m-%dT%H:%M:%S",
        handlers=logging_handlers,
    )
    # Use GMT timezone.
    logging.Formatter.converter = time.gmtime

    if opts.interface:
        interfaces = parse_interfaces(opts.interface)
    else:
        interfaces = {}
    ehide_daemon = ehide.ehide_daemon.EhideDaemon(
        action=action,
        approach=ehide.ehide_daemon.Approach.from_str(opts.approach),
        interfaces=interfaces,
        dhclient_dir=opts.dhclient_dir,
        netns_name=opts.netns_name,
    )

    if action == "start":
        ehide_daemon.start()
    elif action == "stop":
        ehide_daemon.stop()
    elif action == "state":
        print(ehide_daemon.get_state().value)
    else:
        raise ValueError(f"Unknown action: {action}.")


if __name__ == "__main__":
    main()
