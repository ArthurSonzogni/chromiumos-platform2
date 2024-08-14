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

import ehide.ehide_daemon


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
        "-e",
        "--ether-ifname",
        type=str,
        help=(
            "Manually specify the name of the ethernet device to hide. If not "
            "specified, ehide will automatically detect the connected Ethernet "
            "interface."
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

    if opts.static_ipv4_addr and opts.ether_ifname:
        static_ipv4_cidr = {opts.ether_ifname: opts.static_ipv4_addr}
    else:
        static_ipv4_cidr = {}
    ehide_daemon = ehide.ehide_daemon.EhideDaemon(
        action=action,
        approach=ehide.ehide_daemon.Approach.from_str(opts.approach),
        ether_ifname=opts.ether_ifname,
        static_ipv4_cidr=static_ipv4_cidr,
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
