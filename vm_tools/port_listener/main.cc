// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/socket.h>

#include <linux/vm_sockets.h>  // Needs to come after sys/socket.h

#include <memory>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/stringprintf.h>
#include <bpf/libbpf.h>
#include <bpf/libbpf_legacy.h>
#include <chromeos/constants/vm_tools.h>
#include <grpcpp/grpcpp.h>
#include <vm_protos/proto_bindings/common.pb.h>
#include <vm_protos/proto_bindings/tremplin.grpc.pb.h>

#include "vm_tools/port_listener/bpf/generated/skeleton_listen_tracker.ebpf.h"
#include "vm_tools/port_listener/common.h"

typedef std::unordered_map<int, int> port_usage_map;

namespace port_listener {
namespace {

int HandleEvent(void* ctx, void* const data, size_t size) {
  port_usage_map* map = reinterpret_cast<port_usage_map*>(ctx);
  const struct event* ev = (struct event*)data;

  switch (ev->state) {
    case kPortListenerUp:
      (*map)[ev->port]++;
      break;

    case kPortListenerDown:
      if ((*map)[ev->port] > 0) {
        (*map)[ev->port]--;
      } else {
        LOG(INFO) << "Received down event while port count was 0; ignoring";
      }

      break;

    default:
      LOG(ERROR) << "Unknown event state " << ev->state;
  }

  LOG(INFO) << "Listen event: port=" << ev->port << " state=" << ev->state;

  return 0;
}

typedef std::unique_ptr<struct ring_buffer, decltype(&ring_buffer__free)>
    ring_buffer_ptr;
typedef std::unique_ptr<listen_tracker_ebpf,
                        decltype(&listen_tracker_ebpf__destroy)>
    listen_tracker_ptr;

// BPFProgram tracks the state and resources of the listen_tracker BPF program.
class BPFProgram {
 public:
  // Default movable but not copyable.
  BPFProgram(BPFProgram&& other) = default;
  BPFProgram(const BPFProgram& other) = delete;
  BPFProgram& operator=(BPFProgram&& other) = default;
  BPFProgram& operator=(const BPFProgram& other) = delete;

  // Load loads the listen_tracker BPF program and prepares it for polling. On
  // error nullptr is returned.
  static std::unique_ptr<BPFProgram> Load() {
    auto* skel = listen_tracker_ebpf__open();
    if (!skel) {
      PLOG(ERROR) << "Failed to open listen_tracker BPF skeleton";
      return nullptr;
    }
    listen_tracker_ptr skeleton(skel, listen_tracker_ebpf__destroy);

    int err = listen_tracker_ebpf__load(skeleton.get());
    if (err) {
      PLOG(ERROR) << "Failed to load listen_tracker BPF program";
      return nullptr;
    }

    auto map = std::make_unique<port_usage_map>();
    auto* rb = ring_buffer__new(bpf_map__fd(skel->maps.events), HandleEvent,
                                map.get(), NULL);
    if (!rb) {
      PLOG(ERROR) << "Failed to open ring buffer for listen_tracker";
      return nullptr;
    }
    ring_buffer_ptr ringbuf(rb, ring_buffer__free);

    err = listen_tracker_ebpf__attach(skeleton.get());
    if (err) {
      PLOG(ERROR) << "Failed to attach listen_tracker";
      return nullptr;
    }

    return base::WrapUnique(new BPFProgram(std::move(skeleton),
                                           std::move(ringbuf), std::move(map)));
  }

  // Poll waits for the listen_tracker BPF program to post a new event to the
  // ring buffer. BPFProgram handles integrating this new event into the
  // port_usage map and callers should consult port_usage() after Poll returns
  // for the latest data.
  const bool Poll() {
    int err = ring_buffer__poll(rb_.get(), -1);
    if (err < 0) {
      LOG(ERROR) << "Error polling ring buffer ret=" << err;
      return false;
    }

    return true;
  }

  const port_usage_map& port_usage() { return *port_usage_; }

 private:
  BPFProgram(listen_tracker_ptr&& skeleton,
             ring_buffer_ptr&& rb,
             std::unique_ptr<port_usage_map>&& port_usage)
      : skeleton_(std::move(skeleton)),
        rb_(std::move(rb)),
        port_usage_(std::move(port_usage)) {}

  listen_tracker_ptr skeleton_;
  ring_buffer_ptr rb_;
  std::unique_ptr<port_usage_map> port_usage_;
};

}  // namespace
}  // namespace port_listener

int main(int argc, char** argv) {
  logging::InitLogging(logging::LoggingSettings());
  libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

  // Load our BPF program.
  auto program = port_listener::BPFProgram::Load();
  if (program == nullptr) {
    LOG(ERROR) << "Failed to load BPF program";
    return EXIT_FAILURE;
  }

  // Connect back to TremplinListener
  vm_tools::tremplin::TremplinListener::Stub tremplin_listener(
      grpc::CreateChannel(base::StringPrintf("vsock:%u:%u", VMADDR_CID_HOST,
                                             vm_tools::kTremplinListenerPort),
                          grpc::InsecureChannelCredentials()));

  // main loop: poll for listen updates, when an update comes send an rpc to
  // tremplin listener letting it know.
  for (;;) {
    if (!program->Poll()) {
      LOG(ERROR) << "Failure while polling BPF program";
      return EXIT_FAILURE;
    }
    // port_usage will be updated with the latest usage data

    vm_tools::tremplin::ListeningPortInfo_ContainerPortInfo cpi;
    for (auto it : program->port_usage()) {
      if (it.second <= 0) {
        continue;
      }
      cpi.add_listening_tcp4_ports(it.first);
    }

    vm_tools::tremplin::ListeningPortInfo lpi;
    (*lpi.mutable_container_ports())["penguin"] = cpi;

    grpc::ClientContext ctx;
    vm_tools::tremplin::EmptyMessage empty;
    grpc::Status status =
        tremplin_listener.UpdateListeningPorts(&ctx, lpi, &empty);
    if (!status.ok()) {
      LOG(WARNING) << "Failed to notify tremplin of new listening ports: "
                   << status.error_message();
    }
  }
}
