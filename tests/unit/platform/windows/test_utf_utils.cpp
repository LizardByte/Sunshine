/**
 * @file tests/unit/platform/windows/test_utf_utils.cpp
 * @brief Test src/platform/windows/utf_utils.cpp UTF conversion functions.
 */
#include "../../../tests_common.h"

#include <iostream>
#include <string>

#ifdef _WIN32
  #include <src/platform/windows/utf_utils.h>
  #include <Windows.h>
#endif

#ifdef _WIN32
/**
 * @brief Test fixture for utf_utils namespace functions
 */
class UtfUtilsTest: public testing::Test {};

TEST_F(UtfUtilsTest, FromUtf8WithEmptyString) {
  const std::string empty_string = "";
  const std::wstring result = utf_utils::from_utf8(empty_string);

  EXPECT_TRUE(result.empty()) << "Empty UTF-8 string should produce empty wide string";
}

TEST_F(UtfUtilsTest, ToUtf8WithEmptyWideString) {
  const std::wstring empty_wstring = L"";
  const std::string result = utf_utils::to_utf8(empty_wstring);

  EXPECT_TRUE(result.empty()) << "Empty wide string should produce empty UTF-8 string";
}

TEST_F(UtfUtilsTest, FromUtf8WithBasicString) {
  const std::string test_string = "Hello World";
  const std::wstring result = utf_utils::from_utf8(test_string);

  EXPECT_EQ(result, L"Hello World") << "Basic ASCII string should convert correctly";
}

TEST_F(UtfUtilsTest, ToUtf8WithBasicWideString) {
  const std::wstring test_wstring = L"Hello World";
  const std::string result = utf_utils::to_utf8(test_wstring);

  EXPECT_EQ(result, "Hello World") << "Basic wide string should convert correctly";
}

TEST_F(UtfUtilsTest, RoundTripConversionBasic) {
  const std::string original = "Test String";
  const std::wstring wide = utf_utils::from_utf8(original);
  const std::string converted_back = utf_utils::to_utf8(wide);

  EXPECT_EQ(original, converted_back) << "Round trip conversion should preserve basic string";
}

TEST_F(UtfUtilsTest, FromUtf8WithQuotationMarks) {
  // Test various quotation marks that might appear in device names
  const std::string single_quote = "Device 'Audio' Output";
  const std::string double_quote = "Device \"Audio\" Output";
  const std::string left_quote = "Device \u{2018}Audio\u{2019} Output";  // Unicode left/right single quotes
  const std::string right_quote = "Device \u{2019}Audio\u{2018} Output";  // Unicode right/left single quotes
  const std::string left_double = "Device \u{201C}Audio\u{201D} Output";  // Unicode left/right double quotes
  const std::string right_double = "Device \u{201D}Audio\u{201C} Output";  // Unicode right/left double quotes

  const std::wstring result1 = utf_utils::from_utf8(single_quote);
  const std::wstring result2 = utf_utils::from_utf8(double_quote);
  const std::wstring result3 = utf_utils::from_utf8(left_quote);
  const std::wstring result4 = utf_utils::from_utf8(right_quote);
  const std::wstring result5 = utf_utils::from_utf8(left_double);
  const std::wstring result6 = utf_utils::from_utf8(right_double);

  EXPECT_EQ(result1, L"Device 'Audio' Output") << "Single quote conversion failed";
  EXPECT_EQ(result2, L"Device \"Audio\" Output") << "Double quote conversion failed";
  EXPECT_EQ(result3, L"Device \u{2018}Audio\u{2019} Output") << "Left quote conversion failed";
  EXPECT_EQ(result4, L"Device \u{2019}Audio\u{2018} Output") << "Right quote conversion failed";
  EXPECT_EQ(result5, L"Device \u{201C}Audio\u{201D} Output") << "Left double quote conversion failed";
  EXPECT_EQ(result6, L"Device \u{201D}Audio\u{201C} Output") << "Right double quote conversion failed";
}

TEST_F(UtfUtilsTest, FromUtf8WithTrademarkSymbols) {
  // Test trademark and copyright symbols
  const std::string trademark = "Audio Device™";
  const std::string registered = "Audio Device®";
  const std::string copyright = "Audio Device©";
  const std::string combined = "Realtek® Audio™";

  const std::wstring result1 = utf_utils::from_utf8(trademark);
  const std::wstring result2 = utf_utils::from_utf8(registered);
  const std::wstring result3 = utf_utils::from_utf8(copyright);
  const std::wstring result4 = utf_utils::from_utf8(combined);

  EXPECT_EQ(result1, L"Audio Device™") << "Trademark symbol conversion failed";
  EXPECT_EQ(result2, L"Audio Device®") << "Registered symbol conversion failed";
  EXPECT_EQ(result3, L"Audio Device©") << "Copyright symbol conversion failed";
  EXPECT_EQ(result4, L"Realtek® Audio™") << "Combined symbols conversion failed";
}

TEST_F(UtfUtilsTest, FromUtf8WithAccentedCharacters) {
  // Test accented characters that might appear in international device names
  const std::string french = "Haut-parleur à haute qualité";
  const std::string spanish = "Altavoz ñáéíóú";
  const std::string german = "Lautsprecher äöü";
  const std::string mixed = "àáâãäåæçèéêë";

  const std::wstring result1 = utf_utils::from_utf8(french);
  const std::wstring result2 = utf_utils::from_utf8(spanish);
  const std::wstring result3 = utf_utils::from_utf8(german);
  const std::wstring result4 = utf_utils::from_utf8(mixed);

  EXPECT_EQ(result1, L"Haut-parleur à haute qualité") << "French accents conversion failed";
  EXPECT_EQ(result2, L"Altavoz ñáéíóú") << "Spanish accents conversion failed";
  EXPECT_EQ(result3, L"Lautsprecher äöü") << "German accents conversion failed";
  EXPECT_EQ(result4, L"àáâãäåæçèéêë") << "Mixed accents conversion failed";
}

TEST_F(UtfUtilsTest, FromUtf8WithSpecialSymbols) {
  // Test various special symbols
  const std::string math_symbols = "Audio @ 44.1kHz ± 0.1%";
  const std::string punctuation = "Audio Device #1 & #2";
  const std::string programming = "Device $%^&*()";
  const std::string mixed_symbols = "Audio™ @#$%^&*()";

  const std::wstring result1 = utf_utils::from_utf8(math_symbols);
  const std::wstring result2 = utf_utils::from_utf8(punctuation);
  const std::wstring result3 = utf_utils::from_utf8(programming);
  const std::wstring result4 = utf_utils::from_utf8(mixed_symbols);

  EXPECT_EQ(result1, L"Audio @ 44.1kHz ± 0.1%") << "Math symbols conversion failed";
  EXPECT_EQ(result2, L"Audio Device #1 & #2") << "Punctuation conversion failed";
  EXPECT_EQ(result3, L"Device $%^&*()") << "Programming symbols conversion failed";
  EXPECT_EQ(result4, L"Audio™ @#$%^&*()") << "Mixed symbols conversion failed";
}

TEST_F(UtfUtilsTest, ToUtf8WithQuotationMarks) {
  // Test various quotation marks conversion from wide to UTF-8
  const std::wstring single_quote = L"Device 'Audio' Output";
  const std::wstring double_quote = L"Device \"Audio\" Output";
  const std::wstring left_quote = L"Device \u{2018}Audio\u{2019} Output";  // Unicode left/right single quotes
  const std::wstring right_quote = L"Device \u{2019}Audio\u{2018} Output";  // Unicode right/left single quotes
  const std::wstring left_double = L"Device \u{201C}Audio\u{201D} Output";  // Unicode left/right double quotes
  const std::wstring right_double = L"Device \u{201D}Audio\u{201C} Output";  // Unicode right/left double quotes

  const std::string result1 = utf_utils::to_utf8(single_quote);
  const std::string result2 = utf_utils::to_utf8(double_quote);
  const std::string result3 = utf_utils::to_utf8(left_quote);
  const std::string result4 = utf_utils::to_utf8(right_quote);
  const std::string result5 = utf_utils::to_utf8(left_double);
  const std::string result6 = utf_utils::to_utf8(right_double);

  EXPECT_EQ(result1, "Device 'Audio' Output") << "Single quote to UTF-8 conversion failed";
  EXPECT_EQ(result2, "Device \"Audio\" Output") << "Double quote to UTF-8 conversion failed";
  EXPECT_EQ(result3, "Device \u{2018}Audio\u{2019} Output") << "Left quote to UTF-8 conversion failed";
  EXPECT_EQ(result4, "Device \u{2019}Audio\u{2018} Output") << "Right quote to UTF-8 conversion failed";
  EXPECT_EQ(result5, "Device \u{201C}Audio\u{201D} Output") << "Left double quote to UTF-8 conversion failed";
  EXPECT_EQ(result6, "Device \u{201D}Audio\u{201C} Output") << "Right double quote to UTF-8 conversion failed";
}

TEST_F(UtfUtilsTest, ToUtf8WithTrademarkSymbols) {
  // Test trademark and copyright symbols conversion from wide to UTF-8
  const std::wstring trademark = L"Audio Device™";
  const std::wstring registered = L"Audio Device®";
  const std::wstring copyright = L"Audio Device©";
  const std::wstring combined = L"Realtek® Audio™";

  const std::string result1 = utf_utils::to_utf8(trademark);
  const std::string result2 = utf_utils::to_utf8(registered);
  const std::string result3 = utf_utils::to_utf8(copyright);
  const std::string result4 = utf_utils::to_utf8(combined);

  EXPECT_EQ(result1, "Audio Device™") << "Trademark symbol to UTF-8 conversion failed";
  EXPECT_EQ(result2, "Audio Device®") << "Registered symbol to UTF-8 conversion failed";
  EXPECT_EQ(result3, "Audio Device©") << "Copyright symbol to UTF-8 conversion failed";
  EXPECT_EQ(result4, "Realtek® Audio™") << "Combined symbols to UTF-8 conversion failed";
}

TEST_F(UtfUtilsTest, RoundTripConversionWithSpecialCharacters) {
  // Test round trip conversion with various special characters
  const std::string quotes = "Device 'Audio' with \u{201C}Special\u{201D} Characters";
  const std::string symbols = "Realtek® Audio™ @ 44.1kHz ± 0.1%";
  const std::string accents = "Haut-parleur àáâãäåæçèéêë";
  const std::string mixed = "Audio™ 'Device' @#$%^&*() ñáéíóú";

  // Convert to wide and back
  const std::wstring wide1 = utf_utils::from_utf8(quotes);
  const std::wstring wide2 = utf_utils::from_utf8(symbols);
  const std::wstring wide3 = utf_utils::from_utf8(accents);
  const std::wstring wide4 = utf_utils::from_utf8(mixed);

  const std::string back1 = utf_utils::to_utf8(wide1);
  const std::string back2 = utf_utils::to_utf8(wide2);
  const std::string back3 = utf_utils::to_utf8(wide3);
  const std::string back4 = utf_utils::to_utf8(wide4);

  EXPECT_EQ(quotes, back1) << "Round trip failed for quotes";
  EXPECT_EQ(symbols, back2) << "Round trip failed for symbols";
  EXPECT_EQ(accents, back3) << "Round trip failed for accents";
  EXPECT_EQ(mixed, back4) << "Round trip failed for mixed characters";
}

TEST_F(UtfUtilsTest, RealAudioDeviceNames) {
  // Test with realistic audio device names that contain special characters
  const std::string realtek = "Realtek® High Definition Audio";
  const std::string creative = "Creative Sound Blaster™ X-Fi";
  const std::string logitech = "Logitech G533 Gaming Headset";
  const std::string bluetooth = "Sony WH-1000XM4 'Wireless' Headphones";
  const std::string usb = "USB Audio Device @ 48kHz";

  // Test conversion to wide
  const std::wstring wide_realtek = utf_utils::from_utf8(realtek);
  const std::wstring wide_creative = utf_utils::from_utf8(creative);
  const std::wstring wide_logitech = utf_utils::from_utf8(logitech);
  const std::wstring wide_bluetooth = utf_utils::from_utf8(bluetooth);
  const std::wstring wide_usb = utf_utils::from_utf8(usb);

  EXPECT_FALSE(wide_realtek.empty()) << "Realtek device name conversion failed";
  EXPECT_FALSE(wide_creative.empty()) << "Creative device name conversion failed";
  EXPECT_FALSE(wide_logitech.empty()) << "Logitech device name conversion failed";
  EXPECT_FALSE(wide_bluetooth.empty()) << "Bluetooth device name conversion failed";
  EXPECT_FALSE(wide_usb.empty()) << "USB device name conversion failed";

  // Test round trip
  EXPECT_EQ(realtek, utf_utils::to_utf8(wide_realtek)) << "Realtek round trip failed";
  EXPECT_EQ(creative, utf_utils::to_utf8(wide_creative)) << "Creative round trip failed";
  EXPECT_EQ(logitech, utf_utils::to_utf8(wide_logitech)) << "Logitech round trip failed";
  EXPECT_EQ(bluetooth, utf_utils::to_utf8(wide_bluetooth)) << "Bluetooth round trip failed";
  EXPECT_EQ(usb, utf_utils::to_utf8(wide_usb)) << "USB round trip failed";
}

TEST_F(UtfUtilsTest, InvalidUtf8Sequences) {
  // Test with invalid UTF-8 sequences - should return empty string
  const std::string invalid1 = "Test\x{FF}\x{FE}\x{FD}";  // Invalid UTF-8 bytes
  const std::string invalid2 = "Test\x{80}\x{81}\x{82}";  // Invalid continuation bytes

  const std::wstring result1 = utf_utils::from_utf8(invalid1);
  const std::wstring result2 = utf_utils::from_utf8(invalid2);

  // The function should return empty string for invalid UTF-8 sequences
  EXPECT_TRUE(result1.empty()) << "Invalid UTF-8 sequence should return empty string";
  EXPECT_TRUE(result2.empty()) << "Invalid UTF-8 sequence should return empty string";
}

TEST_F(UtfUtilsTest, LongStringsWithSpecialCharacters) {
  // Test with longer strings containing many special characters
  std::string long_special = "Device™ with 'special' characters: àáâãäåæçèéêë ñáéíóú äöü ";
  for (int i = 0; i < 10; ++i) {
    long_special += "Audio® Device™ @#$%^&*() ";
  }

  const std::wstring wide_result = utf_utils::from_utf8(long_special);
  const std::string back_result = utf_utils::to_utf8(wide_result);

  EXPECT_FALSE(wide_result.empty()) << "Long string conversion should not be empty";
  EXPECT_EQ(long_special, back_result) << "Long string round trip should preserve content";
}

#else
// For non-Windows platforms, the utf_utils namespace doesn't exist
TEST(UtfUtilsTest, UtfUtilsNotAvailableOnNonWindows) {
  GTEST_SKIP() << "utf_utils namespace is Windows-specific";
}
#endif
