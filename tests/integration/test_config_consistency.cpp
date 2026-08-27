/**
 * @file tests/integration/test_config_consistency.cpp
 * @brief Test configuration consistency across all configuration files
 */
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <ranges>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include "src/file_handler.h"

class ConfigConsistencyTest: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    // Define the expected mapping between documentation sections and UI tabs
    expectedDocToTabMapping = {
      {"General", "general"},
      {"Input", "input"},
      {"Audio/Video", "av"},
      {"Network", "network"},
      {"Config Files", "files"},
      {"Advanced", "advanced"},
      {"NVIDIA NVENC Encoder", "nv"},
      {"Intel QuickSync Encoder", "qsv"},
      {"AMD AMF Encoder", "amd"},
      {"VideoToolbox Encoder", "vt"},
      {"VA-API Encoder", "vaapi"},
      {"Vulkan Encoder", "vulkan"},
      {"Software Encoder", "sw"}
    };
  }

  // Extract config options from config.cpp - the authoritative source
  static std::set<std::string, std::less<>> extractConfigCppOptions() {
    std::set<std::string, std::less<>> options;
    std::string content = file_handler::read_file("src/config.cpp");

    // Regex patterns to match different config option types in config.cpp
    const std::vector patterns = {
      std::regex(R"DELIM((?:string_f|path_f|string_restricted_f)\s*\(\s*vars\s*,\s*"([^"]+)")DELIM"),
      std::regex(R"DELIM((?:int_f|int_between_f)\s*\(\s*vars\s*,\s*"([^"]+)")DELIM"),
      std::regex(R"DELIM(bool_f\s*\(\s*vars\s*,\s*"([^"]+)")DELIM"),
      std::regex(R"DELIM((?:double_f|double_between_f)\s*\(\s*vars\s*,\s*"([^"]+)")DELIM"),
      std::regex(R"DELIM(generic_f\s*\(\s*vars\s*,\s*"([^"]+)")DELIM"),
      std::regex(R"DELIM(list_prep_cmd_f\s*\(\s*vars\s*,\s*"([^"]+)")DELIM"),
      std::regex(R"DELIM(map_int_int_f\s*\(\s*vars\s*,\s*"([^"]+)")DELIM")
    };

    for (const auto &pattern : patterns) {
      std::sregex_iterator iter(content.begin(), content.end(), pattern);

      for (std::sregex_iterator end; iter != end; ++iter) {
        std::string optionName = (*iter)[1].str();
        options.insert(optionName);
      }
    }

    return options;
  }

  // Helper function to find brace boundaries
  static size_t findClosingBrace(const std::string &content, const size_t start) {
    size_t pos = start + 1;
    int braceLevel = 1;

    while (pos < content.length() && braceLevel > 0) {
      if (content[pos] == '{') {
        braceLevel++;
      } else if (content[pos] == '}') {
        braceLevel--;
      }
      pos++;
    }

    return pos - 1;
  }

  // Maps a tab id to the component file that declares its config OPTIONS.
  static std::string tabIdToFilePath(const std::string &tabId) {
    static const std::map<std::string, std::string, std::less<>> pathById = {
      {"general", "src_assets/common/assets/web/src/components/configs/tabs/General.vue"},
      {"input", "src_assets/common/assets/web/src/components/configs/tabs/Inputs.vue"},
      {"av", "src_assets/common/assets/web/src/components/configs/tabs/AudioVideo.vue"},
      {"network", "src_assets/common/assets/web/src/components/configs/tabs/Network.vue"},
      {"files", "src_assets/common/assets/web/src/components/configs/tabs/Files.vue"},
      {"advanced", "src_assets/common/assets/web/src/components/configs/tabs/Advanced.vue"},
      {"nv", "src_assets/common/assets/web/src/components/configs/tabs/encoders/NvidiaNvencEncoder.vue"},
      {"qsv", "src_assets/common/assets/web/src/components/configs/tabs/encoders/IntelQuickSyncEncoder.vue"},
      {"amd", "src_assets/common/assets/web/src/components/configs/tabs/encoders/AmdAmfEncoder.vue"},
      {"vt", "src_assets/common/assets/web/src/components/configs/tabs/encoders/VideotoolboxEncoder.vue"},
      {"vaapi", "src_assets/common/assets/web/src/components/configs/tabs/encoders/VAAPIEncoder.vue"},
      {"vulkan", "src_assets/common/assets/web/src/components/configs/tabs/encoders/VulkanEncoder.vue"},
      {"sw", "src_assets/common/assets/web/src/components/configs/tabs/encoders/SoftwareEncoder.vue"},
    };
    const auto it = pathById.find(tabId);
    return it != pathById.end() ? it->second : "";
  }

  // Extract every tab id declared in ConfigView.vue's tabs.general/tabs.encoders arrays.
  static std::vector<std::string> extractConfigViewTabIds() {
    std::vector<std::string> ids;
    const std::string content = file_handler::read_file("src_assets/common/assets/web/src/views/ConfigView.vue");

    const size_t tabsStart = content.find("tabs: {");
    if (tabsStart == std::string::npos) {
      return ids;
    }

    const size_t braceStart = content.find('{', tabsStart);
    const size_t braceEnd = findClosingBrace(content, braceStart);
    const std::string tabsSection = content.substr(braceStart + 1, braceEnd - braceStart - 1);

    const std::regex idPattern(R"DELIM(id:\s*"([^"]+)")DELIM");
    std::sregex_iterator iter(tabsSection.begin(), tabsSection.end(), idPattern);

    for (const std::sregex_iterator end; iter != end; ++iter) {
      ids.push_back((*iter)[1].str());
    }

    return ids;
  }

  // Extract the option keys declared in a tab component's OPTIONS constant.
  static std::vector<std::string> extractOptionsFromTabFile(const std::string &filePath) {
    std::vector<std::string> keys;
    const std::string content = file_handler::read_file(filePath.c_str());

    // Anchored to "export const OPTIONS = {" specifically - a plain "OPTIONS = {" search would
    // also match inside an unrelated constant like "CAPTURE_OPTIONS = {" or "ENCODER_OPTIONS = {".
    const size_t optionsStart = content.find("export const OPTIONS = {");
    if (optionsStart == std::string::npos) {
      return keys;
    }

    const size_t braceStart = content.find('{', optionsStart);
    const size_t braceEnd = findClosingBrace(content, braceStart);
    const std::string optionsSection = content.substr(braceStart + 1, braceEnd - braceStart - 1);

    const std::regex keyPattern(R"DELIM("([^"]+)":\s*)DELIM");
    std::sregex_iterator iter(optionsSection.begin(), optionsSection.end(), keyPattern);

    for (const std::sregex_iterator end; iter != end; ++iter) {
      keys.push_back((*iter)[1].str());
    }

    return keys;
  }

  // Helper function to trim whitespace from string
  static void trimWhitespace(std::string &str) {
    str.erase(str.find_last_not_of(" \t\r\n") + 1);
  }

  // Helper function to extract option name from the Markdown line
  static std::string extractOptionFromMarkdownLine(const std::string &line) {
    const std::regex optionPattern(R"(^### ([^#\r\n]+))");
    if (std::smatch optionMatch; std::regex_search(line, optionMatch, optionPattern)) {
      std::string optionName = optionMatch[1].str();
      trimWhitespace(optionName);
      return optionName;
    }
    return "";
  }

  // Extract config options from each tab component's OPTIONS constant, with order preserved.
  static std::map<std::string, std::vector<std::string>, std::less<>> extractConfigViewOptionsWithOrder() {
    std::map<std::string, std::vector<std::string>, std::less<>> optionsByTab;

    for (const auto &tabId : extractConfigViewTabIds()) {
      const std::string filePath = tabIdToFilePath(tabId);
      if (filePath.empty()) {
        continue;
      }

      optionsByTab[tabId] = extractOptionsFromTabFile(filePath);
    }

    return optionsByTab;
  }

  // Extract config options from each tab component's OPTIONS constant, keyed by tab id.
  static std::map<std::string, std::string, std::less<>> extractConfigViewOptions() {
    std::map<std::string, std::string, std::less<>> options;

    for (const auto &[tabId, optionNames] : extractConfigViewOptionsWithOrder()) {
      for (const auto &optionName : optionNames) {
        options[optionName] = tabId;
      }
    }

    return options;
  }

  // Helper function to process markdown line for section headers
  static bool processSectionHeader(const std::string &line, std::string &currentSection) {
    const std::regex sectionPattern(R"(^## ([^#\r\n]+))");

    if (std::smatch sectionMatch; std::regex_search(line, sectionMatch, sectionPattern)) {
      currentSection = sectionMatch[1].str();
      trimWhitespace(currentSection);
      return true;
    }

    return false;
  }

  // Helper function to process markdown line for option headers
  static bool processOptionHeader(const std::string &line, const std::string_view currentSection, std::map<std::string, std::string, std::less<>> &options) {
    if (currentSection.empty()) {
      return false;
    }

    if (const std::string optionName = extractOptionFromMarkdownLine(line); !optionName.empty()) {
      options[optionName] = currentSection;
      return true;
    }

    return false;
  }

  // Extract config options from configuration.md
  static std::map<std::string, std::string, std::less<>> extractConfigMdOptions() {
    std::map<std::string, std::string, std::less<>> options;
    const std::string content = file_handler::read_file("docs/configuration.md");

    std::istringstream stream(content);
    std::string line;
    std::string currentSection;

    while (std::getline(stream, line)) {
      if (processSectionHeader(line, currentSection)) {
        continue;
      }

      processOptionHeader(line, currentSection, options);
    }

    return options;
  }

  // Helper function to process markdown option line for order-preserved extraction
  static void processMarkdownOptionLine(const std::string &line, const std::string &currentSection, std::map<std::string, std::vector<std::string>, std::less<>> &optionsBySection) {
    if (currentSection.empty()) {
      return;
    }

    if (const std::string optionName = extractOptionFromMarkdownLine(line); !optionName.empty()) {
      optionsBySection[currentSection].push_back(optionName);
    }
  }

  // Extract config options from configuration.md with order preserved
  static std::map<std::string, std::vector<std::string>, std::less<>> extractConfigMdOptionsWithOrder() {
    std::map<std::string, std::vector<std::string>, std::less<>> optionsBySection;
    const std::string content = file_handler::read_file("docs/configuration.md");

    std::istringstream stream(content);
    std::string line;
    std::string currentSection;

    while (std::getline(stream, line)) {
      if (processSectionHeader(line, currentSection)) {
        continue;
      }

      processMarkdownOptionLine(line, currentSection, optionsBySection);
    }

    return optionsBySection;
  }

  // Helper function to find the config section end
  static size_t findConfigSectionEnd(const std::string &content, size_t configStart) {
    size_t braceCount = 1;
    size_t configEnd = configStart;

    while (configStart < content.length() && braceCount > 0) {
      if (content[configStart] == '{') {
        braceCount++;
      } else if (content[configStart] == '}') {
        braceCount--;
      }
      configEnd = configStart;
      configStart++;
    }

    return configEnd;
  }

  // Helper function to extract keys from a config section
  static void extractKeysFromConfigSection(const std::string_view configSection, std::set<std::string, std::less<>> &options) {
    const std::regex keyPattern(R"DELIM("([^"]+)":\s*)DELIM");
    std::string configStr(configSection);
    std::sregex_iterator iter(configStr.begin(), configStr.end(), keyPattern);

    for (const std::sregex_iterator end; iter != end; ++iter) {
      options.insert((*iter)[1].str());
    }
  }

  // Extract config options from en.json
  static std::set<std::string, std::less<>> extractEnJsonConfigOptions() {
    std::set<std::string, std::less<>> options;
    const std::string content = file_handler::read_file("src_assets/common/assets/web/public/assets/locale/en.json");

    // Look for the config section
    const std::regex configSectionPattern(R"DELIM("config":\s*\{)DELIM");
    std::smatch match;

    if (!std::regex_search(content, match, configSectionPattern)) {
      return options;
    }

    // Find the config section and extract keys
    const size_t configStart = match.position() + match.length();
    const size_t configEnd = findConfigSectionEnd(content, configStart);
    const std::string configSection = content.substr(configStart, configEnd - configStart);

    extractKeysFromConfigSection(configSection, options);

    return options;
  }

  std::map<std::string, std::string, std::less<>> expectedDocToTabMapping;

  // Helper function to check if an option exists in ConfigView.vue's options
  static bool isOptionInConfigView(const std::string &option, const std::map<std::string, std::string, std::less<>> &configViewOptions) {
    return configViewOptions.contains(option);
  }

  // Helper function to check if an option exists in MD options
  static bool isOptionInMd(const std::string &option, const std::map<std::string, std::string, std::less<>> &mdOptions) {
    return mdOptions.contains(option);
  }

  // Helper function to validate option existence across files
  static void validateOptionExistence(const std::string &option, const std::map<std::string, std::string, std::less<>> &configViewOptions, const std::map<std::string, std::string, std::less<>> &mdOptions, const std::set<std::string, std::less<>> &jsonOptions, std::vector<std::string> &missingFromFiles) {
    if (!isOptionInConfigView(option, configViewOptions)) {
      missingFromFiles.push_back(std::format("ConfigView.vue missing: {}", option));
    }

    if (!isOptionInMd(option, mdOptions)) {
      missingFromFiles.push_back(std::format("configuration.md missing: {}", option));
    }

    if (!jsonOptions.contains(option)) {
      missingFromFiles.push_back(std::format("en.json missing: {}", option));
    }
  }

  // Helper function to check tab correspondence with documentation sections
  static void checkTabCorrespondence(const std::string &tab, const std::map<std::string, std::string, std::less<>> &expectedDocToTabMapping, const std::set<std::string, std::less<>> &mdSections, std::vector<std::string> &inconsistencies) {
    bool found = false;

    for (const auto &[docSection, expectedTab] : expectedDocToTabMapping) {
      if (expectedTab != tab) {
        continue;
      }

      if (!mdSections.contains(docSection)) {
        inconsistencies.push_back(std::format("Tab '{}' maps to doc section '{}' but section not found", tab, docSection));
      }
      found = true;
      break;
    }

    if (!found) {
      inconsistencies.push_back(std::format("Tab '{}' has no corresponding documentation section", tab));
    }
  }

  // Helper function to check if a test fake option is found in missing files
  static void checkTestDummyDetection(const std::vector<std::string> &missingFromFiles, const std::string &testDummyOption, bool &foundMissingDummyInConfigView, bool &foundMissingDummyInMd, bool &foundMissingDummyInJson) {
    for (const auto &missing : missingFromFiles) {
      if (!missing.contains(testDummyOption)) {
        continue;
      }

      if (missing.contains("ConfigView.vue")) {
        foundMissingDummyInConfigView = true;
      }
      if (missing.contains("configuration.md")) {
        foundMissingDummyInMd = true;
      }
      if (missing.contains("en.json")) {
        foundMissingDummyInJson = true;
      }
    }
  }

  // Helper function to create comma-separated string from vector
  static std::string buildCommaSeparatedString(const std::vector<std::string> &options) {
    std::string result;
    for (size_t i = 0; i < options.size(); ++i) {
      if (i > 0) {
        result += ", ";
      }
      result += options[i];
    }
    return result;
  }
};

TEST_F(ConfigConsistencyTest, AllConfigOptionsExistInAllFiles) {
  const auto cppOptions = extractConfigCppOptions();
  const auto configViewOptions = extractConfigViewOptions();
  const auto mdOptions = extractConfigMdOptions();
  const auto jsonOptions = extractEnJsonConfigOptions();

  // Options that are internal/special and shouldn't be in UI/docs
  const std::set<std::string, std::less<>> internalOptions = {
    "flags"  // Internal config flags, not user-configurable
  };

  std::vector<std::string> missingFromFiles;

  // Check that all config.cpp options exist in other files (except internal ones)
  for (const auto &option : cppOptions) {
    if (internalOptions.contains(option)) {
      continue;  // Skip internal options
    }

    validateOptionExistence(option, configViewOptions, mdOptions, jsonOptions, missingFromFiles);
  }

  if (!missingFromFiles.empty()) {
    std::string errorMsg = "Config options missing from files:\n";
    for (const auto &missing : missingFromFiles) {
      errorMsg += std::format("  {}\n", missing);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(ConfigConsistencyTest, ConfigTabsMatchDocumentationSections) {
  auto configViewOptions = extractConfigViewOptions();
  auto mdOptions = extractConfigMdOptions();

  // Get unique tabs and sections
  std::set<std::string, std::less<>> configViewTabs;
  std::set<std::string, std::less<>> mdSections;

  for (const auto &tab : configViewOptions | std::views::values) {
    configViewTabs.insert(tab);
  }

  for (const auto &section : mdOptions | std::views::values) {
    mdSections.insert(section);
  }

  std::vector<std::string> inconsistencies;

  // Check that each ConfigView.vue tab has a corresponding documentation section
  for (const auto &tab : configViewTabs) {
    checkTabCorrespondence(tab, expectedDocToTabMapping, mdSections, inconsistencies);
  }

  // Check that each documentation section has a corresponding ConfigView.vue tab
  for (const auto &section : mdSections) {
    if (!expectedDocToTabMapping.contains(section)) {
      inconsistencies.push_back(std::format("Documentation section '{}' has no corresponding UI tab", section));
    }
  }

  if (!inconsistencies.empty()) {
    std::string errorMsg = "Tab/Section mapping inconsistencies:\n";
    for (const auto &inconsistency : inconsistencies) {
      errorMsg += std::format("  {}\n", inconsistency);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(ConfigConsistencyTest, ConfigOptionsInSameOrderWithinSections) {
  // Extract options with order preserved
  auto configViewOptionsByTab = extractConfigViewOptionsWithOrder();
  auto mdOptionsBySection = extractConfigMdOptionsWithOrder();

  std::vector<std::string> orderInconsistencies;

  // Compare order for each tab/section pair
  for (const auto &[docSection, tabId] : expectedDocToTabMapping) {
    if (!configViewOptionsByTab.contains(tabId) || !mdOptionsBySection.contains(docSection)) {
      continue;  // Skip if either tab or section doesn't exist
    }

    const auto &configViewOrder = configViewOptionsByTab.at(tabId);
    const auto &mdOrder = mdOptionsBySection.at(docSection);

    // Find options that exist in both ConfigView.vue and MD for this section
    std::vector<std::string> commonOptions;
    for (const auto &option : configViewOrder) {
      if (std::ranges::find(mdOrder, option) != mdOrder.end()) {
        commonOptions.push_back(option);
      }
    }

    // Filter MD order to only include common options in the same order they appear in MD
    std::vector<std::string> mdOrderFiltered;
    for (const auto &option : mdOrder) {
      if (std::ranges::find(commonOptions, option) != commonOptions.end()) {
        mdOrderFiltered.push_back(option);
      }
    }

    // Compare the order of common options
    if (commonOptions != mdOrderFiltered && !commonOptions.empty() && !mdOrderFiltered.empty()) {
      // Create readable string representations of the option lists
      std::string configViewOrderStr = buildCommaSeparatedString(commonOptions);
      std::string mdOrderStr = buildCommaSeparatedString(mdOrderFiltered);

      std::string detailMsg = std::format(
        "Section '{}' (tab '{}') has different option order:\n"
        "  ConfigView.vue order: [{}]\n"
        "  MD order:   [{}]",
        docSection,
        tabId,
        configViewOrderStr,
        mdOrderStr
      );
      orderInconsistencies.push_back(detailMsg);
    }
  }

  if (!orderInconsistencies.empty()) {
    std::string errorMsg = "Config option order inconsistencies:\n";
    for (const auto &inconsistency : orderInconsistencies) {
      errorMsg += std::format("  {}\n", inconsistency);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(ConfigConsistencyTest, DummyConfigOptionsDoNotExist) {
  const auto cppOptions = extractConfigCppOptions();
  const auto configViewOptions = extractConfigViewOptions();
  const auto mdOptions = extractConfigMdOptions();
  const auto jsonOptions = extractEnJsonConfigOptions();

  // List of fake config options that should NOT exist in any files
  const std::vector<std::string> dummyOptions = {
    "dummy_config_option",
    "nonexistent_setting",
    "fake_config_parameter",
    "test_dummy_option",
    "invalid_config_key"
  };

  std::vector<std::string> unexpectedlyFound;

  // Check that none of the fake options exist in any of the config files
  for (const auto &dummyOption : dummyOptions) {
    if (cppOptions.contains(dummyOption)) {
      unexpectedlyFound.push_back(std::format("config.cpp contains dummy option: {}", dummyOption));
    }

    if (configViewOptions.contains(dummyOption)) {
      unexpectedlyFound.push_back(std::format("ConfigView.vue contains dummy option: {}", dummyOption));
    }

    if (mdOptions.contains(dummyOption)) {
      unexpectedlyFound.push_back(std::format("configuration.md contains dummy option: {}", dummyOption));
    }

    if (jsonOptions.contains(dummyOption)) {
      unexpectedlyFound.push_back(std::format("en.json contains dummy option: {}", dummyOption));
    }
  }

  // This test should pass (i.e., no fake options should be found)
  // If any fake options are found, it indicates a problem with the test data
  if (!unexpectedlyFound.empty()) {
    std::string errorMsg = "Dummy config options unexpectedly found in files:\n";
    for (const auto &found : unexpectedlyFound) {
      errorMsg += std::format("  {}\n", found);
    }
    FAIL() << errorMsg;
  }
}

TEST_F(ConfigConsistencyTest, TestFrameworkDetectsMissingOptions) {
  const auto cppOptions = extractConfigCppOptions();
  const auto configViewOptions = extractConfigViewOptions();
  const auto mdOptions = extractConfigMdOptions();
  const auto jsonOptions = extractEnJsonConfigOptions();

  // Add a fake option to the cpp options to simulate a missing option scenario
  std::set<std::string, std::less<>> modifiedCppOptions = cppOptions;
  const std::string testDummyOption = "test_framework_validation_option";
  modifiedCppOptions.insert(testDummyOption);

  // Options that are internal/special and shouldn't be in UI/docs
  std::set<std::string, std::less<>> internalOptions = {
    "flags"  // Internal config flags, not user-configurable
  };

  std::vector<std::string> missingFromFiles;

  // Check that the fake option is detected as missing from other files
  for (const auto &option : modifiedCppOptions) {
    if (internalOptions.contains(option)) {
      continue;  // Skip internal options
    }

    if (!configViewOptions.contains(option)) {
      missingFromFiles.push_back(std::format("ConfigView.vue missing: {}", option));
    }

    if (!mdOptions.contains(option)) {
      missingFromFiles.push_back(std::format("configuration.md missing: {}", option));
    }

    if (!jsonOptions.contains(option)) {
      missingFromFiles.push_back(std::format("en.json missing: {}", option));
    }
  }

  // Verify that the test framework detected the missing fake option
  bool foundMissingDummyInConfigView = false;
  bool foundMissingDummyInMd = false;
  bool foundMissingDummyInJson = false;

  checkTestDummyDetection(missingFromFiles, testDummyOption, foundMissingDummyInConfigView, foundMissingDummyInMd, foundMissingDummyInJson);

  // The test framework should have detected the fake option as missing from all files
  EXPECT_TRUE(foundMissingDummyInConfigView) << "Test framework failed to detect missing option in ConfigView.vue";
  EXPECT_TRUE(foundMissingDummyInMd) << "Test framework failed to detect missing option in configuration.md";
  EXPECT_TRUE(foundMissingDummyInJson) << "Test framework failed to detect missing option in en.json";

  // Verify we have at least 3 missing entries (one for each file type)
  EXPECT_GE(missingFromFiles.size(), 3) << "Test framework should detect missing dummy option in all three file types";
}
