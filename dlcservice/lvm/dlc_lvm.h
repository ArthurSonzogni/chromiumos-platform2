// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_LVM_DLC_LVM_H_
#define DLCSERVICE_LVM_DLC_LVM_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dlcservice/dlc_base.h"
#include "dlcservice/types.h"
#include "dlcservice/utils/utils.h"
#include "dlcservice/utils/utils_interface.h"

namespace dlcservice {

// TODO(b/236007986): Restructure parent/base relationship. Create a factory or
// similar design to create DLC image types.
//
// DLC class that is LVM backed.
class DlcLvm : public DlcBase {
 public:
  explicit DlcLvm(DlcId id)
      : DlcLvm(std::move(id), std::make_shared<Utils>()) {}
  DlcLvm(DlcId id, std::shared_ptr<UtilsInterface> utils);
  virtual ~DlcLvm() = default;

  DlcLvm(const DlcLvm&) = delete;
  DlcLvm& operator=(const DlcLvm&) = delete;

 protected:
  FRIEND_TEST(DlcLvmTest, CreateDlc);
  FRIEND_TEST(DlcLvmTest, CreateDlcLvmFailed);
  FRIEND_TEST(DlcLvmTest, DeleteDlc);
  FRIEND_TEST(DlcLvmTest, DeleteDlcLvmFailed);
  FRIEND_TEST(DlcLvmTest, MountDlc);
  FRIEND_TEST(DlcLvmTest, MountDlcImageLoaderFailed);
  FRIEND_TEST(DlcLvmTest, MountDlcEmptyMountPoint);
  FRIEND_TEST(DlcLvmTest, IsActiveImagePresent);
  FRIEND_TEST(DlcLvmTest, LvmMigrationCheckNoFileBasedImages);
  FRIEND_TEST(DlcLvmTest, LvmMigrationCheckLogicalVolumesExist);
  FRIEND_TEST(DlcLvmTest, LvmMigrationCheckFileBasedPriority);

  // `DlcBase` overrides.
  bool CreateDlc(brillo::ErrorPtr* err) override;
  bool DeleteInternal(brillo::ErrorPtr* err) override;
  bool MountInternal(std::string* mount_point, brillo::ErrorPtr* err) override;
  bool MakeReadyForUpdateInternal() const override;
  bool VerifyInternal(const base::FilePath& image_path,
                      std::vector<uint8_t>* image_sha256) override;
  base::FilePath GetImagePath(BootSlot::Slot slot) const override;
  bool IsActiveImagePresent() const override;

  virtual bool UseLogicalVolume() const;

 private:
  bool CreateDlcLogicalVolumes();
  bool DeleteInternalLogicalVolumes();
};

}  // namespace dlcservice

#endif  // DLCSERVICE_LVM_DLC_LVM_H_
