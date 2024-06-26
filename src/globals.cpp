/**
 * @file globals.cpp
 * @brief Definitions for globally accessible variables and functions.
 */
#include "globals.h"

safe::mail_t mail::man;
thread_pool_util::ThreadPool task_pool;
bool display_cursor = true;

#ifdef _WIN32
nvprefs::nvprefs_interface nvprefs_instance;
#endif
