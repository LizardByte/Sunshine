<<<<<<< Updated upstream
/**
 * @file game_detector.h
 * @brief Módulo de autodetección de juegos instalados
 * 
 * Detecta juegos de múltiples plataformas:
 * - Steam
 * - Epic Games Store
 * - Xbox Game Pass / Microsoft Store
 * - EA Desktop / Origin
 * - Ubisoft Connect
 * - GOG Galaxy
 */

#ifndef SUNSHINE_GAME_DETECTOR_H
#define SUNSHINE_GAME_DETECTOR_H

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
=======
#include "game_detector.h"
#include "../logging.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <regex>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif
>>>>>>> Stashed changes

namespace sunshine {
namespace games {

<<<<<<< Updated upstream
/**
 * Estructura que representa un juego detectado
 */
struct DetectedGame {
    std::string id;              // ID único (ej: "steam_271590")
    std::string name;            // Nombre del juego
    std::string platform;        // Plataforma (steam, epic, xbox, etc.)
    std::string executable;      // Ruta completa al ejecutable
    std::string launch_cmd;      // Comando completo para lanzar
    std::string icon_path;       // Ruta al ícono (opcional)
    std::string install_dir;     // Directorio de instalación
    std::string app_id;          // ID de la plataforma (ej: Steam AppID)
    bool requires_launcher;      // Si necesita launcher abierto
};

/**
 * Clase base para detectores de plataforma
 */
class PlatformDetector {
public:
    virtual ~PlatformDetector() = default;
    virtual std::vector<DetectedGame> detect() = 0;
    virtual std::string get_platform_name() const = 0;
    virtual bool is_installed() const = 0;
};

/**
 * Detector para Steam
 */
class SteamDetector : public PlatformDetector {
public:
    std::vector<DetectedGame> detect() override;
    std::string get_platform_name() const override { return "steam"; }
    bool is_installed() const override;

private:
    std::optional<std::string> get_steam_path();
    std::vector<std::string> get_library_folders(const std::string& steam_path);
    std::vector<DetectedGame> parse_acf_files(const std::string& library_path);
    DetectedGame parse_acf_file(const std::string& acf_path);
};

/**
 * Detector para Epic Games Store
 */
class EpicDetector : public PlatformDetector {
public:
    std::vector<DetectedGame> detect() override;
    std::string get_platform_name() const override { return "epic"; }
    bool is_installed() const override;

private:
    std::optional<std::string> get_epic_manifests_path();
    DetectedGame parse_manifest(const std::string& manifest_path);
};

/**
 * Detector para GOG Galaxy
 */
class GOGDetector : public PlatformDetector {
public:
    std::vector<DetectedGame> detect() override;
    std::string get_platform_name() const override { return "gog"; }
    bool is_installed() const override;

private:
    std::optional<std::string> get_gog_path();
    std::vector<DetectedGame> parse_gog_registry();
};

/**
 * Clase principal del Game Detector
 */
class GameDetector {
public:
    GameDetector();
    
    /**
     * Detecta todos los juegos instalados en todas las plataformas
     */
    std::vector<DetectedGame> detect_all_games();
    
    /**
     * Detecta juegos de una plataforma específica
     */
    std::vector<DetectedGame> detect_platform(const std::string& platform);
    
    /**
     * Convierte la lista de juegos a JSON
     */
    std::string to_json(const std::vector<DetectedGame>& games);
    
    /**
     * Obtiene las plataformas disponibles
     */
    std::vector<std::string> get_available_platforms();
    
    /**
     * Instancia singleton
     */
    static GameDetector& instance();

private:
    std::vector<std::unique_ptr<PlatformDetector>> detectors_;
    void initialize_detectors();
};

/**
 * Funciones de utilidad para Windows Registry
 */
namespace winreg {
    std::optional<std::string> read_string(const std::string& key, const std::string& value);
    std::vector<std::string> enumerate_subkeys(const std::string& key);
}

/**
 * Funciones de utilidad para archivos
 */
namespace fileutil {
    bool file_exists(const std::string& path);
    bool directory_exists(const std::string& path);
    std::vector<std::string> list_files(const std::string& dir, const std::string& pattern);
    std::string read_file(const std::string& path);
=======
// --- Funciones de Ayuda (Windows) ---
#ifdef _WIN32
std::string get_registry_value(HKEY hKey, const std::string& subKey, const std::string& valueName) {
    HKEY hSubKey;
    if (RegOpenKeyEx(hKey, subKey.c_str(), 0, KEY_READ, &hSubKey) != ERROR_SUCCESS) {
        return "";
    }

    char buffer[MAX_PATH];
    DWORD bufferSize = sizeof(buffer);
    if (RegQueryValueEx(hSubKey, valueName.c_str(), nullptr, nullptr, (LPBYTE)buffer, &bufferSize) != ERROR_SUCCESS) {
        RegCloseKey(hSubKey);
        return "";
    }

    RegCloseKey(hSubKey);
    return std::string(buffer);
}
#endif

// --- Implementación de GameDetector ---

GameDetector& GameDetector::instance() {
    static GameDetector instance;
    return instance;
}

GameDetector::GameDetector() {
    initialize_detectors();
}

void GameDetector::initialize_detectors() {
    detectors_.push_back(std::make_unique<SteamDetector>());
    detectors_.push_back(std::make_unique<EpicDetector>());
    detectors_.push_back(std::make_unique<GOGDetector>());
}

std::vector<DetectedGame> GameDetector::detect_all_games() {
    std::vector<DetectedGame> all_games;
    BOOST_LOG(info) << "Iniciando detección de juegos...";
    for (const auto& detector : detectors_) {
        if (detector->is_installed()) {
            BOOST_LOG(info) << "Detectando juegos de: " << detector->get_platform_name();
            auto detected_games = detector->detect();
            all_games.insert(all_games.end(), detected_games.begin(), detected_games.end());
        }
    }
    BOOST_LOG(info) << "Detección de juegos completada. Total encontrados: " << all_games.size();
    return all_games;
}

std::vector<DetectedGame> GameDetector::detect_platform(const std::string& platform) {
    for (const auto& detector : detectors_) {
        if (detector->get_platform_name() == platform) {
            if (detector->is_installed()) {
                return detector->detect();
            }
            break;
        }
    }
    return {};
}

std::vector<std::string> GameDetector::get_available_platforms() {
    std::vector<std::string> platforms;
    for (const auto& detector : detectors_) {
        if (detector->is_installed()) {
            platforms.push_back(detector->get_platform_name());
        }
    }
    return platforms;
}

std::string GameDetector::to_json(const std::vector<DetectedGame>& games) {
    nlohmann::json json_games = nlohmann::json::array();
    for (const auto& game : games) {
        nlohmann::json json_game;
        json_game["id"] = game.id;
        json_game["name"] = game.name;
        json_game["platform"] = game.platform;
        json_game["executable"] = game.executable;
        json_game["launch_cmd"] = game.launch_cmd;
        json_game["icon_path"] = game.icon_path;
        json_game["install_dir"] = game.install_dir;
        json_game["app_id"] = game.app_id;
        json_game["requires_launcher"] = game.requires_launcher;
        json_games.push_back(json_game);
    }
    return json_games.dump(2);
}


// --- Implementación de SteamDetector ---

bool SteamDetector::is_installed() const {
#ifdef _WIN32
    return !get_registry_value(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath").empty();
#else
    return true; // Asumir que está instalado para pruebas en Linux
#endif
}

std::vector<DetectedGame> SteamDetector::detect() {
    std::vector<DetectedGame> games;
#ifdef _WIN32
    std::string steam_path = get_registry_value(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath");
    if (steam_path.empty()) {
        return games;
    }

    std::vector<std::string> library_paths;
    library_paths.push_back(steam_path);

    std::string library_folders_path = steam_path + "/steamapps/libraryfolders.vdf";
    std::ifstream vdf_file(library_folders_path);
    if (vdf_file.is_open()) {
        std::string line;
        std::regex path_regex("\"path\"\\s+\"(.+)\"");
        while (std::getline(vdf_file, line)) {
            std::smatch match;
            if (std::regex_search(line, match, path_regex) && match.size() > 1) {
                library_paths.push_back(match[1].str());
            }
        }
    }

    for (const auto& library_path : library_paths) {
        std::string steamapps_path = library_path + "/steamapps";
        for (const auto& entry : std::filesystem::directory_iterator(steamapps_path)) {
            if (entry.path().filename().string().rfind("appmanifest_", 0) == 0) {
                std::ifstream acf_file(entry.path());
                if (acf_file.is_open()) {
                    DetectedGame game;
                    game.platform = "Steam";

                    std::string line;
                    std::regex appid_regex("\"appid\"\\s+\"(\\d+)\"");
                    std::regex name_regex("\"name\"\\s+\"(.+)\"");
                    std::regex installdir_regex("\"installdir\"\\s+\"(.+)\"");

                    while (std::getline(acf_file, line)) {
                        std::smatch match;
                        if (std::regex_search(line, match, appid_regex)) game.app_id = match[1];
                        if (std::regex_search(line, match, name_regex)) game.name = match[1];
                        if (std::regex_search(line, match, installdir_regex)) game.install_dir = match[1];
                    }

                    if (!game.app_id.empty()) {
                        game.id = "steam:" + game.app_id;
                        game.launch_cmd = "steam://rungameid/" + game.app_id;
                        game.icon_path = steam_path + "/steam/games/" + game.app_id + ".ico";
                        games.push_back(game);
                    }
                }
            }
        }
    }
#else
    // Devolver datos de ejemplo para entornos no-Windows (como este)
    games.push_back({"steam:101", "Juego de Ejemplo 1", "Steam", "", "steam://rungameid/101", "", "/ruta/ejemplo1", "101", true});
    games.push_back({"steam:102", "Otro Juego de Prueba", "Steam", "", "steam://rungameid/102", "", "/ruta/ejemplo2", "102", true});
#endif
    return games;
}

// --- Implementaciones de Otros Detectores (Stubs) ---

bool EpicDetector::is_installed() const {
    BOOST_LOG(info) << "Epic detector stub: is_installed() llamado.";
    return false; // Deshabilitar para la prueba
}

std::vector<DetectedGame> EpicDetector::detect() {
    BOOST_LOG(info) << "Epic detector stub: detect() llamado.";
    return {};
}

bool GOGDetector::is_installed() const {
    BOOST_LOG(info) << "GOG detector stub: is_installed() llamado.";
    return false; // Deshabilitar para la prueba
}

std::vector<DetectedGame> GOGDetector::detect() {
    BOOST_LOG(info) << "GOG detector stub: detect() llamado.";
    return {};
>>>>>>> Stashed changes
}

} // namespace games
} // namespace sunshine
<<<<<<< Updated upstream

#endif // SUNSHINE_GAME_DETECTOR_H
=======
>>>>>>> Stashed changes
