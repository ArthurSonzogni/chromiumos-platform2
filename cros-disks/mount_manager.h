// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines cros-disks::MountManager, which is a base class for implementing
// the filesystem mounting service used by CrosDisksServer. It is further
// subclassed to provide the mounting service for particular types of
// filesystem.

#ifndef CROS_DISKS_MOUNT_MANAGER_H_
#define CROS_DISKS_MOUNT_MANAGER_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/macros.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest_prod.h>

#include "cros-disks/mount_options.h"

namespace cros_disks {

class Metrics;
class MountEntry;
class Platform;

// A base class for managing mounted filesystems of certain kinds.
// It provides template methods for mounting and unmounting filesystems.
// A derived class implements pure virtual methods CanMount, DoMount,
// DoUnmount and SuggestMountPath to provide specific operations for
// supporting certain kinds of filesystem.
class MountManager {
 public:
  // Represents status of a mounted volume.
  struct MountState {
    std::string mount_path;
    bool is_read_only;
  };

  // Constructor that takes a mount root directory, an object for providing
  // platform service, and an object for collecting UMA metrics. The mount
  // root directory |mount_root| must be a non-empty path string, but it is
  // OK if the directory does not exist. Both |platform| and |metrics| must
  // be a valid object. An instance of this class does not take ownership
  // of the |platform| and |metrics| object, and thus expects these objects
  // to exist until its destruction. No actual operation is performed at
  // construction. Initialization is performed when Initializes() is called.
  MountManager(const std::string& mount_root, Platform* platform,
               Metrics* metrics);

  // Destructor that performs no specific operations and does not unmount
  // any mounted or reserved mount paths. A derived class should override
  // the destructor to perform appropriate cleanup, such as unmounting
  // mounted filesystems.
  virtual ~MountManager();

  // Initializes the mount manager. Returns true on success.
  // It must be called only once before other methods are called.
  // This base class provides a default implementation that creates the
  // mount root directory. A derived class can override this method to
  // perform any necessary initialization.
  virtual bool Initialize();

  // Starts a session. Returns true on success.
  // This base class provides a default implementation that does nothing.
  // A derived class can override this method to perform any necessary
  // operations when a session starts. This method is called in response
  // to a SessionStateChanged event from the Chromium OS session manager.
  virtual bool StartSession();

  // Stops a session. Returns true on success.
  // This base class provides a default implementation that does nothing.
  // A derived class can override this method to perform any necessary
  // operations when a session stops. This method is called in response
  // to a SessionStateChanged event from the Chromium OS session manager.
  virtual bool StopSession();

  // Implemented by a derived class to return true if it supports mounting
  // |source_path|.
  virtual bool CanMount(const std::string& source_path) const = 0;

  // Returns true if unmounting |path| is supported by the manager.
  // This base class provides a default implementation such that a path
  // can be unmounted by the manager if it can be mounted by the manager
  // or is an immediate child of the mount root directory where mount
  // directories are created. A derived class can override this method to
  // support additional paths.
  virtual bool CanUnmount(const std::string& path) const;

  // Implemented by a derived class to return the type of mount sources
  // it supports.
  virtual MountSourceType GetMountSourceType() const = 0;

  // Mounts |source_path| to |mount_path| as |filesystem_type| with |options|.
  // If "remount" option exists in |options|, attempts to remount |source_path|
  // to the mount path which it's currently mounted to. Content of |mount_path|
  // will be ignored and |mount_path| is set to the existing mount path.
  // Otherwise, attempts to mount a new source. When mounting a new source, if
  // |mount_path| is an empty string, SuggestMountPath() is called to obtain a
  // suggested mount path. |mount_path| is set to actual mount path on success.
  // If an error occurs and ShouldReserveMountPathOnError() returns true for
  // that type of error, the mount path is reserved and |mount_path| is set to
  // the reserved mount path.
  MountErrorType Mount(const std::string& source_path,
                       const std::string& filesystem_type,
                       const std::vector<std::string>& options,
                       std::string* mount_path);

  // Unmounts |path|, which can be a source path or a mount path,
  // with |options|. If the mount path is reserved during Mount(),
  // this method releases the reserved mount path.
  MountErrorType Unmount(const std::string& path,
                         const std::vector<std::string>& options);

  // Unmounts all mounted paths.
  virtual bool UnmountAll();

  // Adds or updates a mapping |source_path| to its mount state in the cache.
  void AddOrUpdateMountStateCache(const std::string& source_path,
                                  const std::string& mount_path,
                                  bool is_read_only);

  // Gets the corresponding |source_path| of |mount_path| from the cache.
  // Returns false if |mount_path| is not found in the cache.
  bool GetSourcePathFromCache(const std::string& mount_path,
                              std::string* source_path) const;

  // Gets the corresponding |mount_path| of |source_path| from the cache.
  // Returns false if |source_path| is not found in the cache.
  bool GetMountPathFromCache(const std::string& source_path,
                             std::string* mount_path) const;

  // Gets the corresponding mount state of |source_path| from the cache.
  // Returns false if |source_path| is not found in the cache.
  bool GetMountStateFromCache(const std::string& source_path,
                              MountState* mount_state) const;

  // Returns true if |mount_path| is found in the cache.
  bool IsMountPathInCache(const std::string& mount_path) const;

  // Removes |mount_path| from the cache. Returns false if |mount_path|
  // is not found in the cache.
  bool RemoveMountPathFromCache(const std::string& mount_path);

  // Returns true if |mount_path| is reserved.
  bool IsMountPathReserved(const std::string& mount_path) const;

  // Returns the mount error that caused |mount_path| to be reserved, or
  // kMountErrorNone if |mount_path| is not a reserved path.
  MountErrorType GetMountErrorOfReservedMountPath(
      const std::string& mount_path) const;

  // Returns a set of reserved mount paths.
  std::set<std::string> GetReservedMountPaths() const;

  // Adds |mount_path| to the set of reserved mount paths. Also records
  // |error_type| that caused the mount path to be reserved. If a |mount_path|
  // has been reserved, subsequent calls to this method with the same
  // |mount_path| but different |error_type| are ignored.
  void ReserveMountPath(const std::string& mount_path,
                        MountErrorType error_type);

  // Removes |mount_path| from the set of reserved mount paths.
  void UnreserveMountPath(const std::string& mount_path);

  // Gets the mount entries managed by this mount manager. |mount_entries| is
  // overwritten to return the mount entries.
  void GetMountEntries(std::vector<MountEntry>* mount_entries);

 protected:
  // Type definition of a cache mapping a source path to its mount state of
  // filesystems mounted by the manager.
  using MountStateMap = std::map<std::string, MountState>;

  // Type definition of a cache mapping a reserved mount path to the mount
  // error that caused the mount path to be reserved.
  using ReservedMountPathMap = std::map<std::string, MountErrorType>;

  // Mounts |source_path| to |mount_path| as |filesystem_type| with |options|.
  MountErrorType MountNewSource(const std::string& source_path,
                                const std::string& filesystem_type,
                                const std::vector<std::string>& options,
                                std::string* mount_path);

  // Remounts |source_path| on |mount_path| as |filesystem_type| with |options|.
  MountErrorType Remount(const std::string& source_path,
                         const std::string& filesystem_type,
                         const std::vector<std::string>& options,
                         std::string* mount_path);

  // Implemented by a derived class to mount |source_path| to |mount_path|
  // as |filesystem_type| with |options|. An implementation may change the
  // options to mount command, and should store all options to
  // |applied_options|.
  virtual MountErrorType DoMount(const std::string& source_path,
                                 const std::string& filesystem_type,
                                 const std::vector<std::string>& options,
                                 const std::string& mount_path,
                                 MountOptions* applied_options) = 0;

  // Implemented by a derived class to unmount |path| with |options|.
  virtual MountErrorType DoUnmount(const std::string& path,
                                   const std::vector<std::string>& options) = 0;

  // Returns a suggested mount path for |source_path|.
  virtual std::string SuggestMountPath(
      const std::string& source_path) const = 0;

  // Extracts the mount label, if any, from an array of options.
  // If a mount label option is found in |options|, returns true, sets
  // |mount_label| to the mount label value, and removes the option from
  // |options|. If multiple mount label options are given, |mount_label|
  // is set to the last mount label value.
  bool ExtractMountLabelFromOptions(std::vector<std::string>* options,
                                    std::string* mount_label) const;

  // Extracts unmount flags for umount() from an array of options.
  virtual bool ExtractUnmountOptions(const std::vector<std::string>& options,
                                     int *unmount_flags) const;

  // Returns true if the manager should reserve a mount path if the mount
  // operation returns a particular type of error. The default implementation
  // returns false on any error. A derived class should override this method
  // if it needs to reserve mount paths on certain types of error.
  virtual bool ShouldReserveMountPathOnError(MountErrorType error_type) const;

  // Returns true if |path| is an immediate child of |parent|, i.e.
  // |path| is an immediate file or directory under |parent|.
  bool IsPathImmediateChildOfParent(const std::string& path,
                                    const std::string& parent) const;

  // Returns true if |mount_path| is a valid mount path, which should be an
  // immediate child of the mount root specified by |mount_root_|. The check
  // performed by this method takes the simplest approach and does not first try
  // to canonicalize |mount_path|, resolve symlinks or determine the absolute
  // path of |mount_path|, so a legitimate mount path may be deemed as invalid.
  // But we don't consider these cases as part of the use cases of cros-disks.
  bool IsValidMountPath(const std::string& mount_path) const;

  // Returns the root directory under which mount directories are created.
  const std::string& mount_root() const { return mount_root_; }

  // Returns an object that provides platform service.
  Platform* platform() const { return platform_; }

  // Returns an object that collects UMA metrics.
  Metrics* metrics() const { return metrics_; }

 private:
  // The root directory under which mount directories are created.
  std::string mount_root_;

  // An object that provides platform service.
  Platform* platform_;

  // An object that collects UMA metrics.
  Metrics* metrics_;

  // A cache mapping a source path to its mount state of filesystems mounted
  // by the manager.
  MountStateMap mount_states_;

  // A cache mapping a reserved mount path to the error that caused
  // the path to reserved.
  ReservedMountPathMap reserved_mount_paths_;

  FRIEND_TEST(MountManagerTest, ExtractMountLabelFromOptions);
  FRIEND_TEST(MountManagerTest, ExtractMountLabelFromOptionsWithNoMountLabel);
  FRIEND_TEST(MountManagerTest, ExtractMountLabelFromOptionsWithTwoMountLabels);
  FRIEND_TEST(MountManagerTest, ExtractSupportedUnmountOptions);
  FRIEND_TEST(MountManagerTest, ExtractUnsupportedUnmountOptions);
  FRIEND_TEST(MountManagerTest, IsPathImmediateChildOfParent);
  FRIEND_TEST(MountManagerTest, IsValidMountPath);

  DISALLOW_COPY_AND_ASSIGN(MountManager);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_MOUNT_MANAGER_H_
