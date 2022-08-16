// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef FEATURED_FEATURE_LIBRARY_H_
#define FEATURED_FEATURE_LIBRARY_H_

#include "featured/c_feature_library.h"  // for enums
#include "featured/feature_export.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/location.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/synchronization/lock.h>
#include <base/task/task_runner.h>
#include <base/thread_annotations.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

namespace feature {

class FEATURE_EXPORT PlatformFeaturesInterface {
 public:
  virtual ~PlatformFeaturesInterface() = default;

  using IsEnabledCallback = base::OnceCallback<void(bool)>;
  // Asynchronously determine whether the given feature is enabled, using the
  // specified default value if Chrome doesn't define a value for the feature
  // or the dbus call fails.
  // If you have multiple related features you wish to look up, you MUST look
  // them all up in the same call using GetParamsAndEnabled{,Blocking} -- if you
  // look them up across multiple calls, chrome may have restarted in between
  // calls, giving inconsistent state.
  // DO NOT CACHE the result of this call across chrome restarts, as it may
  // change -- for example, when a user logs in or out or when they apply
  // changes to chrome://flags.
  // NOTE: As of 2021-12, Chrome only retrieves finch seeds after a first reboot
  // (e.g. when logging in). So, if you need to run an experiment before this it
  // should be set up as a client-side trial.
  virtual void IsEnabled(const VariationsFeature& feature,
                         IsEnabledCallback callback) = 0;

  // Like IsEnabled(), but blocks waiting for the dbus call to finish.
  // If you have multiple related features you wish to look up, you MUST look
  // them all up in the same call using GetParamsAndEnabled{,Blocking} -- if you
  // look them up across multiple calls, chrome may have restarted in between
  // calls, giving inconsistent state.
  // DO NOT CACHE the result of this call across chrome restarts, as it may
  // change -- for example, when a user logs in or out or when they apply
  // changes to chrome://flags.
  // NOTE: As of 2021-12, Chrome only retrieves finch seeds after a first reboot
  // (e.g. when logging in). So, if you need to run an experiment before this it
  // should be set up as a client-side trial.
  // Does *not* block waiting for the service to be available, so may have
  // spurious fallbacks to the default value that could be avoided with
  // IsEnabled(), especially soon after Chrome starts.
  // TODO(b/236009983): Fix this.
  virtual bool IsEnabledBlocking(const VariationsFeature& feature) = 0;

  struct ParamsResultEntry {
   public:
    // Whether the feature is enabled or disabled.
    bool enabled;
    // If non-nullopt, gives the key/value pairs for any parameters, as
    // determined by chromium.
    // If this is empty, callers should fall back to hard-coded default values
    // for all parameters.
    std::map<std::string, std::string> params;
  };
  // Mapping the feature name to its ParamsResultEntry struct.
  using ParamsResult = std::map<std::string, ParamsResultEntry>;
  using GetParamsCallback = base::OnceCallback<void(ParamsResult)>;
  // Asynchronously get the parameters for a given set of related features, as
  // well as a boolean representing whether each feature is enabled.
  // Gives back nullopt if the lookup fails.
  // If you have multiple related features you wish to look up, you MUST look
  // them all up in the same call -- if you look them up across multiple calls,
  // chrome may have restarted in between calls, giving inconsistent state.
  // DO NOT CACHE the result of this call across chrome restarts, as it may
  // change -- for example, when a user logs in or out or when they apply
  // changes to chrome://flags.
  // NOTE: As of 2021-12, Chrome only retrieves finch seeds after a first reboot
  // (e.g. when logging in). So, if you need to run an experiment before this it
  // should be set up as a client-side trial.
  virtual void GetParamsAndEnabled(
      const std::vector<const VariationsFeature* const>& features,
      GetParamsCallback callback) = 0;
  // Like GetParamsAndEnabled(), but blocks waiting for the dbus call to finish.
  // If you have multiple related features you wish to look up, you MUST look
  // them all up in the same call -- if you look them up across multiple calls,
  // chrome may have restarted in between calls, giving inconsistent state.
  // DO NOT CACHE the result of this call across chrome restarts, as it may
  // change -- for example, when a user logs in or out or when they apply
  // changes to chrome://flags.
  // NOTE: As of 2021-12, Chrome only retrieves finch seeds after a first reboot
  // (e.g. when logging in). So, if you need to run an experiment before this it
  // should be set up as a client-side trial.
  // Does *not* block waiting for the service to be available, so may have
  // spurious fallbacks to the default value that could be avoided with
  // GetParamsAndEnabled(), especially soon after Chrome starts.
  // TODO(b/236009983): Fix this.
  virtual ParamsResult GetParamsAndEnabledBlocking(
      const std::vector<const VariationsFeature* const>& features) = 0;

  // Shutdown the bus object, if any. Used for C API, or when destroying it and
  // the bus is no longer owned.
  virtual void ShutdownBus() = 0;
};

class FEATURE_EXPORT PlatformFeatures : public PlatformFeaturesInterface {
 public:
  PlatformFeatures(const PlatformFeatures&) = delete;
  PlatformFeatures& operator=(const PlatformFeatures&) = delete;

  // Construct a new PlatformFeatures object based on the provided |bus|.
  // Returns |nullptr| on failure to create an ObjectProxy
  static std::unique_ptr<PlatformFeatures> New(scoped_refptr<dbus::Bus> bus);

  void IsEnabled(const VariationsFeature& feature,
                 IsEnabledCallback callback) override;

  bool IsEnabledBlocking(const VariationsFeature& feature) override;

  void GetParamsAndEnabled(
      const std::vector<const VariationsFeature* const>& features,
      GetParamsCallback callback) override;

  ParamsResult GetParamsAndEnabledBlocking(
      const std::vector<const VariationsFeature* const>& features) override;

  void ShutdownBus() override;

 protected:
  explicit PlatformFeatures(scoped_refptr<dbus::Bus> bus,
                            dbus::ObjectProxy* proxy);

 private:
  friend class FeatureLibraryTest;
  FRIEND_TEST(FeatureLibraryTest, CheckFeatureIdentity);

  // Callback that is invoked for IsEnabled() when WaitForServiceToBeAvailable()
  // finishes.
  void OnWaitForServiceIsEnabled(const VariationsFeature& feature,
                                 IsEnabledCallback callback,
                                 bool available);

  // Callback that is invoked when proxy_->CallMethod() finishes.
  void HandleIsEnabledResponse(const VariationsFeature& feature,
                               IsEnabledCallback callback,
                               dbus::Response* response);

  // Creates the default response for GetParamsAndEnabled{,Blocking}()
  ParamsResult CreateDefaultGetParamsAndEnabledResponse(
      const std::vector<const VariationsFeature* const>& features);

  // Callback that is invoked for GetParamsAndEnabled() when
  // WaitForServiceToBeAvailable() finishes.
  void OnWaitForServiceGetParams(
      const std::vector<const VariationsFeature* const>& feature,
      GetParamsCallback callback,
      bool available);

  // Callback that is invoked when proxy_->CallMethod() finishes.
  void HandleGetParamsResponse(
      const std::vector<const VariationsFeature* const>& features,
      GetParamsCallback callback,
      dbus::Response* response);

  // Encoding side of both HandleGetParamsResponse and
  // GetParamsAndEnabledBlocking.
  void EncodeGetParamsArgument(
      dbus::MessageWriter* writer,
      const std::vector<const VariationsFeature* const>& features);

  // Decoding side of both HandleGetParamsResponse and
  // GetParamsAndEnabledBlocking.
  ParamsResult ParseGetParamsResponse(
      dbus::Response* response,
      const std::vector<const VariationsFeature* const>& features);

  // Verify that we have only ever seen |feature| with this same address.
  // Used to prevent defining the same feature with distinct default values.
  bool CheckFeatureIdentity(const VariationsFeature& feature)
      LOCKS_EXCLUDED(lock_);

  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* proxy_;

  // Map that keeps track of seen features, to ensure a single feature is
  // only defined once. This verification is only done in builds with DCHECKs
  // enabled.
  base::Lock lock_;
  std::map<std::string, const VariationsFeature*> feature_identity_tracker_
      GUARDED_BY(lock_);

  base::WeakPtrFactory<PlatformFeatures> weak_ptr_factory_{this};
};
}  // namespace feature

#endif  // FEATURED_FEATURE_LIBRARY_H_
