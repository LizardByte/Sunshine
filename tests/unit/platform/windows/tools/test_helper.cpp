/**
 * @file tests/unit/platform/windows/tools/test_helper.cpp
 * @brief Test src/platform/windows/tools/helper.cpp output functions.
 */
#include "../../../../tests_common.h"

#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
  #include <fcntl.h>
  #include <io.h>
  #include <src/platform/windows/tools/helper.h>
  #include <Windows.h>
#endif

namespace {
  /**
   * @brief Helper class to capture console output for testing
   */
  class ConsoleCapture {
  public:
    ConsoleCapture() {
      // Save original cout buffer
      original_cout_buffer = std::cout.rdbuf();
      // Redirect cout to our stringstream
      std::cout.rdbuf(captured_output.rdbuf());
    }

    ~ConsoleCapture() {
      try {
        // Restore original cout buffer
        std::cout.rdbuf(original_cout_buffer);
      } catch (std::exception &e) {
        std::cerr << "Error restoring cout buffer: " << e.what() << std::endl;
      }
    }

    std::string get_output() const {
      return captured_output.str();
    }

    void clear() {
      captured_output.str("");
      captured_output.clear();
    }

  private:
    std::streambuf *original_cout_buffer;
    std::stringstream captured_output;
  };
}  // namespace

#ifdef _WIN32
/**
 * @brief Test fixture for output namespace functions
 */
class UtilityOutputTest: public testing::Test {  // NOSONAR
protected:
  void SetUp() override {
    capture = std::make_unique<ConsoleCapture>();
  }

  void TearDown() override {
    capture.reset();
  }

  std::unique_ptr<ConsoleCapture> capture;
};

TEST_F(UtilityOutputTest, NoNullWithValidString) {
  const wchar_t *test_string = L"Valid String";
  const wchar_t *result = output::no_null(test_string);

  EXPECT_EQ(result, test_string) << "Expected no change for valid string";
  EXPECT_STREQ(result, L"Valid String") << "Expected exact match for valid string";
}

TEST_F(UtilityOutputTest, NoNullWithNullString) {
  const wchar_t *null_string = nullptr;
  const wchar_t *result = output::no_null(null_string);

  EXPECT_NE(result, nullptr) << "Expected non-null result for null input";
  EXPECT_STREQ(result, L"Unknown") << "Expected 'Unknown' for null input";
}

TEST_F(UtilityOutputTest, SafeWcoutWithValidWideString) {
  std::wstring test_string = L"Hello World";

  capture->clear();
  output::safe_wcout(test_string);
  const std::string output = capture->get_output();

  // In test environment, WriteConsoleW will likely fail, so it should fall back to boost::locale conversion
  EXPECT_EQ(output, "Hello World") << "Expected exact string output from safe_wcout";
}

TEST_F(UtilityOutputTest, SafeWcoutWithEmptyWideString) {
  const std::wstring empty_string = L"";

  capture->clear();
  output::safe_wcout(empty_string);
  const std::string output = capture->get_output();

  // Empty string should return early without output
  EXPECT_TRUE(output.empty()) << "Empty wide string should produce no output";
}

TEST_F(UtilityOutputTest, SafeWcoutWithValidWideStringPointer) {
  const wchar_t *test_string = L"Test String";

  capture->clear();
  output::safe_wcout(test_string);
  const std::string output = capture->get_output();

  EXPECT_EQ(output, "Test String") << "Expected exact string output from safe_wcout with pointer";
}

TEST_F(UtilityOutputTest, SafeWcoutWithNullWideStringPointer) {
  const wchar_t *null_string = nullptr;

  capture->clear();
  output::safe_wcout(null_string);
  const std::string output = capture->get_output();

  EXPECT_EQ(output, "Unknown") << "Expected 'Unknown' output from safe_wcout with null pointer";
}

TEST_F(UtilityOutputTest, SafeCoutWithValidString) {
  const std::string test_string = "Hello World";

  capture->clear();
  output::safe_cout(test_string);
  const std::string output = capture->get_output();

  EXPECT_EQ(output, "Hello World") << "Expected exact string output from safe_cout";
}

TEST_F(UtilityOutputTest, SafeCoutWithEmptyString) {
  std::string empty_string = "";

  capture->clear();
  output::safe_cout(empty_string);
  const std::string output = capture->get_output();

  // Empty string should return early
  EXPECT_TRUE(output.empty()) << "Empty string should produce no output";
}

TEST_F(UtilityOutputTest, SafeCoutWithSpecialCharacters) {
  const std::string special_string = "Test\x{01}\x{02}\x{03}String";

  capture->clear();
  output::safe_cout(special_string);
  const std::string output = capture->get_output();

  // Should handle special characters without crashing
  EXPECT_FALSE(output.empty()) << "Expected some output from safe_cout with special chars";

  // The function should either succeed with boost::locale conversion or fall back to character replacement
  // In the fallback case, non-printable characters (\x{01}, \x{02}, \x{03}) should be replaced with '?'
  // So we expect either the original string or "Test???String"
  EXPECT_TRUE(output == "Test\x{01}\x{02}\x{03}String" || output == "Test???String")
    << "Expected either original string or fallback with '?' replacements, got: '" << output << "'";
}

TEST_F(UtilityOutputTest, OutputFieldWithWideStringPointer) {
  const wchar_t *test_value = L"Test Value";
  const std::string label = "Test Label";

  capture->clear();
  output::output_field(label, test_value);
  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Test Label : ") != std::string::npos) << "Expected label in output";
  EXPECT_TRUE(output.find("\n") != std::string::npos) << "Expected newline at the end of output";
}

TEST_F(UtilityOutputTest, OutputFieldWithNullWideStringPointer) {
  const wchar_t *null_value = nullptr;
  const std::string label = "Test Label";

  capture->clear();
  output::output_field(label, null_value);
  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Test Label : ") != std::string::npos) << "Expected label in output";
  EXPECT_TRUE(output.find("Unknown") != std::string::npos) << "Expected 'Unknown' for null value";
  EXPECT_TRUE(output.find("\n") != std::string::npos) << "Expected newline at the end of output";
}

TEST_F(UtilityOutputTest, OutputFieldWithRegularString) {
  const std::string test_value = "Test Value";
  const std::string label = "Test Label";

  capture->clear();
  output::output_field(label, test_value);
  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Test Label : ") != std::string::npos) << "Expected label in output";
  EXPECT_TRUE(output.find("\n") != std::string::npos) << "Expected newline at the end of output";
}

TEST_F(UtilityOutputTest, OutputFieldWithEmptyString) {
  const std::string empty_value = "";
  const std::string label = "Empty Label";

  capture->clear();
  output::output_field(label, empty_value);
  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Empty Label : ") != std::string::npos) << "Expected label in output";
  EXPECT_TRUE(output.find("\n") != std::string::npos) << "Expected newline at the end of output";
}

TEST_F(UtilityOutputTest, OutputFieldWithSpecialCharactersInString) {
  const std::string special_value = "Value\x{01}\x{02}\x{03}With\x{7F}Special";
  const std::string label = "Special Label";

  capture->clear();
  output::output_field(label, special_value);
  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Special Label : ") != std::string::npos) << "Expected label in output";
  EXPECT_TRUE(output.find("\n") != std::string::npos) << "Expected newline at the end of output";
}

TEST_F(UtilityOutputTest, OutputFieldLabelFormatting) {
  const std::string test_value = "Value";
  const std::string label = "My Label";

  capture->clear();
  output::output_field(label, test_value);
  const std::string output = capture->get_output();

  // Check that the format is "Label : Value\n"
  EXPECT_TRUE(output.find("My Label : ") == 0) << "Expected output to start with 'My Label : '";
  EXPECT_TRUE(output.back() == '\n') << "Expected output to end with newline character";
}

// Test case for multiple consecutive calls
TEST_F(UtilityOutputTest, MultipleOutputFieldCalls) {
  capture->clear();

  output::output_field("Label1", "Value1");
  output::output_field("Label2", L"Value2");
  output::output_field("Label3", std::string("Value3"));

  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Label1 : ") != std::string::npos) << "Expected 'Label1' in output";
  EXPECT_TRUE(output.find("Label2 : ") != std::string::npos) << "Expected 'Label2' in output";
  EXPECT_TRUE(output.find("Label3 : ") != std::string::npos) << "Expected 'Label3' in output";

  // Count newlines - should have 3
  const size_t newline_count = std::ranges::count(output, '\n');
  EXPECT_EQ(newline_count, 3);
}

// Test cases for actual Unicode symbols and special characters
TEST_F(UtilityOutputTest, OutputFieldWithQuotationMarks) {
  capture->clear();

  // Test with various quotation marks
  output::output_field("Single Quote", "Device 'Audio' Output");
  output::output_field("Double Quote", "Device \"Audio\" Output");
  output::output_field("Left Quote", "Device 'Audio' Output");
  output::output_field("Right Quote", "Device 'Audio' Output");
  output::output_field("Left Double Quote", "Device \u{201C}Audio\u{201D} Output");
  output::output_field("Right Double Quote", "Device \u{201C}Audio\u{201D} Output");

  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Single Quote : ") != std::string::npos) << "Expected 'Single Quote' in output";
  EXPECT_TRUE(output.find("Double Quote : ") != std::string::npos) << "Expected 'Double Quote' in output";
  EXPECT_TRUE(output.find("Left Quote : ") != std::string::npos) << "Expected 'Left Quote' in output";
  EXPECT_TRUE(output.find("Right Quote : ") != std::string::npos) << "Expected 'Right Quote' in output";
  EXPECT_TRUE(output.find("Left Double Quote : ") != std::string::npos) << "Expected 'Left Double Quote' in output";
  EXPECT_TRUE(output.find("Right Double Quote : ") != std::string::npos) << "Expected 'Right Double Quote' in output";
}

TEST_F(UtilityOutputTest, OutputFieldWithTrademarkSymbols) {
  capture->clear();

  // Test with trademark and copyright symbols
  output::output_field("Trademark", "Audio Device™");
  output::output_field("Registered", "Audio Device®");
  output::output_field("Copyright", "Audio Device©");
  output::output_field("Combined", "Realtek® Audio™");

  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Trademark : ") != std::string::npos) << "Expected 'Trademark' in output";
  EXPECT_TRUE(output.find("Registered : ") != std::string::npos) << "Expected 'Registered' in output";
  EXPECT_TRUE(output.find("Copyright : ") != std::string::npos) << "Expected 'Copyright' in output";
  EXPECT_TRUE(output.find("Combined : ") != std::string::npos) << "Expected 'Combined' in output";
}

TEST_F(UtilityOutputTest, OutputFieldWithAccentedCharacters) {
  capture->clear();

  // Test with accented characters that might appear in device names
  output::output_field("French Accents", "Haut-parleur à haute qualité");
  output::output_field("Spanish Accents", "Altavoz ñáéíóú");
  output::output_field("German Accents", "Lautsprecher äöü");
  output::output_field("Mixed Accents", "àáâãäåæçèéêë");

  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("French Accents : ") != std::string::npos) << "Expected 'French Accents' in output";
  EXPECT_TRUE(output.find("Spanish Accents : ") != std::string::npos) << "Expected 'Spanish Accents' in output";
  EXPECT_TRUE(output.find("German Accents : ") != std::string::npos) << "Expected 'German Accents' in output";
  EXPECT_TRUE(output.find("Mixed Accents : ") != std::string::npos) << "Expected 'Mixed Accents' in output";
}

TEST_F(UtilityOutputTest, OutputFieldWithSpecialSymbols) {
  capture->clear();

  // Test with various special symbols
  output::output_field("Math Symbols", "Audio @ 44.1kHz ± 0.1%");
  output::output_field("Punctuation", "Audio Device #1 & #2");
  output::output_field("Programming", "Device $%^&*()");
  output::output_field("Mixed Symbols", "Audio™ @#$%^&*()");

  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Math Symbols : ") != std::string::npos) << "Expected 'Math Symbols' in output";
  EXPECT_TRUE(output.find("Punctuation : ") != std::string::npos) << "Expected 'Punctuation' in output";
  EXPECT_TRUE(output.find("Programming : ") != std::string::npos) << "Expected 'Programming' in output";
  EXPECT_TRUE(output.find("Mixed Symbols : ") != std::string::npos) << "Expected 'Mixed Symbols' in output";
}

TEST_F(UtilityOutputTest, OutputFieldWithWideCharacterSymbols) {
  capture->clear();

  // Test with wide character symbols
  const wchar_t *device_with_quotes = L"Device 'Audio' Output";
  const wchar_t *device_with_trademark = L"Realtek® Audio™";
  const wchar_t *device_with_accents = L"Haut-parleur àáâãäåæçèéêë";
  const wchar_t *device_with_symbols = L"Audio ñáéíóú & symbols @#$%^&*()";

  output::output_field("Wide Quotes", device_with_quotes);
  output::output_field("Wide Trademark", device_with_trademark);
  output::output_field("Wide Accents", device_with_accents);
  output::output_field("Wide Symbols", device_with_symbols);

  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Wide Quotes : ") != std::string::npos) << "Expected 'Wide Quotes' in output";
  EXPECT_TRUE(output.find("Wide Trademark : ") != std::string::npos) << "Expected 'Wide Trademark' in output";
  EXPECT_TRUE(output.find("Wide Accents : ") != std::string::npos) << "Expected 'Wide Accents' in output";
  EXPECT_TRUE(output.find("Wide Symbols : ") != std::string::npos) << "Expected 'Wide Symbols' in output";
}

TEST_F(UtilityOutputTest, OutputFieldWithRealAudioDeviceNames) {
  capture->clear();

  // Test with realistic audio device names that might contain special characters
  output::output_field("Realtek Device", "Realtek® High Definition Audio");
  output::output_field("Creative Device", "Creative Sound Blaster™ X-Fi");
  output::output_field("Logitech Device", "Logitech G533 Gaming Headset");
  output::output_field("Bluetooth Device", "Sony WH-1000XM4 'Wireless' Headphones");
  output::output_field("USB Device", "USB Audio Device @ 48kHz");

  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Realtek Device : ") != std::string::npos) << "Expected 'Realtek Device' in output";
  EXPECT_TRUE(output.find("Creative Device : ") != std::string::npos) << "Expected 'Creative Device' in output";
  EXPECT_TRUE(output.find("Logitech Device : ") != std::string::npos) << "Expected 'Logitech Device' in output";
  EXPECT_TRUE(output.find("Bluetooth Device : ") != std::string::npos) << "Expected 'Bluetooth Device' in output";
  EXPECT_TRUE(output.find("USB Device : ") != std::string::npos) << "Expected 'USB Device' in output";
}

TEST_F(UtilityOutputTest, OutputFieldWithNullAndSpecialCharacters) {
  capture->clear();

  // Test null wide string with special characters in label
  const wchar_t *null_value = nullptr;
  output::output_field("Device™ with 'quotes'", null_value);
  output::output_field("Device àáâãäåæçèéêë", null_value);
  output::output_field("Device @#$%^&*()", null_value);

  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Device™ with 'quotes' : ") != std::string::npos) << "Expected 'Device™ with quotes' in output";
  EXPECT_TRUE(output.find("Device àáâãäåæçèéêë : ") != std::string::npos) << "Expected 'Device àáâãäåæçèéêë' in output";
  EXPECT_TRUE(output.find("Device @#$%^&*() : ") != std::string::npos) << "Expected 'Device @#$%^&*()' in output";

  // Should contain "Unknown" for null values
  size_t unknown_count = 0;
  size_t pos = 0;
  while ((pos = output.find("Unknown", pos)) != std::string::npos) {
    unknown_count++;
    pos += 7;  // length of "Unknown"
  }
  EXPECT_EQ(unknown_count, 3) << "Expected 'Unknown' to appear 3 times for null values";
}

TEST_F(UtilityOutputTest, OutputFieldWithEmptyAndSpecialCharacters) {
  capture->clear();

  // Test empty values with special character labels
  output::output_field("Empty Device™", "");
  output::output_field("Empty 'Quotes'", "");
  output::output_field("Empty àáâãäåæçèéêë", "");

  const std::string output = capture->get_output();

  EXPECT_TRUE(output.find("Empty Device™ : ") != std::string::npos) << "Expected 'Empty Device™' in output";
  EXPECT_TRUE(output.find("Empty 'Quotes' : ") != std::string::npos) << "Expected 'Empty Quotes' in output";
  EXPECT_TRUE(output.find("Empty àáâãäåæçèéêë : ") != std::string::npos) << "Expected 'Empty àáâãäåæçèéêë' in output";

  // Count newlines - should have 3
  const size_t newline_count = std::ranges::count(output, '\n');
  EXPECT_EQ(newline_count, 3) << "Expected 3 newlines for 3 output fields with empty values";
}

#else
// For non-Windows platforms, the output namespace doesn't exist
TEST(UtilityOutputTest, OutputNamespaceNotAvailableOnNonWindows) {
  GTEST_SKIP() << "output namespace is Windows-specific";
}
#endif
