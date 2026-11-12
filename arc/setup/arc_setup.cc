// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/setup/arc_setup.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/environment.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/cryptohome.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/file_utils.h>
#include <brillo/files/file_util.h>
#include <brillo/files/safe_fd.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <crypto/random.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <metrics/metrics_library.h>
#include <user_data_auth-client/user_data_auth/dbus-proxies.h>

#include "arc/setup/arc_property_util.h"
#include "arc/setup/xml/android_xml_util.h"

#define EXIT_IF(f)                            \
  do {                                        \
    LOG(INFO) << "Running " << (#f) << "..."; \
    CHECK(!(f));                              \
  } while (false)

// TODO(yusukes): use android_filesystem_config.h.
#define AID_ROOT 0        /* traditional unix root user */
#define AID_SYSTEM 1000   /* system server */
#define AID_MEDIA_RW 1023 /* internal media storage write access */

namespace arc {

namespace {

// Lexicographically sorted. Usually you don't have to use these constants
// directly. Prefer base::FilePath variables in ArcPaths instead.
constexpr char kAndroidGeneratedPropertiesDirectory[] =
    "/run/arc/host_generated";
constexpr char kArcVmPerBoardConfigPath[] = "/run/arcvm/host_generated/oem";
constexpr char kArcVmVendorImagePath[] =
    "/opt/google/vms/android/vendor.raw.img";
constexpr char kAudioCodecsFilesDirectoryRelative[] = "etc/";
constexpr char kBuildPropFileVm[] = "/usr/share/arcvm/properties/build.prop";
constexpr char kCameraProfileDir[] = "/var/cache/camera";
constexpr char kCameraTestConfig[] = "/var/cache/camera/test_config.json";
constexpr char kCombinedPropFileVm[] =
    "/run/arcvm/host_generated/combined.prop";
constexpr char kMediaCodecsRelative[] = "etc/media_codecs_c2.xml";
constexpr char kMediaCodecsPerformanceRelative[] =
    "etc/media_codecs_performance_c2.xml";
constexpr char kMediaProfileFile[] = "media_profiles.xml";
constexpr char kPlatformXmlFileRelative[] = "etc/permissions/platform.xml";

constexpr const char kPropertyFilesPathVm[] = "/usr/share/arcvm/properties";
constexpr const char kPropertyFilesPath[] = "/usr/share/arc/properties";
constexpr const char kGeneratedPropertyFilesPathVm[] =
    "/run/arcvm/host_generated";
constexpr const char kGeneratedPropertyFilesPath[] = "/run/arc/host_generated";

// These are board-specific configuration settings, which are managed through
// the chromeos-config architecture.
// For details, see:
// https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/#arc
//
// Board-specific config files are automatically managed/generated via project
// config repos. For details, see:
// https://chromium.googlesource.com/chromiumos/config/
// For an example, see:
// https://chromium.googlesource.com/chromiumos/config/+/HEAD/test/project/fake/fake/sw_build_config/platform/chromeos-config/generated/arc/
constexpr char kAudioCodecsFilesSetting[] = "/arc/audio-codecs-files";
constexpr char kHardwareFeaturesSetting[] = "/arc/hardware-features";
constexpr char kMediaProfilesSetting[] = "/arc/media-profiles";
constexpr char kMediaCodecsSetting[] = "/arc/media-codecs";
constexpr char kMediaCodecsPerformanceSetting[] =
    "/arc/media-codecs-performance";
constexpr char kSystemPath[] = "system-path";

constexpr uid_t kHostArcCameraUid = 603;
constexpr gid_t kHostArcCameraGid = 603;

constexpr uid_t kShiftUid = 655360;
constexpr gid_t kShiftGid = 655360;
constexpr uid_t kRootUid = AID_ROOT + kShiftUid;
constexpr gid_t kRootGid = AID_ROOT + kShiftGid;
constexpr uid_t kSystemUid = AID_SYSTEM + kShiftUid;
constexpr gid_t kSystemGid = AID_SYSTEM + kShiftGid;
constexpr uid_t kMediaUid = AID_MEDIA_RW + kShiftUid;
constexpr gid_t kMediaGid = AID_MEDIA_RW + kShiftGid;

// Time to wait for a ResetApplicationContainerReply from DBus.
// The value is taken from kDefaultTimeoutMs in cryptohome/cryptohome.cc.
constexpr int kResetLvmDbusTimeoutMs = 300000;

// Property name for fingerprint.
constexpr char kFingerprintProp[] = "ro.build.fingerprint";

// Returns SDK version upgrade type to be sent to UMA.
ArcSdkVersionUpgradeType GetUpgradeType(AndroidSdkVersion system_sdk_version,
                                        AndroidSdkVersion data_sdk_version) {
  if (data_sdk_version == AndroidSdkVersion::UNKNOWN ||  // First boot
      data_sdk_version == system_sdk_version) {
    return ArcSdkVersionUpgradeType::NO_UPGRADE;
  }
  if (data_sdk_version == AndroidSdkVersion::ANDROID_N_MR1 &&
      system_sdk_version == AndroidSdkVersion::ANDROID_R) {
    return ArcSdkVersionUpgradeType::N_TO_R;
  }
  if (data_sdk_version == AndroidSdkVersion::ANDROID_P &&
      system_sdk_version == AndroidSdkVersion::ANDROID_R) {
    return ArcSdkVersionUpgradeType::P_TO_R;
  }
  if (data_sdk_version == AndroidSdkVersion::ANDROID_P &&
      system_sdk_version == AndroidSdkVersion::ANDROID_TIRAMISU) {
    return ArcSdkVersionUpgradeType::P_TO_T;
  }
  if (data_sdk_version == AndroidSdkVersion::ANDROID_R &&
      system_sdk_version == AndroidSdkVersion::ANDROID_TIRAMISU) {
    return ArcSdkVersionUpgradeType::R_TO_T;
  }
  if (data_sdk_version < system_sdk_version) {
    LOG(ERROR) << "Unexpected Upgrade: data_sdk_version="
               << static_cast<int>(data_sdk_version) << " system_sdk_version="
               << static_cast<int>(system_sdk_version);
    return ArcSdkVersionUpgradeType::UNKNOWN_UPGRADE;
  }
  LOG(ERROR) << "Unexpected Downgrade: data_sdk_version="
             << static_cast<int>(data_sdk_version)
             << " system_sdk_version=" << static_cast<int>(system_sdk_version);
  return ArcSdkVersionUpgradeType::UNKNOWN_DOWNGRADE;
}

bool IsChromeOSUserAvailable(Mode mode) {
  switch (mode) {
    case Mode::PREPARE_ARCVM_DATA:
    case Mode::REMOVE_DATA:
    case Mode::REMOVE_STALE_DATA:
      return true;
    case Mode::PREPARE_HOST_GENERATED_DIR:
    case Mode::APPLY_PER_BOARD_CONFIG:
      return false;
  }
}

std::optional<base::FilePath> GetConfigPath(brillo::CrosConfigInterface& config,
                                            const std::string& path) {
  std::string value;
  if (!config.GetString(path, kSystemPath, &value)) {
    return std::nullopt;
  }
  return base::FilePath(value);
}

void RemoveStaleDataDirectory(brillo::SafeFD& root_fd,
                              const base::FilePath& path) {
  // To protect itself, base::SafeFD::RmDir() uses a default maximum
  // recursion depth of 256. In this case, we are deleting the user's
  // arbitrary filesystem and want to be more generous. However, RmDir()
  // uses one fd per path level when recursing so we will have the max
  // number of fds per process as an upper bound (default 1024). Leave a
  // 25% buffer below this default 1024 limit to give lots of room for
  // incidental usage elsewhere in the process. Use this everywhere here
  // for consistency.
  constexpr int kRmdirMaxDepth = 768;

  brillo::SafeFD::SafeFDResult parent_dir =
      root_fd.OpenExistingDir(path.DirName());
  if (brillo::SafeFD::IsError(parent_dir.second)) {
    if (parent_dir.second != brillo::SafeFD::Error::kDoesNotExist) {
      LOG(ERROR) << "Errors while claeaning old data from " << path
                 << ": failed to open the parent directory";
    }
    return;
  }

  brillo::SafeFD::Error err =
      parent_dir.first.Rmdir(path.BaseName().value(), true /*recursive*/,
                             kRmdirMaxDepth, true /*keep_going*/);
  if (brillo::SafeFD::IsError(err) &&
      err != brillo::SafeFD::Error::kDoesNotExist) {
    LOG(ERROR) << "Errors while cleaning old data from " << path
               << ": failed to remove the directory";
  }
}

}  // namespace

// A struct that holds all the FilePaths ArcSetup uses.
struct ArcPaths {
  static std::unique_ptr<ArcPaths> Create(Mode mode, const Config& config) {
    base::FilePath root_path;
    base::FilePath user_path;
    base::FilePath android_data;
    base::FilePath android_data_old;

    if (IsChromeOSUserAvailable(mode)) {
      brillo::cryptohome::home::Username chromeos_user(
          config.GetStringOrDie("CHROMEOS_USER"));
      root_path = brillo::cryptohome::home::GetRootPath(chromeos_user);
      user_path = brillo::cryptohome::home::GetUserPath(chromeos_user);

      // Ensure the root directory and the user directory exist.
      EXIT_IF(root_path.empty() || !base::DirectoryExists(root_path));
      EXIT_IF(user_path.empty() || !base::DirectoryExists(user_path));

      android_data = root_path.Append("android-data");
      android_data_old = root_path.Append("android-data-old");
    }
    return base::WrapUnique(
        new ArcPaths(root_path, user_path, android_data, android_data_old));
  }

  // Lexicographically sorted.
  const base::FilePath android_generated_properties_directory{
      kAndroidGeneratedPropertiesDirectory};
  const base::FilePath audio_codecs_files_directory_relative{
      kAudioCodecsFilesDirectoryRelative};
  const base::FilePath camera_profile_dir{kCameraProfileDir};
  const base::FilePath camera_test_config{kCameraTestConfig};
  const base::FilePath media_codecs_relative{kMediaCodecsRelative};
  const base::FilePath media_codecs_performance_relative{
      kMediaCodecsPerformanceRelative};
  const base::FilePath media_profile_file{kMediaProfileFile};
  const base::FilePath platform_xml_file_relative{kPlatformXmlFileRelative};

  const base::FilePath root_directory;
  const base::FilePath user_directory;
  const base::FilePath android_data_directory;
  const base::FilePath android_data_old_directory;

 private:
  ArcPaths(const base::FilePath& root_directory,
           const base::FilePath& user_directory,
           const base::FilePath& android_data_directory,
           const base::FilePath& android_data_old_directory)
      : root_directory(root_directory),
        user_directory(user_directory),
        android_data_directory(android_data_directory),
        android_data_old_directory(android_data_old_directory) {}
  ArcPaths(const ArcPaths&) = delete;
  ArcPaths& operator=(const ArcPaths&) = delete;
};

ArcSetup::ArcSetup(Mode mode, const base::FilePath& config_json)
    : mode_(mode),
      config_(config_json, base::Environment::Create()),
      arcvm_data_type_(ArcVmDataType::kUndefined),
      arc_mounter_(GetDefaultMounter()),
      arc_paths_(ArcPaths::Create(mode_, config_)),
      arc_setup_metrics_(std::make_unique<ArcSetupMetrics>()) {
  CHECK(mode == Mode::APPLY_PER_BOARD_CONFIG || mode == Mode::REMOVE_DATA ||
        mode == Mode::REMOVE_STALE_DATA || !config_json.empty());
}

ArcSetup::ArcSetup(Mode mode, const ArcVmDataType arcvm_data_type)
    : mode_(mode),
      config_(base::FilePath(), base::Environment::Create()),
      arcvm_data_type_(arcvm_data_type),
      arc_mounter_(GetDefaultMounter()),
      arc_paths_(ArcPaths::Create(mode_, config_)),
      arc_setup_metrics_(std::make_unique<ArcSetupMetrics>()) {
  CHECK(mode == Mode::PREPARE_ARCVM_DATA);
  CHECK(arcvm_data_type > ArcVmDataType::kUndefined &&
        arcvm_data_type <= ArcVmDataType::kMaxValue)
      << "Invalid arcvm_data_type: " << static_cast<int>(arcvm_data_type);
}

ArcSetup::~ArcSetup() = default;

void ArcSetup::SetUpAndroidData(const base::FilePath& bind_target) {
  mode_t android_data_mode = 0700;
  gid_t android_data_gid = kRootGid;
  if (USE_ARCVM) {
    // When ARCVM is enabled on the board, allow vm_concierge to access the
    // directory. Note that vm_concierge runs as ugid crosvm in minijail.
    uid_t dummy_uid;
    EXIT_IF(!GetUserId("crosvm", &dummy_uid, &android_data_gid));
    android_data_mode = 0750;
  }
  EXIT_IF(!InstallDirectory(android_data_mode, kRootUid, android_data_gid,
                            arc_paths_->android_data_directory));

  // match android/system/core/rootdir/init.rc
  EXIT_IF(!InstallDirectory(0771, kSystemUid, kSystemGid,
                            arc_paths_->android_data_directory.Append("data")));

  if (USE_ARCVM) {
    // For ARCVM, create /data/media too since crosvm exports the directory via
    // virtio-fs.
    const base::FilePath android_data_media_directory =
        arc_paths_->android_data_directory.Append("data").Append("media");
    EXIT_IF(!InstallDirectory(0770, kMediaUid, kMediaGid,
                              android_data_media_directory));

    // Set up /data/media/0/Download with a strict permission so that users
    // cannot modify the directory before it is covered by Chrome OS Downloads.
    const base::FilePath android_data_media_root_for_user =
        android_data_media_directory.Append("0");
    const base::FilePath android_download_directory =
        android_data_media_root_for_user.Append("Download");
    EXIT_IF(!InstallDirectory(0770, kMediaUid, kMediaGid,
                              android_data_media_root_for_user));
    EXIT_IF(!InstallDirectory(0700, kRootUid, kRootGid,
                              android_download_directory));

    // Restore the contexts of /data/media directories. This is needed to ensure
    // Android's vold can mount Chrome OS Downloads on /data/media/0/Download.
    constexpr char kDataMediaSELinuxContext[] =
        "u:object_r:media_rw_data_file:s0";
    EXIT_IF(!Chcon(kDataMediaSELinuxContext, android_data_media_directory));
    EXIT_IF(!Chcon(kDataMediaSELinuxContext, android_data_media_root_for_user));
    EXIT_IF(!Chcon(kDataMediaSELinuxContext, android_download_directory));
  }

  // To make our bind-mount business easier, we first bind-mount the real
  // android-data directory to bind_target (usually $ANDROID_MUTABLE_SOURCE).
  // Then we do not need to pass the android-data path to other processes.
  EXIT_IF(!arc_mounter_->BindMount(arc_paths_->android_data_directory,
                                   bind_target));
}

void ArcSetup::ApplyPerBoardConfigurationsInternal(
    const base::FilePath& oem_mount_directory) {
  auto config = std::make_unique<brillo::CrosConfig>();

  base::FilePath media_profile_xml =
      base::FilePath(arc_paths_->camera_profile_dir)
          .Append(arc_paths_->media_profile_file);

  std::string media_profile_setting;
  if (auto media_profile_setting =
          GetConfigPath(*config, kMediaProfilesSetting)) {
    media_profile_xml = *media_profile_setting;
  } else {
    // TODO(chromium:1083652) Remove dynamic shell scripts once all overlays
    // are migrated to static XML config.
    const base::FilePath generate_camera_profile(
        "/usr/bin/generate_camera_profile");
    if (base::PathExists(generate_camera_profile)) {
      EXIT_IF(!LaunchAndWait({generate_camera_profile.value()}));
    }
  }

  if (base::PathExists(media_profile_xml)) {
    ManagedString content =
        FilterMediaProfile(media_profile_xml, arc_paths_->camera_test_config);

    if (content.value().size() > 0) {
      const base::FilePath new_media_profile_xml =
          base::FilePath(oem_mount_directory)
              .Append("etc")
              .Append(arc_paths_->media_profile_file);
      brillo::SafeFD dest_parent(
          brillo::SafeFD::Root()
              .first.OpenExistingDir(new_media_profile_xml.DirName())
              .first);
      (void)dest_parent.Unlink(new_media_profile_xml.BaseName().value());
      brillo::SafeFD dest_fd(dest_parent
                                 .MakeFile(new_media_profile_xml.BaseName(),
                                           0644 /*permissions*/,
                                           kHostArcCameraUid, kHostArcCameraGid)
                                 .first);
      EXIT_IF(!base::WriteFileDescriptor(dest_fd.get(), content.value()));
    }
  }
  base::FilePath hardware_features_xml =
      GetConfigPath(*config, kHardwareFeaturesSetting)
          .value_or(base::FilePath("/etc/hardware_features.xml"));
  if (!base::PathExists(hardware_features_xml)) {
    return;
  }

  const base::FilePath platform_xml_file =
      base::FilePath(oem_mount_directory)
          .Append(arc_paths_->platform_xml_file_relative);

  segmentation::FeatureManagement feature_management;
  ManagedString content =
      AppendFeatureManagement(hardware_features_xml, feature_management);

  brillo::SafeFD dest_parent(
      brillo::SafeFD::Root()
          .first.OpenExistingDir(platform_xml_file.DirName())
          .first);
  (void)dest_parent.Unlink(platform_xml_file.BaseName().value());
  brillo::SafeFD dest_fd(dest_parent
                             .MakeFile(platform_xml_file.BaseName(),
                                       0644 /*permissions*/, kRootUid, kRootGid)
                             .first);
  EXIT_IF(!base::WriteFileDescriptor(dest_fd.get(), content.value()));

  // TODO(chromium:1083652) Remove dynamic shell scripts once all overlays
  // are migrated to static XML config.
  const base::FilePath board_hardware_features(
      "/usr/sbin/board_hardware_features");
  if (!base::PathExists(board_hardware_features)) {
    return;
  }

  // The board_hardware_features is usually made by shell script and should
  // receive platform XML file argument in absolute path to avoid unexpected
  // environment issues.
  EXIT_IF(!LaunchAndWait(
      {board_hardware_features.value(), platform_xml_file.value()}));
}

void ArcSetup::SetUpCameraProperty(const base::FilePath& build_prop) {
  // Camera HAL V3 needs two properties from build.prop for picture taking.
  // Copy the information to /var/cache.
  const base::FilePath camera_prop_directory("/var/cache/camera");
  const base::FilePath camera_prop_file =
      base::FilePath(camera_prop_directory).Append("camera.prop");
  EXIT_IF(!brillo::MkdirRecursively(camera_prop_directory, 0755).is_valid());

  std::string content;
  EXIT_IF(!base::ReadFileToString(build_prop, &content));

  std::vector<std::string> properties = base::SplitString(
      content, "\n", base::WhitespaceHandling::TRIM_WHITESPACE,
      base::SplitResult::SPLIT_WANT_ALL);
  const std::string kSystemManufacturer = "ro.product.system.manufacturer=";
  const std::string kManufacturer = "ro.product.manufacturer=";
  const std::string kSystemModel = "ro.product.system.model=";
  const std::string kModel = "ro.product.model=";
  std::string camera_properties;
  for (const auto& property : properties) {
    if (!property.compare(0, kManufacturer.length(), kManufacturer) ||
        !property.compare(0, kModel.length(), kModel)) {
      // For Android P.
      camera_properties += property + "\n";
    } else if (!property.compare(0, kSystemManufacturer.length(),
                                 kSystemManufacturer)) {
      // Android Q+ only has |kSystemManufacturer| in /system/build.prop, and
      // |kSystemManufacturer| is copied to |kManufacturer| at boot time. Do
      // the same here.
      camera_properties +=
          kManufacturer + property.substr(kSystemManufacturer.length()) + "\n";
    } else if (!property.compare(0, kSystemModel.length(), kSystemModel)) {
      // Do the same for |kSystemModel| for Android Q+.
      camera_properties +=
          kModel + property.substr(kSystemModel.length()) + "\n";
    }
  }
  EXIT_IF(!WriteToFile(camera_prop_file, 0644, camera_properties));
}

AndroidSdkVersion ArcSetup::SdkVersionFromString(
    const std::string& version_str) {
  const std::string version_codename_str =
      GetSystemBuildPropertyOrDie("ro.build.version.codename");
  if (version_codename_str != "REL") {
    LOG(INFO) << "Not a release version; classifying as Android Development.";
    return AndroidSdkVersion::ANDROID_DEVELOPMENT;
  }
  int version;
  if (base::StringToInt(version_str, &version)) {
    switch (version) {
      case 23:
        return AndroidSdkVersion::ANDROID_M;
      case 25:
        return AndroidSdkVersion::ANDROID_N_MR1;
      case 28:
        return AndroidSdkVersion::ANDROID_P;
      case 30:
        return AndroidSdkVersion::ANDROID_R;
      case 31:
        return AndroidSdkVersion::ANDROID_S;
      case 32:
        return AndroidSdkVersion::ANDROID_S_V2;
      case 33:
        return AndroidSdkVersion::ANDROID_TIRAMISU;
      case 35:
        return AndroidSdkVersion::ANDROID_VANILLA_ICE_CREAM;
    }
  }

  LOG(ERROR) << "Unknown SDK version : " << version_str;
  return AndroidSdkVersion::UNKNOWN;
}

AndroidSdkVersion ArcSetup::GetSdkVersion() {
  const std::string version_str =
      GetSystemBuildPropertyOrDie("ro.build.version.sdk");
  LOG(INFO) << "SDK version string: " << version_str;

  const AndroidSdkVersion version = SdkVersionFromString(version_str);
  if (version == AndroidSdkVersion::UNKNOWN) {
    LOG(FATAL) << "Unknown SDK version: " << version_str;
  }
  if (version < AndroidSdkVersion::ANDROID_R) {
    LOG(FATAL) << "Unsupported SDK version: " << version_str;
  }
  return version;
}

// Note: This function has to be in sync with Android's arc-boot-type-detector.
// arc-boot-type-detector's main() function is very similar to this.
void ArcSetup::GetBootTypeAndDataSdkVersion(
    const base::FilePath& android_data_directory,
    ArcBootType* out_boot_type,
    AndroidSdkVersion* out_data_sdk_version) {
  const std::string system_fingerprint =
      GetSystemBuildPropertyOrDie(kFingerprintProp);

  // Note: The XML file name has to match com.android.server.pm.Settings's
  // mSettingsFilename. This will be very unlikely to change, but we still need
  // to be careful.
  const base::FilePath packages_xml =
      android_data_directory.Append("data/system/packages.xml");

  if (!base::PathExists(packages_xml)) {
    // This path is taken when /data is empty, which is not an error.
    LOG(INFO) << packages_xml.value()
              << " does not exist. This is the very first boot aka opt-in.";
    *out_boot_type = ArcBootType::FIRST_BOOT;
    *out_data_sdk_version = AndroidSdkVersion::UNKNOWN;
    return;
  }

  // Get a fingerprint from /data/system/packages.xml.
  std::string data_fingerprint;
  std::string data_sdk_version;
  if (!GetFingerprintAndSdkVersionFromPackagesXml(
          packages_xml, &data_fingerprint, &data_sdk_version)) {
    LOG(ERROR) << "Failed to get a fingerprint from " << packages_xml.value();
    // Return kFirstBootAfterUpdate so the caller invalidates art/oat files
    // which is safer than returning kRegularBoot.
    *out_boot_type = ArcBootType::FIRST_BOOT_AFTER_UPDATE;
    *out_data_sdk_version = AndroidSdkVersion::UNKNOWN;
    return;
  }

  // If two fingerprints don't match, this is the first boot after OTA.
  // Android's PackageManagerService.isUpgrade(), at least on M, N, and
  // O releases, does exactly the same to detect OTA.
  const bool ota_detected = system_fingerprint != data_fingerprint;
  if (!ota_detected) {
    LOG(INFO) << "This is regular boot.";
  } else {
    LOG(INFO) << "This is the first boot after OTA: system="
              << system_fingerprint << ", data=" << data_fingerprint;
  }
  LOG(INFO) << "Data SDK version: " << data_sdk_version;
  LOG(INFO) << "System SDK version: " << static_cast<int>(GetSdkVersion());
  *out_boot_type = ota_detected ? ArcBootType::FIRST_BOOT_AFTER_UPDATE
                                : ArcBootType::REGULAR_BOOT;
  *out_data_sdk_version = SdkVersionFromString(data_sdk_version);
}

AndroidSdkVersion ArcSetup::GetArcVmDataSdkVersion() {
  ArcBootType boot_type;
  AndroidSdkVersion data_sdk_version;

  if (arcvm_data_type_ == ArcVmDataType::kVirtiofs) {
    // Just read packages.xml from virtio-fs /data.
    GetBootTypeAndDataSdkVersion(arc_paths_->android_data_directory, &boot_type,
                                 &data_sdk_version);
    return data_sdk_version;
  }

  // Mount virtio-blk /data on a temporary directory.
  const base::FilePath data_device_path = GetArcVmDataDevicePath(
      arcvm_data_type_, config_.GetStringOrDie("CHROMEOS_USER"),
      arc_paths_->root_directory);
  CHECK(!data_device_path.empty());
  base::ScopedTempDir temp_android_data_dir;
  EXIT_IF(!temp_android_data_dir.CreateUniqueTempDir());
  const base::FilePath data_mount_path =
      temp_android_data_dir.GetPath().Append("data");
  EXIT_IF(!InstallDirectory(0700, kRootUid, kRootGid, data_mount_path));
  std::unique_ptr<ScopedMount> android_data_mount =
      ScopedMount::CreateScopedLoopMount(
          arc_mounter_.get(), data_device_path.value(), data_mount_path,
          LoopMountFilesystemType::kExt4,
          MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RDONLY);
  if (!android_data_mount) {
    // Mount can fail when /data has not been formatted yet. Return the unknown
    // value which includes the first boot after opt-in.
    LOG(INFO) << "Failed to mount " << data_device_path << " on "
              << data_mount_path << ". Assuming the first boot after opt-in";
    return AndroidSdkVersion::UNKNOWN;
  }
  LOG(INFO) << "Mounted " << data_device_path << " on " << data_mount_path;
  GetBootTypeAndDataSdkVersion(temp_android_data_dir.GetPath(), &boot_type,
                               &data_sdk_version);
  return data_sdk_version;
}

void ArcSetup::SendUpgradeMetrics(AndroidSdkVersion data_sdk_version) {
  LOG(INFO) << "Sending upgrade metrics";
  arc_setup_metrics_->SendSdkVersionUpgradeType(
      GetUpgradeType(GetSdkVersion(), data_sdk_version));
}

void ArcSetup::DeleteAndroidDataOnUpgrade(AndroidSdkVersion data_sdk_version) {
  if (!ShouldDeleteAndroidData(GetSdkVersion(), data_sdk_version)) {
    return;
  }

  LOG(INFO) << "Deleting old Android data";
  EXIT_IF(!MoveDirIntoDataOldDir(arc_paths_->android_data_directory,
                                 arc_paths_->android_data_old_directory));
}

void ArcSetup::DeleteAndroidMediaProviderDataOnUpgrade(
    AndroidSdkVersion data_sdk_version) {
  if (data_sdk_version != AndroidSdkVersion::ANDROID_P) {
    return;
  }
  LOG(INFO) << "Deleting old Android Media Provider data";
  const auto media_provider_data_directory =
      arc_paths_->android_data_directory.Append(
          "data/data/com.android.providers.media");
  EXIT_IF(!MoveDirIntoDataOldDir(media_provider_data_directory,
                                 arc_paths_->android_data_old_directory));
}

void ArcSetup::OnRemoveData() {
  // Since deleting files in android-data may take long, just move the directory
  // for now and let arc-stale-directory-remover delete files in the background.
  EXIT_IF(!MoveDirIntoDataOldDir(arc_paths_->android_data_directory,
                                 arc_paths_->android_data_old_directory));

  // Delete virtio-blk disk images if they exist.
  if (USE_ARCVM) {
    // /home/root/<hash>/crosvm/<arcvm>(.metadata).img are created by concierge.
    const base::FilePath image_dir =
        arc_paths_->root_directory.Append("crosvm");
    const std::string data_image_file =
        base::StringPrintf("%s.img", kArcvmEncodedName);
    const std::string metadata_image_file =
        base::StringPrintf("%s.metadata.img", kArcvmEncodedName);

    brillo::SafeFD fd;
    brillo::SafeFD::Error err;
    std::tie(fd, err) = brillo::SafeFD::Root().first.OpenExistingDir(image_dir);

    if (!brillo::SafeFD::IsError(err)) {
      for (const std::string& file : {data_image_file, metadata_image_file}) {
        base::ElapsedTimer timer;
        // No need to delete the image in the background because deleting a
        // single image file won't take more than 1 second.
        err = fd.Unlink(file);
        PCHECK(!brillo::SafeFD::IsError(err) ||
               // Abort if the |err| is not ENOENT.
               (err == brillo::SafeFD::Error::kIOError && errno == ENOENT))
            << "err=" << static_cast<int>(err);
        LOG_IF(INFO, !brillo::SafeFD::IsError(err))
            << "Deleting disk image (crosvm/" << file << ") took "
            << timer.Elapsed().InMillisecondsRoundedUp() << "ms";
      }
    } else {
      PLOG(ERROR) << "Failed to open the image directory: " << image_dir
                  << ", err=" << static_cast<int>(err);
    }
  }

  // Ensure to remove ARC /data in LVM stateful partition.
  if (USE_ARCVM && USE_LVM_STATEFUL_PARTITION) {
    RemoveDataInLvm();
  }
}

void ArcSetup::RemoveDataInLvm() {
  brillo::DBusConnection connection;
  scoped_refptr<dbus::Bus> bus = connection.Connect();
  if (!bus) {
    LOG(ERROR) << "Failed to connect to system D-Bus service";
    return;
  }

  auto userdataauth_proxy = org::chromium::UserDataAuthInterfaceProxy(bus);
  user_data_auth::ResetApplicationContainerRequest request;
  user_data_auth::ResetApplicationContainerReply reply;
  brillo::ErrorPtr err;

  request.set_application_name("arcvm");
  const std::string chromeos_user = config_.GetStringOrDie("CHROMEOS_USER");
  request.mutable_account_id()->set_account_id(chromeos_user);

  LOG(INFO) << "Attempting to remove ARC /data in LVM";
  if (!userdataauth_proxy.ResetApplicationContainer(request, &reply, &err,
                                                    kResetLvmDbusTimeoutMs) ||
      err) {
    std::string msg;
    if (err.get()) {
      msg = err->GetDomain() + "," + err->GetCode() + "," + err->GetMessage();
    }
    LOG(ERROR) << "ResetApplicationContainer call failed: " << msg;
    return;
  }
  if (reply.error() !=
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Failed to reset application container: " << reply.error();
    return;
  }
  LOG(INFO) << "Successfully removed ARC /data in LVM";
}

void ArcSetup::OnRemoveStaleData() {
  brillo::SafeFD root = brillo::SafeFD::Root().first;
  if (!root.is_valid()) {
    LOG(ERROR) << "Errors while cleaning old data: failed to open the root "
                  "directory";
    return;
  }

  if (USE_ARCVM) {
    // On ARCVM, stale *.odex files are kept in /data/vendor/arc.
    const base::FilePath arcvm_stale_odex_path =
        arc_paths_->android_data_directory.Append(
            "data/vendor/arc/old_arc_executables_pre_ota");
    RemoveStaleDataDirectory(root, arcvm_stale_odex_path);
  }

  // Moving data to android_data_old no longer has race conditions so it is safe
  // to delete the entire directory.
  RemoveStaleDataDirectory(root, arc_paths_->android_data_old_directory);
}

void ArcSetup::OnPrepareHostGeneratedDir() {
#if USE_ARC_HW_OEMCRYPTO
  const bool hw_oemcrypto_support = true;
#else
  const bool hw_oemcrypto_support = false;
#endif  // USE_ARC_HW_OEMCRYPTO
  const bool debuggable = config_.GetBoolOrDie("ANDROID_DEBUGGABLE");
  LOG(INFO) << "Debuggable is " << debuggable;

  const base::FilePath property_files_source_dir(
      base::FilePath(USE_ARCVM ? kPropertyFilesPathVm : kPropertyFilesPath));
  const base::FilePath property_files_dest_path(
      USE_ARCVM ? base::FilePath(kGeneratedPropertyFilesPathVm)
                      .Append("combined.prop")
                : base::FilePath(kGeneratedPropertyFilesPath));
  const base::FilePath modified_properties_dest_path(
      USE_ARCVM ? base::FilePath(kGeneratedPropertyFilesPathVm)
                      .Append("modified.prop")
                : base::FilePath(kGeneratedPropertyFilesPath));

  brillo::DBusConnection dbus_connection;
  scoped_refptr<::dbus::Bus> bus = nullptr;
  if (hw_oemcrypto_support) {
    bus = dbus_connection.Connect();
    CHECK(bus);
  }

  EXIT_IF(!ExpandPropertyFiles(property_files_source_dir,
                               property_files_dest_path,
                               modified_properties_dest_path,
                               /*single_file=*/USE_ARCVM, hw_oemcrypto_support,
                               /*include_soc_props=*/true, debuggable, bus));

  if (!USE_ARCVM) {
    return;
  }

  // Fix for b/516966155: Set a unique hostname to prevent mDNS conflicts.
  // Generates a random hostname ("android-<hex>") to prevent the "localhost"
  // mDNS cache flush loop.
  std::array<uint8_t, 4> rand_value;
  crypto::RandBytes(rand_value);
  const std::string hostname_prop =
      "net.hostname=android-" +
      base::ToLowerASCII(base::HexEncode(rand_value)) + "\n";
  EXIT_IF(!base::AppendToFile(property_files_dest_path, hostname_prop));
  EXIT_IF(!base::AppendToFile(modified_properties_dest_path, hostname_prop));

  // CACHE_PARTITION is set when a dedicated cache partition is used
  // (b/182953041). The set value is the device number to be used.
  // This option is for test build only, and is not used in production.
  const std::string cache_partition = config_.GetStringOrDie("CACHE_PARTITION");

  // For ARCVM, the first stage fstab file needs to be generated.
  EXIT_IF(!GenerateFirstStageFstab(
      base::FilePath(kGeneratedPropertyFilesPathVm).Append("fstab"),
      base::FilePath(kArcVmVendorImagePath), cache_partition));
}

void ArcSetup::OnApplyPerBoardConfig() {
  base::FilePath per_board_config_path(kArcVmPerBoardConfigPath);
  ApplyPerBoardConfigurationsInternal(per_board_config_path);
  SetUpCameraProperty(base::FilePath(kBuildPropFileVm));

  // ARCVM's platform.xml has to be owned by crosvm for proper ugid mapping by
  // crosvm.
  brillo::SafeFD fd;
  brillo::SafeFD::Error err;
  std::tie(fd, err) = brillo::SafeFD::Root().first.OpenExistingFile(
      per_board_config_path.Append(kPlatformXmlFileRelative));
  if (err == brillo::SafeFD::Error::kDoesNotExist) {
    return;  // the board does not have the file.
  }
  EXIT_IF(!fd.is_valid());

  uid_t crosvm_uid;
  gid_t crosvm_gid;
  EXIT_IF(!GetUserId("crosvm", &crosvm_uid, &crosvm_gid));
  EXIT_IF(fchown(fd.get(), crosvm_uid, crosvm_gid));

  auto config = std::make_unique<brillo::CrosConfig>();
  if (auto media_codecs_c2_xml = GetConfigPath(*config, kMediaCodecsSetting);
      media_codecs_c2_xml && base::PathExists(*media_codecs_c2_xml)) {
    EXIT_IF(!SafeCopyFile(*media_codecs_c2_xml, brillo::SafeFD::Root().first,
                          arc_paths_->media_codecs_relative,
                          brillo::SafeFD::Root()
                              .first.OpenExistingDir(per_board_config_path)
                              .first,
                          0644, crosvm_uid, crosvm_gid));
  }

  if (auto media_codecs_performance_c2_xml =
          GetConfigPath(*config, kMediaCodecsPerformanceSetting);
      media_codecs_performance_c2_xml &&
      base::PathExists(*media_codecs_performance_c2_xml)) {
    EXIT_IF(!SafeCopyFile(*media_codecs_performance_c2_xml,
                          brillo::SafeFD::Root().first,
                          arc_paths_->media_codecs_performance_relative,
                          brillo::SafeFD::Root()
                              .first.OpenExistingDir(per_board_config_path)
                              .first,
                          0644, crosvm_uid, crosvm_gid));
  }

  // Mount per-model ARC Audio codecs files.
  // Custom label tag must not exist to prevent misconfiguration when a model is
  // shared between multiple OEMs.
  std::string custom_label_tag;
  const bool custom_label_tag_exist =
      config->GetString("/identity", "custom-label-tag", &custom_label_tag);
  if (!custom_label_tag_exist || custom_label_tag.empty()) {
    // There may be multiple files, so loop through all of them.
    //
    // Example codecs files:
    // - Source: /etc/arc-audio-codecs-files/media_codecs_codec1.xml
    //   Dest: ${per_board_config_path}/etc/media_codecs_codec1.xml
    //   Dest inside ARC: /oem/etc/media_codecs_codec1.xml
    //
    // /oem/etc/media_codecs_codec1.xml will be bind mounted to /vendor/etc/
    for (int i = 0;; ++i) {
      const std::string config_path =
          base::StringPrintf("%s/%d", kAudioCodecsFilesSetting, i);
      std::string file_name;
      if (!config->GetString(config_path, "name", &file_name)) {
        break;
      }
      if (auto audio_codecs_file =
              GetConfigPath(*config, config_path + "/file");
          audio_codecs_file && base::PathExists(*audio_codecs_file)) {
        EXIT_IF(!SafeCopyFile(
            *audio_codecs_file, brillo::SafeFD::Root().first,
            arc_paths_->audio_codecs_files_directory_relative.Append(file_name),
            brillo::SafeFD::Root()
                .first.OpenExistingDir(per_board_config_path)
                .first,
            0644, crosvm_uid, crosvm_gid));
      }
    }
  }
}

void ArcSetup::OnPrepareArcVmData() {
  // Android's user data needs to be removed in certain upgrading scenarios.
  // Hence first check the data SDK version to decide the upgrade type, send
  // upgrade metrics, and remove /data if necessary.
  const AndroidSdkVersion data_sdk_version = GetArcVmDataSdkVersion();
  SendUpgradeMetrics(data_sdk_version);
  DeleteAndroidMediaProviderDataOnUpgrade(data_sdk_version);
  DeleteAndroidDataOnUpgrade(data_sdk_version);

  if (arcvm_data_type_ != ArcVmDataType::kVirtiofs) {
    // Skip setting up /home/root/<hash>/android-data when virtio-blk /data is
    // used.
    return;
  }
  const base::FilePath bind_target(
      config_.GetStringOrDie("ANDROID_MUTABLE_SOURCE"));
  // bind_target may be already bound if arcvm-prepare-data has previously run
  // during this session.
  EXIT_IF(!arc_mounter_->UmountIfExists(bind_target));
  // Create data folder and bind to bind_target. The bind mount will be cleaned
  // up in vm_concierge.conf's post-stop script, when the mnt_concierge
  // namespace is unmounted.
  SetUpAndroidData(bind_target);
}

std::string ArcSetup::GetSystemBuildPropertyOrDie(const std::string& name) {
  if (system_properties_.empty()) {
    // First time read of system properties file.
    // We don't know if we are in a container or on VM yet, so try the
    // build.prop location on container first and fall back to the
    // combined.prop location on VM if empty.
    const base::FilePath build_prop =
        arc_paths_->android_generated_properties_directory.Append("build.prop");
    GetPropertiesFromFile(build_prop, &system_properties_);
    if (system_properties_.empty()) {
      const base::FilePath combined_prop_vm(kCombinedPropFileVm);
      GetPropertiesFromFile(combined_prop_vm, &system_properties_);
    }
  }
  DCHECK(!system_properties_.empty());
  const auto it = system_properties_.find(name);
  CHECK(system_properties_.end() != it) << "Failed to read property: " << name;
  CHECK(!it->second.empty());
  return it->second;
}

void ArcSetup::Run() {
  switch (mode_) {
    case Mode::PREPARE_HOST_GENERATED_DIR:
      OnPrepareHostGeneratedDir();
      break;
    case Mode::APPLY_PER_BOARD_CONFIG:
      OnApplyPerBoardConfig();
      break;
    case Mode::PREPARE_ARCVM_DATA:
      OnPrepareArcVmData();
      break;
    case Mode::REMOVE_DATA:
      OnRemoveData();
      break;
    case Mode::REMOVE_STALE_DATA:
      OnRemoveStaleData();
      break;
  }
}

}  // namespace arc
