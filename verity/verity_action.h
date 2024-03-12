// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VERITY_VERITY_ACTION_H_
#define VERITY_VERITY_ACTION_H_

#include <memory>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

#include "verity/dm_verity_table.h"

namespace verity {

class BRILLO_EXPORT DmVerityAction {
 public:
  DmVerityAction() = default;
  virtual ~DmVerityAction() = default;

  DmVerityAction(const DmVerityAction&) = delete;
  DmVerityAction& operator=(const DmVerityAction&) = delete;

  // Verifies the given payload and table.
  // Only colocated payloads are supported at this time.
  static int Verify(const base::FilePath& payload_path,
                    const DmVerityTable& dm_verity_table);

  // Static helpers.
  static bool PreVerify(const base::FilePath& payload_path,
                        const DmVerityTable& dm_verity_table);
  static bool TruncatePayloadToSource(
      const base::FilePath& payload_path,
      const base::FilePath& source_img_path,
      const DmVerityTable& dm_verity_table,
      std::unique_ptr<base::File>* out_source_img_file);
};

}  // namespace verity

#endif  // VERITY_VERITY_ACTION_H_
