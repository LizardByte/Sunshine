/**
 * @file tests/utils.cpp
 * @brief Definition for utility functions.
 */
#include "utils.h"

/**
 * @brief Set an environment variable.
 * @param name Name of the environment variable
 * @param value Value of the environment variable
 * @return 0 on success, non-zero error code on failure
 */
int
setEnv(const std::string &name, const std::string &value) {
#ifdef _WIN32
  return _putenv_s(name.c_str(), value.c_str());
#else
  return setenv(name.c_str(), value.c_str(), 1);
#endif
}
