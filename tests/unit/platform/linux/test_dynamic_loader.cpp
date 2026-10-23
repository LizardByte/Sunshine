/**
 * @file tests/unit/platform/linux/test_dynamic_loader.cpp
 * @brief Tests for opaque Linux shared-library handles and symbol loading.
 */
#ifdef __linux__

  // test includes
  #include "../../../tests_common.h"

  // standard includes
  #include <bit>

  // platform includes
  #include <dlfcn.h>

  // local includes
  #include "src/platform/linux/misc.h"

TEST(DynamicLibraryTest, FallsBackToAnAvailableLibraryAndLoadsCallableSymbols) {
  auto *library = dyn::handle({"libsunshine-does-not-exist.so", "libc.so.6"});
  ASSERT_NE(library, nullptr);
  auto close_library = util::fail_guard([library] {
    dlclose(library);
  });
  dyn::apiproc symbol = nullptr;
  ASSERT_EQ(dyn::load(library, {{&symbol, "getpid"}}), 0);
  ASSERT_NE(symbol, nullptr);
  const auto loaded_getpid = std::bit_cast<pid_t (*)()>(symbol);
  EXPECT_EQ(loaded_getpid(), getpid());
}

TEST(DynamicLibraryTest, MissingOptionalSymbolDoesNotFailLoading) {
  auto *library = dyn::handle({"libc.so.6"});
  ASSERT_NE(library, nullptr);
  auto close_library = util::fail_guard([library] {
    dlclose(library);
  });
  dyn::apiproc symbol = nullptr;
  EXPECT_EQ(dyn::load(library, {{&symbol, "sunshine_missing_symbol"}}, false), 0);
  EXPECT_EQ(symbol, nullptr);
  EXPECT_EQ(dyn::load(library, {{&symbol, "sunshine_missing_symbol"}}, true), -1);
}

TEST(DynamicLibraryTest, MissingLibraryReturnsNull) {
  EXPECT_EQ(dyn::handle({"libsunshine-does-not-exist.so", "libsunshine-also-does-not-exist.so"}), nullptr);
}

#endif
