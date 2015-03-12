
/*
 * The olsr.org Optimized Link-State Routing daemon version 2 (olsrd2)
 * Copyright (c) 2004-2015, the olsr.org team - see HISTORY file
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

#include "common/netaddr.h"
#include "subsystems/os_routing.h"

/**
 * Print OS route to string buffer
 * @param buf pointer to string buffer
 * @param route pointer to route
 * @return pointer to string buffer, NULL if an error happened
 */
const char *
os_routing_to_string(struct os_route_str *buf, struct os_route *route) {
  struct netaddr_str buf1, buf2, buf3, buf4;
  char ifbuf[IF_NAMESIZE];
  int result;
  result = snprintf(buf->buf, sizeof(*buf),
      "'src-ip %s gw %s dst %s src-prefix %s metric %u table %u protocol %u if %s (%u)'",
      netaddr_to_string(&buf1, &route->data.src_ip),
      netaddr_to_string(&buf2, &route->data.gw),
      netaddr_to_string(&buf3, &route->data.dst),
      netaddr_to_string(&buf4, &route->data.src_prefix),
      route->data.metric,
      (unsigned int)(route->data.table),
      (unsigned int)(route->data.protocol),
      if_indextoname(route->data.if_index, ifbuf),
      route->data.if_index);

  if (result < 0 || result > (int)sizeof(*buf)) {
    return NULL;
  }
  return buf->buf;
}
