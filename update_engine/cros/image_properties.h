// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This module abstracts the properties tied to the current running image. These
// properties are meant to be constant during the life of this daemon, but can
// be modified in dev-move or non-official builds.

#ifndef UPDATE_ENGINE_CROS_IMAGE_PROPERTIES_H_
#define UPDATE_ENGINE_CROS_IMAGE_PROPERTIES_H_

#include <string>

namespace chromeos_update_engine {

// The read-only system properties of the running image.
struct ImageProperties {
  // The product id of the image used for all channels, except canary.
  std::string product_id;
  // The canary-channel product id.
  std::string canary_product_id;

  // The product version of this image.
  std::string version;

  // The version of all product components in key values pairs.
  std::string product_components;

  // A unique string that identifies this build. Normally a combination of the
  // the version, signing keys and build target.
  std::string build_fingerprint;

  // The Android build type, should be either 'user', 'userdebug' or 'eng'.
  // It's empty string on other platform.
  std::string build_type;

  // The board name this image was built for.
  std::string board;

  // The release channel this image was obtained from.
  std::string current_channel;

  // Whether we allow arbitrary channels instead of just the ones listed in
  // kChannelsByStability.
  bool allow_arbitrary_channels = false;

  // The Omaha URL this image should get updates from.
  std::string omaha_url;

  // The release builder path.
  std::string builder_path;
};

// The mutable image properties are read-write image properties, initialized
// with values from the image but can be modified by storing them in the
// stateful partition.
struct MutableImageProperties {
  // The release channel we are tracking.
  std::string target_channel;

  // Whether powerwash is allowed when downloading an update for the selected
  // target_channel.
  bool is_powerwash_allowed{false};
};

// Loads all the image properties from the running system. In case of error
// loading any of these properties from the read-only system image a default
// value may be returned instead.
ImageProperties LoadImageProperties();

// Loads the mutable image properties from the stateful partition if found or
// the system image otherwise.
MutableImageProperties LoadMutableImageProperties();

// Stores the mutable image properties in the stateful partition. Returns
// whether the operation succeeded.
bool StoreMutableImageProperties(const MutableImageProperties& properties);

// Logs the image properties.
void LogImageProperties();

// Sets the root_prefix used to load files from during unittests to
// |test_root_prefix|. Passing a nullptr value resets it to the default.
namespace test {
void SetImagePropertiesRootPrefix(const char* test_root_prefix);
}  // namespace test

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_IMAGE_PROPERTIES_H_
