// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_EMBEDDING_MODEL_DLC_MODEL_LOADER_H_
#define ODML_EMBEDDING_MODEL_DLC_MODEL_LOADER_H_

#include <optional>
#include <queue>
#include <string>
#include <unordered_map>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/types/expected.h>
#include <base/uuid.h>
#include <base/values.h>
#include <metrics/metrics_library.h>

#include "odml/embedding_model/model_info.h"

namespace embedding_model {

// DlcModelLoader loads DLCs that contains the model.
class DlcModelLoader {
 public:
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  enum class LoadDlcHistogram {
    kSuccess = 0,
    kInvalidUuid = 1,
    kReadJsonFailed = 2,
    kParseJsonFailed = 3,
    kNoModelType = 4,
    kNoModelVersion = 5,
    kNoTfliteInfo = 6,
    kUnknownModelType = 7,
    kNoTflitePath = 8,
    kNoBuiltinSpm = 9,
    kNoSpmPath = 10,
    kNoDelegate = 11,
    kInstallFailed = 12,
    kMaxValue = kInstallFailed,
  };

  explicit DlcModelLoader(const raw_ref<MetricsLibraryInterface> metrics);

  using LoadCallback =
      base::OnceCallback<void(std::optional<ModelInfo> model_info)>;

  // Load the DLC as specified by the UUID.
  void LoadDlcWithUuid(const base::Uuid& uuid, LoadCallback callback);

 private:
  struct DlcLoadingState {
    std::optional<ModelInfo> model_info;
    std::queue<base::OnceCallback<void()>> pending_callbacks;
    bool install_launched = false;
  };

  void OnInstallDlcComplete(const base::Uuid& uuid,
                            base::expected<base::FilePath, std::string> result);

  void ResolveInstallResult(const base::Uuid& uuid, LoadCallback callback);

  std::unordered_map<base::Uuid, DlcLoadingState, base::UuidHash>
      loading_state_;

  const raw_ref<MetricsLibraryInterface> metrics_;

  base::WeakPtrFactory<DlcModelLoader> weak_ptr_factory_{this};
};

}  // namespace embedding_model

#endif  // ODML_EMBEDDING_MODEL_DLC_MODEL_LOADER_H_
