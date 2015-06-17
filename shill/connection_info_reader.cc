// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/connection_info_reader.h"

#include <netinet/in.h>

#include <limits>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "shill/file_reader.h"
#include "shill/logging.h"

using base::FilePath;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kLink;
static string ObjectID(ConnectionInfoReader *c) {
  return "(connection_info_reader)";
}
}

namespace {

const char kConnectionInfoFilePath[] = "/proc/net/ip_conntrack";
const char kSourceIPAddressTag[] = "src=";
const char kSourcePortTag[] = "sport=";
const char kDestinationIPAddressTag[] = "dst=";
const char kDestinationPortTag[] = "dport=";
const char kUnrepliedTag[] = "[UNREPLIED]";

}  // namespace

ConnectionInfoReader::ConnectionInfoReader() {}

ConnectionInfoReader::~ConnectionInfoReader() {}

FilePath ConnectionInfoReader::GetConnectionInfoFilePath() const {
  return FilePath(kConnectionInfoFilePath);
}

bool ConnectionInfoReader::LoadConnectionInfo(
    vector<ConnectionInfo> *info_list) {
  info_list->clear();

  FilePath info_file_path = GetConnectionInfoFilePath();
  FileReader file_reader;
  if (!file_reader.Open(info_file_path)) {
    SLOG(this, 2) << __func__ << ": Failed to open '"
                  << info_file_path.value() << "'.";
    return false;
  }

  string line;
  while (file_reader.ReadLine(&line)) {
    ConnectionInfo info;
    if (ParseConnectionInfo(line, &info))
      info_list->push_back(info);
  }
  return true;
}

bool ConnectionInfoReader::ParseConnectionInfo(const string &input,
                                               ConnectionInfo *info) {
  vector<string> tokens;
  base::SplitStringAlongWhitespace(input, &tokens);
  if (tokens.size() < 10) {
    return false;
  }

  int index = 0;

  int protocol = 0;
  if (!ParseProtocol(tokens[++index], &protocol)) {
    return false;
  }
  info->set_protocol(protocol);

  int64_t time_to_expire_seconds = 0;
  if (!ParseTimeToExpireSeconds(tokens[++index], &time_to_expire_seconds)) {
    return false;
  }
  info->set_time_to_expire_seconds(time_to_expire_seconds);

  if (protocol == IPPROTO_TCP)
    ++index;

  IPAddress ip_address(IPAddress::kFamilyUnknown);
  uint16_t port = 0;
  bool is_source = false;

  if (!ParseIPAddress(tokens[++index], &ip_address, &is_source) || !is_source) {
    return false;
  }
  info->set_original_source_ip_address(ip_address);
  if (!ParseIPAddress(tokens[++index], &ip_address, &is_source) || is_source) {
    return false;
  }
  info->set_original_destination_ip_address(ip_address);

  if (!ParsePort(tokens[++index], &port, &is_source) || !is_source) {
    return false;
  }
  info->set_original_source_port(port);
  if (!ParsePort(tokens[++index], &port, &is_source) || is_source) {
    return false;
  }
  info->set_original_destination_port(port);

  if (tokens[index + 1] == kUnrepliedTag) {
    info->set_is_unreplied(true);
    ++index;
  } else {
    info->set_is_unreplied(false);
  }

  if (!ParseIPAddress(tokens[++index], &ip_address, &is_source) || !is_source) {
    return false;
  }
  info->set_reply_source_ip_address(ip_address);
  if (!ParseIPAddress(tokens[++index], &ip_address, &is_source) || is_source) {
    return false;
  }
  info->set_reply_destination_ip_address(ip_address);

  if (!ParsePort(tokens[++index], &port, &is_source) || !is_source) {
    return false;
  }
  info->set_reply_source_port(port);
  if (!ParsePort(tokens[++index], &port, &is_source) || is_source) {
    return false;
  }
  info->set_reply_destination_port(port);

  return true;
}

bool ConnectionInfoReader::ParseProtocol(const string &input, int *protocol) {
  if (!base::StringToInt(input, protocol) ||
      *protocol < 0 || *protocol >= IPPROTO_MAX) {
    return false;
  }
  return true;
}

bool ConnectionInfoReader::ParseTimeToExpireSeconds(
    const string &input, int64_t *time_to_expire_seconds) {
  if (!base::StringToInt64(input, time_to_expire_seconds) ||
      *time_to_expire_seconds < 0) {
    return false;
  }
  return true;
}

bool ConnectionInfoReader::ParseIPAddress(
    const string &input, IPAddress *ip_address, bool *is_source) {
  string ip_address_string;

  if (base::StartsWithASCII(input, kSourceIPAddressTag, false)) {
    *is_source = true;
    ip_address_string = input.substr(strlen(kSourceIPAddressTag));
  } else if (base::StartsWithASCII(input, kDestinationIPAddressTag, false)) {
    *is_source = false;
    ip_address_string = input.substr(strlen(kDestinationIPAddressTag));
  } else {
    return false;
  }

  IPAddress ipv4_address(IPAddress::kFamilyIPv4);
  if (ipv4_address.SetAddressFromString(ip_address_string)) {
    *ip_address = ipv4_address;
    return true;
  }

  IPAddress ipv6_address(IPAddress::kFamilyIPv6);
  if (ipv6_address.SetAddressFromString(ip_address_string)) {
    *ip_address = ipv6_address;
    return true;
  }

  return false;
}

bool ConnectionInfoReader::ParsePort(
    const string &input, uint16_t *port, bool *is_source) {
  int result = 0;
  string port_string;

  if (base::StartsWithASCII(input, kSourcePortTag, false)) {
    *is_source = true;
    port_string = input.substr(strlen(kSourcePortTag));
  } else if (base::StartsWithASCII(input, kDestinationPortTag, false)) {
    *is_source = false;
    port_string = input.substr(strlen(kDestinationPortTag));
  } else {
    return false;
  }

  if (!base::StringToInt(port_string, &result) ||
      result < 0 || result > std::numeric_limits<uint16_t>::max()) {
    return false;
  }

  *port = result;
  return true;
}

}  // namespace shill
