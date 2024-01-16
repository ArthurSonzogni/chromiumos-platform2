// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/pca_agent/server/rks_agent_service.h"

#include <string>

#include <attestation/proto_bindings/interface.pb.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/shill/dbus-constants.h>
#include <shill/dbus-proxies.h>

#include "attestation/pca-agent/dbus_adaptors/org.chromium.PcaAgent.h"

namespace attestation {
namespace pca_agent {

RksAgentService::RksAgentService(scoped_refptr<dbus::Bus> bus)
    : org::chromium::RksAgentAdaptor(this) {
  fetcher_ = std::make_unique<RksCertificateFetcher>(
      std::make_unique<org::chromium::flimflam::ManagerProxy>(bus));
  fetcher_->StartFetching(
      base::BindRepeating(&RksAgentService::SendCertificateFetchedSignal,
                          weak_factory_.GetWeakPtr()));
}

void RksAgentService::GetCertificate(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<RksCertificateAndSignature>>
        response) {
  response->Return(fetcher_->certificate());
}

}  // namespace pca_agent
}  // namespace attestation
