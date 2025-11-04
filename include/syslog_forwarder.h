/* syslog_forwarder.h - Syslog forwarding with RFC 5424 support
 *
 * Provides syslog message forwarding with TCP/UDP transport,
 * TLS encryption, and automatic reconnection.
 */

#ifndef SYSLOG_FORWARDER_H
#define SYSLOG_FORWARDER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Syslog transport protocol */
enum syslog_transport {
	SYSLOG_TRANSPORT_UDP,
	SYSLOG_TRANSPORT_TCP,
	SYSLOG_TRANSPORT_TLS
};

/* Syslog severity levels (RFC 5424) */
enum syslog_severity {
	SYSLOG_EMERG   = 0,  /* Emergency: system is unusable */
	SYSLOG_ALERT   = 1,  /* Alert: action must be taken immediately */
	SYSLOG_CRIT    = 2,  /* Critical: critical conditions */
	SYSLOG_ERR     = 3,  /* Error: error conditions */
	SYSLOG_WARNING = 4,  /* Warning: warning conditions */
	SYSLOG_NOTICE  = 5,  /* Notice: normal but significant condition */
	SYSLOG_INFO    = 6,  /* Informational: informational messages */
	SYSLOG_DEBUG   = 7   /* Debug: debug-level messages */
};

/* Syslog facility codes (RFC 5424) */
enum syslog_facility {
	SYSLOG_KERN     = 0,   /* kernel messages */
	SYSLOG_USER     = 1,   /* user-level messages */
	SYSLOG_MAIL     = 2,   /* mail system */
	SYSLOG_DAEMON   = 3,   /* system daemons */
	SYSLOG_AUTH     = 4,   /* security/authorization messages */
	SYSLOG_SYSLOG   = 5,   /* messages generated internally by syslogd */
	SYSLOG_LPR      = 6,   /* line printer subsystem */
	SYSLOG_NEWS     = 7,   /* network news subsystem */
	SYSLOG_UUCP     = 8,   /* UUCP subsystem */
	SYSLOG_CRON     = 9,   /* clock daemon */
	SYSLOG_AUTHPRIV = 10,  /* security/authorization messages (private) */
	SYSLOG_FTP      = 11,  /* FTP daemon */
	SYSLOG_LOCAL0   = 16,  /* local use 0 */
	SYSLOG_LOCAL1   = 17,  /* local use 1 */
	SYSLOG_LOCAL2   = 18,  /* local use 2 */
	SYSLOG_LOCAL3   = 19,  /* local use 3 */
	SYSLOG_LOCAL4   = 20,  /* local use 4 */
	SYSLOG_LOCAL5   = 21,  /* local use 5 */
	SYSLOG_LOCAL6   = 22,  /* local use 6 */
	SYSLOG_LOCAL7   = 23   /* local use 7 */
};

/* Syslog forwarder configuration */
struct syslog_config {
	const char *server;           /* Server hostname or IP */
	uint16_t port;                /* Server port (default 514 for UDP, 6514 for TLS) */
	enum syslog_transport transport;
	enum syslog_facility facility;
	const char *hostname;         /* Local hostname (NULL for auto-detect) */
	const char *app_name;         /* Application name */
	const char *tls_cert;         /* TLS certificate file (for TLS transport) */
	const char *tls_key;          /* TLS key file (for TLS transport) */
	const char *tls_ca;           /* TLS CA file (for TLS transport) */
	bool reconnect;               /* Enable automatic reconnection */
	uint32_t reconnect_interval;  /* Reconnection interval in seconds */
};

/* Syslog forwarder handle (opaque) */
struct syslog_forwarder;

/* Syslog message structure */
struct syslog_message {
	enum syslog_severity severity;
	const char *msg_id;           /* Message ID (optional) */
	const char *message;          /* Message text */
	const char *structured_data;  /* RFC 5424 structured data (optional) */
};

/**
 * syslog_forwarder_create() - Create syslog forwarder
 * @config: Syslog configuration
 *
 * Returns: Syslog forwarder handle or NULL on error
 */
struct syslog_forwarder *syslog_forwarder_create(struct syslog_config *config);

/**
 * syslog_forwarder_destroy() - Destroy syslog forwarder
 * @forwarder: Syslog forwarder handle
 */
void syslog_forwarder_destroy(struct syslog_forwarder *forwarder);

/**
 * syslog_forwarder_send() - Send syslog message
 * @forwarder: Syslog forwarder handle
 * @msg: Syslog message to send
 *
 * Returns: true on success, false on error
 */
bool syslog_forwarder_send(struct syslog_forwarder *forwarder,
                           struct syslog_message *msg);

/**
 * syslog_forwarder_is_connected() - Check if forwarder is connected
 * @forwarder: Syslog forwarder handle
 *
 * Returns: true if connected, false otherwise
 */
bool syslog_forwarder_is_connected(struct syslog_forwarder *forwarder);

/**
 * syslog_forwarder_reconnect() - Manually trigger reconnection
 * @forwarder: Syslog forwarder handle
 *
 * Returns: true on success, false on error
 */
bool syslog_forwarder_reconnect(struct syslog_forwarder *forwarder);

/**
 * syslog_forwarder_get_stats() - Get forwarder statistics
 * @forwarder: Syslog forwarder handle
 * @messages_sent: Output for messages sent
 * @messages_failed: Output for messages failed
 * @reconnections: Output for number of reconnections
 *
 * Returns: true on success, false on error
 */
bool syslog_forwarder_get_stats(struct syslog_forwarder *forwarder,
                                uint64_t *messages_sent,
                                uint64_t *messages_failed,
                                uint32_t *reconnections);

#endif /* SYSLOG_FORWARDER_H */
