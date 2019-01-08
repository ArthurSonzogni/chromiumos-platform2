// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/disk_manager.h"

#include <inttypes.h>
#include <libudev.h>
#include <string.h>
#include <sys/mount.h>
#include <time.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>

#include "cros-disks/exfat_mounter.h"
#include "cros-disks/filesystem.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/ntfs_mounter.h"
#include "cros-disks/platform.h"
#include "cros-disks/system_mounter.h"
#include "cros-disks/udev_device.h"

using std::map;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

namespace cros_disks {

namespace {

const char kBlockSubsystem[] = "block";
const char kMmcSubsystem[] = "mmc";
const char kScsiSubsystem[] = "scsi";
const char kScsiDevice[] = "scsi_device";
const char kUdevAddAction[] = "add";
const char kUdevChangeAction[] = "change";
const char kUdevRemoveAction[] = "remove";
const char kPropertyDiskEjectRequest[] = "DISK_EJECT_REQUEST";
const char kPropertyDiskMediaChange[] = "DISK_MEDIA_CHANGE";

// An EnumerateBlockDevices callback that appends a Disk object, created from
// |dev|, to |disks| if |dev| should not be ignored by cros-disks. Always
// returns true to continue the enumeration in EnumerateBlockDevices.
bool AppendDiskIfNotIgnored(vector<Disk>* disks, udev_device* dev) {
  DCHECK(disks);
  DCHECK(dev);

  UdevDevice device(dev);
  if (!device.IsIgnored())
    disks->push_back(device.ToDisk());

  return true;  // Continue the enumeration.
}

// An EnumerateBlockDevices callback that checks if |dev| matches |path|. If
// it's a match, sets |match| to true and |disk| (if not NULL) to a Disk object
// created from |dev|, and returns false to stop the enumeration in
// EnumerateBlockDevices. Otherwise, sets |match| to false, leaves |disk|
// unchanged, and returns true to continue the enumeration in
// EnumerateBlockDevices.
bool MatchDiskByPath(const string& path,
                     bool* match,
                     Disk* disk,
                     udev_device* dev) {
  DCHECK(match);
  DCHECK(dev);

  const char* sys_path = udev_device_get_syspath(dev);
  const char* dev_path = udev_device_get_devpath(dev);
  const char* dev_file = udev_device_get_devnode(dev);
  *match = (sys_path && path == sys_path) || (dev_path && path == dev_path) ||
           (dev_file && path == dev_file);
  if (!*match)
    return true;  // Not a match. Continue the enumeration.

  if (disk)
    *disk = UdevDevice(dev).ToDisk();

  return false;  // Match. Stop enumeration.
}

}  // namespace

DiskManager::DiskManager(const string& mount_root,
                         Platform* platform,
                         Metrics* metrics,
                         DeviceEjector* device_ejector)
    : MountManager(mount_root, platform, metrics),
      device_ejector_(device_ejector),
      udev_(udev_new()),
      udev_monitor_fd_(0),
      eject_device_on_unmount_(true) {
  CHECK(device_ejector_) << "Invalid device ejector";
  CHECK(udev_) << "Failed to initialize udev";
  udev_monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  CHECK(udev_monitor_) << "Failed to create a udev monitor";
  udev_monitor_filter_add_match_subsystem_devtype(udev_monitor_,
                                                  kBlockSubsystem, nullptr);
  udev_monitor_filter_add_match_subsystem_devtype(udev_monitor_, kMmcSubsystem,
                                                  nullptr);
  udev_monitor_filter_add_match_subsystem_devtype(udev_monitor_, kScsiSubsystem,
                                                  kScsiDevice);
  udev_monitor_enable_receiving(udev_monitor_);
  udev_monitor_fd_ = udev_monitor_get_fd(udev_monitor_);
}

DiskManager::~DiskManager() {
  UnmountAll();
  udev_monitor_unref(udev_monitor_);
  udev_unref(udev_);
}

bool DiskManager::Initialize() {
  RegisterDefaultFilesystems();

  // Since there are no udev add events for the devices that already exist
  // when the disk manager starts, emulate udev add events for these devices
  // to correctly populate |disks_detected_|.
  EnumerateBlockDevices(base::Bind(&DiskManager::EmulateBlockDeviceEvent,
                                   base::Unretained(this), kUdevAddAction));

  return MountManager::Initialize();
}

bool DiskManager::StopSession() {
  return UnmountAll();
}

bool DiskManager::EmulateBlockDeviceEvent(const char* action,
                                          udev_device* dev) {
  DCHECK(dev);

  DeviceEventList events;
  ProcessBlockDeviceEvents(dev, action, &events);

  return true;  // Continue the enumeration.
}

vector<Disk> DiskManager::EnumerateDisks() const {
  vector<Disk> disks;
  EnumerateBlockDevices(
      base::Bind(&AppendDiskIfNotIgnored, base::Unretained(&disks)));
  return disks;
}

void DiskManager::EnumerateBlockDevices(
    const base::Callback<bool(udev_device* dev)>& callback) const {
  udev_enumerate* enumerate = udev_enumerate_new(udev_);
  udev_enumerate_add_match_subsystem(enumerate, kBlockSubsystem);
  udev_enumerate_scan_devices(enumerate);

  udev_list_entry *device_list, *device_list_entry;
  device_list = udev_enumerate_get_list_entry(enumerate);
  udev_list_entry_foreach(device_list_entry, device_list) {
    const char* path = udev_list_entry_get_name(device_list_entry);
    udev_device* dev = udev_device_new_from_syspath(udev_, path);
    if (dev == nullptr)
      continue;

    LOG(INFO) << "Device";
    LOG(INFO) << "   Node: " << udev_device_get_devnode(dev);
    LOG(INFO) << "   Subsystem: " << udev_device_get_subsystem(dev);
    LOG(INFO) << "   Devtype: " << udev_device_get_devtype(dev);
    LOG(INFO) << "   Devpath: " << udev_device_get_devpath(dev);
    LOG(INFO) << "   Sysname: " << udev_device_get_sysname(dev);
    LOG(INFO) << "   Syspath: " << udev_device_get_syspath(dev);
    LOG(INFO) << "   Properties: ";
    udev_list_entry *property_list, *property_list_entry;
    property_list = udev_device_get_properties_list_entry(dev);
    udev_list_entry_foreach(property_list_entry, property_list) {
      const char* key = udev_list_entry_get_name(property_list_entry);
      const char* value = udev_list_entry_get_value(property_list_entry);
      LOG(INFO) << "      " << key << " = " << value;
    }

    bool continue_enumeration = callback.Run(dev);
    udev_device_unref(dev);
    if (!continue_enumeration)
      break;
  }
  udev_enumerate_unref(enumerate);
}

void DiskManager::ProcessBlockDeviceEvents(udev_device* dev,
                                           const char* action,
                                           DeviceEventList* events) {
  UdevDevice device(dev);
  if (device.IsIgnored())
    return;

  bool disk_added = false;
  bool disk_removed = false;
  bool child_disk_removed = false;
  if (strcmp(action, kUdevAddAction) == 0) {
    disk_added = true;
  } else if (strcmp(action, kUdevRemoveAction) == 0) {
    disk_removed = true;
  } else if (strcmp(action, kUdevChangeAction) == 0) {
    // For removable devices like CD-ROM, an eject request event
    // is treated as disk removal, while a media change event with
    // media available is treated as disk insertion.
    if (device.IsPropertyTrue(kPropertyDiskEjectRequest)) {
      disk_removed = true;
    } else if (device.IsPropertyTrue(kPropertyDiskMediaChange)) {
      if (device.IsMediaAvailable()) {
        disk_added = true;
      } else {
        child_disk_removed = true;
      }
    }
  }

  string device_path = device.NativePath();
  if (disk_added) {
    if (device.IsAutoMountable()) {
      if (base::ContainsKey(disks_detected_, device_path)) {
        // Disk already exists, so remove it and then add it again.
        events->push_back(DeviceEvent(DeviceEvent::kDiskRemoved, device_path));
      } else {
        disks_detected_[device_path] = set<string>();

        // Add the disk as a child of its parent if the parent is already
        // added to |disks_detected_|.
        udev_device* parent = udev_device_get_parent(dev);
        if (parent) {
          string parent_device_path = UdevDevice(parent).NativePath();
          if (base::ContainsKey(disks_detected_, parent_device_path)) {
            disks_detected_[parent_device_path].insert(device_path);
          }
        }
      }
      events->push_back(DeviceEvent(DeviceEvent::kDiskAdded, device_path));
    }
  } else if (disk_removed) {
    disks_detected_.erase(device_path);
    events->push_back(DeviceEvent(DeviceEvent::kDiskRemoved, device_path));
  } else if (child_disk_removed) {
    bool no_child_disks_found = true;
    if (base::ContainsKey(disks_detected_, device_path)) {
      set<string>& child_disks = disks_detected_[device_path];
      no_child_disks_found = child_disks.empty();
      for (const auto& child_disk : child_disks) {
        events->push_back(DeviceEvent(DeviceEvent::kDiskRemoved, child_disk));
      }
    }
    // When the device contains a full-disk partition, there are no child disks.
    // Remove the device instead.
    if (no_child_disks_found)
      events->push_back(DeviceEvent(DeviceEvent::kDiskRemoved, device_path));
  }
}

void DiskManager::ProcessMmcOrScsiDeviceEvents(udev_device* dev,
                                               const char* action,
                                               DeviceEventList* events) {
  UdevDevice device(dev);
  if (device.IsMobileBroadbandDevice())
    return;

  string device_path = device.NativePath();
  if (strcmp(action, kUdevAddAction) == 0) {
    if (base::ContainsKey(devices_detected_, device_path)) {
      events->push_back(DeviceEvent(DeviceEvent::kDeviceScanned, device_path));
    } else {
      devices_detected_.insert(device_path);
      events->push_back(DeviceEvent(DeviceEvent::kDeviceAdded, device_path));
    }
  } else if (strcmp(action, kUdevRemoveAction) == 0) {
    if (base::ContainsKey(devices_detected_, device_path)) {
      devices_detected_.erase(device_path);
      events->push_back(DeviceEvent(DeviceEvent::kDeviceRemoved, device_path));
    }
  }
}

bool DiskManager::GetDeviceEvents(DeviceEventList* events) {
  CHECK(events) << "Invalid device event list";

  udev_device* dev = udev_monitor_receive_device(udev_monitor_);
  if (!dev) {
    LOG(WARNING) << "Ignore device event with no associated udev device.";
    return false;
  }

  LOG(INFO) << "Got Device";
  LOG(INFO) << "   Syspath: " << udev_device_get_syspath(dev);
  LOG(INFO) << "   Node: " << udev_device_get_devnode(dev);
  LOG(INFO) << "   Subsystem: " << udev_device_get_subsystem(dev);
  LOG(INFO) << "   Devtype: " << udev_device_get_devtype(dev);
  LOG(INFO) << "   Action: " << udev_device_get_action(dev);

  const char* sys_path = udev_device_get_syspath(dev);
  const char* subsystem = udev_device_get_subsystem(dev);
  const char* action = udev_device_get_action(dev);
  if (!sys_path || !subsystem || !action) {
    udev_device_unref(dev);
    return false;
  }

  // |udev_monitor_| only monitors block, mmc, and scsi device changes, so
  // subsystem is either "block", "mmc", or "scsi".
  if (strcmp(subsystem, kBlockSubsystem) == 0) {
    ProcessBlockDeviceEvents(dev, action, events);
  } else {
    // strcmp(subsystem, kMmcSubsystem) == 0 ||
    // strcmp(subsystem, kScsiSubsystem) == 0
    ProcessMmcOrScsiDeviceEvents(dev, action, events);
  }

  udev_device_unref(dev);
  return true;
}

bool DiskManager::GetDiskByDevicePath(const string& device_path,
                                      Disk* disk) const {
  if (device_path.empty())
    return false;

  bool disk_found = false;
  EnumerateBlockDevices(base::Bind(&MatchDiskByPath, device_path,
                                   base::Unretained(&disk_found),
                                   base::Unretained(disk)));
  return disk_found;
}

const Filesystem* DiskManager::GetFilesystem(
    const string& filesystem_type) const {
  map<string, Filesystem>::const_iterator filesystem_iterator =
      filesystems_.find(filesystem_type);
  if (filesystem_iterator == filesystems_.end())
    return nullptr;

  return &filesystem_iterator->second;
}

void DiskManager::RegisterDefaultFilesystems() {
  // TODO(benchan): Perhaps these settings can be read from a config file.
  Filesystem vfat_fs("vfat");
  vfat_fs.accepts_user_and_group_id = true;
  vfat_fs.extra_mount_options = {MountOptions::kOptionDirSync,
                                 MountOptions::kOptionFlush, "shortname=mixed",
                                 MountOptions::kOptionUtf8};
  RegisterFilesystem(vfat_fs);

  Filesystem exfat_fs("exfat");
  exfat_fs.mounter_type = ExFATMounter::kMounterType;
  exfat_fs.accepts_user_and_group_id = true;
  exfat_fs.extra_mount_options = {MountOptions::kOptionDirSync};
  RegisterFilesystem(exfat_fs);

  Filesystem ntfs_fs("ntfs");
  ntfs_fs.mounter_type = NTFSMounter::kMounterType;
  ntfs_fs.accepts_user_and_group_id = true;
  ntfs_fs.extra_mount_options = {MountOptions::kOptionDirSync};
  RegisterFilesystem(ntfs_fs);

  Filesystem hfsplus_fs("hfsplus");
  hfsplus_fs.accepts_user_and_group_id = true;
  hfsplus_fs.extra_mount_options = {MountOptions::kOptionDirSync};
  RegisterFilesystem(hfsplus_fs);

  Filesystem iso9660_fs("iso9660");
  iso9660_fs.is_mounted_read_only = true;
  iso9660_fs.accepts_user_and_group_id = true;
  iso9660_fs.extra_mount_options = {MountOptions::kOptionUtf8};
  RegisterFilesystem(iso9660_fs);

  Filesystem udf_fs("udf");
  udf_fs.is_mounted_read_only = true;
  udf_fs.accepts_user_and_group_id = true;
  udf_fs.extra_mount_options = {MountOptions::kOptionUtf8};
  RegisterFilesystem(udf_fs);

  Filesystem ext2_fs("ext2");
  ext2_fs.extra_mount_options = {MountOptions::kOptionDirSync};
  RegisterFilesystem(ext2_fs);

  Filesystem ext3_fs("ext3");
  ext3_fs.extra_mount_options = {MountOptions::kOptionDirSync};
  RegisterFilesystem(ext3_fs);

  Filesystem ext4_fs("ext4");
  ext4_fs.extra_mount_options = {MountOptions::kOptionDirSync};
  RegisterFilesystem(ext4_fs);
}

void DiskManager::RegisterFilesystem(const Filesystem& filesystem) {
  filesystems_.emplace(filesystem.type, filesystem);
}

unique_ptr<Mounter> DiskManager::CreateMounter(
    const Disk& disk,
    const Filesystem& filesystem,
    const string& target_path,
    const vector<string>& options) const {
  const vector<string>& extra_options = filesystem.extra_mount_options;
  vector<string> extended_options;
  extended_options.reserve(options.size() + extra_options.size());
  extended_options.assign(options.begin(), options.end());
  extended_options.insert(extended_options.end(), extra_options.begin(),
                          extra_options.end());

  if (filesystem.type == "vfat") {
    // FAT32 stores times as local time instead of UTC. By default, the vfat
    // kernel module will use the kernel's time zone, which is set using
    // settimeofday(), to interpret time stamps as local time. However, time
    // zones are complicated and generally a user-space concern in modern Linux.
    // The man page for {get,set}timeofday comments that the |timezone| fields
    // of these functions is obsolete. Chrome OS doesn't appear to set these
    // either. Instead, we pass the time offset explicitly as a mount option so
    // that the user can see file time stamps as local time. This mirrors what
    // the user will see in other operating systems.
    // TODO(amistry): Consider moving this code into a FATMounter.
    time_t now = base::Time::Now().ToTimeT();
    struct tm timestruct;
    // The time zone might have changed since cros-disks was started. Force a
    // re-read of the time zone to ensure the local time is what the user
    // expects.
    tzset();
    localtime_r(&now, &timestruct);
    // tm_gmtoff is a glibc extension.
    int64_t offset_minutes = static_cast<int64_t>(timestruct.tm_gmtoff) / 60;
    // Clamp offset to +-12 hours.
    // Kernel versions <4.5 (commit a513d86983164a1f74a226ab7006deffbf63907e)
    // rejects any offset outside +-12 hours, resulting in an "Invalid Argument"
    // failure when attempting to mount the file system. As a temporary
    // workaround, clamp the offset. This will cause users in those time zones
    // to see invalid file time stamps on FAT32 paritions.
    // TODO(crbug.com/916643): Remove this when the kernel commit
    // a513d86983164a1f74a226ab7006deffbf63907e is back ported to all currently
    // used kernels.
    offset_minutes =
        std::max<int64_t>(-12 * 60, std::min<int64_t>(12 * 60, offset_minutes));
    extended_options.push_back(
        base::StringPrintf("time_offset=%" PRId64, offset_minutes));
  }

  string default_user_id, default_group_id;
  if (filesystem.accepts_user_and_group_id) {
    default_user_id = base::StringPrintf("%d", platform()->mount_user_id());
    default_group_id = base::StringPrintf("%d", platform()->mount_group_id());
  }

  MountOptions mount_options;
  mount_options.WhitelistOption(MountOptions::kOptionNoSymFollow);
  mount_options.Initialize(extended_options,
                           filesystem.accepts_user_and_group_id,
                           default_user_id, default_group_id);

  if (filesystem.is_mounted_read_only || disk.is_read_only ||
      disk.IsOpticalDisk()) {
    mount_options.SetReadOnlyOption();
  }

  if (filesystem.mounter_type == SystemMounter::kMounterType)
    return std::make_unique<SystemMounter>(disk.device_file, target_path,
                                           filesystem.mount_type, mount_options,
                                           platform());

  if (filesystem.mounter_type == ExFATMounter::kMounterType)
    return std::make_unique<ExFATMounter>(disk.device_file, target_path,
                                          filesystem.mount_type, mount_options,
                                          platform());

  if (filesystem.mounter_type == NTFSMounter::kMounterType)
    return std::make_unique<NTFSMounter>(disk.device_file, target_path,
                                         filesystem.mount_type, mount_options,
                                         platform());

  LOG(FATAL) << "Invalid mounter type '" << filesystem.mounter_type << "'";
  return nullptr;
}

bool DiskManager::CanMount(const string& source_path) const {
  // The following paths can be mounted:
  //     /sys/...
  //     /devices/...
  //     /dev/...
  return base::StartsWith(source_path, "/sys/", base::CompareCase::SENSITIVE) ||
         base::StartsWith(source_path, "/devices/",
                          base::CompareCase::SENSITIVE) ||
         base::StartsWith(source_path, "/dev/", base::CompareCase::SENSITIVE);
}

MountErrorType DiskManager::DoMount(const string& source_path,
                                    const string& filesystem_type,
                                    const vector<string>& options,
                                    const string& mount_path,
                                    MountOptions* applied_options) {
  CHECK(!source_path.empty()) << "Invalid source path argument";
  CHECK(!mount_path.empty()) << "Invalid mount path argument";

  Disk disk;
  if (!GetDiskByDevicePath(source_path, &disk)) {
    LOG(ERROR) << "'" << source_path << "' is not a valid device.";
    return MOUNT_ERROR_INVALID_DEVICE_PATH;
  }

  if (disk.is_on_boot_device) {
    LOG(ERROR) << "'" << source_path
               << "' is on boot device and not allowed to mount.";
    return MOUNT_ERROR_INVALID_DEVICE_PATH;
  }

  if (disk.device_file.empty()) {
    LOG(ERROR) << "'" << source_path << "' does not have a device file";
    return MOUNT_ERROR_INVALID_DEVICE_PATH;
  }

  string device_filesystem_type =
      filesystem_type.empty() ? disk.filesystem_type : filesystem_type;
  metrics()->RecordDeviceMediaType(disk.media_type);
  metrics()->RecordFilesystemType(device_filesystem_type);
  if (device_filesystem_type.empty()) {
    LOG(ERROR) << "Failed to determine the file system type of device '"
               << source_path << "'";
    return MOUNT_ERROR_UNKNOWN_FILESYSTEM;
  }

  const Filesystem* filesystem = GetFilesystem(device_filesystem_type);
  if (filesystem == nullptr) {
    LOG(ERROR) << "File system type '" << device_filesystem_type
               << "' on device '" << source_path << "' is not supported";
    return MOUNT_ERROR_UNSUPPORTED_FILESYSTEM;
  }

  unique_ptr<Mounter> mounter(
      CreateMounter(disk, *filesystem, mount_path, options));
  CHECK(mounter) << "Failed to create a mounter";

  MountErrorType error_type = mounter->Mount();
  if (error_type != MOUNT_ERROR_NONE) {
    // Try to mount the filesystem read-only if mounting it read-write failed.
    if (!mounter->mount_options().IsReadOnlyOptionSet()) {
      LOG(INFO) << "Trying to mount '" << source_path << "' read-only";
      vector<string> ro_options = options;
      ro_options.push_back("ro");
      mounter = CreateMounter(disk, *filesystem, mount_path, ro_options);
      CHECK(mounter) << "Failed to create a 'ro' mounter";
      error_type = mounter->Mount();
    }
  }

  if (error_type == MOUNT_ERROR_NONE) {
    ScheduleEjectOnUnmount(mount_path, disk);
  }
  *applied_options = mounter->mount_options();

  return error_type;
}

MountErrorType DiskManager::DoUnmount(const string& path,
                                      const vector<string>& options) {
  CHECK(!path.empty()) << "Invalid path argument";

  int unmount_flags;
  if (!ExtractUnmountOptions(options, &unmount_flags)) {
    LOG(ERROR) << "Invalid unmount options";
    return MOUNT_ERROR_INVALID_UNMOUNT_OPTIONS;
  }

  // The USB or SD drive on some system may be powered off after the system
  // goes into the S3 suspend state. To avoid leaving a mount point in a stale
  // state while its associated physical drive is gone, the cros-disks client
  // on the Chrome side unmounts all the mount points it manages before the
  // system goes into suspend. However, an ongoing filesystem access may keep a
  // mount point busy, which is beyond the control of Chrome or cros-disks. We
  // used to force umount the mount points but that has become undesirable. For
  // instance, when force unmounting a mount point backed by a FUSE process,
  // umount2() reports an error and the mount point is left in a half-broken
  // state. Lazy unmount is preferred over force unmount under such condition
  // (although it still doesn't necessarily guarantee a clean unmount if the
  // filesystem access doesn't finish before the USB or SD drive is powered
  // off). To better handle this kind of situation, we first try performing a
  // normal unmount. If that fails with errno == EBUSY, we retry with a lazy
  // unmount before giving up and reporting an error.
  bool unmount_failed = (umount2(path.c_str(), unmount_flags) != 0);
  if (unmount_failed && errno == EBUSY) {
    LOG(ERROR) << "Failed to unmount '" << path
               << "' as it is busy; retry with lazy unmount";
    unmount_flags |= MNT_DETACH;
    unmount_failed = (umount2(path.c_str(), unmount_flags) != 0);
  }
  if (unmount_failed) {
    PLOG(ERROR) << "Failed to unmount '" << path << "'";
    // TODO(benchan): Extract error from low-level unmount operation.
    return MOUNT_ERROR_UNKNOWN;
  }

  EjectDeviceOfMountPath(path);

  return MOUNT_ERROR_NONE;
}

string DiskManager::SuggestMountPath(const string& source_path) const {
  Disk disk;
  GetDiskByDevicePath(source_path, &disk);
  // If GetDiskByDevicePath fails, disk.GetPresentationName() returns
  // the fallback presentation name.
  return string(mount_root()) + "/" + disk.GetPresentationName();
}

bool DiskManager::ShouldReserveMountPathOnError(
    MountErrorType error_type) const {
  return error_type == MOUNT_ERROR_UNKNOWN_FILESYSTEM ||
         error_type == MOUNT_ERROR_UNSUPPORTED_FILESYSTEM;
}

bool DiskManager::ScheduleEjectOnUnmount(const string& mount_path,
                                         const Disk& disk) {
  if (!disk.IsOpticalDisk())
    return false;

  devices_to_eject_on_unmount_[mount_path] = disk.device_file;
  return true;
}

bool DiskManager::EjectDeviceOfMountPath(const string& mount_path) {
  map<string, string>::iterator device_iterator =
      devices_to_eject_on_unmount_.find(mount_path);
  if (device_iterator == devices_to_eject_on_unmount_.end())
    return false;

  string device_file = device_iterator->second;
  devices_to_eject_on_unmount_.erase(device_iterator);

  if (!eject_device_on_unmount_)
    return false;

  LOG(INFO) << "Eject device '" << device_file << "'.";
  if (!device_ejector_->Eject(device_file)) {
    LOG(WARNING) << "Failed to eject media from optical device '" << device_file
                 << "'.";
    return false;
  }

  return true;
}

bool DiskManager::UnmountAll() {
  // UnmountAll() is called when a user session ends. We do not want to eject
  // devices in that situation and thus set |eject_device_on_unmount_| to
  // false temporarily to prevent devices from being ejected upon unmount.
  eject_device_on_unmount_ = false;
  bool all_unmounted = MountManager::UnmountAll();
  eject_device_on_unmount_ = true;
  return all_unmounted;
}

}  // namespace cros_disks
