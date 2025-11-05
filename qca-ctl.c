/*
 * qca-ctl - Qualcomm Atheros WiFi Driver Control Tool
 * 
 * Control qcacld-3.0 driver from userspace using nl80211 vendor commands
 * 
 * Usage:
 *   qca-ctl <interface> <command> [options]
 * 
 * Examples:
 *   qca-ctl wlan0 get-stats
 *   qca-ctl wlan0 get-info
 *   qca-ctl wlan0 set-roam-rssi 70
 *   qca-ctl wlan0 vendor-cmd 0x0f
 *   qca-ctl wlan0 list-commands
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <net/if.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>

/* QCA Vendor OUI */
#define QCA_NL80211_VENDOR_ID  0x001374

/* Common QCA Vendor Subcommands */
#define QCA_NL80211_VENDOR_SUBCMD_LL_STATS_SET          14
#define QCA_NL80211_VENDOR_SUBCMD_LL_STATS_GET          15
#define QCA_NL80211_VENDOR_SUBCMD_LL_STATS_CLR          16
#define QCA_NL80211_VENDOR_SUBCMD_GET_LOGGER_FEATURE    17
#define QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_INFO         61
#define QCA_NL80211_VENDOR_SUBCMD_ROAM                  64
#define QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_CONFIG       74
#define QCA_NL80211_VENDOR_SUBCMD_PACKET_FILTER         83
#define QCA_NL80211_VENDOR_SUBCMD_GET_BUS_SIZE          139
#define QCA_NL80211_VENDOR_SUBCMD_GET_WAKE_REASON_STATS 171
#define QCA_NL80211_VENDOR_SUBCMD_SET_SAR_LIMITS        172

/* QCA Vendor Attributes */
enum qca_wlan_vendor_attr {
	QCA_WLAN_VENDOR_ATTR_INVALID = 0,
	QCA_WLAN_VENDOR_ATTR_ROAMING_POLICY = 1,
	QCA_WLAN_VENDOR_ATTR_MAC_ADDR = 6,
	QCA_WLAN_VENDOR_ATTR_FEATURE_FLAGS = 7,
	QCA_WLAN_VENDOR_ATTR_TEST = 8,
	QCA_WLAN_VENDOR_ATTR_ROAM_RSSI_THRESHOLD = 10,
};

/* Global state */
static struct {
	struct nl_sock *sock;
	int nl80211_id;
	int ifindex;
	int verbose;
	int wait_for_response;
} g_state;

/* Response handler */
static int response_handler(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	
	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
	          genlmsg_attrlen(gnlh, 0), NULL);
	
	if (tb[NL80211_ATTR_VENDOR_DATA]) {
		void *data = nla_data(tb[NL80211_ATTR_VENDOR_DATA]);
		int len = nla_len(tb[NL80211_ATTR_VENDOR_DATA]);
		
		printf("Response received (%d bytes):\n", len);
		
		/* Hex dump */
		for (int i = 0; i < len; i++) {
			printf("%02x ", ((unsigned char *)data)[i]);
			if ((i + 1) % 16 == 0)
				printf("\n");
		}
		if (len % 16 != 0)
			printf("\n");
	}
	
	return NL_OK;
}

/* Error handler */
static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg)
{
	int *ret = arg;
	*ret = err->error;
	
	if (err->error < 0)
		fprintf(stderr, "Error: %s (%d)\n", strerror(-err->error), err->error);
	
	return NL_STOP;
}

/* Finish handler */
static int finish_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}

/* ACK handler */
static int ack_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_STOP;
}

/* Initialize netlink */
static int init_nl80211(const char *ifname)
{
	int ret;
	
	/* Allocate socket */
	g_state.sock = nl_socket_alloc();
	if (!g_state.sock) {
		fprintf(stderr, "Failed to allocate netlink socket\n");
		return -1;
	}
	
	/* Connect to generic netlink */
	ret = genl_connect(g_state.sock);
	if (ret < 0) {
		fprintf(stderr, "Failed to connect to generic netlink: %s\n",
		        nl_geterror(ret));
		nl_socket_free(g_state.sock);
		return -1;
	}
	
	/* Resolve nl80211 family */
	g_state.nl80211_id = genl_ctrl_resolve(g_state.sock, "nl80211");
	if (g_state.nl80211_id < 0) {
		fprintf(stderr, "nl80211 not found: %s\n",
		        nl_geterror(g_state.nl80211_id));
		nl_socket_free(g_state.sock);
		return -1;
	}
	
	/* Get interface index */
	g_state.ifindex = if_nametoindex(ifname);
	if (g_state.ifindex == 0) {
		fprintf(stderr, "Interface %s not found: %s\n",
		        ifname, strerror(errno));
		nl_socket_free(g_state.sock);
		return -1;
	}
	
	if (g_state.verbose)
		printf("Initialized: nl80211_id=%d ifindex=%d\n",
		       g_state.nl80211_id, g_state.ifindex);
	
	return 0;
}

/* Send vendor command */
static int send_vendor_command(uint32_t subcmd, const void *data, size_t data_len)
{
	struct nl_msg *msg;
	struct nl_cb *cb;
	int ret = -1;
	int err;
	
	/* Allocate message */
	msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "Failed to allocate message\n");
		return -1;
	}
	
	/* Build message */
	genlmsg_put(msg, 0, 0, g_state.nl80211_id, 0, 0,
	            NL80211_CMD_VENDOR, 0);
	
	nla_put_u32(msg, NL80211_ATTR_IFINDEX, g_state.ifindex);
	nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, QCA_NL80211_VENDOR_ID);
	nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD, subcmd);
	
	if (data && data_len > 0)
		nla_put(msg, NL80211_ATTR_VENDOR_DATA, data_len, data);
	
	/* Set up callback */
	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb) {
		fprintf(stderr, "Failed to allocate callback\n");
		nlmsg_free(msg);
		return -1;
	}
	
	err = 1;
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, response_handler, NULL);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &err);
	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);
	
	/* Send message */
	ret = nl_send_auto(g_state.sock, msg);
	if (ret < 0) {
		fprintf(stderr, "Failed to send message: %s\n", nl_geterror(ret));
		goto out;
	}
	
	if (g_state.verbose)
		printf("Sent vendor command: subcmd=0x%x data_len=%zu\n",
		       subcmd, data_len);
	
	/* Wait for response if requested */
	if (g_state.wait_for_response) {
		while (err > 0)
			nl_recvmsgs(g_state.sock, cb);
	}
	
	ret = err;
	
out:
	nl_cb_put(cb);
	nlmsg_free(msg);
	return ret;
}

/* Command: Get link layer statistics */
static int cmd_get_stats(void)
{
	struct {
		uint32_t req_id;
		uint32_t req_mask;
	} __attribute__((packed)) stats_req = {
		.req_id = 1,
		.req_mask = 0x7  /* Radio + Iface + Peer */
	};
	
	printf("Getting link layer statistics...\n");
	g_state.wait_for_response = 1;
	
	return send_vendor_command(QCA_NL80211_VENDOR_SUBCMD_LL_STATS_GET,
	                           &stats_req, sizeof(stats_req));
}

/* Command: Get WiFi info */
static int cmd_get_info(void)
{
	printf("Getting WiFi info...\n");
	g_state.wait_for_response = 1;
	
	return send_vendor_command(QCA_NL80211_VENDOR_SUBCMD_GET_WIFI_INFO,
	                           NULL, 0);
}

/* Command: Get logger features */
static int cmd_get_logger_features(void)
{
	printf("Getting logger features...\n");
	g_state.wait_for_response = 1;
	
	return send_vendor_command(QCA_NL80211_VENDOR_SUBCMD_GET_LOGGER_FEATURE,
	                           NULL, 0);
}

/* Command: Set roaming RSSI threshold */
static int cmd_set_roam_rssi(int rssi_threshold)
{
	struct nlattr *attr_data;
	struct nl_msg *msg;
	int ret;
	
	printf("Setting roaming RSSI threshold to %d dBm...\n", rssi_threshold);
	
	/* Build nested attributes */
	msg = nlmsg_alloc();
	if (!msg)
		return -1;
	
	attr_data = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!attr_data) {
		nlmsg_free(msg);
		return -1;
	}
	
	nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_ROAM_RSSI_THRESHOLD, rssi_threshold);
	nla_nest_end(msg, attr_data);
	
	/* Extract vendor data */
	void *data = nla_data(attr_data);
	size_t data_len = nla_len(attr_data);
	
	ret = send_vendor_command(QCA_NL80211_VENDOR_SUBCMD_ROAM,
	                          data, data_len);
	
	nlmsg_free(msg);
	return ret;
}

/* Command: Clear statistics */
static int cmd_clear_stats(void)
{
	struct {
		uint32_t stats_clear_req_mask;
		uint32_t stop_req;
	} __attribute__((packed)) clear_req = {
		.stats_clear_req_mask = 0x7,
		.stop_req = 0
	};
	
	printf("Clearing statistics...\n");
	
	return send_vendor_command(QCA_NL80211_VENDOR_SUBCMD_LL_STATS_CLR,
	                           &clear_req, sizeof(clear_req));
}

/* Command: Send raw vendor command */
static int cmd_vendor_raw(uint32_t subcmd, const char *hex_data)
{
	unsigned char data[1024];
	size_t data_len = 0;
	
	/* Parse hex data if provided */
	if (hex_data) {
		const char *p = hex_data;
		while (*p && data_len < sizeof(data)) {
			unsigned int byte;
			if (sscanf(p, "%2x", &byte) != 1)
				break;
			data[data_len++] = byte;
			p += 2;
			while (*p == ' ' || *p == ':')
				p++;
		}
	}
	
	printf("Sending raw vendor command: subcmd=0x%x data_len=%zu\n",
	       subcmd, data_len);
	
	g_state.wait_for_response = 1;
	
	return send_vendor_command(subcmd, data_len > 0 ? data : NULL, data_len);
}

/* Command: List available commands */
static void cmd_list_commands(void)
{
	printf("Available commands:\n\n");
	printf("  get-stats              Get link layer statistics\n");
	printf("  get-info               Get WiFi driver info\n");
	printf("  get-logger             Get logger features\n");
	printf("  clear-stats            Clear statistics\n");
	printf("  set-roam-rssi <value>  Set roaming RSSI threshold (dBm)\n");
	printf("  vendor-cmd <subcmd> [hex_data]\n");
	printf("                         Send raw vendor command\n");
	printf("  list-commands          Show this help\n");
	printf("\n");
	printf("Common vendor subcommands:\n");
	printf("  0x0e (14)  - LL_STATS_SET\n");
	printf("  0x0f (15)  - LL_STATS_GET\n");
	printf("  0x10 (16)  - LL_STATS_CLR\n");
	printf("  0x11 (17)  - GET_LOGGER_FEATURE\n");
	printf("  0x3d (61)  - GET_WIFI_INFO\n");
	printf("  0x40 (64)  - ROAM\n");
	printf("  0x4a (74)  - SET_WIFI_CONFIG\n");
	printf("  0x53 (83)  - PACKET_FILTER\n");
	printf("  0xac (172) - SET_SAR_LIMITS\n");
	printf("\n");
	printf("Examples:\n");
	printf("  qca-ctl wlan0 get-stats\n");
	printf("  qca-ctl wlan0 set-roam-rssi 70\n");
	printf("  qca-ctl wlan0 vendor-cmd 0x0f\n");
	printf("  qca-ctl wlan0 vendor-cmd 0x40 01020304\n");
}

/* Usage */
static void usage(const char *prog)
{
	printf("Usage: %s [options] <interface> <command> [args]\n\n", prog);
	printf("Options:\n");
	printf("  -v         Verbose output\n");
	printf("  -h         Show this help\n");
	printf("\n");
	printf("Run '%s <interface> list-commands' for available commands\n", prog);
}

/* Main */
int main(int argc, char *argv[])
{
	const char *ifname, *command;
	int opt, ret = 0;
	
	/* Parse options */
	while ((opt = getopt(argc, argv, "vh")) != -1) {
		switch (opt) {
		case 'v':
			g_state.verbose = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}
	
	/* Check arguments */
	if (optind + 2 > argc) {
		usage(argv[0]);
		return 1;
	}
	
	ifname = argv[optind];
	command = argv[optind + 1];
	
	/* Special case: list-commands doesn't need netlink */
	if (strcmp(command, "list-commands") == 0) {
		cmd_list_commands();
		return 0;
	}
	
	/* Initialize netlink */
	if (init_nl80211(ifname) < 0)
		return 1;
	
	/* Execute command */
	if (strcmp(command, "get-stats") == 0) {
		ret = cmd_get_stats();
	} else if (strcmp(command, "get-info") == 0) {
		ret = cmd_get_info();
	} else if (strcmp(command, "get-logger") == 0) {
		ret = cmd_get_logger_features();
	} else if (strcmp(command, "clear-stats") == 0) {
		ret = cmd_clear_stats();
	} else if (strcmp(command, "set-roam-rssi") == 0) {
		if (optind + 3 > argc) {
			fprintf(stderr, "Missing RSSI threshold value\n");
			ret = 1;
		} else {
			int rssi = atoi(argv[optind + 2]);
			ret = cmd_set_roam_rssi(rssi);
		}
	} else if (strcmp(command, "vendor-cmd") == 0) {
		if (optind + 3 > argc) {
			fprintf(stderr, "Missing vendor subcommand\n");
			ret = 1;
		} else {
			uint32_t subcmd = strtoul(argv[optind + 2], NULL, 0);
			const char *hex_data = (optind + 4 <= argc) ? argv[optind + 3] : NULL;
			ret = cmd_vendor_raw(subcmd, hex_data);
		}
	} else {
		fprintf(stderr, "Unknown command: %s\n", command);
		fprintf(stderr, "Run '%s %s list-commands' for available commands\n",
		        argv[0], ifname);
		ret = 1;
	}
	
	/* Cleanup */
	if (g_state.sock)
		nl_socket_free(g_state.sock);
	
	if (ret == 0)
		printf("Command completed successfully\n");
	else if (ret > 0)
		printf("Command completed with warnings\n");
	
	return ret < 0 ? 1 : 0;
}
