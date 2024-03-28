/**
 * @file tests/test_file_handler.cpp
 * @brief Test src/file_handler.*.
 */
#include <src/file_handler.h>

#include <tests/conftest.cpp>

TEST(FileHandlerTests, WriteFileTest) {
  EXPECT_EQ(file_handler::write_file("write_file_test.txt", "test"), 0);
}

TEST(FileHandlerTests, ReadFileTest) {
  // read file from WriteFileTest
  EXPECT_EQ(file_handler::read_file("write_file_test.txt"), "test\n");  // sunshine adds a newline

  // read missing file
  EXPECT_EQ(file_handler::read_file("non-existing-file.txt"), "");
}
