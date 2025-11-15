<<<<<<< Updated upstream
/**
 * @file games.cpp
 * @brief API REST para detección de juegos
 * 
 * Este archivo debe integrarse con nvhttp.cpp para exponer las rutas:
 * - GET /api/games/detected
 * - GET /api/games/detected/:platform
 * - GET /api/games/platforms
 * - POST /api/games/refresh
 */

#include "../games/game_detector.h"
#include <string>
#include <sstream>
=======
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
>>>>>>> Stashed changes

namespace sunshine {
namespace api {

/**
<<<<<<< Updated upstream
 * GET /api/games/detected
 * Retorna todos los juegos detectados
 */
std::string get_detected_games() {
    try {
        auto& detector = games::GameDetector::instance();
        auto games = detector.detect_all_games();
        return detector.to_json(games);
    } catch (const std::exception& e) {
        std::ostringstream error;
        error << "{\"error\":\"Failed to detect games\",\"message\":\"" 
              << e.what() << "\"}";
        return error.str();
    }
}

/**
 * GET /api/games/detected/:platform
 * Retorna juegos de una plataforma específica
 */
std::string get_platform_games(const std::string& platform) {
    try {
        auto& detector = games::GameDetector::instance();
        auto games = detector.detect_platform(platform);
        return detector.to_json(games);
    } catch (const std::exception& e) {
        std::ostringstream error;
        error << "{\"error\":\"Failed to detect platform games\",\"message\":\"" 
              << e.what() << "\"}";
        return error.str();
    }
}

/**
 * GET /api/games/platforms
 * Retorna las plataformas disponibles
 */
std::string get_available_platforms() {
    try {
        auto& detector = games::GameDetector::instance();
        auto platforms = detector.get_available_platforms();
        
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < platforms.size(); ++i) {
            json << "\"" << platforms[i] << "\"";
            if (i < platforms.size() - 1) {
                json << ",";
            }
        }
        json << "]";
        
        return json.str();
    } catch (const std::exception& e) {
        std::ostringstream error;
        error << "{\"error\":\"Failed to get platforms\",\"message\":\"" 
              << e.what() << "\"}";
        return error.str();
    }
}

/**
 * POST /api/games/refresh
 * Refresca la lista de juegos (re-escanea)
 */
std::string refresh_games() {
    // Por ahora simplemente detecta de nuevo
    return get_detected_games();
}

} // namespace api
} // namespace sunshine
=======
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
>>>>>>> Stashed changes
