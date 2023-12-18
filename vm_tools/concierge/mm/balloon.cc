// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/task/sequenced_task_runner.h>

#include "vm_tools/concierge/crosvm_control.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools::concierge::mm {
namespace {
// This is a blocking call and should only be run on the
// balloon_operations_task_runner_.
std::optional<int64_t> GetCurrentBalloonSize(std::string control_socket) {
  // Sometimes the crosvm socket can be quite slow to respond, especially when
  // memory pressure is high.
  std::optional<BalloonStats> stats = vm_tools::concierge::GetBalloonStats(
      control_socket.c_str(), base::Seconds(5));

  if (!stats) {
    return std::nullopt;
  }

  return stats->balloon_actual;
}

// This is a blocking call and should only be run on the
// balloon_operations_task_runner_.
bool SetBalloonSize(std::string control_socket, int64_t size) {
  return CrosvmControl::Get()->SetBalloonSize(control_socket.c_str(), size,
                                              std::nullopt);
}
}  // namespace

Balloon::Balloon(
    int vm_cid,
    const std::string& control_socket,
    scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner)
    : vm_cid_(vm_cid),
      control_socket_(control_socket),
      balloon_operations_task_runner_(balloon_operations_task_runner) {}

void Balloon::SetStallCallback(
    base::RepeatingCallback<void(StallStatistics, ResizeResult)>
        stall_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  stall_callback_ = stall_callback;
}

void Balloon::DoResize(
    int64_t delta_bytes,
    base::OnceCallback<void(ResizeResult)> completion_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  balloon_operations_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&GetCurrentBalloonSize, control_socket_),
      base::BindOnce(&Balloon::DoResizeInternal, weak_ptr_factory_.GetWeakPtr(),
                     delta_bytes, std::move(completion_callback)));
}

int64_t Balloon::GetTargetSize() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return target_balloon_size_;
}

base::RepeatingCallback<void(Balloon::StallStatistics, Balloon::ResizeResult)>&
Balloon::GetStallCallback() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return stall_callback_;
}

void Balloon::DoResizeInternal(
    int64_t delta_bytes,
    base::OnceCallback<void(ResizeResult)> completion_callback,
    std::optional<int64_t> current_size) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!current_size) {
    LOG(ERROR) << "Failed to get balloon size for VM: " << vm_cid_;
    std::move(completion_callback).Run(ResizeResult{});
    return;
  }

  // Before any adjustments are made, check to see if the balloon is at or above
  // its expected size. If so, then we reset the inflation rate calculation to
  // the current time and size.
  if (BalloonIsExpectedSizeOrLarger(*current_size)) {
    initial_balloon_size_ = target_balloon_size_;
    resize_time_ = base::TimeTicks::Now();
  }

  int64_t operation_base_size = *current_size;

  // Note: Resize requests that originate from the VMs (deflations) are based
  // off of PSI in the guest. Since PSI is an instantaneous measure of pressure,
  // deflations should be based off of the *actual* size of the balloon at the
  // time the request is received. Resize requests that originate from Chrome
  // (inflations) are based off of the memory pressure signal from resourced.
  // Upon receiving this signal, Chrome calculates the target memory to free
  // needed to dip below the critical memory pressure threshold. Because Chrome
  // resize requests are based off of a target value and Chrome continues to
  // send requests until the target is met, the first inflation request in a
  // series should be based on the *actual* balloon size, but subsequent
  // inflations should be based off of the *target* balloon size.
  // TODO(b:305877198) re-evaluate this when other VMs are added.
  if (target_balloon_size_ > current_size && (delta_bytes > 0)) {
    operation_base_size = target_balloon_size_;
  }

  // Can't deflate below zero, so cap deflate operations.
  if (delta_bytes < 0 && std::abs(delta_bytes) > operation_base_size) {
    delta_bytes = -operation_base_size;
  }

  int64_t new_balloon_size = operation_base_size + delta_bytes;

  // No point in resizing the balloon to its current size.
  if (new_balloon_size == current_size) {
    std::move(completion_callback)
        .Run(ResizeResult{
            .success = true,
            .actual_delta_bytes = 0,
            .new_target = *current_size,
        });
    return;
  }

  // Update the target size with the new size.
  target_balloon_size_ = new_balloon_size;

  balloon_operations_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&SetBalloonSize, control_socket_, new_balloon_size),
      base::BindOnce(&Balloon::OnSetBalloonSizeComplete,
                     weak_ptr_factory_.GetWeakPtr(), *current_size,
                     new_balloon_size, std::move(completion_callback)));
}

void Balloon::OnSetBalloonSizeComplete(
    int64_t original_size,
    int64_t new_balloon_size,
    base::OnceCallback<void(ResizeResult)> completion_callback,
    bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!success) {
    LOG(ERROR) << "Failed to set balloon size for VM: " << vm_cid_;
    std::move(completion_callback)
        .Run(ResizeResult{
            .success = false,
            .actual_delta_bytes = 0,
            .new_target = original_size,
        });
    return;
  }

  // If the balloon was inflated, and balloon stall checks are not already
  // running, post a task to check for a stall.
  if (new_balloon_size > original_size && !checking_balloon_stall_) {
    checking_balloon_stall_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Balloon::CheckForAndCorrectBalloonStall,
                       weak_ptr_factory_.GetWeakPtr()),
        kBalloonStallDetectionInterval);
  }

  std::move(completion_callback)
      .Run(ResizeResult{
          .success = true,
          .actual_delta_bytes = new_balloon_size - original_size,
          .new_target = new_balloon_size,
      });

  return;
}

void Balloon::CheckForAndCorrectBalloonStall() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  balloon_operations_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&GetCurrentBalloonSize, control_socket_),
      base::BindOnce(&Balloon::CheckForAndCorrectBalloonStallWithSize,
                     weak_ptr_factory_.GetWeakPtr()));
  return;
}

void Balloon::CheckForAndCorrectBalloonStallWithSize(
    std::optional<int64_t> current_size) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!current_size) {
    LOG(ERROR) << "Failed to get balloon size for VM: " << vm_cid_;
    return;
  }

  // If the balloon is stalled, deflate it by the backoff size and then run the
  // stall callback with the result.
  const std::optional<Balloon::StallStatistics> stall_stats =
      BalloonIsStalled(*current_size);
  if (stall_stats) {
    DoResize(-kBalloonStallBackoffSize,
             base::BindOnce(stall_callback_, *stall_stats));
  }
}

bool Balloon::BalloonIsExpectedSizeOrLarger(int64_t current_size) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (current_size >= target_balloon_size_) {
    return true;
  }

  // Note: target_balloon_size_ is guaranteed to be larger than current_size at
  // this point.
  int64_t size_delta = target_balloon_size_ - current_size;

  // Due to page granularity in the guest, the balloon may not land on the exact
  // byte size that is requested, so use a 1MiB window for the expected size.
  return size_delta < MiB(1);
}

std::optional<Balloon::StallStatistics> Balloon::BalloonIsStalled(
    int64_t current_size) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::TimeDelta time_since_resize = base::TimeTicks::Now() - resize_time_;

  // If the balloon is already at or above the expected size, then it is not
  // stalled on an inflation.
  if (BalloonIsExpectedSizeOrLarger(current_size)) {
    checking_balloon_stall_ = false;
    return std::nullopt;
  }

  // In the case where the balloon deflates itself (such as when deflate-on-oom
  // is invoked), the balloon actual size may be less than the initial balloon
  // size. When this happens the calculated inflation rate will be negative and
  // treated as a balloon stall.

  int64_t size_delta = current_size - initial_balloon_size_;

  int64_t mb_per_s = std::numeric_limits<int64_t>::max();

  if (time_since_resize.InMilliseconds() > 0) {
    mb_per_s =
        ((size_delta * 1000 / time_since_resize.InMilliseconds()) / MiB(1));
  }

  // If the time delta is small then we don't have an accurate inflation
  // rate calculation and can't be sure the balloon is stalled.
  if (time_since_resize > kBalloonStallDetectionThreshold &&
      mb_per_s < kBalloonStallRateMBps) {
    LOG(WARNING) << "Balloon stall detected for VM: " << vm_cid_
                 << " Expected: " << (target_balloon_size_ / MiB(1))
                 << "MiB Actual: " << (current_size / MiB(1)) << "MiB"
                 << " Rate: " << mb_per_s << "MiB/s ";
    checking_balloon_stall_ = false;
    return StallStatistics{mb_per_s};
  }

  // Reset the initial balloon size and resize time so the next stall detection
  // is based only on the inflation amount that occurred since this check.
  initial_balloon_size_ = current_size;
  resize_time_ = base::TimeTicks::Now();

  // The balloon isn't stalled, but it also isn't at the target size yet. Check
  // again in the future.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Balloon::CheckForAndCorrectBalloonStall,
                     weak_ptr_factory_.GetWeakPtr()),
      kBalloonStallDetectionInterval);
  return std::nullopt;
}

}  // namespace vm_tools::concierge::mm
