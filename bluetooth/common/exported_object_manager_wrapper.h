// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLUETOOTH_COMMON_EXPORTED_OBJECT_MANAGER_WRAPPER_H_
#define BLUETOOTH_COMMON_EXPORTED_OBJECT_MANAGER_WRAPPER_H_

#include <map>
#include <memory>
#include <string>

#include <brillo/dbus/exported_object_manager.h>

#include "bluetooth/common/property.h"

namespace bluetooth {

// Represents an exported interface on an exported object.
class ExportedInterface {
 public:
  // |dbus_object| is owned by ExportedObject which always outlives
  // ExportedInterface.
  ExportedInterface(const dbus::ObjectPath& object_path,
                    const std::string& interface_name,
                    brillo::dbus_utils::DBusObject* dbus_object);

  ~ExportedInterface() = default;

  // Exports the interface.
  void ExportAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
          callback);

  // Unexports the interface and all its exported properties.
  void Unexport();

  // Adds a raw method handler for |method_name| in this interface.
  void AddRawMethodHandler(
      const std::string& method_name,
      const base::Callback<void(
          dbus::MethodCall*, dbus::ExportedObject::ResponseSender)>& handler);

  // Adds a method handler for |method_name| in this interface.
  template <typename Instance, typename Class, typename... Args>
  void AddSimpleMethodHandlerWithErrorAndMessage(
      const std::string& method_name,
      Instance instance,
      bool (Class::*handler)(brillo::ErrorPtr*, dbus::Message*, Args...)) {
    dbus_object_->AddOrGetInterface(interface_name_)
        ->AddSimpleMethodHandlerWithErrorAndMessage(method_name, instance,
                                                    handler);
  }

  // Copies the value of the property having name |property_name| to the
  // corresponding exported property. Doesn't own |property_base| and
  // |property_factory| and doesn't keep them.
  void CopyPropertyToExportedProperty(const std::string& property_name,
                                      dbus::PropertyBase* property_base,
                                      PropertyFactoryBase* property_factory);

  // Exports the specified property if not already exported.
  // Doesn't own |property_factory| and doesn't keep it.
  brillo::dbus_utils::ExportedPropertyBase* EnsureExportedPropertyRegistered(
      const std::string& property_name, PropertyFactoryBase* property_factory);

  // Exports the specified property having the specified type |T|, if not
  // already exported.
  template <typename T>
  brillo::dbus_utils::ExportedProperty<T>* EnsureExportedPropertyRegistered(
      const std::string& property_name) {
    PropertyFactory<T> property_factory;
    return static_cast<brillo::dbus_utils::ExportedProperty<T>*>(
        EnsureExportedPropertyRegistered(property_name, &property_factory));
  }

 private:
  // Object path this interface is on.
  dbus::ObjectPath object_path_;
  // The name of this interface.
  std::string interface_name_;

  // The exported DBusObject, owned by ExportedObject which outlives
  // this ExportedInterface object.
  brillo::dbus_utils::DBusObject* dbus_object_;

  // The currently exported property names.
  std::map<std::string,
           std::unique_ptr<brillo::dbus_utils::ExportedPropertyBase>>
      exported_properties_;

  DISALLOW_COPY_AND_ASSIGN(ExportedInterface);
};

// Wrapper of brillo::dbus_utils::DBusObject.
class ExportedObject {
 public:
  // Doesn't own |exported_object_manager|, so callers should make sure that
  // |exported_object_manager| outlives this object.
  ExportedObject(
      brillo::dbus_utils::ExportedObjectManager* exported_object_manager,
      const scoped_refptr<dbus::Bus>& bus,
      const dbus::ObjectPath& object_path,
      brillo::dbus_utils::DBusObject::PropertyHandlerSetupCallback
          property_handler_setup_callback);
  ~ExportedObject();

  // Returns the exported interface having name |interface_name|. The returned
  // pointer is owned by this object so callers should not use use the pointer
  // outside the lifespan of this object.
  ExportedInterface* GetExportedInterface(const std::string& interface_name);

  // Adds an interface on this object. The interface is not yet exported until
  // ExportedInterface::ExportAsync is called.
  void AddExportedInterface(const std::string& interface_name);

  // Removes an interface from being exported.
  void RemoveExportedInterface(const std::string& interface_name);

  // Registers the exported object with D-Bus.
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
          callback);

 private:
  friend class ExportedObjectManagerWrapper;

  dbus::ObjectPath object_path_;
  brillo::dbus_utils::DBusObject dbus_object_;

  std::map<std::string, std::unique_ptr<ExportedInterface>>
      exported_interfaces_;

  bool is_registered_ = false;

  DISALLOW_COPY_AND_ASSIGN(ExportedObject);
};

// A wrapper of brillo::dbus_utils::ExportedObjectManager that provides a higher
// level interface of object management.
class ExportedObjectManagerWrapper {
 public:
  ExportedObjectManagerWrapper(
      scoped_refptr<dbus::Bus> bus,
      std::unique_ptr<brillo::dbus_utils::ExportedObjectManager>
          exported_object_manager);

  // Set the property handler setup callback that will be used to handle D-Bus'
  // Properties method handlers (Get/Set/GetAll).
  void SetPropertyHandlerSetupCallback(
      const brillo::dbus_utils::DBusObject::PropertyHandlerSetupCallback&
          callback);

  // Adds an exported interface |interface_name| to object |object_path|.
  // If the object |object_path| is not yet exported, it will be exported
  // automatically.
  void AddExportedInterface(const dbus::ObjectPath& object_path,
                            const std::string& interface_name);

  // Removes the previously exported interface |interface_name| from object
  // |object_path|. If there is no more exported interface to object
  // |object_path| after the removal, the object will also be unexported.
  void RemoveExportedInterface(const dbus::ObjectPath& object_path,
                               const std::string& interface_name);

  // Returns the previously added ExportedInterface or nullptr if there is no
  // such interface or object. The returned pointer is owned by this object
  // so callers should not use the pointer outside the lifespan of this object.
  ExportedInterface* GetExportedInterface(const dbus::ObjectPath& object_path,
                                          const std::string& interface_name);

 private:
  // Adds and registers an exported object. Does nothing if an exported object
  // with the same object path already exists.
  void EnsureExportedObjectRegistered(const dbus::ObjectPath& object_path);

  // Returns the exported object having the specified object path. The returned
  // pointer is owned by this object so callers should not use the pointer
  // outside the lifespan of this object.
  ExportedObject* GetExportedObject(const dbus::ObjectPath& object_path);

  // Does nothing, used to handle methods that would otherwise pollute the log
  // if unhandled.
  void HandleDoNothing(dbus::MethodCall* method_call,
                       dbus::ExportedObject::ResponseSender response_sender) {}

  scoped_refptr<dbus::Bus> bus_;

  std::unique_ptr<brillo::dbus_utils::ExportedObjectManager>
      exported_object_manager_;

  brillo::dbus_utils::DBusObject::PropertyHandlerSetupCallback
      property_handler_setup_callback_;

  std::map<std::string, std::unique_ptr<ExportedObject>> exported_objects_;

  // Must come last so that weak pointers will be invalidated before other
  // members are destroyed.
  base::WeakPtrFactory<ExportedObjectManagerWrapper> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ExportedObjectManagerWrapper);
};

}  // namespace bluetooth

#endif  // BLUETOOTH_COMMON_EXPORTED_OBJECT_MANAGER_WRAPPER_H_
