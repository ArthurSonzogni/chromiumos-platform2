// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/lifeline_fd_service.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

using testing::ElementsAre;

namespace patchpanel {
namespace {
std::pair<base::ScopedFD, base::ScopedFD> CreatePipeFDs() {
  int pipe_fds[2] = {-1, -1};
  if (pipe2(pipe_fds, O_CLOEXEC) < 0) {
    return {};
  }
  return {base::ScopedFD(pipe_fds[0]), base::ScopedFD(pipe_fds[1])};
}

bool IsValidFD(int fd) {
  return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

std::vector<base::ScopedFD> AssignFDs(int n) {
  std::vector<base::ScopedFD> fds;
  for (int i = 0; i < n; i++) {
    fds.push_back(base::ScopedFD(socket(AF_INET, SOCK_STREAM, 0)));
  }
  return fds;
}

bool ContainsFD(const std::vector<base::ScopedFD>& fds, int target_fd) {
  for (const auto& fd : fds) {
    if (fd.get() == target_fd) {
      return true;
    }
  }
  return false;
}
}  // namespace

class LifelineFDServiceTest : public testing::Test {
 public:
  MOCK_METHOD(void, UserCallback, (), ());

 protected:
  // The environment instances which are required for using
  // base::FileDescriptorWatcher::WatchReadable. Declared them first to ensure
  // they are the last things to be cleaned up.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};

  LifelineFDService lifeline_svc_;
};

TEST_F(LifelineFDServiceTest, LifelineFDIsDestroyedFirst) {
  // Pre-create some file descriptors to force the fds used in the test to be
  // created at an offset. This ensures that asserts on the underlying fd values
  // are safe to do.
  auto fds = AssignFDs(20);

  auto [fd_for_client, fd_for_service] = CreatePipeFDs();
  ASSERT_TRUE(fd_for_client.is_valid());
  ASSERT_TRUE(fd_for_service.is_valid());
  int raw_fd_for_service = fd_for_service.get();

  fds.clear();

  // The user callback will not be invoked if the user destroys the
  // ScopedClosureRunner on their side first.
  EXPECT_CALL(*this, UserCallback).Times(0);
  base::OnceClosure user_callback = base::BindOnce(
      &LifelineFDServiceTest::UserCallback, base::Unretained(this));

  auto closure = lifeline_svc_.AddLifelineFD(std::move(fd_for_service),
                                             std::move(user_callback));
  ASSERT_TRUE(closure);
  EXPECT_THAT(lifeline_svc_.get_lifeline_fds_for_testing(),
              ElementsAre(raw_fd_for_service));
  // The underlying int value of |fd_for_service| is valid
  ASSERT_TRUE(IsValidFD(raw_fd_for_service));
  // The underlying int value of |fd_for_service| is reserved.
  EXPECT_FALSE(ContainsFD(AssignFDs(50), raw_fd_for_service));

  // The local owner of |closure| destroys it before the remote client
  // invalidates |fd_for_service|.
  closure = base::ScopedClosureRunner();
  // The lifeline fd is unregistered in the LifelineFD service.
  EXPECT_THAT(lifeline_svc_.get_lifeline_fds_for_testing(), ElementsAre());
  // The underlying int value of |fd_for_service| is not reserved anymore
  EXPECT_TRUE(ContainsFD(AssignFDs(50), raw_fd_for_service));

  // Remotely invalidating the local fd has no effect.
  fd_for_client.reset();
  task_environment_.RunUntilIdle();
}

TEST_F(LifelineFDServiceTest, RemoteClientInvalidatesFDFirst) {
  // Pre-create some file descriptors to force the fds used in the test to be
  // created at an offset. This ensures that asserts on the underlying fd values
  // are safe to do.
  auto fds = AssignFDs(20);

  auto [fd_for_client, fd_for_service] = CreatePipeFDs();
  ASSERT_TRUE(fd_for_client.is_valid());
  ASSERT_TRUE(fd_for_service.is_valid());
  int raw_fd_for_service = fd_for_service.get();

  fds.clear();

  // The user callback will be invoked if the user fd gets remotely invalidated
  // first.
  EXPECT_CALL(*this, UserCallback);
  base::OnceClosure user_callback = base::BindOnce(
      &LifelineFDServiceTest::UserCallback, base::Unretained(this));

  auto closure = lifeline_svc_.AddLifelineFD(std::move(fd_for_service),
                                             std::move(user_callback));
  ASSERT_TRUE(closure);
  EXPECT_THAT(lifeline_svc_.get_lifeline_fds_for_testing(),
              ElementsAre(raw_fd_for_service));
  // The underlying int value of |fd_for_service| is valid
  ASSERT_TRUE(IsValidFD(raw_fd_for_service));
  // The underlying int value of |fd_for_service| is reserved.
  EXPECT_FALSE(ContainsFD(AssignFDs(50), raw_fd_for_service));

  // The remote client invalidates |fd_for_service| before the local owner of
  // |closure| destroys it.
  fd_for_client.reset();
  task_environment_.RunUntilIdle();

  // The lifeline fd is unregistered in the LifelineFD service.
  EXPECT_THAT(lifeline_svc_.get_lifeline_fds_for_testing(), ElementsAre());
  // The underlying int value of |fd_for_service| is not reserved anymore
  EXPECT_TRUE(ContainsFD(AssignFDs(50), raw_fd_for_service));

  // The local owner is notified via |user_callback| and now destroys its
  // LifelineFD instance. This has no effect.
  closure = base::ScopedClosureRunner();
}

}  // namespace patchpanel
