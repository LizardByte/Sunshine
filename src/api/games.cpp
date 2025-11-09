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

namespace sunshine {
namespace api {

/**
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