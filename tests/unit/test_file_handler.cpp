/**
 * @file tests/test_file_handler.cpp
 * @brief Test src/file_handler.*.
 */
#include <src/file_handler.h>

#include <tests/conftest.cpp>

class FileHandlerTests: public virtual BaseTest, public ::testing::WithParamInterface<std::tuple<int, std::string>> {
protected:
  void
  SetUp() override {
    BaseTest::SetUp();
  }

  void
  TearDown() override {
    BaseTest::TearDown();
  }
};
INSTANTIATE_TEST_SUITE_P(
  TestFiles,
  FileHandlerTests,
  ::testing::Values(
    std::make_tuple(0, ""),  // empty file
    std::make_tuple(1, "a"),  // single character
    std::make_tuple(2, "Mr. Blue Sky - Electric Light Orchestra"),  // single line
    std::make_tuple(3, R"(
Morning! Today's forecast calls for blue skies
The sun is shining in the sky
There ain't a cloud in sight
It's stopped raining
Everybody's in the play
And don't you know, it's a beautiful new day
Hey, hey, hey!
Running down the avenue
See how the sun shines brightly in the city
All the streets where once was pity
Mr. Blue Sky is living here today!
Hey, hey, hey!
    )")  // multi-line
    ));

TEST_P(FileHandlerTests, WriteFileTest) {
  auto [fileNum, content] = GetParam();
  std::string fileName = "write_file_test_" + std::to_string(fileNum) + ".txt";
  EXPECT_EQ(file_handler::write_file(fileName.c_str(), content), 0);
}

TEST_P(FileHandlerTests, ReadFileTest) {
  auto [fileNum, content] = GetParam();
  std::string fileName = "write_file_test_" + std::to_string(fileNum) + ".txt";
  EXPECT_EQ(file_handler::read_file(fileName.c_str()), content);
}

TEST(FileHandlerTests, ReadMissingFileTest) {
  // read missing file
  EXPECT_EQ(file_handler::read_file("non-existing-file.txt"), "");
}
