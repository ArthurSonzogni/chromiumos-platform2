// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/routing_policy_service.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include <base/strings/string_piece.h>
#include <base/strings/stringprintf.h>
#include <brillo/userdb_utils.h>
#include <net-base/ip_address.h>

#include "shill/logging.h"
#include "shill/net/byte_string.h"
#include "shill/net/rtnl_handler.h"
#include "shill/net/rtnl_listener.h"
#include "shill/net/rtnl_message.h"

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
    message.GetAttribute(FRA_TABLE).ConvertToCPUUInt32(&table);
  } else {
    table = route_status.table;
    LOG_IF(WARNING, table == RT_TABLE_COMPAT)
        << "Received RT_TABLE_COMPAT, but message has no FRA_TABLE attribute";
  }
  entry.table = table;

  if (message.HasAttribute(FRA_PRIORITY)) {
    // Rule 0 (local table) doesn't have a priority attribute.
    if (!message.GetAttribute(FRA_PRIORITY)
             .ConvertToCPUUInt32(&entry.priority)) {
      return std::nullopt;
    }
  }

  if (message.HasAttribute(FRA_FWMARK)) {
    RoutingPolicyEntry::FwMark fw_mark;
    if (!message.GetAttribute(FRA_FWMARK).ConvertToCPUUInt32(&fw_mark.value)) {
      return std::nullopt;
    }
    if (message.HasAttribute(FRA_FWMASK)) {
      if (!message.GetAttribute(FRA_FWMASK).ConvertToCPUUInt32(&fw_mark.mask)) {
        return std::nullopt;
      }
    }
    entry.fw_mark = fw_mark;
  }

  if (message.HasAttribute(FRA_UID_RANGE)) {
    struct fib_rule_uid_range r;
    if (!message.GetAttribute(FRA_UID_RANGE).CopyData(sizeof(r), &r)) {
      return std::nullopt;
    }
    entry.uid_range = r;
  }

  if (message.HasAttribute(FRA_IFNAME)) {
    entry.iif_name = reinterpret_cast<const char*>(
        message.GetAttribute(FRA_IFNAME).GetConstData());
  }
  if (message.HasAttribute(FRA_OIFNAME)) {
    entry.oif_name = reinterpret_cast<const char*>(
        message.GetAttribute(FRA_OIFNAME).GetConstData());
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
                        ByteString::CreateFromCPUUInt32(entry.table));
  message->SetAttribute(FRA_PRIORITY,
                        ByteString::CreateFromCPUUInt32(entry.priority));
  if (entry.fw_mark.has_value()) {
    const RoutingPolicyEntry::FwMark& mark = entry.fw_mark.value();
    message->SetAttribute(FRA_FWMARK,
                          ByteString::CreateFromCPUUInt32(mark.value));
    message->SetAttribute(FRA_FWMASK,
                          ByteString::CreateFromCPUUInt32(mark.mask));
  }
  if (entry.uid_range.has_value()) {
    message->SetAttribute(FRA_UID_RANGE,
                          ByteString(reinterpret_cast<const unsigned char*>(
                                         &entry.uid_range.value()),
                                     sizeof(entry.uid_range.value())));
  }
  if (entry.iif_name.has_value()) {
    message->SetAttribute(FRA_IFNAME, ByteString(entry.iif_name.value(), true));
  }
  if (entry.oif_name.has_value()) {
    message->SetAttribute(FRA_OIFNAME,
                          ByteString(entry.oif_name.value(), true));
  }
  if (!entry.dst.address().IsZero()) {
    message->SetAttribute(
        FRA_DST, ByteString(entry.dst.address().ToByteString(), false));
  }
  if (!entry.src.address().IsZero()) {
    message->SetAttribute(
        FRA_SRC, ByteString(entry.src.address().ToByteString(), false));
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

}  // namespace shill
