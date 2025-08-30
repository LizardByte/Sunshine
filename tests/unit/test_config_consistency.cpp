/**
 * @file tests/unit/test_config_consistency.cpp
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
#include <vector>

// local includes
#include "src/file_handler.h"

class ConfigConsistencyTest: public ::testing::Test {
protected:
  void SetUp() override {
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
      {"Software Encoder", "sw"}
    };
  }

  // Extract config options from config.cpp - the authoritative source
  static std::set<std::string, std::less<>> extractConfigCppOptions() {
    std::set<std::string, std::less<>> options;
    std::string content = file_handler::read_file("src/config.cpp");

    // Regex patterns to match different config option types in config.cpp
    std::vector<std::regex> patterns = {
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
      std::sregex_iterator end;

      for (; iter != end; ++iter) {
        std::string optionName = (*iter)[1].str();
        options.insert(optionName);
      }
    }

    return options;
  }

  // Helper function to find brace boundaries
  static size_t findClosingBrace(const std::string &content, size_t start) {
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

  // Helper function to extract tab ID from tab object
  static std::string extractTabId(const std::string &tabObject) {
    std::regex idPattern(R"DELIM(id:\s*"([^"]+)")DELIM");
    std::smatch idMatch;

    if (std::regex_search(tabObject, idMatch, idPattern)) {
      return idMatch[1].str();
    }

    return "";
  }

  // Helper function to process options section
  static void processOptionsSection(const std::string &tabObject, const std::string &tabId, std::map<std::string, std::string, std::less<>> &options) {
    size_t optionsStart = tabObject.find("options:");
    if (optionsStart == std::string::npos || tabId.empty()) {
      return;
    }

    size_t optStart = tabObject.find('{', optionsStart);
    if (optStart == std::string::npos) {
      return;
    }

    size_t optEnd = findClosingBrace(tabObject, optStart);
    std::string optionsSection = tabObject.substr(optStart + 1, optEnd - optStart - 1);

    // Extract option names
    std::regex optionPattern(R"DELIM("([^"]+)":\s*)DELIM");
    std::sregex_iterator optionIter(optionsSection.begin(), optionsSection.end(), optionPattern);
    std::sregex_iterator optionEnd;

    for (; optionIter != optionEnd; ++optionIter) {
      std::string optionName = (*optionIter)[1].str();
      options[optionName] = tabId;
    }
  }

  // Extract config options from config.html
  static std::map<std::string, std::string, std::less<>> extractConfigHtmlOptions() {
    std::map<std::string, std::string, std::less<>> options;
    std::string content = file_handler::read_file("src_assets/common/assets/web/config.html");

    // Find the tabs array in the JavaScript
    size_t tabsStart = content.find("tabs: [");
    if (tabsStart == std::string::npos) {
      return options;
    }

    // Find the end of the tabs array
    size_t pos = tabsStart + 7;  // Skip "tabs: ["
    int bracketLevel = 1;
    size_t tabsEnd = pos;

    while (pos < content.length() && bracketLevel > 0) {
      if (content[pos] == '[') {
        bracketLevel++;
      } else if (content[pos] == ']') {
        bracketLevel--;
      }
      tabsEnd = pos;
      pos++;
    }

    std::string tabsContent = content.substr(tabsStart + 7, tabsEnd - tabsStart - 7);

    // Process each tab object
    size_t tabPos = 0;
    while (tabPos < tabsContent.length()) {
      size_t objStart = tabsContent.find('{', tabPos);
      if (objStart == std::string::npos) {
        break;
      }

      size_t objEnd = findClosingBrace(tabsContent, objStart);
      std::string tabObject = tabsContent.substr(objStart, objEnd - objStart + 1);

      std::string tabId = extractTabId(tabObject);
      processOptionsSection(tabObject, tabId, options);

      tabPos = objEnd + 1;
    }

    return options;
  }

  // Helper function to process markdown line for section headers
  static bool processSectionHeader(const std::string &line, std::string &currentSection) {
    std::regex sectionPattern(R"(^## ([^#\r\n]+))");
    std::smatch sectionMatch;

    if (std::regex_search(line, sectionMatch, sectionPattern)) {
      currentSection = sectionMatch[1].str();
      // Trim whitespace
      currentSection.erase(currentSection.find_last_not_of(" \t\r\n") + 1);
      return true;
    }

    return false;
  }

  // Helper function to process markdown line for option headers
  static bool processOptionHeader(const std::string &line, const std::string &currentSection, std::map<std::string, std::string, std::less<>> &options) {
    if (currentSection.empty()) {
      return false;
    }

    std::regex optionPattern(R"(^### ([^#\r\n]+))");
    std::smatch optionMatch;

    if (std::regex_search(line, optionMatch, optionPattern)) {
      std::string optionName = optionMatch[1].str();
      // Trim whitespace
      optionName.erase(optionName.find_last_not_of(" \t\r\n") + 1);
      options[optionName] = currentSection;
      return true;
    }

    return false;
  }

  // Extract config options from configuration.md
  static std::map<std::string, std::string, std::less<>> extractConfigMdOptions() {
    std::map<std::string, std::string, std::less<>> options;
    std::string content = file_handler::read_file("docs/configuration.md");

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

  // Helper function to find config section end
  static size_t findConfigSectionEnd(const std::string &content, size_t configStart) {
    size_t braceCount = 1;
    size_t configEnd = configStart;

    for (size_t i = configStart; i < content.length() && braceCount > 0; ++i) {
      if (content[i] == '{') {
        braceCount++;
      } else if (content[i] == '}') {
        braceCount--;
      }
      configEnd = i;
    }

    return configEnd;
  }

  // Helper function to extract keys from config section
  static void extractKeysFromConfigSection(const std::string &configSection, std::set<std::string, std::less<>> &options) {
    std::regex keyPattern(R"DELIM("([^"]+)":\s*)DELIM");
    std::sregex_iterator iter(configSection.begin(), configSection.end(), keyPattern);
    std::sregex_iterator end;

    for (; iter != end; ++iter) {
      options.insert((*iter)[1].str());
    }
  }

  // Extract config options from en.json
  static std::set<std::string, std::less<>> extractEnJsonConfigOptions() {
    std::set<std::string, std::less<>> options;
    std::string content = file_handler::read_file("src_assets/common/assets/web/public/assets/locale/en.json");

    // Look for the config section
    std::regex configSectionPattern(R"DELIM("config":\s*\{)DELIM");
    std::smatch match;

    if (!std::regex_search(content, match, configSectionPattern)) {
      return options;
    }

    // Find the config section and extract keys
    size_t configStart = match.position() + match.length();
    size_t configEnd = findConfigSectionEnd(content, configStart);
    std::string configSection = content.substr(configStart, configEnd - configStart);

    extractKeysFromConfigSection(configSection, options);

    return options;
  }

  std::map<std::string, std::string, std::less<>> expectedDocToTabMapping;

  // Helper function to check if option exists in HTML options
  static bool isOptionInHtml(const std::string &option, const std::map<std::string, std::string, std::less<>> &htmlOptions) {
    return htmlOptions.contains(option);
  }

  // Helper function to check if option exists in MD options
  static bool isOptionInMd(const std::string &option, const std::map<std::string, std::string, std::less<>> &mdOptions) {
    return mdOptions.contains(option);
  }

  // Helper function to validate option existence across files
  static void validateOptionExistence(const std::string &option, const std::map<std::string, std::string, std::less<>> &htmlOptions, const std::map<std::string, std::string, std::less<>> &mdOptions, const std::set<std::string, std::less<>> &jsonOptions, std::vector<std::string> &missingFromFiles) {
    if (!isOptionInHtml(option, htmlOptions)) {
      missingFromFiles.push_back(std::format("config.html missing: {}", option));
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

  // Helper function to find common options between two lists
  static std::vector<std::string> findCommonOptions(const std::vector<std::string> &htmlOrder, const std::vector<std::string> &mdOrder) {
    std::vector<std::string> commonOptions;

    for (const auto &option : htmlOrder) {
      if (std::ranges::find(mdOrder, option) != mdOrder.end()) {
        commonOptions.push_back(option);
      }
    }

    return commonOptions;
  }

  // Helper function to filter options to match common ones
  static std::vector<std::string> filterToCommonOptions(const std::vector<std::string> &mdOrder, const std::vector<std::string> &commonOptions) {
    std::vector<std::string> mdOrderFiltered;

    for (const auto &option : mdOrder) {
      if (std::ranges::find(commonOptions, option) != commonOptions.end()) {
        mdOrderFiltered.push_back(option);
      }
    }

    return mdOrderFiltered;
  }

  // Helper function to compare option order for a section/tab pair
  static void compareOptionOrder(const std::string &docSection, const std::string &tabId, const std::map<std::string, std::vector<std::string>, std::less<>> &htmlOptionsByTab, const std::map<std::string, std::vector<std::string>, std::less<>> &mdOptionsBySection, std::vector<std::string> &orderInconsistencies) {
    if (!htmlOptionsByTab.contains(tabId) || !mdOptionsBySection.contains(docSection)) {
      return;
    }

    const auto &htmlOrder = htmlOptionsByTab.at(tabId);
    const auto &mdOrder = mdOptionsBySection.at(docSection);

    std::vector<std::string> commonOptions = findCommonOptions(htmlOrder, mdOrder);
    std::vector<std::string> mdOrderFiltered = filterToCommonOptions(mdOrder, commonOptions);

    if (commonOptions != mdOrderFiltered && !commonOptions.empty() && !mdOrderFiltered.empty()) {
      orderInconsistencies.push_back(std::format("Section '{}' (tab '{}') has different option order", docSection, tabId));
    }
  }
};

TEST_F(ConfigConsistencyTest, AllConfigOptionsExistInAllFiles) {
  auto cppOptions = extractConfigCppOptions();
  auto htmlOptions = extractConfigHtmlOptions();
  auto mdOptions = extractConfigMdOptions();
  auto jsonOptions = extractEnJsonConfigOptions();

  // Options that are internal/special and shouldn't be in UI/docs
  std::set<std::string, std::less<>> internalOptions = {
    "flags"  // Internal config flags, not user-configurable
  };

  std::vector<std::string> missingFromFiles;

  // Check that all config.cpp options exist in other files (except internal ones)
  for (const auto &option : cppOptions) {
    if (internalOptions.contains(option)) {
      continue;  // Skip internal options
    }

    validateOptionExistence(option, htmlOptions, mdOptions, jsonOptions, missingFromFiles);
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
  auto htmlOptions = extractConfigHtmlOptions();
  auto mdOptions = extractConfigMdOptions();

  // Get unique tabs and sections
  std::set<std::string, std::less<>> htmlTabs;
  std::set<std::string, std::less<>> mdSections;

  for (const auto &tab : htmlOptions | std::views::values) {
    htmlTabs.insert(tab);
  }

  for (const auto &section : mdOptions | std::views::values) {
    mdSections.insert(section);
  }

  std::vector<std::string> inconsistencies;

  // Check that each HTML tab has a corresponding documentation section
  for (const auto &tab : htmlTabs) {
    checkTabCorrespondence(tab, expectedDocToTabMapping, mdSections, inconsistencies);
  }

  // Check that each documentation section has a corresponding HTML tab
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
  auto htmlOptions = extractConfigHtmlOptions();
  auto mdOptions = extractConfigMdOptions();

  // Group options by tab/section
  std::map<std::string, std::vector<std::string>, std::less<>> htmlOptionsByTab;
  std::map<std::string, std::vector<std::string>, std::less<>> mdOptionsBySection;

  for (const auto &[option, tab] : htmlOptions) {
    htmlOptionsByTab[tab].push_back(option);
  }

  for (const auto &[option, section] : mdOptions) {
    mdOptionsBySection[section].push_back(option);
  }

  std::vector<std::string> orderInconsistencies;

  // Compare order for each tab/section pair
  for (const auto &[docSection, tabId] : expectedDocToTabMapping) {
    compareOptionOrder(docSection, tabId, htmlOptionsByTab, mdOptionsBySection, orderInconsistencies);
  }

  if (!orderInconsistencies.empty()) {
    std::string errorMsg = "Config option order inconsistencies:\n";
    for (const auto &inconsistency : orderInconsistencies) {
      errorMsg += std::format("  {}\n", inconsistency);
    }
    FAIL() << errorMsg;
  }
}
