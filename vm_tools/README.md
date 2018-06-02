[TOC]

# vm_tools - Utilities for Virtual Machine (VM) orchestration

This directory contains various tools for managing the lifetime of VM instances
and for providing any services those VMs may need while they are running.

## vm_concierge

`vm_concierge` is a system daemon that runs in Chrome OS userspace and is
responsible for managing the lifetime of all VMs.  It exposes a [D-Bus
API](https://chromium.googlesource.com/chromiumos/platform/system_api/+/master/dbus/vm_concierge/)
for starting and stopping VMs.

When `vm_concierge` receives a request to start a VM it allocates various
resources for that VM (IPv4 address, vsock context id, etc) from a shared pool
of resources.  It then launches a new instance of `crosvm` to actually run the
VM.

Once the VM has started up `vm_concierge` communicates with the `maitred`
instance inside the VM to finish setting it up.  This includes configuring the
network and mounting disk images.

## vm_cicerone

`vm_cicerone` is a system daemon that runs in Chrome OS userspace and is
responsible for all communication directly with the container in a VM. It
exposes a [D-Bus API](https://chromium.googlesource.com/chromiumos/platform/system_api/+/master/dbus/vm_cicerone)
for doing things such as launching applications in containers, getting icons
from containers and other container related functionality as it is extended. It
also sends out signals for starting/stopping of containers.

`vm_concierge` communicates with `vm_cicerone` to keep the list of running VMs
in sync and also to retrieve status of containers and get security tokens.

When `vm_cicerone` communicates with a container, it is interacting with the
`garcon` component running inside of that container and is doing so over gRPC.

## maitred

`maitred` is the agent running inside the VM responsible for managing
the VM instance.  It acts as the init system, starting up system services,
mounting file systems, and launching the container with the actual application
that the user wants to run.  It is responsible for shutting down the VM once the
user's application exits or if requested to by `vm_concierge`.

See [docs/init.md](docs/init.md) for more details on the duties maitred carries
out as pid 1.

## garcon

`garcon` is a daemon that runs inside of a container within a VM. gRPC is used
to communicate between `vm_cicerone` and `garcon`. It is used to control/query
things inside the contaienr such as application launching, accessibility,
handling intents, opening files, etc. The communication is bi-directional. It
uses TCP/IP for the transport and firewall rules ensure that only the container
IPs are allowed to connect to the corresponding port for `garcon` that is open
in `vm_cicerone`.

## vsh

`vsh` is a vsock-based remote shell utility. `vshd` runs on the guest/server,
and `vsh` runs on the host/client. For now, this is only capable of launching
a shell on the VM itself, but this may later be extended to launch a shell in
a specific container inside of the VM.

## vm_syslog

`vm_syslog` is the syslog daemon that runs inside the VM.  It is automatically
started by maitred and provides a socket at `/dev/log` for applications to send
it log records.  `vm_syslog` aggregates the log records and then forwards them
outside the VM to the logging service running on the host.  The logging service
tags the records it receives with the unique identifier for the VM from which
the logs originated and then forwards them on to the host syslog service.  This
ensures that the VM logs are captured in any feedback reports that are uploaded
to Google's servers.

Additionally, `vm_syslog` reads kernel logs from `/dev/kmsg` (inside the VM)
and forwards those to the logging service running on the host.

See [docs/logging.md](docs/logging.md) for more details on log handling.

## crash_collector

`crash_collector` is responsible for collecting crash reports of applications
running inside the VM and forwarding them out to the crash collector service
running on the host system.  When `maitred` first starts up it configures
`/proc/sys/kernel/core_pattern` to start the `crash_collector` program and send
the core dump over a pipe to that program.  `crash_collector` then parses the
core dump and converts it to a minidump before sending it out to the host.
The host daemon passes the report on to `crash-reporter`, which takes care of
uploading it to Google servers.

## VM <-> host communication

All communication between `vm_concierge` and the applications inside the VM
happen over a [vsock](https://lwn.net/Articles/695981/) transport. The actual
RPC communication uses the [gRPC](http://grpc.io) framework. Every `maitred`
instance listens on a known port in the vsock namespace (port 8888).

See [docs/vsock.md](docs/vsock.md) for more details about vsock.

### Authentication

Since each `maitred` instance listens on a known port number, it is possible for
an application inside a VM to send a message to `maitred` over a loopback
interface.  To prevent this we block all loopback connections over vsock.

It is not possible for processes in different VMs to send messages to each other
over vsock.  This is blocked by the host kernel driver that manages data
transfer.

### Wire format

gRPC uses [protocol buffers](https://developers.google.com/protocol-buffers) as
the serialization format for messages sent over the vsock and IP transport.  The
[proto](proto/) directory holds the definitions for all the messages sent and
services provided between the host and the VM/container.
