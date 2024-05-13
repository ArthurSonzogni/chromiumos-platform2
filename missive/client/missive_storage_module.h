// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_CLIENT_MISSIVE_STORAGE_MODULE_H_
#define MISSIVE_CLIENT_MISSIVE_STORAGE_MODULE_H_

#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>

#include "missive/client/missive_client.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage_module_interface.h"
#include "missive/util/statusor.h"

namespace reporting {

// MissiveStorageModule is a `StorageModuleInterface` implementation that
// channels enqueue and flush calls to `MissiveClient`.
class MissiveStorageModule : public StorageModuleInterface {
 public:
  // Factory method asynchronously creates `MissiveStorageModule` object.
  static void Create(
      base::OnceCallback<void(StatusOr<scoped_refptr<StorageModuleInterface>>)>
          cb);

  MissiveStorageModule(const MissiveStorageModule& other) = delete;
  MissiveStorageModule& operator=(const MissiveStorageModule& other) = delete;

 private:
  friend base::RefCountedThreadSafe<MissiveStorageModule>;

  // Constructor can only be called by `Create` factory method.
  explicit MissiveStorageModule(MissiveClient* missive_client);

  // Refcounted object must have destructor declared protected or private.
  ~MissiveStorageModule() override;

  // Calls `MissiveClient::EnqueueRecord` forwarding the arguments.
  void AddRecord(Priority priority,
                 Record record,
                 EnqueueCallback callback) override;

  // Calls `MissiveClient::Flush` to initiate upload of collected records
  // according to the priority. Called usually for a queue with an infinite or
  // very large upload period. Multiple `Flush` calls can safely run in
  // parallel. Returns error if cannot start upload.
  void Flush(Priority priority, FlushCallback callback) override;

  const base::RepeatingCallback<void(
      Priority, Record, MissiveStorageModule::EnqueueCallback)>
      add_record_action_;
  const base::RepeatingCallback<void(Priority,
                                     MissiveStorageModule::FlushCallback)>
      flush_action_;
};
}  // namespace reporting

#endif  // MISSIVE_CLIENT_MISSIVE_STORAGE_MODULE_H_
