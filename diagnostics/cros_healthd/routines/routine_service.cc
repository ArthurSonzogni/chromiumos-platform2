// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/routine_service.h"

#include <utility>

#include "diagnostics/cros_healthd/routines/audio/audio_driver.h"
#include "diagnostics/cros_healthd/routines/memory_and_cpu/memory_v2.h"

namespace diagnostics {

namespace mojom = ::ash::cros_healthd::mojom;

RoutineService::RoutineService(Context* context) : context_(context) {}

RoutineService::~RoutineService() = default;

void RoutineService::CreateRoutine(
    mojom::RoutineArgumentPtr routine_arg,
    mojo::PendingReceiver<mojom::RoutineControl> routine_receiver) {
  switch (routine_arg->which()) {
    case mojom::RoutineArgument::Tag::kMemory:
      AddRoutine(std::make_unique<MemoryRoutineV2>(context_,
                                                   routine_arg->get_memory()),
                 std::move(routine_receiver));
      break;
    case mojom::RoutineArgument::Tag::kAudioDriver:
      AddRoutine(std::make_unique<AudioDriverRoutine>(
                     context_, routine_arg->get_audio_driver()),
                 std::move(routine_receiver));
      break;
    case mojom::RoutineArgument::Tag::kUnrecognizedArgument:
      LOG(ERROR) << "Routine Argument not recognized/supported";
      routine_receiver.ResetWithReason(
          static_cast<uint32_t>(
              mojom::RoutineControlExceptionEnum::kNotSupported),
          "Routine Argument not recognized/supported");
      break;
  }
}

void RoutineService::AddRoutine(
    std::unique_ptr<BaseRoutineControl> routine,
    mojo::PendingReceiver<mojom::RoutineControl> routine_receiver) {
  auto routine_ptr = routine.get();
  mojo::ReceiverId receiver_id =
      receiver_set_.Add(std::move(routine), std::move(routine_receiver));
  routine_ptr->SetOnExceptionCallback(
      base::BindOnce(&RoutineService::OnRoutineException,
                     weak_ptr_factory_.GetWeakPtr(), receiver_id));
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
