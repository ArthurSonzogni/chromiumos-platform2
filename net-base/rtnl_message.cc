// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/rtnl_message.h"

// clang-format off
#include <net/if.h>     // NB: order matters; this conflicts with <linux/if.h>
// clang-format on
#include <arpa/inet.h>  // NOLINT(build/include_alpha)
#include <linux/fib_rules.h>
#include <linux/if_addr.h>
#include <linux/if_arp.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/notimplemented.h>
#include <base/notreached.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "net-base/byte_utils.h"
#include "net-base/http_url.h"
#include "net-base/ipv6_address.h"

namespace net_base {
namespace {

// Defined in the kernel at include/net/ndisc.h and not exposed in user space
// headers. Neighbor Discovery user option type definition.
#define ND_OPT_RDNSS 25          /* RFC 5006 */
#define ND_OPT_DNSSL 31          /* RFC 6106 */
#define ND_OPT_CAPTIVE_PORTAL 37 /* RFC 8910 */
#define ND_OPT_PREF64 38         /* RFC 8781 */

using flag_info_t = std::pair<uint32_t, const char*>;

// Helper for pretty printing flags
template <std::size_t Dim>
std::string PrintFlags(uint32_t flags,
                       const std::array<flag_info_t, Dim>& flags_info,
                       const std::string& separator = " | ") {
  std::string str = "";
  if (flags == 0) {
    return str;
  }
  std::string sep = "";
  for (size_t i = 0; i < Dim; i++) {
    if ((flags & flags_info[i].first) == 0) {
      continue;
    }
    str += sep;
    str += flags_info[i].second;
    sep = separator;
  }
  return str;
}

// Flag names for Address events (ifa_flags field of struct ifaddrmsg). Defined
// in uapi/linux/if_addr.h
constexpr std::array<flag_info_t, 12> kIfaFlags{{
    {IFA_F_TEMPORARY, "TEMPORARY"},
    {IFA_F_NODAD, "NODAD"},
    {IFA_F_OPTIMISTIC, "OPTIMISTIC"},
    {IFA_F_DADFAILED, "DADFAILED"},
    {IFA_F_HOMEADDRESS, "HOMEADDRESS"},
    {IFA_F_DEPRECATED, "DEPRECATED"},
    {IFA_F_TENTATIVE, "TENTATIVE"},
    {IFA_F_PERMANENT, "PERMANENT"},
    {IFA_F_MANAGETEMPADDR, "MANAGETEMPADDR"},
    {IFA_F_NOPREFIXROUTE, "NOPREFIXROUTE"},
    {IFA_F_MCAUTOJOIN, "MCAUTOJOIN"},
    {IFA_F_STABLE_PRIVACY, "STABLE_PRIVACY"},
}};

// Flag names for Link events (ifi_flags field of struct ifinfomsg). Defined in
// uapi/linux/if.h
constexpr std::array<flag_info_t, 19> kNetDeviceFlags = {{
    {IFF_ALLMULTI, "ALLMULTI"},
    {IFF_AUTOMEDIA, "AUTOMEDIA"},
    {IFF_BROADCAST, "BROADCAST"},
    {IFF_DEBUG, "DEBUG"},
    {IFF_DORMANT, "DORMANT"},
    {IFF_DYNAMIC, "DYNAMIC"},
    {IFF_ECHO, "ECHO"},
    {IFF_LOOPBACK, "LOOPBACK"},
    {IFF_LOWER_UP, "LOWER_UP"},
    {IFF_MASTER, "MASTER"},
    {IFF_MULTICAST, "MULTICAST"},
    {IFF_NOARP, "NOARP"},
    {IFF_NOTRAILERS, "NOTRAILERS"},
    {IFF_POINTOPOINT, "POINTOPOINT"},
    {IFF_PORTSEL, "PORTSEL"},
    {IFF_PROMISC, "PROMISC"},
    {IFF_RUNNING, "RUNNING"},
    {IFF_SLAVE, "SLAVE"},
    {IFF_UP, "UP"},
}};

// Returns the name associated with the give |ifi_type| corresponding to the
// ifi_type field of a struct ifinfomsg LINK message. The possible type values
// are defined in uapi/linux/if_arp.h.
std::string GetNetDeviceTypeName(unsigned int ifi_type) {
  switch (ifi_type) {
    case ARPHRD_NETROM:
      return "NETROM";
    case ARPHRD_ETHER:
      return "ETHER";
    case ARPHRD_EETHER:
      return "EETHER";
    case ARPHRD_AX25:
      return "AX25";
    case ARPHRD_PRONET:
      return "PRONET";
    case ARPHRD_CHAOS:
      return "CHAOS";
    case ARPHRD_IEEE802:
      return "IEEE802";
    case ARPHRD_ARCNET:
      return "ARCNET";
    case ARPHRD_APPLETLK:
      return "APPLETLK";
    case ARPHRD_DLCI:
      return "DLCI";
    case ARPHRD_ATM:
      return "ATM";
    case ARPHRD_METRICOM:
      return "METRICOM";
    case ARPHRD_IEEE1394:
      return "IEEE1394";
    case ARPHRD_EUI64:
      return "EUI64";
    case ARPHRD_INFINIBAND:
      return "INFINIBAND";
    case ARPHRD_SLIP:
      return "SLIP";
    case ARPHRD_CSLIP:
      return "CSLIP";
    case ARPHRD_SLIP6:
      return "SLIP6";
    case ARPHRD_CSLIP6:
      return "CSLIP6";
    case ARPHRD_RSRVD:
      return "RSRVD";
    case ARPHRD_ADAPT:
      return "ADAPT";
    case ARPHRD_ROSE:
      return "ROSE";
    case ARPHRD_X25:
      return "X25";
    case ARPHRD_HWX25:
      return "HWX25";
    case ARPHRD_CAN:
      return "CAN";
    case ARPHRD_PPP:
      return "PPP";
    case ARPHRD_CISCO:
      return "CISCO";  // also ARPHRD_HDLC
    case ARPHRD_LAPB:
      return "LAPB";
    case ARPHRD_DDCMP:
      return "DDCMP";
    case ARPHRD_RAWHDLC:
      return "RAWHDLC";
    case ARPHRD_RAWIP:
      return "RAWIP";
    case ARPHRD_TUNNEL:
      return "TUNNEL";
    case ARPHRD_TUNNEL6:
      return "TUNNEL6";
    case ARPHRD_FRAD:
      return "FRAD";
    case ARPHRD_SKIP:
      return "SKIP";
    case ARPHRD_LOOPBACK:
      return "LOOPBACK";
    case ARPHRD_LOCALTLK:
      return "LOCALTLK";
    case ARPHRD_FDDI:
      return "FDDI";
    case ARPHRD_BIF:
      return "BIF";
    case ARPHRD_SIT:
      return "SIT";
    case ARPHRD_IPDDP:
      return "IPDDP";
    case ARPHRD_IPGRE:
      return "IPGRE";
    case ARPHRD_PIMREG:
      return "PIMREG";
    case ARPHRD_HIPPI:
      return "HIPPI";
    case ARPHRD_ASH:
      return "ASH";
    case ARPHRD_ECONET:
      return "ECONET";
    case ARPHRD_IRDA:
      return "IRDA";
    case ARPHRD_FCPP:
      return "FCPP";
    case ARPHRD_FCAL:
      return "FCAL";
    case ARPHRD_FCPL:
      return "FCPL";
    case ARPHRD_FCFABRIC:
      return "FCFABRIC";
    case ARPHRD_IEEE802_TR:
      return "IEEE802_TR";
    case ARPHRD_IEEE80211:
      return "IEEE80211";
    case ARPHRD_IEEE80211_PRISM:
      return "IEEE80211_PRISM";
    case ARPHRD_IEEE80211_RADIOTAP:
      return "IEEE80211_RADIOTAP";
    case ARPHRD_IEEE802154:
      return "IEEE802154  ";
    case ARPHRD_IEEE802154_MONITOR:
      return "IEEE802154_MONITOR";
    case ARPHRD_PHONET:
      return "PHONET";
    case ARPHRD_PHONET_PIPE:
      return "PHONET_PIPE";
    case ARPHRD_CAIF:
      return "CAIF";
    case ARPHRD_IP6GRE:
      return "IP6GRE";
    case ARPHRD_NETLINK:
      return "NETLINK";
    case ARPHRD_6LOWPAN:
      return "6LOWPAN";
    case ARPHRD_VSOCKMON:
      return "VSOCKMON";
    case ARPHRD_VOID:
      return "VOID";
    case ARPHRD_NONE:
      return "NONE";
    default:
      return std::to_string(ifi_type);
  }
}

// Returns the name associated with the give |rtm_type| corresponding to the
// rtm_type field of a struct rtmsg ROUTE message. The possible type values
// are defined in uapi/linux/rtnetlink.h.
std::string GetRouteTypeName(uint8_t rtm_type) {
  switch (rtm_type) {
    case RTN_UNSPEC:
      return "UNSPEC";
    case RTN_UNICAST:
      return "UNICAST";
    case RTN_LOCAL:
      return "LOCAL";
    case RTN_BROADCAST:
      return "BROADCAST";
    case RTN_ANYCAST:
      return "ANYCAST";
    case RTN_MULTICAST:
      return "MULTICAST";
    case RTN_BLACKHOLE:
      return "BLACKHOLE";
    case RTN_UNREACHABLE:
      return "UNREACHABLE";
    case RTN_PROHIBIT:
      return "PROHIBIT";
    case RTN_THROW:
      return "THROW";
    case RTN_NAT:
      return "NAT";
    case RTN_XRESOLVE:
      return "XRESOLVE";
    default:
      return std::to_string(rtm_type);
  }
}

// Helper function to return route protocol names defined by the kernel.
// User reserved protocol values are returned as decimal numbers.
// Route protocols. Defined in uapi/linux/rtnetlink.h
std::string GetRouteProtocol(uint8_t protocol) {
  switch (protocol) {
    case RTPROT_UNSPEC:
      return "UNSPEC";
    case RTPROT_REDIRECT:
      return "REDIRECT";
    case RTPROT_KERNEL:
      return "KERNEL";
    case RTPROT_BOOT:
      return "BOOT";
    case RTPROT_STATIC:
      return "STATIC";
    case RTPROT_GATED:
      return "GATED";
    case RTPROT_RA:
      return "RA";
    case RTPROT_MRT:
      return "MRT";
    case RTPROT_ZEBRA:
      return "ZEBRA";
    case RTPROT_BIRD:
      return "BIRD";
    case RTPROT_DNROUTED:
      return "DNROUTED";
    case RTPROT_XORP:
      return "XORP";
    case RTPROT_NTK:
      return "NTK";
    case RTPROT_DHCP:
      return "DHCP";
    case RTPROT_MROUTED:
      return "MROUTED";
    case RTPROT_BABEL:
      return "BABEL";
    // The following protocols are not defined on Linux 4.14
    case 186 /* RTPROT_BGP */:
      return "BGP";
    case 187 /* RTPROT_ISIS */:
      return "ISIS";
    case 188 /* RTPROT_OSPF */:
      return "OSPF";
    case 189 /* RTPROT_RIP */:
      return "RIP";
    case 192 /* RTPROT_EIGRP */:
      return "EIGRP";
    default:
      return std::to_string(protocol);
  }
}

// Returns the name associated with the given |rule_rtm_type| routing rule
// action type corresponding to the rtm_type field of a struct rtmsg message.
// The possible rule action values are defined in  uapi/linux/fib_rules.h. The
// struct fib_rule_hdr in uapi/linux/fib_rules.h such that it aligns with the
// |rtm_type| field of struct rtmsg defined in uapi/linux/rtnetlink.h.
std::string GetRuleActionName(uint16_t rule_rtm_type) {
  switch (rule_rtm_type) {
    case FR_ACT_UNSPEC:
      return "UNSPEC";
    case FR_ACT_TO_TBL:
      return "TO_TBL";
    case FR_ACT_GOTO:
      return "GOTO";
    case FR_ACT_NOP:
      return "NOP";
    case FR_ACT_RES3:
      return "RES3";
    case FR_ACT_RES4:
      return "RES4";
    case FR_ACT_BLACKHOLE:
      return "BLACKHOLE";
    case FR_ACT_UNREACHABLE:
      return "UNREACHABLE";
    case FR_ACT_PROHIBIT:
      return "PROHIBIT";
    default:
      return std::to_string(rule_rtm_type);
  }
}

std::unique_ptr<RTNLAttrMap> ParseAttrs(const struct rtattr* data,
                                        size_t attr_len) {
  const auto* attr_data = reinterpret_cast<const char*>(data);
  if (attr_len > INT_MAX) {
    LOG(ERROR) << "Buffer length " << attr_len << " is over INT_MAX";
    return nullptr;
  }
  int len = static_cast<int>(attr_len);

  RTNLAttrMap attrs;
  while (data && RTA_OK(data, len)) {
    attrs[data->rta_type] = {
        reinterpret_cast<uint8_t*>(RTA_DATA(data)),
        reinterpret_cast<uint8_t*>(RTA_DATA(data)) + RTA_PAYLOAD(data)};
    // Note: RTA_NEXT() performs subtraction on 'len'. It's important that
    // 'len' is a signed integer, so underflow works properly.
    data = RTA_NEXT(data, len);
  }

  if (len) {
    LOG(ERROR) << "Error parsing RTNL attributes <"
               << base::HexEncode(attr_data, attr_len)
               << ">, trailing length: " << len;
    return nullptr;
  }

  return std::make_unique<RTNLAttrMap>(attrs);
}

// Returns the interface name for the device with interface index |ifindex|, or
// returns an empty string if it fails to find the interface.
std::string IndexToName(unsigned int ifindex) {
  char buf[IFNAMSIZ] = {};
  if_indextoname(ifindex, buf);
  return std::string(buf);
}

}  // namespace

struct RTNLHeader {
  RTNLHeader() { memset(this, 0, sizeof(*this)); }
  struct nlmsghdr hdr;
  union {
    struct ifinfomsg ifi;
    struct ifaddrmsg ifa;
    struct rtmsg rtm;
    struct nduseroptmsg nd_user_opt;
    struct ndmsg ndm;
  };
};

// Neighbor Discovery user option header definition.
struct NDUserOptionHeader {
  uint8_t type;
  uint8_t length;
} __attribute__((__packed__));

std::string RTNLMessage::NeighborStatus::ToString() const {
  return base::StringPrintf("NeighborStatus state %d flags %X type %d", state,
                            flags, type);
}

// |lifetime| is unsigned. Printing as signed so that infinity (0xfffffff) get
// printed as -1. Same below.
std::string RTNLMessage::RdnssOption::ToString() const {
  return base::StringPrintf("RdnssOption lifetime %d",
                            static_cast<int>(lifetime));
}

std::string RTNLMessage::DnsslOption::ToString() const {
  // b/408883419: domain names can constitute PIIs and should not be printed
  // directly.
  return base::StrCat({"DnsslOption lifetime: ", base::NumberToString(lifetime),
                       "s, domains: ", base::NumberToString(domains.size())});
}

std::string RTNLMessage::NdUserOption::ToString() const {
  return base::StringPrintf("NdUserOption type %u", type);
}

// static
std::vector<uint8_t> RTNLMessage::PackAttrs(const RTNLAttrMap& attrs) {
  std::vector<uint8_t> attributes;

  for (const auto& pair : attrs) {
    size_t len = RTA_LENGTH(pair.second.size());
    struct rtattr rt_attr = {
        // Linter discourages 'unsigned short', but 'unsigned short' is used in
        // the UAPI.
        static_cast<unsigned short>(len),  // NOLINT(runtime/int)
        pair.first,
    };
    std::vector<uint8_t> header =
        net_base::byte_utils::ToBytes<struct rtattr>(rt_attr);
    header.resize(RTA_ALIGN(header.size()), 0);
    attributes.insert(attributes.end(), header.begin(), header.end());

    std::vector<uint8_t> data = pair.second;
    data.resize(RTA_ALIGN(data.size()), 0);
    attributes.insert(attributes.end(), data.begin(), data.end());
  }

  return attributes;
}

RTNLMessage::RTNLMessage(Type type,
                         Mode mode,
                         uint16_t flags,
                         uint32_t seq,
                         uint32_t pid,
                         int32_t interface_index,
                         sa_family_t family)
    : type_(type),
      mode_(mode),
      flags_(flags),
      seq_(seq),
      pid_(pid),
      interface_index_(interface_index),
      family_(family) {}

// static
std::unique_ptr<RTNLMessage> RTNLMessage::Decode(
    base::span<const uint8_t> data) {
  // Parse the nlmsghdr, and trim the data to the size |nlmsg_len|.
  if (data.size() < sizeof(struct nlmsghdr)) {
    return nullptr;
  }
  const struct nlmsghdr* header =
      reinterpret_cast<const struct nlmsghdr*>(data.data());
  if (data.size() < header->nlmsg_len) {
    return nullptr;
  }
  data = data.first(static_cast<size_t>(header->nlmsg_len));

  // Split the remaining data after the header.
  if (data.size() < NLMSG_HDRLEN) {
    return nullptr;
  }
  const base::span<const uint8_t> payload = data.subspan<NLMSG_HDRLEN>();

  Mode mode = kModeUnknown;
  switch (header->nlmsg_type) {
    case RTM_NEWLINK:
    case RTM_NEWADDR:
    case RTM_NEWROUTE:
    case RTM_NEWRULE:
    case RTM_NEWNDUSEROPT:
    case RTM_NEWNEIGH:
    case RTM_NEWPREFIX:
      mode = kModeAdd;
      break;

    case RTM_DELLINK:
    case RTM_DELADDR:
    case RTM_DELROUTE:
    case RTM_DELRULE:
    case RTM_DELNEIGH:
      mode = kModeDelete;
      break;

    default:
      return nullptr;
  }

  rtattr* attr_data = nullptr;
  size_t attr_length = 0;
  std::unique_ptr<RTNLMessage> msg;
  switch (header->nlmsg_type) {
    case RTM_NEWLINK:
    case RTM_DELLINK:
      attr_data = IFLA_RTA(NLMSG_DATA(header));
      attr_length = IFLA_PAYLOAD(header);
      msg = DecodeLink(mode, payload);
      break;

    case RTM_NEWADDR:
    case RTM_DELADDR:
      attr_data = IFA_RTA(NLMSG_DATA(header));
      attr_length = IFA_PAYLOAD(header);
      msg = DecodeAddress(mode, payload);
      break;

    case RTM_NEWROUTE:
    case RTM_DELROUTE:
      attr_data = RTM_RTA(NLMSG_DATA(header));
      attr_length = RTM_PAYLOAD(header);
      msg = DecodeRoute(mode, payload);
      break;

    case RTM_NEWRULE:
    case RTM_DELRULE:
      attr_data = RTM_RTA(NLMSG_DATA(header));
      attr_length = RTM_PAYLOAD(header);
      msg = DecodeRule(mode, payload);
      break;

    case RTM_NEWNDUSEROPT:
      msg = DecodeNdUserOption(mode, payload);
      break;

    case RTM_NEWPREFIX:
      msg = DecodePrefix(mode, payload);
      break;

    case RTM_NEWNEIGH:
    case RTM_DELNEIGH:
      attr_data = RTM_RTA(NLMSG_DATA(header));
      attr_length = RTM_PAYLOAD(header);
      msg = DecodeNeighbor(mode, payload);
      break;

    default:
      NOTREACHED_IN_MIGRATION();
  }
  if (!msg) {
    return nullptr;
  }

  msg->flags_ = header->nlmsg_flags;
  msg->seq_ = header->nlmsg_seq;
  msg->pid_ = header->nlmsg_pid;

  const std::unique_ptr<RTNLAttrMap> attrs = ParseAttrs(attr_data, attr_length);
  if (!attrs) {
    return nullptr;
  }
  for (const auto& pair : *attrs) {
    msg->SetAttribute(pair.first, pair.second);
  }
  return msg;
}

std::unique_ptr<RTNLMessage> RTNLMessage::DecodeLink(
    Mode mode, base::span<const uint8_t> payload) {
  // Parse ifinfomsg struct.
  if (payload.size() < NLMSG_ALIGN(sizeof(struct ifinfomsg))) {
    return nullptr;
  }
  const struct ifinfomsg* ifi =
      reinterpret_cast<const struct ifinfomsg*>(payload.data());
  payload = payload.subspan<NLMSG_ALIGN(sizeof(struct ifinfomsg))>();

  // Parse the attributes.
  // Note: |payload.data()| here is equivalent to IFLA_RTA() with the original
  // payload.
  std::unique_ptr<RTNLAttrMap> attrs = ParseAttrs(
      reinterpret_cast<const rtattr*>(payload.data()), payload.size());
  if (!attrs) {
    return nullptr;
  }

  std::optional<std::string> kind_option;
  if (base::Contains(*attrs, IFLA_LINKINFO)) {
    auto& bytes = attrs->find(IFLA_LINKINFO)->second;
    struct rtattr* link_data = reinterpret_cast<struct rtattr*>(bytes.data());
    size_t link_len = bytes.size();
    std::unique_ptr<RTNLAttrMap> linkinfo = ParseAttrs(link_data, link_len);

    if (linkinfo && base::Contains(*linkinfo, IFLA_INFO_KIND)) {
      const auto& kind_bytes = linkinfo->find(IFLA_INFO_KIND)->second;
      const auto kind_string =
          net_base::byte_utils::StringFromCStringBytes(kind_bytes);
      if (base::IsStringASCII(kind_string)) {
        kind_option = kind_string;
      } else {
        LOG(ERROR) << base::StringPrintf(
            "Invalid kind <%s>, interface index %d",
            base::HexEncode(kind_bytes).c_str(), ifi->ifi_index);
      }
    }
  }

  auto msg = std::make_unique<RTNLMessage>(kTypeLink, mode, 0, 0, 0,
                                           ifi->ifi_index, ifi->ifi_family);
  msg->set_link_status(
      LinkStatus(ifi->ifi_type, ifi->ifi_flags, ifi->ifi_change, kind_option));
  return msg;
}

// static
std::unique_ptr<RTNLMessage> RTNLMessage::DecodeAddress(
    Mode mode, base::span<const uint8_t> payload) {
  if (payload.size() < sizeof(struct ifaddrmsg)) {
    return nullptr;
  }
  const struct ifaddrmsg* ifa =
      reinterpret_cast<const struct ifaddrmsg*>(payload.data());

  auto msg = std::make_unique<RTNLMessage>(kTypeAddress, mode, 0, 0, 0,
                                           ifa->ifa_index, ifa->ifa_family);
  msg->set_address_status(
      AddressStatus(ifa->ifa_prefixlen, ifa->ifa_flags, ifa->ifa_scope));
  return msg;
}

// static
std::unique_ptr<RTNLMessage> RTNLMessage::DecodeRoute(
    Mode mode, base::span<const uint8_t> payload) {
  if (payload.size() < sizeof(struct rtmsg)) {
    return nullptr;
  }
  const struct rtmsg* rtm =
      reinterpret_cast<const struct rtmsg*>(payload.data());

  auto msg = std::make_unique<RTNLMessage>(kTypeRoute, mode, 0, 0, 0, 0,
                                           rtm->rtm_family);
  msg->set_route_status(RouteStatus(
      rtm->rtm_dst_len, rtm->rtm_src_len, rtm->rtm_table, rtm->rtm_protocol,
      rtm->rtm_scope, rtm->rtm_type, rtm->rtm_flags));
  return msg;
}

std::unique_ptr<RTNLMessage> RTNLMessage::DecodeRule(
    Mode mode, base::span<const uint8_t> payload) {
  if (payload.size() < sizeof(struct rtmsg)) {
    return nullptr;
  }
  const struct rtmsg* rtm =
      reinterpret_cast<const struct rtmsg*>(payload.data());

  auto msg = std::make_unique<RTNLMessage>(kTypeRule, mode, 0, 0, 0, 0,
                                           rtm->rtm_family);
  msg->set_route_status(RouteStatus(
      rtm->rtm_dst_len, rtm->rtm_src_len, rtm->rtm_table, rtm->rtm_protocol,
      rtm->rtm_scope, rtm->rtm_type, rtm->rtm_flags));
  return msg;
}

std::unique_ptr<RTNLMessage> RTNLMessage::DecodeNdUserOption(
    Mode mode, base::span<const uint8_t> payload) {
  // Parse the nduseroptmsg struct.
  if (payload.size() < sizeof(struct nduseroptmsg)) {
    return nullptr;
  }
  const struct nduseroptmsg* nd_user_opt =
      reinterpret_cast<const struct nduseroptmsg*>(payload.data());
  payload = payload.subspan<sizeof(struct nduseroptmsg)>();

  // Verify IP family.
  const int32_t interface_index = nd_user_opt->nduseropt_ifindex;
  const sa_family_t family = nd_user_opt->nduseropt_family;
  if (FromSAFamily(family) != IPFamily::kIPv6) {
    return nullptr;
  }

  // Parse the option header.
  if (payload.size() < sizeof(NDUserOptionHeader)) {
    return nullptr;
  }
  const NDUserOptionHeader* nd_user_option_header =
      reinterpret_cast<const NDUserOptionHeader*>(payload.data());
  payload = payload.subspan<sizeof(NDUserOptionHeader)>();

  // Verify option length.
  // The length field in the header is in units of 8 octets, which indicates the
  // size of payload, including the NDUserOptionHeader.
  const size_t opt_len = nd_user_option_header->length * 8;
  if (opt_len != nd_user_opt->nduseropt_opts_len) {
    return nullptr;
  }
  if (payload.size() < opt_len - sizeof(NDUserOptionHeader)) {
    return nullptr;
  }
  payload = payload.first(opt_len - sizeof(NDUserOptionHeader));

  std::unique_ptr<RTNLMessage> msg;
  switch (nd_user_option_header->type) {
    case ND_OPT_DNSSL: {
      msg = std::make_unique<RTNLMessage>(kTypeDnssl, mode, 0, 0, 0,
                                          interface_index, family);
      if (!msg->ParseDnsslOption(payload)) {
        LOG(ERROR) << "Invalid DNSSL RTNL packet.";
        return nullptr;
      }
      return msg;
    }
    case ND_OPT_RDNSS: {
      // Parse RNDSS (Recursive DNS Server) option.
      msg = std::make_unique<RTNLMessage>(kTypeRdnss, mode, 0, 0, 0,
                                          interface_index, family);
      if (!msg->ParseRdnssOption(payload)) {
        LOG(ERROR) << "Invalid RDNSS RTNL packet.";
        return nullptr;
      }
      return msg;
    }
    case ND_OPT_CAPTIVE_PORTAL: {
      // Parse Captive portal URI option.
      msg = std::make_unique<RTNLMessage>(kTypeCaptivePortal, mode, 0, 0, 0,
                                          interface_index, family);
      if (!msg->ParseCaptivePortalOption(payload)) {
        LOG(ERROR) << "Invalid captive portal URI RTNL packet.";
        return nullptr;
      }
      return msg;
    }
    case ND_OPT_PREF64: {
      msg = std::make_unique<RTNLMessage>(kTypePref64, mode, 0, 0, 0,
                                          interface_index, family);
      if (!msg->ParsePref64Option(payload)) {
        LOG(ERROR) << "Invalid PREF64 RTNL packet.";
        return nullptr;
      }
      return msg;
    }
    default:
      msg = std::make_unique<RTNLMessage>(kTypeNdUserOption, mode, 0, 0, 0,
                                          interface_index, family);
      msg->SetNdUserOptionBytes(base::span(
          reinterpret_cast<const uint8_t*>(nd_user_option_header), opt_len));
      return msg;
  }
}

void RTNLMessage::SetNdUserOptionBytes(base::span<const uint8_t> data) {
  LOG_IF(DFATAL, data.empty()) << "ND user option data should not be empty";
  nd_user_option_.type = data[0];
  nd_user_option_.option_bytes.assign(std::begin(data), std::end(data));
}

bool RTNLMessage::ParseDnsslOption(base::span<const uint8_t> data) {
  // Section 5.2 of RFC8106.
  // The layout of DNSSL option after the type and length field is:
  // - Reserved: 2 bytes
  // - Lifetime: 4 bytes
  // - Domain names of DNS search list: one or more domain name that is encoded
  //   as Section 3.1 of RFC1035.

  if (data.size() < 2 + 4) {
    return false;
  }

  // Skip the reserved field.
  data = data.subspan<2>();

  // Parse the lifetime.
  const uint32_t lifetime =
      ntohl(*byte_utils::FromBytes<uint32_t>(data.first<4>()));
  data = data.subspan<4>();

  std::vector<std::string> domains;
  std::vector<std::string_view> tokens;
  while (!data.empty()) {
    const uint8_t token_size = data[0];
    data = data.subspan<1>();

    if (data.size() < token_size) {
      return false;
    }
    if (token_size > 0) {
      tokens.push_back(std::string_view(
          reinterpret_cast<const char*>(data.data()), token_size));
      data = data.subspan(token_size);
    } else if (!tokens.empty()) {
      domains.push_back(base::JoinString(tokens, "."));
      tokens.clear();
    }
  }
  if (!tokens.empty()) {
    domains.push_back(base::JoinString(tokens, "."));
  }
  // b/408883419: if any invalid character is seen in the list of domain names,
  // the whole option is deemed not trustworthy and is thrown away.
  if (!std::all_of(domains.begin(), domains.end(), [](const std::string& s) {
        return base::IsStringASCII(s);
      })) {
    return false;
  }
  dnssl_option_.domains = std::move(domains);
  dnssl_option_.lifetime = lifetime;
  return true;
}

bool RTNLMessage::ParseRdnssOption(base::span<const uint8_t> data) {
  // Section 5.1 of RFC8106.
  // The layout of RDNSS option after the type and length field is:
  // - Reserved: 2 bytes
  // - Lifetime: 4 bytes
  // - IPv6 Recursive DNS servers: one or more 16-byte IPv6 addresses
  const size_t addr_length = IPv6Address::kAddressLength;

  if (data.size() < 2 + 4 + addr_length) {
    return false;
  }

  // Skip the reserved field.
  data = data.subspan<2>();

  // Parse the lifetime.
  const uint32_t lifetime =
      ntohl(*byte_utils::FromBytes<uint32_t>(data.first<4>()));
  data = data.subspan<4>();

  // Parse the recursive DNS servers.
  // Verify data size are multiple of individual address size.
  if (data.size() % addr_length != 0) {
    return false;
  }

  // Parse the DNS server addresses.
  std::vector<IPv6Address> dns_server_addresses;
  while (!data.empty()) {
    dns_server_addresses.push_back(
        *IPv6Address::CreateFromBytes(data.first(addr_length)));
    data = data.subspan(addr_length);
  }
  set_rdnss_option(RdnssOption(lifetime, dns_server_addresses));
  return true;
}

bool RTNLMessage::ParseCaptivePortalOption(base::span<const uint8_t> data) {
  // Section 2.3 of RFC8910.
  // The layout of RDNSS option after the type and length field is:
  // - URI: padded with 0 to make the total option length a multiple of 8 bytes
  if (data.empty()) {
    LOG(ERROR) << "Empty payload for captive portal URI";
    return false;
  }

  // The string is not guaranteed to be null terminated, so we need to find the
  // length by strnlen().
  const char* str = reinterpret_cast<const char*>(data.data());
  const std::string_view str_view(str, strnlen(str, data.size()));
  const std::optional<HttpUrl> url = HttpUrl::CreateFromString(str_view);
  if (!url.has_value() || url->protocol() != HttpUrl::Protocol::kHttps) {
    LOG(ERROR) << "Invalid captive portal URI: " << str_view;
    return false;
  }
  set_captive_portal_uri(*url);
  return true;
}

bool RTNLMessage::ParsePref64Option(base::span<const uint8_t> data) {
  // Section 4 of RFC8781.
  // The layout of Pref64 option after the type and length field is:
  // - Scaled Lifetime: 13 bits
  // - Prefix Length Code: 3 bits.
  // - Highest 96 bits of the Prefix: 12 bytes.
  if (data.size() != 14) {
    return false;
  }
  const uint8_t plc = data[1] & 0x7;
  int prefix_len = 0;
  switch (plc) {
    case 0:
      prefix_len = 96;
      break;
    case 1:
      prefix_len = 64;
      break;
    case 2:
      prefix_len = 56;
      break;
    case 3:
      prefix_len = 48;
      break;
    case 4:
      prefix_len = 40;
      break;
    case 5:
      prefix_len = 32;
      break;
    default:
      LOG(ERROR) << "Invalid PLC value: " << static_cast<int>(plc);
      return false;
  }
  data = data.subspan<2>();
  std::array<uint8_t, IPv6Address::kAddressLength> address_data{};
  std::copy(data.begin(), data.end(), address_data.begin());
  set_pref64(*IPv6CIDR::CreateFromBytesAndPrefix(address_data, prefix_len));
  // TODO(b/308893691): Lifetime is ignored for now.
  return true;
}

std::unique_ptr<RTNLMessage> RTNLMessage::DecodeNeighbor(
    Mode mode, base::span<const uint8_t> payload) {
  if (payload.size() < sizeof(struct ndmsg)) {
    return nullptr;
  }
  const struct ndmsg* ndm =
      reinterpret_cast<const struct ndmsg*>(payload.data());

  auto msg = std::make_unique<RTNLMessage>(kTypeNeighbor, mode, 0, 0, 0,
                                           ndm->ndm_ifindex, ndm->ndm_family);
  msg->set_neighbor_status(
      NeighborStatus(ndm->ndm_state, ndm->ndm_flags, ndm->ndm_type));
  return msg;
}

std::unique_ptr<RTNLMessage> RTNLMessage::DecodePrefix(
    Mode mode, base::span<const uint8_t> payload) {
  if (payload.size() < sizeof(struct prefixmsg)) {
    return nullptr;
  }
  const struct prefixmsg* pm =
      reinterpret_cast<const struct prefixmsg*>(payload.data());

  auto msg = std::make_unique<RTNLMessage>(
      kTypePrefix, mode, 0, 0, 0, pm->prefix_ifindex, pm->prefix_family);
  PrefixStatus status;
  status.prefix_flags = pm->prefix_flags;

  payload = payload.subspan<sizeof(struct prefixmsg)>();
  std::unique_ptr<RTNLAttrMap> attrs = ParseAttrs(
      reinterpret_cast<const rtattr*>(payload.data()), payload.size());
  if (!attrs) {
    return nullptr;
  }
  std::optional<IPv6CIDR> cidr;
  if (base::Contains(*attrs, PREFIX_ADDRESS)) {
    cidr = IPv6CIDR::CreateFromBytesAndPrefix(
        attrs->find(PREFIX_ADDRESS)->second, pm->prefix_len);
  }
  if (!cidr) {
    return nullptr;
  }
  status.prefix = *cidr;

  msg->set_prefix_status(status);
  return msg;
}

std::vector<uint8_t> RTNLMessage::Encode() const {
  if (type_ != kTypeLink && type_ != kTypeAddress && type_ != kTypeRoute &&
      type_ != kTypeRule && type_ != kTypeNeighbor) {
    return {};
  }

  RTNLHeader hdr;
  hdr.hdr.nlmsg_flags = flags_;
  hdr.hdr.nlmsg_seq = seq_;
  hdr.hdr.nlmsg_pid = pid_;

  switch (type_) {
    case kTypeLink:
      if (!EncodeLink(&hdr)) {
        return {};
      }
      break;

    case kTypeAddress:
      if (!EncodeAddress(&hdr)) {
        return {};
      }
      break;

    case kTypeRoute:
    case kTypeRule:
      if (!EncodeRoute(&hdr)) {
        return {};
      }
      break;

    case kTypeNeighbor:
      if (!EncodeNeighbor(&hdr)) {
        return {};
      }
      break;

    default:
      NOTREACHED_IN_MIGRATION();
  }

  if (mode_ == kModeGet) {
    hdr.hdr.nlmsg_flags |= NLM_F_REQUEST | NLM_F_DUMP;
  }

  uint32_t header_length = hdr.hdr.nlmsg_len;
  const std::vector<uint8_t> attributes = PackAttrs(attributes_);
  hdr.hdr.nlmsg_len =
      NLMSG_ALIGN(hdr.hdr.nlmsg_len) + static_cast<uint32_t>(attributes.size());
  std::vector<uint8_t> packet(reinterpret_cast<uint8_t*>(&hdr),
                              reinterpret_cast<uint8_t*>(&hdr) + header_length);
  packet.insert(packet.end(), attributes.begin(), attributes.end());

  return packet;
}

bool RTNLMessage::EncodeLink(RTNLHeader* hdr) const {
  switch (mode_) {
    case kModeAdd:
      hdr->hdr.nlmsg_type = RTM_NEWLINK;
      break;
    case kModeDelete:
      hdr->hdr.nlmsg_type = RTM_DELLINK;
      break;
    case kModeGet:
    case kModeQuery:
      hdr->hdr.nlmsg_type = RTM_GETLINK;
      break;
    default:
      NOTIMPLEMENTED();
      return false;
  }
  hdr->hdr.nlmsg_len = NLMSG_LENGTH(sizeof(hdr->ifi));
  hdr->ifi.ifi_family = static_cast<unsigned char>(family_);
  hdr->ifi.ifi_index = interface_index_;
  hdr->ifi.ifi_type = static_cast<uint16_t>(link_status_.type);
  hdr->ifi.ifi_flags = link_status_.flags;
  hdr->ifi.ifi_change = link_status_.change;
  return true;
}

bool RTNLMessage::EncodeAddress(RTNLHeader* hdr) const {
  switch (mode_) {
    case kModeAdd:
      hdr->hdr.nlmsg_type = RTM_NEWADDR;
      break;
    case kModeDelete:
      hdr->hdr.nlmsg_type = RTM_DELADDR;
      break;
    case kModeGet:
    case kModeQuery:
      hdr->hdr.nlmsg_type = RTM_GETADDR;
      break;
    default:
      NOTIMPLEMENTED();
      return false;
  }
  hdr->hdr.nlmsg_len = NLMSG_LENGTH(sizeof(hdr->ifa));
  hdr->ifa.ifa_family = static_cast<unsigned char>(family_);
  hdr->ifa.ifa_prefixlen = address_status_.prefix_len;
  hdr->ifa.ifa_flags = address_status_.flags;
  hdr->ifa.ifa_scope = address_status_.scope;
  hdr->ifa.ifa_index = static_cast<uint32_t>(interface_index_);
  return true;
}

bool RTNLMessage::EncodeRoute(RTNLHeader* hdr) const {
  // Routes and routing rules are both based on struct rtm
  switch (mode_) {
    case kModeAdd:
      hdr->hdr.nlmsg_type = (type_ == kTypeRoute) ? RTM_NEWROUTE : RTM_NEWRULE;
      break;
    case kModeDelete:
      hdr->hdr.nlmsg_type = (type_ == kTypeRoute) ? RTM_DELROUTE : RTM_DELRULE;
      break;
    case kModeGet:
    case kModeQuery:
      hdr->hdr.nlmsg_type = (type_ == kTypeRoute) ? RTM_GETROUTE : RTM_GETRULE;
      break;
    default:
      NOTIMPLEMENTED();
      return false;
  }
  hdr->hdr.nlmsg_len = NLMSG_LENGTH(sizeof(hdr->rtm));
  hdr->rtm.rtm_family = static_cast<unsigned char>(family_);
  hdr->rtm.rtm_dst_len = route_status_.dst_prefix;
  hdr->rtm.rtm_src_len = route_status_.src_prefix;
  hdr->rtm.rtm_table = route_status_.table;
  hdr->rtm.rtm_protocol = route_status_.protocol;
  hdr->rtm.rtm_scope = route_status_.scope;
  hdr->rtm.rtm_type = route_status_.type;
  hdr->rtm.rtm_flags = route_status_.flags;
  return true;
}

bool RTNLMessage::EncodeNeighbor(RTNLHeader* hdr) const {
  switch (mode_) {
    case kModeAdd:
      hdr->hdr.nlmsg_type = RTM_NEWNEIGH;
      break;
    case kModeDelete:
      hdr->hdr.nlmsg_type = RTM_DELNEIGH;
      break;
    case kModeGet:
    case kModeQuery:
      hdr->hdr.nlmsg_type = RTM_GETNEIGH;
      break;
    default:
      NOTIMPLEMENTED();
      return false;
  }
  hdr->hdr.nlmsg_len = NLMSG_LENGTH(sizeof(hdr->ndm));
  hdr->ndm.ndm_family = static_cast<unsigned char>(family_);
  hdr->ndm.ndm_ifindex = interface_index_;
  hdr->ndm.ndm_state = neighbor_status_.state;
  hdr->ndm.ndm_flags = neighbor_status_.flags;
  hdr->ndm.ndm_type = neighbor_status_.type;
  return true;
}

uint32_t RTNLMessage::GetUint32Attribute(uint16_t attr) const {
  return net_base::byte_utils::FromBytes<uint32_t>(GetAttribute(attr))
      .value_or(0);
}

std::string RTNLMessage::GetStringAttribute(uint16_t attr) const {
  if (!HasAttribute(attr)) {
    return "";
  }
  return net_base::byte_utils::StringFromCStringBytes(GetAttribute(attr));
}

std::string RTNLMessage::GetIflaIfname() const {
  return GetStringAttribute(IFLA_IFNAME);
}

std::optional<IPCIDR> RTNLMessage::GetAddress() const {
  // Use IFA_LOCAL if it's not empty and fallback to IFA_ADDRESS. According the
  // kernel comment (/usr/include/linux/if_addr.h), for a point-to-point link,
  // IFA_LOCAL is the local address on the interface while IFA_ADDRESS is the
  // peer one. Since IFA_LOCAL won't always be set (e.g., for some IPv6
  // addresses), we also need to query IFA_ADDRESS.
  std::vector<uint8_t> addr_bytes = HasAttribute(IFA_LOCAL)
                                        ? GetAttribute(IFA_LOCAL)
                                        : GetAttribute(IFA_ADDRESS);
  return IPCIDR::CreateFromBytesAndPrefix(
      addr_bytes, address_status_.prefix_len, FromSAFamily(family_));
}

uint32_t RTNLMessage::GetRtaTable() const {
  return GetUint32Attribute(RTA_TABLE);
}

std::optional<IPCIDR> RTNLMessage::GetRtaDst() const {
  return IPCIDR::CreateFromBytesAndPrefix(
      GetAttribute(RTA_DST), route_status_.dst_prefix, FromSAFamily(family_));
}

std::optional<IPCIDR> RTNLMessage::GetRtaSrc() const {
  return IPCIDR::CreateFromBytesAndPrefix(
      GetAttribute(RTA_SRC), route_status_.src_prefix, FromSAFamily(family_));
}

std::optional<IPAddress> RTNLMessage::GetRtaGateway() const {
  return IPAddress::CreateFromBytes(GetAttribute(RTA_GATEWAY),
                                    FromSAFamily(family_));
}

std::optional<IPAddress> RTNLMessage::GetRtaPrefSrc() const {
  return IPAddress::CreateFromBytes(GetAttribute(RTA_PREFSRC),
                                    FromSAFamily(family_));
}

uint32_t RTNLMessage::GetRtaOif() const {
  return GetUint32Attribute(RTA_OIF);
}

std::string RTNLMessage::GetRtaOifname() const {
  return IndexToName(GetRtaOif());
}

uint32_t RTNLMessage::GetRtaPriority() const {
  return GetUint32Attribute(RTA_PRIORITY);
}

uint32_t RTNLMessage::GetFraTable() const {
  return GetUint32Attribute(FRA_TABLE);
}

std::string RTNLMessage::GetFraOifname() const {
  return GetStringAttribute(FRA_OIFNAME);
}

std::string RTNLMessage::GetFraIifname() const {
  return GetStringAttribute(FRA_IIFNAME);
}

std::optional<IPCIDR> RTNLMessage::GetFraSrc() const {
  return IPCIDR::CreateFromBytesAndPrefix(
      GetAttribute(FRA_SRC), route_status_.src_prefix, FromSAFamily(family_));
}

std::optional<IPCIDR> RTNLMessage::GetFraDst() const {
  return IPCIDR::CreateFromBytesAndPrefix(
      GetAttribute(FRA_DST), route_status_.dst_prefix, FromSAFamily(family_));
}

uint32_t RTNLMessage::GetFraFwmark() const {
  return GetUint32Attribute(FRA_FWMARK);
}

uint32_t RTNLMessage::GetFraFwmask() const {
  return GetUint32Attribute(FRA_FWMASK);
}

uint32_t RTNLMessage::GetFraPriority() const {
  return GetUint32Attribute(FRA_PRIORITY);
}

void RTNLMessage::SetIflaInfoKind(const std::string& link_kind,
                                  base::span<const uint8_t> info_data) {
  // The maximum length of IFLA_INFO_KIND attribute is MODULE_NAME_LEN, defined
  // in /include/linux/module.h, as (64 - sizeof(unsigned long)). Set it to a
  // fixed value here.
  constexpr uint32_t kMaxModuleNameLen = 56;
  if (link_kind.length() >= kMaxModuleNameLen) {
    LOG(DFATAL) << "link_kind is too long: " << link_kind;
  }
  link_status_.kind = link_kind;
  RTNLAttrMap link_info_map;
  link_info_map[IFLA_INFO_KIND] =
      net_base::byte_utils::StringToCStringBytes(link_kind.c_str());
  if (!info_data.empty()) {
    link_info_map[IFLA_INFO_DATA] = {info_data.data(),
                                     info_data.data() + info_data.size()};
  }
  if (HasAttribute(IFLA_LINKINFO)) {
    LOG(DFATAL) << "IFLA_LINKINFO has already been set.";
  }
  SetAttribute(IFLA_LINKINFO, PackAttrs(link_info_map));
}

// static
std::string RTNLMessage::ModeToString(RTNLMessage::Mode mode) {
  switch (mode) {
    case RTNLMessage::kModeGet:
      return "Get";
    case RTNLMessage::kModeAdd:
      return "Add";
    case RTNLMessage::kModeDelete:
      return "Delete";
    case RTNLMessage::kModeQuery:
      return "Query";
    default:
      return "UnknownMode";
  }
}

// static
std::string RTNLMessage::TypeToString(RTNLMessage::Type type) {
  switch (type) {
    case RTNLMessage::kTypeLink:
      return "Link";
    case RTNLMessage::kTypeAddress:
      return "Address";
    case RTNLMessage::kTypeRoute:
      return "Route";
    case RTNLMessage::kTypeRule:
      return "Rule";
    case RTNLMessage::kTypeRdnss:
      return "Rdnss";
    case RTNLMessage::kTypeDnssl:
      return "Dnssl";
    case RTNLMessage::kTypeNeighbor:
      return "Neighbor";
    case RTNLMessage::kTypeNdUserOption:
      return "NdUserOption";
    default:
      return "UnknownType";
  }
}

std::string RTNLMessage::ToString() const {
  // Include the space separator in |ip_family_str| to avoid double spaces for
  // messages with family AF_UNSPEC.
  const auto ip_family = FromSAFamily(family());
  std::string ip_family_str =
      " " + (ip_family ? net_base::ToString(*ip_family) : "unknown");
  std::string details;
  switch (type()) {
    case RTNLMessage::kTypeLink:
      ip_family_str = "";
      details = base::StringPrintf(
          "%s[%d] type %s flags <%s> change %X", GetIflaIfname().c_str(),
          interface_index_, GetNetDeviceTypeName(link_status_.type).c_str(),
          PrintFlags(link_status_.flags, kNetDeviceFlags, ",").c_str(),
          link_status_.change);
      if (link_status_.kind.has_value()) {
        details += " kind " + link_status_.kind.value();
      }
      break;
    case RTNLMessage::kTypeAddress:
      if (const auto addr = GetAddress(); addr.has_value()) {
        details = base::StringPrintf(
            "%s if %s[%d] flags %s scope %d", addr->ToString().c_str(),
            IndexToName(static_cast<unsigned int>(interface_index_)).c_str(),
            interface_index_,
            address_status_.flags
                ? PrintFlags(address_status_.flags, kIfaFlags).c_str()
                : "0",
            address_status_.scope);
      } else {
        LOG(ERROR)
            << "RTNL address message does not have a valid local address";
      }
      break;
    case RTNLMessage::kTypeRoute:
      if (const auto addr = GetRtaSrc(); addr.has_value()) {
        details += base::StringPrintf("src %s ", addr->ToString().c_str());
      }
      if (const auto addr = GetRtaDst(); addr.has_value()) {
        details += base::StringPrintf("dst %s ", addr->ToString().c_str());
      }
      if (const auto addr = GetRtaGateway(); addr.has_value()) {
        details += "via " + addr->ToString() + " ";
      }
      if (HasAttribute(RTA_OIF)) {
        details += base::StringPrintf("if %s[%d] ", GetRtaOifname().c_str(),
                                      GetRtaOif());
      }
      details += base::StringPrintf(
          "table %d priority %d protocol %s type %s", GetRtaTable(),
          GetRtaPriority(), GetRouteProtocol(route_status_.protocol).c_str(),
          GetRouteTypeName(route_status_.type).c_str());
      break;
    case RTNLMessage::kTypeRule:
      // Rules are serialized via struct fib_rule_hdr which aligns with struct
      // rtmsg used for routes such that |type| corresponds to the rule action.
      // |protocol| and |scope| are currently unused as of Linux 5.6.
      if (HasAttribute(FRA_IIFNAME)) {
        details += "iif " + GetFraIifname() + " ";
      }
      if (HasAttribute(FRA_OIFNAME)) {
        details += "oif " + GetFraOifname() = " ";
      }
      if (const auto addr = GetFraSrc(); addr.has_value()) {
        details += base::StringPrintf("src %s ", addr->ToString().c_str());
      }
      if (const auto addr = GetFraDst(); addr.has_value()) {
        details += base::StringPrintf("dst %s ", addr->ToString().c_str());
      }
      if (HasAttribute(FRA_FWMARK)) {
        details += base::StringPrintf("fwmark 0x%X/0x%X ", GetFraFwmark(),
                                      GetFraFwmask());
      }
      details += base::StringPrintf(
          "table %d priority %d action %s flags %X", GetFraTable(),
          GetFraPriority(), GetRuleActionName(route_status_.type).c_str(),
          route_status_.flags);
      break;
    case RTNLMessage::kTypeRdnss:
      details = rdnss_option_.ToString();
      break;
    case RTNLMessage::kTypeDnssl:
      details = dnssl_option_.ToString();
      break;
    case RTNLMessage::kTypeNdUserOption:
      details = nd_user_option_.ToString();
      break;
    case RTNLMessage::kTypeNeighbor:
      details = neighbor_status_.ToString();
      break;
    default:
      break;
  }
  return base::StringPrintf("%s%s %s: %s", ModeToString(mode()).c_str(),
                            ip_family_str.c_str(), TypeToString(type()).c_str(),
                            details.c_str());
}

}  // namespace net_base
