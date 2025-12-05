#include "../games/game_detector.h"
#include <string>
#include <nlohmann/json.hpp>

/**
 * @file games.cpp
 * @brief Implementación de la API HTTP para el Game Detector.
 * 
 * Este archivo NO debe ser compilado directamente, sino incluido en nvhttp.cpp
 * para registrar las rutas en el servidor principal de Sunshine.
 */

namespace sunshine {
namespace api {

/**
 * @brief Maneja la petición GET /api/games/detected
 * @return Un JSON con todos los juegos detectados.
 */
std::string get_detected_games() {
    auto& detector = sunshine::games::GameDetector::instance();
    auto games = detector.detect_all_games();
    return detector.to_json(games);
}

/**
 * @brief Maneja la petición GET /api/games/detected/{platform}
 * @param platform La plataforma a escanear (e.g., "steam").
 * @return Un JSON con los juegos de esa plataforma.
 */
std::string get_platform_games(const std::string& platform) {
    auto& detector = sunshine::games::GameDetector::instance();
    auto games = detector.detect_platform(platform);
    return detector.to_json(games);
}

/**
 * @brief Maneja la petición GET /api/games/platforms
 * @return Un JSON con la lista de plataformas instaladas.
 */
std::string get_available_platforms() {
    auto& detector = sunshine::games::GameDetector::instance();
    auto platforms = detector.get_available_platforms();
    nlohmann::json json_platforms = platforms;
    return json_platforms.dump(2);
}

} // namespace api
} // namespace sunshine
