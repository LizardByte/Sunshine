#include <string>
#include <optional>
#include <iostream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace pt = boost::property_tree;

struct ctx_t {
    std::string name;
    std::string platform;
    std::string platform_id; 
    std::string fuji_game_id;
};

int main() {
    pt::ptree tree;
    tree.put("apps.0.name", "Test Game");
    tree.put("apps.0.platform", "steam");
    tree.put("apps.0.platform_id", "570");
    tree.put("apps.0.fuji_game_id", "steam_570");
    
    auto& app_node = tree.get_child("apps.0");
    
    ctx_t ctx;
    ctx.name = app_node.get<std::string>("name");
    
    // Our new parsing code
    auto platform = app_node.get_optional<std::string>("platform");
    auto platform_id = app_node.get_optional<std::string>("platform_id");
    auto fuji_game_id = app_node.get_optional<std::string>("fuji_game_id");
    
    if (platform) {
        ctx.platform = *platform;
    }
    if (platform_id) {
        ctx.platform_id = *platform_id;
    }
    if (fuji_game_id) {
        ctx.fuji_game_id = *fuji_game_id;
    }
    
    std::cout << "Parsed: " << ctx.name << " (" << ctx.fuji_game_id << ")" << std::endl;
    
    // Test XML output like in nvhttp.cpp
    pt::ptree xml_app;
    xml_app.put("AppTitle", ctx.name);
    if (!ctx.platform.empty()) {
        xml_app.put("Platform", ctx.platform);
    }
    if (!ctx.platform_id.empty()) {
        xml_app.put("PlatformID", ctx.platform_id);
    }
    if (!ctx.fuji_game_id.empty()) {
        xml_app.put("FujiGameID", ctx.fuji_game_id);
    }
    
    std::cout << "✓ Code compiles successfully" << std::endl;
    return 0;
}
