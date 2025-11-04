#ifndef NETLINK_MULTI_PROTOCOL_H
#define NETLINK_MULTI_PROTOCOL_H

#include <stdint.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/sock_diag.h>
#include <linux/inet_diag.h>

/* Protocol types supported */
typedef enum {
	NLMON_PROTO_ROUTE = NETLINK_ROUTE,
	NLMON_PROTO_GENERIC = NETLINK_GENERIC,
	NLMON_PROTO_SOCK_DIAG = NETLINK_SOCK_DIAG,
} nlmon_protocol_t;

/* Event types for different protocols */
typedef enum {
	NLMON_EVENT_LINK,
	NLMON_EVENT_ADDR,
	NLMON_EVENT_ROUTE,
	NLMON_EVENT_NEIGH,
	NLMON_EVENT_RULE,
	NLMON_EVENT_GENERIC,
	NLMON_EVENT_SOCK_DIAG,
} nlmon_event_type_t;

/* Generic netlink message info */
struct nlmon_generic_msg {
	uint8_t cmd;
	uint8_t version;
	uint16_t family_id;
	char family_name[32];
};

/* Socket diagnostics info */
struct nlmon_sock_diag {
	uint8_t family;        /* AF_INET, AF_INET6, etc */
	uint8_t state;         /* TCP state */
	uint8_t protocol;      /* IPPROTO_TCP, IPPROTO_UDP, etc */
	uint16_t src_port;
	uint16_t dst_port;
	uint32_t inode;
	char src_addr[64];
	char dst_addr[64];
};

/* Multi-protocol netlink context */
struct nlmon_multi_protocol_ctx {
	int route_sock;
	int generic_sock;
	int sock_diag_sock;
	
	/* Protocol enable flags */
	int enable_route;
	int enable_generic;
	int enable_sock_diag;
	
	/* Callback for events */
	void (*event_callback)(nlmon_event_type_t type, void *data, void *user_data);
	void *user_data;
};

/* Initialize multi-protocol support */
struct nlmon_multi_protocol_ctx *nlmon_multi_protocol_init(void);

/* Enable/disable specific protocols */
int nlmon_multi_protocol_enable(struct nlmon_multi_protocol_ctx *ctx, nlmon_protocol_t proto);
int nlmon_multi_protocol_disable(struct nlmon_multi_protocol_ctx *ctx, nlmon_protocol_t proto);

/* Set event callback */
void nlmon_multi_protocol_set_callback(struct nlmon_multi_protocol_ctx *ctx,
                                       void (*callback)(nlmon_event_type_t, void *, void *),
                                       void *user_data);

/* Get file descriptors for event loop integration */
int nlmon_multi_protocol_get_fd(struct nlmon_multi_protocol_ctx *ctx, nlmon_protocol_t proto);

/* Process messages from a specific protocol */
int nlmon_multi_protocol_process(struct nlmon_multi_protocol_ctx *ctx, nlmon_protocol_t proto);

/* Parse NETLINK_GENERIC messages */
int nlmon_parse_generic_msg(struct nlmsghdr *nlh, struct nlmon_generic_msg *msg);

/* Parse NETLINK_SOCK_DIAG messages */
int nlmon_parse_sock_diag_msg(struct nlmsghdr *nlh, struct nlmon_sock_diag *diag);

/* Cleanup */
void nlmon_multi_protocol_destroy(struct nlmon_multi_protocol_ctx *ctx);

#endif /* NETLINK_MULTI_PROTOCOL_H */
