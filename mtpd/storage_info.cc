// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtpd/storage_info.h"

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "system_api/proto_bindings/mtp_storage_info.pb.h"

namespace mtpd {

StorageInfo::StorageInfo(const std::string& storage_name,
                         const LIBMTP_device_entry_t& device,
                         const LIBMTP_devicestorage_t& storage,
                         const std::string& fallback_vendor,
                         const std::string& fallback_product,
                         const std::string& serial_number)
    : storage_name_(storage_name),
      vendor_id_(device.vendor_id),
      product_id_(device.product_id),
      device_flags_(device.device_flags),
      storage_type_(storage.StorageType),
      filesystem_type_(storage.FilesystemType),
      access_capability_(storage.AccessCapability),
      max_capacity_(storage.MaxCapacity),
      free_space_in_bytes_(storage.FreeSpaceInBytes),
      free_space_in_objects_(storage.FreeSpaceInObjects),
      serial_number_(serial_number) {
  vendor_ = device.vendor ? device.vendor : fallback_vendor;
  product_ = device.product ? device.product : fallback_product;
  if (storage.StorageDescription) {
    storage_description_ = storage.StorageDescription;
  }
  if (storage.VolumeIdentifier) {
    volume_identifier_ = storage.VolumeIdentifier;
  }
}

StorageInfo::StorageInfo()
    : vendor_id_(0),
      product_id_(0),
      device_flags_(0),
      storage_type_(0),
      filesystem_type_(0),
      access_capability_(0),
      max_capacity_(0),
      free_space_in_bytes_(0),
      free_space_in_objects_(0) {}

StorageInfo::~StorageInfo() {}

void StorageInfo::Update(const LIBMTP_devicestorage_t& storage) {
  storage_type_ = storage.StorageType;
  filesystem_type_ = storage.FilesystemType;
  access_capability_ = storage.AccessCapability;
  max_capacity_ = storage.MaxCapacity;
  free_space_in_bytes_ = storage.FreeSpaceInBytes;
  free_space_in_objects_ = storage.FreeSpaceInObjects;
  if (storage.StorageDescription) {
    storage_description_ = storage.StorageDescription;
  }
  if (storage.VolumeIdentifier) {
    volume_identifier_ = storage.VolumeIdentifier;
  }
}

std::vector<uint8_t> StorageInfo::ToDBusFormat() const {
  MtpStorageInfo protobuf;
  protobuf.set_storage_name(storage_name_);
  protobuf.set_vendor(vendor_);
  protobuf.set_vendor_id(vendor_id_);
  protobuf.set_product(product_);
  protobuf.set_product_id(product_id_);
  protobuf.set_device_flags(device_flags_);
  protobuf.set_storage_type(storage_type_);
  protobuf.set_filesystem_type(filesystem_type_);
  protobuf.set_access_capability(access_capability_);
  protobuf.set_max_capacity(max_capacity_);
  protobuf.set_free_space_in_bytes(free_space_in_bytes_);
  protobuf.set_free_space_in_objects(free_space_in_objects_);
  protobuf.set_storage_description(storage_description_);
  protobuf.set_volume_identifier(volume_identifier_);
  protobuf.set_serial_number(serial_number_);

  size_t size = protobuf.ByteSizeLong();
  std::vector<uint8_t> serialized_proto;
  serialized_proto.resize(size);
  CHECK_GT(size, 0);
  CHECK(protobuf.SerializeToArray(&serialized_proto.front(), size));
  return serialized_proto;
}

}  // namespace mtpd
