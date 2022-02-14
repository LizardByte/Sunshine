#include "network.h"
#include "thread_safe.h"

namespace http {

int init();
int create_creds(const std::string &pkey, const std::string &cert);
extern std::string unique_id;
extern net::net_e origin_pin_allowed;
extern net::net_e origin_web_api_allowed;

} // namespace http