// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBPMT_PMT_COLLECTOR_H_
#define LIBPMT_PMT_COLLECTOR_H_

#include <memory>
#include <vector>

#include <brillo/brillo_export.h>
#include <base/time/time.h>

#include <libpmt/bits/pmt_data_interface.h>
#include <libpmt/bits/pmt_data.pb.h>

namespace pmt {

// Processing context for the PMT device.
struct BRILLO_PRIVATE PmtDeviceContext {
  int telemetry_fd;
};

// C++ class for processing Intel PMT data.
class BRILLO_EXPORT PmtCollector {
 public:
  // Default implementation using the real filesystem to gather data.
  PmtCollector();

  // Create a PMT collector with a specified implementation of the PMT data
  // interface.
  //
  // Used for testing.
  // @param intf Implementation of the PMT data interface. Ownership is
  // transferred to the created object.
  explicit PmtCollector(std::unique_ptr<PmtDataInterface> intf);

  // Default destructor will take care of releasing any resources allocated
  // while setting up the collection.
  ~PmtCollector();

  // Detect the PMT devices on the system and return their GUIDs.
  //
  // @return The vector of GUIDs of detected devices.
  std::vector<Guid> DetectDevices();

  // Initialize collection data for the given list of devices.
  //
  // Caller should provide a list of @p guids which the library will initialize
  // and allocate internal data structures for. Only after this function
  // returns, the caller may utilize TakeSnapshot().
  //
  // @param guids List of GUIDs to setup the collection for.
  // @return 0 on success or a negative error code.
  // @retval -EINVAL One of the GUIDs was not detected on the system or no
  //         GUIDs provided.
  // @retval -EBUSY Collection was initialized already.
  // @retval -EBADF There was an issue opening PMT file descriptors.
  int SetUpCollection(const std::vector<Guid> guids);

  // Terminate any device that was set up and clean up associated data.
  //
  // @return 0 on success or a negative error code.
  // @retval -ENOENT Collection was not set up.
  int CleanUpCollection();

  // Take the snapshot of PMT data.
  //
  // Only the configured devices will be sampled and the resulting data will
  // overwrite the current data returned by GetData().
  //
  // @return 0 on success or a negative error code.
  // @retval -EIO Reading from the telemetry file failed.
  int TakeSnapshot();

  // Return the pointer to the snapshot data.
  //
  // Note that this pointer is valid and will not change between
  // SetUpCollection() and CleanUpCollection() calls.
  //
  // @return Pointer to the snapshot data or nullptr if collection was not
  // initialized.
  const Snapshot* GetData() const;

 private:
  // Interface for getting PMT data information from the system.
  std::unique_ptr<PmtDataInterface> intf_;
  // Storage for PMT data snapshots in form of a protobuf message.
  std::unique_ptr<Snapshot> data_;
  // Collection context for configured devices. The order of elements is the
  // same as in data_ for fast reference.
  std::vector<PmtDeviceContext> ctx_;
};

}  // namespace pmt

#endif  // LIBPMT_PMT_COLLECTOR_H_
