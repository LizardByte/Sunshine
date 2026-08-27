/**
 * @file tests/integration/test_locale_consistency.cpp
 * @brief Test locale consistency across configuration files and locale JSON files
 */
#include "../tests_common.h"

// standard includes
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

// local includes
#include "src/file_handler.h"

namespace fs = std::filesystem;

class LocaleConsistencyTest: public BaseTest {
protected:
  // Extract locale options from config.cpp
  static std::set<std::string, std::less<>> extractConfigCppLocales() {
    std::set<std::string, std::less<>> locales;
    const std::string content = file_handler::read_file("src/config.cpp");

    // Find the string_restricted_f call for locale
    const std::regex localeSection(R"(string_restricted_f\s*\(\s*vars\s*,\s*"locale"[^}]*\{([^}]*)\})");

    if (std::smatch match; std::regex_search(content, match, localeSection)) {
      const std::string localeList = match[1].str();

      // Extract individual locale codes
      const std::regex localePattern(R"delimiter("([^"]+)"sv)delimiter");
      std::sregex_iterator iter(localeList.begin(), localeList.end(), localePattern);

      for (const std::sregex_iterator end; iter != end; ++iter) {
        locales.insert((*iter)[1].str());
      }
    }

    return locales;
  }

  // Extract SUPPORTED_LOCALES entries from locale.js
  static std::map<std::string, std::string, std::less<>> extractSupportedLocales() {
    std::map<std::string, std::string, std::less<>> locales;
    const std::string content = file_handler::read_file("src_assets/common/assets/web/src/utils/locale.js");

    // Find the SUPPORTED_LOCALES array specifically
    const std::regex supportedLocalesPattern(R"(SUPPORTED_LOCALES\s*=\s*\[([^\]]*)\])");

    if (std::smatch arrayMatch; std::regex_search(content, arrayMatch, supportedLocalesPattern)) {
      const std::string localesSection = arrayMatch[1].str();

      // Extract {value: '...', label: '...'} entries from the array
      const std::regex entryPattern(R"delimiter(value:\s*'([^']+)',\s*label:\s*'([^']*)')delimiter");
      std::sregex_iterator iter(localesSection.begin(), localesSection.end(), entryPattern);

      for (const std::sregex_iterator end; iter != end; ++iter) {
        const std::string localeCode = (*iter)[1].str();
        const std::string displayName = (*iter)[2].str();
        locales[localeCode] = displayName;
      }
    }

    return locales;
  }

  // Get available locale JSON files
  static std::set<std::string, std::less<>> getAvailableLocaleFiles() {
    std::set<std::string, std::less<>> locales;
    const std::filesystem::path localeDir = "src_assets/common/assets/web/public/assets/locale";

    if (!fs::exists(localeDir)) {
      return locales;
    }

    for (const auto &entry : fs::directory_iterator(localeDir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        const std::string filename = entry.path().stem().string();
        locales.insert(filename);
      }
    }

    return locales;
  }

  // Helper function to check if a locale JSON file is valid using nlohmann/json
  static bool isValidLocaleFile(const std::string &localeCode) {
    const std::string filePath = std::format("src_assets/common/assets/web/public/assets/locale/{}.json", localeCode);

    if (!fs::exists(filePath)) {
      return false;
    }

    try {
      const std::string content = file_handler::read_file(filePath.c_str());

      // Parse JSON using nlohmann/json to validate it's properly formatted
      const nlohmann::json localeJson = nlohmann::json::parse(content);

      // Basic validation - should be a JSON object with some content
      return localeJson.is_object() && !localeJson.empty();
    } catch (const nlohmann::json::parse_error &) {
      return false;
    }
  }
};

TEST_F(LocaleConsistencyTest, AllLocaleFilesHaveConfigCppEntries) {
  const auto configLocales = extractConfigCppLocales();
  const auto localeFiles = getAvailableLocaleFiles();

  std::vector<std::string> missingFromConfig;

  // Check that every locale file has a corresponding entry in config.cpp
  for (const auto &localeFile : localeFiles) {
    if (!configLocales.contains(localeFile)) {
      missingFromConfig.push_back(localeFile);
    }
  }

  if (!missingFromConfig.empty()) {
    std::string errorMsg = "Locale files missing from config.cpp:\n";
    for (const auto &missing : missingFromConfig) {
      errorMsg += std::format("  {}.json\n", missing);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(LocaleConsistencyTest, AllLocaleFilesHaveSupportedLocalesEntries) {
  const auto supportedLocales = extractSupportedLocales();
  const auto localeFiles = getAvailableLocaleFiles();

  std::vector<std::string> missingFromVue;

  // Check that every locale file has a corresponding entry in locale.js
  for (const auto &localeFile : localeFiles) {
    if (!supportedLocales.contains(localeFile)) {
      missingFromVue.push_back(localeFile);
    }
  }

  if (!missingFromVue.empty()) {
    std::string errorMsg = "Locale files missing from locale.js:\n";
    for (const auto &missing : missingFromVue) {
      errorMsg += std::format("  {}.json\n", missing);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(LocaleConsistencyTest, AllConfigCppLocalesHaveFiles) {
  const auto configLocales = extractConfigCppLocales();
  const auto localeFiles = getAvailableLocaleFiles();

  std::vector<std::string> missingFiles;

  // Check that every config.cpp locale has a corresponding JSON file
  for (const auto &configLocale : configLocales) {
    if (!localeFiles.contains(configLocale)) {
      missingFiles.push_back(configLocale);
    }
  }

  if (!missingFiles.empty()) {
    std::string errorMsg = "config.cpp locales missing JSON files:\n";
    for (const auto &missing : missingFiles) {
      errorMsg += std::format("  {}.json\n", missing);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(LocaleConsistencyTest, AllSupportedLocalesHaveFiles) {
  const auto supportedLocales = extractSupportedLocales();
  const auto localeFiles = getAvailableLocaleFiles();

  std::vector<std::string> missingFiles;

  // Check that every locale.js locale has a corresponding JSON file
  for (const auto &supportedLocale : supportedLocales | std::views::keys) {
    if (!localeFiles.contains(supportedLocale)) {
      missingFiles.push_back(supportedLocale);
    }
  }

  if (!missingFiles.empty()) {
    std::string errorMsg = "locale.js locales missing JSON files:\n";
    for (const auto &missing : missingFiles) {
      errorMsg += std::format("  {}.json\n", missing);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(LocaleConsistencyTest, ConfigCppAndSupportedLocalesMatch) {
  const auto configLocales = extractConfigCppLocales();
  const auto supportedLocales = extractSupportedLocales();

  std::vector<std::string> configOnlyLocales;
  std::vector<std::string> vueOnlyLocales;

  // Find locales in config.cpp but not in locale.js
  for (const auto &configLocale : configLocales) {
    if (!supportedLocales.contains(configLocale)) {
      configOnlyLocales.push_back(configLocale);
    }
  }

  // Find locales in locale.js but not in config.cpp
  for (const auto &supportedLocale : supportedLocales | std::views::keys) {
    if (!configLocales.contains(supportedLocale)) {
      vueOnlyLocales.push_back(supportedLocale);
    }
  }

  std::string errorMsg;

  if (!configOnlyLocales.empty()) {
    errorMsg += "Locales in config.cpp but not in locale.js:\n";
    for (const auto &locale : configOnlyLocales) {
      errorMsg += std::format("  {}\n", locale);
    }
  }

  if (!vueOnlyLocales.empty()) {
    errorMsg += "Locales in locale.js but not in config.cpp:\n";
    for (const auto &locale : vueOnlyLocales) {
      errorMsg += std::format("  {}\n", locale);
    }
  }

  if (!errorMsg.empty()) {
    FAIL() << errorMsg;
  }
}

TEST_F(LocaleConsistencyTest, AllLocaleFilesAreValid) {
  const auto localeFiles = getAvailableLocaleFiles();
  std::vector<std::string> invalidFiles;

  // Check that all locale files are valid JSON
  for (const auto &localeFile : localeFiles) {
    if (!isValidLocaleFile(localeFile)) {
      invalidFiles.push_back(localeFile);
    }
  }

  if (!invalidFiles.empty()) {
    std::string errorMsg = "Invalid locale files found:\n";
    for (const auto &invalid : invalidFiles) {
      errorMsg += std::format("  {}.json\n", invalid);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(LocaleConsistencyTest, LocaleDisplayNamesAreConsistent) {
  const auto supportedLocales = extractSupportedLocales();
  const auto localeFiles = getAvailableLocaleFiles();
  std::vector<std::string> inconsistentDisplayNames;

  // Check that all locales in locale.js have corresponding JSON files
  for (const auto &[localeCode, displayName] : supportedLocales) {
    if (!localeFiles.contains(localeCode)) {
      inconsistentDisplayNames.push_back(
        std::format("{}: has display name '{}' but no corresponding JSON file exists", localeCode, displayName)
      );
    }
  }

  // Also check that locale files that exist have entries in locale.js
  for (const auto &localeFile : localeFiles) {
    if (!supportedLocales.contains(localeFile)) {
      inconsistentDisplayNames.push_back(
        std::format("{}: has JSON file but no display name in locale.js", localeFile)
      );
    }
  }

  if (!inconsistentDisplayNames.empty()) {
    std::string errorMsg = "Locale display name inconsistencies found:\n";
    for (const auto &inconsistent : inconsistentDisplayNames) {
      errorMsg += std::format("  {}\n", inconsistent);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(LocaleConsistencyTest, NoOrphanedLocaleReferences) {
  const auto configLocales = extractConfigCppLocales();
  const auto supportedLocales = extractSupportedLocales();
  const auto localeFiles = getAvailableLocaleFiles();

  std::vector<std::string> orphanedReferences;

  // Check for locale references that don't have corresponding files
  for (const auto &configLocale : configLocales) {
    if (!localeFiles.contains(configLocale)) {
      orphanedReferences.push_back(std::format("config.cpp references missing file: {}.json", configLocale));
    }
  }

  for (const auto &supportedLocale : supportedLocales | std::views::keys) {
    if (!localeFiles.contains(supportedLocale)) {
      orphanedReferences.push_back(std::format("locale.js references missing file: {}.json", supportedLocale));
    }
  }

  if (!orphanedReferences.empty()) {
    std::string errorMsg = "Orphaned locale references found:\n";
    for (const auto &orphaned : orphanedReferences) {
      errorMsg += std::format("  {}\n", orphaned);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(LocaleConsistencyTest, TestFrameworkDetectsLocaleInconsistencies) {
  // Test the framework by simulating a missing locale scenario
  const std::string testLocale = "test_framework_validation_locale";

  auto configLocales = extractConfigCppLocales();
  auto supportedLocales = extractSupportedLocales();
  const auto localeFiles = getAvailableLocaleFiles();

  // Add a fake locale to config to simulate a missing file
  configLocales.insert(testLocale);

  std::vector<std::string> missingFiles;
  for (const auto &configLocale : configLocales) {
    if (!localeFiles.contains(configLocale)) {
      missingFiles.push_back(configLocale);
    }
  }

  // Verify the test framework detects the missing fake locale
  bool foundMissingTestLocale = false;
  for (const auto &missing : missingFiles) {
    if (missing == testLocale) {
      foundMissingTestLocale = true;
      break;
    }
  }

  EXPECT_TRUE(foundMissingTestLocale) << "Test framework failed to detect missing locale file";
  EXPECT_GE(missingFiles.size(), 1) << "Test framework should detect at least the fake missing locale";
}
