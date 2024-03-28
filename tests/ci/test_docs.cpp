#include <tests/conftest.cpp>

class DocsTests: public DocsTestFixture, public ::testing::WithParamInterface<std::tuple<const char *, const char *>> {};
INSTANTIATE_TEST_SUITE_P(
  DocFormats,
  DocsTests,
  ::testing::Values(
    std::make_tuple("html", "index.html"),
    std::make_tuple("epub", "Sunshine.epub")));
TEST_P(DocsTests, MakeDocs) {
  auto params = GetParam();
  std::string format = std::get<0>(params);
  std::string expected_filename = std::get<1>(params);

  std::filesystem::path expected_file = std::filesystem::current_path() / "build" / format / expected_filename;

  std::string command = "make " + format;
  int status = BaseTest::exec(command.c_str());
  EXPECT_EQ(status, 0);

  EXPECT_TRUE(std::filesystem::exists(expected_file));
}

class DocsRstTests: public DocsPythonVenvTest, public ::testing::WithParamInterface<std::filesystem::path> {};
INSTANTIATE_TEST_SUITE_P(
  RstFiles,
  DocsRstTests,
  ::testing::Values(
    std::filesystem::path(TESTS_DOCS_DIR),
    std::filesystem::path(TESTS_SOURCE_DIR) / "README.rst"));
TEST_P(DocsRstTests, RstCheckDocs) {
  std::filesystem::path docs_dir = GetParam();
  std::string command = "rstcheck -r " + docs_dir.string();
  int status = BaseTest::exec(command.c_str());
  EXPECT_EQ(status, 0);
}
