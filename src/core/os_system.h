
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2011, the olsr.org team - see HISTORY file
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

#ifndef OS_SYSTEM_H_
#define OS_SYSTEM_H_

#include <stdio.h>
#include <sys/time.h>

#include "common/common_types.h"
#include "olsr_logging.h"

#define MSEC_PER_SEC 1000
#define USEC_PER_MSEC 1000

/*
 * Set one of the following defines in the os specific os_net includes
 * to OS_SPECIFIC to define that the os code is implementing the function
 * itself and does not use the generic function
 * Set it to OS_GENERIC to define that the code use the default implementation.
 *
 * Example from os_system_linux.h:
 *
 * #define OS_SYSTEM_GETTIMEOFDAY OS_GENERIC
 * #define OS_SYSTEM_LOG          OS_GENERIC
 */

/* set the guard macro so we can include the os specific settings */
#define OS_NET_SPECIFIC_INCLUDE
#include "os_helper.h"

#ifdef OS_LINUX
#include "os_linux/os_system_linux.h"
#endif

#ifdef OS_BSD
#include "os_bsd/os_system_bsd.h"
#endif

#ifdef OS_WIN32
#include "os_win32/os_system_win32.h"
#endif

#undef OS_NET_SPECIFIC_INCLUDE

/* prototypes for all os_system functions */
EXPORT int os_system_init(void);
EXPORT void os_system_cleanup(void);

EXPORT void os_system_openlog(void);
EXPORT void os_system_closelog(void);
EXPORT void os_system_log(enum log_severity, const char *);
static INLINE int os_system_gettimeofday(struct timeval *);

/*
 * INLINE implementations for generic os_net functions
 */

#if OS_NET_CLOSE == OS_GENERIC
/**
 * Close a file descriptor
 * @param fd filedescriptor
 */
static INLINE int
os_system_gettimeofday(struct timeval *tv) {
  return gettimeofday(tv, NULL);
}
#endif


#endif /* OS_SYSTEM_H_ */
