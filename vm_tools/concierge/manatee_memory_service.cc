// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/manatee_memory_service.h"

#include <sys/socket.h>

#include <linux/vm_sockets.h>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <base/time/time.h>
#include <base/values.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vm_tools/concierge/future.h"
#include "vm_tools/concierge/vm_util.h"

namespace {
constexpr vm_tools::concierge::VmMemoryId kCrosGuestId = 0;

// The amount of memory each sibling is given to use before
// the balloon policy starts operating. The sibling's balloons
// are initialized to leave this much outside the balloon.
// TODO(stevensd): pick an appropriate, per-VM value
const int64_t kInitSiblingMemSizeMb = 1000;  // MiB

// See sirenia/src/manatee_memory_service.rs for full definitions.
struct mms_message_header {
  uint32_t len;
  uint32_t type;
};

constexpr uint32_t kGetBallonStatsId = 1;
constexpr uint32_t kRebalanceMemoryId = 2;
constexpr uint32_t kPrepareVmId = 3;
constexpr uint32_t kFinishAddVmId = 4;
constexpr uint32_t kRemoveVmId = 5;

std::optional<base::Value::Dict> DoSynchronousCall(
    base::ScopedFD& fd, uint32_t type, const base::Value::Dict& msg) {
  std::string msg_str;
  if (!msg.empty()) {
    base::JSONWriter::Write(msg, &msg_str);
  }

  std::vector<uint8_t> msg_bytes(sizeof(struct mms_message_header) +
                                 msg_str.length());

  auto header = reinterpret_cast<struct mms_message_header*>(msg_bytes.data());
  header->len = msg_str.length();
  header->type = type;
  memcpy(msg_bytes.data() + sizeof(struct mms_message_header), msg_str.data(),
         msg_str.length());

  if (!base::WriteFileDescriptor(fd.get(), msg_bytes)) {
    PLOG(ERROR) << "Failed to write message";
    return std::nullopt;
  }

  struct mms_message_header resp_header;
  if (!base::ReadFromFD(fd.get(), reinterpret_cast<char*>(&resp_header),
                        sizeof(resp_header))) {
    PLOG(ERROR) << "Failed to read header";
    return std::nullopt;
  }

  std::string resp;
  resp.resize(resp_header.len);
  if (!base::ReadFromFD(fd.get(), resp.data(), resp_header.len)) {
    PLOG(ERROR) << "Partial message from mms";
    return std::nullopt;
  }
  resp.resize(resp_header.len);

  if (resp_header.type != type) {
    LOG(ERROR) << "Unexpected response: expected=" << type
               << " actual=" << resp_header.type;
    return std::nullopt;
  }

  auto root_value = base::JSONReader::Read(resp);
  if (!root_value || !root_value->is_dict()) {
    LOG(ERROR) << "Failed to parse resp '" << resp << "'";
  }
  return std::move(root_value->GetDict());
}

bool check_simple_response(const std::optional<base::Value::Dict>& resp,
                           const char* type) {
  auto res = resp ? resp->FindInt("res") : std::nullopt;
  if (!resp || !res) {
    LOG(ERROR) << "Malformed " << type << " resp";
    return false;
  } else if (*res) {
    LOG(ERROR) << "Error " << type << " err=" << *res;
    return false;
  }
  return true;
}

}  // namespace

namespace vm_tools {
namespace concierge {

std::unique_ptr<ManateeMemoryService> ManateeMemoryService::Create(
    base::ScopedFD mms_socket) {
  auto result =
      base::WrapUnique(new ManateeMemoryService(std::move(mms_socket)));
  return result->Init() ? std::move(result) : nullptr;
}

ManateeMemoryService::ManateeMemoryService(base::ScopedFD mms_socket)
    : mms_socket_(std::move(mms_socket)), weak_ptr_factory_(this) {}

bool ManateeMemoryService::Init() {
  if (!mms_thread_.Start()) {
    LOG(ERROR) << "Failed to start mms thread";
    return false;
  }
  return true;
}

void ManateeMemoryService::GetBalloonStats(
    std::vector<VmMemoryId> ids,
    base::OnceCallback<void(TaggedBalloonStats)> stats_cb) {
  mms_thread_.task_runner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ManateeMemoryService::GetBalloonStatsOnThread,
                     base::Unretained(this), std::move(ids)),
      std::move(stats_cb));
}

TaggedBalloonStats ManateeMemoryService::GetBalloonStatsOnThread(
    std::vector<VmMemoryId> ids) {
  base::Value::Dict msg;
  base::Value::List ids_list;
  for (auto id : ids) {
    ids_list.Append(static_cast<int>(id));
  }
  msg.Set("ids", std::move(ids_list));

  auto resp = DoSynchronousCall(mms_socket_, kGetBallonStatsId, msg);
  if (!resp) {
    LOG(ERROR) << "Malformed balloon stats response";
    return TaggedBalloonStats();
  }

  auto all_stats_resp = resp->FindList("all_stats");
  if (!all_stats_resp) {
    LOG(ERROR) << "Malformed balloon stats response";
    return TaggedBalloonStats();
  }

  TaggedBalloonStats tagged_stats;
  for (const auto& resp_stats : *all_stats_resp) {
    if (!resp_stats.is_dict()) {
      LOG(ERROR) << "Malformed balloon stats response";
      return TaggedBalloonStats();
    }
    auto id = resp_stats.GetDict().FindInt("id");
    auto stats = vm_tools::concierge::ParseBalloonStats(resp_stats.GetDict());
    if (!id || !stats) {
      LOG(ERROR) << "Malformed balloon stats response";
      return TaggedBalloonStats();
    }
    tagged_stats.emplace_back(*id, *stats);
  }
  return tagged_stats;
}

void ManateeMemoryService::RebalanceMemory(
    TaggedMemoryMiBDeltas deltas, base::OnceCallback<void(bool)> rebalance_cb) {
  int64_t cros_balloon_delta = 0;
  for (auto& delta : deltas) {
    delta.second = delta.second & ~(getpagesize() - 1);
    cros_balloon_delta -= delta.second;
  }
  deltas.emplace_back(kCrosGuestId, cros_balloon_delta);

  mms_thread_.task_runner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ManateeMemoryService::RebalanceMemoryOnThread,
                     base::Unretained(this), std::move(deltas), 0),
      std::move(rebalance_cb));
}

bool ManateeMemoryService::RebalanceMemoryOnThread(TaggedMemoryMiBDeltas deltas,
                                                   int64_t reserve_delta) {
  base::Value::Dict msg;
  base::Value::List cfgs_list;
  for (auto& delta : deltas) {
    base::Value::Dict cfg_dict;
    cfg_dict.Set("id", static_cast<int>(delta.first));
    cfg_dict.Set("delta", static_cast<double>(delta.second));
    cfgs_list.Append(std::move(cfg_dict));
  }
  msg.Set("deltas", std::move(cfgs_list));

  auto resp = DoSynchronousCall(mms_socket_, kRebalanceMemoryId, msg);
  if (!resp) {
    LOG(ERROR) << "Malformed rebalance memory response";
    return false;
  }

  auto actual_deltas = resp->FindList("actual_deltas");
  if (!actual_deltas) {
    LOG(ERROR) << "Malformed rebalance memory response";
    return false;
  }

  bool full_rebalance = true;
  for (const auto& actual : *actual_deltas) {
    if (!actual.is_dict()) {
      LOG(ERROR) << "Malformed rebalance memory response";
      return false;
    }
    auto id = actual.GetDict().FindInt("id");
    auto delta = actual.GetDict().FindDouble("delta");
    if (!id || !delta) {
      LOG(ERROR) << "Malformed rebalance memory response";
      return false;
    }
    for (auto& requested : deltas) {
      if (requested.first == *id)
        full_rebalance &= (*delta == requested.second);
    }
  }

  return full_rebalance;
}

void ManateeMemoryService::LaunchVm(
    int64_t mem_size_mb,
    base::OnceCallback<bool(VmMemoryId)> start_vm_cb,
    base::OnceCallback<void(void)> stop_vm_cb,
    base::OnceCallback<void(bool)> result_cb) {
  mms_thread_.task_runner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ManateeMemoryService::LaunchVmOnThread,
                     base::Unretained(this), mem_size_mb,
                     std::move(start_vm_cb), std::move(stop_vm_cb),
                     base::SequencedTaskRunnerHandle::Get()),
      std::move(result_cb));
}

bool ManateeMemoryService::LaunchVmOnThread(
    int64_t mem_size_mb,
    base::OnceCallback<bool(VmMemoryId)> start_vm_cb,
    base::OnceCallback<void(void)> stop_vm_cb,
    scoped_refptr<base::SequencedTaskRunner> start_vm_runner) {
  const int64_t init_mem_size =
      std::min(kInitSiblingMemSizeMb, mem_size_mb) * 1024 * 1024;
  const int64_t mem_size = mem_size_mb * 1024 * 1024;

  // Try a couple of times to reserve enough memory. If we failed to reserve
  // enough memory on the previous iteration, then the system is under memory
  // pressure, so it should be working on freeing memory.
  // TODO(stevensd): Support more explicit ways to force memory reclaim.
  const int kNumTries = 5;
  VmMemoryId id;
  bool memory_reserved;
  for (int i = 0; i < kNumTries; i++) {
    // The system should be reclaiming memory, wait a little bit for progress.
    if (i != 0) {
      auto delay_ms = base::Milliseconds(i * 500);
      LOG(INFO) << "Insufficient memory reserved for VM. Retrying in "
                << delay_ms;
      base::PlatformThread::Sleep(delay_ms);
    }
    base::Value::Dict msg;
    msg.Set("mem_size", static_cast<double>(mem_size));
    msg.Set("init_mem_size", static_cast<double>(init_mem_size));

    auto resp = DoSynchronousCall(mms_socket_, kPrepareVmId, msg);
    auto res = resp ? resp->FindInt("res") : std::nullopt;
    auto ret_id = resp ? resp->FindInt("id") : std::nullopt;
    if (!resp || !res || !ret_id) {
      LOG(ERROR) << "Malformed prepare vm resp";
      break;
    }
    id = *ret_id;
    memory_reserved = *res == 0;
    if (memory_reserved)
      break;
  }

  if (memory_reserved) {
    auto future = AsyncNoReject<bool>(
        start_vm_runner, base::BindOnce(std::move(start_vm_cb), id));
    if (future.Get().val) {
      base::Value::Dict msg;
      msg.Set("id", static_cast<int>(id));

      auto resp = DoSynchronousCall(mms_socket_, kFinishAddVmId, msg);
      if (!check_simple_response(resp, "finish add VM")) {
        // The underlying failure could either be an mms failure or a crash in
        // the new VM. If it was a crash, then the normal crash monitoring could
        // end up triggering the cleanup code as well. However, that just
        // results in some extra log lines.
        LOG(ERROR) << "Tearing down partially started VM";
        start_vm_runner->PostTask(FROM_HERE, std::move(stop_vm_cb));
        return false;
      } else {
        return true;
      }
    }
    LOG(ERROR) << "Failed to launch VM";
  } else {
    LOG(ERROR) << "Unable to reserve memory for VM";
  }

  RemoveVmOnThread(id);
  return false;
}

void ManateeMemoryService::RemoveVm(VmMemoryId id) {
  mms_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&ManateeMemoryService::RemoveVmOnThread,
                                base::Unretained(this), id));
}

void ManateeMemoryService::RemoveVmOnThread(VmMemoryId id) {
  base::Value::Dict msg;
  msg.Set("id", static_cast<int>(id));

  auto resp = DoSynchronousCall(mms_socket_, kRemoveVmId, msg);
  check_simple_response(resp, "remove VM");
}

}  // namespace concierge
}  // namespace vm_tools
