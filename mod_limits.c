/*
 * Copyright (C) 2010-2011 Marian Marinov <mm@yuhu.biz>
 * 
 * This code is based on the work of David Jao <djao@dominia.org> and
 * Maxim Chirkov <mc@tyumen.ru>, who made mod_limitipconn for Apache 1.3
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "mod_limits.h"
module AP_MODULE_DECLARE_DATA limits_module;


static void *create_dir_config(apr_pool_t *p, char *path) {
	limits_config *limits = (limits_config *) apr_pcalloc(p, sizeof(limits_config));

    /* default configuration: no limits */
	limits->ip = 0;
	limits->uid = 0;
	limits->loadavg = 0;
	limits->checkavg = 5;

	if ( getloadavg(limits->curavg, 1) > 0 )
		limits->lastavg = time(NULL);

    return (void *) limits;
}

static int limits_handler(request_rec *r) {
	// get configuration information 
	limits_config *limits = (limits_config *) ap_get_module_config(r->per_dir_config, &limits_module);
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
		"current limits IP: %d UID: %d Load: %.2f cAVG: %.2f T: %d",
		limits->ip,
		limits->uid,
		limits->loadavg,
		limits->curavg[0],
		limits->lastavg);

	// Check the loadavg only if we have any limit set
	if (limits->loadavg != 0.0) {
		// get the current load avg only if it is not updated in
		// the last checkavg seconds
		if (time(NULL) - limits->lastavg > limits->checkavg)
			if ( getloadavg(limits->curavg, 1) > 0 )
				limits->lastavg = time(NULL);
		// decline the request if it is over the defined limit
		if (limits->curavg[0] > limits->loadavg) {
			ap_log_error(APLOG_MARK, APLOG_INFO, OK, r->server,
				"%s client rejected because current load %.2f > %.2f",
				r->connection->remote_ip, limits->curavg[0], limits->loadavg);
			/* set an environment variable */
			apr_table_setn(r->subprocess_env, "LIMITED", "1");
			/* return 503 */
			return HTTP_SERVICE_UNAVAILABLE;
		}
	}

    // Check the ipcount only if we have any limit set
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
				apr_table_setn(r->subprocess_env, "LIMITED", "1");
				/* return 503 */
				return HTTP_SERVICE_UNAVAILABLE;
			}
		}
	}
		
	ap_log_error(APLOG_MARK, APLOG_DEBUG, OK, r->server,
		"%s connection count: %d", r->connection->remote_ip, ip_count);
	
	return OK;
}

/* Parse the MaxConnsPerIP directive */
static const char *cfg_perip(cmd_parms *cmd, void *mconfig, const char *arg) {
	limits_config *limits = (limits_config *) mconfig;
	unsigned long int limit = strtol(arg, (char **) NULL, 10);
	if (limit == LONG_MAX) 
		return "Integer overflow or invalid number";
	limits->ip = limit;
	return NULL;
}

/* Parse the MaxConnsPerUID directive */
static const char *cfg_peruid(cmd_parms *cmd, void *mconfig, const char *arg) {
	limits_config *limits = (limits_config *) mconfig;
	unsigned long int limit = strtol(arg, (char **) NULL, 10);
	if (limit == LONG_MAX)
		return "Integer overflow or invalid number";
	limits->uid = limit;
	return NULL;
}

/* Parse the MaxLoadAVG directive */
static const char *cfg_loadavg(cmd_parms *cmd, void *mconfig, const char *arg) {
	limits_config *limits = (limits_config *) mconfig;
	double limit = strtod(arg, (char **) NULL);
	if (limit < 0.0) 
		return "Invalid MaxLoadAVG value";
	limits->loadavg = limit;
	return NULL;
}

/* Parse the CheckLoadAvg directive */
static const char *cfg_checkavg(cmd_parms *cmd, void *mconfig, const char *arg) {
	limits_config *limits = (limits_config *) mconfig;
	unsigned long int v = strtol(arg, (char **) NULL, 10);
	if (v == LONG_MAX)
		return "Integer overflow or invalid number";
	limits->checkavg = v;
	return NULL;
}

/* Array describing structure of configuration directives */
static command_rec limits_cmds[] = {
	AP_INIT_TAKE1(
		"MaxConnsPerIP", cfg_perip, NULL, RSRC_CONF, 
		"maximum simultaneous connections per IP address" ),
	AP_INIT_TAKE1(
		"MaxConnsPerUid", cfg_peruid, NULL, RSRC_CONF,
		"maximum simultaneous connections per user" ),
	AP_INIT_TAKE1(
		"MaxLoadAVG", cfg_loadavg, NULL, RSRC_CONF,
		"maximum permitted load average" ),
	AP_INIT_TAKE1(
		"CheckLoadAvg", cfg_checkavg, NULL, RSRC_CONF,
		"maximum simultaneous connections per user" ),
	{NULL}
};

/* Emit an informational-level log message on startup and init the thread_limit and server_limit  */
static int limits_init(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s) {
    ap_log_error(APLOG_MARK, APLOG_INFO, OK, s, "%s/%s loaded", MODULE_NAME, MODULE_VERSION);
    ap_mpm_query(AP_MPMQ_HARD_LIMIT_THREADS, &thread_limit);
    ap_mpm_query(AP_MPMQ_HARD_LIMIT_DAEMONS, &server_limit);
    return OK;	
}

static void register_hooks(apr_pool_t *p) {
//	static const char * const after_me[] = { "mod_cache.c", NULL };
//	ap_hook_quick_handler(limits_handler, NULL, after_me, APR_HOOK_FIRST);
	ap_hook_post_read_request(limits_handler, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_post_config(limits_init, NULL, NULL, APR_HOOK_MIDDLE);
}


module AP_MODULE_DECLARE_DATA limits_module = {
    STANDARD20_MODULE_STUFF,
    create_dir_config,	/* per-directory config creator */
    NULL,				/* dir config merger */
    NULL,				/* server config creator */
    NULL,				/* server config merger */
    limits_cmds,		/* command table */
    register_hooks		/* set up other request processing hooks */
};
