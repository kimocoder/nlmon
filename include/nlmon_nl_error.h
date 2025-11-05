#ifndef NLMON_NL_ERROR_H
#define NLMON_NL_ERROR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
struct nlmsghdr;

/* Logging levels for netlink operations */
enum nlmon_nl_log_level {
	NLMON_NL_LOG_ERROR = 0,   /* Error messages */
	NLMON_NL_LOG_WARN = 1,    /* Warning messages */
	NLMON_NL_LOG_INFO = 2,    /* Informational messages */
	NLMON_NL_LOG_DEBUG = 3,   /* Debug messages */
};

/**
 * nlmon netlink error codes
 * 
 * These error codes map libnl-tiny errors to nlmon-specific error codes
 * for consistent error handling across the application.
 */
enum nlmon_nl_error {
	NLMON_NL_SUCCESS = 0,           /* Success */
	NLMON_NL_ERR_NOMEM = -1,        /* Out of memory */
	NLMON_NL_ERR_INVAL = -2,        /* Invalid argument */
	NLMON_NL_ERR_AGAIN = -3,        /* Try again (non-blocking) */
	NLMON_NL_ERR_PROTO = -4,        /* Protocol error */
	NLMON_NL_ERR_NOACCESS = -5,     /* No access / permission denied */
	NLMON_NL_ERR_NOATTR = -6,       /* Attribute not found */
	NLMON_NL_ERR_PARSE = -7,        /* Parse error */
	NLMON_NL_ERR_NOTCONN = -8,      /* Socket not connected */
	NLMON_NL_ERR_NOTFOUND = -9,     /* Object not found */
	NLMON_NL_ERR_BUSY = -10,        /* Resource busy */
	NLMON_NL_ERR_MSGSIZE = -11,     /* Message size error */
	NLMON_NL_ERR_OVERFLOW = -12,    /* Buffer overflow */
	NLMON_NL_ERR_TRUNC = -13,       /* Message truncated */
	NLMON_NL_ERR_SEQMISMATCH = -14, /* Sequence number mismatch */
	NLMON_NL_ERR_DUMP_INTR = -15,   /* Dump interrupted */
	NLMON_NL_ERR_UNKNOWN = -99,     /* Unknown error */
};

/**
 * Map libnl error code to nlmon error code
 * 
 * Converts libnl-tiny error codes (NLE_*) to nlmon netlink error codes
 * (NLMON_NL_ERR_*) for consistent error handling.
 * 
 * @param libnl_err libnl error code (negative value)
 * @return nlmon netlink error code
 */
int nlmon_nl_map_error(int libnl_err);

/**
 * Get error string for nlmon netlink error code
 * 
 * Returns a human-readable error message for the given nlmon netlink
 * error code.
 * 
 * @param err nlmon netlink error code
 * @return Error message string (never NULL)
 */
const char *nlmon_nl_strerror(int err);

/**
 * Map system errno to nlmon netlink error code
 * 
 * Converts standard errno values to nlmon netlink error codes.
 * 
 * @param errno_val System errno value
 * @return nlmon netlink error code
 */
int nlmon_nl_map_errno(int errno_val);

/**
 * Set logging level for netlink operations
 * 
 * @param level Logging level to set
 */
void nlmon_nl_set_log_level(enum nlmon_nl_log_level level);

/**
 * Get current logging level
 * 
 * @return Current logging level
 */
enum nlmon_nl_log_level nlmon_nl_get_log_level(void);

/**
 * Enable/disable message dumping on parse errors
 * 
 * @param enable 1 to enable, 0 to disable
 */
void nlmon_nl_set_dump_on_error(int enable);

/**
 * Log netlink error with context
 * 
 * Logs an error message with context information. Automatically maps
 * libnl and errno error codes to nlmon error codes.
 * 
 * @param context Context string describing the operation
 * @param error Error code (negative for libnl, positive for errno)
 */
void nlmon_nl_log_error(const char *context, int error);

/**
 * Log netlink message details (for debugging)
 * 
 * Logs detailed information about a netlink message including type,
 * flags, sequence number, and PID.
 * 
 * @param nlh Netlink message header
 * @param direction Direction string ("RX" or "TX")
 */
void nlmon_nl_log_message(struct nlmsghdr *nlh, const char *direction);

/**
 * Dump netlink message in hex format
 * 
 * Dumps the raw bytes of a netlink message in hex format for debugging.
 * Only dumps if dump_on_error is enabled and log level is DEBUG.
 * 
 * @param nlh Netlink message header
 * @param len Length of message in bytes
 */
void nlmon_nl_dump_message(struct nlmsghdr *nlh, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NLMON_NL_ERROR_H */
