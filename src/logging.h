/**
 * @file src/logging.h
 * @brief Logging header file for the Sunshine application.
 */

// macros
#pragma once

// lib includes
#include <boost/log/common.hpp>
#include <boost/log/sinks.hpp>

extern boost::shared_ptr<boost::log::sinks::asynchronous_sink<boost::log::sinks::text_ostream_backend>> sink;
using text_sink = boost::log::sinks::asynchronous_sink<boost::log::sinks::text_ostream_backend>;

extern boost::log::sources::severity_logger<int> verbose;
extern boost::log::sources::severity_logger<int> debug;
extern boost::log::sources::severity_logger<int> info;
extern boost::log::sources::severity_logger<int> warning;
extern boost::log::sources::severity_logger<int> error;
extern boost::log::sources::severity_logger<int> fatal;

// functions
void
log_flush();
void
print_help(const char *name);
