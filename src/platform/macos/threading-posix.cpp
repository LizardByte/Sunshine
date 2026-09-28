/*
 * Adapted from threading-posix.c from OBS. Non-Apple code removed.
 *
 * Copyright (c) 2023 Lain Bailey <lain@obsproject.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/time.h>
#include <mach/semaphore.h>
#include <mach/task.h>
#include <mach/mach_init.h>
#include <cerrno>

#include "bmem.h"
#include "threading.h"

int os_event_init(os_event_t **event, enum os_event_type type)
{
	int code = 0;

	struct os_event_data *data = (struct os_event_data *)bzalloc(sizeof(struct os_event_data));

	if ((code = pthread_mutex_init(&data->mutex, NULL)) < 0) {
		bfree(data);
		return code;
	}

	if ((code = pthread_cond_init(&data->cond, NULL)) < 0) {
		pthread_mutex_destroy(&data->mutex);
		bfree(data);
		return code;
	}

	data->manual = (type == OS_EVENT_TYPE_MANUAL);
	data->signalled = false;
	*event = data;

	return 0;
}

void os_event_destroy(os_event_t *event)
{
	if (event) {
		pthread_mutex_destroy(&event->mutex);
		pthread_cond_destroy(&event->cond);
		bfree(event);
	}
}

int os_event_wait(os_event_t *event)
{
	int code = 0;
	pthread_mutex_lock(&event->mutex);
	while (!event->signalled) {
		code = pthread_cond_wait(&event->cond, &event->mutex);
		if (code != 0)
			break;
	}

	if (code == 0) {
		if (!event->manual)
			event->signalled = false;
	}

	pthread_mutex_unlock(&event->mutex);

	return code;
}

static inline void add_ms_to_ts(struct timespec *ts, unsigned long milliseconds)
{
	ts->tv_sec += milliseconds / 1000;
	ts->tv_nsec += (milliseconds % 1000) * 1000000;
	if (ts->tv_nsec >= 1000000000) {
		ts->tv_sec += 1;
		ts->tv_nsec -= 1000000000;
	}
}

int os_event_timedwait(os_event_t *event, unsigned long milliseconds)
{
	int code = 0;
	pthread_mutex_lock(&event->mutex);
	while (!event->signalled) {
		struct timespec ts;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		ts.tv_sec = tv.tv_sec;
		ts.tv_nsec = tv.tv_usec * 1000;
		add_ms_to_ts(&ts, milliseconds);
		code = pthread_cond_timedwait(&event->cond, &event->mutex, &ts);
		if (code != 0)
			break;
	}

	if (code == 0) {
		if (!event->manual)
			event->signalled = false;
	}

	pthread_mutex_unlock(&event->mutex);

	return code;
}

int os_event_try(os_event_t *event)
{
	int ret = EAGAIN;

	pthread_mutex_lock(&event->mutex);
	if (event->signalled) {
		if (!event->manual)
			event->signalled = false;
		ret = 0;
	}
	pthread_mutex_unlock(&event->mutex);

	return ret;
}

int os_event_signal(os_event_t *event)
{
	int code = 0;

	pthread_mutex_lock(&event->mutex);
	code = pthread_cond_signal(&event->cond);
	event->signalled = true;
	pthread_mutex_unlock(&event->mutex);

	return code;
}

void os_event_reset(os_event_t *event)
{
	pthread_mutex_lock(&event->mutex);
	event->signalled = false;
	pthread_mutex_unlock(&event->mutex);
}

int os_sem_init(os_sem_t **sem, int value)
{
	semaphore_t new_sem;
	task_t task = mach_task_self();

	if (semaphore_create(task, &new_sem, 0, value) != KERN_SUCCESS)
		return -1;

	*sem = (os_sem_t *)bzalloc(sizeof(struct os_sem_data));
	if (!*sem)
		return -2;

	(*sem)->sem = new_sem;
	(*sem)->task = task;
	return 0;
}

void os_sem_destroy(os_sem_t *sem)
{
	if (sem) {
		semaphore_destroy(sem->task, sem->sem);
		bfree(sem);
	}
}

int os_sem_post(os_sem_t *sem)
{
	if (!sem)
		return -1;
	return (semaphore_signal(sem->sem) == KERN_SUCCESS) ? 0 : -1;
}

int os_sem_wait(os_sem_t *sem)
{
	if (!sem)
		return -1;
	return (semaphore_wait(sem->sem) == KERN_SUCCESS) ? 0 : -1;
}
