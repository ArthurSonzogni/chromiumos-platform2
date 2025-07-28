# VPN

## Overview

From the perspective of Shill's architecture, VPN is inherently different from
physical connections because the corresponding `Device` (in this case
representing a virtual interface) may not exist when a Connect is
requested. Therefore the standard means of a `Service` passing Connect requests
over to its corresponding `Device` does not work. Also, since the `VirtualDevice`
class is a general-purpose one used by other connection types like Cellular,
the VPN-specific logic cannot be contained within the `VirtualDevice` instance
itself.

For VPN, this is solved through the use of `VPNDrivers`. A `VPNDriver` takes
care of attaining a proper `VirtualDevice`, communicating with processes outside
of Shill which implement some part of the VPN functionality, and setting up
routes and routing rules for the corresponding `VirtualDevice`. Thus a
`VPNService` passes Connect and Disconnect requests to its corresponding
`VPNDriver`. Note that `VPNDriver` D-Bus properties are exposed through the
owning `VPNService`; `VPNDrivers` are an implementation detail that is not
exposed to D-Bus clients.

ChromeOS supports 6 types of VPN solutions:
*   Android 3rd-party VPN apps in ARC
*   Built-in IKEv2 VPN
*   Built-in L2TP/IPsec VPN
*   Built-in OpenVPN
*   Built-in WireGuard VPN
*   Chrome Extension VPN App

Each of these types has a corresponding `VPNDriver` child which contains the
functionality needed on the Shill-side to support that VPN solution (note that
Shill's involvement varies between different types of VPNs).

When a `VPNService` is created by `VPNProvider` (whether from a `Manager`
ConfigureService D-Bus call or from a `Profile` containing an already-configured
`VPNService`), the "Provider.Type" `Service` property is used to specify what type
of `VPNDriver` that `VPNService` should use. Note that "Provider.Type" is only
valid for `Services` whose "Type" property is of value "vpn". See
`VPNProvider::CreateServiceInner` for more details.

## VPN Types

### Android 3rd-party VPN in ARC

Android 3rd-party VPNs (implemented using the [Android `VpnService` API] in ARC
are the VPN type requiring the least amount of functionality within Shill, where
the majority of the `ArcVpnDriver` functionality is just setting up routing
properly. patchpanel creates an ARC bridge, which serves as a host-side (v4
NAT-ed) proxy for the arc0 interface on the Android-side. In addition,
patchpanel creates a corresponding arc_${IFNAME} interface for each interface
named ${IFNAME} exposed by the Shill `Manager` (see patchpanel
`Manager::OnShillDevicesChanged` for more detail). This allows traffic from the
Android-side to have a specific host-side interface that will carry it.

Traffic that needs to pass through the VPN gets sent to the ARC bridge rather
than out of a physical interface. VPN-tunnelled traffic will then be sent out of
Android to arc_${IFNAME} interfaces to actually send the traffic out of the
system.

Internally, Chrome's [`ArcNetHostImpl`] and the ARC [`ArcNetworkBridge`]
communicate between each other to create the appropriate behavior as specified
by the [ARC net.mojom interface]. For example, the ARC [`VpnTracker`] will
trigger `ArcNetworkBridge.androidVpnConnected` when an Android VPN
connects. This triggers `ArcNetHostImpl::AndroidVpnConnected` on the
Chrome-side, which will Connect the appropriate `VpnService` in Shill, first
configuring a new `VpnService` in Shill if needed.

### Built-in IKEv2 VPN

The built-in IKEv2 VPN is implemented in the `IKEv2Driver`. It relies on the
`IPsecConnection` class to establish a connection with the remote server using
the [strongSwan](https://www.strongswan.org) (charon) daemon.

Upon a connect request, the `IKEv2Driver` creates an `IPsecConnection` object,
which is responsible for managing the lifecycle of the `charon` process.
Configuration properties from the `VPNService` (e.g., server address,
authentication parameters) are passed to the `IPsecConnection`. The driver
supports authentication via Pre-Shared Key (PSK), user certificate, or EAP
(currently only MS-CHAPv2). Once the IKEv2 negotiation is complete and the
tunnel is established, `IPsecConnection` notifies the driver, which then sets
up the virtual network interface with the IP configuration received from the
server.

### Built-in L2TP/IPsec VPN

The built-in L2TP/IPsec VPN is implemented within shill by the `L2TPIPsecDriver`.
This driver coordinates two main components: an `IPsecConnection` for the outer
IPsec tunnel and an `L2TPConnection` for the inner L2TP tunnel.

Upon a connect request, `L2TPIPsecDriver` first resolves the VPN server hostname
and then creates and starts an `IPsecConnection`. This object manages the
[strongSwan](https://www.strongswan.org) (charon) process to establish the IKEv1
IPsec tunnel. Once the IPsec tunnel is up, `IPsecConnection` starts the
`L2TPConnection` object. This in turn spawns and manages an
[xl2tpd](https://linux.die.net/man/8/xl2tpd) process to create the inner L2TP
tunnel over the IPsec transport. `xl2tpd` itself uses `pppd` to establish the
PPP link. When the PPP link is ready, the driver is notified with the new
interface details and configures the connection using the network parameters
from the server.

>   Note: There are actually two distinct L2TP standards (distinguished as
>   L2TPv2 and L2TPv3). [RFC 2661] defines L2TPv2, which is a protocol
>   specifically designed for the tunnelling of PPP traffic. [RFC 3931]
>   generalizes L2TPv2 such that the assumption of the L2 protocol being PPP is
>   removed. L2TP/IPsec is described in [RFC 3193], which--as the RFC numbers
>   might suggest--is based on L2TPv2. In particular, xl2tpd is an
>   implementation of RFC 2661, and all references to L2TP in Shill are
>   specifically referencing L2TPv2.

### Built-in OpenVPN

The built-in OpenVPN implementation consists primarily of the open-source
[OpenVPN](https://openvpn.net) project, and of Shill's `OpenVPNDriver` and
`OpenVPNManagementServer`. Upon a Connect request, `OpenVPNDriver` creates a TUN
interface and spawns an `openvpn` process, passing a set of command-line options
including the interface name of the created TUN interface (using the OpenVPN
"dev" option). Shill interacts with the spawned `openvpn` process in two
distinct ways.

One interaction is between `openvpn` and `OpenVPNDriver::Notify`. The OpenVPN
"up" and "up-restart" options are set so that the [shill
openvpn_script](../shims/openvpn_script.cc) is called when `openvpn` first opens
the TUN interface *and* whenever `openvpn` restarts. This script leads to
`OpenVPNDriver::Notify` being invoked (through OpenVPNDriver::rpc_task_), which
will process environment variables passed by `openvpn` in order to populate an
`net_base::NetworkConfig` instance appropriately.

>   Note: From the OpenVPN documentation:
>   >   On restart, OpenVPN will not pass the full set of environment variables
>   >   to the script. Namely, everything related to routing and gateways will
>   >   not be passed, as nothing needs to be done anyway – all the routing
>   >   setup is already in place.

The other interaction is between `openvpn` and `OpenVPNManagementServer`.
OpenVPN provides the concept of a management server, which is an entity external
to the `openvpn` process which provides administrative control. Communication
between the `openvpn` and management server processes occurs either over TCP or
unix domain sockets. In this case, `OpenVPNManagementServer` uses a TCP socket
over 127.0.0.1 to communicate with the OpenVPN client. This allows for Shill to
control `openvpn` behavior like holds (keeping `openvpn` hibernated until the
hold is released) and restarts (triggered by sending "signal SIGUSR1" over the
socket), but also allows for `openvpn` to send information like state changes
and failure events back over to Shill (see
`OpenVPNManagementServer::OnInput`). To clarify, the communication between
`OpenVPNManagementServer` and `openvpn` is an out-of-band control channel; since
`openvpn` already has the TUN interface opened, the Shill-side is not involved
with processing data packets themselves.

### Built-in WireGuard VPN

The built-in WireGuard VPN is implemented in the `WireGuardDriver` and requires
kernel support (available on kernel version 5.4+). Unlike other VPN types, it
does not involve a long-running userspace daemon for the data path. Instead, it
configures the in-kernel WireGuard module.

Upon a connect request, the `WireGuardDriver` first requests the creation of a
`wireguard` type network interface from `DeviceInfo`. Once the interface is
ready, the driver generates a configuration file in memory based on the service
properties (private key, peer public keys, allowed IPs, etc.) and pipes it to
the `wg setconf` command from the `wireguard-tools` package. This command
configures the kernel interface with the specified cryptographic keys, peer
information, and routing rules. After the configuration is applied, the driver
populates the `NetworkConfig` with the local IP addresses and routes defined in
the service properties and notifies the service that it is connected. The driver
also periodically runs `wg show` to update link statistics.

### Chrome Extension VPN App

`ThirdPartyVpnDriver` exposes the [Shill ThirdPartyVpn API] through
`ThirdPartyVpnDBusAdaptor`, which [Chrome `VpnService`] instances use, such that
Chrome VPN App information can be passed between Shill and Chrome. Chrome's
`VpnService` is wrapped by `VpnThreadExtensionFunction` children to create the
[Chrome vpnProvider API] for Chrome Apps.

When the Shill `VpnService` receives a Connect call, the `ThirdPartyVpnDriver`
will create a TUN interface where packets received on the interface reach
`ThirdPartyVpnDriver::OnInput` as a vector of bytes. Within `OnInput`, IPv4
packets are sent using the OnPacketReceived D-Bus signal, which Chrome's
`VpnService` will forward to the Chrome VPN App. In the other direction, Chrome
VPN Apps use the SendPacket vpnProvider function to cause its Chrome
`VpnService` to call the SendPacket D-Bus method on the corresponding
`ThirdPartyVpnDriver` in Shill, which causes the driver to send that packet to
the TUN interface. Understandably, the performance of Chrome App VPNs is not
optimal, but the performance drawbacks of this design are embedded in the
ThirdPartyVpn and Chrome vpnProvider APIs, as opposed to being hidden
implementation details. One can contrast this with how built-in OpenVPN above
works, where the TUN interface is passed to `openvpn` so that the Shill <->
external VPN entity communication is exclusively a control channel rather than
both a control and data channel.

[Android `VpnService` API]: https://developer.android.com/reference/android/net/VpnService
[ARC net.mojom interface]: https://cs.chromium.org/chromium/src/components/arc/mojom/net.mojom
[`ArcNetHostImpl`]: https://cs.chromium.org/chromium/src/components/arc/net/arc_net_host_impl.h
[`ArcNetworkBridge`]: https://source.corp.google.com/rvc-arc/vendor/google_arc/libs/arc-net-services/src/com/android/server/arc/net/ArcNetworkBridge.java
[Chrome `VpnService`]: https://cs.chromium.org/chromium/src/extensions/browser/api/vpn_provider/vpn_service.h
[Chrome vpnProvider API]: https://developer.chrome.com/apps/vpnProvider
[RFC 2661]: https://tools.ietf.org/html/rfc2661
[RFC 3193]: https://tools.ietf.org/html/rfc3193
[RFC 3931]: https://tools.ietf.org/html/rfc3931
[Shill ThirdPartyVpn API]: thirdpartyvpn-api.txt
[`VpnTracker`]: https://cs.corp.google.com/pi-arcvm-dev/vendor/google_arc/libs/arc-services/src/com/android/server/arc/net/VpnTracker.java
