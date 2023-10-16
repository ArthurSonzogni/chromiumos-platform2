// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/capport_client.h"

#include <compare>
#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/task/single_thread_task_runner.h>

namespace shill {
namespace {

CapportClient::Result ConvertFromCapportStatus(const CapportStatus& status) {
  return CapportClient::Result{
      .state = status.is_captive ? CapportClient::State::kClosed
                                 : CapportClient::State::kOpen,
      .user_portal_url = status.user_portal_url,
      .venue_info_url = status.venue_info_url,
  };
}

CapportClient::Result GetFailedResult() {
  CapportClient::Result result;
  result.state = CapportClient::State::kFailed;
  return result;
}

}  // namespace

bool CapportClient::Result::operator==(const CapportClient::Result&) const =
    default;
bool CapportClient::Result::operator!=(const CapportClient::Result&) const =
    default;

CapportClient::CapportClient(std::unique_ptr<CapportProxy> proxy,
                             ResultCallback result_callback,
                             std::string_view logging_tag)
    : proxy_(std::move(proxy)),
      result_callback_(std::move(result_callback)),
      logging_tag_(std::string(logging_tag)) {}

CapportClient::~CapportClient() = default;

void CapportClient::QueryCapport() {
  if (proxy_->IsRunning()) {
    LOG(WARNING) << logging_tag_ << "The previous query is not finished";
    return;
  }

  // Using base::Unretained(this) is safe, because |proxy_| won't call the
  // callback after the proxy is destroyed, and the proxy is destroyed prior
  // than |*this| is destroyed.
  proxy_->SendRequest(
      base::BindOnce(&CapportClient::OnStatusReceived, base::Unretained(this)));
}

void CapportClient::OnStatusReceived(std::optional<CapportStatus> status) {
  if (!status.has_value()) {
    LOG(ERROR) << logging_tag_ << "Failed to get result from CAPPORT server";
    result_callback_.Run(GetFailedResult());
    return;
  }
  if (status->is_captive && !status->user_portal_url.has_value()) {
    LOG(WARNING) << logging_tag_
                 << "The user_portal_url is missing when the is_captive is set";
    result_callback_.Run(GetFailedResult());
    return;
  }

  result_callback_.Run(ConvertFromCapportStatus(*status));
}

}  // namespace shill
