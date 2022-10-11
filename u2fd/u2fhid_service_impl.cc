// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2fhid_service_impl.h"

#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <attestation-client/attestation/dbus-constants.h>
#include <attestation/proto_bindings/interface.pb.h>
#include <base/logging.h>
#include <metrics/metrics_library.h>
#include <session_manager/dbus-proxies.h>
#include <trunks/cr50_headers/virtual_nvmem.h>

#include "u2fd/client/tpm_vendor_cmd.h"
#include "u2fd/client/u2f_corp_firmware_version.h"
#include "u2fd/client/user_state.h"
#include "u2fd/u2f_corp_processor_interface.h"

namespace u2f {

namespace {

constexpr char kDeviceName[] = "Integrated U2F";
constexpr char kKeyLabelEmk[] = "attest-ent-machine";

constexpr uint32_t kDefaultVendorId = 0x18d1;
constexpr uint32_t kDefaultProductId = 0x502c;
constexpr uint32_t kCorpVendorId = 0x18d1;
constexpr uint32_t kCorpProductId = 0x5212;

}  // namespace

U2fHidServiceImpl::U2fHidServiceImpl(bool legacy_kh_fallback)
    : legacy_kh_fallback_(legacy_kh_fallback) {}

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
    bool enable_corp_protocol,
    std::function<void()> request_user_presence,
    UserState* user_state,
    org::chromium::SessionManagerInterfaceProxy* sm_proxy,
    MetricsLibraryInterface* metrics) {
  U2fCorpFirmwareVersion fw_version;
  std::string dev_id(8, '\0');

  if (enable_corp_protocol) {
    TpmRwVersion rw_version;
    uint32_t status = tpm_proxy_.GetRwVersion(&rw_version);
    if (status != 0) {
      LOG(ERROR) << "GetRwVersion failed with status " << std::hex << status
                 << ".";
    } else {
      fw_version = U2fCorpFirmwareVersion::FromTpmRwVersion(rw_version);

      u2f_corp_processor_ = std::make_unique<U2fCorpProcessorInterface>();
      u2f_corp_processor_->Initialize(fw_version, sm_proxy, &tpm_proxy_,
                                      metrics, request_user_presence);
    }

    std::string cert;
    status = tpm_proxy_.GetG2fCertificate(&cert);
    if (status != 0) {
      LOG(ERROR) << "GetG2fCertificate failed with status " << std::hex
                 << status << ".";
    } else {
      std::optional<brillo::Blob> sn =
          util::ParseSerialNumberFromCert(brillo::BlobFromString(cert));
      if (!sn.has_value()) {
        LOG(ERROR) << "Failed to parse serial number from g2f cert.";
      } else {
        dev_id = brillo::BlobToString(util::Sha256(*sn));
      }
    }
  }

  uint32_t vendor_id = enable_corp_protocol ? kCorpVendorId : kDefaultVendorId;
  uint32_t product_id =
      enable_corp_protocol ? kCorpProductId : kDefaultProductId;

  std::unique_ptr<u2f::AllowlistingUtil> allowlisting_util;

  if (include_g2f_allowlisting_data) {
    allowlisting_util = std::make_unique<u2f::AllowlistingUtil>(
        [this](int cert_size) { return GetCertifiedG2fCert(cert_size); });
  }

  u2f_msg_handler_ = std::make_unique<u2f::U2fMessageHandler>(
      std::move(allowlisting_util), request_user_presence, user_state,
      &tpm_proxy_, sm_proxy, metrics, legacy_kh_fallback_,
      allow_g2f_attestation, u2f_corp_processor_.get());

  u2fhid_ = std::make_unique<u2f::U2fHid>(
      std::make_unique<u2f::UHidDevice>(vendor_id, product_id, kDeviceName,
                                        "u2fd-tpm-cr50"),
      fw_version, dev_id, u2f_msg_handler_.get(), u2f_corp_processor_.get());

  return u2fhid_->Init();
}

std::optional<attestation::GetCertifiedNvIndexReply>
U2fHidServiceImpl::GetCertifiedG2fCert(int g2f_cert_size) {
  if (g2f_cert_size < 1 || g2f_cert_size > VIRTUAL_NV_INDEX_G2F_CERT_SIZE) {
    LOG(ERROR)
        << "Invalid G2F cert size specified for whitelisting data request";
    return std::nullopt;
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
    return std::nullopt;
  }

  attestation::GetCertifiedNvIndexReply reply;

  dbus::MessageReader reader(dbus_response.get());
  if (!reader.PopArrayOfBytesAsProto(&reply)) {
    LOG(ERROR) << "Failed to parse GetCertifiedNvIndexReply";
    return std::nullopt;
  }

  if (reply.status() != attestation::AttestationStatus::STATUS_SUCCESS) {
    LOG(ERROR) << "Call get GetCertifiedNvIndex failed, status: "
               << reply.status();
    return std::nullopt;
  }

  return reply;
}

}  // namespace u2f
