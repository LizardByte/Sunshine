#include <string>
#include <chrono>
#include <vector>

struct ctx_t {
    std::vector<std::string> prep_cmds;
    std::vector<std::string> detached;
    
    std::string name;
    std::string cmd;
    std::string working_dir;
    std::string output;
    std::string image_path;
    std::string id;
    
    // Platform metadata fields for Backbone/Fuji integration
    std::string platform;      // "steam", "epic", "gog", etc
    std::string platform_id;   // "570", "Fortnite", etc  
    std::string fuji_game_id;  // "steam_570", "epic_Fortnite"
    
    bool elevated;
    bool auto_detach;
    bool wait_all;
    std::chrono::seconds exit_timeout;
};

int main() {
    ctx_t ctx;
    ctx.platform = "steam";
    ctx.platform_id = "570";
    ctx.fuji_game_id = "steam_570";
    return 0;
}
