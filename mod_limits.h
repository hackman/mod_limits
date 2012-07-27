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

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "http_request.h"
#include "util_script.h"
#include "scoreboard.h"

#define MODULE_NAME "mod_limits"
#define MODULE_VERSION "0.06"

#ifndef APACHE_RELEASE
#define APACHE2
#include "http_connection.h"
#include "ap_mpm.h"
#include "apr_strings.h"
#endif

#if defined(AP_SERVER_MAJORVERSION_NUMBER) && AP_SERVER_MAJORVERSION_NUMBER == 2 && defined(AP_SERVER_MINORVERSION_NUMBER) && AP_SERVER_MINORVERSION_NUMBER >= 4
#define APACHE24
#endif


static int server_limit, thread_limit;

typedef struct {
    unsigned int ip;	/* max number of connections per IP */
    unsigned int uid;	/* max number of connections per UID */
	double loadavg; 	/* max load average */
	double curavg[1];	/* current load average */
	time_t lastavg;		/* last time we updated the load average */
	unsigned int checkavg; /* how often we will check the load average */
} limits_config;
