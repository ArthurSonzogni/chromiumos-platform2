// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/routing_policy_service.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include <base/strings/string_piece.h>
#include <base/strings/stringprintf.h>
#include <brillo/userdb_utils.h>
#include <net-base/ip_address.h>
#include <net-base/byte_utils.h>

#include "shill/logging.h"
#include "shill/net/rtnl_handler.h"
#include "shill/net/rtnl_listener.h"
#include "shill/net/rtnl_message.h"

bool operator==(const fib_rule_uid_range& a, const fib_rule_uid_range& b) {
  return (a.start == b.start) && (a.end == b.end);
}

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kRoute;
}  // namespace Logging

namespace {

// Amount added to an interface index to come up with the routing table ID for
// that interface.
constexpr int kInterfaceTableIdIncrement = 1000;
static_assert(
    kInterfaceTableIdIncrement > RT_TABLE_LOCAL,
    "kInterfaceTableIdIncrement must be greater than RT_TABLE_LOCAL, "
    "as otherwise some interface's table IDs may collide with system tables.");

// For VPN drivers that only want to pass traffic for specific users,
// these are the usernames that will be used to create the routing policy
// rules. Also, when an AlwaysOnVpnPackage is set and a corresponding VPN
// service is not active, traffic from these users will blackholed.
// Currently the "user traffic" as defined by these usernames does not include
// e.g. Android apps or system processes like the update engine.
constexpr std::array<base::StringPiece, 9> kUserTrafficUsernames = {
    "chronos",         // Traffic originating from chrome and nacl applications
    "debugd",          // crosh terminal
    "cups",            // built-in printing using the cups daemon
    "lpadmin",         // printer configuration utility used by cups
    "kerberosd",       // Chrome OS Kerberos daemon
    "kerberosd-exec",  // Kerberos third party untrusted code
    // While tlsdate is not user traffic, time sync should be attempted over
    // VPN. It is OK to send tlsdate traffic over VPN because it will also try
    // to sync time immediately after boot on the sign-in screen when no VPN can
    // be active.
    // TODO(https://crbug.com/1065378): Find a way for tlsdate to try both with
    // and without VPN explicitly.
    "tlsdate",    // tlsdate daemon (secure time sync)
    "pluginvm",   // plugin vm problem report utility (b/160916677)
    "fuse-smbfs"  // smbfs SMB filesystem daemon
};

std::vector<uint32_t> ComputeUserTrafficUids() {
  std::vector<uint32_t> uids;
  for (const auto& username : kUserTrafficUsernames) {
    uid_t uid;
    if (!brillo::userdb::GetUserInfo(std::string(username), &uid, nullptr)) {
      LOG(WARNING) << "Unable to look up UID for " << username;
      continue;
    }
    uids.push_back(uint32_t{uid});
  }
  return uids;
}

}  // namespace

RoutingPolicyService::RoutingPolicyService()
    : rtnl_handler_(RTNLHandler::GetInstance()) {
  SLOG(2) << __func__;
}

RoutingPolicyService::~RoutingPolicyService() = default;

RoutingPolicyService* RoutingPolicyService::GetInstance() {
  static base::NoDestructor<RoutingPolicyService> instance;
  return instance.get();
}

void RoutingPolicyService::Start() {
  SLOG(2) << __func__;

  rule_listener_.reset(new RTNLListener(
      RTNLHandler::kRequestRule,
      base::BindRepeating(&RoutingPolicyService::RuleMsgHandler,
                          base::Unretained(this))));
  rtnl_handler_->RequestDump(RTNLHandler::kRequestRule);
}

void RoutingPolicyService::Stop() {
  SLOG(2) << __func__;

  rule_listener_.reset();
}

void RoutingPolicyService::RuleMsgHandler(const RTNLMessage& message) {
  // Family will be set to the real value in ParseRoutingPolicyMessage().
  auto entry = ParseRoutingPolicyMessage(message);

  if (!entry) {
    return;
  }

  if (!(entry->priority > kRulePriorityLocal &&
        entry->priority < kRulePriorityMain)) {
    // Don't touch the system-managed rules.
    return;
  }

  // If this rule matches one of our known rules, ignore it.  Otherwise,
  // assume it is left over from an old run and delete it.
  for (const auto& table : policy_tables_) {
    if (std::find(table.second.begin(), table.second.end(), *entry) !=
        table.second.end()) {
      return;
    }
  }

  ApplyRule(-1, *entry, RTNLMessage::kModeDelete, 0);
  return;
}

std::optional<RoutingPolicyEntry>
RoutingPolicyService::ParseRoutingPolicyMessage(const RTNLMessage& message) {
  if (message.type() != RTNLMessage::kTypeRule) {
    return std::nullopt;
  }

  const RTNLMessage::RouteStatus& route_status = message.route_status();
  if (route_status.type != RTN_UNICAST) {
    return std::nullopt;
  }

  auto family = net_base::FromSAFamily(message.family());
  if (!family) {
    return std::nullopt;
  }

  auto entry = RoutingPolicyEntry(*family);

  entry.invert_rule = !!(route_status.flags & FIB_RULE_INVERT);

  // The rtmsg structure [0] has a table id field that is only a single
  // byte. Prior to Linux v2.6, routing table IDs were of type u8. v2.6 changed
  // this so that table IDs were u32s, but the uapi here couldn't
  // change. Instead, a separate FRA_TABLE attribute is used to be able to send
  // a full 32-bit table ID. When the table ID is greater than 255, the
  // rtm_table field is set to RT_TABLE_COMPAT.
  //
  // 0) elixir.bootlin.com/linux/v5.0/source/include/uapi/linux/rtnetlink.h#L206
  uint32_t table;
  if (message.HasAttribute(FRA_TABLE)) {
    table = net_base::byte_utils::FromBytes<uint32_t>(
                message.GetAttribute(FRA_TABLE))
                .value_or(0);
  } else {
    table = route_status.table;
    LOG_IF(WARNING, table == RT_TABLE_COMPAT)
        << "Received RT_TABLE_COMPAT, but message has no FRA_TABLE attribute";
  }
  entry.table = table;

  if (message.HasAttribute(FRA_PRIORITY)) {
    // Rule 0 (local table) doesn't have a priority attribute.
    const auto priority = net_base::byte_utils::FromBytes<uint32_t>(
        message.GetAttribute(FRA_PRIORITY));
    if (!priority) {
      return std::nullopt;
    }
    entry.priority = *priority;
  }

  if (message.HasAttribute(FRA_FWMARK)) {
    RoutingPolicyEntry::FwMark fw_mark;
    const auto value = net_base::byte_utils::FromBytes<uint32_t>(
        message.GetAttribute(FRA_FWMARK));
    if (!value) {
      return std::nullopt;
    }
    fw_mark.value = *value;

    if (message.HasAttribute(FRA_FWMASK)) {
      const auto mask = net_base::byte_utils::FromBytes<uint32_t>(
          message.GetAttribute(FRA_FWMASK));
      if (!mask) {
        return std::nullopt;
      }
      fw_mark.mask = *mask;
    }
    entry.fw_mark = fw_mark;
  }

  if (message.HasAttribute(FRA_UID_RANGE)) {
    const auto range =
        net_base::byte_utils::FromBytes<struct fib_rule_uid_range>(
            message.GetAttribute(FRA_UID_RANGE));
    if (!range) {
      return std::nullopt;
    }
    entry.uid_range = *range;
  }

  if (message.HasAttribute(FRA_IFNAME)) {
    entry.iif_name = message.GetStringAttribute(FRA_IFNAME);
  }
  if (message.HasAttribute(FRA_OIFNAME)) {
    entry.oif_name = message.GetStringAttribute(FRA_OIFNAME);
  }

  if (auto tmp_dst = message.GetFraDst(); tmp_dst.has_value()) {
    if (tmp_dst->GetFamily() == family) {
      entry.dst = *tmp_dst;
    } else {
      LOG(WARNING) << "FRA_DST family mismatch.";
    }
  }
  if (auto tmp_src = message.GetFraSrc(); tmp_src.has_value()) {
    if (tmp_src->GetFamily() == family) {
      entry.src = *tmp_src;
    } else {
      LOG(WARNING) << "FRA_SRC family mismatch.";
    }
  }

  return entry;
}

bool RoutingPolicyService::AddRule(int interface_index,
                                   const RoutingPolicyEntry& entry) {
  if (!ApplyRule(interface_index, entry, RTNLMessage::kModeAdd,
                 NLM_F_CREATE | NLM_F_EXCL)) {
    return false;
  }
  // Add entry into policy table if no identical entry exists.
  // Note that the main routing table route can be added multiple times without
  // removal so duplication check is essential here.
  auto& policy_table = policy_tables_[interface_index];
  if (std::find(policy_table.begin(), policy_table.end(), entry) !=
      policy_table.end()) {
    return true;
  }
  policy_table.push_back(entry);
  return true;
}

void RoutingPolicyService::FlushRules(int interface_index) {
  SLOG(2) << __func__;

  auto table = policy_tables_.find(interface_index);
  if (table == policy_tables_.end()) {
    return;
  }

  for (const auto& nent : table->second) {
    ApplyRule(interface_index, nent, RTNLMessage::kModeDelete, 0);
  }
  table->second.clear();
}

bool RoutingPolicyService::ApplyRule(uint32_t interface_index,
                                     const RoutingPolicyEntry& entry,
                                     RTNLMessage::Mode mode,
                                     unsigned int flags) {
  SLOG(2) << base::StringPrintf(
      "%s: index %d family %s prio %d", __func__, interface_index,
      net_base::ToString(entry.family).c_str(), entry.priority);

  auto message = std::make_unique<RTNLMessage>(
      RTNLMessage::kTypeRule, mode, NLM_F_REQUEST | flags, 0, 0, 0,
      net_base::ToSAFamily(entry.family));
  message->set_route_status(RTNLMessage::RouteStatus(
      entry.dst.prefix_length(), entry.src.prefix_length(),
      entry.table < 256 ? entry.table : RT_TABLE_COMPAT, RTPROT_BOOT,
      RT_SCOPE_UNIVERSE, RTN_UNICAST, entry.invert_rule ? FIB_RULE_INVERT : 0));

  message->SetAttribute(FRA_TABLE,
                        net_base::byte_utils::ToBytes<uint32_t>(entry.table));
  message->SetAttribute(
      FRA_PRIORITY, net_base::byte_utils::ToBytes<uint32_t>(entry.priority));
  if (entry.fw_mark.has_value()) {
    const RoutingPolicyEntry::FwMark& mark = entry.fw_mark.value();
    message->SetAttribute(FRA_FWMARK,
                          net_base::byte_utils::ToBytes<uint32_t>(mark.value));
    message->SetAttribute(FRA_FWMASK,
                          net_base::byte_utils::ToBytes<uint32_t>(mark.mask));
  }
  if (entry.uid_range.has_value()) {
    message->SetAttribute(
        FRA_UID_RANGE, net_base::byte_utils::ToBytes<struct fib_rule_uid_range>(
                           *entry.uid_range));
  }
  if (entry.iif_name.has_value()) {
    message->SetAttribute(
        FRA_IFNAME,
        net_base::byte_utils::StringToCStringBytes(*entry.iif_name));
  }
  if (entry.oif_name.has_value()) {
    message->SetAttribute(
        FRA_OIFNAME,
        net_base::byte_utils::StringToCStringBytes(*entry.oif_name));
  }
  if (!entry.dst.address().IsZero()) {
    message->SetAttribute(FRA_DST, entry.dst.address().ToBytes());
  }
  if (!entry.src.address().IsZero()) {
    message->SetAttribute(FRA_SRC, entry.src.address().ToBytes());
  }

  return rtnl_handler_->SendMessage(std::move(message), nullptr);
}

const std::vector<uint32_t>& RoutingPolicyService::GetUserTrafficUids() {
  if (user_traffic_uids_.empty()) {
    user_traffic_uids_ = ComputeUserTrafficUids();
  }
  return user_traffic_uids_;
}

uint32_t RoutingPolicyService::GetShillUid() {
  return getuid();
}

RoutingPolicyEntry::RoutingPolicyEntry(net_base::IPFamily family)
    : family(family),
      dst(net_base::IPCIDR(family)),
      src(net_base::IPCIDR(family)) {}

std::ostream& operator<<(std::ostream& os, const RoutingPolicyEntry& entry) {
  os << "{" << net_base::ToString(entry.family) << " " << entry.priority
     << ": ";
  if (entry.invert_rule) {
    os << "not ";
  }
  os << "from ";
  if (!entry.src.address().IsZero()) {
    os << entry.src.ToString() << " ";
  } else {
    os << "all ";
  }
  if (!entry.dst.address().IsZero()) {
    os << "to " << entry.dst.ToString() << " ";
  }
  if (entry.fw_mark) {
    os << base::StringPrintf("fwmark 0x%08x/0x%08x ", entry.fw_mark->value,
                             entry.fw_mark->mask);
  }
  if (entry.iif_name) {
    os << "iif " << *entry.iif_name << " ";
  }
  if (entry.oif_name) {
    os << "oif " << *entry.oif_name << " ";
  }
  if (entry.uid_range) {
    os << "uidrange " << entry.uid_range->start << "-" << entry.uid_range->end
       << " ";
  }
  os << "lookup " << entry.table << "}";
  return os;
}

// clang-format off
bool RoutingPolicyEntry::operator==(const RoutingPolicyEntry& b) const {
    return (family == b.family &&
            priority == b.priority &&
            table == b.table &&
            dst == b.dst &&
            src == b.src &&
            fw_mark == b.fw_mark &&
            uid_range == b.uid_range &&
            iif_name == b.iif_name &&
            oif_name == b.oif_name &&
            invert_rule == b.invert_rule);
}
// clang-format on

}  // namespace shill
