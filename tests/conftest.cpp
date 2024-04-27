#include <filesystem>
#include <gtest/gtest.h>

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/shared_ptr.hpp>

#include <src/globals.h>
#include <src/logging.h>
#include <src/platform/common.h>

#include <tests/utils.h>

namespace boost_logging = boost::log;
namespace sinks = boost_logging::sinks;

// Undefine the original TEST macro
#undef TEST

// Redefine TEST to use our BaseTest class, to automatically use our BaseTest fixture
#define TEST(test_case_name, test_name)              \
  GTEST_TEST_(test_case_name, test_name, ::BaseTest, \
    ::testing::internal::GetTypeId<::BaseTest>())

/**
 * @brief Base class for tests.
 *
 * This class provides a base test fixture for all tests.
 *
 * ``cout``, ``stderr``, and ``stdout`` are redirected to a buffer, and the buffer is printed if the test fails.
 *
 * @todo Retain the color of the original output.
 */
class BaseTest: public ::testing::Test {
protected:
  // https://stackoverflow.com/a/58369622/11214013

  // we can possibly use some internal googletest functions to capture stdout and stderr, but I have not tested this
  // https://stackoverflow.com/a/33186201/11214013

  // Add a member variable for deinit_guard
  std::unique_ptr<logging::deinit_t> deinit_guard;

  // Add a member variable to store the sink
  boost::shared_ptr<sinks::synchronous_sink<sinks::text_ostream_backend>> test_sink;

  BaseTest():
      sbuf { nullptr }, pipe_stdout { nullptr }, pipe_stderr { nullptr } {
    // intentionally empty
  }

  ~BaseTest() override = default;

  void
  SetUp() override {
    // todo: only run this one time, instead of every time a test is run
    // see: https://stackoverflow.com/questions/2435277/googletest-accessing-the-environment-from-a-test
    // get command line args from the test executable
    testArgs = ::testing::internal::GetArgvs();

    // then get the directory of the test executable
    // std::string path = ::testing::internal::GetArgvs()[0];
    testBinary = testArgs[0];

    // get the directory of the test executable
    testBinaryDir = std::filesystem::path(testBinary).parent_path();

    // If testBinaryDir is empty or `.` then set it to the current directory
    // maybe some better options here: https://stackoverflow.com/questions/875249/how-to-get-current-directory
    if (testBinaryDir.empty() || testBinaryDir.string() == ".") {
      testBinaryDir = std::filesystem::current_path();
    }

    // Create a sink that writes to our stringstream (BOOST_LOG)
    typedef sinks::synchronous_sink<sinks::text_ostream_backend> test_text_sink;
    test_sink = boost::make_shared<test_text_sink>();

    // Set the stringstream as the target of the sink (BOOST_LOG)
    boost::shared_ptr<std::ostream> stream(&boost_log_buffer, [](std::ostream *) {});
    test_sink->locked_backend()->add_stream(stream);

    // Register the sink in the logging core (BOOST_LOG)
    boost_logging::core::get()->add_sink(test_sink);

    sbuf = std::cout.rdbuf();  // save cout buffer (std::cout)
    std::cout.rdbuf(cout_buffer.rdbuf());  // redirect cout to buffer (std::cout)

    // todo: do this only once
    // setup a mail object
    mail::man = std::make_shared<safe::mail_raw_t>();

    deinit_guard = logging::init(0, "test.log");
    if (!deinit_guard) {
      FAIL() << "Logging failed to initialize";
    }
  }

  void
  TearDown() override {
    std::cout.rdbuf(sbuf);  // restore cout buffer

    // get test info
    const ::testing::TestInfo *const test_info = ::testing::UnitTest::GetInstance()->current_test_info();

    if (test_info->result()->Failed()) {
      std::cout << std::endl
                << "Test failed: " << test_info->name() << std::endl
                << std::endl
                << "Captured boost log:" << std::endl
                << boost_log_buffer.str() << std::endl
                << "Captured cout:" << std::endl
                << cout_buffer.str() << std::endl
                << "Captured stdout:" << std::endl
                << stdout_buffer.str() << std::endl
                << "Captured stderr:" << std::endl
                << stderr_buffer.str() << std::endl;
    }

    sbuf = nullptr;  // clear sbuf
    if (pipe_stdout) {
      pclose(pipe_stdout);
      pipe_stdout = nullptr;
    }
    if (pipe_stderr) {
      pclose(pipe_stderr);
      pipe_stderr = nullptr;
    }

    // Remove the sink from the logging core (BOOST_LOG)
    boost_logging::core::get()->remove_sink(test_sink);
    test_sink.reset();
  }

  // functions and variables
  std::vector<std::string> testArgs;  // CLI arguments used
  std::filesystem::path testBinary;  // full path of this binary
  std::filesystem::path testBinaryDir;  // full directory of this binary
  std::stringstream boost_log_buffer;  // declare boost_log_buffer
  std::stringstream cout_buffer;  // declare cout_buffer
  std::stringstream stdout_buffer;  // declare stdout_buffer
  std::stringstream stderr_buffer;  // declare stderr_buffer
  std::streambuf *sbuf;
  FILE *pipe_stdout;
  FILE *pipe_stderr;

  int
  exec(const char *cmd) {
    std::array<char, 128> buffer {};
    pipe_stdout = popen((std::string(cmd) + " 2>&1").c_str(), "r");
    pipe_stderr = popen((std::string(cmd) + " 2>&1").c_str(), "r");
    if (!pipe_stdout || !pipe_stderr) {
      throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe_stdout) != nullptr) {
      stdout_buffer << buffer.data();
    }
    while (fgets(buffer.data(), buffer.size(), pipe_stderr) != nullptr) {
      stderr_buffer << buffer.data();
    }
    int returnCode = pclose(pipe_stdout);
    pipe_stdout = nullptr;
    if (returnCode != 0) {
      std::cout << "Error: " << stderr_buffer.str() << std::endl
                << "Return code: " << returnCode << std::endl;
    }
    return returnCode;
  }
};

class PlatformInitBase: public virtual BaseTest {
protected:
  void
  SetUp() override {
    std::cout << "PlatformInitTest:: starting Fixture SetUp" << std::endl;

    // initialize the platform
    auto deinit_guard = platf::init();
    if (!deinit_guard) {
      FAIL() << "Platform failed to initialize";
    }

    std::cout << "PlatformInitTest:: finished Fixture SetUp" << std::endl;
  }

  void
  TearDown() override {
    std::cout << "PlatformInitTest:: starting Fixture TearDown" << std::endl;
    std::cout << "PlatformInitTest:: finished Fixture TearDown" << std::endl;
  }
};

class DocsPythonVenvBase: public virtual BaseTest {
protected:
  void
  SetUp() override {
#if defined TESTS_ENABLE_VENV_TESTS && TESTS_ENABLE_VENV_TESTS == 0
    GTEST_SKIP_("TESTS_ENABLE_VENV_TESTS is disabled by CMake");
#else
    std::cout << "DocsPythonVenvTest:: starting Fixture SetUp" << std::endl;

    std::string pythonBinDirArray[] = { "bin", "Scripts" };
    std::filesystem::path pythonPath = "python";
    std::string binPath;
    std::string command;
    int exit_code;

    std::filesystem::path venvPath = ".venv";
    std::filesystem::path fullVenvPath = BaseTest::testBinaryDir / venvPath;

    // check for existence of venv, and create it if necessary
    std::cout << "DocsPythonVenvTest:: checking for venv" << std::endl;
    if (!std::filesystem::exists(fullVenvPath)) {
      std::cout << "DocsPythonVenvTest:: venv not found" << std::endl;

      // create the venv
      command = "\"" TESTS_PYTHON_EXECUTABLE "\" -m venv " + fullVenvPath.string();
      std::cout << "DocsPythonVenvTest:: trying to create venv with command: " << command << std::endl;
      exit_code = BaseTest::exec(command.c_str());
      if (exit_code != 0) {
        if (!std::filesystem::exists(fullVenvPath)) {
          FAIL() << "Command failed: " << command << " with exit code: " << exit_code;
        }
        else {
          // venv command will randomly complain that some files already exist...
          std::cout << "DocsPythonVenvTest:: exit code (" << exit_code << ") indicates venv creation failed, but venv exists" << std::endl;
        }
      }
    }

    // determine if bin directory is `bin` (Unix) or `Scripts` (Windows)
    // cannot assume `Scripts` on Windows, as it could be `bin` if using MSYS2, cygwin, etc.
    std::cout << "DocsPythonVenvTest:: checking structure of venv" << std::endl;
    for (const std::string &binDir : pythonBinDirArray) {
      // check if bin directory exists
      if (std::filesystem::exists(fullVenvPath / binDir)) {
        binPath = binDir;
        std::cout << "DocsPythonVenvTest:: found binPath: " << binPath << std::endl;
        break;
      }
    }

    if (binPath.empty()) {
      FAIL() << "Python venv not found";
    }

    // set fullPythonPath and fullPythonBinPath
    fullPythonPath = fullVenvPath / binPath / pythonPath;
    fullPythonBinPath = fullVenvPath / binPath;

    std::cout << "DocsPythonVenvTest:: fullPythonPath: " << fullPythonPath << std::endl;
    std::cout << "DocsPythonVenvTest:: fullPythonBinPath: " << fullPythonBinPath << std::endl;

    std::filesystem::path requirements_path = std::filesystem::path(TESTS_DOCS_DIR) / "requirements.txt";

    // array of commands to run
    std::string CommandArray[] = {
      "\"" + fullPythonPath.string() + "\" -m pip install -r " + requirements_path.string(),
    };

    for (const std::string &_command : CommandArray) {
      std::cout << "DocsPythonVenvTest:: running command: " << _command << std::endl;
      exit_code = BaseTest::exec(_command.c_str());
      if (exit_code != 0) {
        FAIL() << "Command failed: " << command << " with exit code: " << exit_code;
      }
    }

    // Save the original PATH
    originalEnvPath = std::getenv("PATH") ? std::getenv("PATH") : "";
    std::cout << "DocsPythonVenvTest:: originalEnvPath: " << originalEnvPath << std::endl;

    // Set the temporary PATH
    std::string tempPath;
    std::string envPathSep;

  #ifdef _WIN32
    envPathSep = ";";
  #else
    envPathSep = ":";
  #endif
    tempPath = fullPythonBinPath.string() + envPathSep + originalEnvPath;
    std::cout << "DocsPythonVenvTest:: tempPath: " << tempPath << std::endl;
    setEnv("PATH", tempPath);

    std::cout << "DocsPythonVenvTest:: finished Fixture SetUp" << std::endl;
#endif
  }

  void
  TearDown() override {
    std::cout << "DocsPythonVenvTest:: starting Fixture TearDown" << std::endl;

    // Restore the original PATH
    if (!originalEnvPath.empty()) {
      std::cout << "DocsPythonVenvTest:: restoring originalEnvPath: " << originalEnvPath << std::endl;
      setEnv("PATH", originalEnvPath);
    }

    std::cout << "DocsPythonVenvTest:: finished Fixture TearDown" << std::endl;
  }

  // functions and variables
  std::filesystem::path fullPythonPath;
  std::filesystem::path fullPythonBinPath;
  std::string originalEnvPath;
};

class DocsPythonVenvTest: public virtual BaseTest, public DocsPythonVenvBase {
protected:
  void
  SetUp() override {
    BaseTest::SetUp();
    DocsPythonVenvBase::SetUp();
  }

  void
  TearDown() override {
    DocsPythonVenvBase::TearDown();
    BaseTest::TearDown();
  }
};

class DocsWorkingDirectoryBase: public virtual BaseTest {
protected:
  void
  SetUp() override {
#if defined TESTS_ENABLE_VENV_TESTS && TESTS_ENABLE_VENV_TESTS == 1
    std::cout << "DocsWorkingDirectoryTest:: starting Fixture SetUp" << std::endl;

    temp_dir = TESTS_DOCS_DIR;
    std::cout << "DocsWorkingDirectoryTest:: temp_dir: " << temp_dir << std::endl;

    // change directory to `docs`
    original_dir = std::filesystem::current_path();  // save original directory
    std::cout << "DocsWorkingDirectoryTest:: original_dir: " << original_dir << std::endl;
    std::filesystem::current_path(temp_dir);
    std::cout << "DocsWorkingDirectoryTest:: working directory set to: " << std::filesystem::current_path() << std::endl;

    std::cout << "DocsWorkingDirectoryTest:: finished Fixture SetUp" << std::endl;
#endif
  }

  void
  TearDown() override {
#if defined TESTS_ENABLE_VENV_TESTS && TESTS_ENABLE_VENV_TESTS == 1
    std::cout << "DocsWorkingDirectoryTest:: starting Fixture TearDown" << std::endl;

    // change directory back to original
    std::filesystem::current_path(original_dir);
    std::cout << "DocsWorkingDirectoryTest:: working directory set to: " << std::filesystem::current_path() << std::endl;

    std::cout << "DocsWorkingDirectoryTest:: finished Fixture TearDown" << std::endl;
#endif
  }

  // functions and variables
  std::filesystem::path original_dir;
  std::filesystem::path temp_dir;
};

class DocsWorkingDirectoryTest: public virtual BaseTest, public DocsWorkingDirectoryBase {
protected:
  void
  SetUp() override {
    BaseTest::SetUp();
    DocsWorkingDirectoryBase::SetUp();
  }

  void
  TearDown() override {
    DocsWorkingDirectoryBase::TearDown();
    BaseTest::TearDown();
  }
};

class DocsTestFixture: public virtual BaseTest, public DocsPythonVenvBase, public DocsWorkingDirectoryBase {
protected:
  void
  SetUp() override {
    BaseTest::SetUp();
    DocsPythonVenvBase::SetUp();
    DocsWorkingDirectoryBase::SetUp();
  }

  void
  TearDown() override {
    DocsWorkingDirectoryBase::TearDown();
    DocsPythonVenvBase::TearDown();
    BaseTest::TearDown();
  }
};
