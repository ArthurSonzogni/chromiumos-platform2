// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/carrier_entitlement.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/functional/callback_helpers.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <brillo/http/http_utils.h>
#include <brillo/http/http_request.h>

#include "shill/cellular/cellular.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/metrics.h"
#include "shill/network/network.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
}  // namespace Logging

CarrierEntitlement::CarrierEntitlement(
    Cellular* cellular,
    Metrics* metrics,
    base::RepeatingCallback<void(Result)> check_cb)
    : cellular_(cellular),
      metrics_(metrics),
      check_cb_(check_cb),
      transport_(brillo::http::Transport::CreateDefault()),
      request_in_progress_(false),
      weak_ptr_factory_(this) {}

CarrierEntitlement::~CarrierEntitlement() {
  // cancel pending request if it exists
  transport_->CancelRequest(request_id_);
  background_check_cancelable.Cancel();
}

EventDispatcher* CarrierEntitlement::dispatcher() {
  return cellular_->dispatcher();
}

void CarrierEntitlement::Check(
    const MobileOperatorMapper::EntitlementConfig& config) {
  config_ = config;

  CheckInternal(/* user_triggered */ true);
}

void CarrierEntitlement::CheckInternal(bool user_triggered) {
  SLOG(3) << __func__;
  if (request_in_progress_) {
    LOG(WARNING)
        << "Entitlement check already in progress. New request ignored.";
    metrics_->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckInProgress);
    // The new request is ignored, but the client will receive an update
    // when the previous request completes.
    return;
  }
  // Reset the cache value on a background check.
  if (!user_triggered) {
    last_result_ = Result::kGenericError;
    LOG(INFO) << "Initiating a background entitlement check.";
  }

  if (config_.url.empty()) {
    SLOG(3) << "Carrier doesn't require an entitlement check.";
    // Skip reporting metrics, since this result would dominate the results,
    // and it's not a very useful value to know.
    SendResult(Result::kAllowed);
    return;
  }

  std::unique_ptr<base::Value> content = BuildContentPayload(config_.params);
  if (!content) {
    LOG(ERROR) << "Failed to build entitlement check message.";
    SendResult(Result::kGenericError);
    metrics_->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckFailedToBuildPayload);
    return;
  }
  auto network = cellular_->GetPrimaryNetwork();
  if (!network) {
    LOG(ERROR)
        << "Cannot run entitlement check because Network object is missing";
    SendResult(Result::kNetworkNotReady);
    metrics_->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckNoNetwork);
    return;
  }

  if (!network->IsConnected()) {
    LOG(ERROR)
        << "Cannot run entitlement check because the network is not connected";
    SendResult(Result::kNetworkNotReady);
    metrics_->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckNetworkNotConnected);
    return;
  }

  if (!network->HasInternetConnectivity()) {
    LOG(ERROR) << "Cannot run entitlement check because cellular is not online";
    SendResult(Result::kNetworkNotReady);
    metrics_->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckNetworkNotOnline);
    return;
  }

  std::vector<std::string> dns_list_str;
  for (auto& ip : network->GetDNSServers()) {
    dns_list_str.push_back(ip.ToString());
  }
  transport_->SetDnsServers(dns_list_str);
  transport_->SetDnsInterface(network->interface_name());
  transport_->SetInterface(network->interface_name());
  transport_->UseCustomCertificate(brillo::http::Transport::Certificate::kNss);

  transport_->SetDefaultTimeout(kHttpRequestTimeout);
  auto cb_http_success =
      base::BindOnce(&CarrierEntitlement::HttpRequestSuccessCallback,
                     weak_ptr_factory_.GetWeakPtr());
  auto cb_http_error =
      base::BindOnce(&CarrierEntitlement::HttpRequestErrorCallback,
                     weak_ptr_factory_.GetWeakPtr());
  request_in_progress_ = true;
  if (config_.method == brillo::http::request_type::kGet) {
    // No content is sent.
    request_id_ =
        brillo::http::Get(config_.url, {} /*headers*/, transport_,
                          std::move(cb_http_success), std::move(cb_http_error));
  } else {
    request_id_ = brillo::http::PostJson(
        config_.url, std::move(content), {} /*headers*/, transport_,
        std::move(cb_http_success), std::move(cb_http_error));
  }
}

void CarrierEntitlement::PostBackgroundCheck() {
  background_check_cancelable.Reset(base::BindOnce(
      &CarrierEntitlement::CheckInternal, weak_ptr_factory_.GetWeakPtr(),
      /* user_triggered */ false));
  dispatcher()->PostDelayedTask(FROM_HERE,
                                background_check_cancelable.callback(),
                                kBackgroundCheckPeriod);
}

void CarrierEntitlement::Reset() {
  SLOG(3) << __func__;
  // cancel pending request if it exists
  transport_->CancelRequest(request_id_);
  last_result_ = Result::kGenericError;
  background_check_cancelable.Cancel();
  request_in_progress_ = false;
}

std::unique_ptr<base::Value> CarrierEntitlement::BuildContentPayload(
    const Stringmap& params) {
  base::Value::Dict dict;
  for (auto pair : params)
    dict.Set(pair.first, pair.second);

  return std::make_unique<base::Value>(std::move(dict));
}

void CarrierEntitlement::SendResult(Result result) {
  request_in_progress_ = false;
  dispatcher()->PostTask(FROM_HERE, base::BindOnce(check_cb_, result));
}

void CarrierEntitlement::HttpRequestSuccessCallback(
    brillo::http::RequestID request_id,
    std::unique_ptr<brillo::http::Response> response) {
  DCHECK(request_id == request_id_);
  if (request_id != request_id_) {
    LOG(ERROR) << "EntitlementCheck: Expected request ID " << request_id_
               << " but got " << request_id;
    SendResult(Result::kGenericError);
    metrics_->NotifyCellularEntitlementCheckResult(
        Metrics::kCellularEntitlementCheckUnexpectedRequestId);
    return;
  }

  int http_status_code = response->GetStatusCode();
  std::string response_code;
  base::TrimString(response->ExtractDataAsString(), "\r\n ", &response_code);
  SLOG(3) << __func__ << " status_code:" << http_status_code
          << ". response text:" << response_code;

  switch (http_status_code) {
    case brillo::http::status_code::Ok:
      last_result_ = Result::kAllowed;
      metrics_->NotifyCellularEntitlementCheckResult(
          Metrics::kCellularEntitlementCheckAllowed);
      PostBackgroundCheck();
      break;
    case brillo::http::status_code::Forbidden:
      if (response_code == kServerCodeUserNotAllowedToTether) {
        last_result_ = Result::kUserNotAllowedToTether;
        LOG(INFO) << __func__ << ": User not allowed to tether.";
        metrics_->NotifyCellularEntitlementCheckResult(
            Metrics::kCellularEntitlementCheckUserNotAllowedToTether);
      } else if (response_code == kServerCodeHttpSyntaxError) {
        last_result_ = Result::kGenericError;
        LOG(INFO) << __func__ << ": Syntax error of HTTP Request.";
        metrics_->NotifyCellularEntitlementCheckResult(
            Metrics::kCellularEntitlementCheckHttpSyntaxErrorOnServer);
      } else if (response_code == kServerCodeUnrecognizedUser) {
        last_result_ = Result::kUnrecognizedUser;
        LOG(INFO) << __func__ << ": Unrecognized User.";
        metrics_->NotifyCellularEntitlementCheckResult(
            Metrics::kCellularEntitlementCheckUnrecognizedUser);
      } else if (response_code == kServerCodeInternalError) {
        LOG(INFO) << __func__ << ": Server error. Using cached value";
        metrics_->NotifyCellularEntitlementCheckResult(
            Metrics::kCellularEntitlementCheckInternalErrorOnServer);
      } else {
        last_result_ = Result::kGenericError;
        LOG(INFO) << __func__ << ": Unrecognized error:" << response_code;
        metrics_->NotifyCellularEntitlementCheckResult(
            Metrics::kCellularEntitlementCheckUnrecognizedErrorCode);
      }
      break;
    default:
      LOG(INFO) << __func__ << ": Unrecognized http status code.";
      metrics_->NotifyCellularEntitlementCheckResult(
          Metrics::kCellularEntitlementCheckUnrecognizedHttpStatusCode);
      last_result_ = Result::kGenericError;
      break;
  }
  SendResult(last_result_);
}

void CarrierEntitlement::HttpRequestErrorCallback(
    brillo::http::RequestID request_id, const brillo::Error* error) {
  // On a request failure, the result will be the cached value.
  if (request_id != request_id_) {
    LOG(ERROR) << "EntitlementCheck: Expected request ID " << request_id_
               << " but got " << request_id;
  } else {
    LOG(ERROR) << "Entitlement check failed with error code :"
               << error->GetCode() << ":" << error->GetMessage();
  }
  SendResult(last_result_);
  metrics_->NotifyCellularEntitlementCheckResult(
      Metrics::kCellularEntitlementCheckHttpRequestError);
}

}  // namespace shill
