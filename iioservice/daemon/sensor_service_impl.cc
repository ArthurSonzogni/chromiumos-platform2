// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/sensor_service_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/containers/flat_map.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <libmems/iio_device.h>
#include <libmems/iio_channel.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "iioservice/daemon/sensor_metrics.h"
#include "iioservice/include/common.h"

namespace iioservice {

// Add a namespace here to not leak |DeviceHasType|.
namespace {

// Assume there won't be more than 10000 iio devices.
constexpr int32_t kFusionDeviceIdDelta = 10000;

// Prefixes for each cros::mojom::DeviceType channel.
constexpr char kChnPrefixes[][12] = {
    "",             // NONE
    "accel_",       // ACCEL
    "anglvel_",     // ANGLVEL
    "illuminance",  // LIGHT
    "count",        // COUNT
    "magn_",        // MAGN
    "angl",         // ANGL
    "pressure",     // BARO
};

bool DeviceHasType(libmems::IioDevice* iio_device,
                   cros::mojom::DeviceType type) {
  auto channels = iio_device->GetAllChannels();
  int type_int = static_cast<int>(type);
  switch (type) {
    case cros::mojom::DeviceType::ACCEL:
    case cros::mojom::DeviceType::ANGLVEL:
    case cros::mojom::DeviceType::MAGN:
      for (auto chn : channels) {
        if (strncmp(chn->GetId(), kChnPrefixes[type_int],
                    strlen(kChnPrefixes[type_int])) == 0)
          return true;
      }

      return false;

    case cros::mojom::DeviceType::LIGHT:
    case cros::mojom::DeviceType::COUNT:
    case cros::mojom::DeviceType::ANGL:
    case cros::mojom::DeviceType::BARO:
      for (auto chn : channels) {
        if (strcmp(chn->GetId(), kChnPrefixes[type_int]) == 0)
          return true;
      }

      return false;

    default:
      // TODO(chenghaogyang): Support the uncalibrated devices.
      return false;
  }
}

Location GetLocation(libmems::IioDevice* device) {
  auto location_opt = device->ReadStringAttribute(cros::mojom::kLocation);
  if (location_opt.has_value()) {
    std::string location_str = std::string(
        base::TrimString(location_opt.value(), base::StringPiece("\0\n", 2),
                         base::TRIM_TRAILING));

    if (location_str.compare(cros::mojom::kLocationBase) == 0)
      return Location::kBase;

    if (location_str.compare(cros::mojom::kLocationLid) == 0)
      return Location::kLid;

    if (location_str.compare(cros::mojom::kLocationCamera) == 0)
      return Location::kCamera;
  }

  return Location::kNone;
}

std::string LocationToString(Location location) {
  switch (location) {
    case Location::kBase:
      return cros::mojom::kLocationBase;

    case Location::kLid:
      return cros::mojom::kLocationLid;

    case Location::kCamera:
      return cros::mojom::kLocationCamera;

    default:
      return "";
  }
}

}  // namespace

// static
void SensorServiceImpl::SensorServiceImplDeleter(SensorServiceImpl* service) {
  if (service == nullptr)
    return;

  if (!service->ipc_task_runner_->RunsTasksInCurrentSequence()) {
    service->ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SensorServiceImpl::SensorServiceImplDeleter, service));
    return;
  }

  delete service;
}

// static
SensorServiceImpl::ScopedSensorServiceImpl SensorServiceImpl::Create(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    std::unique_ptr<libmems::IioContext> context) {
  DCHECK(ipc_task_runner->RunsTasksInCurrentSequence());

  auto sensor_device = SensorDeviceImpl::Create(ipc_task_runner, context.get());

  if (!sensor_device) {
    LOGF(ERROR) << "Failed to get SensorDevice";
    return ScopedSensorServiceImpl(nullptr, SensorServiceImplDeleter);
  }

  return ScopedSensorServiceImpl(
      new SensorServiceImpl(std::move(ipc_task_runner), std::move(context),
                            std::move(sensor_device)),
      SensorServiceImplDeleter);
}

SensorServiceImpl::~SensorServiceImpl() {
  for (int i = 0; i < receiver_set_.size(); ++i)
    SensorMetrics::GetInstance()->SendSensorClientDisconnected();
}

void SensorServiceImpl::AddReceiver(
    mojo::PendingReceiver<cros::mojom::SensorService> request) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  receiver_set_.Add(this, std::move(request), ipc_task_runner_);

  SensorMetrics::GetInstance()->SendSensorClientConnected();
}

void SensorServiceImpl::OnDeviceAdded(int iio_device_id) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (device_types_map_.find(iio_device_id) != device_types_map_.end()) {
    // Device is already added. Skipping.
    return;
  }

  // Reload to check if there are new devices available.
  context_->Reload();
  if (!context_->IsValid()) {
    LOGF(ERROR) << "No devices in the context";
    return;
  }

  auto device = context_->GetDeviceById(iio_device_id);
  if (!device) {
    LOGF(ERROR) << "Cannot find device by id: " << iio_device_id;
    return;
  }

  AddDevice(device);
}

void SensorServiceImpl::GetDeviceIds(cros::mojom::DeviceType type,
                                     GetDeviceIdsCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  std::vector<int32_t> ids;

  for (auto device_types : device_types_map_) {
    for (cros::mojom::DeviceType device_type : device_types.second) {
      if (device_type == type) {
        ids.push_back(device_types.first);
        break;
      }
    }
  }

  std::move(callback).Run(std::move(ids));
}

void SensorServiceImpl::GetAllDeviceIds(GetAllDeviceIdsCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>> ids(
      device_types_map_.begin(), device_types_map_.end());

  std::move(callback).Run(ids);
}

void SensorServiceImpl::GetDevice(
    int32_t iio_device_id,
    mojo::PendingReceiver<cros::mojom::SensorDevice> device_request) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (iio_device_id < kFusionDeviceIdDelta) {  // IIO device
    if (!sensor_device_) {
      LOGF(ERROR) << "No available SensorDevice";
      return;
    }

    auto it = device_types_map_.find(iio_device_id);
    if (it == device_types_map_.end()) {
      LOGF(ERROR) << "No available device with id: " << iio_device_id;
      return;
    }

    const auto& types = it->second;
    sensor_device_->AddReceiver(
        iio_device_id, std::move(device_request),
        std::set<cros::mojom::DeviceType>(types.begin(), types.end()));
  } else {  // Fusion device
    auto it = sensor_device_fusions_.find(iio_device_id);
    if (it == sensor_device_fusions_.end()) {
      LOGF(ERROR) << "Invalid iio_device_id: " << iio_device_id;
      return;
    }

    it->second->AddReceiver(std::move(device_request));
  }
}

void SensorServiceImpl::RegisterNewDevicesObserver(
    mojo::PendingRemote<cros::mojom::SensorServiceNewDevicesObserver>
        observer) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  observers_.emplace_back(
      mojo::Remote<cros::mojom::SensorServiceNewDevicesObserver>(
          std::move(observer)));
}

SensorServiceImpl::SensorServiceImpl(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    std::unique_ptr<libmems::IioContext> context,
    SensorDeviceImpl::ScopedSensorDeviceImpl sensor_device)
    : ipc_task_runner_(ipc_task_runner),
      context_(std::move(context)),
      sensor_device_(std::move(sensor_device)) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (!sensor_device_)
    LOGF(ERROR) << "Failed to get SensorDevice";

  if (context_->IsValid()) {
    for (auto device : context_->GetAllDevices())
      AddDevice(device);
  }

  receiver_set_.set_disconnect_handler(
      base::BindRepeating(&SensorServiceImpl::OnSensorServiceDisconnect,
                          weak_factory_.GetWeakPtr()));
}

void SensorServiceImpl::AddDevice(libmems::IioDevice* device) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  const int32_t id = device->GetId();
  if (!device->DisableBuffer()) {
    LOGF(ERROR) << "Permissions and ownerships hasn't been set for device: "
                << id;
    return;
  }

  std::vector<cros::mojom::DeviceType> types;
  for (int32_t i = static_cast<int32_t>(cros::mojom::DeviceType::ACCEL);
       i <= static_cast<int32_t>(cros::mojom::DeviceType::kMaxValue); ++i) {
    auto type = static_cast<cros::mojom::DeviceType>(i);
    if (DeviceHasType(device, type))
      types.push_back(type);
  }

  Location location = GetLocation(device);
  AddDevice(id, types, location);

  // Check fusion devices.
  for (const auto& type : types) {
    if (base::Contains(device_maps_[type], location)) {
      LOGF(WARNING) << "Duplicated pair of type : " << type
                    << ", and location: " << static_cast<int>(location);
      continue;
    }

    device_maps_[type][location] = id;

    // TODO(chenghaoyang): Check if we should create fusion devices based on
    // this iio device.
  }
}

void SensorServiceImpl::AddDevice(
    int32_t id,
    const std::vector<cros::mojom::DeviceType>& types,
    Location location) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  device_types_map_.emplace(id, types);

  SensorMetrics::GetInstance()->SetConfigForDevice(id, types,
                                                   LocationToString(location));
  for (auto& observer : observers_)
    observer->OnNewDeviceAdded(id, types);
}

void SensorServiceImpl::OnSensorServiceDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(INFO) << "SensorDevice disconnected. ReceiverId: "
             << receiver_set_.current_receiver();

  SensorMetrics::GetInstance()->SendSensorClientDisconnected();
}

}  // namespace iioservice
