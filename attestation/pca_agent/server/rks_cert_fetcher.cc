// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/pca_agent/server/rks_cert_fetcher.h"

#include <memory>
#include <utility>
#include <vector>

#include <attestation/proto_bindings/interface.pb.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/http/http_transport.h>
#include <brillo/http/http_request.h>
#include <brillo/http/http_utils.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/shill/dbus-constants.h>
#include <shill/dbus-proxies.h>

namespace attestation {
namespace pca_agent {

namespace {

using OnCertFetchedCallback =
    base::RepeatingCallback<void(const RksCertificateAndSignature&)>;

// The "CryptAuth Vault Service", which we refers to "recoverable key store
// service" in this codebase, hosts its endpoint certificates at this url. It
// will periodically rotate, and old certificates might become outdated after a
// while.
constexpr char kCertXmlUrl[] =
    "https://www.gstatic.com/cryptauthvault/v0/cert.xml";
// A separate signature file is hosted at this url, to provide integrity check
// on the certificate file above.
constexpr char kSignatureXmlUrl[] =
    "https://www.gstatic.com/cryptauthvault/v0/cert.sig.xml";

// The server-side certificate updates every few months, so it is frequent
// enough to fetch certificates once per day. If the fetch request failed, retry
// in 10 minutes.
constexpr base::TimeDelta kPeriodicFetchInterval = base::Days(1);
constexpr base::TimeDelta kFetchFailedRetryInterval = base::Minutes(10);

std::optional<std::string> ExtractDataFromResponse(
    std::unique_ptr<brillo::http::Response> response) {
  int status_code = response->GetStatusCode();
  if (status_code != brillo::http::status_code::Ok) {
    LOG(ERROR) << "Request failed with status code: " << status_code << ".";
    return std::nullopt;
  }
  return response->ExtractDataAsString();
}

}  // namespace

RksCertificateFetcher::RksCertificateFetcher(
    std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
        manager_proxy)
    : manager_proxy_(std::move(manager_proxy)) {}

void RksCertificateFetcher::StartFetching(
    OnCertFetchedCallback on_cert_fetched) {
  http_utils_->GetChromeProxyServersAsync(
      kCertXmlUrl, base::BindOnce(&RksCertificateFetcher::OnGetProxyServers,
                                  weak_factory_.GetWeakPtr(), on_cert_fetched));
}

std::shared_ptr<brillo::http::Transport> RksCertificateFetcher::GetTransport() {
  if (proxy_servers_.empty()) {
    return transport_factory_->CreateWithProxy(brillo::http::kDirectProxy);
  }
  return transport_factory_->CreateWithProxy(proxy_servers_[0]);
}

void RksCertificateFetcher::GetChromeProxyServersAsync(
    const std::string& url,
    brillo::http::GetChromeProxyServersCallback callback) {
  scoped_refptr<dbus::Bus> bus = connection_.Connect();
  if (!bus) {
    LOG(ERROR) << "Failed to connect to system bus through libbrillo.";
    std::move(callback).Run(false, {});
    return;
  }
  // Wait until the network proxy service is ready before sending requests to
  // it.
  dbus::ObjectProxy* network_proxy =
      bus->GetObjectProxy(chromeos::kNetworkProxyServiceName,
                          dbus::ObjectPath(chromeos::kNetworkProxyServicePath));
  network_proxy->WaitForServiceToBeAvailable(base::BindOnce(
      [](brillo::http::GetChromeProxyServersCallback callback,
         scoped_refptr<dbus::Bus> bus, const std::string& url,
         bool is_available) {
        if (!is_available) {
          LOG(WARNING) << "Network proxy service is not available.";
          std::move(callback).Run(false, {});
          return;
        }
        brillo::http::GetChromeProxyServersAsync(bus, url, std::move(callback));
      },
      std::move(callback), bus, url));
}

void RksCertificateFetcher::OnGetProxyServers(
    OnCertFetchedCallback on_cert_fetched,
    bool success,
    const std::vector<std::string>& servers) {
  if (success) {
    proxy_servers_ = servers;
  }

  manager_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(&RksCertificateFetcher::OnManagerServiceAvailable,
                     weak_factory_.GetWeakPtr(), on_cert_fetched));
}

void RksCertificateFetcher::OnManagerServiceAvailable(
    OnCertFetchedCallback on_cert_fetched, bool is_available) {
  if (!is_available) {
    LOG(ERROR) << "Shill manager service is not available.";
    return;
  }
  fetch_when_online_ = true;
  manager_proxy_->RegisterPropertyChangedSignalHandler(
      base::BindRepeating(&RksCertificateFetcher::OnManagerPropertyChange,
                          weak_factory_.GetWeakPtr(), on_cert_fetched),
      base::BindOnce(
          &RksCertificateFetcher::OnManagerPropertyChangeRegistration,
          weak_factory_.GetWeakPtr(), on_cert_fetched));
}

void RksCertificateFetcher::OnManagerPropertyChangeRegistration(
    OnCertFetchedCallback on_cert_fetched,
    const std::string& /*interface*/,
    const std::string& /*signal_name*/,
    bool success) {
  if (!success) {
    LOG(ERROR) << "Unable to register for shill manager change events, so "
                  "unable to fetch certificates.";
    return;
  }

  brillo::VariantDictionary properties;
  if (!manager_proxy_->GetProperties(&properties, nullptr)) {
    LOG(WARNING) << "Unable to get shill manager properties.";
    return;
  }

  auto it = properties.find(shill::kConnectionStateProperty);
  if (it == properties.end()) {
    return;
  }
  OnManagerPropertyChange(on_cert_fetched, shill::kConnectionStateProperty,
                          it->second);
}

void RksCertificateFetcher::OnManagerPropertyChange(
    OnCertFetchedCallback on_cert_fetched,
    const std::string& property_name,
    const brillo::Any& property_value) {
  // Only handle changes to the connection state.
  if (property_name != shill::kConnectionStateProperty) {
    return;
  }

  std::string connection_state;
  if (!property_value.GetValue(&connection_state)) {
    LOG(WARNING)
        << "Connection state fetched from shill manager is not a string.";
    return;
  }

  is_online_ = (connection_state == shill::kStateOnline);
  if (is_online_ && fetch_when_online_) {
    fetch_when_online_ = false;
    Fetch(on_cert_fetched);
  }
}

void RksCertificateFetcher::Fetch(OnCertFetchedCallback on_cert_fetched) {
  // If we aren't online when we want to fetch the certs, set
  // |fetch_when_online_| to true so that when the network is up, we can fetch
  // the certs immediately.
  if (!is_online_) {
    fetch_when_online_ = true;
    return;
  }

  brillo::http::Get(
      kCertXmlUrl, {}, GetTransport(),
      base::BindRepeating(&RksCertificateFetcher::OnFetchCertSuccess,
                          weak_factory_.GetWeakPtr(), on_cert_fetched),
      base::BindRepeating(&RksCertificateFetcher::OnFetchGetError,
                          weak_factory_.GetWeakPtr(), on_cert_fetched));
}

void RksCertificateFetcher::OnFetchCertSuccess(
    OnCertFetchedCallback on_cert_fetched,
    brillo::http::RequestID /*request_id*/,
    std::unique_ptr<brillo::http::Response> response) {
  std::optional<std::string> cert_xml =
      ExtractDataFromResponse(std::move(response));
  if (!cert_xml.has_value()) {
    LOG(ERROR) << "Failed to extract data from cert XML response.";
    OnFetchError(on_cert_fetched);
    return;
  }
  // Continue to fetch the signature xml.
  brillo::http::Get(
      kSignatureXmlUrl, {}, GetTransport(),
      base::BindRepeating(&RksCertificateFetcher::OnFetchSignatureSuccess,
                          weak_factory_.GetWeakPtr(), on_cert_fetched,
                          *cert_xml),
      base::BindRepeating(&RksCertificateFetcher::OnFetchGetError,
                          weak_factory_.GetWeakPtr(), on_cert_fetched));
}

void RksCertificateFetcher::OnFetchSignatureSuccess(
    OnCertFetchedCallback on_cert_fetched,
    const std::string& cert_xml,
    brillo::http::RequestID /*request_id*/,
    std::unique_ptr<brillo::http::Response> response) {
  std::optional<std::string> sig_xml =
      ExtractDataFromResponse(std::move(response));
  if (!sig_xml.has_value()) {
    LOG(ERROR) << "Failed to extract data from signature XML response.";
    OnFetchError(on_cert_fetched);
    return;
  }
  certificate_.set_certificate_xml(cert_xml);
  certificate_.set_signature_xml(std::move(*sig_xml));
  on_cert_fetched.Run(certificate_);

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&RksCertificateFetcher::Fetch, weak_factory_.GetWeakPtr(),
                     on_cert_fetched),
      kPeriodicFetchInterval);
}

void RksCertificateFetcher::OnFetchGetError(
    OnCertFetchedCallback on_cert_fetched,
    brillo::http::RequestID /*request_id*/,
    const brillo::Error* error) {
  LOG(ERROR) << "GET failed: " << error->GetMessage();
  OnFetchError(on_cert_fetched);
}

void RksCertificateFetcher::OnFetchError(
    OnCertFetchedCallback on_cert_fetched) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&RksCertificateFetcher::Fetch, weak_factory_.GetWeakPtr(),
                     on_cert_fetched),
      kFetchFailedRetryInterval);
}

}  // namespace pca_agent
}  // namespace attestation
