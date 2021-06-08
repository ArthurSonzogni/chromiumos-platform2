// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/csme/pinweaver_provision_client.h"

#include <algorithm>
#include <string>

#include <base/logging.h>

#include "trunks/csme/mei_client.h"
#include "trunks/csme/mei_client_factory.h"
#include "trunks/csme/pinweaver_csme_types.h"

namespace trunks {
namespace csme {

namespace {

template <typename Type>
std::string SerializeToString(const Type& t) {
  const char* buffer = reinterpret_cast<const char*>(&t);
  return std::string(buffer, buffer + sizeof(t));
}

template <typename Type>
bool UnpackFromResponse(const pw_heci_header_req& req_header,
                        const std::string& response,
                        Type* output) {
  struct __attribute__((packed)) PackedResponse {
    pw_heci_header_res header;
    Type output;
  };
  if (response.size() != sizeof(PackedResponse)) {
    LOG(ERROR) << __func__ << ": Unexpected size for fixed-sized response: "
               << response.size() << "; expecting " << sizeof(PackedResponse)
               << ".";
    return false;
  }
  const PackedResponse* resp =
      reinterpret_cast<const PackedResponse*>(response.data());

  // Perform rationality check.
  if (req_header.pw_heci_seq != resp->header.pw_heci_seq) {
    LOG(ERROR) << __func__ << ": Mismatched seqquence: expected "
               << req_header.pw_heci_seq << " got " << resp->header.pw_heci_seq;
    return false;
  }
  if (req_header.pw_heci_cmd != resp->header.pw_heci_cmd) {
    LOG(ERROR) << __func__ << ": Mismatched command: expected "
               << req_header.pw_heci_cmd << " got " << resp->header.pw_heci_cmd;
    return false;
  }
  if (resp->header.pw_heci_rc) {
    LOG(ERROR) << __func__
               << ": CSME returns error: " << resp->header.pw_heci_rc;
    return false;
  }
  if (resp->header.total_length != sizeof(Type)) {
    LOG(ERROR) << __func__
               << ": Unexpected payload length: " << resp->header.total_length;
    return false;
  }
  *output = resp->output;
  return true;
}

}  // namespace

PinWeaverProvisionClient::PinWeaverProvisionClient(
    MeiClientFactory* mei_client_factory)
    : mei_client_factory_(mei_client_factory) {
  CHECK(mei_client_factory_);
}

// TODO(b/b:190621192): Extract the common code for all commands using tamplate
// or lambda.
bool PinWeaverProvisionClient::SetSaltingKeyHash(const std::string& hash) {
  pw_prov_salting_key_hash_set_request req;
  BuildFixedSizedRequest(PW_SALTING_KEY_HASH_SET, &req);
  if (hash.size() != req.header.total_length) {
    LOG(ERROR) << __func__ << ": unexpected hash size: " << hash.size();
    return false;
  }
  std::copy(hash.begin(), hash.end(), req.buffer);
  const std::string request = SerializeToString(req);
  std::string response;
  if (!GetMeiClient()->Send(request) || !GetMeiClient()->Receive(&response)) {
    LOG(ERROR) << __func__ << ": Failed to send request.";
    return false;
  }

  uint32_t rc = 0;
  if (!UnpackFromResponse(req.header, response, &rc)) {
    LOG(ERROR) << __func__ << ": failed to unpack response.";
    return false;
  }
  if (rc) {
    LOG(ERROR) << __func__ << ": Operation failed: " << rc;
    return false;
  }
  return true;
}

bool PinWeaverProvisionClient::GetSaltingKeyHash(std::string* salting_key_hash,
                                                 bool* committed) {
  pw_prov_salting_key_hash_commit_request req;
  BuildFixedSizedRequest(PW_SALTING_KEY_HASH_GET, &req);
  const std::string request = SerializeToString(req);
  std::string response;
  if (!GetMeiClient()->Send(request) || !GetMeiClient()->Receive(&response)) {
    LOG(ERROR) << __func__ << ": Failed to send request.";
    return false;
  }

  struct __attribute__((packed)) {
    uint32_t rc;  // response code
    uint8_t committed;
    uint8_t buffer[PW_SHA_256_DIGEST_SIZE];
  } payload;
  if (!UnpackFromResponse(req.header, response, &payload)) {
    LOG(ERROR) << __func__ << ": failed to unpack response.";
    return false;
  }
  if (payload.rc) {
    LOG(ERROR) << __func__ << ": Operation failed: " << payload.rc;
    return false;
  }
  *committed = payload.committed;
  salting_key_hash->assign(std::begin(payload.buffer),
                           std::end(payload.buffer));
  return true;
}

bool PinWeaverProvisionClient::CommitSaltingKeyHash() {
  pw_prov_salting_key_hash_commit_request req;
  BuildFixedSizedRequest(PW_SALTING_KEY_HASH_COMMIT, &req);
  const std::string request = SerializeToString(req);
  std::string response;
  if (!GetMeiClient()->Send(request) || !GetMeiClient()->Receive(&response)) {
    LOG(ERROR) << __func__ << ": Failed to send request.";
    return false;
  }

  uint32_t rc = 0;
  if (!UnpackFromResponse(req.header, response, &rc)) {
    LOG(ERROR) << __func__ << ": failed to unpack response.";
    return false;
  }
  if (rc) {
    LOG(ERROR) << __func__ << ": Operation failed: " << rc;
    return false;
  }
  return true;
}

bool PinWeaverProvisionClient::InitOwner() {
  pw_prov_initialize_owner_request req;
  BuildFixedSizedRequest(PW_PROV_INITIALIZE_OWNER, &req);
  const std::string request = SerializeToString(req);
  std::string response;
  if (!GetMeiClient()->Send(request) || !GetMeiClient()->Receive(&response)) {
    LOG(ERROR) << __func__ << ": Failed to send request.";
    return false;
  }

  uint32_t rc = 0;
  if (!UnpackFromResponse(req.header, response, &rc)) {
    LOG(ERROR) << __func__ << ": failed to unpack response.";
    return false;
  }
  if (rc) {
    LOG(ERROR) << __func__ << ": Operation failed: " << rc;
    return false;
  }
  return true;
}

MeiClient* PinWeaverProvisionClient::GetMeiClient() {
  if (!mei_client_) {
    mei_client_ = mei_client_factory_->CreateMeiClientForPinWeaverProvision();
  }
  return mei_client_.get();
}

template <typename Type>
void PinWeaverProvisionClient::BuildFixedSizedRequest(int cmd, Type* req) {
  req->header.pw_heci_cmd = cmd;
  req->header.pw_heci_seq = seq_++;
  req->header.total_length = sizeof(Type) - sizeof(req->header);
}

}  // namespace csme
}  // namespace trunks
