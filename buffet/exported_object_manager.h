// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_EXPORTED_OBJECT_MANAGER_H_
#define BUFFET_EXPORTED_OBJECT_MANAGER_H_

#include <map>
#include <string>

#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <dbus/object_path.h>

namespace buffet {

namespace dbus_utils {

// ExportedObjectManager is a delegate that implements the
// org.freedesktop.DBus.ObjectManager interface on behalf of another
// object. It handles sending signals when new interfaces are added.
//
// This class is very similar to the ExportedPropertySet class, except that
// it allows objects to expose an object manager interface rather than the
// properties interface.
//
//  Example usage:
//
//   class ExampleObjectManager {
//    public:
//     ExampleObjectManager(dbus::Bus* bus)
//         : object_manager_(bus, "/my/objects/path") { }
//
//     void Init(const OnInitFinish& cb) { object_manager_.Init(cb); }
//     void ClaimInterface(const dbus::ObjectPath& path,
//                         const std::string& interface_name,
//                         const PropertyWriter& writer) {
//       object_manager_->ClaimInterface(...);
//     }
//     void ReleaseInterface(const dbus::ObjectPath& path,
//                           const std::string& interface_name) {
//       object_manager_->ReleaseInterface(...);
//     }
//
//    private:
//     ExportedObjectManager object_manager_;
//   };
//
//   class MyObjectClaimingAnInterface {
//    public:
//     MyObjectClaimingAnInterface(ExampleObjectManager* object_manager)
//       : object_manager_(object_manager) {}
//
//     void OnInitFinish(bool success) {
//       if (!success) { /* handle that */ }
//       object_manager_->ClaimInterface(
//           my_path_, my_interface_, my_properties_.GetWriter());
//     }
//
//    private:
//     struct Properties : public ExportedPropertySet {
//      public:
//       /* Lots of interesting properties. */
//     };
//
//     Properties my_properties_;
//     ExampleObjectManager* object_manager_;
//   };
class ExportedObjectManager
    : public base::SupportsWeakPtr<ExportedObjectManager> {
 public:
  // Writes a dictionary of property name to property value variants to writer.
  typedef base::Callback<void(dbus::MessageWriter* writer)> PropertyWriter;
  typedef base::Callback<void(bool success)> OnInitFinish;
  typedef std::map<std::string, PropertyWriter> InterfaceProperties;

  ExportedObjectManager(scoped_refptr<dbus::Bus> bus,
                        const dbus::ObjectPath& path);

  // Registers methods implementing the ObjectManager interface on the object
  // exported on the path given in the constructor. Must be called on the
  // origin thread.
  void Init(const OnInitFinish& cb);

  // Trigger a signal that |path| has added an interface |interface_name|
  // with properties as given by |writer|.
  void ClaimInterface(const dbus::ObjectPath& path,
                      const std::string& interface_name,
                      const PropertyWriter& writer);

  // Trigger a signal that |path| has removed an interface |interface_name|.
  void ReleaseInterface(const dbus::ObjectPath& path,
                        const std::string& interface_name);

  const scoped_refptr<dbus::Bus>& GetBus() const { return bus_; }

 private:
  void HandleGetManagedObjects(
      dbus::MethodCall* method_call,
      dbus::ExportedObject::ResponseSender response_sender) const;

  scoped_refptr<dbus::Bus> bus_;
  // |exported_object_| outlives *this.
  dbus::ExportedObject* const exported_object_;
  // Tracks all objects currently known to the ExportedObjectManager.
  std::map<dbus::ObjectPath, InterfaceProperties> registered_objects_;

  friend class ExportedObjectManagerTest;
  DISALLOW_COPY_AND_ASSIGN(ExportedObjectManager);
};

}  //  namespace dbus_utils

}  //  namespace buffet

#endif  // BUFFET_EXPORTED_OBJECT_MANAGER_H_
