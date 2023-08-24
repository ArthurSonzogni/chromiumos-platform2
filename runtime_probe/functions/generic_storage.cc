// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/generic_storage.h"

#include "runtime_probe/functions/ata_storage.h"
#include "runtime_probe/functions/mmc_storage.h"
#include "runtime_probe/functions/nvme_storage.h"
#include "runtime_probe/functions/ufs_storage.h"
#include "runtime_probe/probe_function.h"

namespace runtime_probe {

bool GenericStorageFunction::PostParseArguments() {
  runner_.AddFunction(CreateProbeFunction<AtaStorageFunction>());
  runner_.AddFunction(CreateProbeFunction<MmcStorageFunction>());
  runner_.AddFunction(CreateProbeFunction<NvmeStorageFunction>());
  runner_.AddFunction(CreateProbeFunction<UfsStorageFunction>());
  return runner_.IsValid();
}

}  // namespace runtime_probe
