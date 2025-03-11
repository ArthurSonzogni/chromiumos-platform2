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

  void Start(feature::PlatformFeaturesInterface* feature_lib);

  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

  bool FirmwareDumpsAllowedByFinch() const { return allowed_; }

  void set_base_dir_for_test(const base::FilePath& base_dir) {
    base_dir_ = base_dir;
  }

 private:
  void Refetch();

  void OnConnected(bool ready);

  void OnFetched(bool allowed);

  // Base directory where the file containing the value of the Finch flag is
  // stored, typically /run/fbpreprocessord/. Unit tests can replace this
  // directory with local temporary directories by calling
  // |set_base_dir_for_test()|.
  base::FilePath base_dir_;

  bool allowed_;

  feature::PlatformFeaturesInterface* feature_lib_;

  // List of PlatformFeaturesClient observers
  base::ObserverList<Observer>::Unchecked observers_;

  base::WeakPtrFactory<PlatformFeaturesClient> weak_factory_{this};
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_PLATFORM_FEATURES_CLIENT_H_
