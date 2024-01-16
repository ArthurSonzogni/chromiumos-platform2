// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_PCA_AGENT_SERVER_RKS_CERT_FETCHER_H_
#define ATTESTATION_PCA_AGENT_SERVER_RKS_CERT_FETCHER_H_

#include "attestation/pca_agent/server/pca_agent_service.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <attestation/proto_bindings/interface.pb.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/http/http_transport.h>
#include <dbus/bus.h>
#include <shill/dbus-proxies.h>

#include "attestation/pca_agent/server/default_transport_factory.h"
#include "attestation/pca_agent/server/pca_http_utils.h"

namespace attestation {
namespace pca_agent {

class RksCertificateFetcher final : private DefaultTransportFactory,
                                    private PcaHttpUtils {
 public:
  explicit RksCertificateFetcher(
      std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
          manager_proxy);

  // Not copyable or movable.
  RksCertificateFetcher(const RksCertificateFetcher&) = delete;
  RksCertificateFetcher(RksCertificateFetcher&&) = delete;
  RksCertificateFetcher& operator=(const RksCertificateFetcher&) = delete;
  RksCertificateFetcher& operator=(RksCertificateFetcher&&) = delete;

  ~RksCertificateFetcher() = default;

  void StartFetching(
      base::RepeatingCallback<void(const RksCertificateAndSignature&)>
          on_cert_fetched);

  // Return the most recently fetched certificate. Empty certificate and
  // signature XMLs will be returned if no certificate files have been fetched
  // yet.
  const RksCertificateAndSignature& certificate() const { return certificate_; }

  void set_transport_factory_for_testing(TransportFactory* factory) {
    transport_factory_ = factory;
  }

  void set_pca_http_utils_for_testing(PcaHttpUtils* utils) {
    http_utils_ = utils;
  }

 private:
  using OnCertFetchedCallback =
      base::RepeatingCallback<void(const RksCertificateAndSignature&)>;

  std::shared_ptr<brillo::http::Transport> GetTransport();

  // |PcaRequestHttpUtils| overrides.
  void GetChromeProxyServersAsync(
      const std::string& url,
      brillo::http::GetChromeProxyServersCallback callback) override;

  // The callback of |GetChromeProxyServersAsync|; triggers
  // |SendRequestWithProxySetting| after storing the proxy servers into
  // |proxy_servers_|. In case of |!success|, inserts an identifier of direct
  // connection.
  void OnGetProxyServers(OnCertFetchedCallback on_cert_fetched,
                         bool success,
                         const std::vector<std::string>& servers);

  // The callback of |WaitForServiceToBeAvailable|. Starts to connect the
  // manager property change signals after the service is ready.
  void OnManagerServiceAvailable(OnCertFetchedCallback on_cert_fetched,
                                 bool is_available);

  // This is called when receiving the signal that we successfully registered
  // shill manager's property changes. It will check whether the connection
  // state property is already "online" after registration.
  void OnManagerPropertyChangeRegistration(
      OnCertFetchedCallback on_cert_fetched,
      const std::string& interface,
      const std::string& signal_name,
      bool success);

  // This is called whenever we receive a property change signal. It checks
  // whether it is a property change of the connection state. If connection
  // state is online and we're waiting to fetch the certificates, it will fetch
  // the certificates from the server endpoint url.
  void OnManagerPropertyChange(OnCertFetchedCallback on_cert_fetched,
                               const std::string& property_name,
                               const brillo::Any& property_value);

  // Fetch the certificates from the server by sending GET requests.
  void Fetch(OnCertFetchedCallback on_cert_fetched);

  // These are used as callback functions of the GET request. If successful,
  // |OnCertificateFetched| will eventually be called. Otherwise, |OnFetchError|
  // will be called.
  void OnFetchCertSuccess(OnCertFetchedCallback on_cert_fetched,
                          brillo::http::RequestID request_id,
                          std::unique_ptr<brillo::http::Response> response);
  void OnFetchSignatureSuccess(
      OnCertFetchedCallback on_cert_fetched,
      const std::string& cert_xml,
      brillo::http::RequestID request_id,
      std::unique_ptr<brillo::http::Response> response);
  void OnFetchGetError(OnCertFetchedCallback on_cert_fetched,
                       brillo::http::RequestID request_id,
                       const brillo::Error* error);

  // Schedules another fetch operation after some delay, as the current fetch
  // request failed.
  void OnFetchError(OnCertFetchedCallback on_cert_fetched);

  // A |TransportFactory| used to create |brillo::http::Transport| instance;
  // alternated during unittest for testability.
  TransportFactory* transport_factory_{this};

  // A |PcaRequestHttpUtils| used to perform HTTP related functions;
  // alternated during unittest for testability.
  PcaHttpUtils* http_utils_{this};

  // The list of proxy servers used to try to send the request with.
  std::vector<std::string> proxy_servers_;

  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
      manager_proxy_;

  // Keeps state of whether the network is connected. We should only send
  // requests when |is_online_| is true.
  bool is_online_ = false;
  // As when a fetch request is scheduled to run, the network might not be
  // connected, this keeps state of whether there is a pending fetch request
  // that should be scheduled as soon as the network is connected.
  bool fetch_when_online_ = false;

  // Used to retrieve proxy servers from Chrome.
  brillo::DBusConnection connection_;

  // Most recently fetched certificate files.
  RksCertificateAndSignature certificate_;

  base::WeakPtrFactory<RksCertificateFetcher> weak_factory_{this};
};

}  // namespace pca_agent
}  // namespace attestation

#endif  // ATTESTATION_PCA_AGENT_SERVER_RKS_CERT_FETCHER_H_
