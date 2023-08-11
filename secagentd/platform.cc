// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/platform.h"

#include <net/if.h>
#include <memory>
#include <utility>
#include "base/files/file_descriptor_watcher_posix.h"

namespace secagentd {
namespace {
std::unique_ptr<PlatformInterface> platform{nullptr};
}

base::WeakPtr<PlatformInterface> SetPlatform(
    std::unique_ptr<PlatformInterface> platform_in) {
  platform = std::move(platform_in);
  return platform->GetWeakPtr();
}

base::WeakPtr<PlatformInterface> GetPlatform() {
  if (platform == nullptr) {
    platform = std::make_unique<Platform>();
  }
  return platform->GetWeakPtr();
}

base::WeakPtr<PlatformInterface> Platform::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

int Platform::IfNameToIndex(const std::string& ifname) {
  return if_nametoindex(ifname.c_str());
}

int Platform::BpfMapDeleteElem(struct bpf_map* map,
                               const void* key,
                               size_t key_sz,
                               __u64 flags) {
  return bpf_map__delete_elem(map, key, key_sz, flags);
}

int Platform::BpfMapUpdateElem(const struct bpf_map* map,
                               const void* key,
                               size_t key_sz,
                               const void* value,
                               size_t value_sz,
                               __u64 flags) {
  return bpf_map__update_elem(map, key, key_sz, value, value_sz, flags);
}

int Platform::BpfMapLookupElem(const struct bpf_map* map,
                               const void* key,
                               size_t key_sz,
                               void* value,
                               size_t value_sz,
                               __u64 flags) {
  return bpf_map__lookup_elem(map, key, key_sz, value, value_sz, flags);
}

int Platform::BpfMapGetNextKey(const struct bpf_map* map,
                               const void* cur_key,
                               void* next_key,
                               size_t key_sz) {
  return bpf_map__get_next_key(map, cur_key, next_key, key_sz);
}

int Platform::LibbpfSetStrictMode(enum libbpf_strict_mode mode) {
  return libbpf_set_strict_mode(mode);
}

int Platform::BpfObjectLoadSkeleton(struct bpf_object_skeleton* s) {
  return bpf_object__load_skeleton(s);
}

int Platform::BpfObjectAttachSkeleton(struct bpf_object_skeleton* s) {
  return bpf_object__attach_skeleton(s);
}

void Platform::BpfObjectDetachSkeleton(struct bpf_object_skeleton* s) {
  bpf_object__detach_skeleton(s);
}

void Platform::BpfObjectDestroySkeleton(struct bpf_object_skeleton* s) {
  bpf_object__destroy_skeleton(s);
}

int Platform::BpfMapFd(const struct bpf_map* map) {
  return bpf_map__fd(map);
}

struct ring_buffer* Platform::RingBufferNew(
    int map_fd,
    ring_buffer_sample_fn sample_cb,
    void* ctx,
    const struct ring_buffer_opts* opts) {
  return ring_buffer__new(map_fd, sample_cb, ctx, opts);
}

int Platform::RingBufferEpollFd(const struct ring_buffer* rb) {
  return ring_buffer__epoll_fd(rb);
}

int Platform::RingBufferConsume(struct ring_buffer* rb) {
  return ring_buffer__consume(rb);
}

void Platform::RingBufferFree(struct ring_buffer* rb) {
  ring_buffer__free(rb);
}

std::unique_ptr<base::FileDescriptorWatcher::Controller>
Platform::WatchReadable(int fd, const base::RepeatingClosure& callback) {
  return base::FileDescriptorWatcher::WatchReadable(fd, callback);
}
}  // namespace secagentd
