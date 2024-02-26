// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gbm.h>
#include <sys/mman.h>
#include <unistd.h>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/test/test_timeouts.h>
#include <benchmark/benchmark.h>

#include "hardware_buffer/allocator.h"

namespace cros {

constexpr int KiB = (1024);
constexpr int MiB = (1024 * 1024);

void CopyHwBufToCpuBuf(benchmark::State& state,
                       Allocator::BufferObject* hwbuf,
                       void* cpubuf,
                       size_t size) {
  for (auto _ : state) {
    CHECK(hwbuf->BeginCpuAccess(cros::SyncType::kSyncRead, 0));
    std::memcpy(cpubuf, hwbuf->GetPlaneAddr(0), size);
    CHECK(hwbuf->EndCpuAccess(cros::SyncType::kSyncRead, 0));
  }
}

void CopyCpuBufToHwBuf(benchmark::State& state,
                       void* cpubuf,
                       Allocator::BufferObject* hwbuf,
                       size_t size) {
  for (auto _ : state) {
    CHECK(hwbuf->BeginCpuAccess(cros::SyncType::kSyncWrite, 0));
    std::memcpy(hwbuf->GetPlaneAddr(0), cpubuf, size);
    CHECK(hwbuf->EndCpuAccess(cros::SyncType::kSyncWrite, 0));
  }
}

void CopyCpuBufToCpuBuf(benchmark::State& state,
                        void* from,
                        void* to,
                        size_t size) {
  for (auto _ : state) {
    std::memcpy(to, from, size);
  }
}

class MemcpyFixture {
 public:
  explicit MemcpyFixture(size_t buf_size) {
    // Allocate a buffer on CPU through malloc and fill it with random bytes.
    int page_size = getpagesize();
    rand_buffer_ = aligned_alloc(page_size, buf_size);
    CHECK(rand_buffer_);

    for (int i = 0; i < buf_size; ++i) {
      uint8_t rand = random();
      static_cast<uint8_t*>(rand_buffer_)[i] = rand;
    }
    cpu_buffer_ = aligned_alloc(page_size, buf_size);
    CHECK(cpu_buffer_);

    // Allocate a R8 blob buffer from minigbm.
    gbm_allocator_ =
        cros::Allocator::Create(cros::Allocator::Backend::kMinigbm);
    CHECK(gbm_allocator_);

    gbm_bo_ = gbm_allocator_->CreateBo(
        buf_size, 1, DRM_FORMAT_R8,
        GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN);
    CHECK(gbm_bo_);
    CHECK(gbm_bo_->Map(0));

    // Allocate a R8 blob buffer from DMA-buf heap.
    dmaheap_allocator_ =
        cros::Allocator::Create(cros::Allocator::Backend::kDmaBufHeap);
    CHECK(dmaheap_allocator_);

    dmaheap_bo_ = dmaheap_allocator_->CreateBo(
        buf_size, 1, DRM_FORMAT_R8,
        GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN);
    CHECK(dmaheap_bo_);
    CHECK(dmaheap_bo_->Map(0));
  }

  ~MemcpyFixture() {
    free(rand_buffer_);
    free(cpu_buffer_);
  }

  void* cpu_buf() { return cpu_buffer_; }
  void* rand_buf() { return rand_buffer_; }
  Allocator::BufferObject* gbm_bo() { return gbm_bo_.get(); }
  Allocator::BufferObject* dmaheap_bo() { return dmaheap_bo_.get(); }

 private:
  void* cpu_buffer_ = nullptr;
  void* rand_buffer_ = nullptr;

  std::unique_ptr<Allocator> gbm_allocator_;
  std::unique_ptr<Allocator::BufferObject> gbm_bo_;

  std::unique_ptr<Allocator> dmaheap_allocator_;
  std::unique_ptr<Allocator::BufferObject> dmaheap_bo_;
};

static void BM_CpuToMinigbm(benchmark::State& state) {
  size_t buf_size = state.range(0);
  MemcpyFixture fixture(buf_size);
  CopyCpuBufToHwBuf(state, fixture.rand_buf(), fixture.gbm_bo(), buf_size);
}

static void BM_MinigbmToCpu(benchmark::State& state) {
  size_t buf_size = state.range(0);
  MemcpyFixture fixture(buf_size);
  CopyHwBufToCpuBuf(state, fixture.gbm_bo(), fixture.cpu_buf(), buf_size);
}

static void BM_CpuToCpu(benchmark::State& state) {
  size_t buf_size = state.range(0);
  MemcpyFixture fixture(buf_size);
  CopyCpuBufToCpuBuf(state, fixture.rand_buf(), fixture.cpu_buf(), buf_size);
}

static void BM_CpuToDmaHeap(benchmark::State& state) {
  size_t buf_size = state.range(0);
  MemcpyFixture fixture(buf_size);
  CopyCpuBufToHwBuf(state, fixture.rand_buf(), fixture.dmaheap_bo(), buf_size);
}

static void BM_DmaHeapToCpu(benchmark::State& state) {
  size_t buf_size = state.range(0);
  MemcpyFixture fixture(buf_size);
  CopyHwBufToCpuBuf(state, fixture.dmaheap_bo(), fixture.cpu_buf(), buf_size);
}

static void MemcpyBenchmarkArgs(benchmark::internal::Benchmark* b) {
  b->Unit(benchmark::kMillisecond)
      ->Args({32 * KiB})
      ->Args({256 * KiB})
      ->Args({512 * KiB})
      ->Args({1 * MiB})
      ->Args({3 * MiB})
      ->Args({10 * MiB});
}

BENCHMARK(BM_CpuToMinigbm)->Apply(MemcpyBenchmarkArgs);
BENCHMARK(BM_MinigbmToCpu)->Apply(MemcpyBenchmarkArgs);
BENCHMARK(BM_CpuToCpu)->Apply(MemcpyBenchmarkArgs);
BENCHMARK(BM_CpuToDmaHeap)->Apply(MemcpyBenchmarkArgs);
BENCHMARK(BM_DmaHeapToCpu)->Apply(MemcpyBenchmarkArgs);

}  // namespace cros

// Use our own main function instead of BENCHMARK_MAIN() because we need to
// initialize libchrome test supports.
int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  TestTimeouts::Initialize();
  ::benchmark::Initialize(&argc, argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return 0;
}
