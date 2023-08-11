// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_TEST_MOCK_PLATFORM_H_
#define SECAGENTD_TEST_MOCK_PLATFORM_H_

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "secagentd/platform.h"
namespace secagentd::testing {
class MockPlatform : public PlatformInterface {
 public:
  MockPlatform() : weak_ptr_factory_(this) {}
  base::WeakPtr<PlatformInterface> GetWeakPtr() override {
    return weak_ptr_factory_.GetWeakPtr();
  }

  MOCK_METHOD(int, IfNameToIndex, (const std::string& ifname), (override));

  MOCK_METHOD(int,
              BpfMapDeleteElem,
              (struct bpf_map*, const void* key, size_t key_sz, __u64 flags),
              (override));

  MOCK_METHOD(int,
              BpfMapUpdateElem,
              (const struct bpf_map* map,
               const void* key,
               size_t key_sz,
               const void* value,
               size_t value_sz,
               __u64 flags),
              (override));

  MOCK_METHOD(int,
              BpfMapLookupElem,
              (const struct bpf_map* map,
               const void* key,
               size_t key_sz,
               void* value,
               size_t value_sz,
               __u64 flags),
              (override));

  MOCK_METHOD(int,
              BpfMapGetNextKey,
              (const struct bpf_map* map,
               const void* cur_key,
               void* next_key,
               size_t key_sz),
              (override));

  MOCK_METHOD(int,
              LibbpfSetStrictMode,
              (enum libbpf_strict_mode mode),
              (override));

  MOCK_METHOD(int,
              BpfObjectLoadSkeleton,
              (struct bpf_object_skeleton * s),
              (override));
  MOCK_METHOD(int,
              BpfObjectAttachSkeleton,
              (struct bpf_object_skeleton * s),
              (override));
  MOCK_METHOD(void,
              BpfObjectDetachSkeleton,
              (struct bpf_object_skeleton * s),
              (override));
  MOCK_METHOD(void,
              BpfObjectDestroySkeleton,
              (struct bpf_object_skeleton * s),
              (override));
  MOCK_METHOD(int, BpfMapFd, (const struct bpf_map* map), (override));
  MOCK_METHOD(struct ring_buffer*,
              RingBufferNew,
              (int map_fd,
               ring_buffer_sample_fn sample_cb,
               void* ctx,
               const struct ring_buffer_opts* opts),
              (override));
  MOCK_METHOD(int,
              RingBufferEpollFd,
              (const struct ring_buffer* rb),
              (override));
  MOCK_METHOD(int, RingBufferConsume, (struct ring_buffer * rb), (override));
  MOCK_METHOD(void, RingBufferFree, (struct ring_buffer * rb), (override));

  MOCK_METHOD(std::unique_ptr<base::FileDescriptorWatcher::Controller>,
              WatchReadable,
              (int fd, const base::RepeatingClosure& callback),
              (override));

 private:
  base::WeakPtrFactory<MockPlatform> weak_ptr_factory_;
};
}  // namespace secagentd::testing
#endif  // SECAGENTD_TEST_MOCK_PLATFORM_H_
