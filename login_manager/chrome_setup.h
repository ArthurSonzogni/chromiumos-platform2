// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_CHROME_SETUP_H_
#define LOGIN_MANAGER_CHROME_SETUP_H_

#include <sys/types.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/gtest_prod_util.h>

namespace brillo {
class CrosConfigInterface;
}  // namespace brillo

namespace chromeos {
namespace ui {
class ChromiumCommandBuilder;
}  // namespace ui
}  // namespace chromeos

namespace segmentation {
class FeatureManagement;
}  // namespace segmentation

namespace login_manager {

class ChromeSetupTest;

// Sets up environment, command line flag, and env vars etc. to run
// chromeos-chrome.
class ChromeSetup {
 public:
  ChromeSetup(brillo::CrosConfigInterface& cros_config,
              segmentation::FeatureManagement& feature_management);

  ChromeSetup(const ChromeSetup&) = delete;
  ChromeSetup& operator=(const ChromeSetup&) = delete;
  ~ChromeSetup();

  struct Result {
    // Command line arguments to launch chromeos-chrome.
    std::vector<std::string> args;

    // Environment values. Each element is in "KEY=value" format.
    std::vector<std::string> env;

    // Whether the user is developer.
    bool is_developer_end_user;

    // The UID to run chrome. Practically, chronos user.
    uid_t uid;
  };
  // Runs the set up, and returns parameters/attributes to launch
  // chromeos-chrome.
  std::optional<Result> Run();

 private:
  FRIEND_TEST_ALL_PREFIXES(ChromeSetupTest, CreateSymlinkIfMissing);
  FRIEND_TEST_ALL_PREFIXES(ChromeSetupTest, EnsureDirectoryExists);

  // Ensures that necessary directory exist with the correct permissions and
  // sets related arguments and environment variables.
  void CreateDirectories(chromeos::ui::ChromiumCommandBuilder* builder);

  // Create the target for the /var/lib/timezone/localtime symlink.
  // This allows the Chromium process to change the time zone.
  void SetUpTimezoneSymlink(uid_t uid, gid_t gid);

  // Creates a symlink to the source at target.
  void CreateSymlinkIfMissing(const base::FilePath& in_source,
                              const base::FilePath& in_target,
                              uid_t uid,
                              gid_t gid) const;

  // If missing, creates a directory at `path`. If a non directory
  // exists at `path`, delete the existing entry then create a new directory.
  // Then, sets `uid`, `gid` and `mode` to the directory (even if it already
  // exists). If -1 is passed to uid and/or gid, that means the uid/gid are
  // kept respectively. Setting uid/gid requires CAP_CHOWN, which is not
  // available in the unittest environment.
  // Returns true on success.
  bool EnsureDirectoryExists(const base::FilePath& path,
                             uid_t uid,
                             gid_t gid,
                             mode_t mode) const;

  // TODO(hidehiko): Mark them as const.
  brillo::CrosConfigInterface& cros_config_;
  segmentation::FeatureManagement& feature_management_;
};

// Property name of the wallpaper setting in CrosConfig.
extern const char kWallpaperProperty[];

// Property name of the per-model regulatory label directory in CrosConfig.
extern const char kRegulatoryLabelProperty[];

// Path to get the power button position info from cros_config.
extern const char kPowerButtonPositionPath[];

// Edge property in power button position info.
extern const char kPowerButtonEdgeField[];

// Position property in power button position info.
extern const char kPowerButtonPositionField[];

// Property name of the display setting in CrosConfig.
extern const char kDisplayCategoryField[];

// Property name of the form factor string in CrosConfig.
extern const char kFormFactorField[];

// Path to hardware properties in CrosConfig.
extern const char kHardwarePropertiesPath[];

// Path to powerd prefs in cros_config.
extern const char kPowerPath[];

// Powerd pref to allow Ambient EQ in cros_config.
extern const char kAllowAmbientEQField[];

// AllowAmbientEQ feature to enable on Chrome.
extern const char kAllowAmbientEQFeature[];

// Path to instant tethering prefs in cros_config.
extern const char kInstantTetheringPath[];

// Property to disable the Instant Tethering feature.
extern const char kDisableInstantTetheringProperty[];

// Path to get nnpalm data from cros_config.
extern const char kOzoneNNPalmPropertiesPath[];

// Property for compatibility with NNPalm in Ozone.
extern const char kOzoneNNPalmCompatibleProperty[];

// Property for model version in NNPalm for Ozone.
extern const char kOzoneNNPalmModelVersionProperty[];

// Property for radius polynomial in NNPalm for Ozone.
extern const char kOzoneNNPalmRadiusProperty[];

// Path to scheduler tune.
extern const char kSchedulerTunePath[];

// Property for urgent tasks boosting value.
extern const char kBoostUrgentProperty[];

// Initializes a ChromiumCommandBuilder and performs additional Chrome-specific
// setup. Returns environment variables that the caller should export for Chrome
// and arguments that it should pass to the Chrome binary, along with the UID
// that should be used to run Chrome.
//
// Initialization that is common across all Chromium-derived binaries (e.g.
// content_shell, app_shell, etc.) rather than just applying to the Chrome
// browser should be added to libchromeos's ChromiumCommandBuilder class
// instead.
//
// |cros_config| (if non-null) provides the device model configuration (used to
// look up the default wallpaper filename).
// |feature_management| provides interface to list the features enabled for
// the device.
void PerformChromeSetup(brillo::CrosConfigInterface* cros_config,
                        segmentation::FeatureManagement* feature_management,
                        bool* is_developer_end_user_out,
                        std::map<std::string, std::string>* env_vars_out,
                        std::vector<std::string>* args_out,
                        uid_t* uid_out);

// Add flags to override default scheduler tunings
void SetUpSchedulerFlags(chromeos::ui::ChromiumCommandBuilder* builder,
                         brillo::CrosConfigInterface* cros_config);

// Add switches pertinent to the Ash window manager generated at
// build-time by cros_config_schema.  These are stored in
// /ui:serialized-ash-flags, an implicitly generated element.
void AddSerializedAshSwitches(chromeos::ui::ChromiumCommandBuilder* builder,
                              brillo::CrosConfigInterface* cros_config);

// Add flags to specify the wallpaper to use. This is called by
// PerformChromeSetup and only present in the header for testing.
// Flags are added to |builder|, and |path_exists| is called to test whether a
// given file exists (e.g. use base::BindRepeating(base::PathExists)).
// |cros_config| (if non-null) provides the device model configuration (used to
// look up the default wallpaper filename).
void SetUpWallpaperFlags(
    chromeos::ui::ChromiumCommandBuilder* builder,
    brillo::CrosConfigInterface* cros_config,
    const base::RepeatingCallback<bool(const base::FilePath&)>& path_exists);

// Add "--delay_on_active_camera_client_change_for_notification" switch to
// spacify that a notification workaround should be used for the issue with
// delayed camera privacy switch events that occur on Jinlon device.
void SetUpDelayOnActiveCameraClientChangeForNotificationFlag(
    chromeos::ui::ChromiumCommandBuilder* builder,
    brillo::CrosConfigInterface* cros_config);

// Add "--device-help-content-id" switch to specify the help content
// to be displayed in the Showoff app.
void SetUpHelpContentSwitch(chromeos::ui::ChromiumCommandBuilder* builder,
                            brillo::CrosConfigInterface* cros_config);

// Add "--regulatory-label-dir" flag to specify the regulatory label directory
// containing per-region sub-directories, if the model-specific
// regulatory-label read from |cros_config| is non-null.
void SetUpRegulatoryLabelFlag(chromeos::ui::ChromiumCommandBuilder* builder,
                              brillo::CrosConfigInterface* cros_config);

// Add "--ash-power-button-position" flag with value in JSON format read from
// |cros_config|.
void SetUpPowerButtonPositionFlag(chromeos::ui::ChromiumCommandBuilder* builder,
                                  brillo::CrosConfigInterface* cros_config);

// Add "--ash-side-volume-button-position" flag with value in JSON format read
// from |cros_config|.
void SetUpSideVolumeButtonPositionFlag(
    chromeos::ui::ChromiumCommandBuilder* builder,
    brillo::CrosConfigInterface* cros_config);

// Add "--has-internal-stylus" flag if the device has
// an internal stylus.
void SetUpInternalStylusFlag(chromeos::ui::ChromiumCommandBuilder* builder,
                             brillo::CrosConfigInterface* cros_config);

// Add "--fingerprint-sensor-location" flag with value read from |cros_config|.
// If value is not "none".
void SetUpFingerprintSensorLocationFlag(
    chromeos::ui::ChromiumCommandBuilder* builder,
    brillo::CrosConfigInterface* cros_config);

// Flips feature flag for shelf auto-dimming if cros config indicates shelf
// auto-dimming should be enabled.
void SetUpAutoDimFlag(chromeos::ui::ChromiumCommandBuilder* builder,
                      brillo::CrosConfigInterface* cros_config);

// Add "--form-factor" flag with value read from |cros_config|.
void SetUpFormFactorFlag(chromeos::ui::ChromiumCommandBuilder* builder,
                         brillo::CrosConfigInterface* cros_config);

// Add "--ozone-nnpalm-properties" flag with value read from |cros_config|.
void SetUpOzoneNNPalmPropertiesFlag(
    chromeos::ui::ChromiumCommandBuilder* builder,
    brillo::CrosConfigInterface* cros_config);

// Add "AllowAmbientEQ" flag if allow-ambient-eq powerd pref is set to 1 in
// |cros_config|. Do not add flag is allow-ambient-eq is set to 0 or not set.
void SetUpAllowAmbientEQFlag(chromeos::ui::ChromiumCommandBuilder* builder,
                             brillo::CrosConfigInterface* cros_config);

// Gets a powerd pref from |cros_config|, falling back on searching the
// file-based powerd preferences if not found.
bool GetPowerdPref(const char* pref_name,
                   brillo::CrosConfigInterface* cros_config,
                   std::string* val_out);

// Disable instant tethering flag with value read from |cros_config| or USE
// flags.
void SetUpInstantTetheringFlag(chromeos::ui::ChromiumCommandBuilder* builder,
                               brillo::CrosConfigInterface* cros_config);

// Determine which Chrome crash handler this board wants to use. (Crashpad or
// Breakpad). Add the --enable-crashpad or --no-enable-crashpad flag as
// appropriate.
void AddCrashHandlerFlag(chromeos::ui::ChromiumCommandBuilder* builder);

// Add appropriate patterns to the --vmodule argument.
void AddVmodulePatterns(chromeos::ui::ChromiumCommandBuilder* builder);

// Adds flags related to ARC.
void AddArcFlags(chromeos::ui::ChromiumCommandBuilder* builder,
                 std::set<std::string>* disallowed_params_out,
                 brillo::CrosConfigInterface* cros_config);

// Adds flags related to machine learning features that are enabled only on a
// supported subset of devices.
void AddMlFlags(chromeos::ui::ChromiumCommandBuilder* builder,
                brillo::CrosConfigInterface* cros_config);

// Adds flags related to feature management that must be enabled for this
// device.
void AddFeatureManagementFlags(
    chromeos::ui::ChromiumCommandBuilder* builder,
    segmentation::FeatureManagement* FeatureManagement);

// Adds flags related to specific devices and/or overlays
void AddDeviceSpecificFlags(chromeos::ui::ChromiumCommandBuilder* builder);

// Adds flags related to the Mantis project
void AddMantisFlags(chromeos::ui::ChromiumCommandBuilder* builder);

// Adds flags for features using the XS model.
void AddXSFlags(chromeos::ui::ChromiumCommandBuilder* builder);

// Adds flags related to the Coral project.
void AddCoralFlags(chromeos::ui::ChromiumCommandBuilder* builder);

// Adds flags related to the Cuttlefish project.
void AddCuttlefishFlags(chromeos::ui::ChromiumCommandBuilder* builder);

// Allows Chrome to access GPU memory information despite /sys/kernel/debug
// being owned by debugd. This limits the security attack surface versus
// leaving the whole debug directory world-readable. See crbug.com/175828
void SetUpDebugfsGpu();

}  // namespace login_manager

#endif  // LOGIN_MANAGER_CHROME_SETUP_H_
