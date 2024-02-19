/**
 * @file globals.cpp
 * @brief Implementation for globally accessible variables and functions.
 */
#include "globals.h"

/**
 * @brief A process-wide communication mechanism.
 */
safe::mail_t mail::man;

/**
 * @brief A thread pool for processing tasks.
 */
thread_pool_util::ThreadPool task_pool;

/**
 * @brief A boolean flag to indicate whether the cursor should be displayed.
 */
bool display_cursor = true;

#ifdef _WIN32
/**
 * @brief A global singleton used for NVIDIA control panel modifications.
 */
nvprefs::nvprefs_interface nvprefs_instance;
#endif
