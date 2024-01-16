// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_PCA_AGENT_SERVER_RKS_AGENT_SERVICE_H_
#define ATTESTATION_PCA_AGENT_SERVER_RKS_AGENT_SERVICE_H_

#include <memory>
#include <utility>

#include <attestation/proto_bindings/pca_agent.pb.h>
#include <base/memory/ref_counted.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/exported_object_manager.h>
#include <dbus/attestation/dbus-constants.h>

#include "attestation/pca-agent/dbus_adaptors/org.chromium.PcaAgent.h"
#include "attestation/pca_agent/server/rks_cert_fetcher.h"

namespace attestation {
namespace pca_agent {

// RksAgentService (which stands for Recoverable Key Store Service) provides
// functionalities related to the recoverable key store feature that need
// network access. Recoverable key store is a feature that syncs some data
// across different devices of the same Google user backed by their device local
// knowledge factors.
class RksAgentService : public org::chromium::RksAgentInterface,
                        public org::chromium::RksAgentAdaptor {
 public:
  explicit RksAgentService(scoped_refptr<dbus::Bus> bus);
  RksAgentService(const RksAgentService&) = delete;
  RksAgentService& operator=(const RksAgentService&) = delete;

  virtual ~RksAgentService() = default;

  // org::chromium::RksAgentInterface overrides.
  // Gets the most recently fetched certificate and signature XML pair.
  void GetCertificate(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                          RksCertificateAndSignature>> response) override;

 private:
  std::unique_ptr<RksCertificateFetcher> fetcher_;

  base::WeakPtrFactory<RksAgentService> weak_factory_{this};
};

}  // namespace pca_agent
}  // namespace attestation

#endif  // ATTESTATION_PCA_AGENT_SERVER_RKS_AGENT_SERVICE_H_
