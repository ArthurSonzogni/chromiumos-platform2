// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/lifeline_fd_service.h"

#include <fcntl.h>
#include <unistd.h>

#include <utility>

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
  auto [fd_for_client, fd_for_service] = CreatePipeFDs();
  ASSERT_TRUE(fd_for_client.is_valid());
  ASSERT_TRUE(fd_for_service.is_valid());

  EXPECT_CALL(*this, UserCallback).Times(0);
  base::OnceClosure user_callback = base::BindOnce(
      &LifelineFDServiceTest::UserCallback, base::Unretained(this));

  auto closure = lifeline_svc_.AddLifelineFD(std::move(fd_for_service),
                                             std::move(user_callback));
  ASSERT_TRUE(closure);
  EXPECT_EQ(1, lifeline_svc_.get_lifeline_fds_for_testing().size());

  // The local owner of |closure| destroys it before the remote client
  // invalidates |fd_for_service|.
  closure = base::ScopedClosureRunner();
  // The lifeline fd is unregistered in the LifelineFD service.
  EXPECT_THAT(lifeline_svc_.get_lifeline_fds_for_testing(), ElementsAre());

  // Remotely invalidating the local fd has no effect.
  fd_for_client.reset();
  task_environment_.RunUntilIdle();
}

TEST_F(LifelineFDServiceTest, RemoteClientInvalidatesFDFirst) {
  auto [fd_for_client, fd_for_service] = CreatePipeFDs();
  ASSERT_TRUE(fd_for_client.is_valid());
  ASSERT_TRUE(fd_for_service.is_valid());

  EXPECT_CALL(*this, UserCallback);
  base::OnceClosure user_callback = base::BindOnce(
      &LifelineFDServiceTest::UserCallback, base::Unretained(this));

  auto closure = lifeline_svc_.AddLifelineFD(std::move(fd_for_service),
                                             std::move(user_callback));

  ASSERT_TRUE(closure);
  EXPECT_EQ(1, lifeline_svc_.get_lifeline_fds_for_testing().size());

  // The remote client invalidates |fd_for_service| before the local owner of
  // |closure| destroys it.
  fd_for_client.reset();
  task_environment_.RunUntilIdle();

  // the lifeline fd is unregistered in the LifelineFD service.
  EXPECT_THAT(lifeline_svc_.get_lifeline_fds_for_testing(), ElementsAre());

  // The local owner is notified via |user_callback| and now destroys its
  // LifelineFD instance. This has no effect.
  closure = base::ScopedClosureRunner();
}

}  // namespace patchpanel
