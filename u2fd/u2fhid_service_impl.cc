// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2fhid_service_impl.h"

#include <functional>
#include <memory>
#include <utility>

#include <attestation-client/attestation/dbus-constants.h>
#include <attestation/proto_bindings/interface.pb.h>
#include <base/logging.h>
#include <base/optional.h>
#include <metrics/metrics_library.h>
#include <trunks/cr50_headers/virtual_nvmem.h>

#include "u2fd/user_state.h"

namespace u2f {

namespace {

constexpr char kDeviceName[] = "Integrated U2F";
constexpr char kKeyLabelEmk[] = "attest-ent-machine";

}  // namespace

U2fHidServiceImpl::U2fHidServiceImpl(bool legacy_kh_fallback,
                                     uint32_t vendor_id,
                                     uint32_t product_id)
    : legacy_kh_fallback_(legacy_kh_fallback),
      vendor_id_(vendor_id),
      product_id_(product_id) {}

bool U2fHidServiceImpl::InitializeDBusProxies(dbus::Bus* bus) {
  if (!tpm_proxy_.Init()) {
    LOG(ERROR) << "Failed to initialize trunksd DBus proxy";
    return false;
  }

  attestation_proxy_ = bus->GetObjectProxy(
      attestation::kAttestationServiceName,
      dbus::ObjectPath(attestation::kAttestationServicePath));

  if (!attestation_proxy_) {
    LOG(ERROR) << "Failed to initialize attestationd DBus proxy";
    return false;
  }

  return true;
}

bool U2fHidServiceImpl::CreateU2fHid(
    bool allow_g2f_attestation,
    bool include_g2f_allowlisting_data,
    std::function<void()> request_user_presence,
    UserState* user_state,
    MetricsLibraryInterface* metrics) {
  std::unique_ptr<u2f::AllowlistingUtil> allowlisting_util;

  if (include_g2f_allowlisting_data) {
    allowlisting_util = std::make_unique<u2f::AllowlistingUtil>(
        [this](int cert_size) { return GetCertifiedG2fCert(cert_size); });
  }

  u2f_msg_handler_ = std::make_unique<u2f::U2fMessageHandler>(
      std::move(allowlisting_util), request_user_presence, user_state,
      &tpm_proxy_, metrics, legacy_kh_fallback_, allow_g2f_attestation);

  u2fhid_ = std::make_unique<u2f::U2fHid>(
      std::make_unique<u2f::UHidDevice>(vendor_id_, product_id_, kDeviceName,
                                        "u2fd-tpm-cr50"),
      u2f_msg_handler_.get());

  return u2fhid_->Init();
}

base::Optional<attestation::GetCertifiedNvIndexReply>
U2fHidServiceImpl::GetCertifiedG2fCert(int g2f_cert_size) {
  if (g2f_cert_size < 1 || g2f_cert_size > VIRTUAL_NV_INDEX_G2F_CERT_SIZE) {
    LOG(ERROR)
        << "Invalid G2F cert size specified for whitelisting data request";
    return base::nullopt;
  }

  attestation::GetCertifiedNvIndexRequest request;

  request.set_nv_index(VIRTUAL_NV_INDEX_G2F_CERT);
  request.set_nv_size(g2f_cert_size);
  request.set_key_label(kKeyLabelEmk);

  brillo::ErrorPtr error;

  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallMethodAndBlock(
          attestation_proxy_, attestation::kAttestationInterface,
          attestation::kGetCertifiedNvIndex, &error, request);

  if (!dbus_response) {
    LOG(ERROR) << "Failed to retrieve certified G2F cert from attestationd";
    return base::nullopt;
  }

  attestation::GetCertifiedNvIndexReply reply;

  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&reply)) {
    LOG(ERROR) << "Failed to parse GetCertifiedNvIndexReply";
    return base::nullopt;
  }

  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    LOG(ERROR) << "Call get GetCertifiedNvIndex failed, status: "
               << reply.status();
    return base::nullopt;
  }

  return reply;
}

}  // namespace u2f
