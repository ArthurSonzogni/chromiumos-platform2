// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_DLC_BASE_H_
#define DLCSERVICE_DLC_BASE_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <brillo/errors/error.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <libimageloader/manifest.h>
#include <chromeos/dbus/service_constants.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/types.h"
#include "dlcservice/utils/utils.h"
#include "dlcservice/utils/utils_interface.h"

namespace dlcservice {

class DlcInterface {
 public:
  DlcInterface() = default;
  virtual ~DlcInterface() = default;

  DlcInterface(const DlcInterface&) = delete;
  DlcInterface& operator=(const DlcInterface&) = delete;

  // Initializes the DLC. This should be called right after creating the DLC
  // object.
  virtual bool Initialize() = 0;

  // Returns the ID of the DLC.
  virtual const DlcId& GetId() const = 0;

  // Returns the human readable name of the DLC.
  virtual const std::string& GetName() const = 0;

  // Returns the description of the DLC.
  virtual const std::string& GetDescription() const = 0;

  // Update the current state of the DLC.
  virtual void UpdateState() = 0;

  // Returns the current state of the DLC.
  virtual DlcState GetState() const = 0;

  // Returns the root directory inside a mounted DLC module.
  virtual base::FilePath GetRoot() const = 0;

  // Returns true if the DLC is currently being installed.
  virtual bool IsInstalling() const = 0;

  // Returns true if the DLC is already installed and mounted.
  virtual bool IsInstalled() const = 0;

  // Returns true if the DLC is marked verified.
  virtual bool IsVerified() const = 0;

  // Returns true if the DLC is scaled.
  virtual bool IsScaled() const = 0;

  // Returns true if the DLC wants to force OTA.
  virtual bool IsForceOTA() const = 0;

  // Returns true if the DLC is user-tied.
  virtual bool IsUserTied() const = 0;

  // Returns true if the DLC has any content on disk that is taking space. This
  // means mainly if it has images on disk.
  virtual bool HasContent() const = 0;

  // Returns the amount of disk space this DLC is using right now.
  virtual uint64_t GetUsedBytesOnDisk() const = 0;

  // Returns true if the DLC has a boolean true for 'preload-allowed'
  // attribute in the manifest for the given |id| and |package|.
  virtual bool IsPreloadAllowed() const = 0;

  // Returns true if the DLC has a boolean true for 'factory-install'
  // attribute in the manifest for the given `id` and `package`.
  virtual bool IsFactoryInstall() const = 0;

  // Creates the DLC image based on the fields from the manifest if the DLC is
  // not installed. If the DLC image exists or is installed already, some
  // verifications are passed to validate that the DLC is mounted.
  // Initializes the installation like creating the necessary files, etc.
  virtual bool Install(brillo::ErrorPtr* err) = 0;

  // This is called after the update_engine finishes the installation of a
  // DLC. This marks the DLC as installed and mounts the DLC image.
  virtual bool FinishInstall(bool installed_by_ue, brillo::ErrorPtr* err) = 0;

  // Cancels the ongoing installation of this DLC. The state will be set to
  // uninstalled after this call if successful.
  // The |err_in| argument is the error that causes the install to be cancelled.
  virtual bool CancelInstall(const brillo::ErrorPtr& err_in,
                             brillo::ErrorPtr* err) = 0;

  // Uninstalls the DLC.
  // Deletes all files associated with the DLC.
  virtual bool Uninstall(brillo::ErrorPtr* err) = 0;

  // Is called when the DLC image is finally installed on the disk and is
  // verified.
  virtual bool InstallCompleted(brillo::ErrorPtr* err) = 0;

  // Is called when the inactive DLC image is updated and verified.
  virtual bool UpdateCompleted(brillo::ErrorPtr* err) = 0;

  // Makes the DLC ready to be updated (creates and resizes the inactive
  // image). Returns false if anything goes wrong.
  virtual bool MakeReadyForUpdate() const = 0;

  // Changes the install progress on this DLC. Only changes if the |progress| is
  // greater than the current progress value.
  virtual void ChangeProgress(double progress) = 0;

  // Toggle for DLC to be reserved.
  // Will return the value set, pass `nullptr` to use as getter.
  virtual bool SetReserve(std::optional<bool> reserve) = 0;

  // Create DLC slots and load deployed DLC image into the slots.
  virtual bool Deploy(brillo::ErrorPtr* err) = 0;

  // Unmount the DLC image and set the status to `NOT_INSTALLED`.
  virtual bool Unload(brillo::ErrorPtr* err) = 0;
};

// TODO(kimjae): Make `DlcBase` a true base class by only holding and
// implementation truly common methods.
class DlcBase : public DlcInterface {
 public:
  explicit DlcBase(DlcId id)
      : DlcBase(std::move(id), std::make_shared<Utils>()) {}
  DlcBase(DlcId id, std::shared_ptr<UtilsInterface> utils)
      : id_(std::move(id)), utils_(utils), weak_ptr_factory_{this} {}
  virtual ~DlcBase() = default;

  DlcBase(const DlcBase&) = delete;
  DlcBase& operator=(const DlcBase&) = delete;

  bool Initialize() override;
  const DlcId& GetId() const override;
  const std::string& GetName() const override;
  const std::string& GetDescription() const override;
  void UpdateState() override;
  DlcState GetState() const override;
  base::FilePath GetRoot() const override;
  bool IsInstalling() const override;
  bool IsInstalled() const override;
  bool IsVerified() const override;
  bool IsScaled() const override;
  bool IsForceOTA() const override;
  bool IsUserTied() const override;
  bool HasContent() const override;
  uint64_t GetUsedBytesOnDisk() const override;
  bool IsPreloadAllowed() const override;
  bool IsFactoryInstall() const override;
  bool Install(brillo::ErrorPtr* err) override;
  bool FinishInstall(bool installed_by_ue, brillo::ErrorPtr* err) override;
  bool CancelInstall(const brillo::ErrorPtr& err_in,
                     brillo::ErrorPtr* err) override;
  bool Uninstall(brillo::ErrorPtr* err) override;
  bool InstallCompleted(brillo::ErrorPtr* err) override;
  bool UpdateCompleted(brillo::ErrorPtr* err) override;
  bool MakeReadyForUpdate() const override;
  void ChangeProgress(double progress) override;
  bool SetReserve(std::optional<bool> reserve) override;
  bool Deploy(brillo::ErrorPtr* err) override;
  bool Unload(brillo::ErrorPtr* err) override;

 protected:
  friend class DBusServiceTest;
  FRIEND_TEST(DlcBaseTest, InitializationReservedSpace);
  FRIEND_TEST(DlcBaseTest, InitializationReservedSpaceOmitted);
  FRIEND_TEST(DlcBaseTestRemovable,
              InitializationReservedSpaceOnRemovableDevice);
  FRIEND_TEST(DlcBaseTest, InitializationReservedSpaceDoesNotSparsifyAgain);
  FRIEND_TEST(DlcBaseTest, ReinstallingNonReservedSpaceDoesNotSparsifyAgain);
  FRIEND_TEST(DBusServiceTest, GetInstalled);
  FRIEND_TEST(DlcBaseTest, GetUsedBytesOnDisk);
  FRIEND_TEST(DlcBaseTest, DefaultState);
  FRIEND_TEST(DlcBaseTest, ChangeStateNotInstalled);
  FRIEND_TEST(DlcBaseTest, ChangeStateInstalling);
  FRIEND_TEST(DlcBaseTest, ChangeStateInstalled);
  FRIEND_TEST(DlcBaseTest, ChangeProgress);
  FRIEND_TEST(DlcBaseTest, MakeReadyForUpdate);
  FRIEND_TEST(DlcBaseTest, MarkUnverified);
  FRIEND_TEST(DlcBaseTest, MarkVerified);
  FRIEND_TEST(DlcBaseTest, PreloadCopyShouldMarkUnverified);
  FRIEND_TEST(DlcBaseTest, PreloadCopyFailOnInvalidFileSize);
  FRIEND_TEST(DlcBaseTest, InstallingCorruptPreloadedImageCleansUp);
  FRIEND_TEST(DlcBaseTest, PreloadingSkippedOnAlreadyVerifiedDlc);
  FRIEND_TEST(DlcBaseTest, UnmountClearsMountPoint);
  FRIEND_TEST(DlcBaseTest, ReserveInstall);
  FRIEND_TEST(DlcBaseTest, UnReservedInstall);
  FRIEND_TEST(DlcBaseTest, IsInstalledButUnmounted);
  FRIEND_TEST(DlcBaseTest, DeployingSkippedOnInstalledDLC);
  FRIEND_TEST(DlcBaseTest, DeployingSkippedOnInstallingDLC);
  FRIEND_TEST(DlcBaseTest, GetDaemonStorePath);

  virtual bool MakeReadyForUpdateInternal() const;

  // Returns the path to the DLC image given the slot. Returns empty path on
  // error.
  virtual base::FilePath GetImagePath(BootSlot::Slot slot) const;

  // Creates the DLC directories and files if they don't exist. This function
  // should be used as fall-through. We should call this even if we presumably
  // know the files are already there. This allows us to create any new DLC
  // files that didn't exist on a previous version of the DLC.
  virtual bool CreateDlc(brillo::ErrorPtr* err);

  // Mark the current active DLC image as verified.
  bool MarkVerified();

  // Mark the current active DLC image as unverified.
  bool MarkUnverified();

  // Returns true if the DLC image in the current active slot matches the hash
  // of that in the rootfs manifest for the DLC.
  bool Verify();
  virtual bool VerifyInternal(const base::FilePath& image_path,
                              std::vector<uint8_t>* image_sha256);

  // Helper used to load in (copy + cleanup) preloadable files for the DLC.
  bool PreloadedCopier(brillo::ErrorPtr* err);

  // Helper used to load in (copy + cleanup) factory installed DLC.
  bool FactoryInstallCopier();

  // Helper used to load in (copy + cleanup) deployed DLC.
  bool DeployCopier(brillo::ErrorPtr* err);

  // Mounts the DLC image.
  bool Mount(brillo::ErrorPtr* err);
  virtual bool MountInternal(std::string* mount_point, brillo::ErrorPtr* err);

  // Unmounts the DLC image.
  bool Unmount(brillo::ErrorPtr* err);

  // Returns true if the active DLC image is present.
  virtual bool IsActiveImagePresent() const;

  // Deletes DLC and performs related cleanups.
  bool Delete(brillo::ErrorPtr* err);

  // Deletes all directories related to this DLC.
  virtual bool DeleteInternal(brillo::ErrorPtr* err);

  // Changes the state of the current DLC. It also notifies the state change
  // reporter that a state change has been made.
  void ChangeState(DlcState::State state);

  // Sets the DLC as being active or not based on |active| value.
  void SetActiveValue(bool active);
  void OnSetActiveValueSuccess();
  void OnSetActiveValueError(brillo::Error* err);

  DlcId id_;
  std::string package_;

  // The verification value which validates the current verification stamps is
  // valid.
  std::string verification_value_;

  DlcState state_;

  base::FilePath mount_point_;

  std::shared_ptr<imageloader::Manifest> manifest_;

  std::shared_ptr<UtilsInterface> utils_;

  // Indicator to keep DLC in cache even if installation fails.
  bool reserve_ = false;

  // The directories on the stateful partition where the DLC image will reside.
  base::FilePath content_id_path_;
  base::FilePath content_package_path_;
  base::FilePath prefs_path_;
  base::FilePath prefs_package_path_;
  base::FilePath preloaded_image_path_;
  base::FilePath factory_install_image_path_;
  base::FilePath deployed_image_path_;

  base::WeakPtrFactory<DlcBase> weak_ptr_factory_;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_BASE_H_
