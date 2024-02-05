/**
 * @file src/logging.h
 * @brief Logging header file for the Sunshine application.
 */

// macros
#pragma once

// lib includes
#include <boost/log/common.hpp>

extern boost::log::sources::severity_logger<int> verbose;
extern boost::log::sources::severity_logger<int> debug;
extern boost::log::sources::severity_logger<int> info;
extern boost::log::sources::severity_logger<int> warning;
extern boost::log::sources::severity_logger<int> error;
extern boost::log::sources::severity_logger<int> fatal;
