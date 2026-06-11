/*
 * Adapted from bmem.c from OBS:
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

#include <stdlib.h>
#include <string.h>
#include <atomic>
#include "bmem.h"
#include "src/logging.h"

/*
 * NOTE: totally jacked the mem alignment trick from ffmpeg, credit to them:
 *   http://www.ffmpeg.org/
 */

#define ALIGNMENT 32

/*
 * Attention, intrepid adventurers, exploring the depths of the libobs code!
 *
 * There used to be a TODO comment here saying that we should use memalign on
 * non-Windows platforms. However, since *nix/POSIX systems do not provide an
 * aligned realloc(), this is currently not (easily) achievable.
 * So while the use of posix_memalign()/memalign() would be a fairly trivial
 * change, it would also ruin our memory alignment for some reallocated memory
 * on those platforms.
 */
#if defined(_WIN32)
#define ALIGNED_MALLOC 1
#else
#define ALIGNMENT_HACK 1
#endif

static void *a_malloc(size_t size)
{
#ifdef ALIGNED_MALLOC
	return _aligned_malloc(size, ALIGNMENT);
#elif ALIGNMENT_HACK
	void *ptr = NULL;
	long diff;

	ptr = malloc(size + ALIGNMENT);
	if (ptr) {
		diff = ((~(long)ptr) & (ALIGNMENT - 1)) + 1;
		ptr = (char *)ptr + diff;
		((char *)ptr)[-1] = (char)diff;

	}

	return ptr;
#else
	return malloc(size);
#endif
}

static void *a_realloc(void *ptr, size_t size)
{
#ifdef ALIGNED_MALLOC
	return _aligned_realloc(ptr, size, ALIGNMENT);
#elif ALIGNMENT_HACK
	long diff;

	if (!ptr)
		return a_malloc(size);
	diff = ((char *)ptr)[-1];
	ptr = realloc((char *)ptr - diff, size + diff);
	if (ptr)
		ptr = (char *)ptr + diff;
	return ptr;
#else
	return realloc(ptr, size);
#endif
}

static void a_free(void *ptr)
{
#ifdef ALIGNED_MALLOC
	_aligned_free(ptr);
#elif ALIGNMENT_HACK
	if (ptr)
		free((char *)ptr - ((char *)ptr)[-1]);
#else
	free(ptr);
#endif
}

std::atomic<long> num_allocs {0};

void *bmalloc(size_t size)
{
	if (!size) {
    BOOST_LOG(fatal) << "bmalloc: Allocating 0 bytes is broken behavior, please fix your code!";
	}

	void *ptr = a_malloc(size);

	if (!ptr) {
		BOOST_LOG(fatal) << "Out of memory while trying to allocate " << (unsigned long)size << " bytes";
	}

  num_allocs.fetch_add(1);

	return ptr;
}

void *brealloc(void *ptr, size_t size)
{
	if (!ptr)
    num_allocs.fetch_add(1);

	if (!size) {
		BOOST_LOG(fatal) << "brealloc: Allocating 0 bytes is broken behavior, please fix your code!";
	}

	ptr = a_realloc(ptr, size);

	if (!ptr) {
		BOOST_LOG(fatal) << "Out of memory while trying to allocate " << (unsigned long)size << " bytes";
	}

	return ptr;
}

void bfree(void *ptr)
{
	if (ptr) {
    num_allocs.fetch_sub(1);
		a_free(ptr);
	}
}

long bnum_allocs(void)
{
	return num_allocs.load();
}

int base_get_alignment(void)
{
	return ALIGNMENT;
}

void *bmemdup(const void *ptr, size_t size)
{
	void *out = bmalloc(size);
	if (size)
		memcpy(out, ptr, size);

	return out;
}
