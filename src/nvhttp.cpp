#include <iostream>
#include <sstream>
#include <string>
#include "sunshine_api.h"  // Tu API de Sunshine
#include "server_http.hpp"  // Librería del servidor

using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

int main() {
    HttpServer server;
    server.config.port = 8080;

    // Obtener todos los juegos detectados
    server.resource["^/api/games/detected$"]["GET"] = 
        [](auto response, auto request) {
            try {
                std::string json = sunshine::api::get_detected_games();
                std::ostringstream stream;
                stream << "HTTP/1.1 200 OK\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << json.length() << "\r\n"
                       << "Access-Control-Allow-Origin: *\r\n\r\n"
                       << json;
                *response << stream.str();
            } catch(const std::exception &e) {
                std::string error = "{\"error\":\"" + std::string(e.what()) + "\"}";
                std::ostringstream stream;
                stream << "HTTP/1.1 500 Internal Server Error\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << error.length() << "\r\n"
                       << "Access-Control-Allow-Origin: *\r\n\r\n"
                       << error;
                *response << stream.str();
            }
        };

    // Obtener juegos por plataforma
    server.resource["^/api/games/detected/([a-z]+)$"]["GET"] = 
        [](auto response, auto request) {
            try {
                std::string platform = request->path_match[1];
                std::string json = sunshine::api::get_platform_games(platform);
                std::ostringstream stream;
                stream << "HTTP/1.1 200 OK\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << json.length() << "\r\n"
                       << "Access-Control-Allow-Origin: *\r\n\r\n"
                       << json;
                *response << stream.str();
            } catch(const std::exception &e) {
                std::string error = "{\"error\":\"" + std::string(e.what()) + "\"}";
                std::ostringstream stream;
                stream << "HTTP/1.1 500 Internal Server Error\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << error.length() << "\r\n"
                       << "Access-Control-Allow-Origin: *\r\n\r\n"
                       << error;
                *response << stream.str();
            }
        };

    // Obtener plataformas disponibles
    server.resource["^/api/games/platforms$"]["GET"] = 
        [](auto response, auto request) {
            try {
                std::string json = sunshine::api::get_available_platforms();
                std::ostringstream stream;
                stream << "HTTP/1.1 200 OK\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << json.length() << "\r\n"
                       << "Access-Control-Allow-Origin: *\r\n\r\n"
                       << json;
                *response << stream.str();
            } catch(const std::exception &e) {
                std::string error = "{\"error\":\"" + std::string(e.what()) + "\"}";
                std::ostringstream stream;
                stream << "HTTP/1.1 500 Internal Server Error\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << error.length() << "\r\n"
                       << "Access-Control-Allow-Origin: *\r\n\r\n"
                       << error;
                *response << stream.str();
            }
        };

    // Refrescar lista de juegos
    server.resource["^/api/games/refresh$"]["POST"] = 
        [](auto response, auto request) {
            try {
                std::string json = sunshine::api::refresh_games();
                std::ostringstream stream;
                stream << "HTTP/1.1 200 OK\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << json.length() << "\r\n"
                       << "Access-Control-Allow-Origin: *\r\n\r\n"
                       << json;
                *response << stream.str();
            } catch(const std::exception &e) {
                std::string error = "{\"error\":\"" + std::string(e.what()) + "\"}";
                std::ostringstream stream;
                stream << "HTTP/1.1 500 Internal Server Error\r\n"
                       << "Content-Type: application/json\r\n"
                       << "Content-Length: " << error.length() << "\r\n"
                       << "Access-Control-Allow-Origin: *\r\n\r\n"
                       << error;
                *response << stream.str();
            }
        };

    server.start();
}
