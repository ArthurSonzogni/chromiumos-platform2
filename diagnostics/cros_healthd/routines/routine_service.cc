// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/routine_service.h"

#include <utility>

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/routines/audio/audio_driver.h"
#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_power_v2.h"
#include "diagnostics/cros_healthd/routines/hardware_button/volume_button.h"
#include "diagnostics/cros_healthd/routines/led/led_lit_up.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/cpu_cache.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/cpu_stress.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/floating_point_v2.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/memory.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/prime_search.h"
#include "diagnostics/cros_healthd/routines/storage/disk_read.h"
#include "diagnostics/cros_healthd/routines/storage/ufs_lifetime.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {

namespace mojom = ::ash::cros_healthd::mojom;

RoutineService::RoutineService(Context* context) : context_(context) {
  CHECK(context_);
  ground_truth_ = std::make_unique<GroundTruth>(context_);
}

RoutineService::~RoutineService() = default;

void RoutineService::CreateRoutine(
    mojom::RoutineArgumentPtr routine_arg,
    mojo::PendingReceiver<mojom::RoutineControl> routine_receiver,
    mojo::PendingRemote<mojom::RoutineObserver> routine_observer) {
  auto routine_control =
      CreateRoutineControl(std::move(routine_arg), routine_receiver);
  if (!routine_control) {
    return;
  }

  AddRoutine(std::move(routine_control), std::move(routine_receiver),
             std::move(routine_observer));
}

std::unique_ptr<BaseRoutineControl> RoutineService::CreateRoutineControl(
    ash::cros_healthd::mojom::RoutineArgumentPtr routine_arg,
    mojo::PendingReceiver<mojom::RoutineControl>& routine_receiver) {
  switch (routine_arg->which()) {
    case mojom::RoutineArgument::Tag::kPrimeSearch:
      return std::make_unique<PrimeSearchRoutine>(
          context_, routine_arg->get_prime_search());
    case mojom::RoutineArgument::Tag::kFloatingPoint:
      return std::make_unique<FloatingPointRoutineV2>(
          context_, routine_arg->get_floating_point());
    case mojom::RoutineArgument::Tag::kMemory:
      return std::make_unique<MemoryRoutine>(context_,
                                             routine_arg->get_memory());
    case mojom::RoutineArgument::Tag::kAudioDriver:
      return std::make_unique<AudioDriverRoutine>(
          context_, routine_arg->get_audio_driver());
    case mojom::RoutineArgument::Tag::kCpuStress:
      return std::make_unique<CpuStressRoutine>(context_,
                                                routine_arg->get_cpu_stress());
    case mojom::RoutineArgument::Tag::kUfsLifetime:
      return std::make_unique<UfsLifetimeRoutine>(
          context_, routine_arg->get_ufs_lifetime());
    case mojom::RoutineArgument::Tag::kDiskRead: {
      auto routine =
          DiskReadRoutine::Create(context_, routine_arg->get_disk_read());
      if (routine.has_value()) {
        return std::move(routine.value());
      }
      routine_receiver.ResetWithReason(
          static_cast<uint32_t>(mojom::Exception::Reason::kUnsupported),
          routine.error());

      return nullptr;
    }
    case mojom::RoutineArgument::Tag::kCpuCache:
      return std::make_unique<CpuCacheRoutine>(context_,
                                               routine_arg->get_cpu_cache());
    case mojom::RoutineArgument::Tag::kVolumeButton:
      return std::make_unique<VolumeButtonRoutine>(
          context_, routine_arg->get_volume_button());
    case mojom::RoutineArgument::Tag::kLedLitUp:
      return std::make_unique<LedLitUpV2Routine>(
          context_, std::move(routine_arg->get_led_lit_up()));
    case mojom::RoutineArgument::Tag::kBluetoothPower:
      return std::make_unique<BluetoothPowerRoutineV2>(
          context_, std::move(routine_arg->get_bluetooth_power()));
    case mojom::RoutineArgument::Tag::kUnrecognizedArgument: {
      LOG(ERROR) << "Routine Argument not recognized/supported";
      routine_receiver.ResetWithReason(
          static_cast<uint32_t>(mojom::Exception::Reason::kUnsupported),
          "Routine Argument not recognized/supported");
      return nullptr;
    }
  }
}

void RoutineService::IsRoutineArgumentSupported(
    mojom::RoutineArgumentPtr routine_arg,
    mojom::CrosHealthdRoutinesService::IsRoutineArgumentSupportedCallback
        callback) {
  ground_truth_->IsRoutineArgumentSupported(std::move(routine_arg),
                                            std::move(callback));
}

void RoutineService::AddRoutine(
    std::unique_ptr<BaseRoutineControl> routine,
    mojo::PendingReceiver<mojom::RoutineControl> routine_receiver,
    mojo::PendingRemote<ash::cros_healthd::mojom::RoutineObserver>
        routine_observer) {
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
