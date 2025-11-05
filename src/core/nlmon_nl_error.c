#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <netlink/errno.h>

#include "nlmon_nl_error.h"

/**
 * Error message strings for nlmon netlink errors
 */
static const char *nlmon_nl_error_strings[] = {
	[0] = "Success",
	[1] = "Out of memory",
	[2] = "Invalid argument",
	[3] = "Try again (non-blocking operation)",
	[4] = "Protocol error",
	[5] = "No access or permission denied",
	[6] = "Attribute not found",
	[7] = "Parse error",
	[8] = "Socket not connected",
	[9] = "Object not found",
	[10] = "Resource busy",
	[11] = "Message size error",
	[12] = "Buffer overflow",
	[13] = "Message truncated",
	[14] = "Sequence number mismatch",
	[15] = "Dump interrupted",
	[99] = "Unknown error",
};

#define NLMON_NL_ERR_MAX 99

/**
 * Map libnl error code to nlmon error code
 */
int nlmon_nl_map_error(int libnl_err)
{
	/* libnl errors are negative, convert to positive for comparison */
	int err = -libnl_err;
	
	/* If already positive or zero, it's likely already an nlmon error */
	if (libnl_err >= 0)
		return NLMON_NL_SUCCESS;
	
	/* Map libnl error codes to nlmon error codes */
	switch (err) {
	case NLE_SUCCESS:
		return NLMON_NL_SUCCESS;
	
	case NLE_NOMEM:
		return NLMON_NL_ERR_NOMEM;
	
	case NLE_INVAL:
	case NLE_RANGE:
		return NLMON_NL_ERR_INVAL;
	
	case NLE_AGAIN:
		return NLMON_NL_ERR_AGAIN;
	
	case NLE_PROTO_MISMATCH:
	case NLE_MSGTYPE_NOSUPPORT:
	case NLE_OPNOTSUPP:
	case NLE_AF_NOSUPPORT:
		return NLMON_NL_ERR_PROTO;
	
	case NLE_NOACCESS:
	case NLE_PERM:
		return NLMON_NL_ERR_NOACCESS;
	
	case NLE_NOATTR:
	case NLE_MISSING_ATTR:
		return NLMON_NL_ERR_NOATTR;
	
	case NLE_PARSE_ERR:
	case NLE_MSG_TOOSHORT:
		return NLMON_NL_ERR_PARSE;
	
	case NLE_BAD_SOCK:
		return NLMON_NL_ERR_NOTCONN;
	
	case NLE_OBJ_NOTFOUND:
	case NLE_NODEV:
	case NLE_NOADDR:
		return NLMON_NL_ERR_NOTFOUND;
	
	case NLE_BUSY:
		return NLMON_NL_ERR_BUSY;
	
	case NLE_MSGSIZE:
		return NLMON_NL_ERR_MSGSIZE;
	
	case NLE_MSG_OVERFLOW:
		return NLMON_NL_ERR_OVERFLOW;
	
	case NLE_MSG_TRUNC:
		return NLMON_NL_ERR_TRUNC;
	
	case NLE_SEQ_MISMATCH:
		return NLMON_NL_ERR_SEQMISMATCH;
	
	case NLE_DUMP_INTR:
		return NLMON_NL_ERR_DUMP_INTR;
	
	case NLE_FAILURE:
	case NLE_INTR:
	case NLE_EXIST:
	case NLE_AF_MISMATCH:
	case NLE_OBJ_MISMATCH:
	case NLE_NOCACHE:
	case NLE_SRCRT_NOSUPPORT:
	case NLE_PKTLOC_FILE:
	case NLE_IMMUTABLE:
	default:
		return NLMON_NL_ERR_UNKNOWN;
	}
}

/**
 * Get error string for nlmon netlink error code
 */
const char *nlmon_nl_strerror(int err)
{
	/* Convert to positive index */
	int idx = -err;
	
	/* Handle success case */
	if (err == NLMON_NL_SUCCESS || err > 0)
		return nlmon_nl_error_strings[0];
	
	/* Bounds check */
	if (idx < 0 || idx > NLMON_NL_ERR_MAX)
		return nlmon_nl_error_strings[99]; /* Unknown error */
	
	/* Special case for error code 99 (unknown) */
	if (idx == 99)
		return nlmon_nl_error_strings[99];
	
	/* Return error string if within normal range */
	if (idx <= 15)
		return nlmon_nl_error_strings[idx];
	
	/* Default to unknown error */
	return nlmon_nl_error_strings[99];
}

/**
 * Map system errno to nlmon netlink error code
 */
int nlmon_nl_map_errno(int errno_val)
{
	switch (errno_val) {
	case 0:
		return NLMON_NL_SUCCESS;
	
	case ENOMEM:
	case ENOBUFS:
		return NLMON_NL_ERR_NOMEM;
	
	case EINVAL:
	case EFAULT:
	case ENOPROTOOPT:
		return NLMON_NL_ERR_INVAL;
	
	case EAGAIN:
#if EAGAIN != EWOULDBLOCK
	case EWOULDBLOCK:
#endif
		return NLMON_NL_ERR_AGAIN;
	
	case EPROTONOSUPPORT:
	case EAFNOSUPPORT:
	case EOPNOTSUPP:
		return NLMON_NL_ERR_PROTO;
	
	case EACCES:
	case EPERM:
		return NLMON_NL_ERR_NOACCESS;
	
	case ENOENT:
	case ESRCH:
	case ENODEV:
		return NLMON_NL_ERR_NOTFOUND;
	
	case EBUSY:
		return NLMON_NL_ERR_BUSY;
	
	case EMSGSIZE:
		return NLMON_NL_ERR_MSGSIZE;
	
	case EBADF:
	case ENOTSOCK:
	case ENOTCONN:
		return NLMON_NL_ERR_NOTCONN;
	
	case EOVERFLOW:
		return NLMON_NL_ERR_OVERFLOW;
	
	case ERANGE:
		return NLMON_NL_ERR_INVAL;
	
	default:
		return NLMON_NL_ERR_UNKNOWN;
	}
}

/**
 * Logging support for netlink operations
 */

#include <stdarg.h>
#include <time.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

/* Global logging level */
static enum nlmon_nl_log_level current_log_level = NLMON_NL_LOG_INFO;

/* Flag to enable message dumping on errors */
static int dump_on_error = 0;

/**
 * Set logging level for netlink operations
 */
void nlmon_nl_set_log_level(enum nlmon_nl_log_level level)
{
	current_log_level = level;
}

/**
 * Get current logging level
 */
enum nlmon_nl_log_level nlmon_nl_get_log_level(void)
{
	return current_log_level;
}

/**
 * Enable/disable message dumping on parse errors
 */
void nlmon_nl_set_dump_on_error(int enable)
{
	dump_on_error = enable;
}

/**
 * Get timestamp string for logging
 */
static void get_timestamp(char *buf, size_t len)
{
	time_t now;
	struct tm *tm_info;
	
	time(&now);
	tm_info = localtime(&now);
	strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm_info);
}

/**
 * Internal logging function
 */
static void nlmon_nl_log(enum nlmon_nl_log_level level, const char *fmt, ...)
{
	va_list args;
	char timestamp[32];
	const char *level_str;
	
	/* Check if this message should be logged */
	if (level > current_log_level)
		return;
	
	/* Get level string */
	switch (level) {
	case NLMON_NL_LOG_ERROR:
		level_str = "ERROR";
		break;
	case NLMON_NL_LOG_WARN:
		level_str = "WARN";
		break;
	case NLMON_NL_LOG_INFO:
		level_str = "INFO";
		break;
	case NLMON_NL_LOG_DEBUG:
		level_str = "DEBUG";
		break;
	default:
		level_str = "UNKNOWN";
		break;
	}
	
	/* Get timestamp */
	get_timestamp(timestamp, sizeof(timestamp));
	
	/* Print log message */
	fprintf(stderr, "[%s] [NETLINK-%s] ", timestamp, level_str);
	
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	
	fprintf(stderr, "\n");
}

/**
 * Log netlink error with context
 */
void nlmon_nl_log_error(const char *context, int error)
{
	int nlmon_err;
	
	/* Map error to nlmon error code */
	if (error < 0) {
		/* Negative error, likely from libnl */
		nlmon_err = nlmon_nl_map_error(error);
		nlmon_nl_log(NLMON_NL_LOG_ERROR, "%s: %s (libnl error: %s)",
		             context, nlmon_nl_strerror(nlmon_err), nl_geterror(error));
	} else if (error > 0) {
		/* Positive error, likely errno */
		nlmon_err = nlmon_nl_map_errno(error);
		nlmon_nl_log(NLMON_NL_LOG_ERROR, "%s: %s (errno: %d - %s)",
		             context, nlmon_nl_strerror(nlmon_err), error, strerror(error));
	} else {
		/* No error */
		nlmon_nl_log(NLMON_NL_LOG_DEBUG, "%s: Success", context);
	}
}

/**
 * Get netlink message type name
 */
static const char *get_nlmsg_type_name(uint16_t type)
{
	/* Standard netlink message types */
	switch (type) {
	case NLMSG_NOOP:
		return "NLMSG_NOOP";
	case NLMSG_ERROR:
		return "NLMSG_ERROR";
	case NLMSG_DONE:
		return "NLMSG_DONE";
	case NLMSG_OVERRUN:
		return "NLMSG_OVERRUN";
	}
	
	/* NETLINK_ROUTE message types */
	switch (type) {
	case RTM_NEWLINK:
		return "RTM_NEWLINK";
	case RTM_DELLINK:
		return "RTM_DELLINK";
	case RTM_GETLINK:
		return "RTM_GETLINK";
	case RTM_NEWADDR:
		return "RTM_NEWADDR";
	case RTM_DELADDR:
		return "RTM_DELADDR";
	case RTM_GETADDR:
		return "RTM_GETADDR";
	case RTM_NEWROUTE:
		return "RTM_NEWROUTE";
	case RTM_DELROUTE:
		return "RTM_DELROUTE";
	case RTM_GETROUTE:
		return "RTM_GETROUTE";
	case RTM_NEWNEIGH:
		return "RTM_NEWNEIGH";
	case RTM_DELNEIGH:
		return "RTM_DELNEIGH";
	case RTM_GETNEIGH:
		return "RTM_GETNEIGH";
	case RTM_NEWRULE:
		return "RTM_NEWRULE";
	case RTM_DELRULE:
		return "RTM_DELRULE";
	case RTM_GETRULE:
		return "RTM_GETRULE";
	default:
		return "UNKNOWN";
	}
}

/**
 * Get netlink message flags string
 */
static void get_nlmsg_flags_str(uint16_t flags, char *buf, size_t len)
{
	int pos = 0;
	
	buf[0] = '\0';
	
	if (flags & NLM_F_REQUEST)
		pos += snprintf(buf + pos, len - pos, "REQUEST|");
	if (flags & NLM_F_MULTI)
		pos += snprintf(buf + pos, len - pos, "MULTI|");
	if (flags & NLM_F_ACK)
		pos += snprintf(buf + pos, len - pos, "ACK|");
	if (flags & NLM_F_ECHO)
		pos += snprintf(buf + pos, len - pos, "ECHO|");
	if (flags & NLM_F_DUMP_INTR)
		pos += snprintf(buf + pos, len - pos, "DUMP_INTR|");
	if (flags & NLM_F_DUMP_FILTERED)
		pos += snprintf(buf + pos, len - pos, "DUMP_FILTERED|");
	if (flags & NLM_F_ROOT)
		pos += snprintf(buf + pos, len - pos, "ROOT|");
	if (flags & NLM_F_MATCH)
		pos += snprintf(buf + pos, len - pos, "MATCH|");
	if (flags & NLM_F_ATOMIC)
		pos += snprintf(buf + pos, len - pos, "ATOMIC|");
	if (flags & NLM_F_REPLACE)
		pos += snprintf(buf + pos, len - pos, "REPLACE|");
	if (flags & NLM_F_EXCL)
		pos += snprintf(buf + pos, len - pos, "EXCL|");
	if (flags & NLM_F_CREATE)
		pos += snprintf(buf + pos, len - pos, "CREATE|");
	if (flags & NLM_F_APPEND)
		pos += snprintf(buf + pos, len - pos, "APPEND|");
	
	/* Remove trailing pipe */
	if (pos > 0 && buf[pos - 1] == '|')
		buf[pos - 1] = '\0';
	
	if (buf[0] == '\0')
		snprintf(buf, len, "NONE");
}

/**
 * Log netlink message details (for debugging)
 */
void nlmon_nl_log_message(struct nlmsghdr *nlh, const char *direction)
{
	char flags_str[256];
	
	if (!nlh || current_log_level < NLMON_NL_LOG_DEBUG)
		return;
	
	get_nlmsg_flags_str(nlh->nlmsg_flags, flags_str, sizeof(flags_str));
	
	nlmon_nl_log(NLMON_NL_LOG_DEBUG,
	             "Message %s: type=%s(%u) len=%u flags=%s seq=%u pid=%u",
	             direction,
	             get_nlmsg_type_name(nlh->nlmsg_type),
	             nlh->nlmsg_type,
	             nlh->nlmsg_len,
	             flags_str,
	             nlh->nlmsg_seq,
	             nlh->nlmsg_pid);
}

/**
 * Dump netlink message in hex format
 */
void nlmon_nl_dump_message(struct nlmsghdr *nlh, size_t len)
{
	unsigned char *data;
	size_t i, j;
	char hex_buf[64];
	char ascii_buf[20];
	int hex_pos, ascii_pos;
	
	if (!nlh || !dump_on_error)
		return;
	
	if (current_log_level < NLMON_NL_LOG_DEBUG)
		return;
	
	data = (unsigned char *)nlh;
	
	nlmon_nl_log(NLMON_NL_LOG_DEBUG, "Message dump (%zu bytes):", len);
	
	for (i = 0; i < len; i += 16) {
		hex_pos = 0;
		ascii_pos = 0;
		
		/* Print offset */
		fprintf(stderr, "  %04zx: ", i);
		
		/* Print hex bytes */
		for (j = 0; j < 16; j++) {
			if (i + j < len) {
				hex_pos += snprintf(hex_buf + hex_pos,
				                    sizeof(hex_buf) - hex_pos,
				                    "%02x ", data[i + j]);
				
				/* Print ASCII representation */
				if (data[i + j] >= 32 && data[i + j] <= 126)
					ascii_buf[ascii_pos++] = data[i + j];
				else
					ascii_buf[ascii_pos++] = '.';
			} else {
				hex_pos += snprintf(hex_buf + hex_pos,
				                    sizeof(hex_buf) - hex_pos,
				                    "   ");
			}
			
			/* Add extra space after 8 bytes */
			if (j == 7)
				hex_pos += snprintf(hex_buf + hex_pos,
				                    sizeof(hex_buf) - hex_pos,
				                    " ");
		}
		
		ascii_buf[ascii_pos] = '\0';
		fprintf(stderr, "%-48s  |%s|\n", hex_buf, ascii_buf);
	}
}
