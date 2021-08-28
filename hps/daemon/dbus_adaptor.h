// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HPS_DAEMON_DBUS_ADAPTOR_H_
#define HPS_DAEMON_DBUS_ADAPTOR_H_

#include <memory>

#include <base/sequence_checker.h>
#include <base/timer/timer.h>
#include <dbus_adaptors/org.chromium.Hps.h>
#include <hps/hps.h>

namespace hps {

class DBusAdaptor : public org::chromium::HpsAdaptor,
                    public org::chromium::HpsInterface {
 public:
  DBusAdaptor(scoped_refptr<dbus::Bus> bus,
              std::unique_ptr<HPS>,
              uint32_t poll_time_ms);

  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb);

  // Timer Callback used to poll hps hardware and debounce results.
  void PollTask();

  // Methods for HpsInterface
  bool EnableFeature(brillo::ErrorPtr* error, uint8_t feature) override;

  bool DisableFeature(brillo::ErrorPtr* error, uint8_t feature) override;

  bool GetFeatureResult(brillo::ErrorPtr* error,
                        uint8_t feature,
                        uint16_t* result) override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
  std::unique_ptr<HPS> hps_;
  const uint32_t poll_time_ms_;
  base::RepeatingTimer poll_timer_;
  std::bitset<kFeatures> enabled_features_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace hps

#endif  // HPS_DAEMON_DBUS_ADAPTOR_H_
