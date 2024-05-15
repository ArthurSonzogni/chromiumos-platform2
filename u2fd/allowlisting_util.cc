// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/allowlisting_util.h"

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <attestation/proto_bindings/interface.pb.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <libhwsec/structures/u2f.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

#include "u2fd/client/util.h"

namespace u2f {

namespace {

using ::hwsec::u2f::FipsCertificationStatus;

// Tags for the ASN1 types we are going to append.
constexpr uint8_t kSequence = 0x30;
constexpr uint8_t kInteger = 0x02;
constexpr uint8_t kOctetString = 0x04;
constexpr uint8_t kPrintableString = 0x13;

// The certificate is hardcoded in the cr50 firmware; we can simplify the logic
// needed to modify it by making some assumptions.
constexpr uint8_t kCertExpectedFirstByte =
    kSequence;  // Root node is a sequence.
// Sequence length field is 2 bytes long.
constexpr uint8_t kCertExpectedSecondByte = 0x82;
// The two bytes above, plus the length bytes.
constexpr int kCertRootSeqPrefixLength = 4;

// This is the data signed by the TPM as part of the NV_Certify response; it is
// fixed length, defined by the spec, and not expected to change.
constexpr int kExpectedTpmMetadataLength = 109;

std::vector<uint8_t> EncodeLength(uint16_t length) {
  if (length < 128) {
    // Short form.
    return {static_cast<uint8_t>(length)};
  }

  // Values above 127 need to use the long form; this is made of 1 byte that
  // describes the length field itself, followed by 1 or more bytes that contain
  // the value. The first byte has the top bit set to indicate long form. The
  // remaining 7 bits indicate the number of bytes in the length field. We only
  // support uint16_t length values here, so we need at most two bytes to
  // represent the length.
  constexpr uint8_t kLongFormLengthOneByte = 0x81;
  constexpr uint8_t kLongFormLengthTwoBytes = 0x82;

  if (length < 256) {
    return {kLongFormLengthOneByte, static_cast<uint8_t>(length)};
  }

  uint8_t high = length >> 8;
  uint8_t low = static_cast<uint8_t>(length);
  return {kLongFormLengthTwoBytes, high, low};
}

// Appends a string field of the specified type to |cert|; returns true on
// success.
template <typename C>
bool AppendString(uint8_t string_type,
                  const C& str,
                  std::vector<uint8_t>* cert) {
  if (str.size() > std::numeric_limits<uint16_t>::max()) {
    LOG(ERROR) << "Attempting to append unexpectedly long ASN1 string";
    return false;
  }

  cert->push_back(string_type);
  util::AppendToVector(EncodeLength(str.size()), cert);
  util::AppendToVector(str, cert);
  return true;
}

// We only need to append positive integers less than 128, so we can use the
// 1-byte integer form here to simplify implementation.
bool AppendShortInteger(int num, std::vector<uint8_t>* cert) {
  if (num < 0 || num >= 128) {
    return false;
  }
  // The format will be "02 01 num".
  cert->push_back(kInteger);
  cert->push_back(1);
  cert->push_back(static_cast<uint8_t>(num));
  return true;
}

}  // namespace

AllowlistingUtil::AllowlistingUtil(
    std::function<std::optional<attestation::GetCertifiedNvIndexReply>(int)>
        get_certified_g2f_cert,
    hwsec::u2f::FipsInfo fips_info)
    : get_certified_g2f_cert_(get_certified_g2f_cert),
      fips_info_(std::move(fips_info)),
      policy_provider_(std::make_unique<policy::PolicyProvider>()) {}

//
// The attestation certificate is an X509 certificate, which uses ASN1 encoding.
// The top-level layout of the certificate is shown below.
//
// SEQUENCE (3 elem)
//   SEQUENCE (8 elem)
//     <certificate body>
//   SEQUENCE
//     <signature format>
//   BIT STRING
//     <signature>
//
// To preserve a valid ASN1 structure, we will append fields to the end of the
// root sequence, so that the final structure is as shown below.
//
// SEQUENCE (7 elem)
//   SEQUENCE (8 elem)
//     <certificate body...>
//   SEQUENCE (1 elem)
//     <signature format>
//   BIT STRING
//     <signature>
//   OCTET STRING
//     <certificate prefix>
//   OCTET STRING
//     <certificate signature>
//   PRINTABLE STRING
//     <device id>
//   SEQUENCE (2 elem)
//     INTEGER
//       <FIPS physical certification status>
//     INTEGER
//       <FIPS logical certification status>
//

bool AllowlistingUtil::AppendDataToCert(std::vector<uint8_t>* cert) {
  if (cert == nullptr) {
    LOG(ERROR) << "Attempting to append to null certificate";
    return false;
  }

  int orig_cert_size = cert->size();

  // Sanity check.
  if (orig_cert_size < kCertRootSeqPrefixLength ||
      (*cert)[0] != kCertExpectedFirstByte ||
      (*cert)[1] != kCertExpectedSecondByte) {
    LOG(ERROR) << "Unexpected attestation certificate, cannot append data";
    return false;
  }

  // Collect all the data we need to append.
  std::vector<uint8_t> cert_prefix;
  std::vector<uint8_t> signature;
  std::optional<std::string> device_id = GetDeviceId();
  if (!device_id.has_value() ||
      !GetCertifiedAttestationCert(orig_cert_size, &cert_prefix, &signature)) {
    return false;
  }

  // By default, treat FIPS status as not certified. Only fill in values when
  // we're certain the implementation is certified.
  hwsec::u2f::FipsCertificationLevel level{
      .physical_certification_status = FipsCertificationStatus::kNotCertified,
      .logical_certification_status = FipsCertificationStatus::kNotCertified};
  if (fips_info_.activation_status == hwsec::u2f::FipsStatus::kActive &&
      fips_info_.certification_level.has_value()) {
    level = *fips_info_.certification_level;
  }

  std::vector<uint8_t> fips_status_seq_body;
  if (!AppendShortInteger(static_cast<int>(level.physical_certification_status),
                          &fips_status_seq_body) ||
      !AppendShortInteger(static_cast<int>(level.logical_certification_status),
                          &fips_status_seq_body)) {
    cert->resize(orig_cert_size);
    return false;
  }
  std::vector<uint8_t> fips_status{kSequence};
  util::AppendToVector(EncodeLength(fips_status_seq_body.size()), &fips_status);
  util::AppendToVector(fips_status_seq_body, &fips_status);

  // Actually append the data.
  if (!AppendString(kOctetString, cert_prefix, cert) ||
      !AppendString(kOctetString, signature, cert) ||
      !AppendString(kPrintableString, *device_id, cert)) {
    // Restore cert to it's original state.
    cert->resize(orig_cert_size);
    return false;
  }
  util::AppendToVector(fips_status, cert);

  // Update length of the root sequence.
  int seq_size = cert->size() - kCertRootSeqPrefixLength;

  // Sanity check.
  if (seq_size > std::numeric_limits<uint16_t>::max()) {
    LOG(ERROR) << "Updated sequence too long";
    // Restore cert to it's original state.
    cert->resize(orig_cert_size);
    return false;
  }

  std::vector<uint8_t> seq_length = EncodeLength(seq_size);
  // The certificate from cr50 is always >256 bytes long (and we've appended
  // more data), so we're always in 2 byte long from.
  DCHECK_EQ(kCertExpectedSecondByte, seq_length[0]);
  (*cert)[2] = seq_length[1];
  (*cert)[3] = seq_length[2];

  return true;
}

bool AllowlistingUtil::GetCertifiedAttestationCert(
    int orig_cert_size,
    std::vector<uint8_t>* cert_prefix,
    std::vector<uint8_t>* signature) {
  std::optional<attestation::GetCertifiedNvIndexReply> reply =
      get_certified_g2f_cert_(orig_cert_size);

  if (!reply.has_value() || reply->status() != attestation::STATUS_SUCCESS) {
    LOG(ERROR) << "Couldn't get certified attestation cert";
    return false;
  }

  if (reply->certified_data().size() < orig_cert_size) {
    LOG(ERROR) << "Received certified attestation data with incorrect size";
    return false;
  }

  // The 'certified' copy of the attestation certificate includes a prefix with
  // some TPM metadata. The blob as a whole is what is signed by the TPM, so
  // although we do not need or verify the contents of the prefix, we must
  // provide it so that the signature can later be verified. The certified data
  // is the metadata prefix immediately followed by the attestation certificate,
  // with no suffix.
  int cert_prefix_length = reply->certified_data().size() - orig_cert_size;

  // If this fails, cr50 and/or attestationd are not behaving as expected.
  if (kExpectedTpmMetadataLength != cert_prefix_length) {
    LOG(ERROR) << "Unexpected TPM metadata length";
    return false;
  }

  util::AppendToVector(reply->certified_data().substr(0, cert_prefix_length),
                       cert_prefix);
  util::AppendToVector(reply->signature(), signature);

  return true;
}

std::optional<std::string> AllowlistingUtil::GetDeviceId() {
  if (!policy_provider_->Reload()) {
    LOG(ERROR) << "Failed to load device policy";
    return std::nullopt;
  }

  return policy_provider_->GetDevicePolicy().GetDeviceDirectoryApiId();
}

void AllowlistingUtil::SetPolicyProviderForTest(
    std::unique_ptr<policy::PolicyProvider> provider) {
  policy_provider_ = std::move(provider);
}

}  // namespace u2f
