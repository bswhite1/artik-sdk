/****************************************************************************
 * webclient.c
 * Implementation of the HTTP client.
 *
 *   Copyright (C) 2007, 2009, 2011-2012, 2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Based on uIP which also has a BSD style license:
 *
 *   Author: Adam Dunkels <adam@dunkels.com>
 *   Copyright (c) 2002, Adam Dunkels.
 *   All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/* This example shows a HTTP client that is able to download web pages
 * and files from web servers. It requires a number of callback
 * functions to be implemented by the module that utilizes the code:
 * webclient_datahandler().
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#ifndef CONFIG_WEBCLIENT_HOST
#  include <tinyara/config.h>
#  include <tinyara/compiler.h>
#  include <debug.h>
#endif

#include <sys/socket.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <tinyara/version.h>

#include <apps/netutils/netlib.h>
#include "tizenrt_http.h"
#include "artik_log.h"

#if defined(CONFIG_NETUTILS_CODECS)
#  if defined(CONFIG_CODECS_URLCODE)
#    define WGET_USE_URLENCODE 1
#    include "netutils/urldecode.h"
#  endif
#  if defined(CONFIG_CODECS_BASE64)
#    include "netutils/base64.h"
#  endif
#else
#  undef CONFIG_CODECS_URLCODE
#  undef CONFIG_CODECS_BASE64
#endif

#ifdef CONFIG_NET_SECURITY_TLS
#include "tls/config.h"
#include "tls/entropy.h"
#include "tls/ctr_drbg.h"
#include "tls/certs.h"
#include "tls/x509.h"
#include "tls/ssl.h"
#include "tls/net.h"
#include "tls/error.h"
#include "tls/debug.h"
#include "tls/ssl_cache.h"
#endif

#ifndef CONFIG_NSH_WGET_USERAGENT
#  if CONFIG_VERSION_MAJOR != 0 || CONFIG_VERSION_MINOR != 0
#    define CONFIG_NSH_WGET_USERAGENT \
"TizenRT/" CONFIG_VERSION_STRING " (; http:/www.tizen.org/)"
#  else
#    define CONFIG_NSH_WGET_USERAGENT \
"TizenRT/6.xx.x (; http://www.tizen.org/)"
#  endif
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CONFIG_WEBCLIENT_MAXHTTPLINE 600
//#define CONFIG_WEBCLIENT_MAXMIMESIZE 32
#define CONFIG_WEBCLIENT_MAXHOSTNAME 40
#define CONFIG_WEBCLIENT_MAXFILENAME 513

#ifndef CONFIG_WEBCLIENT_TIMEOUT
#  define CONFIG_WEBCLIENT_TIMEOUT 10
#endif

#define WEBCLIENT_CONF_HANDSHAKE_RETRY    3

#define WEBCLIENT_STATE_STATUSLINE 0
#define WEBCLIENT_STATE_HEADERS    1
#define WEBCLIENT_STATE_DATA       2
#define WEBCLIENT_STATE_CLOSE      3

#define HTTPSTATUS_NONE            0
#define HTTPSTATUS_OK              1
#define HTTPSTATUS_MOVED           2
#define HTTPSTATUS_ERROR           3

#define ISO_nl                     0x0a
#define ISO_cr                     0x0d
#define ISO_space                  0x20

#define WGET_MODE_GET              0
#define WGET_MODE_POST             1

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct wget_s {
  /* Internal status */

	uint8_t state;
	uint16_t httpstatus;

	uint16_t port; /* The port number to use in the connection */

	/* These describe the just-received buffer of data */

	/* user-provided buffer */
	FAR char *buffer;
	/* Length of the user provided buffer */
	int buflen;
	/* Offset to the beginning of interesting data */
	int offset;
	/* Offset+1 to the last valid byte of data in the buffer */
	int datend;

	/* Buffer HTTP header data and parse line at a time */

	char line[CONFIG_WEBCLIENT_MAXHTTPLINE];
	int  ndx;

#ifdef CONFIG_WEBCLIENT_GETMIMETYPE
	char mimetype[CONFIG_WEBCLIENT_MAXMIMESIZE];
#endif
	char hostname[CONFIG_WEBCLIENT_MAXHOSTNAME];
	char filename[CONFIG_WEBCLIENT_MAXFILENAME];

	ssize_t (*recv)(void *conn, unsigned char *buffer, int buflen);
	ssize_t (*send)(void *conn, unsigned char *buffer, int buflen);
	void (*close)(void *conn);
	void *conn;
};


struct wget_request {
	FAR const char *url;
	FAR char *buffer;
	int buflen;
	wget_callback_stream_t callback;
	FAR void *user_data;
	FAR const char *posts;
	uint8_t mode;
	int with_tls;
	void *tls_conf;
	int status;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char g_http10[]          = "HTTP/1.0";
static const char g_http11[]          = "HTTP/1.1";
#ifdef CONFIG_WEBCLIENT_GETMIMETYPE
static const char g_httpcontenttype[] = "content-type: ";
#endif
static const char g_httphost[]        = "host: ";
static const char g_httplocation[]    = "location: ";
static const char g_httpget[]         = "GET ";
static const char g_httppost[]        = "POST ";

static const char g_httpuseragentfields[] =
	"Connection: close\r\n"
	"User-Agent: "
	CONFIG_NSH_WGET_USERAGENT
	"\r\n\r\n";

static const char g_httpcrnl[]        = "\r\n";

static const char g_httpform[]        = "Content-Type: application/x-www-form-urlencoded";
static const char g_httpcontsize[]    = "Content-Length: ";
//static const char g_httpconn[]      = "Connection: Keep-Alive";
//static const char g_httpcache[]     = "Cache-Control: no-cache";

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: wget_strcpy
 ****************************************************************************/

static char *wget_strcpy(char *dest, const char *src)
{
	int len = strlen(src);

	memcpy(dest, src, len);
	dest[len] = '\0';
	return dest + len;
}

/****************************************************************************
 * Name: wget_urlencode_strcpy
 ****************************************************************************/

#ifdef WGET_USE_URLENCODE
static char *wget_urlencode_strcpy(char *dest, const char *src)
{
	int len = strlen(src);
	int d_len;

	d_len = urlencode_len(src, len);
	urlencode(src, len, dest, &d_len);
	return dest + d_len;
}
#endif

/****************************************************************************
 * Name: wget_parsestatus
 ****************************************************************************/

static inline int wget_parsestatus(struct wget_s *ws)
{
	char *dest;
	char status_code[4] = {0, };
	int offset = ws->offset;
	int ndx    = ws->ndx;

	while (offset < ws->datend) {
		ws->line[ndx] = ws->buffer[offset];
		if (ws->line[ndx] == ISO_nl) {
			ws->line[ndx] = '\0';
			if ((strncmp(ws->line, g_http10,
				strlen(g_http10)) == 0) || (strncmp(ws->line,
				g_http11, strlen(g_http11)) == 0)) {
				dest = &(ws->line[9]);
				ws->httpstatus = HTTPSTATUS_NONE;

				strncpy(status_code, dest, 4);
				ws->httpstatus = atoi(status_code);
			} else
				return -ECONNABORTED;

			/* We're done parsing the status line, so start parsing
			 * the HTTP headers.
			 */

			ws->state = WEBCLIENT_STATE_HEADERS;
			break;
		}

		offset++;
		ndx++;
	}

	ws->offset = offset;
	ws->ndx    = ndx;
	return OK;
}

/****************************************************************************
 * Name: wget_parseheaders
 ****************************************************************************/

static inline int wget_parseheaders(struct wget_s *ws)
{
	int offset = ws->offset;
	int ndx    = ws->ndx;

	while (offset < ws->datend) {
		ws->line[ndx] = ws->buffer[offset];
		if (ws->line[ndx] == ISO_nl) {
			/* We have an entire HTTP header line in s.line, so
			 * we parse it.
			 */

			 /* Should always be true */
			if (ndx > 0) {
				if (ws->line[0] == ISO_cr) {
				/* This was the last header line (i.e.,
				 * and empty "\r\n"), so we are done with the
				 * headers and proceed with the actual
				 * data.
				 */

					ws->state = WEBCLIENT_STATE_DATA;
					goto exit;
				}

				/* Truncate the trailing \r\n */

				ws->line[ndx-1] = '\0';

				/* Check for specific HTTP header fields. */

#ifdef CONFIG_WEBCLIENT_GETMIMETYPE
				if (strncasecmp(ws->line, g_httpcontenttype,
					strlen(g_httpcontenttype)) == 0) {
					/* Found Content-type field. */

					char *dest = strchr(ws->line, ';');

					if (dest != NULL)
						*dest = 0;

					strncpy(ws->mimetype, ws->line +
						strlen(g_httpcontenttype),
						sizeof(ws->mimetype));
				} else
#endif
					if (strncasecmp(ws->line,
						g_httplocation,
						strlen(g_httplocation)) == 0) {
						/* Parse the new HTTP host and
						 * filename from the URL. Note
						 * that the return value is
						 * ignored. In the event of
						 * failure, we retain the
						 * current location.
						 */

						(void)netlib_parsehttpurl(
						ws->line +
						strlen(g_httplocation),
						&ws->port, ws->hostname,
						CONFIG_WEBCLIENT_MAXHOSTNAME,
						ws->filename,
						CONFIG_WEBCLIENT_MAXFILENAME);

						log_dbg("New hostname='%s' filename='%s'\n",
							ws->hostname,
							ws->filename);
					}
				}

				/* We're done parsing this line,
				 * so we reset the index to the start
				 * of the next line.
				 */

				ndx = 0;
			} else
				ndx++;

			offset++;
		}

exit:
		ws->offset = ++offset;
		ws->ndx    = ndx;
		return OK;
	}

/****************************************************************************
 * Name: wget_gethostip
 *
 * Description:
 *   Call gethostbyname() to get the IPv4 address associated with a hostname.
 *
 * Input Parameters
 *   hostname - The host name to use in the nslookup.
 *   ipv4addr - The location to return the IPv4 address.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int wget_gethostip(FAR char *hostname, in_addr_t *ipv4addr)
{
	FAR struct hostent *he;

	he = gethostbyname(hostname);
	if (he == NULL) {
		log_dbg("WARNING: gethostbyname failed: %d\n", h_errno);
		return -ENOENT;
	} else if (he->h_addrtype != AF_INET) {
		log_dbg("WARNING: gethostbyname returned an address of type: %d\n",
				he->h_addrtype);
		return -ENOEXEC;
	}

	memcpy(ipv4addr, he->h_addr, sizeof(in_addr_t));
	return OK;
}

/****************************************************************************
 * Name: wget_base
 *
 * Description:
 *   Obtain the requested file from an HTTP server using the GET method.
 *
 *   Note: If the function is passed a host name, it must already be in
 *   the resolver cache in order for the function to connect to the web
 *   server. It is therefore up to the calling module to implement the
 *   resolver calls and the signal handler used for reporting a resolv
 *   query answer.
 *
 * Input Parameters
 *   url      - A pointer to a string containing either the full URL to
 *              the file to get (e.g., http://www.nutt.org/index.html, or
 *              http://192.168.23.1:80/index.html).
 *   buffer   - A user provided buffer to receive the file data (also
 *              used for the outgoing GET request
 *   buflen   - The size of the user provided buffer
 *   callback - As data is obtained from the host, this function is
 *              to dispose of each block of file data as it is received.
 *   mode     - Indicates GET or POST modes
 *
 * Returned Value:
 *   0: if the GET operation completed successfully;
 *  -1: On a failure with errno set appropriately
 *
 ****************************************************************************/

#ifdef CONFIG_NET_SECURITY_TLS
struct wget_tls_t {
	mbedtls_ssl_context tls_ssl;
	mbedtls_net_context tls_net;
	mbedtls_ssl_config *tls_conf;
};

static int wget_tls_handshake(struct wget_tls_t *tls,
				const char *hostname, int sockfd)
{
	int result = 0;

	mbedtls_net_init(&tls->tls_net);
	mbedtls_ssl_init(&tls->tls_ssl);

	tls->tls_net.fd = sockfd;

	if (mbedtls_net_set_block(&tls->tls_net) < 0) {
		log_dbg("Error: mbedtls_net_set_block fail\n");
		goto HANDSHAKE_FAIL;
	}

	log_dbg("TLS Init Success\n");

	if (mbedtls_ssl_setup(&tls->tls_ssl, tls->tls_conf) != 0) {
		log_dbg("Error: mbedtls_ssl_setup returned %d\n", result);
		goto HANDSHAKE_FAIL;
	}

	/*
	 * Server name intication is an extension to the TLS networking
	 * protocol.
	 * If server presents multiple certificates on the same IP
	 * address, client could make multiple secure session depends
	 * on hostname.
	 *
	 * Note : Hostname in TLS is a subject's common name(CN) of
	 * certificates.
	 */

	if (mbedtls_ssl_set_hostname(&(tls->tls_ssl), hostname) != 0) {
		log_dbg("Error: mbedtls_hostname fail %d\n", result);
		goto HANDSHAKE_FAIL;
	}

	mbedtls_ssl_set_bio(&(tls->tls_ssl), &(tls->tls_net),
	mbedtls_net_send,
	mbedtls_net_recv, NULL);

	/* Handshake */
	while ((result = mbedtls_ssl_handshake(&(tls->tls_ssl))) != 0) {
		if (result != MBEDTLS_ERR_SSL_WANT_READ &&
		result != MBEDTLS_ERR_SSL_WANT_WRITE) {
			log_dbg("Error: TLS Handshake fail returned %d\n",
			result);
			goto HANDSHAKE_FAIL;
		}
	}

	log_dbg("TLS Handshake Success\n");

	return 0;

HANDSHAKE_FAIL:
	return result;
}

static void wget_tls_ssl_release(struct wget_tls_t *tls)
{
	mbedtls_net_free(&tls->tls_net);
	mbedtls_ssl_free(&tls->tls_ssl);
}

static ssize_t ssl_recv(void *conn, unsigned char *buffer, int buflen)
{
	struct wget_tls_t *tls = (struct wget_tls_t *)conn;
	int ret = mbedtls_ssl_read(&tls->tls_ssl, buffer, buflen);

	if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
			ret == MBEDTLS_ERR_SSL_CONN_EOF) {
		return 0;
	}

	return ret;
}

static ssize_t ssl_send(void *conn, unsigned char *buffer, int buflen)
{
	struct wget_tls_t *tls = (struct wget_tls_t *)conn;

	return mbedtls_ssl_write(&tls->tls_ssl, buffer, buflen);
}

static void ssl_close(void *conn)
{
	struct wget_tls_t *tls = (struct wget_tls_t *)conn;

	wget_tls_ssl_release(tls);
}

#endif

static ssize_t raw_recv(void *conn, unsigned char *buffer, int buflen)
{
	int sockfd = (int)conn;

	return recv(sockfd, buffer, buflen, 0);
}

static ssize_t raw_send(void *conn, unsigned char *buffer, int buflen)
{
	int sockfd = (int)conn;

	return send(sockfd, buffer, buflen, 0);
}

static void raw_close(void *conn)
{
	int sockfd = (int)conn;

	close(sockfd);
}

static int wget_connect(struct wget_s *ws, int tls, void *tls_data)
{
#ifdef CONFIG_NET_SECURITY_TLS
	int handshake_retry = WEBCLIENT_CONF_HANDSHAKE_RETRY;
	struct wget_tls_t *client_tls = tls_data;
#endif
	struct sockaddr_in server;
	struct timeval tv;
	int sockfd;
	int retry;
	int ret;

	do {
		retry = 0;
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) {
			/* socket failed.  It will set the errno
			 * appropriately
			 */
			log_dbg("ERROR: socket failed: %d\n", errno);
			return ERROR;
		}

		/* Set send and receive timeout values */

		tv.tv_sec  = CONFIG_WEBCLIENT_TIMEOUT;
		tv.tv_usec = 0;

		(void)setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
			(FAR const void *)&tv,
			sizeof(struct timeval));
		(void)setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO,
			(FAR const void *)&tv,
			sizeof(struct timeval));

		/* Get the server address from the host name */

		server.sin_family = AF_INET;
		server.sin_port   = htons(ws->port);
		ret = wget_gethostip(ws->hostname, &server.sin_addr.s_addr);
		if (ret < 0) {
			/* Could not resolve host (or malformed IP address) */

			log_dbg("WARNING: Failed to resolve hostname\n");
			ret = -EHOSTUNREACH;
			close(sockfd);
			goto errout_with_errno;
		}

		/* Connect to server.  First we have to set some fields in the
		 * 'server' address structure.  The system will assign me an
		 * arbitrary local port that is not in use.
		 */
		ret = connect(sockfd, (struct sockaddr *)&server,
			sizeof(struct sockaddr_in));
		if (ret < 0) {
			log_dbg("ERROR: connect failed: %d\n", errno);
			close(sockfd);
			goto errout;
		}

#ifdef CONFIG_NET_SECURITY_TLS
		if (tls) {
			ret = wget_tls_handshake(client_tls, ws->hostname,
				sockfd);
			if (ret != 0) {
				retry = handshake_retry > 0;
				wget_tls_ssl_release(client_tls);
				if (retry &&
					(ret == MBEDTLS_ERR_NET_SEND_FAILED
						|| ret ==
						MBEDTLS_ERR_NET_RECV_FAILED
						|| ret ==
						MBEDTLS_ERR_SSL_CONN_EOF)) {
					handshake_retry--;
					log_dbg("Handshake again...\n");
					continue;
				}
				log_dbg("TLS Handshake failed with %d\n", ret);
				goto errout;
			}
		}
#endif
	} while (retry);

	ws->recv = raw_recv;
	ws->send = raw_send;
	ws->close = raw_close;
	ws->conn = (void *)sockfd;

#ifdef CONFIG_NET_SECURITY_TLS
	if (tls) {
		ws->recv = ssl_recv;
		ws->send = ssl_send;
		ws->close = ssl_close;
		ws->conn = &client_tls->tls_ssl;
	}
#endif
	return 0;
errout_with_errno:
	set_errno(-ret);
errout:

	return ret;
}

static int wget_base(FAR struct wget_request *request)
{
	struct wget_s ws;
#ifdef CONFIG_NET_SECURITY_TLS
	struct wget_tls_t tls;
#endif
	bool redirected;
	char *dest, post_size[8];
	int len, post_len;
	int ret;

	/* Initialize the state structure */

	memset(&ws, 0, sizeof(struct wget_s));
	ws.buffer = request->buffer;
	ws.buflen = request->buflen;
	ws.port   = 80;
	tls.tls_conf = request->tls_conf;

	/* Parse the hostname (with optional port number) and filename
	 * from the URL
	 */

	ret = netlib_parsehttpurl(request->url, &ws.port,
		ws.hostname, CONFIG_WEBCLIENT_MAXHOSTNAME,
		ws.filename, CONFIG_WEBCLIENT_MAXFILENAME);
	if (ret != 0) {
		log_dbg("WARNING: Malformed HTTP URL: %s\n", request->url);
		set_errno(-ret);
		return ERROR;
	}

	log_dbg("with_tls='%d', hostname='%s' filename='%s'\n", request->with_tls,
		ws.hostname, ws.filename);

	/* The following sequence may repeat indefinitely if we are redirected
	 */

	do {
		/* Re-initialize portions of the state structure that could
		 * have been left from the previous time through the loop and
		 * should not persist with the new connection.
		 */

		ws.httpstatus = HTTPSTATUS_NONE;
		ws.offset     = 0;
		ws.datend     = 0;
		ws.ndx        = 0;

		/* Create a socket */
		ret = wget_connect(&ws, request->with_tls, &tls);
		if (ret != 0) {
			log_dbg("ERROR: connection failed\n");
			return ERROR;
		}

		/* Send the GET request */
		dest = ws.buffer;
		if (request->mode == WGET_MODE_POST)
			dest = wget_strcpy(dest, g_httppost);
		else
			dest = wget_strcpy(dest, g_httpget);

#ifndef WGET_USE_URLENCODE
		dest = wget_strcpy(dest, ws.filename);
#else
		//dest = wget_urlencode_strcpy(dest, ws.filename);
		dest = wget_strcpy(dest, ws.filename);
#endif

		*dest++ = ISO_space;
		dest = wget_strcpy(dest, g_http10);
		dest = wget_strcpy(dest, g_httpcrnl);
		dest = wget_strcpy(dest, g_httphost);
		dest = wget_strcpy(dest, ws.hostname);
		dest = wget_strcpy(dest, g_httpcrnl);

		if (request->mode == WGET_MODE_POST) {
			dest = wget_strcpy(dest, g_httpform);
			dest = wget_strcpy(dest, g_httpcrnl);
			dest = wget_strcpy(dest, g_httpcontsize);

			/* Post content size */

			post_len = strlen((char *)request->posts);
			sprintf(post_size, "%d", post_len);
			dest = wget_strcpy(dest, post_size);
			dest = wget_strcpy(dest, g_httpcrnl);
		}

		dest = wget_strcpy(dest, g_httpuseragentfields);
		if (request->mode == WGET_MODE_POST)
			dest = wget_strcpy(dest, (char *)request->posts);

		len = dest - ws.buffer;

		ret = ws.send(ws.conn, (unsigned char *)ws.buffer, len);
		if (ret < 0) {
			log_dbg("ERROR: send failed: %d\n", errno);
			goto errout;
		}

		/* Now loop to get the file sent in response to the GET. This
		 * loop continues until either we read the end of file
		 * (nbytes == 0) or until we detect that we have been
		 * redirected.
		 */

		ws.state   = WEBCLIENT_STATE_STATUSLINE;
		redirected = false;
		for (;;) {
			ws.datend = ws.recv(ws.conn, (unsigned char *)ws.buffer,
								ws.buflen);
			if (ws.datend < 0) {
				log_dbg("ERROR: recv failed: %d\n", errno);
				ret = ws.datend;
				goto errout_with_errno;
			} else if (ws.datend == 0) {
				log_dbg("Connection lost\n");
				ws.close(ws.conn);
				break;
			}

			/* Handle initial parsing of the status line */
			ws.offset = 0;
			if (ws.state == WEBCLIENT_STATE_STATUSLINE) {
				ret = wget_parsestatus(&ws);
				if (ret < 0) {
					log_err("Parse status failed [%d]\n",
						ret);
					goto errout_with_errno;
				}
			}

			if (ws.state == WEBCLIENT_STATE_HEADERS) {
				ret = wget_parseheaders(&ws);
				if (ret < 0) {
					log_err("Parse headers failed [%d]\n",
						ret);
					goto errout_with_errno;
				}
			}

			/* Dispose of the data payload */
			if (ws.state == WEBCLIENT_STATE_DATA) {
				if ((ws.httpstatus != 301) && (ws.httpstatus != 302)) {
				/* Let the client decide what to do with the
				 * received file
				 */
					request->callback(&ws.buffer, ws.offset,
						ws.datend, &request->buflen, request->user_data);
				} else {
					redirected = true;
					ws.close(ws.conn);
					break;
				}
			}
		}
	} while (redirected);
	request->status = ws.httpstatus;

	return OK;

errout_with_errno:
	set_errno(-ret);
errout:
	if (ws.conn)
		ws.close(ws.conn);
	return ERROR;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: web_post_str
 ****************************************************************************/

#ifdef WGET_USE_URLENCODE
char *web_post_str(FAR char *buffer, int *size, FAR char *name,
	FAR char *value)
{
	char *dst = buffer;

	buffer = wget_strcpy(buffer, name);
	buffer = wget_strcpy(buffer, "=");
	buffer = wget_urlencode_strcpy(buffer, value);
	*size  = buffer - dst;
	return dst;
}
#endif

/****************************************************************************
 * Name: web_post_strlen
 ****************************************************************************/

#ifdef WGET_USE_URLENCODE
int web_post_strlen(FAR char *name, FAR char *value)
{
	return strlen(name) + urlencode_len(value, strlen(value)) + 1;
}
#endif

/****************************************************************************
 * Name: web_posts_str
 ****************************************************************************/

#ifdef WGET_USE_URLENCODE
char *web_posts_str(FAR char *buffer, int *size, FAR char **name,
	FAR char **value, int len)
{
	char *dst = buffer;
	int wlen;
	int i;

	for (i = 0; i < len; i++) {
		if (i > 0)
			buffer = wget_strcpy(buffer, "&");

		wlen    = *size;
		buffer  = web_post_str(buffer, &wlen, name[i], value[i]);
		buffer += wlen;
	}

	*size = buffer-dst;
	return dst;
}
#endif

/****************************************************************************
 * Name: web_posts_strlen
 ****************************************************************************/

#ifdef WGET_USE_URLENCODE
int web_posts_strlen(FAR char **name, FAR char **value, int len)
{
	int wlen = 0;
	int i;

	for (i = 0; i < len; i++)
		wlen += web_post_strlen(name[i], value[i]);

	return wlen + len - 1;
}
#endif

/****************************************************************************
 * Name: wget
 *
 * Description:
 *   Obtain the requested file from an HTTP server using the GET method.
 *
 *   Note: If the function is passed a host name, it must already be in
 *   the resolver cache in order for the function to connect to the web
 *   server. It is therefore up to the calling module to implement the
 *   resolver calls and the signal handler used for reporting a resolv
 *   query answer.
 *
 * Input Parameters
 *   url      - A pointer to a string containing either the full URL to
 *              the file to get (e.g., http://www.nutt.org/index.html, or
 *              http://192.168.23.1:80/index.html).
 *   buffer   - A user provided buffer to receive the file data (also
 *              used for the outgoing GET request
 *   buflen   - The size of the user provided buffer
 *   callback - As data is obtained from the host, this function is
 *              to dispose of each block of file data as it is received.
 *
 * Returned Value:
 *   0: if the GET operation completed successfully;
 *  -1: On a failure with errno set appropriately
 *
 ****************************************************************************/

int wget(FAR const char *url, int *status, FAR char *buffer, int buflen,
	wget_callback_stream_t callback, FAR void *arg, int with_tls,
	void *tls_conf)
{
	struct wget_request req = { url, buffer, buflen, callback, arg, NULL,
		WGET_MODE_GET, with_tls, tls_conf, 0 };
	int ret = wget_base(&req);

	if (status)
		*status = req.status;

	return ret;
}

/****************************************************************************
 * Name: wget_post
 ****************************************************************************/

int wget_post(FAR const char *url, FAR const char *posts, FAR int *status,
	FAR char *buffer, int buflen, wget_callback_stream_t callback,
	FAR void *arg, int with_tls, void *tls_conf)
{
	struct wget_request req = {url, buffer, buflen, callback, arg, posts,
		WGET_MODE_POST, with_tls, tls_conf, 0};
	int ret = wget_base(&req);

	if (status)
		*status = req.status;

	return ret;
}
