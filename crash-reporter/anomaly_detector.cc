// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/anomaly_detector.h"

#include <utility>

#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <metrics/metrics_library.h>
#include <re2/re2.h>

namespace {

// This hashing algorithm dates back to before this was migrated from C to C++.
// We're stuck with it now because we store the hash values in UMA, and we would
// like the hashes to remain the same over time for a given crash.
uint32_t StringHash(const std::string& input) {
  uint32_t hash = 0;
  for (auto& c : input) {
    hash = (hash << 5) + hash + c;
  }
  return hash;
}

std::string OnlyAsciiAlpha(std::string s) {
  s.erase(std::remove_if(s.begin(), s.end(),
                         [](char c) -> bool { return !base::IsAsciiAlpha(c); }),
          s.end());
  return s;
}

}  // namespace

namespace anomaly {

Parser::~Parser() {}

// We expect only a handful of different anomalies per boot session, so the
// probability of a collision is very low, and statistically it won't matter
// (unless anomalies with the same hash also happens in tandem, which is even
// rarer).
bool Parser::WasAlreadySeen(uint32_t hash) {
  size_t bit_index = hash % HASH_BITMAP_SIZE;
  bool return_val = hash_bitmap_[bit_index];
  hash_bitmap_.set(bit_index, true);
  return return_val;
}

constexpr LazyRE2 service_failure = {
    R"((\S+) \S+ process \(\d+\) terminated with status (\d+))"};

MaybeCrashReport ServiceParser::ParseLogEntry(const std::string& line) {
  std::string service_name;
  std::string exit_status;
  if (!RE2::FullMatch(line, *service_failure, &service_name, &exit_status))
    return base::nullopt;

  uint32_t hash = StringHash(service_name.c_str());

  MetricsLibrary().SendSparseToUMA("Platform.ServiceFailureHashes", hash);

  if (WasAlreadySeen(hash))
    return base::nullopt;

  std::string text = base::StringPrintf(
      "%08x-exit%s-%s\n", hash, exit_status.c_str(), service_name.c_str());
  std::string flag;
  if (base::StartsWith(service_name, "arc-", base::CompareCase::SENSITIVE))
    flag = "--arc_service_failure=" + service_name;
  else
    flag = "--service_failure=" + service_name;
  return {{std::move(text), std::move(flag)}};
}

std::string GetField(const std::string& line, std::string pattern) {
  std::string field_value;
  RE2::PartialMatch(line, pattern, &field_value);
  // This will return the empty string if there wasn't a match.
  return field_value;
}

constexpr LazyRE2 granted = {"avc:[ ]*granted"};

MaybeCrashReport SELinuxParser::ParseLogEntry(const std::string& line) {
  std::string only_alpha = OnlyAsciiAlpha(line);
  uint32_t hash = StringHash(only_alpha.c_str());
  MetricsLibrary().SendSparseToUMA("Platform.SELinuxViolationHashes", hash);
  if (WasAlreadySeen(hash))
    return base::nullopt;
  std::string signature;

  // This case is strange: the '-' is only added if 'granted' was present.
  if (RE2::PartialMatch(line, *granted))
    signature += "granted-";

  std::string scontext = GetField(line, R"(scontext=(\S*))");
  std::string tcontext = GetField(line, R"(tcontext=(\S*))");
  std::string permission = GetField(line, R"(\{ (\S*) \})");
  std::string comm = GetField(line, R"'(comm="([^"]*)")'");
  std::string name = GetField(line, R"'(name="([^"]*)")'");

  signature += base::JoinString({scontext, tcontext, permission,
                                 OnlyAsciiAlpha(comm), OnlyAsciiAlpha(name)},
                                "-");
  std::string text =
      base::StringPrintf("%08x-selinux-%s\n", hash, signature.c_str());

  if (!comm.empty())
    text += "comm\x01" + comm + "\x02";
  if (!name.empty())
    text += "name\x01" + name + "\x02";
  if (!scontext.empty())
    text += "scontext\x01" + scontext + "\x02";
  if (!tcontext.empty())
    text += "tcontext\x01" + tcontext + "\x02";
  text += "\n";
  text += line;

  return {{std::move(text), "--selinux_violation"}};
}

std::string DetermineFlag(const std::string& info) {
  if (info.find("drivers/net/wireless") != std::string::npos)
    return "--kernel_wifi_warning";
  if (info.find("drivers/idle") != std::string::npos)
    return "--kernel_suspend_warning";
  return "--kernel_warning";
}

constexpr char cut_here[] = "------------[ cut here";
constexpr char end_trace[] = "---[ end trace";

// The CPU and PID information got added in the 3.11 kernel development cycle
// per commit dcb6b45254e2281b6f99ea7f2d51343954aa3ba8. That part is marked
// optional to make sure the old format still gets accepted. Once we no longer
// care about kernel version 3.10 and earlier, we can update the code to require
// CPU and PID to be present unconditionally.
constexpr LazyRE2 header = {R"(^WARNING:(?: CPU: \d+ PID: \d+)? at (.+))"};

MaybeCrashReport KernelParser::ParseLogEntry(const std::string& line) {
  if (last_line_ == LineType::None) {
    if (line.find(cut_here) == 0)
      last_line_ = LineType::Start;
  } else if (last_line_ == LineType::Start) {
    std::string info;
    if (RE2::FullMatch(line, *header, &info)) {
      // The info string looks like: "file:line func+offset/offset() [mod]".
      // The [mod] suffix is only present if the address is located within a
      // kernel module.
      uint32_t hash = StringHash(info.c_str());
      MetricsLibrary().SendSparseToUMA("Platform.KernelWarningHashes", hash);
      if (WasAlreadySeen(hash)) {
        last_line_ = LineType::None;
        return base::nullopt;
      }
      flag_ = DetermineFlag(info);

      size_t spacep = info.find(" ");
      bool found = spacep != std::string::npos;
      auto function = found ? info.substr(spacep + 1) : "unknown-function";

      text_ += base::StringPrintf("%08x-%s\n", hash, function.c_str());
      text_ += info;
      last_line_ = LineType::Body;
    } else {
      last_line_ = LineType::None;
    }
  } else if (last_line_ == LineType::Body) {
    if (line.find(end_trace) == 0) {
      last_line_ = LineType::None;
      std::string text_tmp;
      text_tmp.swap(text_);
      return {{std::move(text_tmp), std::move(flag_)}};
    }
    text_ += line + "\n";
  }

  return base::nullopt;
}

}  // namespace anomaly
