// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_remote_provisioning_context.h"

#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <keymaster/cppcose/cppcose.h>
#include <libarc-attestation/lib/interface.h>

#include <algorithm>
#include <string>

#include <base/logging.h>
#include <openssl/rand.h>

/*
A lot of data structures in this file mimic the structures in
|ProtectedData.aidl| -
https://cs.android.com/android/platform/superproject/main/+/main:hardware/interfaces/security/rkp/aidl/android/hardware/security/keymint/ProtectedData.aidl.
*/
namespace arc::keymint::context {

constexpr uint32_t kP256AffinePointSize = 32;
constexpr uint32_t kP256SignatureLength = 64;
constexpr uint32_t kP256EcdsaPrivateKeyLength = 32;
const std::vector<uint8_t> kBccPayloadKeyUsage{0x20};
constexpr const char kProductBuildPropertyRootDir[] =
    "/usr/share/arcvm/properties/";
constexpr const char kProductBuildPropertyFileName[] = "product_build.prop";
constexpr char kProductBrand[] = "ro.product.product.brand";
constexpr char kProductDevice[] = "ro.product.product.device";
constexpr char kProductManufacturer[] = "ro.product.product.manufacturer";
constexpr char kProductModel[] = "ro.product.product.model";
constexpr char kProductName[] = "ro.product.product.name";

/*
This function creates BccEntryInput and then returns it after signing
by the key from CrOS DK cert.
*/
cppcose::ErrMsgOr<std::vector<uint8_t>> createCoseSign1SignatureFromDK(
    const std::vector<uint8_t>& protectedParams,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& additionalAuthData) {
  // |signatureInput| is the BccEntryInput structure for |ProtectedData.aidl|.
  std::vector<uint8_t> signatureInput = cppbor::Array()
                                            .add("Signature1")
                                            .add(protectedParams)
                                            .add(additionalAuthData)
                                            .add(payload)
                                            .encode();

  std::vector<uint8_t> ecdsaDERSignature(kP256SignatureLength);
  arc_attestation::AndroidStatus status =
      arc_attestation::SignWithP256Dk(signatureInput, ecdsaDERSignature);

  if (!status.is_ok()) {
    LOG(ERROR) << "Signing by libarc-attestation failed";
    int32_t error_code = status.get_error_code();
    std::string error = "Error Message = " + status.get_message() +
                        ", Error Code = " + std::to_string(error_code);
    return error;
  }

  // The signature returned from libarc-attestation is in DER format.
  // Convert it to COSE Format.
  cppcose::ErrMsgOr<std::vector<uint8_t>> p256DkSignature =
      cppcose::ecdsaDerSignatureToCose(ecdsaDERSignature);

  if (!p256DkSignature) {
    auto errorMessage = p256DkSignature.moveMessage();
    LOG(ERROR) << "Error extracting Cose Signature from Chrome OS ECDSA Der "
                  "Signature: "
               << errorMessage;
    return errorMessage;
  }
  return p256DkSignature;
}

/*
This function returns BccEntry as in |ProtectedData.aidl|
*/
cppcose::ErrMsgOr<cppbor::Array> constructCoseSign1FromDK(
    cppbor::Map protectedParamsMap,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& additionalAuthData) {
  std::vector<uint8_t> protectedParams =
      protectedParamsMap.add(cppcose::ALGORITHM, cppcose::ES256)
          .canonicalize()
          .encode();

  // |signature| represents BccEntryInput from |ProtectedtData.aidl|.
  auto signature = createCoseSign1SignatureFromDK(protectedParams, payload,
                                                  additionalAuthData);
  if (!signature) {
    return signature.moveMessage();
  }

  // Unprotected Parameters.
  auto unprotectedParams = cppbor::Map();

  // Returns the Bcc Entry.
  return cppbor::Array()
      .add(std::move(protectedParams))
      .add(std::move(unprotectedParams))
      .add(std::move(payload))
      .add(signature.moveValue());
}

std::unique_ptr<cppbor::Map> CreateDeviceInfoMap(
    std::string& properties_content) {
  auto result = std::make_unique<cppbor::Map>(cppbor::Map());
  std::vector<std::string> properties = base::SplitString(
      properties_content, "\n", base::WhitespaceHandling::TRIM_WHITESPACE,
      base::SplitResult::SPLIT_WANT_ALL);

  std::map<std::string, std::pair<std::string, std::string>> property_map = {
      {kProductBrand, std::make_pair("brand", "")},
      {kProductDevice, std::make_pair("device", "")},
      {kProductManufacturer, std::make_pair("manufacturer", "")},
      {kProductModel, std::make_pair("model", "")},
      {kProductName, std::make_pair("product", "")}};

  constexpr char separator[] = "=";

  // If the property exists in input properties, add its value in property map.
  for (const auto& property : properties) {
    auto separatorIndex = property.find(separator);
    auto key = property.substr(0, separatorIndex);

    auto itr = property_map.find(key);
    if (itr != property_map.end()) {
      auto value = property.substr(separatorIndex + 1, property.size());
      itr->second.second = value;
    }
  }

  // Convert property map into cppbor map.
  for (auto& [key, value] : property_map) {
    if (value.second.size()) {
      result->add(cppbor::Tstr(value.first), cppbor::Tstr(value.second));
    }
  }
  return result;
}

namespace {

std::vector<uint8_t> GetRandomVector() {
  std::vector<uint8_t> seed_vector(32, 0);
  // This is used in code paths that cannot fail, so CHECK. If it turns
  // out that we can actually run out of entropy during these code paths,
  // we'll need to refactor the interfaces to allow errors to propagate.
  CHECK_EQ(RAND_bytes(seed_vector.data(), seed_vector.size()), 1)
      << "Unable to get random vector";
  return seed_vector;
}

std::optional<std::vector<uint8_t>> provisionAndFetchDkCert() {
  // Provision certificate.
  arc_attestation::AndroidStatus provision_status =
      arc_attestation::ProvisionDkCert(true /*blocking*/);
  if (!provision_status.is_ok()) {
    LOG(ERROR) << "Error in Provisioning Dk Cert from libarc-attestation";
    return std::nullopt;
  }

  // Extract DK Cert Chain from libarc-attestation.
  std::vector<std::vector<uint8_t>> cert_chain;
  arc_attestation::AndroidStatus cert_status =
      arc_attestation::GetDkCertChain(cert_chain);
  if (!cert_status.is_ok()) {
    LOG(ERROR) << "Error in fetching DK Cert Chain from libarc-attestation";
    return std::nullopt;
  }

  if (cert_chain.size() == 0) {
    LOG(ERROR) << "DK Cert Chain from libarc-attestation is empty";
    return std::nullopt;
  }
  // First element of cert chain carries UDS Pub.
  return cert_chain.front();
}

// Generates Boot Certificate Chain for Test mode.
// |private_key_vector| is passed as a parameter, which is filled with the
// actual private key from this function.
cppcose::ErrMsgOr<cppbor::Array> GenerateBccForTestMode(
    bool test_mode, std::vector<uint8_t>& private_key_vector) {
  if (!test_mode) {
    auto error_message = "Not Allowed to generate Test BCC in Production Mode";
    LOG(ERROR) << error_message;
    return error_message;
  }

  std::vector<uint8_t> x_vect(kP256AffinePointSize);
  std::vector<uint8_t> y_vect(kP256AffinePointSize);
  absl::Span<uint8_t> x_coord(x_vect);
  absl::Span<uint8_t> y_coord(y_vect);
  absl::Span<uint8_t> private_key(private_key_vector);

  // Get ECDSA key from Seed in Test Mode.
  std::vector<uint8_t> seed_vector = GetRandomVector();
  absl::Span<uint8_t> seed(seed_vector);
  std::string private_key_pem;
  auto error = GenerateEcdsa256KeyFromSeed(test_mode, seed, private_key,
                                           private_key_pem, x_coord, y_coord);
  if (error != KM_ERROR_OK) {
    auto error_message = "Failed to get ECDSA key from seed in test mode";
    LOG(ERROR) << error_message;
    return error_message;
  }

  auto coseKey =
      cppbor::Map()
          .add(cppcose::CoseKey::KEY_TYPE, cppcose::EC2)
          .add(cppcose::CoseKey::ALGORITHM, cppcose::ES256)
          .add(cppcose::CoseKey::CURVE, cppcose::P256)
          .add(cppcose::CoseKey::KEY_OPS, cppbor::Array().add(cppcose::VERIFY))
          .add(cppcose::CoseKey::PUBKEY_X, x_vect)
          .add(cppcose::CoseKey::PUBKEY_Y, y_vect)
          .canonicalize();

  // This map is based on the Protected Data AIDL, which is further based on
  // the Open Profile for DICE.
  // |sign1Payload| represents BccPayload data structure from
  // |ProtectedData.aidl|. Fields - Issuer and Subject are redundant for a
  // degenerate Bcc chain like here.
  auto sign1Payload =
      cppbor::Map()
          .add(BccPayloadLabel::ISSUER, "Issuer")
          .add(BccPayloadLabel::SUBJECT, "Subject")
          .add(BccPayloadLabel::SUBJECT_PUBLIC_KEY, coseKey.encode())
          .add(BccPayloadLabel::KEY_USAGE, kBccPayloadKeyUsage)
          .canonicalize()
          .encode();
  std::vector<uint8_t> additional_authenticated_data;

  cppcose::ErrMsgOr<cppbor::Array> coseSign1("");
  coseSign1 = cppcose::constructECDSACoseSign1(private_key_vector,
                                               cppbor::Map(), sign1Payload,
                                               additional_authenticated_data);
  if (!coseSign1) {
    auto error_message = coseSign1.moveMessage();
    LOG(ERROR) << "Bcc Generation failed in test mode: " << error_message;
    return error_message;
  }
  auto cbor_array =
      cppbor::Array().add(std::move(coseKey)).add(coseSign1.moveValue());
  return cbor_array;
}

// This function generates Boot Chain Certificate for Production mode.
// Final signature is signed by libarc-attestation.
cppcose::ErrMsgOr<cppbor::Array> GenerateBccForProductionMode() {
  std::vector<uint8_t> x_vect(kP256AffinePointSize);
  std::vector<uint8_t> y_vect(kP256AffinePointSize);
  absl::Span<uint8_t> x_coord(x_vect);
  absl::Span<uint8_t> y_coord(y_vect);

  std::optional<std::vector<uint8_t>> uds_pub = provisionAndFetchDkCert();
  if (!uds_pub.has_value()) {
    auto error_message =
        "Failed to get a valid device cert from libarc-attestation";
    return error_message;
  }

  // Extract Affine coordinates from libarc-attestation certificate.
  // Get ECDSA Key from Device Cert in Production Mode.
  auto error = GetEcdsa256KeyFromCertBlob(uds_pub.value(), x_coord, y_coord);
  if (error != KM_ERROR_OK) {
    auto error_message =
        "Failed to extract Affine coordinates from ChromeOS cert";
    LOG(ERROR) << error_message;
    return error_message;
  }

  // Construct Cose Key.
  auto coseKey =
      cppbor::Map()
          .add(cppcose::CoseKey::KEY_TYPE, cppcose::EC2)
          .add(cppcose::CoseKey::ALGORITHM, cppcose::ES256)
          .add(cppcose::CoseKey::CURVE, cppcose::P256)
          .add(cppcose::CoseKey::KEY_OPS, cppbor::Array().add(cppcose::VERIFY))
          .add(cppcose::CoseKey::PUBKEY_X, x_vect)
          .add(cppcose::CoseKey::PUBKEY_Y, y_vect)
          .canonicalize();

  // This map is based on the Protected Data AIDL, which is further based on
  // the Open Profile for DICE.
  // |sign1Payload| represents BccPayload data structure from
  // |ProtectedData.aidl|. Fields - Issuer and Subject are redundant for a
  // degenerate Bcc chain like here.
  auto sign1Payload =
      cppbor::Map()
          .add(BccPayloadLabel::ISSUER, "Issuer")
          .add(BccPayloadLabel::SUBJECT, "Subject")
          .add(BccPayloadLabel::SUBJECT_PUBLIC_KEY, coseKey.encode())
          .add(BccPayloadLabel::KEY_USAGE, kBccPayloadKeyUsage)
          .canonicalize()
          .encode();
  std::vector<uint8_t> additional_authenticated_data;

  // |coseSign1| represents the Bcc entry.
  auto coseSign1 = constructCoseSign1FromDK(cppbor::Map(), sign1Payload,
                                            additional_authenticated_data);
  if (!coseSign1) {
    auto error_message = coseSign1.moveMessage();
    LOG(ERROR) << "Bcc Generation failed in Production Mode: " << error_message;
    return error_message;
  }
  auto cbor_array =
      cppbor::Array().add(std::move(coseKey)).add(coseSign1.moveValue());
  return cbor_array;
}

}  // namespace

ArcRemoteProvisioningContext::ArcRemoteProvisioningContext(
    keymaster_security_level_t security_level)
    : PureSoftRemoteProvisioningContext(security_level),
      security_level_(security_level),
      property_dir_(kProductBuildPropertyRootDir) {}

ArcRemoteProvisioningContext::~ArcRemoteProvisioningContext() = default;

std::optional<std::pair<std::vector<uint8_t>, cppbor::Array>>
ArcRemoteProvisioningContext::GenerateBcc(bool test_mode) const {
  std::vector<uint8_t> private_key_vector(kP256EcdsaPrivateKeyLength);

  cppcose::ErrMsgOr<cppbor::Array> cbor_array("");
  if (test_mode) {
    // Test Mode.
    cbor_array = GenerateBccForTestMode(test_mode, private_key_vector);
  } else {
    // Production Mode.
    cbor_array = GenerateBccForProductionMode();
  }

  if (!cbor_array) {
    auto error_message = cbor_array.moveMessage();
    LOG(ERROR) << "Bcc Generation failed: " << error_message;
    return std::nullopt;
  }

  // Boot Certificate Chain.
  return std::make_pair(std::move(private_key_vector), cbor_array.moveValue());
}

cppcose::ErrMsgOr<std::vector<uint8_t>>
ArcRemoteProvisioningContext::BuildProtectedDataPayload(
    bool test_mode,
    const std::vector<uint8_t>& mac_key,
    const std::vector<uint8_t>& additional_auth_data) const {
  cppbor::Array boot_cert_chain;
  cppcose::ErrMsgOr<cppbor::Array> signed_mac("");
  if (test_mode) {
    // In Test mode, signature is constructed by signing with the
    // seed generated Ecdsa key.
    auto bcc = GenerateBcc(/*test_mode*/ true);
    std::vector<uint8_t> signing_key_test_mode;
    if (bcc.has_value()) {
      // Extract signing key and boot cert chain from the pair
      // returned by GenerateBcc function.
      signing_key_test_mode = std::move(bcc.value().first);
      boot_cert_chain = std::move(bcc.value().second);
      signed_mac = cppcose::constructECDSACoseSign1(
          signing_key_test_mode, cppbor::Map(), mac_key, additional_auth_data);
    }
  } else {
    // In Production mode, libarc-attestation does the signing.
    ArcLazyInitProdBcc();
    auto clone = boot_cert_chain_.clone();
    if (!clone->asArray()) {
      auto error_message = "The Boot Cert Chain is not an array";
      LOG(ERROR) << error_message;
      return error_message;
    }
    boot_cert_chain = std::move(*clone->asArray());
    signed_mac = constructCoseSign1FromDK(/*Protected Params*/ {}, mac_key,
                                          additional_auth_data);
  }

  if (!signed_mac) {
    auto error_message = signed_mac.moveMessage();
    LOG(ERROR) << "Signing while building Protected Data Payload failed: "
               << error_message;
    return error_message;
  }

  return cppbor::Array()
      .add(signed_mac.moveValue())
      .add(std::move(boot_cert_chain))
      .encode();
}

void ArcRemoteProvisioningContext::ArcLazyInitProdBcc() const {
  std::call_once(bcc_initialized_flag_, [this]() {
    auto bcc = GenerateBcc(/*test_mode=*/false);
    if (bcc.has_value()) {
      // Extract boot cert chain from the pair returned by GenerateBcc.
      // In Production mode, the first element of the pair - |private key|
      // is not used.
      boot_cert_chain_ = std::move(bcc.value().second);
    }
  });
}

void ArcRemoteProvisioningContext::set_property_dir_for_tests(
    base::FilePath& path) {
  property_dir_ = base::FilePath(path);
}

void ArcRemoteProvisioningContext::SetSystemVersion(uint32_t os_version,
                                                    uint32_t os_patchlevel) {
  os_version_ = os_version;
  os_patchlevel_ = os_patchlevel;
}

std::unique_ptr<cppbor::Map> ArcRemoteProvisioningContext::CreateDeviceInfo()
    const {
  const base::FilePath prop_file_path =
      property_dir_.Append(kProductBuildPropertyFileName);

  std::string properties_content;
  if (!base::ReadFileToString(prop_file_path, &properties_content)) {
    // In case of failure to read properties into string, return a blank map.
    LOG(ERROR) << "Failed to read properties from the properties file";
    return std::make_unique<cppbor::Map>(cppbor::Map());
  }

  auto device_info_map = CreateDeviceInfoMap(properties_content);

  if (os_version_.has_value()) {
    device_info_map->add(cppbor::Tstr("os_version"),
                         cppbor::Tstr(std::to_string(os_version_.value())));
  }

  if (os_patchlevel_.has_value()) {
    device_info_map->add(cppbor::Tstr("system_patch_level"),
                         cppbor::Uint(os_patchlevel_.value()));
  }

  const char* security_level = "tee";
  if (security_level_ == KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT) {
    device_info_map->add(cppbor::Tstr("security_level"),
                         cppbor::Tstr(security_level));
  }

  device_info_map->canonicalize();
  return device_info_map;
}
}  // namespace arc::keymint::context
