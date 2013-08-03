// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_PPP_DEVICE_FACTORY_H_
#define SHILL_PPP_DEVICE_FACTORY_H_

#include <string>

#include <base/lazy_instance.h>

namespace shill {

class ControlInterface;
class EventDispatcher;
class Manager;
class Metrics;
class PPPDevice;

class PPPDeviceFactory {
 public:
  virtual ~PPPDeviceFactory();

  // This is a singleton. Use PPPDeviceFactory::GetInstance()->Foo().
  static PPPDeviceFactory *GetInstance();

  virtual PPPDevice *CreatePPPDevice(
      ControlInterface *control,
      EventDispatcher *dispatcher,
      Metrics *metrics,
      Manager *manager,
      const std::string &link_name,
      int interface_index);

 protected:
  PPPDeviceFactory();

 private:
  friend struct base::DefaultLazyInstanceTraits<PPPDeviceFactory>;

  DISALLOW_COPY_AND_ASSIGN(PPPDeviceFactory);
};

}  // namespace shill

#endif  // SHILL_PPP_DEVICE_FACTORY_H_
