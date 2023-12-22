// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/fuzzers/fuzzed_chaps_factory.h"

#include <brillo/secure_blob.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "chaps/chaps_factory.h"
#include "chaps/fuzzers/fuzzed_object_pool.h"
#include "chaps/session_mock.h"

namespace chaps {

FuzzedChapsFactory::FuzzedChapsFactory(FuzzedDataProvider* data_provider)
    : data_provider_(data_provider),
      random_seed_(
          brillo::SecureBlob(data_provider_->ConsumeRandomLengthString())) {}

Session* FuzzedChapsFactory::CreateSession(int slot_id,
                                           ObjectPool* token_object_pool,
                                           const hwsec::ChapsFrontend* hwsec,
                                           HandleGenerator* handle_generator,
                                           bool is_read_only) {
  return new SessionMock();
}

ObjectPool* FuzzedChapsFactory::CreateObjectPool(
    HandleGenerator* handle_generator,
    SlotPolicy* slot_policy,
    ObjectStore* store) {
  return new FuzzedObjectPool(data_provider_);
}

ObjectStore* FuzzedChapsFactory::CreateObjectStore(
    const base::FilePath& file_name) {
  return nullptr;
}

Object* FuzzedChapsFactory::CreateObject() {
  return nullptr;
}

ObjectPolicy* FuzzedChapsFactory::CreateObjectPolicy(CK_OBJECT_CLASS type) {
  return nullptr;
}

SlotPolicy* FuzzedChapsFactory::CreateSlotPolicy(bool is_shared_slot) {
  return nullptr;
}

ObjectPolicy* FuzzedChapsFactory::GetObjectPolicyForType(CK_OBJECT_CLASS type) {
  return nullptr;
}

const brillo::SecureBlob& FuzzedChapsFactory::GetRandomSeed() const {
  return random_seed_;
}

}  // namespace chaps
