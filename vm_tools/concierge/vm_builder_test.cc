// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_builder.h"

#include <optional>
#include <utility>

#include <base/files/scoped_file.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace vm_tools::concierge {

namespace {

std::string JoinStringPairs(const base::StringPairs& pairs) {
  std::string result;
  for (auto& pair : pairs) {
    result += (pair.first + "=" + pair.second + " ");
  }
  return result;
}

}  // namespace

TEST(VmBuilderTest, DefaultValuesSucceeds) {
  VmBuilder builder;
  EXPECT_FALSE(std::move(builder).BuildVmArgs(nullptr)->empty());
}

TEST(VmBuilderTest, CustomParametersWithCrosvmFlags) {
  CustomParametersForDev dev{R"(prerun:--log-level=debug)"};

  VmBuilder builder;
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();

  EXPECT_EQ(result[0].first, "/usr/bin/crosvm");
  EXPECT_EQ(result[1].first, "--log-level");
  EXPECT_EQ(result[1].second, "debug");
  EXPECT_EQ(result[2].first, "run");
}

TEST(VmBuilderTest, CustomParametersWithSyslogTag) {
  CustomParametersForDev dev{R"(prerun:--log-level=debug)"};

  VmBuilder builder;
  builder.SetSyslogTag("TEST");
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();

  EXPECT_EQ(result[0].first, "/usr/bin/crosvm");
  EXPECT_EQ(result[1].first, "--syslog-tag");
  EXPECT_EQ(result[1].second, "TEST");
  EXPECT_EQ(result[2].first, "--log-level");
  EXPECT_EQ(result[2].second, "debug");
  EXPECT_EQ(result[3].first, "run");
}

TEST(VmBuilderTest, CustomParametersWithStrace) {
  CustomParametersForDev dev{
      R"(precrosvm:/usr/local/bin/strace
precrosvm:-f
precrosvm:-o=/run/vm/crosvm_strace)"};

  VmBuilder builder;
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();
  EXPECT_EQ(result[0].first, "/usr/local/bin/strace");
  EXPECT_EQ(result[1].first, "-f");
  EXPECT_EQ(result[1].second, "");
  // We can't do preprocessing on the precrosvm arguments, so let it just pass
  // through.
  EXPECT_EQ(result[2].first, "-o=/run/vm/crosvm_strace");
  EXPECT_EQ(result[2].second, "");
  EXPECT_EQ(result[3].first, "/usr/bin/crosvm");
  EXPECT_EQ(result[4].first, "run");
}

TEST(VmBuilderTest, ODirectN) {
  CustomParametersForDev dev{R"(O_DIRECT_N=2)"};

  base::ScopedFD fd0(open("/dev/null", O_RDONLY));
  base::ScopedFD fd1(open("/dev/null", O_RDONLY));
  base::ScopedFD fd2(open("/dev/null", O_RDONLY));
  int raw_fd0 = fd0.get();
  int raw_fd1 = fd1.get();
  int raw_fd2 = fd2.get();

  VmBuilder builder;
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd0),
  });
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd1),
  });
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd2),
  });
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();

  std::vector<std::string> disk_params;
  for (auto& p : result) {
    if (p.first == "--block") {
      disk_params.push_back(p.second);
    }
  }

  EXPECT_EQ(disk_params[0],
            base::StringPrintf("/proc/self/fd/%d,ro=true", raw_fd0));
  EXPECT_EQ(disk_params[1],
            base::StringPrintf("/proc/self/fd/%d,ro=true", raw_fd1));
  EXPECT_EQ(
      disk_params[2],
      base::StringPrintf(
          "/proc/self/fd/%d,ro=true,o_direct=true,block_size=4096", raw_fd2));
}

TEST(VmBuilderTest, ODirectNs) {
  CustomParametersForDev dev{R"(O_DIRECT_N=1
O_DIRECT_N=2)"};

  base::ScopedFD fd0(open("/dev/null", O_RDONLY));
  base::ScopedFD fd1(open("/dev/null", O_RDONLY));
  base::ScopedFD fd2(open("/dev/null", O_RDONLY));
  int raw_fd0 = fd0.get();
  int raw_fd1 = fd1.get();
  int raw_fd2 = fd2.get();

  VmBuilder builder;
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd0),
  });
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd1),
  });
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd2),
  });
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();

  std::vector<std::string> disk_params;
  for (auto& p : result) {
    if (p.first == "--block") {
      disk_params.push_back(p.second);
    }
  }

  EXPECT_EQ(disk_params[0],
            base::StringPrintf("/proc/self/fd/%d,ro=true", raw_fd0));
  EXPECT_EQ(
      disk_params[1],
      base::StringPrintf(
          "/proc/self/fd/%d,ro=true,o_direct=true,block_size=4096", raw_fd1));
  EXPECT_EQ(
      disk_params[2],
      base::StringPrintf(
          "/proc/self/fd/%d,ro=true,o_direct=true,block_size=4096", raw_fd2));
}

TEST(VmBuilderTest, ODirectTooLargeNDeath) {
  CustomParametersForDev dev{R"(O_DIRECT_N=15)"};
  VmBuilder builder;
  ASSERT_DEATH(
      {
        base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();
      },
      "out_of_range");
}

TEST(VmBuilderTest, DefaultKernel) {
  base::ScopedFD kernel_fd(open("/dev/null", O_RDONLY));
  int raw_fd = kernel_fd.get();

  VmBuilder builder;
  builder.SetKernel(std::move(kernel_fd));
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  EXPECT_EQ(result[result.size() - 1].first,
            base::StringPrintf("/proc/self/fd/%d", raw_fd));
}

TEST(VmBuilderTest, CustomKernel) {
  CustomParametersForDev dev{R"(KERNEL_PATH=/dev/zero)"};

  base::ScopedFD kernel_fd(open("/dev/null", O_RDONLY));
  int orig_fd = kernel_fd.get();
  VmBuilder builder;
  builder.SetKernel(std::move(kernel_fd));
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();

  EXPECT_THAT(result[result.size() - 1].first,
              testing::MatchesRegex("/proc/self/fd/[0-9]+"));
  EXPECT_NE(result[result.size() - 1].first,
            base::StringPrintf("/proc/self/fd/%d", orig_fd));
}

TEST(VmBuilderTest, SingleTapNetParams) {
  base::ScopedFD fake_fd(open("/dev/zero", O_RDONLY));
  int raw_fd = fake_fd.get();

  VmBuilder builder;
  builder.AppendTapFd(std::move(fake_fd));
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  EXPECT_THAT(
      result,
      testing::Contains(
          std::make_pair("--net", base::StringPrintf(
                                      "packed-queue=true,tap-fd=%d", raw_fd)))
          .Times(1));
}

TEST(VmBuilderTest, MultipleTapNetParams) {
  base::ScopedFD fake_fd_1(open("/dev/zero", O_RDONLY));
  base::ScopedFD fake_fd_2(open("/dev/zero", O_RDONLY));
  int raw_fd_1 = fake_fd_1.get();
  int raw_fd_2 = fake_fd_2.get();

  VmBuilder builder;
  builder.AppendTapFd(std::move(fake_fd_1));
  builder.AppendTapFd(std::move(fake_fd_2));
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  EXPECT_THAT(
      result,
      testing::Contains(
          std::make_pair("--net", base::StringPrintf(
                                      "packed-queue=true,tap-fd=%d", raw_fd_1)))
          .Times(1));
  EXPECT_THAT(
      result,
      testing::Contains(
          std::make_pair("--net", base::StringPrintf(
                                      "packed-queue=true,tap-fd=%d", raw_fd_2)))
          .Times(1));
}

TEST(VmBuilderTest, LaunchSpecWithTapFds) {
  base::ScopedFD fake_fd(open("/dev/zero", O_RDONLY | O_CLOEXEC));
  int raw_fd = fake_fd.get();

  VmBuilder builder;
  builder.AppendTapFd(std::move(fake_fd));
  auto spec = std::move(builder).BuildVmLaunchSpec(nullptr);
  ASSERT_TRUE(spec.has_value());

  EXPECT_EQ(spec->fds.size(), 1);
  EXPECT_EQ(spec->fds[0].get(), raw_fd);
  EXPECT_EQ(fcntl(raw_fd, F_GETFD) & FD_CLOEXEC, 0);
}

TEST(VmBuilderTest, PmemDevice) {
  VmBuilder builder;
  builder.AppendPmemDevice(VmBuilder::PmemDevice{
      .path = "/dev/read_only_pmem",
  });
  builder.AppendPmemDevice(VmBuilder::PmemDevice{
      .path = "/dev/writable_pmem",
      .writable = true,
  });
  builder.AppendPmemDevice(
      VmBuilder::PmemDevice{.path = "virtual_swap_pmem",
                            .writable = true,
                            .vma_size = std::optional{1024},
                            .swap_interval_ms = std::optional{1000}});
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  std::vector<std::string> pmem_params;
  for (auto& p : result) {
    if (p.first.contains("pmem")) {
      pmem_params.push_back(p.first + " " + p.second);
    }
  }

  EXPECT_EQ(pmem_params.size(), 3);
  EXPECT_EQ(pmem_params[0], "--pmem /dev/read_only_pmem,ro=true");
  EXPECT_EQ(pmem_params[1], "--pmem /dev/writable_pmem,ro=false");
  EXPECT_EQ(
      pmem_params[2],
      "--pmem virtual_swap_pmem,ro=false,vma-size=1024,swap-interval-ms=1000");
}

TEST(VmBuilderTest, CrostiniDisks) {
  base::ScopedFD fd0(open("/dev/null", O_RDONLY));
  base::ScopedFD fd1(open("/dev/null", O_RDONLY));
  int raw_fd0 = fd0.get();
  int raw_fd1 = fd1.get();

  VmBuilder builder;
  // For rootfs.
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd0),
  });
  // For user data.
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd1),
      .writable = true,
      .sparse = false,
  });
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  std::vector<std::string> blocks;
  for (auto& p : result) {
    if (p.first == "--block") {
      blocks.push_back(p.second);
    }
  }

  EXPECT_EQ(blocks.size(), 2);
  EXPECT_EQ(blocks[0], base::StringPrintf("/proc/self/fd/%d,ro=true", raw_fd0));
  EXPECT_EQ(blocks[1], base::StringPrintf(
                           "/proc/self/fd/%d,ro=false,sparse=false", raw_fd1));
}

TEST(VmBuilderTest, ARCVMDisks) {
  base::ScopedFD fd0(open("/dev/null", O_RDONLY));
  base::ScopedFD fd1(open("/dev/null", O_RDONLY));
  base::ScopedFD fd2(open("/dev/null", O_RDONLY));
  int raw_fd0 = fd0.get();
  int raw_fd1 = fd1.get();
  int raw_fd2 = fd2.get();

  VmBuilder builder;
  // For system.img and vendor.img.
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd0),
      .o_direct = true,
      .block_size = 4096,
  });
  // For dummy fds.
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd1),
      .o_direct = false,
  });
  // For user data image.
  builder.AppendDisk(VmBuilder::Disk{
      .fd = std::move(fd2),
      .writable = true,
      .o_direct = true,
      .block_size = 4096,
  });
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  std::vector<std::string> blocks;
  for (auto& p : result) {
    if (p.first == "--block") {
      blocks.push_back(p.second);
    }
  }

  EXPECT_EQ(blocks.size(), 3);
  EXPECT_EQ(
      blocks[0],
      base::StringPrintf(
          "/proc/self/fd/%d,ro=true,o_direct=true,block_size=4096", raw_fd0));
  EXPECT_EQ(blocks[1], base::StringPrintf(
                           "/proc/self/fd/%d,ro=true,o_direct=false", raw_fd1));
  EXPECT_EQ(
      blocks[2],
      base::StringPrintf(
          "/proc/self/fd/%d,ro=false,o_direct=true,block_size=4096", raw_fd2));
}

TEST(VmBuilderTest, VmCpuArgs) {
  VmBuilder::VmCpuArgs vm_cpu_args = {
      .cpu_affinity = {"0=0,1:1=0,1:2=2,3:3=2,3"},
      .cpu_capacity = {"0=741", "1=741", "2=1024", "3=1024"},
      .cpu_clusters = {{"0", "1"}, {}, {"2", "3"}},
  };

  VmBuilder builder;
  builder.SetVmCpuArgs(vm_cpu_args);
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  EXPECT_THAT(result,
              testing::Contains(
                  std::make_pair("--cpu-affinity", "0=0,1:1=0,1:2=2,3:3=2,3"))
                  .Times(1));
  EXPECT_THAT(result,
              testing::Contains(
                  std::make_pair("--cpu-capacity", "0=741,1=741,2=1024,3=1024"))
                  .Times(1));

  EXPECT_THAT(
      result,
      testing::Contains(std::make_pair("--cpu-cluster", "0,1")).Times(1));

  // The empty CPU cluster should not produce a cluster argument.
  EXPECT_THAT(
      result,
      testing::Not(testing::Contains(std::make_pair("--cpu-cluster", ""))));

  EXPECT_THAT(
      result,
      testing::Contains(std::make_pair("--cpu-cluster", "2,3")).Times(1));
}

TEST(VmBuilderTest, BlockMultipleWorkers) {
  // multiple_workers option is not enabled by default
  VmBuilder::Disk disk{.fd = base::ScopedFD(open("/dev/null", O_RDONLY))};
  EXPECT_FALSE(
      JoinStringPairs(disk.GetCrosvmArgs()).contains("multiple-workers=true"));

  // Test that a disk config with multiple workers builds the correct arguments.
  VmBuilder::Disk disk_multiple_workers{
      .fd = base::ScopedFD(open("/dev/null", O_RDONLY)),
      .multiple_workers = true};
  EXPECT_TRUE(JoinStringPairs(disk_multiple_workers.GetCrosvmArgs())
                  .contains("multiple-workers=true"));
}

TEST(VmBuilderTest, BlockSize) {
  VmBuilder::Disk disk{.fd = base::ScopedFD(open("/dev/null", O_RDONLY))};
  EXPECT_FALSE(JoinStringPairs(disk.GetCrosvmArgs()).contains("block_size"));

  VmBuilder::Disk disk_with_block_size{
      .fd = base::ScopedFD(open("/dev/null", O_RDONLY)), .block_size = 4096};
  EXPECT_TRUE(JoinStringPairs(disk_with_block_size.GetCrosvmArgs())
                  .contains("block_size=4096"));
}

TEST(VmBuilderTest, BlockAsyncExecutor) {
  // Test that a disk config with uring executor builds the correct arguments.
  VmBuilder::Disk disk_uring{
      .fd = base::ScopedFD(open("/dev/null", O_RDONLY)),
      .async_executor = VmBuilder::AsyncExecutor::kUring};
  EXPECT_TRUE(JoinStringPairs(disk_uring.GetCrosvmArgs())
                  .contains("async_executor=uring"));

  // Test that a disk config with epoll executor builds the correct arguments.
  VmBuilder::Disk disk_epoll{
      .fd = base::ScopedFD(open("/dev/null", O_RDONLY)),
      .async_executor = VmBuilder::AsyncExecutor::kEpoll};
  EXPECT_TRUE(JoinStringPairs(disk_epoll.GetCrosvmArgs())
                  .contains("async_executor=epoll"));
}

TEST(VmBuilderTest, StringToAsyncExecutor) {
  EXPECT_EQ(StringToAsyncExecutor("uring"),
            std::optional{VmBuilder::AsyncExecutor::kUring});
  EXPECT_EQ(StringToAsyncExecutor("epoll"),
            std::optional{VmBuilder::AsyncExecutor::kEpoll});

  EXPECT_EQ(StringToAsyncExecutor("unknown_value"), std::nullopt);
}

TEST(VmBuilderTest, ScopedFdImagesAndDisks) {
  base::ScopedFD kernel_fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
  ASSERT_TRUE(kernel_fd.is_valid());
  int kernel_raw_fd = kernel_fd.get();

  base::ScopedFD initrd_fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
  ASSERT_TRUE(initrd_fd.is_valid());
  int initrd_raw_fd = initrd_fd.get();

  base::ScopedFD bios_fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
  ASSERT_TRUE(bios_fd.is_valid());
  int bios_raw_fd = bios_fd.get();

  base::ScopedFD pflash_fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
  ASSERT_TRUE(pflash_fd.is_valid());
  int pflash_raw_fd = pflash_fd.get();

  base::ScopedFD rootfs_fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
  ASSERT_TRUE(rootfs_fd.is_valid());
  int rootfs_raw_fd = rootfs_fd.get();

  base::ScopedFD disk_fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
  ASSERT_TRUE(disk_fd.is_valid());
  int disk_raw_fd = disk_fd.get();

  VmBuilder builder;
  builder.SetKernel(std::move(kernel_fd))
      .SetInitrd(std::move(initrd_fd))
      .SetBios(std::move(bios_fd))
      .SetPflash(std::move(pflash_fd))
      .SetRootfs(VmBuilder::Rootfs{
          .fd = std::move(rootfs_fd),
          .writable = false,
      })
      .AppendDisk(VmBuilder::Disk{
          .fd = std::move(disk_fd),
          .writable = true,
      });

  auto spec = std::move(builder).BuildVmLaunchSpec(nullptr);
  ASSERT_TRUE(spec.has_value());
  const base::StringPairs& result = spec->args;

  EXPECT_EQ(spec->fds.size(), 6);
  for (const auto& fd : spec->fds) {
    EXPECT_TRUE(fd.is_valid());
    EXPECT_EQ(fcntl(fd.get(), F_GETFD) & FD_CLOEXEC, 0);
  }

  std::string kernel_expected =
      "/proc/self/fd/" + std::to_string(kernel_raw_fd);
  std::string initrd_expected =
      "/proc/self/fd/" + std::to_string(initrd_raw_fd);
  std::string bios_expected = "/proc/self/fd/" + std::to_string(bios_raw_fd);
  std::string pflash_expected =
      "path=/proc/self/fd/" + std::to_string(pflash_raw_fd);
  std::string rootfs_expected =
      "/proc/self/fd/" + std::to_string(rootfs_raw_fd) + ",ro=true,root=true";
  std::string disk_expected =
      "/proc/self/fd/" + std::to_string(disk_raw_fd) + ",ro=false";

  EXPECT_EQ(result.back().first, kernel_expected);

  std::string initrd_arg;
  std::string bios_arg;
  std::string pflash_arg;
  std::vector<std::string> blocks;

  for (const auto& p : result) {
    if (p.first == "-i") {
      initrd_arg = p.second;
    } else if (p.first == "--bios") {
      bios_arg = p.second;
    } else if (p.first == "--pflash") {
      pflash_arg = p.second;
    } else if (p.first == "--block") {
      blocks.push_back(p.second);
    }
  }

  EXPECT_EQ(initrd_arg, initrd_expected);
  EXPECT_EQ(bios_arg, bios_expected);
  EXPECT_EQ(pflash_arg, pflash_expected);
  ASSERT_EQ(blocks.size(), 2);
  EXPECT_EQ(blocks[0], rootfs_expected);
  EXPECT_EQ(blocks[1], disk_expected);
}
}  // namespace vm_tools::concierge
