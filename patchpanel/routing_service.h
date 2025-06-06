// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_ROUTING_SERVICE_H_
#define PATCHPANEL_ROUTING_SERVICE_H_

#include <arpa/inet.h>
#include <limits.h>
#include <stdint.h>
#include <sys/socket.h>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/stringprintf.h>
#include <base/types/cxx23_to_underlying.h>

#include "patchpanel/lifeline_fd_service.h"
#include "patchpanel/system.h"

namespace patchpanel {

// Constant used for establishing a stable mapping between routing table ids
// and interface indexes. An interface with ifindex 2 will be assigned the
// routing table with id 1002 by the routing layer. This stable mapping is used
// for configuring ip rules, iptables fwmark mangle rules, and the
// accept_ra_rt_table sysctl for all physical interfaces.
// TODO(b/161507671) Consolidate with shill::kInterfaceTableIdIncrement
// in platform2/shill/routing_table.cc once routing and ip rule configuration
// is migrated to patchpanel.
constexpr const int kInterfaceTableIdIncrement = 1000;

// The list of all sources of traffic that need to be distinguished
// for routing or traffic accounting. Currently 6 bits are used for encoding
// the TrafficSource enum in a fwmark. The enum is split into two groups:local
// sources and forwarded sources. The enum values of forwarded sources are
// offset by 0x20 so that their most significant bit is always set and can be
// easily matched separately from local sources.
enum TrafficSource {
  kUnknown = 0,

  // Local sources:
  // Traffic corresponding to uid "chronos".
  kChrome = 1,
  // Other uids classified as "user" for traffic purposes: debugd, cups,
  // tlsdate, pluginvm (Parallels), etc.
  kUser = 2,
  // Traffic from Update engine.
  kUpdateEngine = 3,
  // Other system traffic.
  kSystem = 4,
  // Traffic emitted on an underlying physical network by the built-in OpenVPN
  // and L2TP clients, or Chrome 3rd party VPN Apps. This traffic constitutes
  // the VPN tunnel.
  kHostVpn = 5,

  // Forwarded sources:
  // ARC++ and ARCVM.
  kArc = 0x20,
  // Crostini VMs and lxd containers.
  kCrostiniVM = 0x21,
  // Parallels VMs.
  kParallelsVM = 0x22,
  // A tethered downstream network.
  kTetherDownstream = 0x23,
  // Traffic emitted by Android VPNs for their tunnelled connections.
  kArcVpn = 0x24,
  // Bruschetta VMs.
  kBruschettaVM = 0x25,
  // Borealis VMs.
  kBorealisVM = 0x26,
  // WiFi Direct network.
  kWiFiDirect = 0x27,
  // WiFi local only hotspot network.
  kWiFiLOHS = 0x28,
};

// Possible policies for VPN routing available to a socket.
enum class VPNRoutingPolicy : uint8_t {
  // Let the routing layer apply the default policy for that process uid. This
  // is the default policy for newly created sockets.
  kDefault = 0,
  // The socket traffic is always routed through the VPN if there is one. Note
  // that the traffic will still be routed through physical network if the
  // destination is not included in VPN routes.
  kRouteOnVPN = 1,
  // The socket traffic is always routed through the physical network. Setting
  // this will also make the socket  this will bypass VPN lockdown mode.
  kBypassVPN = 2,
};

// The list of all possible socket traffic annotations. The source of truth is
// defined in system_api/traffic_annotation/traffic_annotation.proto.
enum class TrafficAnnotationId : uint8_t {
  // The traffic comes from an unspecified source.
  kUnspecified = 0,
  // The traffic comes from Shill's portal detector.
  kShillPortalDetector = 1,
  // The traffic comes from Shill CAPPORT client.
  kShillCapportClient = 2,
  // The traffic comes from Shill carrier entitlement.
  kShillCarrierEntitlement = 3,
};

// QoSCategory in fwmark indicates the inferred result from each QoS detector
// (e.g., WebRTC detector, ARC connection monitor). The final QoS decision
// (e.g., the DSCP value used in WiFi QoS) will be decided by QoSService.
// Currently 3 bits are used for encoding QoSCategory in a fwmark.
enum class QoSCategory : uint8_t {
  // Either unknown or uninteresting in terms of QoS.
  kDefault = 0,

  // The QoS category specified via the patchpanel API. Note that currently that
  // API will only be used by ARC++ connection monitor.
  kRealTimeInteractive = 1,
  kMultimediaConferencing = 2,

  // Network control traffics, e.g., TCP handshake packets, DNS packets.
  kNetworkControl = 3,

  // WebRTC traffic detected by the WebRTC detector.
  kWebRTC = 4,
};

std::string_view TrafficSourceName(TrafficSource source);

// Returns the "mark/mask" string for `category` which can be used as an
// argument to call iptables, e.g., "0x00000040/0x000000e0".
std::string QoSFwmarkWithMask(QoSCategory category);

// Returns the "mark/mask" string for `source` which can be used as an
// argument to call iptables, e.g., "0x00002400/0x00003f00".
std::string SourceFwmarkWithMask(TrafficSource source);

// A representation of how fwmark bits are split and used for tagging and
// routing traffic. The 32 bits of the fwmark are currently organized as such:
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |        routing table id       |VPN|source enum| QoS | rsvd. |*|
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// routing table id (16bits): the routing table id of a physical device managed
//                            by shill or of a virtual private network.
// VPN (2bits): policy bits controlled by host application to force VPN routing
//              or bypass VPN routing.
// source enum(6bits): policy bits controlled by patchpanel for grouping
//                     originated traffic by domain.
// QoS (3bits): the QoS category of the packet, used by QoSService.
// reserved(4bits): no usage at the moment.
// legacy SNAT(1bit): legacy bit used for setting up SNAT for ARC, Crostini, and
//                    Parallels VMs with iptables MASQUERADE.
//
// Note that bitfields are not a portable way to define a
// stable Fwmark. Also note that the in-memory representation of values of
// this union changes depending on endianness, so care must be taken when
// serializing or deserializing Fwmark values, or when aliasing with raw bytes
// through pointers. In practice client code should not rely on a specific
// memory representation and should instead use ToString() and Value().
union Fwmark {
  // Note that the memory layout of bit-field is implementation-defined. Since
  // the definition below does not straddle bytes, we should be able to get a
  // consistent behavior across platforms.
  struct {
    // The LSB is currently only used for applying IPv4 SNAT to egress traffic
    // from ARC and other VMs; indicated by a value of 1.
    uint8_t legacy : 5;
    // The QoS category for the packet. Used by QoS service.
    uint8_t qos_category : 3;
    // The 3rd byte is used to store the intent and policy to be applied to the
    // traffic. The first 2 bits are used for host processes to select a VPN
    // routing intent via patchpanel TagSocket API. The next 6 bits of are used
    // for tagging the traffic with a source.
    uint8_t policy;
    // The 2 upper bytes corresponds to the routing table id associated with
    // a shill device or a VPN.
    uint16_t rt_table_id;
  };
  // The raw memory representation of this fwmark as a uint32_t.
  uint32_t fwmark;

  // Returns a String representation of this Fwmark.
  std::string ToString() const { return base::StringPrintf("0x%08x", Value()); }

  // Returns the logical uint32_t value of this Fwmark.
  constexpr uint32_t Value() const {
    return static_cast<uint32_t>(rt_table_id) << 16 |
           static_cast<uint32_t>(policy) << 8 |
           static_cast<uint32_t>(qos_category) << 5 |
           static_cast<uint32_t>(legacy);
  }

  constexpr TrafficSource Source() {
    return static_cast<TrafficSource>(policy & 0x3f);
  }

  // Note: naming it as QoSCategory() will hide the definition of the enum,
  // which incurs a compile error.
  constexpr QoSCategory GetQoSCategory() {
    return static_cast<QoSCategory>(qos_category);
  }

  constexpr bool operator==(Fwmark that) const { return fwmark == that.fwmark; }

  constexpr Fwmark operator|(Fwmark that) const {
    return {.fwmark = fwmark | that.fwmark};
  }

  constexpr Fwmark operator&(Fwmark that) const {
    return {.fwmark = fwmark & that.fwmark};
  }

  constexpr Fwmark operator~() const { return {.fwmark = ~fwmark}; }

  static constexpr Fwmark FromSource(TrafficSource source) {
    return {.legacy = 0,
            .qos_category = 0,
            .policy = static_cast<uint8_t>(source),
            .rt_table_id = 0};
  }

  static constexpr std::optional<Fwmark> FromIfIndex(int ifindex) {
    int table_id = ifindex + kInterfaceTableIdIncrement;
    if (ifindex < 0 || table_id > INT16_MAX) {
      return std::nullopt;
    }
    return {{.legacy = 0,
             .qos_category = 0,
             .policy = 0,
             .rt_table_id = static_cast<uint16_t>(table_id)}};
  }

  static constexpr Fwmark FromQoSCategory(QoSCategory category) {
    return {.legacy = 0,
            .qos_category = base::to_underlying(category),
            .policy = 0,
            .rt_table_id = 0};
  }
};

// Specifies how the local traffic originating from a given source should be
// tagged in mangle OUTPUT. A source is either identified by a uid or by a
// cgroup classid identifier.
struct LocalSourceSpecs {
  TrafficSource source_type;
  const char* uid_name;
  uint32_t classid;
  bool is_on_vpn;
};

std::ostream& operator<<(std::ostream& stream, const LocalSourceSpecs& source);

// This block defines the names of uids whose traffic is always routed through a
// VPN connection. Chrome and nacl applications
constexpr char kUidChronos[] = "chronos";
// Crosh terminal and feedback reports
constexpr char kUidDebugd[] = "debugd";
// Printing
constexpr char kUidCups[] = "cups";
// Printer and print queues configuration utility used for cups
constexpr char kUidLpadmin[] = "lpadmin";
// Chrome OS printing and scanning daemon
constexpr char kUidPrintscanmgr[] = "printscanmgr";
// DNS proxy user with traffic that is routed through VPN
constexpr char kUidDnsProxyUser[] = "dns-proxy-user";
// Chrome OS Kerberos daemon
constexpr char kUidKerberosd[] = "kerberosd";
// Kerberos third party untrusted code
constexpr char kUidKerberosdExec[] = "kerberosd-exec";
// While tlsdate is not user traffic, time sync should be attempted over
// VPN. It is OK to send tlsdate traffic over VPN because it will also try
// to sync time immediately after boot on the sign-in screen when no VPN can
// be active.
constexpr char kUidTlsdate[] = "tlsdate";
// Parallels VM problem report utility (b/160916677)
constexpr char kUidPluginvm[] = "pluginvm";
// smbfs SMB filesystem daemon
constexpr char kUidFuseSmbfs[] = "fuse-smbfs";

// The list of all local sources to tag in mangle OUTPUT with the VPN intent
// bit, or with a source tag, or with both. This arrays specifies: 1) the source
// type, 2) the uid name of the source or empty cstring if none is defined (the
// cstring must be defined and cannot be null), 3) the cgroup classid of the
// source (or 0 if none is defined), and 4) if the traffic originated from that
// source should be routed through VPN connections by default or not.
constexpr std::array<LocalSourceSpecs, 12> kLocalSourceTypes{{
    {TrafficSource::kChrome, kUidChronos, 0, true},
    {TrafficSource::kUser, kUidDebugd, 0, true},
    {TrafficSource::kUser, kUidCups, 0, true},
    {TrafficSource::kUser, kUidLpadmin, 0, true},
    {TrafficSource::kUser, kUidPrintscanmgr, 0, true},
    {TrafficSource::kUser, kUidDnsProxyUser, 0, true},
    {TrafficSource::kSystem, kUidKerberosd, 0, true},
    {TrafficSource::kSystem, kUidKerberosdExec, 0, true},
    {TrafficSource::kSystem, kUidTlsdate, 0, true},
    {TrafficSource::kUser, kUidPluginvm, 0, true},
    {TrafficSource::kSystem, kUidFuseSmbfs, 0, true},
    // The classid value for update engine must stay in sync with
    // src/aosp/system/update_engine/init/update-engine.conf.
    {TrafficSource::kUpdateEngine, "", 0x10001, false},
}};

// All local sources
constexpr std::array<TrafficSource, 5> kLocalSources{
    {kChrome, kUser, kUpdateEngine, kSystem, kHostVpn}};

// All forwarded sources
constexpr std::array<TrafficSource, 9> kForwardedSources{
    {kArc, kBorealisVM, kBruschettaVM, kCrostiniVM, kParallelsVM,
     kTetherDownstream, kWiFiDirect, kWiFiLOHS, kArcVpn}};

// All sources
constexpr std::array<TrafficSource, 14> kAllSources{
    {kChrome, kUser, kUpdateEngine, kSystem, kHostVpn, kArc, kBorealisVM,
     kBruschettaVM, kCrostiniVM, kParallelsVM, kTetherDownstream, kWiFiDirect,
     kWiFiLOHS, kArcVpn}};

// All sources for user traffic. For VPN drivers that only want to pass traffic
// for specific users, these are the usernames that will be used to create the
// routing policy rules. Also, when an AlwaysOnVpnPackage is set and a
// corresponding VPN service is not active, traffic from these users will
// blackholed. Currently the "user traffic" as defined by these usernames does
// not include e.g. Android apps or system processes like the update engine.
constexpr std::array<std::string_view, 11> kUserTrafficUsernames = {
    kUidChronos, kUidDebugd,       kUidDnsProxyUser, kUidCups,
    kUidLpadmin, kUidPrintscanmgr, kUidKerberosd,    kUidKerberosdExec,
    kUidTlsdate, kUidPluginvm,     kUidFuseSmbfs,
};

// Constant fwmark value for tagging traffic with the "route-on-vpn" intent.
constexpr const Fwmark kFwmarkRouteOnVpn = {.policy = 0x80};
// Constant fwmark value for tagging traffic with the "bypass-vpn" intent.
constexpr const Fwmark kFwmarkBypassVpn = {.policy = 0x40};
// constexpr const Fwmark kFwmarkVpnMask = kFwmarkRouteOnVpn | kFwmarkBypassVpn;
constexpr const Fwmark kFwmarkVpnMask = {.policy = 0xc0};
// A mask for matching fwmarks on the routing table id.
constexpr const Fwmark kFwmarkRoutingMask = {.rt_table_id = 0xffff};
// A mask for matching fwmarks on the source.
constexpr const Fwmark kFwmarkAllSourcesMask = {.policy = 0x3f};
// A mast for matching fwmarks of forwarded sources.
constexpr const Fwmark kFwmarkForwardedSourcesMask = {.policy = 0x20};
// A mask for matching fwmarks on the policy byte.
constexpr const Fwmark kFwmarkPolicyMask = {.policy = 0xff};
// Both the mask and fwmark values for legacy SNAT rules used for ARC and other
// containers.
constexpr const Fwmark kFwmarkLegacySNAT = {.legacy = 0x1};
// Constant fmwark value for mask for the QoS category bits.
constexpr const Fwmark kFwmarkQoSCategoryMask = {.qos_category = 0x7};

// Service implementing routing features of patchpanel.
// TODO(hugobenichi) Explain how this coordinates with shill's RoutingTable.
class RoutingService {
 public:
  RoutingService(System* system, LifelineFDService* lifeline_fd_service);
  RoutingService(const RoutingService&) = delete;
  RoutingService& operator=(const RoutingService&) = delete;
  virtual ~RoutingService();

  // Sets the routing tag and VPN bits of the fwmark for the given socket
  // according to the input parameters. Preserves any other bits of the fwmark
  // already set.
  // TODO(b/331744250): |annotation_id| is ignored for now.
  virtual bool TagSocket(int sockfd,
                         std::optional<int> network_id,
                         VPNRoutingPolicy vpn_policy,
                         std::optional<TrafficAnnotationId> annotation_id);

  // Sets the fwmark on the given socket with the given mask.
  // Preserves any other bits of the fwmark already set.
  bool SetFwmark(int sockfd, Fwmark mark, Fwmark mask);

  // Allocates a new unique network id. Network id values assigned with this
  // function do not need to be returned or freed and are never reused. If the
  // operation that requested a network id fails, the network id can simply be
  // discarded.
  virtual int AllocateNetworkID();
  // Assigns the interface |ifname| to the network id |network_id|. An interface
  // cannot be assigned to two network ids at the same time. Currently
  // patchpanel also only supports a single interface by network id. Returns
  // true if the assignment was successful.
  virtual bool AssignInterfaceToNetwork(int network_id,
                                        std::string_view ifname,
                                        base::ScopedFD client_fd);
  // Forgets any network interface assignment to |network_id|.
  virtual void ForgetNetworkID(int network_id);
  // Returns the interface assigned to |network_id| if any.
  const std::string* GetInterface(int network_id) const;
  // Returns the routing Fwmark of the interface assigned to |network_id| if
  // any.
  std::optional<Fwmark> GetRoutingFwmark(int network_id) const;
  // Returns the network id to which |ifname| is assigned, or nullopt otherwise.
  std::optional<int> GetNetworkID(std::string_view ifname) const;
  // Returns all network ids with a network interface assigned.
  std::vector<int> GetNetworkIDs() const;

 protected:
  // Can be overridden in tests.
  virtual int GetSockopt(
      int sockfd, int level, int optname, void* optval, socklen_t* optlen);
  virtual int SetSockopt(
      int sockfd, int level, int optname, const void* optval, socklen_t optlen);

 private:
  // Owned by PatchpanelDaemon.
  System* system_;
  // Owner by Manager
  LifelineFDService* lifeline_fd_svc_;

  // Monotonically increasing counter for assigning unique network ids.
  int next_network_id_ = 1;
  // Bidirectional maps of all network ids currently with a network interface
  // assignment.
  std::map<int, std::string> network_ids_to_interfaces_;
  std::map<std::string, int, std::less<>> interfaces_to_network_ids_;

  // Map of scoped closures for automatically releasing lifeline FDs registered
  // to |lifeline_fd_svc_|, keyed by network id.
  std::map<int, base::ScopedClosureRunner> cancel_lifeline_fds_;

  base::WeakPtrFactory<RoutingService> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_ROUTING_SERVICE_H_
