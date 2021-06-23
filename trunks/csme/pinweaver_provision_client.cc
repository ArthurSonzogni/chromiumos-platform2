// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/csme/pinweaver_provision_client.h"

#include <algorithm>
#include <string>

#include <base/check.h>
#include <base/logging.h>

#include "trunks/csme/mei_client.h"
#include "trunks/csme/mei_client_factory.h"
#include "trunks/csme/pinweaver_csme_types.h"

namespace trunks {
namespace csme {

// TODO(b/190621192): Extract these utility functions so we can share the
// implementation with other types of CSME clients.
namespace {

template <typename Type>
std::string SerializeToString(const Type& t) {
  const char* buffer = reinterpret_cast<const char*>(&t);
  return std::string(buffer, buffer + sizeof(t));
}

bool CheckResponse(const pw_heci_header_req& req_header,
                   const pw_heci_header_res& resp_header) {
  if (req_header.pw_heci_seq != resp_header.pw_heci_seq) {
    LOG(ERROR) << __func__ << ": Mismatched sequence: expected "
               << req_header.pw_heci_seq << " got " << resp_header.pw_heci_seq;
    return false;
  }
  if (resp_header.pw_heci_rc) {
    LOG(ERROR) << __func__
               << ": CSME returns error: " << resp_header.pw_heci_rc;
    return false;
  }
  if (req_header.pw_heci_cmd != resp_header.pw_heci_cmd) {
    LOG(ERROR) << __func__ << ": Mismatched command: expected "
               << req_header.pw_heci_cmd << " got " << resp_header.pw_heci_cmd;
    return false;
  }
  return true;
}

// Implementation of deserialization of the packed data from CSME at recursion.
template <typename... OutputTypes>
class UnpackImpl;

// Unpacks the first data from the serialized payload, and invokes recursion.
template <typename FirstOutputType, typename... OutputTypes>
class UnpackImpl<FirstOutputType, OutputTypes...> {
 public:
  UnpackImpl() = default;
  bool Unpack(const std::string& serialized,
              FirstOutputType* first,
              OutputTypes*... outputs) {
    if (serialized.size() < sizeof(*first)) {
      LOG(ERROR) << __func__ << ": Serialized data too short; expected >= "
                 << sizeof(*first) << "; got " << serialized.size();
      return false;
    }
    memcpy(first, serialized.data(), sizeof(*first));
    return UnpackImpl<OutputTypes...>().Unpack(
        serialized.substr(sizeof(*first)), outputs...);
  }
};

// Special handling for cases that all the output data are unpacked.
template <>
class UnpackImpl<> {
 public:
  UnpackImpl() = default;
  bool Unpack(const std::string& serialized) {
    if (!serialized.empty()) {
      LOG(ERROR) << __func__ << ": Execessively long data; reminaing size="
                 << serialized.size();
      return false;
    }
    return true;
  }
};

// Deserializes the response from CSME, including the integrity check against
// the CSME command.
template <typename... OutputTypes>
bool UnpackFromResponse(const pw_heci_header_req& req_header,
                        const std::string& response,
                        OutputTypes*... outputs) {
  if (response.size() < sizeof(pw_heci_header_res)) {
    LOG(ERROR) << __func__ << ": response too short; size=" << response.size();
    return false;
  }
  const pw_heci_header_res* resp_header =
      reinterpret_cast<const pw_heci_header_res*>(response.data());

  if (!CheckResponse(req_header, *resp_header)) {
    LOG(ERROR) << __func__ << ": Failed to vlaidate response header.";
    return false;
  }

  const std::string serialized_outputs =
      response.substr(sizeof(pw_heci_header_res));
  if (resp_header->total_length != serialized_outputs.size()) {
    LOG(ERROR) << __func__ << ": Unexpected payload length; specified: "
               << resp_header->total_length << " actual "
               << serialized_outputs.size();
    return false;
  }
  if (!UnpackImpl<OutputTypes...>().Unpack(serialized_outputs, outputs...)) {
    LOG(ERROR) << __func__ << ": Unpacking error.";
    return false;
  }
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

  if (!UnpackFromResponse(req.header, response)) {
    LOG(ERROR) << __func__ << ": failed to unpack response.";
    return false;
  }
  return true;
}

bool PinWeaverProvisionClient::GetSaltingKeyHash(std::string* salting_key_hash,
                                                 bool* committed) {
  pw_prov_salting_key_hash_get_request req;
  BuildFixedSizedRequest(PW_SALTING_KEY_HASH_GET, &req);
  const std::string request = SerializeToString(req);
  std::string response;
  if (!GetMeiClient()->Send(request) || !GetMeiClient()->Receive(&response)) {
    LOG(ERROR) << __func__ << ": Failed to send request.";
    return false;
  }

  uint8_t buffer[PW_SHA_256_DIGEST_SIZE];
  if (!UnpackFromResponse(req.header, response, committed, &buffer)) {
    LOG(ERROR) << __func__ << ": failed to unpack response.";
    return false;
  }
  salting_key_hash->assign(std::begin(buffer), std::end(buffer));
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
  if (!UnpackFromResponse(req.header, response)) {
    LOG(ERROR) << __func__ << ": failed to unpack response.";
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

  if (!UnpackFromResponse(req.header, response)) {
    LOG(ERROR) << __func__ << ": failed to unpack response.";
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
