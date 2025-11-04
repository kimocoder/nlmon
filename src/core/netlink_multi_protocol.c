#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/sock_diag.h>
#include <linux/inet_diag.h>
#include <linux/unix_diag.h>
#include <arpa/inet.h>
#include <netlink/attr.h>

#include "netlink_multi_protocol.h"
#include "qca_vendor.h"

#define NLMON_RECV_BUF_SIZE 8192

/* Create and bind a netlink socket for a specific protocol */
static int create_netlink_socket(int protocol)
{
	struct sockaddr_nl addr;
	int sock;
	int buf_size = 32768;
	
	sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, protocol);
	if (sock < 0) {
		perror("socket");
		return -1;
	}
	
	/* Increase buffer sizes */
	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) < 0) {
		perror("setsockopt SO_RCVBUF");
	}
	
	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = 0;  /* No multicast groups by default */
	
	/* For NETLINK_ROUTE, subscribe to all groups */
	if (protocol == NETLINK_ROUTE) {
		addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR |
		                 RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE |
		                 RTMGRP_NEIGH | RTMGRP_IPV4_RULE;
#ifdef RTMGRP_IPV6_RULE
		addr.nl_groups |= RTMGRP_IPV6_RULE;
#endif
	}
	
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(sock);
		return -1;
	}
	
	return sock;
}

struct nlmon_multi_protocol_ctx *nlmon_multi_protocol_init(void)
{
	struct nlmon_multi_protocol_ctx *ctx;
	
	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;
	
	ctx->route_sock = -1;
	ctx->generic_sock = -1;
	ctx->sock_diag_sock = -1;
	
	/* By default, enable NETLINK_ROUTE */
	ctx->enable_route = 1;
	ctx->enable_generic = 0;
	ctx->enable_sock_diag = 0;
	
	return ctx;
}

int nlmon_multi_protocol_enable(struct nlmon_multi_protocol_ctx *ctx, nlmon_protocol_t proto)
{
	int sock;
	
	if (!ctx)
		return -1;
	
	switch (proto) {
	case NLMON_PROTO_ROUTE:
		if (ctx->route_sock >= 0)
			return 0;  /* Already enabled */
		
		sock = create_netlink_socket(NETLINK_ROUTE);
		if (sock < 0)
			return -1;
		
		ctx->route_sock = sock;
		ctx->enable_route = 1;
		break;
		
	case NLMON_PROTO_GENERIC:
		if (ctx->generic_sock >= 0)
			return 0;  /* Already enabled */
		
		sock = create_netlink_socket(NETLINK_GENERIC);
		if (sock < 0)
			return -1;
		
		ctx->generic_sock = sock;
		ctx->enable_generic = 1;
		break;
		
	case NLMON_PROTO_SOCK_DIAG:
		if (ctx->sock_diag_sock >= 0)
			return 0;  /* Already enabled */
		
		sock = create_netlink_socket(NETLINK_SOCK_DIAG);
		if (sock < 0)
			return -1;
		
		ctx->sock_diag_sock = sock;
		ctx->enable_sock_diag = 1;
		break;
		
	default:
		return -1;
	}
	
	return 0;
}

int nlmon_multi_protocol_disable(struct nlmon_multi_protocol_ctx *ctx, nlmon_protocol_t proto)
{
	if (!ctx)
		return -1;
	
	switch (proto) {
	case NLMON_PROTO_ROUTE:
		if (ctx->route_sock >= 0) {
			close(ctx->route_sock);
			ctx->route_sock = -1;
		}
		ctx->enable_route = 0;
		break;
		
	case NLMON_PROTO_GENERIC:
		if (ctx->generic_sock >= 0) {
			close(ctx->generic_sock);
			ctx->generic_sock = -1;
		}
		ctx->enable_generic = 0;
		break;
		
	case NLMON_PROTO_SOCK_DIAG:
		if (ctx->sock_diag_sock >= 0) {
			close(ctx->sock_diag_sock);
			ctx->sock_diag_sock = -1;
		}
		ctx->enable_sock_diag = 0;
		break;
		
	default:
		return -1;
	}
	
	return 0;
}

void nlmon_multi_protocol_set_callback(struct nlmon_multi_protocol_ctx *ctx,
                                       void (*callback)(nlmon_event_type_t, void *, void *),
                                       void *user_data)
{
	if (!ctx)
		return;
	
	ctx->event_callback = callback;
	ctx->user_data = user_data;
}

int nlmon_multi_protocol_get_fd(struct nlmon_multi_protocol_ctx *ctx, nlmon_protocol_t proto)
{
	if (!ctx)
		return -1;
	
	switch (proto) {
	case NLMON_PROTO_ROUTE:
		return ctx->route_sock;
	case NLMON_PROTO_GENERIC:
		return ctx->generic_sock;
	case NLMON_PROTO_SOCK_DIAG:
		return ctx->sock_diag_sock;
	default:
		return -1;
	}
}

int nlmon_parse_generic_msg(struct nlmsghdr *nlh, struct nlmon_generic_msg *msg)
{
	struct genlmsghdr *ghdr;
	struct nlattr *attr;
	int attr_len;
	uint32_t vendor_id = 0;
	uint32_t vendor_subcmd = 0;
	int is_vendor_cmd = 0;
	
	if (!nlh || !msg)
		return -1;
	
	if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct genlmsghdr)))
		return -1;
	
	ghdr = (struct genlmsghdr *)NLMSG_DATA(nlh);
	
	msg->cmd = ghdr->cmd;
	msg->version = ghdr->version;
	msg->family_id = nlh->nlmsg_type;
	
	/* Parse attributes to check for vendor commands */
	attr = (struct nlattr *)((char *)ghdr + GENL_HDRLEN);
	attr_len = nlh->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
	
	while (nla_ok(attr, attr_len)) {
		/* Check for vendor ID and subcmd attributes (nl80211 specific) */
		if (nla_type(attr) == 195) {  /* NL80211_ATTR_VENDOR_ID */
			if ((size_t)nla_len(attr) >= sizeof(uint32_t)) {
				vendor_id = *(uint32_t *)nla_data(attr);
				is_vendor_cmd = 1;
			}
		} else if (nla_type(attr) == 196) {  /* NL80211_ATTR_VENDOR_SUBCMD */
			if ((size_t)nla_len(attr) >= sizeof(uint32_t)) {
				vendor_subcmd = *(uint32_t *)nla_data(attr);
			}
		}
		attr = nla_next(attr, &attr_len);
	}
	
	/* Check if this is a QCA vendor command */
	if (is_vendor_cmd && vendor_id == OUI_QCA) {
		/* TODO: Implement qca_vendor_subcmd_to_string() for vendor subcmd decoding */
		snprintf(msg->family_name, sizeof(msg->family_name), 
		         "nl80211/QCA:0x%x", vendor_subcmd);
		msg->cmd = vendor_subcmd;  /* Override with vendor subcmd for better display */
	} else if (is_vendor_cmd) {
		snprintf(msg->family_name, sizeof(msg->family_name), 
		         "nl80211/vendor:0x%06x", vendor_id);
	} else {
		/* Family name would need to be resolved separately */
		snprintf(msg->family_name, sizeof(msg->family_name), "family_%u", msg->family_id);
	}
	
	return 0;
}

int nlmon_parse_sock_diag_msg(struct nlmsghdr *nlh, struct nlmon_sock_diag *diag)
{
	struct inet_diag_msg *idiag;
	
	if (!nlh || !diag)
		return -1;
	
	if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct inet_diag_msg)))
		return -1;
	
	idiag = (struct inet_diag_msg *)NLMSG_DATA(nlh);
	
	memset(diag, 0, sizeof(*diag));
	
	diag->family = idiag->idiag_family;
	diag->state = idiag->idiag_state;
	diag->protocol = 0;  /* Not directly available in inet_diag_msg */
	diag->src_port = ntohs(idiag->id.idiag_sport);
	diag->dst_port = ntohs(idiag->id.idiag_dport);
	diag->inode = idiag->idiag_inode;
	
	/* Convert addresses to string */
	if (idiag->idiag_family == AF_INET) {
		inet_ntop(AF_INET, &idiag->id.idiag_src, diag->src_addr, sizeof(diag->src_addr));
		inet_ntop(AF_INET, &idiag->id.idiag_dst, diag->dst_addr, sizeof(diag->dst_addr));
	} else if (idiag->idiag_family == AF_INET6) {
		inet_ntop(AF_INET6, &idiag->id.idiag_src, diag->src_addr, sizeof(diag->src_addr));
		inet_ntop(AF_INET6, &idiag->id.idiag_dst, diag->dst_addr, sizeof(diag->dst_addr));
	}
	
	return 0;
}

static void process_route_msg(struct nlmon_multi_protocol_ctx *ctx, struct nlmsghdr *nlh)
{
	nlmon_event_type_t event_type;
	
	/* Determine event type based on message type */
	switch (nlh->nlmsg_type) {
	case RTM_NEWLINK:
	case RTM_DELLINK:
	case RTM_GETLINK:
	case RTM_SETLINK:
		event_type = NLMON_EVENT_LINK;
		break;
		
	case RTM_NEWADDR:
	case RTM_DELADDR:
	case RTM_GETADDR:
		event_type = NLMON_EVENT_ADDR;
		break;
		
	case RTM_NEWROUTE:
	case RTM_DELROUTE:
	case RTM_GETROUTE:
		event_type = NLMON_EVENT_ROUTE;
		break;
		
	case RTM_NEWNEIGH:
	case RTM_DELNEIGH:
	case RTM_GETNEIGH:
		event_type = NLMON_EVENT_NEIGH;
		break;
		
	case RTM_NEWRULE:
	case RTM_DELRULE:
	case RTM_GETRULE:
		event_type = NLMON_EVENT_RULE;
		break;
		
	default:
		return;  /* Unknown message type */
	}
	
	if (ctx->event_callback)
		ctx->event_callback(event_type, nlh, ctx->user_data);
}

static void process_generic_msg(struct nlmon_multi_protocol_ctx *ctx, struct nlmsghdr *nlh)
{
	struct nlmon_generic_msg msg;
	
	if (nlmon_parse_generic_msg(nlh, &msg) == 0) {
		if (ctx->event_callback)
			ctx->event_callback(NLMON_EVENT_GENERIC, &msg, ctx->user_data);
	}
}

static void process_sock_diag_msg(struct nlmon_multi_protocol_ctx *ctx, struct nlmsghdr *nlh)
{
	struct nlmon_sock_diag diag;
	
	if (nlmon_parse_sock_diag_msg(nlh, &diag) == 0) {
		if (ctx->event_callback)
			ctx->event_callback(NLMON_EVENT_SOCK_DIAG, &diag, ctx->user_data);
	}
}

int nlmon_multi_protocol_process(struct nlmon_multi_protocol_ctx *ctx, nlmon_protocol_t proto)
{
	char buf[NLMON_RECV_BUF_SIZE];
	struct nlmsghdr *nlh;
	int sock;
	ssize_t len;
	
	if (!ctx)
		return -1;
	
	sock = nlmon_multi_protocol_get_fd(ctx, proto);
	if (sock < 0)
		return -1;
	
	len = recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
	if (len < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;  /* No data available */
		return -1;
	}
	
	if (len == 0)
		return 0;
	
	/* Process all messages in the buffer */
	for (nlh = (struct nlmsghdr *)buf;
	     NLMSG_OK(nlh, len);
	     nlh = NLMSG_NEXT(nlh, len)) {
		
		/* Check for end of multipart message */
		if (nlh->nlmsg_type == NLMSG_DONE)
			break;
		
		/* Check for error */
		if (nlh->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nlh);
			if (err->error != 0) {
				fprintf(stderr, "Netlink error: %s\n", strerror(-err->error));
			}
			continue;
		}
		
		/* Process based on protocol */
		switch (proto) {
		case NLMON_PROTO_ROUTE:
			process_route_msg(ctx, nlh);
			break;
		case NLMON_PROTO_GENERIC:
			process_generic_msg(ctx, nlh);
			break;
		case NLMON_PROTO_SOCK_DIAG:
			process_sock_diag_msg(ctx, nlh);
			break;
		}
	}
	
	return 0;
}

void nlmon_multi_protocol_destroy(struct nlmon_multi_protocol_ctx *ctx)
{
	if (!ctx)
		return;
	
	if (ctx->route_sock >= 0)
		close(ctx->route_sock);
	if (ctx->generic_sock >= 0)
		close(ctx->generic_sock);
	if (ctx->sock_diag_sock >= 0)
		close(ctx->sock_diag_sock);
	
	free(ctx);
}
