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

namespace sunshine {
namespace games {

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
 * Detector para Xbox / Microsoft Store (Game Pass)
 */
class XboxDetector : public PlatformDetector {
public:
    std::vector<DetectedGame> detect() override;
    std::string get_platform_name() const override { return "xbox"; }
    bool is_installed() const override;
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
}

} // namespace games
} // namespace sunshine

#endif // SUNSHINE_GAME_DETECTOR_H