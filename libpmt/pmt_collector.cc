// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

#include <libpmt/bits/pmt_data.pb.h>
#include <libpmt/pmt_collector.h>
#include <libpmt/pmt_impl.h>

namespace pmt {

PmtCollector::PmtCollector() : intf_(new PmtSysfsData()) {}

PmtCollector::~PmtCollector() {
  CleanUpCollection();
}

PmtCollector::PmtCollector(std::unique_ptr<PmtDataInterface> intf)
    : intf_(std::move(intf)) {}

std::vector<Guid> PmtCollector::DetectDevices() {
  return intf_->DetectDevices();
}

int PmtCollector::SetUpCollection(const std::vector<Guid> guids) {
  if (data_)
    return -EBUSY;
  if (guids.empty())
    return -EINVAL;
  // First check if all requested GUIDs have been detected.
  for (const auto guid : guids) {
    if (!intf_->IsValid(guid)) {
      LOG(ERROR) << "Unrecognized GUID: 0x" << std::hex << guid;
      return -EINVAL;
    }
  }
  // Sort by GUIDs. GUIDs need to be sorted because some transformations
  // are relying on data from other devices (see the 'pkgc_block_cause'
  // transformation).
  auto sorted_guids = guids;
  std::sort(sorted_guids.begin(), sorted_guids.end());
  // Now start the initialization.
  data_ = std::make_unique<Snapshot>(Snapshot());
  // Set timestamp field, otherwise structure size will be incomplete.
  data_->set_timestamp(base::Time::Now().InMillisecondsSinceUnixEpoch());
  for (Guid guid : sorted_guids) {
    size_t size = intf_->GetTelemetrySize(guid);

    // Now setup the file context for sampling.
    std::optional<base::FilePath> telemetry_path =
        intf_->GetTelemetryFile(guid);
    if (!telemetry_path) {
      LOG(ERROR) << "No telemetry file for GUID 0x" << std::hex << guid;
      goto fail;
    }
    int fd = ::open(telemetry_path->value().c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
      PLOG(ERROR) << "Failed to open " << *telemetry_path;
      goto fail;
    }
    PmtDeviceContext ctx = {
        .telemetry_fd = fd,
    };
    auto device = data_->add_devices();
    device->set_guid(guid);
    // Pre-allocate the data buffer to be re-used on each TakeSnapshot().
    device->set_data(std::string(size, '\0'));

    ctx_.push_back(ctx);
  }

  return 0;
fail:
  CleanUpCollection();
  return -EBADF;
}

int PmtCollector::CleanUpCollection() {
  if (ctx_.empty())
    return -ENOENT;

  for (auto dev : ctx_) {
    close(dev.telemetry_fd);
  }
  ctx_.clear();
  data_.release();
  return 0;
}

int PmtCollector::TakeSnapshot() {
  // NOTE: The PMT data snapshot is backed by a protobuf message. Given that it
  // is mostly consisting of repeated fields, this means that data sample for
  // each device will be stored in potentially a separate page instead of
  // putting them all in a virtually contiguous memory region. The upside though
  // is that the (de)serialization is stable for protobuf messages, eliminating
  // the need for a hand-crafted var-array handling.

  if (!data_) {
    LOG(ERROR) << "Telemetry collector has not been setup";
    return -EPERM;
  }

  data_->set_timestamp(base::Time::Now().InMillisecondsSinceUnixEpoch());
  for (int i = 0; i < data_->devices_size(); i++) {
    // Get a non-const pointer to a device sample.
    pmt::DeviceSample* dev = data_->mutable_devices(i);
    int telemetry_fd = ctx_[i].telemetry_fd;
    auto buf = base::make_span(dev->mutable_data()->data(), dev->data().size());
    if (!base::ReadFromFD(telemetry_fd, buf)) {
      PLOG(ERROR) << "Incomplete telemetry data for 0x" << std::hex
                  << dev->guid();
      return -EIO;
    }
    // Reset the file for the next read.
    if (lseek(telemetry_fd, 0, SEEK_SET) != 0) {
      PLOG(ERROR) << "Failed to reset the telemetry file for " << dev->guid();
      return -EIO;
    }
  }
  return 0;
}

const Snapshot* PmtCollector::GetData() const {
  return data_.get();
}

}  // namespace pmt
