// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_LVM_DLC_LVM_H_
#define DLCSERVICE_LVM_DLC_LVM_H_

#include <string>

#include "dlcservice/dlc.h"
#include "dlcservice/types.h"

namespace dlcservice {

// TODO(b/236007986): Restructure parent/base relationship. Create a factory or
// similar design to create DLC image types.
//
// DLC class that is LVM backed.
class DlcLvm : public DlcBase {
 public:
  explicit DlcLvm(DlcId id);
  virtual ~DlcLvm() = default;

  DlcLvm(const DlcLvm&) = delete;
  DlcLvm& operator=(const DlcLvm&) = delete;

 protected:
  // `DlcBase` overrides.
  bool CreateDlc(brillo::ErrorPtr* err) override;
  bool DeleteInternal(brillo::ErrorPtr* err) override;
  bool MountInternal(std::string* mount_point, brillo::ErrorPtr* err) override;
  bool MakeReadyForUpdateInternal() const override;
  base::FilePath GetVirtualImagePath(BootSlot::Slot slot) const override;

 private:
  bool CreateDlcLogicalVolumes();
  bool DeleteInternalLogicalVolumes();
};

}  // namespace dlcservice

#endif  // DLCSERVICE_LVM_DLC_LVM_H_
