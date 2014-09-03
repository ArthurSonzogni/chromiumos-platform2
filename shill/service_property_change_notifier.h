// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SERVICE_PROPERTY_CHANGE_NOTIFIER_H_
#define SHILL_SERVICE_PROPERTY_CHANGE_NOTIFIER_H_

#include <string>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/scoped_ptr.h>

#include "shill/accessor_interface.h"

namespace shill {

class PropertyObserverInterface;
class ServiceAdaptorInterface;

// A collection of property observers used by objects to deliver
// property change notifications.  This object holds an un-owned
// pointer to the ServiceAdaptor to which notifications should be
// posted.  This pointer must be valid for the lifetime of this
// property change notifier.
class ServicePropertyChangeNotifier {
 public:
  explicit ServicePropertyChangeNotifier(ServiceAdaptorInterface *adaptor);
  virtual ~ServicePropertyChangeNotifier();

  virtual void AddBoolPropertyObserver(const std::string &name,
                                       BoolAccessor accessor);
  virtual void AddUint8PropertyObserver(const std::string &name,
                                        Uint8Accessor accessor);
  virtual void AddUint16PropertyObserver(const std::string &name,
                                         Uint16Accessor accessor);
  virtual void AddUint16sPropertyObserver(const std::string &name,
                                          Uint16sAccessor accessor);
  virtual void AddUintPropertyObserver(const std::string &name,
                                       Uint32Accessor accessor);
  virtual void AddIntPropertyObserver(const std::string &name,
                                      Int32Accessor accessor);
  virtual void AddRpcIdentifierPropertyObserver(const std::string &name,
                                                RpcIdentifierAccessor accessor);
  virtual void AddStringPropertyObserver(const std::string &name,
                                         StringAccessor accessor);
  virtual void AddStringmapPropertyObserver(const std::string &name,
                                            StringmapAccessor accessor);
  virtual void UpdatePropertyObservers();

 private:
  // Redirects templated calls to a value reference to a by-copy version.
  void BoolPropertyUpdater(const std::string &name, const bool &value);
  void Uint8PropertyUpdater(const std::string &name, const uint8_t &value);
  void Uint16PropertyUpdater(const std::string &name, const uint16_t &value);
  void Uint32PropertyUpdater(const std::string &name, const uint32_t &value);
  void Int32PropertyUpdater(const std::string &name, const int32_t &value);

  ServiceAdaptorInterface *rpc_adaptor_;
  std::vector<scoped_ptr<PropertyObserverInterface>> property_observers_;

  DISALLOW_COPY_AND_ASSIGN(ServicePropertyChangeNotifier);
};

}  // namespace shill

#endif  // SHILL_SERVICE_PROPERTY_CHANGE_NOTIFIER_H_
