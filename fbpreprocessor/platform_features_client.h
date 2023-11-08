// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_PLATFORM_FEATURES_CLIENT_H_
#define FBPREPROCESSOR_PLATFORM_FEATURES_CLIENT_H_

#include <base/observer_list.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>

namespace fbpreprocessor {

class PlatformFeaturesClientInterface {
 public:
  class Observer {
   public:
    virtual void OnFeatureChanged(bool allowed) = 0;

    virtual ~Observer() = default;
  };

  // Adds and removes the observer.
  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;

  virtual ~PlatformFeaturesClientInterface() = default;
};

class PlatformFeaturesClient : public PlatformFeaturesClientInterface {
 public:
  PlatformFeaturesClient();

  void Start(dbus::Bus* bus);

  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

  bool FirmwareDumpsAllowedByFinch() const { return allowed_; }

 private:
  void Refetch();

  void OnConnected(bool ready);

  void OnFetched(bool allowed);

  bool allowed_;

  feature::PlatformFeatures* feature_lib_;

  // List of PlatformFeaturesClient observers
  base::ObserverList<Observer>::Unchecked observers_;

  base::WeakPtrFactory<PlatformFeaturesClient> weak_factory_{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_PLATFORM_FEATURES_CLIENT_H_
