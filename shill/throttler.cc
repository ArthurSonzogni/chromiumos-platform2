// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/throttler.h"

#include <stdlib.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "shill/logging.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kTC;
}  // namespace Logging

namespace {
constexpr std::string_view kTCCleanUpCmds[] = {
    "qdisc del dev ${INTERFACE} root\n",
    "qdisc del dev ${INTERFACE} ingress\n"};

// For fq_codel quantum 300 gives a boost to interactive flows
// Only works for bandwidths < 50 Mbps.
constexpr std::string_view kTCThrottleUplinkCmds[] = {
    "qdisc add dev ${INTERFACE} root handle 1: htb default 11\n",
    "class add dev ${INTERFACE} parent 1: classid 1:1 htb rate ${ULRATE}\n",
    ("class add dev ${INTERFACE} parent 1:1 classid 1:11 htb rate ${ULRATE} "
     "prio 0 quantum 300\n")};

constexpr std::string_view kTCThrottleDownlinkCmds[] = {
    "qdisc add dev ${INTERFACE} handle ffff: ingress\n",
    "filter add dev ${INTERFACE} parent ffff: protocol all"
    " prio 50 u32 match ip"
    " src 0.0.0.0/0 police rate ${DLRATE} burst ${BURST}k mtu 66000"
    " drop flowid :1\n"};

constexpr std::string_view kTemplateInterface = "${INTERFACE}";
constexpr std::string_view kTemplateULRate = "${ULRATE}";
constexpr std::string_view kTemplateDLRate = "${DLRATE}";
constexpr std::string_view kTemplateBurst = "${BURST}";

// Generates the TC commands to throttle the interface with upload/download
// bitrates.
std::vector<std::string> GenerateThrottleCommands(
    std::string_view interface,
    uint32_t upload_rate_kbits,
    uint32_t download_rate_kbits) {
  std::vector<std::string> commands;

  // Easier to clean up first and start afresh than issue tc changes.
  for (const auto& command_templete : kTCCleanUpCmds) {
    std::string command(command_templete);
    base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateInterface,
                                       interface);
    commands.push_back(command);
  }

  // Add commands for upload(egress) queueing disciplines
  // and filters
  if (upload_rate_kbits) {
    const std::string ulrate(base::NumberToString(upload_rate_kbits) + "kbit");
    for (const auto& command_templete : kTCThrottleUplinkCmds) {
      std::string command(command_templete);
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateInterface,
                                         interface);
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateULRate, ulrate);
      commands.push_back(command);
    }
  }

  // Add commands for download(ingress) queueing disciplines
  // and filters
  if (download_rate_kbits) {
    const std::string dlrate(base::NumberToString(download_rate_kbits) +
                             "kbit");
    const std::string to_burst(base::NumberToString(download_rate_kbits * 2));
    for (const auto& command_templete : kTCThrottleDownlinkCmds) {
      std::string command(command_templete);
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateInterface,
                                         interface);
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateDLRate, dlrate);
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateBurst, to_burst);
      commands.push_back(command);
    }
  }

  return commands;
}

//  Generates the TC commands to disable the throttling on |interfaces|.
std::vector<std::string> GenerateDisabledThrottlingCommands(
    const std::vector<std::string>& interfaces) {
  std::vector<std::string> commands;
  for (const auto& interface_name : interfaces) {
    for (const auto& command_templete : kTCCleanUpCmds) {
      std::string command(command_templete);
      base::ReplaceSubstringsAfterOffset(&command, 0, kTemplateInterface,
                                         interface_name);
      commands.push_back(command);
    }
  }
  return commands;
}

}  // namespace

Throttler::Throttler(std::unique_ptr<TCProcessFactory> tc_process_factory)
    : tc_process_factory_(std::move(tc_process_factory)) {
  SLOG(2) << __func__;
}

Throttler::~Throttler() {
  SLOG(2) << __func__;
}

bool Throttler::DisableThrottlingOnAllInterfaces(
    ResultCallback callback, const std::vector<std::string>& interfaces) {
  if (!callback_.is_null()) {
    ResetAndReply(Error::kOperationAborted, "Aborted by the following request");
  }

  callback_ = std::move(callback);
  upload_rate_kbits_ = 0;
  download_rate_kbits_ = 0;

  if (interfaces.empty()) {
    std::move(callback_).Run(Error(Error::kSuccess, "", FROM_HERE));
    return true;
  }
  return StartTCProcess(GenerateDisabledThrottlingCommands(interfaces));
}

bool Throttler::ThrottleInterfaces(ResultCallback callback,
                                   uint32_t upload_rate_kbits,
                                   uint32_t download_rate_kbits,
                                   const std::vector<std::string>& interfaces) {
  // At least one of upload/download should be throttled.
  // 0 value indicates no throttling.
  if ((upload_rate_kbits == 0) && (download_rate_kbits == 0)) {
    std::move(callback).Run(Error(Error::kInvalidArguments,
                                  "One of download/upload rates should be set",
                                  FROM_HERE));
    return false;
  }
  if (interfaces.empty()) {
    std::move(callback).Run(Error(Error::kOperationFailed,
                                  "No interfaces available for throttling",
                                  FROM_HERE));
    return false;
  }

  if (!callback_.is_null()) {
    ResetAndReply(Error::kOperationAborted, "Aborted by the following request");
  }

  callback_ = std::move(callback);
  upload_rate_kbits_ = upload_rate_kbits;
  download_rate_kbits_ = download_rate_kbits;
  pending_throttled_interfaces_ = interfaces;

  ThrottleNextPendingInterface();
  return true;
}

bool Throttler::ApplyThrottleToNewInterface(const std::string& interface) {
  if (upload_rate_kbits_ == 0 && download_rate_kbits_ == 0) {
    return false;
  }

  pending_throttled_interfaces_.push_back(interface);
  // If there is no pending throttling task, then trigger the throttling task.
  if (callback_.is_null()) {
    callback_ = base::DoNothing();
    ThrottleNextPendingInterface();
  }
  return true;
}

void Throttler::ThrottleNextPendingInterface() {
  CHECK(!pending_throttled_interfaces_.empty());
  CHECK(!callback_.is_null());

  const std::string interface_name = pending_throttled_interfaces_.back();
  pending_throttled_interfaces_.pop_back();

  StartTCProcess(GenerateThrottleCommands(interface_name, upload_rate_kbits_,
                                          download_rate_kbits_));
}

bool Throttler::StartTCProcess(const std::vector<std::string>& commands) {
  CHECK(!callback_.is_null());

  // Drop the previous process and its callback if it exists.
  weak_ptr_factory_.InvalidateWeakPtrs();
  tc_process_.reset();

  tc_process_ = tc_process_factory_->Create(
      commands, base::BindOnce(&Throttler::OnTCProcessExited,
                               weak_ptr_factory_.GetWeakPtr()));
  if (!tc_process_) {
    ResetAndReply(Error::kOperationFailed, "Failed to start TC process");
    return false;
  }
  return true;
}

void Throttler::OnTCProcessExited(int exit_status) {
  CHECK(!callback_.is_null());

  // We do the best effort to throttle on all the remaining interfaces, even the
  // previous one failed.
  if (exit_status != 0) {
    LOG(ERROR) << "Throttler failed with status: " << exit_status;
  }

  if (pending_throttled_interfaces_.empty()) {
    ResetAndReply(Error::kSuccess, "");
  } else {
    ThrottleNextPendingInterface();
  }
}

void Throttler::ResetAndReply(Error::Type error_type,
                              std::string_view message) {
  CHECK(!callback_.is_null());

  weak_ptr_factory_.InvalidateWeakPtrs();
  tc_process_.reset();
  pending_throttled_interfaces_.clear();

  const Error error(error_type, message, FROM_HERE);
  if (error_type != Error::kSuccess) {
    error.Log();
  }
  std::move(callback_).Run(error);
}

}  // namespace shill
