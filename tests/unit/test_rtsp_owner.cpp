/**
 * @file tests/unit/test_rtsp_owner.cpp
 * @brief Test RTSP pending-session ownership and callback lifetimes.
 */
// test includes
#include "../tests_common.h"
#include "tests/server_test_context.h"

/**
 * @brief Each listener retains and clears only its own pending launch session.
 */
TEST(RtspOwnerTest, PendingSessionsAreIndependentAndRequireMatchingIdentity) {
  test_utils::server_context_t first;
  test_utils::server_context_t second;
  auto first_launch = std::make_shared<rtsp_stream::launch_session_t>();
  first_launch->id = 7;
  auto second_launch = std::make_shared<rtsp_stream::launch_session_t>();
  second_launch->id = 7;
  std::weak_ptr<rtsp_stream::launch_session_t> first_pending = first_launch;
  std::weak_ptr<rtsp_stream::launch_session_t> second_pending = second_launch;
  first.rtsp.launch_session_raise(std::move(first_launch));
  second.rtsp.launch_session_raise(std::move(second_launch));
  first.rtsp.launch_session_clear(8);
  EXPECT_FALSE(first_pending.expired());
  first.rtsp.launch_session_clear(7);
  EXPECT_TRUE(first_pending.expired());
  EXPECT_FALSE(second_pending.expired());
  second.rtsp.launch_session_clear(7);
  EXPECT_TRUE(second_pending.expired());
  EXPECT_EQ(first.rtsp.session_count(), 0);
  EXPECT_EQ(second.rtsp.session_count(), 0);
}

/**
 * @brief A connected-stream callback consumes its launch and safely expires with the listener.
 */
TEST(RtspOwnerTest, ConnectionCallbackConsumesLaunchAndCanOutliveListener) {
  std::function<void()> on_connected;
  {
    test_utils::server_context_t context;
    auto launch = std::make_shared<rtsp_stream::launch_session_t>();
    launch->id = 19;
    context.rtsp.launch_session_raise(launch);
    on_connected = launch->on_stream_connected;
    const std::weak_ptr<rtsp_stream::launch_session_t> pending = launch;
    launch.reset();
    ASSERT_FALSE(pending.expired());
    on_connected();
    EXPECT_TRUE(pending.expired());
    on_connected();
  }
  ASSERT_TRUE(on_connected);
  EXPECT_NO_THROW(on_connected());
}

/**
 * @brief A second pending launch cannot replace the launch awaiting its RTSP handshake.
 */
TEST(RtspOwnerTest, ExistingPendingLaunchIsPreserved) {
  test_utils::server_context_t context;
  auto first = std::make_shared<rtsp_stream::launch_session_t>();
  first->id = 1;
  auto replacement = std::make_shared<rtsp_stream::launch_session_t>();
  replacement->id = 2;
  std::weak_ptr<rtsp_stream::launch_session_t> pending = first;
  std::weak_ptr<rtsp_stream::launch_session_t> rejected = replacement;
  context.rtsp.launch_session_raise(std::move(first));
  context.rtsp.launch_session_raise(std::move(replacement));
  EXPECT_TRUE(rejected.expired());
  context.rtsp.launch_session_clear(2);
  EXPECT_FALSE(pending.expired());
  context.rtsp.launch_session_clear(1);
  EXPECT_TRUE(pending.expired());
}

/**
 * @brief Allocating session state preserves per-client identity without starting shared sockets.
 */
TEST(StreamOwnerTest, IndependentServicesAllocateStoppedSessions) {
  test_utils::server_context_t first;
  test_utils::server_context_t second;
  stream::config_t config {};
  rtsp_stream::launch_session_t launch {};
  launch.iv.resize(16);
  launch.gcm_key.resize(16);
  launch.client_cert = "first client";
  auto first_session = first.streams.alloc(config, launch);
  launch.client_cert = "second client";
  auto second_session = second.streams.alloc(config, launch);
  ASSERT_TRUE(first_session);
  ASSERT_TRUE(second_session);
  EXPECT_EQ(stream::session::state(*first_session), stream::session::state_e::STOPPED);
  EXPECT_EQ(stream::session::state(*second_session), stream::session::state_e::STOPPED);
  EXPECT_EQ(stream::session::client_cert(*first_session), "first client");
  EXPECT_EQ(stream::session::client_cert(*second_session), "second client");
  EXPECT_EQ(first.rtsp.session_count(), 0);
  EXPECT_EQ(second.rtsp.session_count(), 0);
}
