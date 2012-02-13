/*
 * httptelnet.c
 *
 *  Created on: Oct 7, 2011
 *      Author: rogge
 */

#include "common/common_types.h"
#include "common/autobuf.h"

#include "config/cfg_schema.h"
#include "olsr_cfg.h"
#include "olsr_http.h"
#include "olsr_logging.h"
#include "olsr_plugins.h"
#include "olsr_telnet.h"
#include "olsr.h"

/* constants */
#define _CFG_SECTION "httptelnet"

static const char *_HTTP_PATH = "/telnet";

/* prototypes */
static int _cb_plugin_load(void);
static int _cb_plugin_unload(void);
static int _cb_plugin_enable(void);
static int _cb_plugin_disable(void);

static enum olsr_http_result _cb_generate_site(
    struct autobuf *out, struct olsr_http_session *);

static void _cb_config_changed(void);

/* html handler */
struct olsr_http_handler _http_site_handler = {
  .content_handler = _cb_generate_site,
};

/* plugin declaration */
OLSR_PLUGIN7 {
  .descr = "OLSRD http2telnet bridge plugin",
  .author = "Henning Rogge",

  .load = _cb_plugin_load,
  .unload = _cb_plugin_unload,
  .enable = _cb_plugin_enable,
  .disable = _cb_plugin_disable,

  .deactivate = true,
};

/* configuration */
static struct cfg_schema_section _httptelnet_section = {
  .type = _CFG_SECTION,
  .cb_delta_handler = _cb_config_changed,
};

static struct cfg_schema_entry _httptelnet_entries[] = {
  CFG_MAP_STRING(olsr_http_handler, site, "site", "/telnet", "Path for http2telnet bridge"),
  CFG_MAP_ACL(olsr_http_handler, acl, "acl", "default_accept", "acl for http2telnet bridge"),
  CFG_MAP_STRINGLIST(olsr_http_handler, auth, "auth", "", "TODO"),
};

static const char *_last_site;

/**
 * Constructor of plugin
 * @return 0 if initialization was successful, -1 otherwise
 */
static int
_cb_plugin_load(void) {
  _http_site_handler.site = strdup(_HTTP_PATH);
  if (_http_site_handler.site == NULL) {
    return -1;
  }

  _last_site = _http_site_handler.site;

  cfg_schema_add_section(olsr_cfg_get_schema(), &_httptelnet_section,
      _httptelnet_entries, ARRAYSIZE(_httptelnet_entries));

  olsr_acl_add(&_http_site_handler.acl);
  strarray_init(&_http_site_handler.auth);

  return 0;
}

/**
 * Destructor of plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_unload(void) {
  strarray_free(&_http_site_handler.auth);
  olsr_acl_remove(&_http_site_handler.acl);
  free((char *)_http_site_handler.site);

  cfg_schema_remove_section(olsr_cfg_get_schema(), &_httptelnet_section);
  return 0;
}

/**
 * Enable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_enable(void) {
  olsr_http_add(&_http_site_handler);
  return 0;
}

/**
 * Disable plugin
 * @return always returns 0 (cannot fail)
 */
static int
_cb_plugin_disable(void) {
  olsr_http_remove(&_http_site_handler);
  return 0;
}

/**
 * Callback for generating a http site from the output of the
 * triggered telnet command
 * @param out pointer to output buffer
 * @param session pointer to http session
 * @return http result code
 */
static enum olsr_http_result
_cb_generate_site(struct autobuf *out, struct olsr_http_session *session) {
  const char *command, *param;

  command = olsr_http_lookup_param(session, "c");
  param = olsr_http_lookup_param(session, "p");

  if (command == NULL) {
    return HTTP_404_NOT_FOUND;
  }

  switch (olsr_telnet_execute(command, param, out, session->remote)) {
    case TELNET_RESULT_ACTIVE:
    case TELNET_RESULT_QUIT:
      session->content_type = HTTP_CONTENTTYPE_TEXT;
      return HTTP_200_OK;

    case TELNET_RESULT_UNKNOWN_COMMAND:
      return HTTP_404_NOT_FOUND;

    default:
      return HTTP_400_BAD_REQ;
  }
}

/**
 * Update configuration of remotecontrol plugin
 */
static void
_cb_config_changed(void) {
  if (cfg_schema_tobin(&_http_site_handler, _httptelnet_section.post,
      _httptelnet_entries, ARRAYSIZE(_httptelnet_entries))) {
    OLSR_WARN(LOG_CONFIG, "Could not convert httptelnet config to bin");
    return;
  }

  if (_http_site_handler.site != _last_site) {
    _last_site = _http_site_handler.site;

    _cb_plugin_disable();
    _cb_plugin_enable();
  }
}
