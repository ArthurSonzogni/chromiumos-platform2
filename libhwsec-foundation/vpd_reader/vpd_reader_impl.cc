// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/vpd_reader/vpd_reader_impl.h"

#include <unistd.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <brillo/process/process.h>
#include <base/strings/string_split.h>

namespace hwsec_foundation {

namespace {

constexpr char kDefaultVpdPath[] = "/usr/sbin/vpd";

// Parses the output of `vpd -l` into a table of key-value pairs. If any parsing
// error occurs, returns `std::nullopt`.
std::optional<std::unordered_map<std::string, std::string> > ParsekeyValuePairs(
    const std::string vpd_output) {
  std::unordered_map<std::string, std::string> table;
  std::vector<std::string> pairs = base::SplitString(
      vpd_output, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // TODO(b/227463940): Rewrite with regex.
  for (const std::string& pair_str : pairs) {
    const size_t split_pos = pair_str.find('=');
    if (split_pos == std::string::npos) {
      LOG(ERROR) << __func__
                 << ": Missing '=' in vpd output line: " << pair_str;
      return std::nullopt;
    }
    // Make the string of key and value, respectively.
    std::string pair[2] = {pair_str.substr(0, split_pos),
                           pair_str.substr(split_pos + 1)};
    // Remove the the doule quote marks at head and tail after validate the
    // existence.
    for (std::string& key_or_value : pair) {
      if (key_or_value.size() < 2 || key_or_value.front() != '"' ||
          key_or_value.back() != '"') {
        LOG(ERROR) << __func__ << ": Missing double quotes in vpd output line: "
                   << pair_str;
        return std::nullopt;
      }
      key_or_value = key_or_value.substr(1, key_or_value.size() - 2);
    }
    table.emplace(std::move(pair[0]), std::move(pair[1]));
  }

  return table;
}

}  // namespace

VpdReaderImpl::VpdReaderImpl(std::unique_ptr<brillo::Process> process,
                             const std::string& vpd_path)
    : process_(std::move(process)), vpd_path_(vpd_path) {
  Prepare();
}

VpdReaderImpl::VpdReaderImpl()
    : VpdReaderImpl(std::make_unique<brillo::ProcessImpl>(), kDefaultVpdPath) {}

void VpdReaderImpl::Prepare() {
  if (table_.has_value()) {
    LOG(DFATAL) << __func__ << " should be called only once.";
    return;
  }

  // Invoke `vpd_path_`.
  process_->AddArg(vpd_path_);
  // List all key-value pairs in RO_VPD.
  process_->AddArg("-l");
  // Redirect the outputs to memory for later use.
  process_->RedirectUsingMemory(STDOUT_FILENO);
  process_->RedirectUsingMemory(STDERR_FILENO);
  if (process_->Run() != 0) {
    LOG(ERROR) << "Failed to run vpd: "
               << process_->GetOutputString(STDERR_FILENO);
    return;
  }
  table_ = ParsekeyValuePairs(process_->GetOutputString(STDOUT_FILENO));
}

std::optional<std::string> VpdReaderImpl::Get(const std::string& key) {
  // `Prepare()` should print error messages already.
  if (table_ == std::nullopt) {
    return std::nullopt;
  }
  if (table_->count(key) == 0) {
    LOG(WARNING) << __func__ << ": " << key << " missing in RO_VPD.";
    return std::nullopt;
  }
  return table_->at(key);
}

}  // namespace hwsec_foundation
