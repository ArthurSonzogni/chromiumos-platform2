// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/client/tpm_vendor_cmd.h"

#include <array>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/sys_byteorder.h>
#include <trunks/trunks_dbus_proxy.h>

namespace {

// All TPM extension commands use this struct for input and output. Any other
// data follows immediately after. All values are big-endian over the wire.
struct TpmCmdHeader {
  uint16_t tag;              // TPM_ST_NO_SESSIONS
  uint32_t size;             // including this header
  uint32_t code;             // Command out, Response back.
  uint16_t subcommand_code;  // Additional command/response codes
} __attribute__((packed));

// From src/platform/cr50/chip/g/upgrade_fw.h.
struct signed_header_version {
  uint32_t minor;
  uint32_t major;
  uint32_t epoch;
};

struct first_response_pdu {
  uint32_t return_value;
  uint32_t protocol_version;
  uint32_t backup_ro_offset;
  uint32_t backup_rw_offset;
  struct signed_header_version shv[2];
  uint32_t keyid[2];
};

struct sysinfo_s {
  uint32_t ro_keyid;
  uint32_t rw_keyid;
  uint32_t dev_id0;
  uint32_t dev_id1;
} __attribute__((packed));

// TPMv2 Spec mandates that vendor-specific command codes have bit 29 set,
// while bits 15-0 indicate the command. All other bits should be zero. We
// define one of those 16-bit command values for Cr50 purposes, and use the
// subcommand_code in struct TpmCmdHeader to further distinguish the desired
// operation.
const uint32_t kTpmCcVendorBit = 0x20000000;

// Vendor-specific command codes
const uint32_t kTpmCcVendorCr50 = 0x0000;

// Cr50 vendor-specific subcommand codes. 16 bits available.
// TODO(louiscollard): Replace this with constants from cr50 header.
const uint16_t kVendorCcU2fApdu = 27;
const uint16_t kVendorCcU2fGenerate = 44;
const uint16_t kVendorCcU2fSign = 45;
const uint16_t kVendorCcU2fAttest = 46;

}  // namespace

namespace u2f {

TpmVendorCommandProxy::TpmVendorCommandProxy()
    : transceiver_(std::make_unique<trunks::TrunksDBusProxy>()) {}
TpmVendorCommandProxy::TpmVendorCommandProxy(
    std::unique_ptr<trunks::CommandTransceiver> transceiver)
    : transceiver_(std::move(transceiver)) {
  CHECK(transceiver_);
}

bool TpmVendorCommandProxy::Init() {
  return transceiver_->Init();
}

uint32_t TpmVendorCommandProxy::VendorCommand(uint16_t cc,
                                              const std::string& input,
                                              std::string* output) {
  // Pack up the header and the input
  TpmCmdHeader header;
  header.tag = base::HostToNet16(trunks::TPM_ST_NO_SESSIONS);
  header.size = base::HostToNet32(sizeof(header) + input.size());
  header.code = base::HostToNet32(kTpmCcVendorBit | kTpmCcVendorCr50);
  header.subcommand_code = base::HostToNet16(cc);

  std::string command(reinterpret_cast<char*>(&header), sizeof(header));
  command += input;

  // Send the command, get the response
  VLOG(2) << "Out(" << command.size()
          << "): " << base::HexEncode(command.data(), command.size());
  std::string response = transceiver_->SendCommandAndWait(command);
  VLOG(2) << "In(" << response.size()
          << "):  " << base::HexEncode(response.data(), response.size());

  if (response.size() < sizeof(header)) {
    LOG(ERROR) << "TPM response was too short!";
    return -1;
  }

  // Unpack the response header and any output
  memcpy(&header, response.data(), sizeof(header));
  header.size = base::NetToHost32(header.size);
  header.code = base::NetToHost32(header.code);

  // Pass back any reply beyond the header
  *output = response.substr(sizeof(header));

  return header.code;
}

namespace {

template <typename Request>
std::string RequestToString(const Request& req) {
  return std::string(reinterpret_cast<const char*>(&req), sizeof(req));
}

template <>
std::string RequestToString(const struct u2f_attest_req& req) {
  return std::string(reinterpret_cast<const char*>(&req),
                     offsetof(struct u2f_attest_req, data) + req.dataLen);
}

}  // namespace

template <typename Request, typename Response>
uint32_t TpmVendorCommandProxy::VendorCommandStruct(uint16_t cc,
                                                    const Request& input,
                                                    Response* output) {
  std::string output_str;
  uint32_t resp_code = VendorCommand(cc, RequestToString(input), &output_str);

  if (resp_code == 0) {
    if (output_str.size() == sizeof(*output)) {
      memcpy(output, output_str.data(), sizeof(*output));
    } else {
      LOG(ERROR) << "Invalid response size for successful vendor command, "
                 << "expected: " << sizeof(*output)
                 << ", actual: " << output_str.size();
      return kVendorRcInvalidResponse;
    }
  }

  return resp_code;
}

uint32_t TpmVendorCommandProxy::SendU2fApdu(const std::string& req,
                                            std::string* resp_out) {
  return VendorCommand(kVendorCcU2fApdu, req, resp_out);
}

uint32_t TpmVendorCommandProxy::SendU2fGenerate(
    const struct u2f_generate_req& req, struct u2f_generate_resp* resp_out) {
  if ((req.flags & U2F_UV_ENABLED_KH) != 0) {
    LOG(ERROR) << "Invalid flags in u2f_generate request.";
    return -1;
  }

  return VendorCommandStruct(kVendorCcU2fGenerate, req, resp_out);
}

uint32_t TpmVendorCommandProxy::SendU2fGenerate(
    const struct u2f_generate_req& req,
    struct u2f_generate_versioned_resp* resp_out) {
  if ((req.flags & U2F_UV_ENABLED_KH) == 0) {
    LOG(ERROR) << "Invalid flags in u2f_generate request.";
    return -1;
  }

  return VendorCommandStruct(kVendorCcU2fGenerate, req, resp_out);
}

template <typename Request>
uint32_t TpmVendorCommandProxy::SendU2fSignGeneric(
    const Request& req, struct u2f_sign_resp* resp_out) {
  // |resp_out| can be |nullptr| only when the request is 'check only'
  DCHECK(((req.flags & U2F_AUTH_CHECK_ONLY) == U2F_AUTH_CHECK_ONLY) ||
         resp_out);

  std::string output_str;
  uint32_t resp_code =
      VendorCommand(kVendorCcU2fSign, RequestToString(req), &output_str);

  if (resp_code != 0) {
    return resp_code;
  }

  // A success response may or may not have a body, depending on whether the
  // request was a full sign request, or simply a 'check only' request, to
  // test ownership of the specified key handle.
  if ((req.flags & U2F_AUTH_CHECK_ONLY) == U2F_AUTH_CHECK_ONLY) {
    // We asked to test ownership of a key handle; success response code
    // indicates it is owned. No response body expected.
    if (output_str.size() == 0) {
      return resp_code;
    }
  } else {
    if (resp_out && output_str.size() == sizeof(struct u2f_sign_resp)) {
      memcpy(resp_out, output_str.data(), sizeof(*resp_out));
      return resp_code;
    }
  }
  LOG(ERROR) << "Invalid response size for successful vendor command, "
             << "expected: " << (resp_out ? sizeof(struct u2f_sign_resp) : 0)
             << ", actual: " << output_str.size();
  return kVendorRcInvalidResponse;
}

uint32_t TpmVendorCommandProxy::SendU2fSign(const struct u2f_sign_req& req,
                                            u2f_sign_resp* resp) {
  return SendU2fSignGeneric(req, resp);
}

uint32_t TpmVendorCommandProxy::SendU2fSign(
    const struct u2f_sign_versioned_req& req, u2f_sign_resp* resp) {
  return SendU2fSignGeneric(req, resp);
}

uint32_t TpmVendorCommandProxy::SendU2fAttest(
    const struct u2f_attest_req& req, struct u2f_attest_resp* resp_out) {
  return VendorCommandStruct(kVendorCcU2fAttest, req, resp_out);
}

uint32_t TpmVendorCommandProxy::GetG2fCertificate(std::string* cert_out) {
  constexpr std::array<uint8_t, 0x23> kCertRequest{
      0x80, 0x02,                    // TPM_ST_SESSIONS
      0x00, 0x00, 0x00, 0x23,        // size
      0x00, 0x00, 0x01, 0x4e,        // TPM_CC_NV_READ
      0x01, 0x3f, 0xff, 0x02,        // authHandle : TPMI_RH_NV_AUTH
      0x01, 0x3f, 0xff, 0x02,        // nvIndex    : TPMI_RH_NV_INDEX
      0x00, 0x00, 0x00, 0x09,        // authorizationSize : UINT32
      0x40, 0x00, 0x00, 0x09,        // sessionHandle : empty password
      0x00, 0x00, 0x00, 0x00, 0x00,  // nonce, sessionAttributes, hmac
      0x01, 0x3b,                    // nvSize   : UINT16
      0x00, 0x00                     // nvOffset : UINT16
  };

  constexpr std::array<uint8_t, 16> kExpectedCertResponseHeader{
      0x80, 0x02,              // TPM_ST_SESSIONS
      0x00, 0x00, 0x01, 0x50,  // responseSize
      0x00, 0x00, 0x00, 0x00,  // responseCode : TPM_RC_SUCCESS
      0x00, 0x00, 0x01, 0x3d,  // parameterSize
      0x01, 0x3b,              // TPM2B_MAX_NV_BUFFER : size
  };

  constexpr int kCertSize = 0x013b;
  constexpr int kTpmResponseHeaderSize = 10;
  constexpr int kExpectedCertResponseSize = 0x0150;

  std::string req(kCertRequest.begin(), kCertRequest.end());

  VLOG(2) << "Out(" << req.size()
          << "): " << base::HexEncode(req.data(), req.size());

  std::string resp = transceiver_->SendCommandAndWait(req);

  VLOG(2) << "In(" << resp.size()
          << "):  " << base::HexEncode(resp.data(), resp.size());

  if (resp.size() < kTpmResponseHeaderSize) {
    return kVendorRcInvalidResponse;
  }

  if (resp.size() != kExpectedCertResponseSize ||
      resp.compare(0, 16,
                   std::string(kExpectedCertResponseHeader.begin(),
                               kExpectedCertResponseHeader.end())) != 0) {
    uint32_t resp_code;
    memcpy(&resp_code, resp.c_str() + 6, sizeof(uint32_t));
    return base::NetToHost32(resp_code);
  }

  *cert_out = resp.substr(kExpectedCertResponseHeader.size(), kCertSize);

  return 0;
}

uint32_t TpmVendorCommandProxy::GetRwVersion(TpmRwVersion* version) {
  // GSC tool uses the FW upgrade command to retrieve the RO/RW versions. There
  // are two phases of FW upgrade: connection establishment and actual image
  // transfer. RO/RW versions are included in the response of the first PDU to
  // establish connection, in new enough cr50 protocol versions. We can use this
  // first PDU to retrieve the RW version info we want without side effects. It
  // has all-zero digest and address.
  constexpr std::array<uint8_t, 20> kExtensionFwUpgradeRequest{
      0x80, 0x01,              // tag: TPM_ST_NO_SESSIONS
      0x00, 0x00, 0x00, 0x14,  // length
      0xba, 0xcc, 0xd0, 0x0a,  // ordinal: CONFIG_EXTENSION_COMMAND
      0x00, 0x04,              // subcmd: EXTENSION_FW_UPGRADE
      0x00, 0x00, 0x00, 0x00,  // digest : UINT32
      0x00, 0x00, 0x00, 0x00,  // address : UINT32
  };

  constexpr std::array<uint8_t, 16> kExpectedExtensionFwUpgradeResponseHeader{
      0x80, 0x01,              // TPM_ST_NO_SESSIONS
      0x00, 0x00, 0x00, 0x3c,  // length
      0x00, 0x00, 0x00, 0x00,  // ordinal
      0x00, 0x04,              // subcmd: EXTENSION_FW_UPGRADE
      0x00, 0x00, 0x00, 0x00,  // return_value: TPM_RC_SUCCESS
  };

  constexpr int kTpmResponseHeaderSize = 16;
  constexpr int kResponsePduOffset = 12;
  constexpr int kExpectedResponseSize =
      kResponsePduOffset + sizeof(first_response_pdu);
  constexpr int kRwVersionIndex = 1;

  std::string req(kExtensionFwUpgradeRequest.begin(),
                  kExtensionFwUpgradeRequest.end());

  VLOG(2) << "Out(" << req.size()
          << "): " << base::HexEncode(req.data(), req.size());

  std::string resp = transceiver_->SendCommandAndWait(req);

  VLOG(2) << "In(" << resp.size()
          << "):  " << base::HexEncode(resp.data(), resp.size());

  if (resp.size() < kTpmResponseHeaderSize) {
    return kVendorRcInvalidResponse;
  }

  if (resp.size() != kExpectedResponseSize ||
      resp.compare(
          0, 16,
          std::string(kExpectedExtensionFwUpgradeResponseHeader.begin(),
                      kExpectedExtensionFwUpgradeResponseHeader.end())) != 0) {
    uint32_t resp_code;
    memcpy(&resp_code, resp.c_str() + kResponsePduOffset, sizeof(uint32_t));
    return base::NetToHost32(resp_code);
  }

  struct first_response_pdu pdu;
  memcpy(&pdu, resp.c_str() + kResponsePduOffset, sizeof(first_response_pdu));
  *version = TpmRwVersion{
      .epoch = base::NetToHost32(pdu.shv[kRwVersionIndex].epoch),
      .major = base::NetToHost32(pdu.shv[kRwVersionIndex].major),
      .minor = base::NetToHost32(pdu.shv[kRwVersionIndex].minor),
  };

  return 0;
}

void TpmVendorCommandProxy::LogIndividualCertificate() {
  std::string cert;

  uint32_t cert_status = GetG2fCertificate(&cert);

  if (cert_status) {
    VLOG(1) << "Failed to retrieve G2F certificate: " << std::hex
            << cert_status;
  } else {
    VLOG(1) << "Certificate: " << base::HexEncode(cert.data(), cert.size());
  }
}

}  // namespace u2f
