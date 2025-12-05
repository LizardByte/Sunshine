#include "../games/game_detector.h"
#include <string>
#include <sstream>
#include <nlohmann/json.hpp>

/**
 * @file games.cpp
 * @brief Implementación de la API HTTP para el Game Detector.
 * 
 * Este archivo implementa las funciones que conectan la lógica de detección de juegos
 * con el servidor HTTP de Sunshine.
 */

namespace sunshine {
namespace api {

/**
 * @brief Maneja la petición GET /api/games/detected
 * @return Un JSON con todos los juegos detectados.
 */
std::string get_detected_games() {
    try {
        auto& detector = sunshine::games::GameDetector::instance();
        auto games = detector.detect_all_games();
        return detector.to_json(games);
    } catch (const std::exception& e) {
        nlohmann::json error;
        error["error"] = "Failed to detect games";
        error["message"] = e.what();
        return error.dump();
    }
}

/**
 * @brief Maneja la petición GET /api/games/detected/{platform}
 * @param platform La plataforma a escanear (e.g., "steam").
 * @return Un JSON con los juegos de esa plataforma.
 */
std::string get_platform_games(const std::string& platform) {
    try {
        auto& detector = sunshine::games::GameDetector::instance();
        auto games = detector.detect_platform(platform);
        return detector.to_json(games);
    } catch (const std::exception& e) {
        nlohmann::json error;
        error["error"] = "Failed to detect platform games";
        error["message"] = e.what();
        return error.dump();
    }
}

/**
 * @brief Maneja la petición GET /api/games/platforms
 * @return Un JSON con la lista de plataformas instaladas.
 */
std::string get_available_platforms() {
    try {
        auto& detector = sunshine::games::GameDetector::instance();
        auto platforms = detector.get_available_platforms();
        nlohmann::json json_platforms = platforms;
        return json_platforms.dump(2);
    } catch (const std::exception& e) {
        nlohmann::json error;
        error["error"] = "Failed to get platforms";
        error["message"] = e.what();
        return error.dump();
    }
}

} // namespace api
} // namespace sunshine
