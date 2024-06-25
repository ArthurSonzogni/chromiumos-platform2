# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A daemon for hiding Ethernet while still enabling the SSH connection."""

from __future__ import annotations

import datetime
import enum
import glob
import json
import logging
import os
import pathlib
import shlex
import signal
import subprocess
import sys
import time
from typing import List, NoReturn, Optional

import psutil

from . import daemon
from . import dbus_utils


class Approach(enum.Enum):
    """An enum class for the ehide approaches."""

    FORWARDING = "forwarding"
    SHELL = "shell"

    @classmethod
    def from_str(cls, s: str) -> Optional[Approach]:
        """Creates an Approach enum from string.

        Args:
            s: The string.

        Returns:
            An Approach enum. If fails then return None.
        """
        for approach in Approach:
            if approach.value == s:
                return approach
        return None


class IpFamily(enum.Enum):
    """An enum class for different IP families (IPv4 and IPv6)."""

    IPv4 = 4
    IPv6 = 6


EHIDE_DIR = "/run/ehide"
SSHD_PATH = "/usr/sbin/sshd"
THIS_FILE = pathlib.Path(__file__).resolve()
THIS_DIR = THIS_FILE.parent
FORCE_COMMAND_PATH = THIS_DIR / "force_cmd.sh"
DHCLIENT_SCRIPT_PATH = "/usr/local/sbin/dhclient-script"
RECOVER_DUTS_SERVICE_NAME = "recover_duts"

# The maximum time to wait for a process to exit gracefully.
GRACEFUL_EXIT_TIMEOUT = datetime.timedelta(seconds=5)
# The maximum time to wait for an interface to appear in the root netns.
INTERFACE_APPEARANCE_TIMEOUT = datetime.timedelta(seconds=15)
# The maximum time to wait for an IP address to be set.
IP_SET_TIMEOUT = datetime.timedelta(seconds=10)
# The maximum time to wait for socat to start listening on port:22 (only in the
# forwarding approach).
SOCAT_STARTUP_TIMEOUT = datetime.timedelta(seconds=5)


def run(*args: str) -> subprocess.CompletedProcess:
    """Runs a command with subprocess.run() and check=False.

    This function does not capture the output.

    Args:
        *args: The command to run.

    Returns:
        A subprocess.CompletedProcess instance that is returned by
        subprocess.run().
    """
    logging.info("> %s", shlex.join(args))
    return subprocess.run(args, check=False)


def run_output(*args: str) -> str:
    """Captures the output from subprocess.run() and check=False.

    Args:
        *args: The command to run.

    Returns:
        A string that is the command output.
    """
    return subprocess.run(
        args, check=False, capture_output=True, encoding="utf-8"
    ).stdout


def check_service_running(service_name: str) -> bool:
    """Checks whether an init.d service is running.

    Args:
        service_name: The name of the service.

    Returns:
        A bool that indicates whether the service is running.
    """
    output = run_output("status", service_name)
    return "running" in output


def start_service(service_name: str) -> None:
    """Starts an init.d service.

    Args:
        service_name: The name of the service.
    """
    run("start", service_name)


def stop_service(service_name: str) -> None:
    """Stops an init.d service.

    Args:
        service_name: The name of the service.
    """
    run("stop", service_name)


def run_ip_link_show(
    ifname: str, netns_name: Optional[str] = None, pid: Optional[int] = None
) -> str:
    """Runs "ip link show" command for the given interface in the given netns.

    "-j" option is used for parsing.
    The network namespace can be specified either by the |netns_name| or the
    |pid|.
    If both |netns_name| and |pid| is assigned, then only the |netns_name| will
    be used.
    If neither of the |netns_name| or |pid| is assigned, then it defaults to
    return "ip link show" in the root network namespace.

    Args:
        ifname: The interface name.
        netns_name: The network namespace name.
        pid: Specifies the network namespace where the process |pid| runs. Only
            effective if it is not None and |netns_name| is None.

    Returns:
        The string output of the "ip link show" command.
    """
    options = []
    prefix = []
    if netns_name:
        options = ["-n", netns_name]
    elif pid:
        prefix = ["nsenter", "-t", str(pid), "-n"]
    options += ["-j"]
    cmd = prefix + ["ip"] + options + ["link", "show", "dev", ifname]
    return run_output(*cmd)


def get_mac_address_in_netns(
    ifname: str, netns_name: Optional[str] = None, pid: Optional[int] = None
) -> str:
    """Gets the MAC address of the given interface in the given netns.

    The network namespace can be specified either by the |netns_name| or the
    |pid|.
    If both |netns_name| and |pid| is assigned, then only the |netns_name| will
    be used.
    If neither of the |netns_name| or |pid| is assigned, then it defaults to
    return the MAC address in the root network namespace.

    Args:
        ifname: The interface name.
        netns_name: The network namespace name.
        pid: Specifies the network namespace where the process |pid| runs. Only
            effective if it is not None and |netns_name| is None.

    Returns:
        A string of the MAC address.
    """
    ip_output = run_ip_link_show(ifname, netns_name, pid)
    if not ip_output:
        return ""
    try:
        ip_dict = json.loads(ip_output)
    except json.JSONDecodeError as e:
        logging.warning("Error in parsing %s: %s", ip_output, e)
        return ""
    try:
        mac = ip_dict[0]["address"]
    except (IndexError, KeyError) as e:
        logging.warning(
            "Error in getting the MAC address from %s: %s", ip_output, e
        )
        return ""
    return mac


def get_proc_name(pid: int) -> str:
    """Gets process name with |pid|.

    Args:
        pid: The process pid.

    Returns:
        The process name. Returns "" if the process does not exist.
    """
    return run_output("ps", "-p", str(pid), "-o", "comm=").strip()


def terminate_pid(pid: int) -> None:
    """Terminates process of |pid|.

    Tries to terminate gracefully first. If the process is still running after 1
    second, then kills it by force.

    Args:
        pid: The process pid.
    """
    if not psutil.pid_exists(pid):
        logging.warning("Pid %d is not running!", pid)
        return
    try:
        # Attempt graceful shutdown.
        logging.info("Terminating pid %d gracefully...", pid)
        os.kill(pid, signal.SIGTERM)
        # Polling for a maximum of GRACEFUL_EXIT_TIMEOUT.
        start_time = datetime.datetime.now()
        end_time = start_time + GRACEFUL_EXIT_TIMEOUT
        while datetime.datetime.now() < end_time:
            if not psutil.pid_exists(pid):
                logging.info("Pid %d has exited gracefully.", pid)
                return
            time.sleep(0.1)
        logging.warning("Pid %d still running, killing by force...", pid)
        os.kill(pid, signal.SIGKILL)
    except OSError as e:
        logging.error(e)


def terminate_pids(pids: List[int]) -> None:
    """Terminates processes of |pids|.

    Args:
        pids: The process pids.
    """
    for pid in pids:
        terminate_pid(pid)


def get_pids_in_netns(netns_name: str) -> List[int]:
    """Gets pids running in a network namespace.

    Args:
        netns_name: The name of the network namespace.

    Returns:
        A list of the pids.
    """
    pids_str = run_output("ip", "netns", "pids", netns_name)
    return [int(pid) for pid in pids_str.split()]


def get_pids_from_proc_name_in_netns(
    proc_name: str, netns_name: str
) -> List[int]:
    """Gets pids of the process name in a network namespace.

    Args:
        proc_name: The process name.
        netns_name: The name of the network namespace.

    Returns:
        A list of pids.
    """
    pids_in_netns = get_pids_in_netns(netns_name)
    return [pid for pid in pids_in_netns if get_proc_name(pid) == proc_name]


def get_pids_in_netns_with_interface(ifname: str, mac: str) -> List[int]:
    """Gets all pids in the network namespace where the interface exists.

    Args:
        ifname: The name of the interface.
        mac: The MAC address of the interface.

    Returns:
        A list of all pids that run in the network namespace where the interface
        exists.
    """
    # Get a list of interface statistics file paths. Each file path will be in
    # the format of /proc/<pid>/task/<pid>/net/*/|ifname|, so we can extract
    # the pid out of it.
    stat_file_paths = glob.glob(f"/proc/*/task/*/net/*/{ifname}")

    pids = []
    for path in stat_file_paths:
        # First of all, the path must be a file, not a directory or anything
        # else.
        if not os.path.isfile(path):
            continue

        # Extract the pid.
        pid_str = path.split("/")[2]
        try:
            pid = int(pid_str)
        except ValueError:
            continue

        # Now we get the pid, but we still need to check whether the MAC address
        # of the interface that lives in the network namespace of pid is the
        # same as |ifmac| (to exclude other interfaces with the same name).
        if mac == get_mac_address_in_netns(ifname, pid=pid):
            pids.append(pid)

    return pids


def check_interface_in_netns(
    ifname: str, netns_name: Optional[str] = None
) -> bool:
    """Checks whether the given interface is in the given network namespace.

    Args:
        ifname: The interface name.
        netns_name: The network namespace name. The default is the root netns.

    Returns:
        A bool that indicates whether the interface is in the netns.
    """
    return bool(run_ip_link_show(ifname, netns_name))


def wait_for_interface_in_root_netns(ifname: str) -> bool:
    """Waits for the given interface to appear in the root network namespace.

    The method is used for ehide recovery. When the ehide network namespace is
    deleted unexpectedly and is still not freed due to the remaining
    processes in the namespace, we need to kill all the processes and wait for
    the ehide network namespace to be freed. We achieve this by waiting for the
    Ethernet interface to appear in the root network namespace in a polling
    manner.

    Args:
        ifname: The interface name.

    Returns:
        A bool that indicates whether the interface has shown up in the root
        network namespace before timeout.
    """
    logging.info("Waiting for %s to appear in the root netns...", ifname)
    # Polling for a maximum of INTERFACE_APPEARANCE_TIMEOUT.
    start_time = datetime.datetime.now()
    end_time = start_time + INTERFACE_APPEARANCE_TIMEOUT
    while datetime.datetime.now() < end_time:
        if check_interface_in_netns(ifname):
            logging.info("%s has appeared in the root netns.", ifname)
            return True
        time.sleep(0.2)
    return False


def move_interface_to_netns(ifname: str, netns_name: str) -> None:
    """Moves the given interface from the root netns to the given netns.

    Args:
        ifname: The interface name. The interface must be in the root netns.
        netns_name: The name of the netns to move the given interface to.
    """
    run("ip", "link", "set", "dev", ifname, "netns", netns_name)


def move_interface_to_root_netns(ifname: str, original_netns_name: str) -> None:
    """Moves the given interface back to the root network namespace.

    Args:
        ifname: The interface name. The interface must not be in the root netns.
        original_netns_name: The name of the netns where the given interface
            stays in.
    """
    run(
        "ip",
        "-n",
        original_netns_name,
        "link",
        "set",
        "dev",
        ifname,
        "netns",
        "1",
    )


def check_interface_up(ifname: str, netns_name: Optional[str] = None) -> bool:
    """Checks whether the given interface has the flag "UP" in the given netns.

    Args:
        ifname: The interface name.
        netns_name: The network namespace name. The default is the root netns.

    Returns:
        A bool that indicates whether the given interface has the flag "UP".
    """
    ip_output = run_ip_link_show(ifname, netns_name)
    if ip_output == "":
        return False
    try:
        ip_dict = json.loads(ip_output)
    except json.JSONDecodeError as e:
        logging.warning("Error in parsing %s: %s", ip_output, e)
        return False
    try:
        flags = ip_dict[0]["flags"]
    except (IndexError, KeyError) as e:
        logging.warning("Error in parsing flags from %s: %s", ip_output, e)
        return False
    return "UP" in flags


def bring_up_interface(ifname: str, netns_name: Optional[str] = None) -> None:
    """Brings up the given interface in the given namespace.

    Args:
        ifname: The interface name.
        netns_name: The network namespace name. The default is the root netns.
    """
    if netns_name:
        options = ["-n", netns_name]
    else:
        options = []
    cmd = ["ip"] + options + ["link", "set", "dev", ifname, "up"]
    run(*cmd)


def check_interface_ip_in_netns(
    family: IpFamily, ifname: str, netns_name: Optional[str] = None
) -> bool:
    """Checks whether the given interface has any global IPv4 or IPv6 address.

    Args:
        family: The IP family.
        ifname: The interface name.
        netns_name: The name of the network namespace where the given interface
            stays. The default is the root netns.

    Returns:
        A bool indicates that whether the given interface has the specified type
        of IP address.
    """
    options = []
    if netns_name:
        options += ["-n", netns_name]
    options += [f"-{family.value}"]
    cmd = ["ip"] + options + ["addr", "show", ifname, "scope", "global"]
    ip_output = run_output(*cmd)
    return bool(ip_output)


def wait_for_ip_set_in_netns(
    family: IpFamily, ifname: str, netns_name: Optional[str] = None
) -> bool:
    """Waits for IP address set on the given interface in a polling manner.

    Args:
        family: The IP family.
        ifname: The interface name.
        netns_name: The name of the network namespace where given interface
            stays. The default is the root netns.

    Returns:
        A bool indicates that whether the IP address is set before timeout.
    """
    logging.info("Waiting for IPv%s address set...", family.value)
    # Polling for a maximum of IP_SET_TIMEOUT.
    start_time = datetime.datetime.now()
    end_time = start_time + IP_SET_TIMEOUT
    while datetime.datetime.now() < end_time:
        if check_interface_ip_in_netns(family, ifname, netns_name):
            logging.info("IPv%s address is set.", family.value)
            return True
        time.sleep(0.2)
    logging.error("Failed to wait for IPv%s address set.", family.value)
    return False


def enable_slaac_in_netns(ifname: str, netns_name: str) -> None:
    """Enables SLAAC for the given interface in the given netns.

    Args:
        ifname: The interface name.
        netns_name: The network namespace name.
    """
    logging.info("Enabling SLAAC...")
    run(
        "ip",
        "netns",
        "exec",
        netns_name,
        "sysctl",
        "-w",
        f"net.ipv6.conf.{ifname}.disable_ipv6=0",
        "-w",
        f"net.ipv6.conf.{ifname}.accept_dad=1",
        "-w",
        f"net.ipv6.conf.{ifname}.accept_ra=2",
        "-w",
        f"net.ipv6.conf.{ifname}.forwarding=1",
    )


def check_netns(netns_name: str) -> bool:
    """Checks whether the given network namespace exists.

    Args:
        netns_name: The network namespace name.

    Returns:
        A bool that indicates whether the network namespace exists.
    """
    return netns_name in run_output("ip", "netns").split()


def close_ssh_sockets(netns_name: Optional[str] = None) -> None:
    """Forcibly closes all the incoming SSH sockets.

    Args:
        netns_name: The network namespace name. The default is the root netns.
    """
    if netns_name is None:
        cmd = []
    else:
        cmd = ["ip", "netns", "exec", netns_name]
    # "ss -K" attempts to forcibly close sockets.
    cmd += ["ss", "-K", "sport", "22"]
    run(*cmd)


class EhideDaemon(daemon.Daemon):
    """The Ethernet-hide daemon class.

    Attributes:
        approach: The approach to hiding Ethernet. Can be FORWARDING or SHELL.
        static_ipv4_cidr: The static IPv4 CIDR used to set the Ethernet
            interface. If it is None then the IPv4 address will be set
            dynamically using DHCP.
        dhclient_dir: The directory path of dhclient files: the dhclient lease
            file, pid file, and the configuration file.
        netns_name: The network namespace name used to hide the Ethernet
            interface.
        ether_ifname: Only exists in the daemon process (not the process that
            stops the daemon). The name of the Ethernet interface to hide.
        ether_mac: Only exists in the daemon process (not the process that stops
            the daemon). The MAC address of the Ethernet interface.
        has_ipv4_initially: Only exists in the daemon process (not the process
            that stops the daemon). Whether the Ethernet interface has any IPv4
            address before starting ehide.
        has_ipv6_initially: Only exists in the daemon process (not the process
            that stops the daemon). Whether the Ethernet interface has any IPv6
            address before starting ehide.
        socat_proc: Only exists in the daemon process and when the approach is
            FORWARDING. It stores a subprocess.Popen object of the socat
            process. If socat is not running, then socat_proc is None.
        recover_duts_running_initially: Only exists in the daemon process.
            Whether the recover_duts service is running before starting ehide.
            This service has to be stopped before ehide starts, since it
            interferes with ehide.
    """

    def __init__(
        self,
        approach: Approach,
        ether_ifname: Optional[str],
        static_ipv4_cidr: Optional[str],
        dhclient_dir: str,
        netns_name: str,
    ):
        """Initializes the ehide daemon.

        In addition to the attribute initialization, it also checks the
        environment if it is going to be forked to become a daemon process.
        These checks include whether the Ethernet interface has IPv4 or IPv6
        addresses, and whether the recover_duts service is running. If the IPv4
        address exists, the daemon needs to configure IPv4 address after moving
        the Ethernet interface to the ehide network namespace. Same for the IPv6
        address. If the recover_duts is running, ehide will stop it when running
        and start it again when exiting.

        Args:
            approach: The approach to hiding Ethernet interface. Can be
                FORWARDING or SHELL.
            ether_ifname: Manually specified ethernet interface name. If it is
                None then the Ethernet interface name will be determined
                automatically using the shill API.
            static_ipv4_cidr: Static IPv4 CIDR used to set the Ethernet
                interface. If it is None then the IPv4 address will be
                dynamically configured using DHCP.
            dhclient_dir: The directory path of dhclient files.
            netns_name: The name of the network namespace to hide Ethernet
                interface.
        """
        os.makedirs(EHIDE_DIR, exist_ok=True)
        super().__init__(
            pidfile=os.path.join(EHIDE_DIR, "ehide.pid"),
            state_file=os.path.join(EHIDE_DIR, "ehide_state"),
            stdin="/dev/null",
            stdout=os.path.join(EHIDE_DIR, "stdout"),
            stderr=os.path.join(EHIDE_DIR, "stderr"),
        )

        self.approach = approach
        self.static_ipv4_cidr: Optional[str] = static_ipv4_cidr
        self.dhclient_dir = dhclient_dir
        self.netns_name = netns_name

        if self.get_state() == daemon.State.OFF:
            if ether_ifname is None:
                ether_ifname = dbus_utils.get_connected_ethernet_interface()
                if ether_ifname is None:
                    logging.error("No Ethernet interface found.")
                    sys.exit(1)
            self.ether_ifname: str = ether_ifname
            logging.info("Ethernet interface to hide: %s.", self.ether_ifname)

            self.ether_mac = get_mac_address_in_netns(self.ether_ifname)
            if self.ether_mac:
                logging.info(
                    "MAC address of %s: %s.", self.ether_ifname, self.ether_mac
                )
            else:
                logging.error(
                    "Failed to acquire MAC address of %s.", self.ether_ifname
                )
                sys.exit(1)
            self.has_ipv4_initially = check_interface_ip_in_netns(
                IpFamily.IPv4, self.ether_ifname
            )
            if self.has_ipv4_initially:
                logging.info("Init: Found IPv4 address.")
            else:
                logging.info("Init: Not found IPv4 address.")
            self.has_ipv6_initially = check_interface_ip_in_netns(
                IpFamily.IPv6, self.ether_ifname
            )
            if self.has_ipv6_initially:
                logging.info("Init: Found IPv6 address.")
            else:
                logging.info("Init: Not found IPv6 address.")
            if self.approach == Approach.FORWARDING:
                self.socat_proc: Optional[subprocess.Popen] = None
            # If the service "Recover DUTs" is running, we need to disable it
            # otherwise it will try to recover the Ethernet interface (by
            # rebooting the DUT) when ehide is on. Although the
            # "check_ethernet.hook" invoked by the service offers a function to
            # pause itself, the 30-minute pause limit does not satisfy our needs
            # under all circumstances.
            self.recover_duts_running_initially = check_service_running(
                RECOVER_DUTS_SERVICE_NAME
            )

    def set_up(self) -> bool:
        """Sets up the ehide environment.

        Returns:
            A bool indicates that whether the setup is successful. If it
            succeeds, the daemon will move to state on and run loop(). If not,
            the daemon will move to state tear_down and run tear_down().
        """
        logging.info("Setting up ehide...")
        if self.get_state() == daemon.State.SET_UP:
            # Ehide is being set up, not being recovered.
            # The 0.1s delay here is to allow the SSH connection from the client
            # to the DUT to close gracefully, for the environment setups require
            # moving the Ethernet interface to another network namespace, which
            # breaks the SSH connection.
            time.sleep(0.1)
            if self.recover_duts_running_initially:
                stop_service(RECOVER_DUTS_SERVICE_NAME)
        run("ip", "netns", "add", self.netns_name)
        if not check_netns(self.netns_name):
            logging.error(
                "Failed to create network namespace %s.", self.netns_name
            )
            return False
        close_ssh_sockets()
        move_interface_to_netns(self.ether_ifname, self.netns_name)
        if not check_interface_in_netns(self.ether_ifname, self.netns_name):
            logging.error(
                "Failed to move %s to %s", self.ether_ifname, self.netns_name
            )
            return False
        for ifname in [self.ether_ifname, "lo"]:
            bring_up_interface(ifname, self.netns_name)
            if not check_interface_up(ifname, self.netns_name):
                logging.error(
                    "Failed to bring up %s in %s", ifname, self.netns_name
                )
                return False
        if self.has_ipv4_initially or self.static_ipv4_cidr:
            if not self._set_up_ipv4(self.ether_ifname):
                return False
        if self.has_ipv6_initially:
            if not self._set_up_ipv6(self.ether_ifname):
                return False
        if self.approach == Approach.FORWARDING:
            return self._start_socat()
        elif self.approach == Approach.SHELL:
            return self._start_sshd()

    def tear_down(self) -> None:
        """Tears down the ehide environment."""
        logging.info("Tearing down ehide...")
        # Again, sleep for 0.1s to allow the SSH connection to the DUT to close
        # gracefully.
        time.sleep(0.1)
        close_ssh_sockets(self.netns_name)
        move_interface_to_root_netns(self.ether_ifname, self.netns_name)
        if self.approach == Approach.FORWARDING:
            self._stop_socat()
        pids = get_pids_in_netns(self.netns_name)
        terminate_pids(pids)
        run("ip", "netns", "delete", self.netns_name)
        if self.recover_duts_running_initially:
            start_service(RECOVER_DUTS_SERVICE_NAME)

    def loop(self) -> NoReturn:
        """Runs the infinite loop and monitors the ehide environment.

        The monitoring will be conducted every 3 seconds. If it fails, stops
        itself.
        """
        while self.monitor():
            time.sleep(3)
        logging.error("Encountered unrecoverable failure, stopping ehide.")
        run("ehide", "stop")
        # Wait for the SIGTERM here.
        while True:
            pass

    def monitor(self) -> bool:
        """Monitors the ehide environment.

        Monitors the ehide environment, if there is unexpected process
        termination or ehide network namespace deletion, try to recover.

        Returns:
            True if there is no failure or the failure can be recovered,
            otherwise False.
        """
        if not check_netns(self.netns_name):
            logging.warning("%s is deleted unexpectedly.", self.netns_name)
            if not check_interface_in_netns(self.ether_ifname):
                logging.info(
                    "%s is not freed yet, terminating remaining processes...",
                    self.netns_name,
                )
                if self.approach == Approach.FORWARDING:
                    self._stop_socat()
                pids = get_pids_in_netns_with_interface(
                    self.ether_ifname, self.ether_mac
                )
                terminate_pids(pids)
                if not wait_for_interface_in_root_netns(self.ether_ifname):
                    logging.error(
                        "Failed to wait for %s to appear in the root netns.",
                        self.ether_ifname,
                    )
                    return False
            logging.info("Creating %s again...", self.netns_name)
            return self.set_up()

        # Check Ethernet interface in ehide netns.
        if not check_interface_in_netns(self.ether_ifname, self.netns_name):
            logging.error(
                "Could not detect %s in %s.", self.ether_ifname, self.netns_name
            )
            return False

        # Check whether loopback and Ethernet interfaces are up.
        for ifname in ["lo", self.ether_ifname]:
            if not check_interface_up(ifname, self.netns_name):
                logging.error(
                    "Interface %s in %s is down.",
                    ifname,
                    self.netns_name,
                )
                return False

        if self.has_ipv4_initially or self.static_ipv4_cidr:
            # Check IPv4 availability.
            if not check_interface_ip_in_netns(
                IpFamily.IPv4, self.ether_ifname, self.netns_name
            ):
                logging.error("IPv4 address lost.")
                return False

        if self.has_ipv6_initially:
            # Check IPv6 availability.
            if not check_interface_ip_in_netns(
                IpFamily.IPv6, self.ether_ifname, self.netns_name
            ):
                logging.error("IPv6 address lost.")
                return False

        if self.has_ipv4_initially and not self.static_ipv4_cidr:
            # Check whether dhclient is running.
            dhclient_pids = get_pids_from_proc_name_in_netns(
                "dhclient", self.netns_name
            )
            if not dhclient_pids:
                logging.warning("Could not detect dhclient, recovering...")
                self._start_dhclient()
                if not wait_for_ip_set_in_netns(
                    IpFamily.IPv4, self.ether_ifname, self.netns_name
                ):
                    return False

        if self.approach == Approach.FORWARDING:
            # Check whether socat is running.
            if self.socat_proc.poll() is not None:
                logging.warning("Socat has exited unexpectedly, recovering...")
                self._stop_socat()  # clean its child processes
                if not self._start_socat():
                    return False

        if self.approach == Approach.SHELL:
            # Check whether sshd is running.
            sshd_pids = get_pids_from_proc_name_in_netns(
                "sshd", self.netns_name
            )
            if not sshd_pids:
                logging.warning("Could not detect sshd, recovering...")
                if not self._start_sshd():
                    return False

        return True

    def _set_up_ipv4(self, ifname: str) -> bool:
        """Sets up IPv4 address on the given interface.

        Args:
            ifname: The interface name.

        Returns:
            A bool indicates whether the setup has been successful.
        """
        if self.static_ipv4_cidr:
            run(
                "ip",
                "-n",
                self.netns_name,
                "addr",
                "add",
                self.static_ipv4_cidr,
                "dev",
                ifname,
            )
        else:
            self._start_dhclient()
        return wait_for_ip_set_in_netns(IpFamily.IPv4, ifname, self.netns_name)

    def _set_up_ipv6(self, ifname: str) -> bool:
        """Sets up IPv6 address on the given interface.

        Args:
            ifname: The interface name.

        Returns:
            A bool indicates whether the setup has been successful.
        """
        enable_slaac_in_netns(ifname, self.netns_name)
        return wait_for_ip_set_in_netns(IpFamily.IPv6, ifname, self.netns_name)

    def _start_dhclient(self) -> None:
        """Starts dhclient."""
        old_pids = get_pids_from_proc_name_in_netns("dhclient", self.netns_name)
        if old_pids:
            logging.warning("Old dhclient running, terminating...")
            terminate_pids(old_pids)
        os.makedirs(self.dhclient_dir, exist_ok=True)
        conf_path = os.path.join(self.dhclient_dir, "dhclient.conf")
        logging.info("Writing dhclinet configuration to %s...", conf_path)
        with open(conf_path, "w", encoding="utf-8") as f:
            f.writelines(
                (
                    f'interface "{self.ether_ifname}" {{\n',
                    f"    send dhcp-client-identifier 01:{self.ether_mac};\n",
                    "}\n",
                )
            )
        logging.info("Starting dhclient...")
        run(
            "ip",
            "netns",
            "exec",
            self.netns_name,
            "dhclient",
            "-4",
            "-lf",
            os.path.join(self.dhclient_dir, "dhclient.leases"),
            "-pf",
            os.path.join(self.dhclient_dir, "dhclient.pid"),
            "-sf",
            DHCLIENT_SCRIPT_PATH,
            "-cf",
            conf_path,
        )

    def _start_socat(self) -> bool:
        """Starts socat.

        Returns:
            A bool that indicates whether socat has started successfully.
        """
        logging.info("Starting socat...")
        try:
            # Since we need this self.socat_proc to stay open during ehide is
            # on, we cannot use 'with' statement here. Disabling pylint
            # consider-using-with.
            # pylint: disable=consider-using-with
            self.socat_proc = subprocess.Popen(
                [
                    "socat",
                    "--experimental",
                    "TCP6-LISTEN:22,reuseaddr,fork,netns=netns-ehide",
                    "TCP6-CONNECT:localhost:22",
                ]
            )
        except OSError as e:
            logging.error("Failed to start socat: %s", e)
            return False
        # Polling for a maximum of SOCAT_STARTUP_TIMEOUT to wait for socat to
        # start listening on port:22 in the ehide netns.
        start_time = datetime.datetime.now()
        end_time = start_time + SOCAT_STARTUP_TIMEOUT
        while datetime.datetime.now() < end_time:
            # Run ss command to check whether there is any listening socket on
            # port:22 in the ehide netns.
            # -t: Display TCP sockets.
            # -l: Display only listening sockets.
            # -H: Suppress header line.
            # -N NSNAME: Switch to the specified network namespace name.
            ss_output = run_output(
                "ss", "-tlH", "-N", self.netns_name, "src", ":22"
            )
            if bool(ss_output):
                return True
            time.sleep(0.2)
        logging.error("Socat not listening on port:22 in %s.", self.netns_name)
        return False

    def _stop_socat(self) -> None:
        """Stops socat. Also terminates all its forked child processes."""
        logging.info("Stopping socat and its child processes...")
        if self.socat_proc is None:
            return
        proc = psutil.Process(self.socat_proc.pid)
        for child in proc.children(recursive=True):
            child.kill()
        proc.kill()
        self.socat_proc = None

    def _start_sshd(self) -> bool:
        """Starts sshd.

        Returns:
            A bool that indicates whether sshd has started successfully.
        """
        old_pids = get_pids_from_proc_name_in_netns("sshd", self.netns_name)
        if old_pids:
            logging.warning("Old sshd running, terminating...")
            terminate_pids(old_pids)
        logging.info("Starting sshd...")
        run(
            "ip",
            "netns",
            "exec",
            self.netns_name,
            SSHD_PATH,
            "-o",
            f"ForceCommand=/bin/bash {FORCE_COMMAND_PATH}",
        )
        sshd_pids = get_pids_from_proc_name_in_netns("sshd", self.netns_name)
        if not sshd_pids:
            logging.error("Sshd has exited unexpectedly.")
            return False
        return True
