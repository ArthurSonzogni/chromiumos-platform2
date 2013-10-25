// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MM1_SIM_PROXY_
#define SHILL_MM1_SIM_PROXY_

#include <string>

#include "dbus_proxies/org.freedesktop.ModemManager1.Sim.h"
#include "shill/dbus_properties.h"
#include "shill/mm1_sim_proxy_interface.h"

namespace shill {
namespace mm1 {

// A proxy to org.freedesktop.ModemManager1.Sim.
class SimProxy : public SimProxyInterface {
 public:
  // Constructs an org.freedesktop.ModemManager1.Sim DBus object
  // proxy at |path| owned by |service|.
  SimProxy(DBus::Connection *connection,
           const std::string &path,
           const std::string &service);
  virtual ~SimProxy();

  // Inherited methods from SimProxyInterface.
  virtual void SendPin(const std::string &pin,
                       Error *error,
                       const ResultCallback &callback,
                       int timeout);
  virtual void SendPuk(const std::string &puk,
                       const std::string &pin,
                       Error *error,
                       const ResultCallback &callback,
                       int timeout);
  virtual void EnablePin(const std::string &pin,
                         const bool enabled,
                         Error *error,
                         const ResultCallback &callback,
                         int timeout);
  virtual void ChangePin(const std::string &old_pin,
                         const std::string &new_pin,
                         Error *error,
                         const ResultCallback &callback,
                         int timeout);

 private:
  class Proxy : public org::freedesktop::ModemManager1::Sim_proxy,
                public DBus::ObjectProxy {
   public:
    Proxy(DBus::Connection *connection,
          const std::string &path,
          const std::string &service);
    virtual ~Proxy();

   private:
    // Method callbacks inherited from
    // org::freedesktop::ModemManager1::SimProxy
    virtual void SendPinCallback(const ::DBus::Error &dberror,
                                 void *data);
    virtual void SendPukCallback(const ::DBus::Error &dberror,
                                 void *data);
    virtual void EnablePinCallback(const ::DBus::Error &dberror,
                                   void *data);
    virtual void ChangePinCallback(const ::DBus::Error &dberror,
                                   void *data);

    DISALLOW_COPY_AND_ASSIGN(Proxy);
  };

  Proxy proxy_;

  DISALLOW_COPY_AND_ASSIGN(SimProxy);
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_MM1_SIM_PROXY_
