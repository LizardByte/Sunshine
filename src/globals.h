/**
 * @file globals.h
 * @brief Header for globally accessible variables and functions.
 */
#pragma once

#include "entry_handler.h"
#include "thread_pool.h"

extern safe::mail_t mail::man;
extern thread_pool_util::ThreadPool task_pool;
extern bool display_cursor;
