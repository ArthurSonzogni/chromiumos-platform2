// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/routine_service.h"

#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/routines/audio/audio_driver.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_discovery.h"
#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_pairing.h"
#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_power.h"
#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_scanning.h"
#include "diagnostics/cros_healthd/routines/fan/fan.h"
#include "diagnostics/cros_healthd/routines/hardware_button/volume_button.h"
#include "diagnostics/cros_healthd/routines/led/led_lit_up.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/cpu_cache.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/cpu_stress.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/floating_point_v2.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/memory.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/prime_search.h"
#include "diagnostics/cros_healthd/routines/storage/disk_read.h"
#include "diagnostics/cros_healthd/routines/storage/ufs_lifetime.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/ground_truth.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
using CreateRoutineResult = base::expected<std::unique_ptr<BaseRoutineControl>,
                                           mojom::SupportStatusPtr>;
using CreateRoutineCallback = base::OnceCallback<void(CreateRoutineResult)>;

template <typename RoutineType, typename... Args>
CreateRoutineResult MakeRoutineIfSupported(mojom::SupportStatusPtr status,
                                           Args&&... args) {
  if (status->is_supported()) {
    return base::ok(std::make_unique<RoutineType>(std::forward<Args>(args)...));
  }
  return base::unexpected(std::move(status));
}

CreateRoutineResult ReturnIfSupported(
    std::unique_ptr<BaseRoutineControl> routine,
    mojom::SupportStatusPtr status) {
  if (status->is_supported()) {
    return base::ok(std::move(routine));
  }
  return base::unexpected(std::move(status));
}

// Overload a `CreateRoutineHelperSync` if creation is synchronous. Otherwise,
// overload a `CreateRoutineHelper`.

CreateRoutineResult CreateRoutineHelperSync(
    Context* context, mojom::UfsLifetimeRoutineArgumentPtr arg) {
  return MakeRoutineIfSupported<UfsLifetimeRoutine>(
      context->ground_truth()->PrepareRoutineUfsLifetime(), context, arg);
}

CreateRoutineResult CreateRoutineHelperSync(Context* context,
                                            mojom::FanRoutineArgumentPtr arg) {
  return FanRoutine::Create(context, std::move(arg));
}

CreateRoutineResult CreateRoutineHelperSync(
    Context* context, mojom::DiskReadRoutineArgumentPtr arg) {
  return DiskReadRoutine::Create(context, arg);
}

CreateRoutineResult CreateRoutineHelperSync(
    Context* context, mojom::VolumeButtonRoutineArgumentPtr arg) {
  return MakeRoutineIfSupported<VolumeButtonRoutine>(
      context->ground_truth()->PrepareRoutineVolumeButton(), context, arg);
}

CreateRoutineResult CreateRoutineHelperSync(
    Context* context, mojom::LedLitUpRoutineArgumentPtr arg) {
  return MakeRoutineIfSupported<LedLitUpV2Routine>(
      context->ground_truth()->PrepareRoutineLedLitUp(), context,
      std::move(arg));
}

void CreateRoutineHelper(Context* context,
                         mojom::BluetoothPowerRoutineArgumentPtr arg,
                         CreateRoutineCallback callback) {
  context->ground_truth()->PrepareRoutineBluetoothFloss(
      base::BindOnce(&ReturnIfSupported,
                     std::make_unique<floss::BluetoothPowerRoutine>(
                         context, std::move(arg)))
          .Then(std::move(callback)));
}

void CreateRoutineHelper(Context* context,
                         mojom::BluetoothDiscoveryRoutineArgumentPtr arg,
                         CreateRoutineCallback callback) {
  context->ground_truth()->PrepareRoutineBluetoothFloss(
      base::BindOnce(&ReturnIfSupported,
                     std::make_unique<floss::BluetoothDiscoveryRoutine>(
                         context, std::move(arg)))
          .Then(std::move(callback)));
}

void CreateRoutineHelper(Context* context,
                         mojom::BluetoothPairingRoutineArgumentPtr arg,
                         CreateRoutineCallback callback) {
  context->ground_truth()->PrepareRoutineBluetoothFloss(
      base::BindOnce(&ReturnIfSupported,
                     std::make_unique<floss::BluetoothPairingRoutine>(
                         context, std::move(arg)))
          .Then(std::move(callback)));
}

void CreateRoutineHelper(Context* context,
                         mojom::BluetoothScanningRoutineArgumentPtr arg,
                         CreateRoutineCallback callback) {
  floss::BluetoothScanningRoutine::Create(context, arg, std::move(callback));
}

// Default implementation of `CreateRoutineHelperSync` raises compile error.
template <typename Arg>
CreateRoutineResult CreateRoutineHelperSync(Context* context, Arg arg) {
  static_assert(false,
                "CreateRoutineHelperSync for specific type not defined.");
  NOTREACHED_NORETURN();
}

// Default implementation of `CreateRoutineHelper` calls
// `CreateRoutineHelperSync`.
template <typename ArgumentPtr>
void CreateRoutineHelper(Context* context,
                         ArgumentPtr arg,
                         CreateRoutineCallback callback) {
  std::move(callback).Run(CreateRoutineHelperSync(context, std::move(arg)));
}

}  // namespace

RoutineService::RoutineService(Context* context) : context_(context) {
  CHECK(context_);
}

RoutineService::~RoutineService() = default;

void RoutineService::CheckAndCreateRoutine(
    mojom::RoutineArgumentPtr routine_arg,
    CheckAndCreateRoutineCallback callback) {
  switch (routine_arg->which()) {
    case mojom::RoutineArgument::Tag::kPrimeSearch: {
      auto routine = std::make_unique<PrimeSearchRoutine>(
          context_, routine_arg->get_prime_search());
      std::move(callback).Run(base::ok(std::move(routine)));
      return;
    }
    case mojom::RoutineArgument::Tag::kFloatingPoint: {
      auto routine = std::make_unique<FloatingPointRoutineV2>(
          context_, routine_arg->get_floating_point());
      std::move(callback).Run(base::ok(std::move(routine)));
      return;
    }
    case mojom::RoutineArgument::Tag::kMemory: {
      auto routine =
          std::make_unique<MemoryRoutine>(context_, routine_arg->get_memory());
      std::move(callback).Run(base::ok(std::move(routine)));
      return;
    }
    case mojom::RoutineArgument::Tag::kAudioDriver: {
      auto routine = std::make_unique<AudioDriverRoutine>(
          context_, routine_arg->get_audio_driver());
      std::move(callback).Run(base::ok(std::move(routine)));
      return;
    }
    case mojom::RoutineArgument::Tag::kCpuStress: {
      auto routine = std::make_unique<CpuStressRoutine>(
          context_, routine_arg->get_cpu_stress());
      std::move(callback).Run(base::ok(std::move(routine)));
      return;
    }
    case mojom::RoutineArgument::Tag::kUfsLifetime: {
      CreateRoutineHelper(context_, std::move(routine_arg->get_ufs_lifetime()),
                          std::move(callback));
      return;
    }
    case mojom::RoutineArgument::Tag::kDiskRead: {
      CreateRoutineHelper(context_, std::move(routine_arg->get_disk_read()),
                          std::move(callback));
      return;
    }
    case mojom::RoutineArgument::Tag::kCpuCache: {
      auto routine = std::make_unique<CpuCacheRoutine>(
          context_, routine_arg->get_cpu_cache());
      std::move(callback).Run(base::ok(std::move(routine)));
      return;
    }
    case mojom::RoutineArgument::Tag::kVolumeButton: {
      CreateRoutineHelper(context_, std::move(routine_arg->get_volume_button()),
                          std::move(callback));
      return;
    }
    case mojom::RoutineArgument::Tag::kLedLitUp: {
      CreateRoutineHelper(context_, std::move(routine_arg->get_led_lit_up()),
                          std::move(callback));
      return;
    }
    case mojom::RoutineArgument::Tag::kBluetoothPower: {
      CreateRoutineHelper(context_,
                          std::move(routine_arg->get_bluetooth_power()),
                          std::move(callback));
      return;
    }
    case mojom::RoutineArgument::Tag::kBluetoothDiscovery: {
      CreateRoutineHelper(context_,
                          std::move(routine_arg->get_bluetooth_discovery()),
                          std::move(callback));
      return;
    }
    case mojom::RoutineArgument::Tag::kFan: {
      CreateRoutineHelper(context_, std::move(routine_arg->get_fan()),
                          std::move(callback));
      return;
    }
    case mojom::RoutineArgument::Tag::kBluetoothScanning: {
      CreateRoutineHelper(context_,
                          std::move(routine_arg->get_bluetooth_scanning()),
                          std::move(callback));
      return;
    }
    case mojom::RoutineArgument::Tag::kBluetoothPairing: {
      CreateRoutineHelper(context_,
                          std::move(routine_arg->get_bluetooth_pairing()),
                          std::move(callback));
      return;
    }
    case mojom::RoutineArgument::Tag::kUnrecognizedArgument: {
      LOG(ERROR) << "Got RoutineArgument::UnrecognizedArgument";
      std::move(callback).Run(base::unexpected(
          mojom::SupportStatus::NewUnsupported(mojom::Unsupported::New(
              "Routine argument is not recognized/supported",
              /*reason=*/nullptr))));
      return;
    }
  }
}

void RoutineService::IsRoutineArgumentSupported(
    mojom::RoutineArgumentPtr routine_arg,
    mojom::CrosHealthdRoutinesService::IsRoutineArgumentSupportedCallback
        callback) {
  auto wrapped_callback =
      base::BindOnce([](CheckAndCreateRoutineResult result) {
        return result.has_value()
                   ? mojom::SupportStatus::NewSupported(mojom::Supported::New())
                   : std::move(result.error());
      }).Then(std::move(callback));
  CheckAndCreateRoutine(std::move(routine_arg), std::move(wrapped_callback));
}

void RoutineService::AddRoutine(
    std::unique_ptr<BaseRoutineControl> routine,
    mojo::PendingReceiver<mojom::RoutineControl> routine_receiver,
    mojo::PendingRemote<mojom::RoutineObserver> routine_observer) {
  auto routine_ptr = routine.get();
  mojo::ReceiverId receiver_id =
      receiver_set_.Add(std::move(routine), std::move(routine_receiver));
  routine_ptr->SetOnExceptionCallback(
      base::BindOnce(&RoutineService::OnRoutineException,
                     weak_ptr_factory_.GetWeakPtr(), receiver_id));
  if (routine_observer.is_valid()) {
    routine_ptr->SetObserver(std::move(routine_observer));
  }
}

void RoutineService::HandleCheckAndCreateRoutineResponseForCreateRoutine(
    mojo::PendingReceiver<mojom::RoutineControl> routine_receiver,
    mojo::PendingRemote<mojom::RoutineObserver> routine_observer,
    CheckAndCreateRoutineResult result) {
  if (result.has_value()) {
    AddRoutine(std::move(result.value()), std::move(routine_receiver),
               std::move(routine_observer));
    return;
  }
  switch (result.error()->which()) {
    case mojom::SupportStatus::Tag::kUnmappedUnionField:
    case mojom::SupportStatus::Tag::kSupported:
      NOTREACHED_NORETURN();
    case mojom::SupportStatus::Tag::kException:
      routine_receiver.ResetWithReason(
          static_cast<uint32_t>(mojom::Exception::Reason::kUnexpected),
          result.error()->get_exception()->debug_message);
      return;
    case mojom::SupportStatus::Tag::kUnsupported:
      routine_receiver.ResetWithReason(
          static_cast<uint32_t>(mojom::Exception::Reason::kUnsupported),
          result.error()->get_unsupported()->debug_message);
      return;
  }
}

void RoutineService::CreateRoutine(
    mojom::RoutineArgumentPtr routine_arg,
    mojo::PendingReceiver<mojom::RoutineControl> routine_receiver,
    mojo::PendingRemote<mojom::RoutineObserver> routine_observer) {
  CheckAndCreateRoutine(
      std::move(routine_arg),
      base::BindOnce(
          &RoutineService::HandleCheckAndCreateRoutineResponseForCreateRoutine,
          weak_ptr_factory_.GetWeakPtr(), std::move(routine_receiver),
          std::move(routine_observer)));
}

void RoutineService::OnRoutineException(mojo::ReceiverId receiver_id,
                                        uint32_t error,
                                        const std::string& reason) {
  if (!receiver_set_.HasReceiver(receiver_id)) {
    LOG(ERROR) << "Receiver ID not found in receiver set: " << receiver_id;
    return;
  }
  receiver_set_.RemoveWithReason(receiver_id, error, reason);
}

}  // namespace diagnostics
