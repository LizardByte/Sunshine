/**
 * @file tests/unit/test_resource_ownership.cpp
 * @brief Tests for resource lifetime through Sunshine's owning pointer aliases.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <bit>
#include <cerrno>
#include <cstdio>
#include <utility>

// platform includes
#ifdef _WIN32
  // Winsock must precede Windows.h.
  // clang-format off
  #include <WinSock2.h>
  #include <Windows.h>
  // clang-format on
  #include <io.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
#endif

// local includes
#include <src/network.h>
#include <src/process.h>
#include <src/video.h>
#ifdef _WIN32
  #include <src/platform/windows/nvprefs/nvprefs_common.h>
  #include <src/platform/windows/nvprefs/nvprefs_interface.h>
#endif

namespace {
  /**
   * @brief Allocate an FFmpeg buffer that records its final release.
   *
   * @param releases Counter incremented when the buffer has no remaining references.
   * @param data Storage kept alive until the buffer is released.
   * @return Owning reference to the tracked buffer.
   */
  video::avcodec_buffer_t tracked_buffer(int &releases, uint8_t &data) {
    return video::avcodec_buffer_t {av_buffer_create(&data, 1, [](auto *opaque, uint8_t *) {
      ++*static_cast<int *>(opaque);
    },
                                                     &releases,
                                                     0)};
  }
}  // namespace

/**
 * @brief Moving and resetting a frame releases its attached buffer exactly once.
 */
TEST(ResourceOwnershipTest, FrameMoveTransfersBufferOwnership) {
  int releases = 0;
  uint8_t data = 0;
  auto buffer = tracked_buffer(releases, data);
  ASSERT_TRUE(buffer);
  video::avcodec_frame_t frame {av_frame_alloc()};
  ASSERT_TRUE(frame);
  frame->buf[0] = buffer.release();

  auto moved = std::move(frame);
  EXPECT_FALSE(frame);
  EXPECT_EQ(releases, 0);
  moved.reset();
  EXPECT_EQ(releases, 1);
  moved.reset();
  EXPECT_EQ(releases, 1);
}

/**
 * @brief Destroying a codec context releases its hardware-frame context reference.
 */
TEST(ResourceOwnershipTest, CodecContextDestructionReleasesHardwareFramesReference) {
  int releases = 0;
  uint8_t data = 0;
  {
    auto buffer = tracked_buffer(releases, data);
    ASSERT_TRUE(buffer);
    video::avcodec_ctx_t context {avcodec_alloc_context3(nullptr)};
    ASSERT_TRUE(context);
    context->hw_frames_ctx = buffer.release();
    EXPECT_EQ(releases, 0);
  }
  EXPECT_EQ(releases, 1);
}

/**
 * @brief An FFmpeg buffer remains alive until its final owning reference is destroyed.
 */
TEST(ResourceOwnershipTest, BufferDestructionPreservesOtherReferences) {
  int releases = 0;
  uint8_t data = 0;
  {
    auto first = tracked_buffer(releases, data);
    ASSERT_TRUE(first);
    video::avcodec_buffer_t second {av_buffer_ref(first.get())};
    ASSERT_TRUE(second);
    first.reset();
    EXPECT_EQ(releases, 0);
    EXPECT_EQ(av_buffer_get_ref_count(second.get()), 1);
  }
  EXPECT_EQ(releases, 1);
}

/**
 * @brief Resetting a packet owner destroys each displaced ENet packet exactly once.
 */
TEST(ResourceOwnershipTest, PacketResetReleasesReplacedPacket) {
  int releases = 0;
  const auto make_packet = [&releases]() {
    net::packet_t packet {enet_packet_create(nullptr, 0, 0)};
    if (packet) {
      packet->userData = &releases;
      packet->freeCallback = [](ENetPacket *released) {
        ++*static_cast<int *>(released->userData);
      };
    }
    return packet;
  };

  {
    auto packet = make_packet();
    auto replacement = make_packet();
    ASSERT_TRUE(packet);
    ASSERT_TRUE(replacement);
    packet.reset(replacement.release());
    EXPECT_EQ(releases, 1);
  }
  EXPECT_EQ(releases, 2);
}

/**
 * @brief Resetting a file owner closes a securely created temporary file's underlying resource.
 */
TEST(ResourceOwnershipTest, FileResetClosesUnderlyingDescriptor) {
#ifdef _WIN32
  FILE *temporary_file = nullptr;
  ASSERT_EQ(tmpfile_s(&temporary_file), 0);
  proc::file_t file {temporary_file};
#else
  proc::file_t file {std::tmpfile()};
#endif
  ASSERT_TRUE(file);
#ifdef _WIN32
  const auto handle = std::bit_cast<HANDLE>(_get_osfhandle(_fileno(file.get())));
  ASSERT_NE(handle, INVALID_HANDLE_VALUE);
  file.reset();
  DWORD flags = 0;
  EXPECT_FALSE(GetHandleInformation(handle, &flags));
  EXPECT_EQ(GetLastError(), ERROR_INVALID_HANDLE);
#else
  const auto descriptor = fileno(file.get());
  ASSERT_GE(descriptor, 0);
  file.reset();
  errno = 0;
  EXPECT_EQ(fcntl(descriptor, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
#endif
}

#ifdef _WIN32
/**
 * @brief Const NVIDIA preference inspection does not claim ownership before the interface is loaded.
 */
TEST(ResourceOwnershipTest, ConstNvPrefsInterfaceStartsWithoutAnUndoFile) {
  const nvprefs::nvprefs_interface preferences;
  EXPECT_FALSE(preferences.owning_undo_file());
  EXPECT_FALSE(preferences.owning_undo_file());
}

/**
 * @brief A const LocalAlloc deleter releases a movable allocation and accepts null pointers.
 */
TEST(ResourceOwnershipTest, ConstLocalAllocDeleterReleasesTheAllocation) {
  const auto allocation = LocalAlloc(LMEM_MOVEABLE, 16);
  ASSERT_NE(allocation, nullptr);
  ASSERT_NE(LocalFlags(allocation), LMEM_INVALID_HANDLE);
  const nvprefs::safe_hlocal_deleter destroy;
  destroy(allocation);
  EXPECT_EQ(LocalFlags(allocation), LMEM_INVALID_HANDLE);
  destroy(static_cast<void *>(nullptr));
}

/**
 * @brief Moving an NVPrefs handle transfers responsibility for closing it.
 */
TEST(ResourceOwnershipTest, WindowsHandleMoveClosesHandleOnDestruction) {
  HANDLE raw_handle = nullptr;
  {
    nvprefs::safe_handle handle {CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ASSERT_TRUE(handle);
    raw_handle = handle.get();
    auto moved = std::move(handle);
    EXPECT_FALSE(handle);
    EXPECT_TRUE(moved);
    EXPECT_EQ(WaitForSingleObject(raw_handle, 0), WAIT_TIMEOUT);
  }
  DWORD flags = 0;
  EXPECT_FALSE(GetHandleInformation(raw_handle, &flags));
  EXPECT_EQ(GetLastError(), ERROR_INVALID_HANDLE);
}
#endif
