// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/client/missive_storage_module.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/task/bind_post_task.h>
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

MissiveStorageModule::MissiveStorageModule(MissiveClient* missive_client)
    : add_record_action_(base::BindPostTask(
          missive_client->origin_task_runner(),
          base::BindRepeating(&MissiveClient::EnqueueRecord,
                              missive_client->GetWeakPtr()))),
      flush_action_(base::BindPostTask(
          missive_client->origin_task_runner(),
          base::BindRepeating(&MissiveClient::Flush,
                              missive_client->GetWeakPtr()))) {}

MissiveStorageModule::~MissiveStorageModule() = default;

// static
void MissiveStorageModule::Create(
    base::OnceCallback<void(StatusOr<scoped_refptr<StorageModuleInterface>>)>
        cb) {
  MissiveClient* const missive_client = MissiveClient::Get();
  if (!missive_client) {
    std::move(cb).Run(base::unexpected(Status(
        error::FAILED_PRECONDITION,
        "Missive Client unavailable, probably has not been initialized")));
    return;
  }
  // Refer to the storage module.
  auto missive_storage_module =
      base::WrapRefCounted(new MissiveStorageModule(missive_client));
  LOG(WARNING) << "Store reporting data by a Missive daemon";
  std::move(cb).Run(missive_storage_module);
  return;
}

void MissiveStorageModule::AddRecord(Priority priority,
                                     Record record,
                                     EnqueueCallback callback) {
  add_record_action_.Run(priority, std::move(record), std::move(callback));
}

void MissiveStorageModule::Flush(Priority priority, FlushCallback callback) {
  flush_action_.Run(priority, std::move(callback));
}
}  // namespace reporting
