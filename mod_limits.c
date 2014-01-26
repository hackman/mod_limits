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

#ifdef APACHE2
module AP_MODULE_DECLARE_DATA limits_module;
#else
module MODULE_VAR_EXPORT limits_module;
#endif

#ifdef APACHE2
static void *create_dir_config(apr_pool_t *p, char *path) {
	limits_config *limits = (limits_config *) apr_pcalloc(p, sizeof(limits_config));
#else
static void *create_dir_config(pool *p, char *path) {
	limits_config *limits = (limits_config *) ap_pcalloc(p, sizeof(limits_config));
#endif

    // default configuration: no limits 
	limits->ip = 0;
	limits->uid = 0;
	limits->vhost = 0;
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
	int i;
	// current connection count from this address 
	int ip_count = 0;
	// current connections for this vhost
	int vhost_count = 0;
	// scoreboard data structure 
#ifdef APACHE2
	worker_score *ws_record;
	int j;
#else
	short_score score_record;
#endif

	// We decline to handle subrequests: otherwise, in the next step we could get into an infinite loop. 
	if (!ap_is_initial_req(r))
		return DECLINED;
#ifdef APACHE2
	ap_log_error(APLOG_MARK, APLOG_DEBUG, OK, r->server, 
		"mod_limits: current limits IP: %d UID: %d VHost: %d Load: %.2f cAVG: %.2f T: %d",
		limits->ip,
		limits->uid,
		limits->vhost,
		limits->loadavg,
		limits->curavg[0],
		(int) limits->lastavg);
#else
	ap_log_error(APLOG_MARK, APLOG_DEBUG, r->server,
		"mod_limits: current limits IP: %d UID: %d VHost: %d Load: %.2f cAVG: %.2f T: %d",
		limits->ip,
		limits->uid,
		limits->vhost,
		limits->loadavg,
		limits->curavg[0],
		(int) limits->lastavg);
#endif

	// Check the loadavg only if we have any limit set
	if (limits->loadavg != 0.0) {
		// get the current load avg only if it is not updated in
		// the last checkavg seconds
		if (time(NULL) - limits->lastavg > limits->checkavg)
			if ( getloadavg(limits->curavg, 1) > 0 )
				limits->lastavg = time(NULL);
		// decline the request if it is over the defined limit
		if (limits->curavg[0] > limits->loadavg) {
#ifdef APACHE2
			ap_log_error(APLOG_MARK, APLOG_INFO, OK, r->server,
				"mod_limits: %s client rejected because current load %.2f > %.2f",
#ifdef APACHE24
				r->connection->client_ip, limits->curavg[0], limits->loadavg);
#else
				r->connection->remote_ip, limits->curavg[0], limits->loadavg);
#endif // APACHE24
			// set an environment variable 
			apr_table_setn(r->subprocess_env, "LIMITED", "1");
#else
			ap_log_error(APLOG_MARK, APLOG_INFO, r->server,
				"mod_limits: %s client rejected because current load %.2f > %.2f",
				r->connection->remote_ip, limits->curavg[0], limits->loadavg);
			// set an environment variable 
			ap_table_setn(r->subprocess_env, "LIMITED", "1");
#endif // APACHE2
			// return 503 
			return HTTP_SERVICE_UNAVAILABLE;
		}
	}

#ifdef APACHE2
	// Apache 2.x handling here
    for (i = 0; i < server_limit; ++i) {
        for (j = 0; j < thread_limit; ++j) {
			// Count the number of connections from this IP address from the scoreboard 
#ifdef APACHE24
			ws_record = ap_get_scoreboard_worker_from_indexes(i, j);
			if (limits->ip > 0) {
				if (strcmp(r->connection->client_ip, ws_record->client) == 0)
#else
            ws_record = ap_get_scoreboard_worker(i, j);
			if (limits->ip > 0) {
				if (strcmp(r->connection->remote_ip, ws_record->client) == 0)
#endif // APACHE24
					ip_count++;
				if (ip_count > limits->ip) {
					ap_log_error(APLOG_MARK, APLOG_INFO, OK, r->server, 
#ifdef APACHE24
						"mod_limits: %s client exceeded connection limit", r->connection->client_ip);
#else
						"mod_limits: %s client exceeded connection limit", r->connection->remote_ip);
#endif // APACHE24
					// set an environment variable
					apr_table_setn(r->subprocess_env, "LIMITED", "1");
					// return 503
					return HTTP_SERVICE_UNAVAILABLE;
				}
			}

			if (limits->vhost > 0) {
				if (strncmp(r->server->server_hostname, ws_record->vhost, 31) == 0)
					vhost_count++;
				if (vhost_count > limits->vhost) {
					ap_log_error(APLOG_MARK, APLOG_INFO, OK, r->server,
#ifdef APACHE24
						"mod_limits: %s client exceeded vhost connection limit", r->connection->client_ip);
#else
						"mod_limits: %s client exceeded vhost connection limit", r->connection->remote_ip);
#endif // APACHE24
					// set an environment variable
					apr_table_setn(r->subprocess_env, "LIMITED", "1");
					// return 503
					return HTTP_SERVICE_UNAVAILABLE;
				}
			}
		}
	}
		
	ap_log_error(APLOG_MARK, APLOG_DEBUG, OK, r->server,
#ifdef APACHE24
		"mod_limits: %s connection count: %d", r->connection->client_ip, ip_count);
#else
		"mod_limits: %s connection count: %d", r->connection->remote_ip, ip_count);
#endif // APACHE24
#else
	// Apache 1.3 code here

	for (i = 0; i < HARD_SERVER_LIMIT; ++i) {
		score_record = ap_scoreboard_image->servers[i];
		if (limits->ip > 0) {
			// Count the number of connections from this IP address from the scoreboard
			if (strcmp(r->connection->remote_ip, score_record.client) == 0)
				ip_count++;
			if (ip_count > limits->ip) {
				ap_log_error(APLOG_MARK, APLOG_INFO, r->server,
					"mod_limits: %s client exceeded connection limit", r->connection->remote_ip);
				// set an environment variable
				ap_table_setn(r->subprocess_env, "LIMITED", "1");
				// return 503
				return HTTP_SERVICE_UNAVAILABLE;
			}
		}
		if (limits->vhost > 0) {
			if (strcmp(r->server->server_hostname, score_record.vhostrec->server_hostname) == 0)
				vhost_count++;
			if (vhost_count > limits->vhost) {
				ap_log_error(APLOG_MARK, APLOG_INFO, r->server,
					"mod_limits: %s client exceeded vhost connection limit", r->connection->remote_ip);
				// set an environment variable
				ap_table_setn(r->subprocess_env, "LIMITED", "1");
				// return 503
				return HTTP_SERVICE_UNAVAILABLE;
			}
		}
	}

	ap_log_error(APLOG_MARK, APLOG_DEBUG, r->server,
		"mod_limits: %s connection count: %d", r->connection->remote_ip, ip_count);
#endif // APACHE2
	
	return OK;
}

// Parse the LimitMaxConnsPerIP directive
static const char *cfg_perip(cmd_parms *cmd, void *mconfig, const char *arg) {
	limits_config *limits = (limits_config *) mconfig;
	unsigned long int limit = strtol(arg, (char **) NULL, 10);
	if (limit == LONG_MAX)
		return "Integer overflow or invalid number";
	limits->ip = limit;
	return NULL;
}

// Parse the LimitMaxConnsPerVhost directive
static const char *cfg_pervhost(cmd_parms *cmd, void *mconfig, const char *arg) {
	limits_config *limits = (limits_config *) mconfig;
	unsigned long int limit = strtol(arg, (char **) NULL, 10);
	if (limit == LONG_MAX)
		return "Integer overflow or invalid number";
	limits->vhost = limit;
	return NULL;
}

// Parse the LimitMaxConnsPerUID directive
static const char *cfg_peruid(cmd_parms *cmd, void *mconfig, const char *arg) {
	limits_config *limits = (limits_config *) mconfig;
	unsigned long int limit = strtol(arg, (char **) NULL, 10);
	if (limit == LONG_MAX)
		return "Integer overflow or invalid number";
	limits->uid = limit;
	return NULL;
}

// Parse the LimitMaxLoadAVG directive
static const char *cfg_loadavg(cmd_parms *cmd, void *mconfig, const char *arg) {
	limits_config *limits = (limits_config *) mconfig;
	double limit = strtod(arg, (char **) NULL);
	if (limit < 0.0) 
		return "Invalid MaxLoadAVG value";
	limits->loadavg = limit;
	return NULL;
}

// Parse the CheckLoadInterval directive 
static const char *cfg_checkavg(cmd_parms *cmd, void *mconfig, const char *arg) {
	limits_config *limits = (limits_config *) mconfig;
	unsigned long int v = strtol(arg, (char **) NULL, 10);
	if (v == LONG_MAX)
		return "Integer overflow or invalid number";
	limits->checkavg = v;
	return NULL;
}

// Array describing structure of configuration directives 
static command_rec limits_cmds[] = {
#ifdef APACHE2
	AP_INIT_TAKE1(
		"LimitMaxConnsPerIP", cfg_perip, NULL, RSRC_CONF, 
		"maximum simultaneous connections per IP address" ),
	AP_INIT_TAKE1(
		"LimitMaxConnsPerVhost", cfg_pervhost, NULL, RSRC_CONF,
		"maximum simultaneous connections per Vhost" ),
	AP_INIT_TAKE1(
		"LimitMaxConnsPerUid", cfg_peruid, NULL, RSRC_CONF,
		"maximum simultaneous connections per user" ),
	AP_INIT_TAKE1(
		"LimitMaxLoadAVG", cfg_loadavg, NULL, RSRC_CONF,
		"maximum permitted load average" ),
	AP_INIT_TAKE1(
		"CheckLoadInterval", cfg_checkavg, NULL, RSRC_CONF,
		"maximum simultaneous connections per user" ),
#else
	{"LimitMaxConnsPerIP", cfg_perip, NULL, RSRC_CONF, TAKE1,
		"maximum simultaneous connections per IP address" },
	{"LimitMaxConnsPerVhost", cfg_pervhost, NULL, RSRC_CONF, TAKE1,
		"maximum simultaneous connections per vhost" },
	{"LimitMaxConnsPerUid", cfg_peruid, NULL, RSRC_CONF, TAKE1,
		"maximum simultaneous connections per user" },
	{ "LimitMaxLoadAVG", cfg_loadavg, NULL, RSRC_CONF, TAKE1,
		"maximum permitted load average" },
	{ "CheckLoadInterval", cfg_checkavg, NULL, RSRC_CONF, TAKE1,
		"maximum simultaneous connections per user" },
#endif
	{NULL}
};

#ifdef APACHE2
// Emit an informational-level log message on startup and init the thread_limit and server_limit  
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
    create_dir_config,	// per-directory config creator
    NULL,				// dir config merger
    NULL,				// server config creator
    NULL,				// server config merger
    limits_cmds,		// command table
    register_hooks		// set up other request processing hooks
};
#else
module MODULE_VAR_EXPORT limits_module = {
	STANDARD_MODULE_STUFF,
	NULL,				// initializer
	create_dir_config,	// dir config creater
	NULL,				// dir merger --- default is to override
	NULL,				// server config
	NULL,				// merge server config
	limits_cmds,		// command table
	NULL,				// handlers
	NULL,				// filename translation
	NULL,				// check_user_id
	NULL,				// check auth
	limits_handler,		// check access
	NULL,				// type_checker
	NULL,				// fixups
	NULL,				// logger
	NULL,				// header parser
	NULL,				// child_init
	NULL,				// child_exit
	NULL 				// post read-request
};
#endif // APACHE2
