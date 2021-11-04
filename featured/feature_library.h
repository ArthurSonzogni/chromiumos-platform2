// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEATURED_FEATURE_LIBRARY_H_
#define FEATURED_FEATURE_LIBRARY_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/callback.h>
#include <base/location.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/task_runner.h>
#include <base/thread_annotations.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#define FEATURE_EXPORT __attribute__((__visibility__("default")))
#define FEATURE_PRIVATE __attribute__((__visibility__("hidden")))

namespace feature {

// Specifies whether a given feature is enabled or disabled by default.
// NOTE: The actual runtime state may be different, due to a field trial or a
// command line switch.
enum FEATURE_EXPORT FeatureState {
  FEATURE_DISABLED_BY_DEFAULT,
  FEATURE_ENABLED_BY_DEFAULT,
};

// The Feature struct is used to define the default state for a feature. See
// comment below for more details. There must only ever be one struct instance
// for a given feature name - generally defined as a constant global variable or
// file static. It should never be used as a constexpr as it breaks
// pointer-based identity lookup.
struct FEATURE_EXPORT Feature {
  // The name of the feature. This should be unique to each feature and is used
  // for enabling/disabling features via command line flags and experiments.
  // It is strongly recommended to use CamelCase style for feature names, e.g.
  // "MyGreatFeature".
  // In almost all cases, your feature name should start with "CrOSLateBoot",
  // otherwise the lookup will fail.
  const char* const name;

  // The default state (i.e. enabled or disabled) for this feature.
  // NOTE: The actual runtime state may be different, due to a field trial or a
  // command line switch.
  const FeatureState default_state;
};

class FEATURE_EXPORT PlatformFeaturesInterface {
 public:
  virtual ~PlatformFeaturesInterface() = default;

  using IsEnabledCallback = base::OnceCallback<void(bool)>;
  // Asynchronously determine whether the given feature is enabled, using the
  // specified default value if Chrome doesn't define a value for the feature
  // or the dbus call fails.
  // DO NOT CACHE the result of this call, as it may change -- for example, when
  // Chrome restarts or when a user logs in or out.
  virtual void IsEnabled(const Feature& feature,
                         IsEnabledCallback callback) = 0;

  // Like IsEnabled(), but blocks waiting for the dbus call to finish.
  // Does *not* block waiting for the service to be available, so may have
  // spurious fallbacks to the default value that could be avoided with
  // IsEnabled(), especially soon after Chrome starts.
  // DO NOT CACHE the result of this call, as it may change -- for example, when
  // Chrome restarts or when a user logs in or out.
  virtual bool IsEnabledBlocking(const Feature& feature) = 0;
};

class FEATURE_EXPORT PlatformFeatures : public PlatformFeaturesInterface {
 public:
  PlatformFeatures(const PlatformFeatures&) = delete;
  PlatformFeatures& operator=(const PlatformFeatures&) = delete;

  // Construct a new PlatformFeatures object based on the provided |bus|.
  // Returns |nullptr| on failure to create an ObjectProxy
  static std::unique_ptr<PlatformFeatures> New(scoped_refptr<dbus::Bus> bus);

  void IsEnabled(const Feature& feature, IsEnabledCallback callback) override;

  bool IsEnabledBlocking(const Feature& feature) override;

 protected:
  explicit PlatformFeatures(scoped_refptr<dbus::Bus> bus,
                            dbus::ObjectProxy* proxy);

 private:
  friend class FeatureLibraryTest;
  FRIEND_TEST(FeatureLibraryTest, CheckFeatureIdentity);

  // Callback that is invoked when WaitForServiceToBeAvailable() finishes.
  void OnWaitForService(const Feature& Feature,
                        IsEnabledCallback callback,
                        bool available);

  // Callback that is invoked when proxy_->CallMethod() finishes.
  void HandleIsEnabledResponse(const Feature& Feature,
                               IsEnabledCallback callback,
                               dbus::Response* response);

  // Verify that we have only ever seen |feature| with this same address.
  // Used to prevent defining the same feature with distinct default values.
  bool CheckFeatureIdentity(const Feature& feature) LOCKS_EXCLUDED(lock_);

  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* proxy_;

  // Map that keeps track of seen features, to ensure a single feature is
  // only defined once. This verification is only done in builds with DCHECKs
  // enabled.
  base::Lock lock_;
  std::map<std::string, const Feature*> feature_identity_tracker_
      GUARDED_BY(lock_);

  base::WeakPtrFactory<PlatformFeatures> weak_ptr_factory_{this};
};

// Fake class for testing, which just returns a specified value.
class FEATURE_EXPORT FakePlatformFeatures : public PlatformFeaturesInterface {
 public:
  explicit FakePlatformFeatures(scoped_refptr<dbus::Bus> bus, bool enabled)
      : bus_(bus), enabled_(enabled) {}

  FakePlatformFeatures(const FakePlatformFeatures&) = delete;
  FakePlatformFeatures& operator=(const FakePlatformFeatures&) = delete;

  void IsEnabled(const Feature& feature, IsEnabledCallback callback) override {
    bus_->AssertOnOriginThread();
    bus_->GetOriginTaskRunner()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), enabled_));
  }

  bool IsEnabledBlocking(const Feature& feature) override { return enabled_; }

  void SetEnabled(bool enabled) { enabled_ = enabled; }

 private:
  scoped_refptr<dbus::Bus> bus_;
  bool enabled_ = false;
};
}  // namespace feature

#endif  // FEATURED_FEATURE_LIBRARY_H_
