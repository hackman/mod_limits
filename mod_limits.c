/*
 * Copyright (C) 2010-2011 Marian Marinov <mm@yuhu.biz>
 * 
 * This code is based on the work of David Jao <djao@dominia.org> and
 * Maxim Chirkov <mc@tyumen.ru>, who made mod_limitipconn for Apache 1.3
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, this permission notice, and the
 * following disclaimer shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "http_request.h"
#include "util_script.h"
#include "http_connection.h"
#include "scoreboard.h"
#include "ap_mpm.h"
#include "apr_strings.h"

#define MODULE_NAME "mod_m2"
#define MODULE_VERSION "0.04"

module AP_MODULE_DECLARE_DATA m2_module;

static int server_limit, thread_limit;

typedef struct {
    unsigned int ip;	/* max number of connections per IP */
    unsigned int uid;	/* max number of connections per UID */
    unsigned int vhost;	/* max number of connections per VHost */
} m2_config;

static void *m2_create_dir_config(apr_pool_t *p, char *path) {
	m2_config *cfg = (m2_config *) apr_pcalloc(p, sizeof(m2_config));

    /* default configuration: no limit, and both arrays are empty */
    cfg->ip = 0;
    cfg->uid = 0;
    cfg->vhost = 0;

    return (void *) cfg;
}
static int m2_handler(request_rec *r) {
	// get configuration information 
	m2_config *limits = (m2_config *) ap_get_module_config(r->per_dir_config, &m2_module);
	// loop index variable 
	int i,j;
	// current connection count from this address 
	int ip_count = 0;
	// scoreboard data structure 
	worker_score *ws_record;
	/* We decline to handle subrequests: otherwise, in the next step we
	 * could get into an infinite loop. */
	if (!ap_is_initial_req(r))
		return DECLINED;

	ap_log_error(APLOG_MARK, APLOG_DEBUG, OK, r->server, 
		"%s: current limit %d", MODULE_NAME, limits->ip);

    /* A limit value of 0 by convention means no limit. */
    if (limits->ip == 0)
        return OK; 

    for (i = 0; i < server_limit; ++i) {
        for (j = 0; j < thread_limit; ++j) {
            ws_record = ap_get_scoreboard_worker(i, j);
			/* Count the number of connections from this IP address 
			 * from the scoreboard */ 
            if (strcmp(r->connection->remote_ip, ws_record->client) == 0)
				ip_count++;
			if (ip_count > limits->ip) {
				ap_log_error(APLOG_MARK, APLOG_INFO, OK, r->server, 
					"%s client exceeded connection limit", r->connection->remote_ip);
				/* set an environment variable */
				apr_table_setn(r->subprocess_env, "LIMITIP", "1");
				/* return 503 */
				return HTTP_SERVICE_UNAVAILABLE;
			}
		}
	}
		
	ap_log_error(APLOG_MARK, APLOG_DEBUG, OK, r->server, 
		"%s: current connection count for this client: %d", MODULE_NAME, ip_count);
	
	return OK;
}

/* Parse the MaxConnsPerIP directive */
static const char *cfg_perip(cmd_parms *cmd, void *mconfig, const char *arg) {
	m2_config *limits = (m2_config *) mconfig;
	unsigned long int limit = strtol(arg, (char **) NULL, 10);
	if (limit == LONG_MAX) 
		return "Integer overflow or invalid number";
	limits->ip = limit;
	return NULL;
}


/* Array describing structure of configuration directives */
static command_rec m2_cmds[] = {
	AP_INIT_TAKE1(
		"MaxConnsPerIP", cfg_perip, NULL, RSRC_CONF, 
		"maximum simultaneous connections per IP address" ),
	AP_INIT_TAKE1(
		"MaxConnsPerUid", cfg_peruid, NULL, RSRC_CONF,
		"maximum simultaneous connections per user"	),
	AP_INIT_TAKE1(
		"MaxConnsPerVhost", cfg_pervhost, NULL, RSRC_CONF,
		"maximum simultaneous connections per virtual host" ),
	AP_INIT_TAKE1(
		"MaxLoad1", cfg_load1, NULL, RSRC_CONF,
		"maximum Load Overage value for the past 1 minute" ),
	AP_INIT_TAKE1(
		"MaxLoad5", cfg_load5, NULL, RSRC_CONF,
		"maximum Load Overage value for the past 5 minutes" ),
	AP_INIT_TAKE1(
		"MaxLoad15", cfg_load15, NULL, RSRC_CONF,
		"maximum Load Overage value for the past 15 minutes" ),
	{NULL}
};

/* Emit an informational-level log message on startup and init the thread_limit and server_limit  */
static int m2_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
    ap_log_error(APLOG_MARK, APLOG_DEBUG, OK, s, "%s/%s loaded", MODULE_NAME, MODULE_VERSION);
    ap_mpm_query(AP_MPMQ_HARD_LIMIT_THREADS, &thread_limit);
    ap_mpm_query(AP_MPMQ_HARD_LIMIT_DAEMONS, &server_limit);
    return OK;	
}

static void register_hooks(apr_pool_t *p) {
//	ap_hook_handler(m2_handler, NULL, NULL, APR_HOOK_MIDDLE);
//	ap_hook_pre_connection(m2_handler, NULL, NULL, APR_HOOK_MIDDLE);

	ap_hook_post_read_request(m2_handler, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_post_config(m2_init, NULL, NULL, APR_HOOK_MIDDLE);
    ap_log_error(APLOG_MARK, APLOG_INFO, 0, NULL, "%s/%s registered", MODULE_NAME, MODULE_VERSION);
}


module AP_MODULE_DECLARE_DATA m2_module = {
    STANDARD20_MODULE_STUFF,
    m2_create_dir_config,	/* per-directory config creator */
    NULL,					/* dir config merger */
    NULL,					/* server config creator */
    NULL,					/* server config merger */
    m2_cmds,				/* command table */
    register_hooks			/* set up other request processing hooks */
};
