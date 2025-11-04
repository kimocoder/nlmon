/* syslog_forwarder.c - Syslog forwarding with RFC 5424 support */

#include "syslog_forwarder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>

#ifdef ENABLE_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#define MAX_SYSLOG_MSG_SIZE 8192
#define MAX_HOSTNAME_SIZE 256

struct syslog_forwarder {
	struct syslog_config config;
	int sock;
	bool connected;
	char hostname[MAX_HOSTNAME_SIZE];
	
#ifdef ENABLE_TLS
	SSL_CTX *ssl_ctx;
	SSL *ssl;
#endif
	
	/* Statistics */
	uint64_t messages_sent;
	uint64_t messages_failed;
	uint32_t reconnections;
	
	pthread_mutex_t lock;
};

static bool get_hostname(char *hostname, size_t size)
{
	if (gethostname(hostname, size) != 0)
		return false;
	
	hostname[size - 1] = '\0';
	return true;
}

static void format_rfc5424_timestamp(char *buffer, size_t size)
{
	struct timespec ts;
	struct tm tm;
	
	clock_gettime(CLOCK_REALTIME, &ts);
	gmtime_r(&ts.tv_sec, &tm);
	
	snprintf(buffer, size, "%04d-%02d-%02dT%02d:%02d:%02d.%06ldZ",
	        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	        tm.tm_hour, tm.tm_min, tm.tm_sec,
	        ts.tv_nsec / 1000);
}

static bool connect_tcp(struct syslog_forwarder *forwarder)
{
	struct addrinfo hints, *result, *rp;
	char port_str[16];
	int sock = -1;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	snprintf(port_str, sizeof(port_str), "%u", forwarder->config.port);
	
	if (getaddrinfo(forwarder->config.server, port_str, &hints, &result) != 0)
		return false;
	
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock == -1)
			continue;
		
		if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		
		close(sock);
		sock = -1;
	}
	
	freeaddrinfo(result);
	
	if (sock == -1)
		return false;
	
	forwarder->sock = sock;
	forwarder->connected = true;
	
	return true;
}

static bool connect_udp(struct syslog_forwarder *forwarder)
{
	struct addrinfo hints, *result;
	char port_str[16];
	int sock;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	
	snprintf(port_str, sizeof(port_str), "%u", forwarder->config.port);
	
	if (getaddrinfo(forwarder->config.server, port_str, &hints, &result) != 0)
		return false;
	
	sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (sock == -1) {
		freeaddrinfo(result);
		return false;
	}
	
	if (connect(sock, result->ai_addr, result->ai_addrlen) != 0) {
		close(sock);
		freeaddrinfo(result);
		return false;
	}
	
	freeaddrinfo(result);
	
	forwarder->sock = sock;
	forwarder->connected = true;
	
	return true;
}

#ifdef ENABLE_TLS
static bool connect_tls(struct syslog_forwarder *forwarder)
{
	if (!connect_tcp(forwarder))
		return false;
	
	/* Initialize SSL context */
	forwarder->ssl_ctx = SSL_CTX_new(TLS_client_method());
	if (!forwarder->ssl_ctx) {
		close(forwarder->sock);
		forwarder->sock = -1;
		forwarder->connected = false;
		return false;
	}
	
	/* Load certificates if provided */
	if (forwarder->config.tls_ca) {
		if (!SSL_CTX_load_verify_locations(forwarder->ssl_ctx,
		                                   forwarder->config.tls_ca, NULL)) {
			SSL_CTX_free(forwarder->ssl_ctx);
			close(forwarder->sock);
			forwarder->sock = -1;
			forwarder->connected = false;
			return false;
		}
	}
	
	if (forwarder->config.tls_cert && forwarder->config.tls_key) {
		if (!SSL_CTX_use_certificate_file(forwarder->ssl_ctx,
		                                  forwarder->config.tls_cert,
		                                  SSL_FILETYPE_PEM) ||
		    !SSL_CTX_use_PrivateKey_file(forwarder->ssl_ctx,
		                                 forwarder->config.tls_key,
		                                 SSL_FILETYPE_PEM)) {
			SSL_CTX_free(forwarder->ssl_ctx);
			close(forwarder->sock);
			forwarder->sock = -1;
			forwarder->connected = false;
			return false;
		}
	}
	
	/* Create SSL connection */
	forwarder->ssl = SSL_new(forwarder->ssl_ctx);
	if (!forwarder->ssl) {
		SSL_CTX_free(forwarder->ssl_ctx);
		close(forwarder->sock);
		forwarder->sock = -1;
		forwarder->connected = false;
		return false;
	}
	
	SSL_set_fd(forwarder->ssl, forwarder->sock);
	
	if (SSL_connect(forwarder->ssl) != 1) {
		SSL_free(forwarder->ssl);
		SSL_CTX_free(forwarder->ssl_ctx);
		close(forwarder->sock);
		forwarder->sock = -1;
		forwarder->ssl = NULL;
		forwarder->ssl_ctx = NULL;
		forwarder->connected = false;
		return false;
	}
	
	return true;
}
#endif

struct syslog_forwarder *syslog_forwarder_create(struct syslog_config *config)
{
	struct syslog_forwarder *forwarder;
	
	if (!config || !config->server)
		return NULL;
	
	forwarder = calloc(1, sizeof(*forwarder));
	if (!forwarder)
		return NULL;
	
	/* Copy configuration */
	forwarder->config.server = strdup(config->server);
	forwarder->config.port = config->port ? config->port :
	                        (config->transport == SYSLOG_TRANSPORT_TLS ? 6514 : 514);
	forwarder->config.transport = config->transport;
	forwarder->config.facility = config->facility;
	forwarder->config.app_name = config->app_name ? strdup(config->app_name) : strdup("nlmon");
	forwarder->config.reconnect = config->reconnect;
	forwarder->config.reconnect_interval = config->reconnect_interval ? config->reconnect_interval : 30;
	
	if (config->hostname) {
		forwarder->config.hostname = strdup(config->hostname);
		strncpy(forwarder->hostname, config->hostname, MAX_HOSTNAME_SIZE - 1);
	} else {
		get_hostname(forwarder->hostname, MAX_HOSTNAME_SIZE);
		forwarder->config.hostname = strdup(forwarder->hostname);
	}
	
#ifdef ENABLE_TLS
	if (config->tls_cert)
		forwarder->config.tls_cert = strdup(config->tls_cert);
	if (config->tls_key)
		forwarder->config.tls_key = strdup(config->tls_key);
	if (config->tls_ca)
		forwarder->config.tls_ca = strdup(config->tls_ca);
#endif
	
	pthread_mutex_init(&forwarder->lock, NULL);
	forwarder->sock = -1;
	
	/* Initial connection */
	syslog_forwarder_reconnect(forwarder);
	
	return forwarder;
}

void syslog_forwarder_destroy(struct syslog_forwarder *forwarder)
{
	if (!forwarder)
		return;
	
#ifdef ENABLE_TLS
	if (forwarder->ssl) {
		SSL_shutdown(forwarder->ssl);
		SSL_free(forwarder->ssl);
	}
	if (forwarder->ssl_ctx)
		SSL_CTX_free(forwarder->ssl_ctx);
#endif
	
	if (forwarder->sock >= 0)
		close(forwarder->sock);
	
	free((void *)forwarder->config.server);
	free((void *)forwarder->config.hostname);
	free((void *)forwarder->config.app_name);
#ifdef ENABLE_TLS
	free((void *)forwarder->config.tls_cert);
	free((void *)forwarder->config.tls_key);
	free((void *)forwarder->config.tls_ca);
#endif
	
	pthread_mutex_destroy(&forwarder->lock);
	free(forwarder);
}

bool syslog_forwarder_send(struct syslog_forwarder *forwarder,
                           struct syslog_message *msg)
{
	char buffer[MAX_SYSLOG_MSG_SIZE];
	char timestamp[64];
	int priority;
	ssize_t len, sent;
	bool success = false;
	
	if (!forwarder || !msg || !msg->message)
		return false;
	
	pthread_mutex_lock(&forwarder->lock);
	
	if (!forwarder->connected) {
		if (forwarder->config.reconnect) {
			syslog_forwarder_reconnect(forwarder);
		}
		if (!forwarder->connected) {
			forwarder->messages_failed++;
			pthread_mutex_unlock(&forwarder->lock);
			return false;
		}
	}
	
	/* Calculate priority */
	priority = (forwarder->config.facility << 3) | msg->severity;
	
	/* Format RFC 5424 message */
	format_rfc5424_timestamp(timestamp, sizeof(timestamp));
	
	len = snprintf(buffer, sizeof(buffer),
	              "<%d>1 %s %s %s - %s %s %s\n",
	              priority,
	              timestamp,
	              forwarder->hostname,
	              forwarder->config.app_name,
	              msg->msg_id ? msg->msg_id : "-",
	              msg->structured_data ? msg->structured_data : "-",
	              msg->message);
	
	if (len >= (ssize_t)sizeof(buffer)) {
		len = sizeof(buffer) - 1;
	}
	
	/* Send message */
	switch (forwarder->config.transport) {
	case SYSLOG_TRANSPORT_UDP:
		sent = send(forwarder->sock, buffer, len, 0);
		success = (sent == len);
		break;
		
	case SYSLOG_TRANSPORT_TCP:
		sent = send(forwarder->sock, buffer, len, 0);
		if (sent != len) {
			forwarder->connected = false;
			close(forwarder->sock);
			forwarder->sock = -1;
		} else {
			success = true;
		}
		break;
		
	case SYSLOG_TRANSPORT_TLS:
#ifdef ENABLE_TLS
		if (forwarder->ssl) {
			sent = SSL_write(forwarder->ssl, buffer, len);
			if (sent != len) {
				forwarder->connected = false;
				SSL_shutdown(forwarder->ssl);
				SSL_free(forwarder->ssl);
				SSL_CTX_free(forwarder->ssl_ctx);
				close(forwarder->sock);
				forwarder->sock = -1;
				forwarder->ssl = NULL;
				forwarder->ssl_ctx = NULL;
			} else {
				success = true;
			}
		}
#else
		/* TLS not supported in this build */
		success = false;
#endif
		break;
	}
	
	if (success) {
		forwarder->messages_sent++;
	} else {
		forwarder->messages_failed++;
	}
	
	pthread_mutex_unlock(&forwarder->lock);
	
	return success;
}

bool syslog_forwarder_is_connected(struct syslog_forwarder *forwarder)
{
	bool connected;
	
	if (!forwarder)
		return false;
	
	pthread_mutex_lock(&forwarder->lock);
	connected = forwarder->connected;
	pthread_mutex_unlock(&forwarder->lock);
	
	return connected;
}

bool syslog_forwarder_reconnect(struct syslog_forwarder *forwarder)
{
	bool success = false;
	
	if (!forwarder)
		return false;
	
	pthread_mutex_lock(&forwarder->lock);
	
	/* Close existing connection */
	if (forwarder->sock >= 0) {
#ifdef ENABLE_TLS
		if (forwarder->ssl) {
			SSL_shutdown(forwarder->ssl);
			SSL_free(forwarder->ssl);
			forwarder->ssl = NULL;
		}
		if (forwarder->ssl_ctx) {
			SSL_CTX_free(forwarder->ssl_ctx);
			forwarder->ssl_ctx = NULL;
		}
#endif
		close(forwarder->sock);
		forwarder->sock = -1;
	}
	
	forwarder->connected = false;
	
	/* Attempt reconnection */
	switch (forwarder->config.transport) {
	case SYSLOG_TRANSPORT_UDP:
		success = connect_udp(forwarder);
		break;
		
	case SYSLOG_TRANSPORT_TCP:
		success = connect_tcp(forwarder);
		break;
		
	case SYSLOG_TRANSPORT_TLS:
#ifdef ENABLE_TLS
		success = connect_tls(forwarder);
#else
		/* TLS not supported in this build */
		success = false;
#endif
		break;
	}
	
	if (success) {
		forwarder->reconnections++;
	}
	
	pthread_mutex_unlock(&forwarder->lock);
	
	return success;
}

bool syslog_forwarder_get_stats(struct syslog_forwarder *forwarder,
                                uint64_t *messages_sent,
                                uint64_t *messages_failed,
                                uint32_t *reconnections)
{
	if (!forwarder)
		return false;
	
	pthread_mutex_lock(&forwarder->lock);
	
	if (messages_sent)
		*messages_sent = forwarder->messages_sent;
	if (messages_failed)
		*messages_failed = forwarder->messages_failed;
	if (reconnections)
		*reconnections = forwarder->reconnections;
	
	pthread_mutex_unlock(&forwarder->lock);
	
	return true;
}
