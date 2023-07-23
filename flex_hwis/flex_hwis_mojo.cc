// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis_mojo.h"

#include <iomanip>
#include <map>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/run_loop.h>
#include <diagnostics/mojom/public/cros_healthd.mojom.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/service_constants.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

namespace flex_hwis {

namespace {
// Since flex_hwis_tool mainly retrieves the data source from
// cros_health_tool, refer to the interaction method with mojo
// interface from: src/platform2/diagnostics/cros_health_tool/mojo_util.h.

// Gets the mojo service manager interface from the initialized global
// instances.
chromeos::mojo_service_manager::mojom::ServiceManager*
GetServiceManagerProxy() {
  static const base::NoDestructor<
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>>
      remote(chromeos::mojo_service_manager::ConnectToMojoServiceManager());

  CHECK(remote->is_bound()) << "Failed to connect to mojo service manager.";
  return remote->get();
}

// Requests a service and sets the disconnect handler to raise fatal error on
// disconnect.
void RequestMojoServiceWithDisconnectHandler(
    const std::string& service_name,
    mojo::Remote<mojom::CrosHealthdProbeService>& remote) {
  GetServiceManagerProxy()->Request(
      service_name, /*timeout=*/std::nullopt,
      remote.BindNewPipeAndPassReceiver().PassPipe());
  remote.set_disconnect_with_reason_handler(base::BindOnce(
      [](const std::string& service_name, uint32_t error,
         const std::string& reason) {
        LOG(FATAL) << "Service " << service_name
                   << " disconnected, error: " << error
                   << ", reason: " << reason;
      },
      service_name));
}

// A helper class which uses base::RunLoop to make mojo calls synchronized.
class MojoResponseWaiter {
 public:
  MojoResponseWaiter() = default;
  MojoResponseWaiter(const MojoResponseWaiter&) = delete;
  MojoResponseWaiter& operator=(const MojoResponseWaiter&) = delete;
  ~MojoResponseWaiter() = default;

  // Creates a callback to get the mojo response. Passes this to the mojo calls.
  base::OnceCallback<void(mojom::TelemetryInfoPtr)> CreateCallback() {
    return base::BindOnce(&MojoResponseWaiter::OnMojoResponseReceived,
                          weak_factory_.GetWeakPtr());
  }

  // Waits for the callback to be called and returns the response. Must be
  // called after `CreateCallback()` is used or it will block forever.
  mojom::TelemetryInfoPtr WaitForResponse() {
    run_loop_.Run();
    return std::move(data_);
  }

 private:
  void OnMojoResponseReceived(mojom::TelemetryInfoPtr response) {
    data_ = std::move(response);
    run_loop_.Quit();
  }

  // The run loop for waiting the callback to be called.
  base::RunLoop run_loop_;
  // The data to return.
  mojom::TelemetryInfoPtr data_;
  // Must be the last member.
  base::WeakPtrFactory<MojoResponseWaiter> weak_factory_{this};
};

std::string int_to_hex(uint16_t i) {
  std::stringstream stream;
  stream << std::setfill('0') << std::setw(4) << std::hex << i;
  return stream.str();
}

}  // namespace

void FlexHwisMojo::SetSystemInfo(hwis_proto::Device* data) {
  const auto& system_result = telemetry_info_->system_result;
  if (system_result && system_result->is_system_info()) {
    const auto& system_info = system_result->get_system_info();
    const mojom::BootMode boot_mode = system_info->os_info->boot_mode;
    data->mutable_dmi_info()->set_vendor(
        system_info->dmi_info->sys_vendor.value());
    data->mutable_dmi_info()->set_product_name(
        system_info->dmi_info->product_name.value());
    data->mutable_dmi_info()->set_product_version(
        system_info->dmi_info->product_version.value());
    data->mutable_bios()->set_bios_version(
        system_info->dmi_info->bios_version.value());
    data->mutable_bios()->set_secureboot(boot_mode ==
                                         mojom::BootMode::kCrosEfiSecure);
    data->mutable_bios()->set_uefi(boot_mode ==
                                       mojom::BootMode::kCrosEfiSecure ||
                                   boot_mode == mojom::BootMode::kCrosEfi);
  } else {
    LOG(INFO) << "No system telemetry info from cros_healthd service.";
  }
}

void FlexHwisMojo::SetCpuInfo(hwis_proto::Device* data) {
  const auto& cpu_result = telemetry_info_->cpu_result;
  if (cpu_result && cpu_result->is_cpu_info()) {
    const auto& cpu_info = cpu_result->get_cpu_info();
    for (const auto& physical_cpu : cpu_info->physical_cpus) {
      hwis_proto::Device_Cpu* cpu = data->add_cpu();
      cpu->set_name(physical_cpu->model_name.value());
    }
  } else {
    LOG(INFO) << "No CPU telemetry info from cros_healthd service.";
  }
}

void FlexHwisMojo::SetMemoryInfo(hwis_proto::Device* data) {
  const auto& memory_result = telemetry_info_->memory_result;
  if (memory_result && memory_result->is_memory_info()) {
    const auto& memory_info = memory_result->get_memory_info();
    data->mutable_memory()->set_total_kib(memory_info->total_memory_kib);
  } else {
    LOG(INFO) << "No memory telemetry info from cros_healthd service.";
  }
}

template <class T>
void FlexHwisMojo::SetDeviceInfo(const mojom::BusDevicePtr& device,
                                 const T& controller) {
  controller->set_name(device->vendor_name + " " + device->product_name);
  switch (device->bus_info->which()) {
    case mojom::BusInfo::Tag::kPciBusInfo: {
      const auto& pci_info = device->bus_info->get_pci_bus_info();
      controller->set_id("pci:" + int_to_hex(pci_info->vendor_id) + ":" +
                         int_to_hex(pci_info->device_id));
      controller->set_bus(hwis_proto::Device::PCI);
      controller->add_driver(pci_info->driver.value());
      break;
    }
    case mojom::BusInfo::Tag::kUsbBusInfo: {
      const auto& usb_info = device->bus_info->get_usb_bus_info();
      controller->set_id("usb:" + int_to_hex(usb_info->vendor_id) + ":" +
                         int_to_hex(usb_info->product_id));
      controller->set_bus(hwis_proto::Device::USB);
      for (const auto& interface : usb_info->interfaces) {
        controller->add_driver(interface->driver.value());
      }
      break;
    }
    case mojom::BusInfo::Tag::kThunderboltBusInfo: {
      break;
    }
    case mojom::BusInfo::Tag::kUnmappedField: {
      break;
    }
  }
}

void FlexHwisMojo::SetBusInfo(hwis_proto::Device* data) {
  const auto& bus_result = telemetry_info_->bus_result;
  if (bus_result && bus_result->is_bus_devices()) {
    const auto& devices = bus_result->get_bus_devices();
    for (const auto& device : devices) {
      switch (device->device_class) {
        case mojom::BusDeviceClass::kEthernetController: {
          SetDeviceInfo(device, data->add_ethernet_adapter());
          break;
        }
        case mojom::BusDeviceClass::kWirelessController: {
          SetDeviceInfo(device, data->add_wireless_adapter());
          break;
        }
        case mojom::BusDeviceClass::kBluetoothAdapter: {
          SetDeviceInfo(device, data->add_bluetooth_adapter());
          break;
        }
        case mojom::BusDeviceClass::kDisplayController: {
          SetDeviceInfo(device, data->add_gpu());
          break;
        }
        default:
          break;
      }
    }
  } else {
    LOG(INFO) << "No BUS telemetry info from cros_healthd service.";
  }
}

void FlexHwisMojo::SetGraphicInfo(hwis_proto::Device* data) {
  const auto& graphics_result = telemetry_info_->graphics_result;
  if (graphics_result && graphics_result->is_graphics_info()) {
    const auto& graphics_info = graphics_result->get_graphics_info();
    data->mutable_graphics_info()->set_gl_version(
        graphics_info->gles_info->version);
    data->mutable_graphics_info()->set_gl_shading_version(
        graphics_info->gles_info->shading_version);
    data->mutable_graphics_info()->set_gl_vendor(
        graphics_info->gles_info->vendor);
    data->mutable_graphics_info()->set_gl_renderer(
        graphics_info->gles_info->renderer);

    for (const auto& extension : graphics_info->gles_info->extensions) {
      data->mutable_graphics_info()->add_gl_extensions(extension);
    }
  } else {
    LOG(INFO) << "No Graphics telemetry info from cros_healthd service.";
  }
}

void FlexHwisMojo::SetInputInfo(hwis_proto::Device* data) {
  const auto& input_result = telemetry_info_->input_result;
  if (input_result && input_result->is_input_info()) {
    const auto& input_info = input_result->get_input_info();
    data->mutable_touchpad()->set_stack(input_info->touchpad_library_name);
  } else {
    LOG(INFO) << "No input telemetry info from cros_healthd service.";
  }
}

void FlexHwisMojo::SetTpmInfo(hwis_proto::Device* data) {
  const auto& tpm_result = telemetry_info_->tpm_result;
  // ChromeOS Flex supports only certain TPM 1.2 and TPM 2.0 chipsets.
  std::map<int, std::string> tpm_version_map = {{0x312e3200, "1.2"},
                                                {0x322e3000, "2.0"}};
  if (tpm_result && tpm_result->is_tpm_info()) {
    const auto& tpm_info = tpm_result->get_tpm_info();

    std::string version = "Unknown";
    if (tpm_version_map.find(tpm_info->version->family) !=
        tpm_version_map.end()) {
      version = tpm_version_map[tpm_info->version->family];
    }
    data->mutable_tpm()->set_tpm_version(version);
    data->mutable_tpm()->set_spec_level(tpm_info->version->spec_level);
    data->mutable_tpm()->set_manufacturer(tpm_info->version->manufacturer);
    data->mutable_tpm()->set_did_vid(tpm_info->did_vid.value());
    data->mutable_tpm()->set_tpm_allow_listed(
        tpm_info->supported_features->is_allowed);
    data->mutable_tpm()->set_tpm_owned(tpm_info->status->owned);
  } else {
    LOG(INFO) << "No tpm telemetry info from cros_healthd service.";
  }
}

void FlexHwisMojo::SetTelemetryInfoForTesting(mojom::TelemetryInfoPtr info) {
  telemetry_info_ = std::move(info);
}

void FlexHwisMojo::SetHwisInfo(hwis_proto::Device* data) {
  // List of hardware information categories to request from cros_healthd.
  const mojom::ProbeCategoryEnum categories[] = {
      mojom::ProbeCategoryEnum::kSystem,   mojom::ProbeCategoryEnum::kCpu,
      mojom::ProbeCategoryEnum::kMemory,   mojom::ProbeCategoryEnum::kBus,
      mojom::ProbeCategoryEnum::kGraphics, mojom::ProbeCategoryEnum::kInput,
      mojom::ProbeCategoryEnum::kTpm};

  if (telemetry_info_.is_null()) {
    std::vector<mojom::ProbeCategoryEnum> categories_to_probe;
    for (const auto& category : categories) {
      categories_to_probe.push_back(category);
    }
    // Collect hardware information from cros_healthd over mojo interface.
    mojo::Remote<mojom::CrosHealthdProbeService> remote;
    RequestMojoServiceWithDisconnectHandler(
        chromeos::mojo_services::kCrosHealthdProbe, remote);
    MojoResponseWaiter waiter;
    remote->ProbeTelemetryInfo(categories_to_probe, waiter.CreateCallback());
    telemetry_info_ = waiter.WaitForResponse();
  }

  SetSystemInfo(data);
  SetCpuInfo(data);
  SetMemoryInfo(data);
  SetBusInfo(data);
  SetGraphicInfo(data);
  SetInputInfo(data);
  SetTpmInfo(data);
}

void FlexHwisMojo::SetHwisUuid(hwis_proto::Device* data,
                               std::optional<std::string> uuid) {
  data->set_uuid(uuid.value());
}
}  // namespace flex_hwis
