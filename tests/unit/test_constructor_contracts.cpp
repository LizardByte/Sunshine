/**
 * @file tests/unit/test_constructor_contracts.cpp
 * @brief Tests for explicit conversions, ownership transfers, and member initialization.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// local includes
#include "src/crypto.h"
#include "src/move_by_copy.h"
#include "src/platform/common.h"
#include "src/sync.h"
#include "src/task_pool.h"
#include "src/thread_safe.h"
#include "src/utility.h"

#ifdef _WIN32
  #include "src/platform/windows/display.h"
#endif

namespace {
  using callback_t = std::function<void()>;  ///< Callable used by task and cleanup wrappers.
  using int_ptr_t = util::uniq_ptr<int, std::default_delete<int>>;  ///< Integer owned by a Sunshine unique pointer.
  using shared_ptr_t = safe::shared_t<int>::ptr_t;  ///< Reference to shared integer storage.
  using move_wrapper_t = move_by_copy_util::MoveByCopy<std::unique_ptr<int>>;  ///< Copy-shaped owner of a move-only integer.

  static_assert(std::is_constructible_v<int_ptr_t, int *>);
  static_assert(!std::is_convertible_v<int *, int_ptr_t>);
  static_assert(std::is_convertible_v<std::nullptr_t, int_ptr_t>);
  static_assert(std::is_convertible_v<std::unique_ptr<int>, int_ptr_t>);
  static_assert(!std::is_convertible_v<int *, util::wrap_ptr<int>>);
  static_assert(!std::is_convertible_v<std::unique_ptr<int>, util::wrap_ptr<int>>);
  static_assert(!std::is_convertible_v<callback_t, util::FailGuard<callback_t>>);
  static_assert(!std::is_convertible_v<callback_t, task_pool_util::_Impl<callback_t>>);
  static_assert(!std::is_convertible_v<int, sync_util::sync_t<int>>);
  static_assert(!std::is_convertible_v<safe::mail_t, safe::post_t<safe::event_t<int>>>);
  static_assert(!std::is_convertible_v<shared_ptr_t, bool>);
  static_assert(!std::is_convertible_v<move_wrapper_t, std::unique_ptr<int>>);
  static_assert(!std::is_convertible_v<platf::high_precision_timer &, bool>);
  static_assert(!std::is_convertible_v<crypto::aes_t, crypto::cipher::ecb_t>);
  static_assert(!std::is_convertible_v<crypto::aes_t, crypto::cipher::gcm_t>);
  static_assert(!std::is_convertible_v<crypto::aes_t, crypto::cipher::cbc_t>);

  /**
   * @brief Polymorphic resource used to check ownership-preserving pointer conversions.
   */
  struct base_t {
    virtual ~base_t() = default;
    int value {42};  ///< Value retained when ownership is moved to a base pointer.
  };

  /**
   * @brief Derived resource that can be owned through its base class.
   */
  struct derived_t: base_t {};

  using base_ptr_t = util::uniq_ptr<base_t, std::default_delete<base_t>>;  ///< Base-class owner with a virtual destructor.
  using derived_ptr_t = util::uniq_ptr<derived_t, std::default_delete<base_t>>;  ///< Compatible derived-class owner.

  static_assert(std::is_convertible_v<derived_ptr_t, base_ptr_t>);
  static_assert(std::is_convertible_v<std::unique_ptr<derived_t, std::default_delete<base_t>>, base_ptr_t>);
}  // namespace

TEST(ConstructorContracts, EmptyUniquePointersHaveNoOwnedResource) {
  int_ptr_t default_pointer;
  int_ptr_t null_pointer = nullptr;

  EXPECT_EQ(default_pointer.get(), nullptr);
  EXPECT_EQ(null_pointer.get(), nullptr);
}

TEST(ConstructorContracts, ExplicitRawPointerConstructionTakesOwnership) {
  auto original = std::make_unique<int>(7);
  const auto address = original.get();
  int_ptr_t pointer {original.release()};

  EXPECT_EQ(pointer.get(), address);
  EXPECT_EQ(*pointer, 7);
}

TEST(ConstructorContracts, CompatibleUniquePointerMovesPreserveOwnership) {
  std::unique_ptr<derived_t, std::default_delete<base_t>> original {std::make_unique<derived_t>()};
  const auto address = original.get();
  derived_ptr_t derived = std::move(original);
  base_ptr_t base = std::move(derived);

  EXPECT_EQ(original.get(), nullptr);
  EXPECT_EQ(derived.get(), nullptr);
  EXPECT_EQ(base.get(), address);
  EXPECT_EQ(base->value, 42);
}

TEST(ConstructorContracts, CompatibleStandardPointerMovesCanTargetBaseOwners) {
  std::unique_ptr<derived_t, std::default_delete<base_t>> original {std::make_unique<derived_t>()};
  const auto address = original.get();
  base_ptr_t base = std::move(original);

  EXPECT_EQ(original.get(), nullptr);
  EXPECT_EQ(base.get(), address);
}

TEST(ConstructorContracts, WrapPointersKeepBorrowedAndOwnedValuesAccessible) {
  int borrowed_value = 7;
  util::wrap_ptr borrowed {&borrowed_value};
  util::wrap_ptr owned {std::make_unique<int>(42)};

  EXPECT_EQ(*borrowed, 7);
  EXPECT_EQ(*owned, 42);
}

TEST(ConstructorContracts, MovingDisabledFailGuardsKeepsCleanupDisabled) {
  int calls = 0;
  {
    auto original = util::fail_guard([&calls]() {
      ++calls;
    });
    original.disable();
    auto moved = std::move(original);
    EXPECT_FALSE(moved.failure);
  }
  EXPECT_EQ(calls, 0);
}

TEST(ConstructorContracts, ExplicitTaskConstructionRunsTheSuppliedCallable) {
  int calls = 0;
  task_pool_util::_Impl task {callback_t {[&calls]() {
    ++calls;
  }}};
  task.run();

  EXPECT_EQ(calls, 1);
}

TEST(ConstructorContracts, ExplicitSynchronizedConstructionKeepsTheInitialValue) {
  sync_util::sync_t<int> value {42};
  EXPECT_EQ(value.raw, 42);
}

TEST(ConstructorContracts, ExplicitMoveWrapperConversionTransfersTheValue) {
  auto original = std::make_unique<int>(42);
  auto wrapper = move_by_copy_util::cmove(original);
  auto value = static_cast<std::unique_ptr<int>>(wrapper);

  EXPECT_EQ(original.get(), nullptr);
  ASSERT_NE(value.get(), nullptr);
  EXPECT_EQ(*value, 42);
}

TEST(ConstructorContracts, SharedPointersSupportExplicitBooleanChecks) {
  shared_ptr_t pointer;

  EXPECT_FALSE(static_cast<bool>(pointer));
}

TEST(ConstructorContracts, ExplicitCipherConstructionKeepsEncryptionWorking) {
  const crypto::aes_t key {16};
  crypto::aes_t iv {16};
  constexpr std::string_view plaintext = "0123456789abcdef";
  crypto::cipher::ecb_t ecb {key, false};
  crypto::cipher::cbc_t cbc {key, false};
  crypto::cipher::gcm_t gcm {key, false};
  std::vector<std::uint8_t> encrypted;
  std::vector<std::uint8_t> decrypted;
  std::array<std::uint8_t, 16> cipher {};
  std::array<std::uint8_t, crypto::cipher::tag_size> tag {};

  ASSERT_EQ(ecb.encrypt(plaintext, encrypted), 0);
  const std::string ciphertext {encrypted.begin(), encrypted.end()};
  ASSERT_EQ(ecb.decrypt(ciphertext, decrypted), 0);
  EXPECT_EQ((std::string {decrypted.begin(), decrypted.end()}), plaintext);
  EXPECT_EQ(cbc.encrypt(plaintext, cipher.data(), &iv), plaintext.size());
  EXPECT_EQ(gcm.encrypt(plaintext, tag.data(), cipher.data(), &iv), plaintext.size());
}

#ifdef _WIN32
TEST(ConstructorContracts, CursorViewportsStartEmptyWithTheFullDepthRange) {
  platf::dxgi::gpu_cursor_t cursor;

  EXPECT_FLOAT_EQ(cursor.cursor_view.TopLeftX, 0.0f);
  EXPECT_FLOAT_EQ(cursor.cursor_view.TopLeftY, 0.0f);
  EXPECT_FLOAT_EQ(cursor.cursor_view.Width, 0.0f);
  EXPECT_FLOAT_EQ(cursor.cursor_view.Height, 0.0f);
  EXPECT_FLOAT_EQ(cursor.cursor_view.MinDepth, 0.0f);
  EXPECT_FLOAT_EQ(cursor.cursor_view.MaxDepth, 1.0f);
}
#endif
