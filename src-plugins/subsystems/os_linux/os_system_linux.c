
/*
 * The olsr.org Optimized Link-State Routing daemon version 2 (olsrd2)
 * Copyright (c) 2004-2013, the olsr.org team - see HISTORY file
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

/* must be first because of a problem with linux/rtnetlink.h */
#include <sys/socket.h>

/* and now the rest of the includes */
#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include "common/common_types.h"
#include "common/string.h"
#include "core/oonf_subsystem.h"
#include "subsystems/oonf_socket.h"

#include "subsystems/os_system.h"

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif

/* Definitions */
#define LOG_OS_SYSTEM _oonf_os_system_subsystem.logging

/* prototypes */
static int _init(void);
static void _cleanup(void);

static void _cb_handle_netlink_timeout(void *);
static void _netlink_handler(int fd, void *data,
    bool event_read, bool event_write);
static void _cb_rtnetlink_message(struct nlmsghdr *hdr);
static void _cb_rtnetlink_error(uint32_t seq, int error);
static void _cb_rtnetlink_done(uint32_t seq);
static void _cb_rtnetlink_timeout(void);
static void _address_finished(struct os_system_address *addr, int error);

static void _handle_nl_err(struct os_system_netlink *, struct nlmsghdr *);

/* ioctl socket */
static int _ioctl_fd = -1;

/* static buffers for receiving/sending a netlink message */
static struct sockaddr_nl _netlink_nladdr = {
  .nl_family = AF_NETLINK
};

static struct iovec _netlink_rcv_iov;
static struct msghdr _netlink_rcv_msg = {
  &_netlink_nladdr,
  sizeof(_netlink_nladdr),
  &_netlink_rcv_iov,
  1,
  NULL,
  0,
  0
};

static struct nlmsghdr _netlink_hdr_done = {
  .nlmsg_len = sizeof(struct nlmsghdr),
  .nlmsg_type = NLMSG_DONE
};

static struct iovec _netlink_send_iov[2] = {
    { NULL, 0 },
    { &_netlink_hdr_done, sizeof(_netlink_hdr_done) },
};

static struct msghdr _netlink_send_msg = {
  &_netlink_nladdr,
  sizeof(_netlink_nladdr),
  &_netlink_send_iov[0],
  1,
  NULL,
  0,
  0
};

/* netlink timeout handling */
static struct oonf_timer_class _netlink_timer= {
  .name = "netlink feedback timer",
  .callback = _cb_handle_netlink_timeout,
};

/* list of interface change listeners */
static struct list_entity _ifchange_listener;

/* subsystem definition */
static const char *_dependencies[] = {
  OONF_SOCKET_SUBSYSTEM,
};

static struct oonf_subsystem _oonf_os_system_subsystem = {
  .name = OONF_OS_SYSTEM_SUBSYSTEM,
  .dependencies = _dependencies,
  .dependencies_count = ARRAYSIZE(_dependencies),
  .init = _init,
  .cleanup = _cleanup,
};
DECLARE_OONF_PLUGIN(_oonf_os_system_subsystem);

/* built in rtnetlink receiver */
static struct os_system_netlink _rtnetlink_receiver = {
  .used_by = &_oonf_os_system_subsystem,
  .cb_message = _cb_rtnetlink_message,
  .cb_error = _cb_rtnetlink_error,
  .cb_done = _cb_rtnetlink_done,
  .cb_timeout = _cb_rtnetlink_timeout,
};

static struct list_entity _rtnetlink_feedback;

static const uint32_t _rtnetlink_mcast[] = {
  RTNLGRP_LINK, RTNLGRP_IPV4_IFADDR, RTNLGRP_IPV6_IFADDR
};

/* tracking of used netlink sequence numbers */
static uint32_t _seq_used = 0;

/**
 * Initialize os-specific subsystem
 * @return -1 if an error happened, 0 otherwise
 */
static int
_init(void) {
  _ioctl_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (_ioctl_fd == -1) {
    OONF_WARN(LOG_OS_SYSTEM, "Cannot open ioctl socket: %s (%d)",
        strerror(errno), errno);
    return -1;
  }

  if (os_system_netlink_add(&_rtnetlink_receiver, NETLINK_ROUTE)) {
    close(_ioctl_fd);
    return -1;
  }

  if (os_system_netlink_add_mc(&_rtnetlink_receiver, _rtnetlink_mcast, ARRAYSIZE(_rtnetlink_mcast))) {
    os_system_netlink_remove(&_rtnetlink_receiver);
    close(_ioctl_fd);
    return -1;
  }

  oonf_timer_add(&_netlink_timer);
  list_init_head(&_ifchange_listener);
  list_init_head(&_rtnetlink_feedback);
  return 0;
}

/**
 * Cleanup os-specific subsystem
 */
static void
_cleanup(void) {
  oonf_timer_remove(&_netlink_timer);
  os_system_netlink_remove(&_rtnetlink_receiver);
  close(_ioctl_fd);
}

/**
 * Set interface up or down
 * @param dev pointer to name of interface
 * @param up true if interface should be up, false if down
 * @return -1 if an error happened, 0 otherwise
 */
int
os_system_set_interface_state(const char *dev, bool up) {
  int oldflags;
  struct ifreq ifr;

  memset(&ifr, 0, sizeof(ifr));
  strscpy(ifr.ifr_name, dev, IFNAMSIZ);

  if (ioctl(_ioctl_fd, SIOCGIFFLAGS, &ifr) < 0) {
    OONF_WARN(LOG_OS_SYSTEM,
        "ioctl SIOCGIFFLAGS (get flags) error on device %s: %s (%d)\n",
        dev, strerror(errno), errno);
    return -1;
  }

  oldflags = ifr.ifr_flags;
  if (up) {
    ifr.ifr_flags |= IFF_UP;
  }
  else {
    ifr.ifr_flags &= ~IFF_UP;
  }

  if (oldflags == ifr.ifr_flags) {
    /* interface is already up/down */
    return 0;
  }

  if (ioctl(_ioctl_fd, SIOCSIFFLAGS, &ifr) < 0) {
    OONF_WARN(LOG_OS_SYSTEM,
        "ioctl SIOCSIFFLAGS (set flags %s) error on device %s: %s (%d)\n",
        up ? "up" : "down", dev, strerror(errno), errno);
    return -1;
  }
  return 0;
}

void
os_system_iflistener_add(struct os_system_if_listener *listener) {
  list_add_tail(&_ifchange_listener, &listener->_node);
}

void
os_system_iflistener_remove(struct os_system_if_listener *listener) {
  list_remove(&listener->_node);
}

/**
 * Open a new bidirectional netlink socket
 * @param nl pointer to initialized netlink socket handler
 * @param protocol protocol id (NETLINK_ROUTING for example)
 * @return -1 if an error happened, 0 otherwise
 */
int
os_system_netlink_add(struct os_system_netlink *nl, int protocol) {
  struct sockaddr_nl addr;

  nl->socket.fd = socket(PF_NETLINK, SOCK_RAW, protocol);
  if (nl->socket.fd < 0) {
    OONF_WARN(nl->used_by->logging, "Cannot open sync rtnetlink socket: %s (%d)",
        strerror(errno), errno);
    goto os_add_netlink_fail;
  }

  if (abuf_init(&nl->out)) {
    OONF_WARN(nl->used_by->logging, "Not enough memory for netlink output buffer");
    goto os_add_netlink_fail;
  }

  nl->in = calloc(1, getpagesize());
  if (nl->in == NULL) {
    OONF_WARN(nl->used_by->logging, "Not enough memory for netlink input buffer");
    goto os_add_netlink_fail;
  }
  nl->in_len = getpagesize();

  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;

  /* kernel will assign appropriate number instead of pid */
  /* addr.nl_pid = 0; */

  if (bind(nl->socket.fd, (struct sockaddr *)&addr, sizeof(addr))<0) {
    OONF_WARN(nl->used_by->logging, "Could not bind netlink socket: %s (%d)",
        strerror(errno), errno);
    goto os_add_netlink_fail;
  }

  nl->socket.process = _netlink_handler;
  nl->socket.event_read = true;
  nl->socket.data = nl;
  oonf_socket_add(&nl->socket);

  nl->timeout.cb_context = nl;
  nl->timeout.class = &_netlink_timer;

  return 0;

os_add_netlink_fail:
  if (nl->socket.fd != -1) {
    close(nl->socket.fd);
  }
  free (nl->in);
  abuf_free(&nl->out);
  return -1;
}

/**
 * Close a netlink socket handler
 * @param nl pointer to handler
 */
void
os_system_netlink_remove(struct os_system_netlink *nl) {
  oonf_socket_remove(&nl->socket);

  close(nl->socket.fd);
  free (nl->in);
  abuf_free(&nl->out);
}

/**
 * Add a netlink message to the outgoign queue of a handler
 * @param nl pointer to netlink handler
 * @param nl_hdr pointer to netlink message
 * @return sequence number used for message
 */
int
os_system_netlink_send(struct os_system_netlink *nl,
    struct nlmsghdr *nl_hdr) {
#if defined(OONF_LOG_DEBUG_INFO)
  struct autobuf hexbuf;
#endif

  OONF_INFO(nl->used_by->logging, "Prepare to send netlink message (%u bytes)",
      nl_hdr->nlmsg_len);
  _seq_used = (_seq_used + 1) & INT32_MAX;

  nl_hdr->nlmsg_seq = _seq_used;
  nl_hdr->nlmsg_flags |= NLM_F_ACK | NLM_F_MULTI;

  abuf_memcpy(&nl->out, nl_hdr, nl_hdr->nlmsg_len);

#if defined(OONF_LOG_DEBUG_INFO)
  abuf_init(&hexbuf);
  abuf_hexdump(&hexbuf, "", nl_hdr, nl_hdr->nlmsg_len);
  
  OONF_DEBUG(nl->used_by->logging, "Content of netlink message:\n%s", abuf_getptr(&hexbuf));
  abuf_free(&hexbuf);
#endif
  
  /* trigger write */
  oonf_socket_set_write(&nl->socket, true);
  return _seq_used;
}

/**
 * Join a list of multicast groups for a netlink socket
 * @param nl pointer to netlink handler
 * @param groups pointer to array of multicast groups
 * @param groupcount number of entries in groups array
 * @return -1 if an error happened, 0 otherwise
 */
int
os_system_netlink_add_mc(struct os_system_netlink *nl,
    const uint32_t *groups, size_t groupcount) {
  size_t i;

  for (i=0; i<groupcount; i++) {
    if (setsockopt(nl->socket.fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
             &groups[i], sizeof(groups[i]))) {
      OONF_WARN(nl->used_by->logging,
          "Could not join netlink mc group: %x", groups[i]);
      return -1;
    }
  }
  return 0;
}

/**
 * Leave a list of multicast groups for a netlink socket
 * @param nl pointer to netlink handler
 * @param groups pointer to array of multicast groups
 * @param groupcount number of entries in groups array
 * @return -1 if an error happened, 0 otherwise
 */
int
os_system_netlink_drop_mc(struct os_system_netlink *nl,
    const int *groups, size_t groupcount) {
  size_t i;

  for (i=0; i<groupcount; i++) {
    if (setsockopt(nl->socket.fd, SOL_NETLINK, NETLINK_DROP_MEMBERSHIP,
             &groups[i], sizeof(groups[i]))) {
      OONF_WARN(nl->used_by->logging,
          "Could not drop netlink mc group: %x", groups[i]);
      return -1;
    }
  }
  return 0;
}

/**
 * Add an attribute to a netlink message
 * @param n pointer to netlink header
 * @param type type of netlink attribute
 * @param data pointer to data of netlink attribute
 * @param len length of data of netlink attribute
 * @return -1 if netlink message got too large, 0 otherwise
 */
int
os_system_netlink_addreq(struct nlmsghdr *n,
    int type, const void *data, int len)
{
  struct nlattr *nl_attr;
  size_t aligned_msg_len, aligned_attr_len;

  /* calculate aligned length of message and new attribute */
  aligned_msg_len = NLMSG_ALIGN(n->nlmsg_len);
  aligned_attr_len = NLA_HDRLEN + len;

  if (aligned_msg_len + aligned_attr_len > UIO_MAXIOV) {
    OONF_WARN(LOG_OS_SYSTEM, "Netlink message got too large!");
    return -1;
  }

  nl_attr = (struct nlattr *) ((void*)((char *)n + aligned_msg_len));
  nl_attr->nla_type = type;
  nl_attr->nla_len = aligned_attr_len;

  /* fix length of netlink message */
  n->nlmsg_len = aligned_msg_len + aligned_attr_len;

  if (len) {
    memcpy((char *)nl_attr + NLA_HDRLEN, data, len);
  }
  return 0;
}

int
os_system_ifaddr_set(struct os_system_address *addr) {
  uint8_t buffer[UIO_MAXIOV];
  struct nlmsghdr *msg;
  struct ifaddrmsg *ifaddrreq;
  int seq;
#if defined(OONF_LOG_DEBUG_INFO)
  struct netaddr_str nbuf;
#endif

  memset(buffer, 0, sizeof(buffer));

  /* get pointers for netlink message */
  msg = (void *)&buffer[0];

  if (addr->set) {
    msg->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
    msg->nlmsg_type = RTM_NEWADDR;
  }
  else {
    msg->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    msg->nlmsg_type = RTM_DELADDR;
  }

  /* set length of netlink message with ifaddrmsg payload */
  msg->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));

  OONF_DEBUG(LOG_OS_SYSTEM, "%sset address on if %d: %s",
      addr->set ? "" : "re", addr->if_index,
      netaddr_to_string(&nbuf, &addr->address));

  ifaddrreq = NLMSG_DATA(msg);
  ifaddrreq->ifa_family = netaddr_get_address_family(&addr->address);
  ifaddrreq->ifa_prefixlen = netaddr_get_prefix_length(&addr->address);
  ifaddrreq->ifa_index= addr->if_index;
  ifaddrreq->ifa_scope = addr->scope;

  if (os_system_netlink_addnetaddr(msg, IFA_LOCAL, &addr->address)) {
    return -1;
  }

  /* cannot fail */
  seq = os_system_netlink_send(&_rtnetlink_receiver, msg);

  if (addr->cb_finished) {
    list_add_tail(&_rtnetlink_feedback, &addr->_internal._node);
    addr->_internal.nl_seq = seq;
  }
  return 0;
}

void
os_system_ifaddr_interrupt(struct os_system_address *addr) {
  if (list_is_node_added(&addr->_internal._node)) {
    /* remove first to prevent any kind of recursive cleanup */
    list_remove(&addr->_internal._node);

    if (addr->cb_finished) {
      addr->cb_finished(addr, -1);
    }
  }
}

/**
 * Handle timeout of netlink acks
 * @param ptr pointer to netlink handler
 */
static void
_cb_handle_netlink_timeout(void *ptr) {
  struct os_system_netlink *nl = ptr;

  if (nl->cb_timeout) {
    nl->cb_timeout();
  }
  nl->msg_in_transit = 0;
}

/**
 * Send all netlink messages in the outgoing queue to the kernel
 * @param nl pointer to netlink handler
 */
static void
_flush_netlink_buffer(struct os_system_netlink *nl) {
  ssize_t ret;
  int err;

  /* send outgoing message */
  _netlink_send_iov[0].iov_base = abuf_getptr(&nl->out);
  _netlink_send_iov[0].iov_len = abuf_getlen(&nl->out);

  if ((ret = sendmsg(nl->socket.fd, &_netlink_send_msg, 0)) <= 0) {
    err = errno;
    OONF_WARN(nl->used_by->logging,
        "Cannot send data to netlink socket: %s (%d)",
        strerror(err), err);

    /* remove netlink message from internal queue */
    nl->cb_error(nl->in->nlmsg_seq, err);
    return;
  }

  OONF_INFO(nl->used_by->logging, "Sent %"PRINTF_SSIZE_T_SPECIFIER
      "/%"PRINTF_SIZE_T_SPECIFIER" bytes for netlink seqno: %d",
      ret, abuf_getlen(&nl->out), _seq_used);
  abuf_clear(&nl->out);

  oonf_socket_set_write(&nl->socket, false);

  nl->msg_in_transit++;

  /* start feedback timer */
  oonf_timer_set(&nl->timeout, OS_SYSTEM_NETLINK_TIMEOUT);
}

/**
 * Cleanup netlink handler because all outstanding jobs
 * are finished
 * @param nl pointer to os_system_netlink handler
 */
static void
_netlink_job_finished(struct os_system_netlink *nl) {
  if (nl->msg_in_transit > 0) {
    nl->msg_in_transit--;
  }
  if (nl->msg_in_transit == 0) {
    oonf_timer_stop(&nl->timeout);
  }
  OONF_DEBUG(nl->used_by->logging, "netlink finished: %d still in transit",
      nl->msg_in_transit);
}

/**
 * Handler for incoming netlink messages
 * @param fd
 * @param data
 * @param event_read
 * @param event_write
 */
static void
_netlink_handler(int fd, void *data, bool event_read, bool event_write) {
  struct os_system_netlink *nl;
  struct nlmsghdr *nh;
  ssize_t ret;
  size_t len;
  int flags;
  uint32_t current_seq = 0;
  bool trigger_is_done;

#if defined(OONF_LOG_DEBUG_INFO)
  struct autobuf hexbuf;
#endif

  nl = data;
  if (event_write) {
    _flush_netlink_buffer(nl);
  }

  if (!event_read) {
    return;
  }

  /* handle incoming messages */
  _netlink_rcv_msg.msg_flags = 0;
  flags = MSG_PEEK;

netlink_rcv_retry:
  _netlink_rcv_iov.iov_base = nl->in;
  _netlink_rcv_iov.iov_len = nl->in_len;

  OONF_DEBUG(nl->used_by->logging, "Read netlink message with %"PRINTF_SIZE_T_SPECIFIER" bytes buffer",
      nl->in_len);
  if ((ret = recvmsg(fd, &_netlink_rcv_msg, MSG_DONTWAIT | flags)) < 0) {
    if (errno != EAGAIN) {
      OONF_WARN(nl->used_by->logging,"netlink recvmsg error: %s (%d)\n",
          strerror(errno), errno);
    }
    return;
  }

  /* not enough buffer space ? */
  if (nl->in_len < (size_t)ret || (_netlink_rcv_msg.msg_flags & MSG_TRUNC) != 0) {
    void *ptr;

    ptr = realloc(nl->in, nl->in_len + getpagesize());
    if (!ptr) {
      OONF_WARN(nl->used_by->logging, "Not enough memory to increase netlink input buffer");
      return;
    }
    nl->in = ptr;
    nl->in_len += getpagesize();
    goto netlink_rcv_retry;
  }
  if (flags) {
    /* it worked, not remove the message from the queue */
    flags = 0;
    OONF_DEBUG(nl->used_by->logging, "Got estimate of netlink message size, retrieve it");
    goto netlink_rcv_retry;
  }

  OONF_INFO(nl->used_by->logging, "Got netlink message of %"
      PRINTF_SSIZE_T_SPECIFIER" bytes", ret);

#if defined(OONF_LOG_DEBUG_INFO)
  abuf_init(&hexbuf);
  abuf_hexdump(&hexbuf, "", nl->in, ret);
  
  OONF_DEBUG(nl->used_by->logging, "Content of netlink message:\n%s", abuf_getptr(&hexbuf));
  abuf_free(&hexbuf);
#endif
  
  trigger_is_done = false;

  /* loop through netlink headers */
  len = (size_t) ret;
  for (nh = nl->in; NLMSG_OK (nh, len); nh = NLMSG_NEXT (nh, len)) {
    OONF_INFO(nl->used_by->logging,
        "Netlink message received: type %d seq %u\n", nh->nlmsg_type,
        nh->nlmsg_seq);

    if (nh == nl->in) {
      current_seq = nh->nlmsg_seq;
    }

    if (current_seq != nh->nlmsg_seq && trigger_is_done) {
      nl->cb_done(current_seq);
      trigger_is_done = false;
    }

    switch (nh->nlmsg_type) {
      case NLMSG_NOOP:
        break;

      case NLMSG_DONE:
        /* End of a multipart netlink message reached */
        trigger_is_done = true;
        break;

      case NLMSG_ERROR:
        /* Feedback for async netlink message */
        trigger_is_done = false;
        _handle_nl_err(nl, nh);
        break;

      default:
        if (nl->cb_message) {
          nl->cb_message(nh);
        }
        break;
    }
  }

  if (trigger_is_done) {
    oonf_timer_stop(&nl->timeout);
    nl->cb_done(current_seq);
    _netlink_job_finished(nl);
  }

  /* reset timeout if necessary */
  if (oonf_timer_is_active(&nl->timeout)) {
    oonf_timer_set(&nl->timeout, OS_SYSTEM_NETLINK_TIMEOUT);
  }
}

/**
 * Handle incoming rtnetlink multicast messages for interface listeners
 * @param hdr pointer to netlink message
 */
static void
_cb_rtnetlink_message(struct nlmsghdr *hdr) {
  struct ifinfomsg *ifi;
  struct ifaddrmsg *ifa;

  struct os_system_if_listener *listener;

  if (hdr->nlmsg_type == RTM_NEWLINK || hdr->nlmsg_type == RTM_DELLINK) {
    ifi = (struct ifinfomsg *) NLMSG_DATA(hdr);

    OONF_DEBUG(LOG_OS_SYSTEM, "Linkstatus of interface %d changed", ifi->ifi_index);
    list_for_each_element(&_ifchange_listener, listener, _node) {
      listener->if_changed(ifi->ifi_index, (ifi->ifi_flags & IFF_UP) == 0);
    }
  }

  else if (hdr->nlmsg_type == RTM_NEWADDR || hdr->nlmsg_type == RTM_DELADDR) {
    ifa = (struct ifaddrmsg *) NLMSG_DATA(hdr);

    OONF_DEBUG(LOG_OS_SYSTEM, "Address of interface %u changed", ifa->ifa_index);
    list_for_each_element(&_ifchange_listener, listener, _node) {
      listener->if_changed(ifa->ifa_index, (ifa->ifa_flags & IFF_UP) == 0);
    }
  }
}

/**
 * Handle feedback from netlink socket
 * @param seq
 * @param error
 */
static void
_cb_rtnetlink_error(uint32_t seq, int error) {
  struct os_system_address *addr;

  OONF_INFO(LOG_OS_SYSTEM, "Netlink socket provided feedback: %d %d", seq, error);

  /* transform into errno number */
  list_for_each_element(&_rtnetlink_feedback, addr, _internal._node) {
    if (seq == addr->_internal.nl_seq) {
      _address_finished(addr, error);
      break;
    }
  }
}

/**
 * Handle ack timeout from netlink socket
 */
static void
_cb_rtnetlink_timeout(void) {
  struct os_system_address *addr;

  OONF_INFO(LOG_OS_SYSTEM, "Netlink socket timed out");

  list_for_each_element(&_rtnetlink_feedback, addr, _internal._node) {
    _address_finished(addr, -1);
  }
}

/**
 * Handle done from multipart netlink messages
 * @param seq
 */
static void
_cb_rtnetlink_done(uint32_t seq) {
  struct os_system_address *addr;

  OONF_INFO(LOG_OS_SYSTEM, "Netlink operation finished: %u", seq);

  list_for_each_element(&_rtnetlink_feedback, addr, _internal._node) {
    if (seq == addr->_internal.nl_seq) {
      _address_finished(addr, 0);
      break;
    }
  }
}

/**
 * Stop processing of an ip address command and set error code
 * for callback
 * @param addr pointer to os_system_address
 * @param error error code, 0 if no error
 */
static void
_address_finished(struct os_system_address *addr, int error) {
  if (list_is_node_added(&addr->_internal._node)) {
    /* remove first to prevent any kind of recursive cleanup */
    list_remove(&addr->_internal._node);

    if (addr->cb_finished) {
      addr->cb_finished(addr, error);
    }
  }
}

/**
 * Handle result code in netlink message
 * @param nl pointer to netlink handler
 * @param nh pointer to netlink message
 */
static void
_handle_nl_err(struct os_system_netlink *nl, struct nlmsghdr *nh) {
  struct nlmsgerr *err;

  err = (struct nlmsgerr *) NLMSG_DATA(nh);

  OONF_INFO(nl->used_by->logging,
      "Received netlink seq %u feedback (%u bytes): %s (%d)",
      nh->nlmsg_seq, nh->nlmsg_len, strerror(-err->error), -err->error);

  if (err->error) {
    if (nl->cb_error) {
      nl->cb_error(err->msg.nlmsg_seq, -err->error);
    }
  }
  else {
    if (nl->cb_done) {
      nl->cb_done(err->msg.nlmsg_seq);
    }
  }

  _netlink_job_finished(nl);
}
