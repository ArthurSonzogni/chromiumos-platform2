#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is used to set up sshd in the ehide environment only in the shell
# approach. It is used in the ForceCommand option in sshd to force the SSH
# client’s shell to enter the root netns.

# ForceCommand stores the original client command in the SSH_ORIGINAL_COMMAND
# environment variable. For example, if the client side executes "ssh ${DUT}
# ls", then SSH_ORIGINAL_COMMAND will be set to "ls".

# The nsenter command executes a program in the namespace(s) that are specified
# in the command-line options. In the nsenter command, "-t PID" means enter
# namespaces of process PID. PID 1 is "init", which is always in the root
# namespace. "-n" and "-m" indicates entering both the network namespace and
# mount namespace. There is "cd /root" here because "nsenter -m" also reset the
# working directory to “/”, and we need to change it to “/root” to match the SSH
# behavior when the tool is off.

# Entering the mount namespace is necessary because without it, some tast
# fixtures such as shillSimulatedWiFi will fail. Such fixtures search through
# /sys/class/net on DUT to get an interface list. Normally, "ls /sys/class/net"
# lists the interfaces in the network namespace. However, this network namespace
# is not the current netns, but the netns of the process who initiated /sys.
# Therefore, to align /sys/class/net with current netns, we need to both enter
# the network namespace and the mount namespace when switching namespaces.

: "${SSH_ORIGINAL_COMMAND:=cd /root; exec /bin/bash}"
nsenter -t 1 -n -m /bin/bash -c "${SSH_ORIGINAL_COMMAND}"
