// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_H_

#include <glib.h>
#include <list>
#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/file_path.h"
#include "base/observer_list.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/signal_callback.h"
#include "power_manager/powerd/system/ambient_light_observer.h"
#include "power_manager/powerd/system/async_file_reader.h"

namespace power_manager {
namespace system {

class AmbientLightSensorInterface {
 public:
  AmbientLightSensorInterface() {}
  virtual ~AmbientLightSensorInterface() {}

  // Adds or removes observers for sensor readings.
  virtual void AddObserver(AmbientLightObserver* observer) = 0;
  virtual void RemoveObserver(AmbientLightObserver* observer) = 0;

  // Used by observers in their callback to get the raw reading from the sensor
  // for the ambient light level. -1 is considered an error value.
  virtual int GetAmbientLightLux() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(AmbientLightSensorInterface);
};

class AmbientLightSensor : public AmbientLightSensorInterface {
 public:
  AmbientLightSensor();
  virtual ~AmbientLightSensor();

  void set_device_list_path_for_testing(const base::FilePath& path) {
    device_list_path_ = path;
  }
  void set_poll_interval_ms_for_testing(int interval_ms) {
    poll_interval_ms_ = interval_ms;
  }

  // Starts polling.  This is separate from c'tor so that tests can call
  // set_*_for_testing() first.
  void Init();

  // AmbientLightSensorInterface implementation:
  virtual void AddObserver(AmbientLightObserver* observer) OVERRIDE;
  virtual void RemoveObserver(AmbientLightObserver* observer) OVERRIDE;
  virtual int GetAmbientLightLux() OVERRIDE;

 private:
  // Handler for a periodic event that reads the ambient light sensor.
  SIGNAL_CALLBACK_0(AmbientLightSensor, gboolean, ReadAls);

  // Asynchronous I/O success and error handlers, respectively.
  void ReadCallback(const std::string& data);
  void ErrorCallback();

  // Initializes |als_file_|. Returns true on success.
  bool InitAlsFile();

  // Path containing backlight devices.  Typically under /sys, but can be
  // overridden by tests.
  base::FilePath device_list_path_;

  // GLib timeout ID for running ReadAls(), or 0 if unset.
  guint poll_timeout_id_;

  // Time between polls of the sensor file, in milliseconds.
  int poll_interval_ms_;

  // List of backlight controllers that are currently interested in updates from
  // this sensor.
  ObserverList<AmbientLightObserver> observers_;

  // Lux value read by the class. If this read did not succeed or no read has
  // occured yet this variable is set to -1.
  int lux_value_;

  // Number of attempts to find and open the lux file made so far.
  int num_init_attempts_;

  // This is the ambient light sensor asynchronous file I/O object.
  AsyncFileReader als_file_;

  // Callbacks for asynchronous file I/O.
  base::Callback<void(const std::string&)> read_cb_;
  base::Callback<void()> error_cb_;

  DISALLOW_COPY_AND_ASSIGN(AmbientLightSensor);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_H_
