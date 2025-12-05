#include "game_detector.h"
#include "../logging.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <regex>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace sunshine {
namespace games {

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

std::string get_program_data_path() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_COMMON_APPDATA, NULL, 0, path))) {
        return std::string(path);
    }
    return "C:\\ProgramData";
}

// Helper to execute a command and return output
std::string exec_command(const std::string& cmd) {
    std::string result;
    char buffer[128];
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return "ERROR";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    _pclose(pipe);
    return result;
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
    detectors_.push_back(std::make_unique<XboxDetector>());
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

// --- Implementación de Epic Games Detector ---

bool EpicDetector::is_installed() const {
#ifdef _WIN32
    // Verificar si existe la carpeta de manifiestos
    std::string program_data = get_program_data_path();
    std::string manifests_path = program_data + "\\Epic\\EpicGamesLauncher\\Data\\Manifests";
    return std::filesystem::exists(manifests_path);
#else
    return false;
#endif
}

std::vector<DetectedGame> EpicDetector::detect() {
    std::vector<DetectedGame> games;
#ifdef _WIN32
    std::string program_data = get_program_data_path();
    std::string manifests_path = program_data + "\\Epic\\EpicGamesLauncher\\Data\\Manifests";

    if (!std::filesystem::exists(manifests_path)) {
        return games;
    }

    for (const auto& entry : std::filesystem::directory_iterator(manifests_path)) {
        if (entry.path().extension() == ".item") {
            try {
                std::ifstream item_file(entry.path());
                nlohmann::json item_json;
                item_file >> item_json;

                DetectedGame game;
                game.platform = "Epic";

                if (item_json.contains("DisplayName"))
                    game.name = item_json["DisplayName"];

                if (item_json.contains("InstallLocation"))
                    game.install_dir = item_json["InstallLocation"];

                if (item_json.contains("MainGameAppName"))
                    game.app_id = item_json["MainGameAppName"];

                if (item_json.contains("LaunchExecutable"))
                    game.executable = item_json["LaunchExecutable"];

                if (!game.name.empty() && !game.app_id.empty()) {
                    game.id = "epic:" + game.app_id;
                    // Comando de lanzamiento oficial de Epic
                    game.launch_cmd = "com.epicgames.launcher://apps/" + game.app_id + "?action=launch&silent=true";

                    // Buscar ícono si es posible (no siempre está en el manifiesto)
                    // game.icon_path = ...

                    games.push_back(game);
                }
            } catch (const std::exception& e) {
                BOOST_LOG(warning) << "Error parseando manifiesto de Epic: " << entry.path().string() << " - " << e.what();
            }
        }
    }
#endif
    return games;
}

// --- Implementación de GOG Galaxy Detector ---

bool GOGDetector::is_installed() const {
#ifdef _WIN32
    // Verificar si la clave de registro de GOG existe
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    // Intentar sin WOW6432Node
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
#else
    return false;
#endif
}

std::vector<DetectedGame> GOGDetector::detect() {
    std::vector<DetectedGame> games;
#ifdef _WIN32
    const std::vector<std::string> registry_paths = {
        "SOFTWARE\\WOW6432Node\\GOG.com\\Games",
        "SOFTWARE\\GOG.com\\Games"
    };

    for (const auto& reg_path : registry_paths) {
        HKEY hKey;
        if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, reg_path.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            continue;
        }

        DWORD index = 0;
        char subKeyName[256];
        DWORD subKeyNameSize = sizeof(subKeyName);

        while (RegEnumKeyEx(hKey, index, subKeyName, &subKeyNameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            std::string gameId = subKeyName;
            std::string gameKeyPath = reg_path + "\\" + gameId;

            std::string gameName = get_registry_value(HKEY_LOCAL_MACHINE, gameKeyPath, "gameName");
            std::string exe = get_registry_value(HKEY_LOCAL_MACHINE, gameKeyPath, "exe");
            std::string path = get_registry_value(HKEY_LOCAL_MACHINE, gameKeyPath, "path");

            if (!gameName.empty() && !path.empty()) {
                DetectedGame game;
                game.platform = "GOG";
                game.name = gameName;
                game.app_id = gameId;
                game.install_dir = path;
                game.id = "gog:" + gameId;

                // Construir ruta completa al ejecutable
                if (!exe.empty()) {
                    // Si 'exe' es relativo, lo unimos a 'path'. Si es absoluto, lo usamos tal cual.
                    std::filesystem::path exePath(exe);
                    if (exePath.is_absolute()) {
                        game.executable = exe;
                    } else {
                        game.executable = (std::filesystem::path(path) / exePath).string();
                    }
                    // Para GOG, lanzamos el ejecutable directamente
                    game.launch_cmd = "\"" + game.executable + "\"";
                }

                games.push_back(game);
            }

            subKeyNameSize = sizeof(subKeyName);
            index++;
        }
        RegCloseKey(hKey);
    }
#endif
    return games;
}

// --- Implementación de Xbox / Microsoft Store Detector ---

bool XboxDetector::is_installed() const {
#ifdef _WIN32
    // En Windows 10/11, la tienda siempre está presente
    return true;
#else
    return false;
#endif
}

std::vector<DetectedGame> XboxDetector::detect() {
    std::vector<DetectedGame> games;
#ifdef _WIN32
    // Comando PowerShell para obtener paquetes Appx filtrados
    // Se filtra por "SignatureKind: Store" para obtener apps de la tienda
    // NOTA: Esto puede traer muchas apps (Calculadora, Fotos, etc.).
    // Idealmente, en el futuro se debería filtrar por una lista de "juegos conocidos" o permitir al usuario ocultar.
    std::string ps_cmd = "powershell -NoProfile -Command \"Get-AppxPackage | Where-Object {$_.SignatureKind -eq 'Store' -and $_.IsFramework -eq $false} | Select-Object Name, PackageFamilyName, InstallLocation | ConvertTo-Json\"";

    std::string json_output = exec_command(ps_cmd);
    if (json_output.empty() || json_output == "ERROR") {
        BOOST_LOG(warning) << "Fallo al ejecutar PowerShell para detección de Xbox/Store";
        return games;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(json_output);

        // Convertir a array si es un objeto único (PowerShell ConvertTo-Json quirks)
        if (!root.is_array()) {
            root = nlohmann::json::array({root});
        }

        for (const auto& item : root) {
            std::string install_loc;
            if (item.contains("InstallLocation") && !item["InstallLocation"].is_null()) {
                install_loc = item["InstallLocation"];
            }

            // Filtrar apps del sistema vacías o sin ruta de instalación
            if (install_loc.empty()) continue;

            DetectedGame game;
            game.platform = "Xbox";

            if (item.contains("Name")) game.name = item["Name"];
            if (item.contains("PackageFamilyName")) game.app_id = item["PackageFamilyName"]; // Usamos PFN como AppID

            game.install_dir = install_loc;
            game.id = "xbox:" + game.app_id;

            // Comando mágico para lanzar apps UWP: shell:AppsFolder\PackageFamilyName!AppId
            // Nota: "!App" es el sufijo más común, pero algunos juegos usan "!Game" u otros.
            // Una forma más robusta sería parsear el AppxManifest.xml, pero por simplicidad usaremos "!App"
            // y dejaremos que el usuario lo edite si falla.
            game.launch_cmd = "shell:AppsFolder\\" + game.app_id + "!App";

            // Filtro rudimentario para evitar listar "Calculadora" y similares como "Juegos"
            // Se asume que los juegos suelen estar en carpetas como "WindowsApps" o "XboxGames"
            // y que el usuario puede tenerlos en cualquier disco (D:\XboxGames, etc.)
            // Por ahora, agregamos todo lo que parezca una app de la tienda no-framework.
            games.push_back(game);
        }

    } catch (const std::exception& e) {
        BOOST_LOG(warning) << "Error parseando JSON de Xbox/Store: " << e.what();
    }
#endif
    return games;
}

} // namespace games
} // namespace sunshine
