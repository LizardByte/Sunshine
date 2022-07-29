#include "network.h"
#include "thread_safe.h"

namespace http {

int init();
int create_creds(const std::string &pkey, const std::string &cert);
bool save_credentials(std::string password, bool isHashed);
bool load_credentials(const std::string &password);
int renew_credentials(const std::string &old_password, std::string new_password);
bool credentials_exists();

extern std::string unique_id;
extern net::net_e origin_pin_allowed;

} // namespace http