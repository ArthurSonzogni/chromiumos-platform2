// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/samba_helper.h"

#include <vector>

#include <base/guid.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "authpolicy/anonymizer.h"

namespace {

// Map GUID position to octet position for each byte xx.
// The bytes of the first 3 groups have to be reversed.
// GUID:
//   |0    |6 |9|1114|1619|21|24       |34
//   xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
// Octet:
//    |1       |10|13|16|19|22|25|28|31            |46
//   \XX\XX\XX\XX\XX\XX\XX\XX\XX\XX\XX\XX\XX\XX\XX\XX
// clang-format off
const int octet_pos_map[16][2] = {  // Maps GUID position to octet position.
  {0, 10}, {2, 7}, {4, 4}, {6, 1},  // First group, reversed byte order.
  {9, 16}, {11, 13},                // Second group, reversed byte order.
  {14, 22}, {16, 19},               // Third group, reversed byte order.
  {19, 25}, {21, 28},               // Fourth group, same byte order.
  {24, 31}, {26, 34}, {28, 37}, {30, 40}, {32, 43}, {34, 46}};  // Last group.
// clang-format on

const size_t kGuidSize = 36;   // 16 bytes, xx each byte, plus 4 '-'.
const size_t kOctetSize = 48;  // 16 bytes, \XX each byte.

}  // namespace

namespace authpolicy {

// Prefix for Active Directory account ids. A prefixed |account_id| is usually
// called |account_id_key|. Must match Chromium AccountId::kKeyAdIdPrefix.
const char kActiveDirectoryPrefix[] = "a-";

// Flags for parsing GPO.
const char* const kGpFlagsStr[] = {
    "0 GPFLAGS_ALL_ENABLED",
    "1 GPFLAGS_USER_SETTINGS_DISABLED",
    "2 GPFLAGS_MACHINE_SETTINGS_DISABLED",
    "3 GPFLAGS_ALL_DISABLED",
};

bool ParseUserPrincipalName(const std::string& user_principal_name,
                            std::string* user_name,
                            std::string* realm,
                            std::string* normalized_user_principal_name) {
  std::vector<std::string> parts = base::SplitString(
      user_principal_name, "@", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() != 2 || parts.at(0).empty() || parts.at(1).empty()) {
    // Don't log user_principal_name, it might contain sensitive data.
    LOG(ERROR) << "Failed to parse user principal name. Expected form "
                  "'user@some.realm'.";
    return false;
  }
  *user_name = parts.at(0);
  *realm = base::ToUpperASCII(parts.at(1));
  *normalized_user_principal_name = *user_name + "@" + *realm;
  return true;
}

bool FindToken(const std::string& in_str,
               char token_separator,
               const std::string& token,
               std::string* result) {
  std::vector<std::string> lines = base::SplitString(
      in_str, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const std::string& line : lines) {
    if (FindTokenInLine(line, token_separator, token, result))
      return true;
  }

  // Don't log in_str, it might contain sensitive data.
  LOG(ERROR) << "Failed to find '" << token << "' in string";
  return false;
}

bool FindTokenInLine(const std::string& in_line,
                     char token_separator,
                     const std::string& token,
                     std::string* result) {
  size_t sep_pos = in_line.find(token_separator);
  if (sep_pos == std::string::npos)
    return false;

  std::string line_token;
  base::TrimWhitespaceASCII(in_line.substr(0, sep_pos), base::TRIM_ALL,
                            &line_token);
  if (line_token != token)
    return false;

  base::TrimWhitespaceASCII(in_line.substr(sep_pos + 1), base::TRIM_ALL,
                            result);
  return !result->empty();
}

bool ParseGpoVersion(const std::string& str, unsigned int* version) {
  DCHECK(version);
  *version = 0;
  unsigned int version_hex = 0;
  if (sscanf(str.c_str(), "%u (0x%08x)", version, &version_hex) != 2 ||
      *version != version_hex)
    return false;

  return true;
}

bool ParseGpFlags(const std::string& str, int* gp_flags) {
  for (int flag = 0; flag < static_cast<int>(arraysize(kGpFlagsStr)); ++flag) {
    if (str == kGpFlagsStr[flag]) {
      *gp_flags = flag;
      return true;
    }
  }
  return false;
}

bool Contains(const std::string& str, const std::string& substr) {
  return str.find(substr) != std::string::npos;
}

std::string GuidToOctetString(const std::string& guid) {
  std::string octet_str;
  if (!base::IsValidGUID(guid))
    return octet_str;
  DCHECK_EQ(kGuidSize, guid.size());

  octet_str.assign(kOctetSize, '\\');
  for (size_t n = 0; n < arraysize(octet_pos_map); ++n) {
    for (int hex_digit = 0; hex_digit < 2; ++hex_digit) {
      octet_str.at(octet_pos_map[n][1] + hex_digit) =
          toupper(guid.at(octet_pos_map[n][0] + hex_digit));
    }
  }

  return octet_str;
}

std::string OctetStringToGuidForTesting(const std::string& octet_str) {
  std::string guid;
  if (octet_str.size() != kOctetSize)
    return guid;

  guid.assign(kGuidSize, '-');
  for (size_t n = 0; n < arraysize(octet_pos_map); ++n) {
    for (int hex_digit = 0; hex_digit < 2; ++hex_digit) {
      guid.at(octet_pos_map[n][0] + hex_digit) =
          tolower(octet_str.at(octet_pos_map[n][1] + hex_digit));
    }
  }
  return guid;
}

std::string GetAccountIdKey(const std::string& account_id) {
  return kActiveDirectoryPrefix + account_id;
}

void LogLongString(const std::string& header,
                   const std::string& str,
                   Anonymizer* anonymizer) {
  if (!LOG_IS_ON(INFO))
    return;

  std::string anonymized_str = anonymizer->Process(str);
  std::vector<std::string> lines = base::SplitString(
      anonymized_str, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (lines.size() <= 1) {
    LOG(INFO) << header << anonymized_str;
  } else {
    LOG(INFO) << header;
    for (const std::string& line : lines)
      LOG(INFO) << "  " << line;
  }
}

}  // namespace authpolicy
