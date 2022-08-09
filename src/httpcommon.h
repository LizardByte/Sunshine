#include "network.h"
#include "thread_safe.h"

namespace http {

int init();
int create_creds(const std::string &pkey, const std::string &cert);
int save_user_creds(
  const std::string &file,
  const std::string &username,
  const std::string &password,
  bool run_our_mouth = false);

int reload_user_creds(const std::string &file);
extern std::string unique_id;
extern net::net_e origin_pin_allowed;
extern net::net_e origin_web_ui_allowed;

} // namespace http