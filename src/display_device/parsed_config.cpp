// lib includes
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <cmath>

// local includes
#include "parsed_config.h"
#include "src/config.h"
#include "src/logging.h"
#include "src/rtsp.h"
#include "to_string.h"

namespace display_device {

  namespace {
    /**
     * @brief Parse resolution value from the string.
     * @param input String to be parsed.
     * @param output Reference to output variable.
     * @returns True on successful parsing (empty string allowed), false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * boost::optional<resolution_t> resolution;
     * if (parse_resolution_string("1920x1080", resolution)) {
     *   if (resolution) {
     *     // Value was specified
     *   }
     *   else {
     *     // Value was empty
     *   }
     * }
     * ```
     */
    bool
    parse_resolution_string(const std::string &input, boost::optional<resolution_t> &output) {
      const std::string trimmed_input { boost::algorithm::trim_copy(input) };
      const boost::regex resolution_regex { R"(^(\d+)x(\d+)$)" };  // std::regex hangs in CTOR for some reason when called in a thread. Problem with MSYS2 packages (UCRT64), maybe?

      boost::smatch match;
      if (boost::regex_match(trimmed_input, match, resolution_regex)) {
        try {
          output = resolution_t {
            static_cast<unsigned int>(std::stol(match[1])),
            static_cast<unsigned int>(std::stol(match[2]))
          };
        }
        catch (const std::invalid_argument &err) {
          BOOST_LOG(error) << "Failed to parse resolution string " << trimmed_input << " (invalid argument):\n"
                           << err.what();
          return false;
        }
        catch (const std::out_of_range &err) {
          BOOST_LOG(error) << "Failed to parse resolution string " << trimmed_input << " (number out of range):\n"
                           << err.what();
          return false;
        }
        catch (const std::exception &err) {
          BOOST_LOG(error) << "Failed to parse resolution string " << trimmed_input << ":\n"
                           << err.what();
          return false;
        }
      }
      else {
        output = boost::none;

        if (!trimmed_input.empty()) {
          BOOST_LOG(error) << "Failed to parse resolution string " << trimmed_input << ". It must match a \"1920x1080\" pattern!";
          return false;
        }
      }

      return true;
    }

    /**
     * @brief Parse refresh rate value from the string.
     * @param input String to be parsed.
     * @param output Reference to output variable.
     * @param allow_decimal_point Specify whether the decimal point is allowed in the string.
     * @returns True on successful parsing (empty string allowed), false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * boost::optional<refresh_rate_t> refresh_rate;
     * if (parse_refresh_rate_string("59.95", refresh_rate)) {
     *   if (refresh_rate) {
     *     // Value was specified
     *   }
     *   else {
     *     // Value was empty
     *   }
     * }
     * ```
     */
    bool
    parse_refresh_rate_string(const std::string &input, boost::optional<refresh_rate_t> &output, bool allow_decimal_point = true) {
      const std::string trimmed_input { boost::algorithm::trim_copy(input) };
      // std::regex hangs in CTOR for some reason when called in a thread. Problem with MSYS2 packages (UCRT64), maybe?
      const boost::regex refresh_rate_regex { allow_decimal_point ? R"(^(\d+)(?:\.(\d+))?$)" : R"(^(\d+)$)" };

      boost::smatch match;
      if (boost::regex_match(trimmed_input, match, refresh_rate_regex)) {
        try {
          if (allow_decimal_point && match[2].matched) {
            // We have a decimal point and will have to split it into numerator and denominator.
            // For example:
            //   59.995:
            //     numerator = 59995
            //     denominator = 1000

            // We are essentially removing the decimal point here: 59.995 -> 59995
            const std::string numerator_str { match[1].str() + match[2].str() };
            const auto numerator { static_cast<unsigned int>(std::stol(numerator_str)) };

            // Here we are counting decimal places and calculating denominator: 10^decimal_places
            const auto denominator { static_cast<unsigned int>(std::pow(10, std::distance(match[2].first, match[2].second))) };

            output = refresh_rate_t { numerator, denominator };
          }
          else {
            // We do not have a decimal point, just a valid number.
            // For example:
            //   60:
            //     numerator = 60
            //     denominator = 1
            output = refresh_rate_t { static_cast<unsigned int>(std::stol(match[1])), 1 };
          }
        }
        catch (const std::invalid_argument &err) {
          BOOST_LOG(error) << "Failed to parse refresh rate or FPS string " << trimmed_input << " (invalid argument):\n"
                           << err.what();
          return false;
        }
        catch (const std::out_of_range &err) {
          BOOST_LOG(error) << "Failed to parse refresh rate or FPS string " << trimmed_input << " (number out of range):\n"
                           << err.what();
          return false;
        }
        catch (const std::exception &err) {
          BOOST_LOG(error) << "Failed to parse refresh rate or FPS string " << trimmed_input << ":\n"
                           << err.what();
          return false;
        }
      }
      else {
        output = boost::none;

        if (!trimmed_input.empty()) {
          BOOST_LOG(error) << "Failed to parse refresh rate or FPS string " << trimmed_input << ". Must have a pattern of " << (allow_decimal_point ? "\"123\" or \"123.456\"" : "\"123\"") << "!";
          return false;
        }
      }

      return true;
    }

    /**
     * @brief Parse resolution option from the user configuration and the session information.
     * @param config User's video related configuration.
     * @param session Session information.
     * @param parsed_config A reference to a config object that will be modified on success.
     * @returns True on successful parsing, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     *
     * parsed_config_t parsed_config;
     * const bool success = parse_resolution_option(video_config, *launch_session, parsed_config);
     * ```
     */
    bool
    parse_resolution_option(const config::video_t &config, const rtsp_stream::launch_session_t &session, parsed_config_t &parsed_config) {
      const auto resolution_option { static_cast<parsed_config_t::resolution_change_e>(config.resolution_change) };
      switch (resolution_option) {
        case parsed_config_t::resolution_change_e::automatic: {
          if (!session.enable_sops) {
            BOOST_LOG(warning) << "Sunshine is configured to change resolution automatically, but the \"Optimize game settings\" is not set in the client! Resolution will not be changed.";
            parsed_config.resolution = boost::none;
          }
          else if (session.width >= 0 && session.height >= 0) {
            parsed_config.resolution = resolution_t {
              static_cast<unsigned int>(session.width),
              static_cast<unsigned int>(session.height)
            };
          }
          else {
            BOOST_LOG(error) << "Resolution provided by client session config is invalid: " << session.width << "x" << session.height;
            return false;
          }
          break;
        }
        case parsed_config_t::resolution_change_e::manual: {
          if (!session.enable_sops) {
            BOOST_LOG(warning) << "Sunshine is configured to change resolution manually, but the \"Optimize game settings\" is not set in the client! Resolution will not be changed.";
            parsed_config.resolution = boost::none;
          }
          else {
            if (!parse_resolution_string(config.manual_resolution, parsed_config.resolution)) {
              BOOST_LOG(error) << "Failed to parse manual resolution string!";
              return false;
            }

            if (!parsed_config.resolution) {
              BOOST_LOG(error) << "Manual resolution must be specified!";
              return false;
            }
          }
          break;
        }
        case parsed_config_t::resolution_change_e::no_operation:
        default:
          break;
      }

      return true;
    }

    /**
     * @brief Parse refresh rate option from the user configuration and the session information.
     * @param config User's video related configuration.
     * @param session Session information.
     * @param parsed_config A reference to a config object that will be modified on success.
     * @returns True on successful parsing, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     *
     * parsed_config_t parsed_config;
     * const bool success = parse_refresh_rate_option(video_config, *launch_session, parsed_config);
     * ```
     */
    bool
    parse_refresh_rate_option(const config::video_t &config, const rtsp_stream::launch_session_t &session, parsed_config_t &parsed_config) {
      const auto refresh_rate_option { static_cast<parsed_config_t::refresh_rate_change_e>(config.refresh_rate_change) };
      switch (refresh_rate_option) {
        case parsed_config_t::refresh_rate_change_e::automatic: {
          if (session.fps >= 0) {
            parsed_config.refresh_rate = refresh_rate_t { static_cast<unsigned int>(session.fps), 1 };
          }
          else {
            BOOST_LOG(error) << "FPS value provided by client session config is invalid: " << session.fps;
            return false;
          }
          break;
        }
        case parsed_config_t::refresh_rate_change_e::manual: {
          if (!parse_refresh_rate_string(config.manual_refresh_rate, parsed_config.refresh_rate)) {
            BOOST_LOG(error) << "Failed to parse manual refresh rate string!";
            return false;
          }

          if (!parsed_config.refresh_rate) {
            BOOST_LOG(error) << "Manual refresh rate must be specified!";
            return false;
          }
          break;
        }
        case parsed_config_t::refresh_rate_change_e::no_operation:
        default:
          break;
      }

      return true;
    }

    /**
     * @brief Remap the already parsed display mode based on the user configuration.
     * @param config User's video related configuration.
     * @param parsed_config A reference to a config object that will be modified on success.
     * @returns True is display mode was remapped or no remapping was needed, false otherwise.
     *
     * EXAMPLES:
     * ```cpp
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     *
     * parsed_config_t parsed_config;
     * const bool success = remap_display_modes_if_needed(video_config, *launch_session, parsed_config);
     * ```
     */
    bool
    remap_display_modes_if_needed(const config::video_t &config, const rtsp_stream::launch_session_t &session, parsed_config_t &parsed_config) {
      constexpr auto mixed_remapping { "" };
      constexpr auto resolution_only_remapping { "resolution_only" };
      constexpr auto refresh_rate_only_remapping { "refresh_rate_only" };

      const auto resolution_option { static_cast<parsed_config_t::resolution_change_e>(config.resolution_change) };
      const auto refresh_rate_option { static_cast<parsed_config_t::refresh_rate_change_e>(config.refresh_rate_change) };

      // Copy only the remapping values that we can actually use with our configuration options
      std::vector<config::video_t::display_mode_remapping_t> remapping_values;
      std::copy_if(std::begin(config.display_mode_remapping), std::end(config.display_mode_remapping), std::back_inserter(remapping_values), [&](const auto &value) {
        if (resolution_option == parsed_config_t::resolution_change_e::automatic && refresh_rate_option == parsed_config_t::refresh_rate_change_e::automatic) {
          return value.type == mixed_remapping;  // Comparison instead of empty check to be explicit
        }
        else if (resolution_option == parsed_config_t::resolution_change_e::automatic) {
          return value.type == resolution_only_remapping;
        }
        else if (refresh_rate_option == parsed_config_t::refresh_rate_change_e::automatic) {
          return value.type == refresh_rate_only_remapping;
        }

        return false;
      });

      if (remapping_values.empty()) {
        BOOST_LOG(debug) << "No values are available for display mode remapping.";
        return true;
      }
      BOOST_LOG(debug) << "Trying to remap display modes...";

      struct parsed_remapping_values_t {
        boost::optional<resolution_t> received_resolution;
        boost::optional<refresh_rate_t> received_fps;
        boost::optional<resolution_t> final_resolution;
        boost::optional<refresh_rate_t> final_refresh_rate;
      };

      std::vector<parsed_remapping_values_t> parsed_values;
      for (const auto &entry : remapping_values) {
        boost::optional<resolution_t> received_resolution;
        boost::optional<refresh_rate_t> received_fps;
        boost::optional<resolution_t> final_resolution;
        boost::optional<refresh_rate_t> final_refresh_rate;

        if (entry.type == resolution_only_remapping) {
          if (!parse_resolution_string(entry.received_resolution, received_resolution) ||
              !parse_resolution_string(entry.final_resolution, final_resolution)) {
            BOOST_LOG(error) << "Failed to parse entry value: " << entry.received_resolution << " -> " << entry.final_resolution;
            return false;
          }

          if (!received_resolution || !final_resolution) {
            BOOST_LOG(error) << "Both values must be set for remapping resolution! Current entry value: " << entry.received_resolution << " -> " << entry.final_resolution;
            return false;
          }

          if (!session.enable_sops) {
            BOOST_LOG(warning) << "Skipping remapping resolution, because the \"Optimize game settings\" is not set in the client!";
            return true;
          }
        }
        else if (entry.type == refresh_rate_only_remapping) {
          if (!parse_refresh_rate_string(entry.received_fps, received_fps, false) ||
              !parse_refresh_rate_string(entry.final_refresh_rate, final_refresh_rate)) {
            BOOST_LOG(error) << "Failed to parse entry value: " << entry.received_fps << " -> " << entry.final_refresh_rate;
            return false;
          }

          if (!received_fps || !final_refresh_rate) {
            BOOST_LOG(error) << "Both values must be set for remapping refresh rate! Current entry value: " << entry.received_fps << " -> " << entry.final_refresh_rate;
            return false;
          }
        }
        else {
          if (!parse_resolution_string(entry.received_resolution, received_resolution) ||
              !parse_refresh_rate_string(entry.received_fps, received_fps, false) ||
              !parse_resolution_string(entry.final_resolution, final_resolution) ||
              !parse_refresh_rate_string(entry.final_refresh_rate, final_refresh_rate)) {
            BOOST_LOG(error) << "Failed to parse entry value: "
                             << "[" << entry.received_resolution << "|" << entry.received_fps << "] -> [" << entry.final_resolution << "|" << entry.final_refresh_rate << "]";
            return false;
          }

          if ((!received_resolution && !received_fps) || (!final_resolution && !final_refresh_rate)) {
            BOOST_LOG(error) << "At least one received and final value must be set for remapping display modes! Entry: "
                             << "[" << entry.received_resolution << "|" << entry.received_fps << "] -> [" << entry.final_resolution << "|" << entry.final_refresh_rate << "]";
            return false;
          }

          if (!session.enable_sops && (received_resolution || final_resolution)) {
            BOOST_LOG(warning) << "Skipping remapping entry, because the \"Optimize game settings\" is not set in the client! Entry: "
                               << "[" << entry.received_resolution << "|" << entry.received_fps << "] -> [" << entry.final_resolution << "|" << entry.final_refresh_rate << "]";
            continue;
          }
        }

        parsed_values.push_back({ received_resolution, received_fps, final_resolution, final_refresh_rate });
      }

      const auto compare_resolution { [](const resolution_t &a, const resolution_t &b) {
        return a.width == b.width && a.height == b.height;
      } };
      const auto compare_refresh_rate { [](const refresh_rate_t &a, const refresh_rate_t &b) {
        return a.numerator == b.numerator && a.denominator == b.denominator;
      } };

      for (const auto &entry : parsed_values) {
        bool do_remap { false };
        if (entry.received_resolution && entry.received_fps) {
          if (parsed_config.resolution && parsed_config.refresh_rate) {
            do_remap = compare_resolution(*entry.received_resolution, *parsed_config.resolution) && compare_refresh_rate(*entry.received_fps, *parsed_config.refresh_rate);
          }
          else {
            // Sanity check
            BOOST_LOG(error) << "Cannot remap: (parsed_config.resolution && parsed_config.refresh_rate) == false!";
            return false;
          }
        }
        else if (entry.received_resolution) {
          if (parsed_config.resolution) {
            do_remap = compare_resolution(*entry.received_resolution, *parsed_config.resolution);
          }
          else {
            // Sanity check
            BOOST_LOG(error) << "Cannot remap: parsed_config.resolution == false!";
            return false;
          }
        }
        else if (entry.received_fps) {
          if (parsed_config.refresh_rate) {
            do_remap = compare_refresh_rate(*entry.received_fps, *parsed_config.refresh_rate);
          }
          else {
            // Sanity check
            BOOST_LOG(error) << "Cannot remap: parsed_config.refresh_rate == false!";
            return false;
          }
        }
        else {
          // Sanity check
          BOOST_LOG(error) << "Cannot remap: (entry.received_resolution || entry.received_fps) == false!";
          return false;
        }

        if (do_remap) {
          if (!entry.final_resolution && !entry.final_refresh_rate) {
            // Sanity check
            BOOST_LOG(error) << "Cannot remap: (!entry.final_resolution && !entry.final_refresh_rate) == true!";
            return false;
          }

          if (entry.final_resolution) {
            BOOST_LOG(debug) << "Remapping resolution to: " << to_string(*entry.final_resolution);
            parsed_config.resolution = entry.final_resolution;
          }
          if (entry.final_refresh_rate) {
            BOOST_LOG(debug) << "Remapping refresh rate to: " << to_string(*entry.final_refresh_rate);
            parsed_config.refresh_rate = entry.final_refresh_rate;
          }

          break;
        }
      }

      return true;
    }

    /**
     * @brief Parse HDR option from the user configuration and the session information.
     * @param config User's video related configuration.
     * @param session Session information.
     * @returns Parsed HDR state value we need to switch to (true == ON, false == OFF).
     *          Empty optional if no action is required.
     *
     * EXAMPLES:
     * ```cpp
     * const std::shared_ptr<rtsp_stream::launch_session_t> launch_session; // Assuming ptr is properly initialized
     * const config::video_t &video_config { config::video };
     * const auto hdr_option = parse_hdr_option(video_config, *launch_session);
     * ```
     */
    boost::optional<bool>
    parse_hdr_option(const config::video_t &config, const rtsp_stream::launch_session_t &session) {
      const auto hdr_prep_option { static_cast<parsed_config_t::hdr_prep_e>(config.hdr_prep) };
      switch (hdr_prep_option) {
        case parsed_config_t::hdr_prep_e::automatic:
          return session.enable_hdr;
        case parsed_config_t::hdr_prep_e::no_operation:
        default:
          return boost::none;
      }
    }
  }  // namespace

  int
  parsed_config_t::device_prep_from_view(std::string_view value) {
    using namespace std::string_view_literals;
#define _CONVERT_(x) \
  if (value == #x##sv) return static_cast<int>(parsed_config_t::device_prep_e::x);
    _CONVERT_(no_operation);
    _CONVERT_(ensure_active);
    _CONVERT_(ensure_primary);
    _CONVERT_(ensure_only_display);
#undef _CONVERT_
    return static_cast<int>(parsed_config_t::device_prep_e::no_operation);
  }

  int
  parsed_config_t::resolution_change_from_view(std::string_view value) {
    using namespace std::string_view_literals;
#define _CONVERT_(x) \
  if (value == #x##sv) return static_cast<int>(parsed_config_t::resolution_change_e::x);
    _CONVERT_(no_operation);
    _CONVERT_(automatic);
    _CONVERT_(manual);
#undef _CONVERT_
    return static_cast<int>(parsed_config_t::resolution_change_e::no_operation);
  }

  int
  parsed_config_t::refresh_rate_change_from_view(std::string_view value) {
    using namespace std::string_view_literals;
#define _CONVERT_(x) \
  if (value == #x##sv) return static_cast<int>(parsed_config_t::refresh_rate_change_e::x);
    _CONVERT_(no_operation);
    _CONVERT_(automatic);
    _CONVERT_(manual);
#undef _CONVERT_
    return static_cast<int>(parsed_config_t::refresh_rate_change_e::no_operation);
  }

  int
  parsed_config_t::hdr_prep_from_view(std::string_view value) {
    using namespace std::string_view_literals;
#define _CONVERT_(x) \
  if (value == #x##sv) return static_cast<int>(parsed_config_t::hdr_prep_e::x);
    _CONVERT_(no_operation);
    _CONVERT_(automatic);
#undef _CONVERT_
    return static_cast<int>(parsed_config_t::hdr_prep_e::no_operation);
  }

  boost::optional<parsed_config_t>
  make_parsed_config(const config::video_t &config, const rtsp_stream::launch_session_t &session) {
    parsed_config_t parsed_config;
    parsed_config.device_id = config.output_name;
    parsed_config.device_prep = static_cast<parsed_config_t::device_prep_e>(config.display_device_prep);
    parsed_config.change_hdr_state = parse_hdr_option(config, session);

    if (!parse_resolution_option(config, session, parsed_config)) {
      // Error already logged
      return boost::none;
    }

    if (!parse_refresh_rate_option(config, session, parsed_config)) {
      // Error already logged
      return boost::none;
    }

    if (!remap_display_modes_if_needed(config, session, parsed_config)) {
      // Error already logged
      return boost::none;
    }

    BOOST_LOG(debug) << "Parsed display device config:\n"
                     << "device_id: " << parsed_config.device_id << "\n"
                     << "device_prep: " << static_cast<int>(parsed_config.device_prep) << "\n"
                     << "change_hdr_state: " << (parsed_config.change_hdr_state ? *parsed_config.change_hdr_state ? "true" : "false" : "none") << "\n"
                     << "resolution: " << (parsed_config.resolution ? to_string(*parsed_config.resolution) : "none") << "\n"
                     << "refresh_rate: " << (parsed_config.refresh_rate ? to_string(*parsed_config.refresh_rate) : "none") << "\n";

    return parsed_config;
  }

}  // namespace display_device
