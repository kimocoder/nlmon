/* nlmon - monitor kernel netlink events for interface changes
 *
 * Copyright (C) 2020-2025  Christian Bremvaag <christian@aircrack-ng.org>
 * Copyright (C) 2009-2011  Mårten Wikström <marten.wikstrom@keystream.se>
 * Copyright (C) 2009-2022  Joachim Wiberg <troglobit@gmail.com>
 * Copyright (c) 2015       Tobias Waldekranz <tobias@waldekranz.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <ev.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ncurses.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

/* <linux/if.h>/<net/if.h> can NOT co-exist! */
#define _LINUX_IF_H
#include <linux/if_packet.h>
#include <linux/if_ether.h>

#include <net/if.h>

/* Note: We use libnl-tiny for netlink monitoring, but keep system libnl3
 * for nlmon device creation to avoid conflicts. The create_nlmon_device
 * function uses system libnl3 API. */
#ifdef USE_SYSTEM_LIBNL_FOR_DEVICE_CREATION
#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/route/link.h>
#include <netlink/route/link/veth.h>
#include <netlink/route/route.h>
#include <netlink/route/addr.h>
#include <netlink/route/neighbour.h>
#include <netlink/route/rule.h>
#endif
#include <linux/netlink.h>

/* Memory management and resource tracking */
#include "memory_tracker.h"
#include "resource_tracker.h"
#include "signal_handler.h"
#include "netlink_multi_protocol.h"

/* Forward declarations for nlmon netlink manager (avoid header conflicts) */
struct nlmon_nl_manager;
struct nlmon_nl_manager *nlmon_nl_manager_init(void);
void nlmon_nl_manager_destroy(struct nlmon_nl_manager *mgr);
int nlmon_nl_enable_route(struct nlmon_nl_manager *mgr);
int nlmon_nl_enable_generic(struct nlmon_nl_manager *mgr);
int nlmon_nl_enable_diag(struct nlmon_nl_manager *mgr);
int nlmon_nl_enable_netfilter(struct nlmon_nl_manager *mgr);
int nlmon_nl_get_route_fd(struct nlmon_nl_manager *mgr);
int nlmon_nl_get_genl_fd(struct nlmon_nl_manager *mgr);
int nlmon_nl_get_diag_fd(struct nlmon_nl_manager *mgr);
int nlmon_nl_get_nf_fd(struct nlmon_nl_manager *mgr);
int nlmon_nl_process_route(struct nlmon_nl_manager *mgr);
int nlmon_nl_process_genl(struct nlmon_nl_manager *mgr);
int nlmon_nl_process_diag(struct nlmon_nl_manager *mgr);
int nlmon_nl_process_nf(struct nlmon_nl_manager *mgr);

/* WMI monitoring support */
#include "wmi_log_reader.h"
#include "wmi_event_bridge.h"

/* CLI mode globals */
static int cli_mode = 0;
static WINDOW *main_win = NULL;
static WINDOW *status_win = NULL;
static WINDOW *cmd_win = NULL;
static pthread_mutex_t screen_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_EVENTS 1000
static char event_log[MAX_EVENTS][256];
static int event_count = 0;
static int event_scroll = 0;

/* Statistics */
struct stats {
	unsigned long link_events;
	unsigned long route_events;
	unsigned long addr_events;
	unsigned long neigh_events;
	unsigned long rule_events;
	unsigned long generic_events;
	unsigned long sock_diag_events;
};

static struct stats event_stats = {0};

/* Memory management and resource tracking */
static struct memory_tracker *g_memory_tracker = NULL;
static struct resource_tracker *g_resource_tracker = NULL;
static struct signal_handler *g_signal_handler = NULL;
static struct nlmon_multi_protocol_ctx *g_multi_proto_ctx = NULL;
static struct nlmon_nl_manager *g_nl_manager = NULL;

struct context {
	/* Legacy fields - kept for compatibility but may be unused */
	struct nl_sock        *ns;
	struct nl_cache_mngr  *mngr;
	struct nl_cache       *lcache;
	struct nl_cache       *rcache;
	struct nl_cache       *acache;  /* address cache */
	struct nl_cache       *ncache;  /* neighbor cache */
	struct nl_cache       *rucache; /* rule cache */
};

static int veth_only;
static int monitor_addr = 1;
static int monitor_neigh = 1;
static int monitor_rules = 1;
static int use_nlmon = 0;
static char *nlmon_device = NULL;
static char *pcap_file = NULL;
static int verbose_mode = 0;
static int nlmon_sock = -1;
static int filter_msg_type = -1;  /* -1 means no filter */
static int show_generic_netlink = 0;
static int show_all_protocols = 0;

/* WMI monitoring options */
static int enable_wmi = 0;
static char *wmi_source = NULL;
static int wmi_follow_mode = 0;
static char *wmi_filter_expr = NULL;
static pthread_t wmi_thread;
static int wmi_thread_started = 0;

#define MAX_NETLINK_PACKET_SIZE 65536

/* PCAP file format structures */
struct pcap_file_header {
	uint32_t magic_number;   /* magic number */
	uint16_t version_major;  /* major version number */
	uint16_t version_minor;  /* minor version number */
	int32_t  thiszone;       /* GMT to local correction */
	uint32_t sigfigs;        /* accuracy of timestamps */
	uint32_t snaplen;        /* max length of captured packets, in octets */
	uint32_t network;        /* data link type */
};

struct pcap_packet_header {
	uint32_t ts_sec;         /* timestamp seconds */
	uint32_t ts_usec;        /* timestamp microseconds */
	uint32_t incl_len;       /* number of octets of packet saved in file */
	uint32_t orig_len;       /* actual length of packet */
};

static FILE *pcap_fp = NULL;

static void log_event(const char *msg)
{
	time_t now;
	struct tm *tm_info;
	char timestamp[20];
	char formatted_msg[256];

	time(&now);
	tm_info = localtime(&now);
	strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

	snprintf(formatted_msg, sizeof(formatted_msg), "[%s] %s", timestamp, msg);

	if (cli_mode) {
		pthread_mutex_lock(&screen_mutex);
		if (event_count < MAX_EVENTS) {
			snprintf(event_log[event_count], sizeof(event_log[0]), "%s", formatted_msg);
			event_count++;
		} else {
			/* Shift events up */
			memmove(event_log[0], event_log[1], sizeof(event_log[0]) * (MAX_EVENTS - 1));
			snprintf(event_log[MAX_EVENTS - 1], sizeof(event_log[0]), "%s", formatted_msg);
		}
		pthread_mutex_unlock(&screen_mutex);
	} else {
		warnx("%s", msg);
	}
}

/* nlmon device management */
static int create_nlmon_device(const char *dev_name)
{
	/* TODO: Reimplement using raw netlink or system libnl3 in separate compilation unit
	 * to avoid header conflicts with libnl-tiny. For now, nlmon device creation is disabled.
	 * Users should manually create the nlmon device using:
	 *   sudo modprobe nlmon
	 *   sudo ip link add nlmon0 type nlmon
	 *   sudo ip link set nlmon0 up
	 */
	warnx("Automatic nlmon device creation is temporarily disabled.");
	warnx("Please manually create the nlmon device:");
	warnx("  sudo modprobe nlmon");
	warnx("  sudo ip link add %s type nlmon", dev_name);
	warnx("  sudo ip link set %s up", dev_name);
	return -1;
}

static int bind_nlmon_socket(const char *dev_name)
{
	struct sockaddr_ll sll;
	struct ifreq ifr;
	int sock;

	/* Create raw socket */
	sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sock < 0) {
		warn("Failed to create raw socket");
		return -1;
	}

	/* Get interface index */
	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';
	if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
		warn("Failed to get interface index for %s", dev_name);
		close(sock);
		return -1;
	}

	/* Bind to nlmon device */
	memset(&sll, 0, sizeof(sll));
	sll.sll_family = AF_PACKET;
	sll.sll_ifindex = ifr.ifr_ifindex;
	sll.sll_protocol = htons(ETH_P_ALL);

	if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
		warn("Failed to bind to nlmon device %s", dev_name);
		close(sock);
		return -1;
	}

	if (verbose_mode) {
		char msg[256];
		snprintf(msg, sizeof(msg), "Bound to nlmon device %s (ifindex=%d)", 
		         dev_name, ifr.ifr_ifindex);
		log_event(msg);
	}

	return sock;
}

static int init_pcap_file(const char *filename)
{
	struct pcap_file_header fh;

	pcap_fp = fopen(filename, "wb");
	if (!pcap_fp) {
		warn("Failed to open pcap file %s", filename);
		return -1;
	}

	/* Write PCAP file header */
	fh.magic_number = 0xa1b2c3d4;
	fh.version_major = 2;
	fh.version_minor = 4;
	fh.thiszone = 0;
	fh.sigfigs = 0;
	fh.snaplen = 65535;
	fh.network = 253;  /* DLT_NETLINK for netlink messages */

	if (fwrite(&fh, sizeof(fh), 1, pcap_fp) != 1) {
		warn("Failed to write pcap header");
		fclose(pcap_fp);
		pcap_fp = NULL;
		return -1;
	}

	fflush(pcap_fp);

	if (verbose_mode) {
		char msg[256];
		snprintf(msg, sizeof(msg), "Writing netlink packets to %s", filename);
		log_event(msg);
	}

	return 0;
}

static void write_pcap_packet(const unsigned char *data, size_t len)
{
	struct pcap_packet_header ph;
	struct timeval tv;

	if (!pcap_fp)
		return;

	gettimeofday(&tv, NULL);

	ph.ts_sec = tv.tv_sec;
	ph.ts_usec = tv.tv_usec;
	ph.incl_len = len;
	ph.orig_len = len;

	if (fwrite(&ph, sizeof(ph), 1, pcap_fp) != 1) {
		warn("Failed to write pcap packet header");
		return;
	}

	if (fwrite(data, 1, len, pcap_fp) != len) {
		warn("Failed to write pcap packet data");
		return;
	}

	fflush(pcap_fp);
}

/* Generic netlink event callback */
static void genetlink_event_cb(nlmon_event_type_t type, void *data, void *user_data)
{
	char buf[256];

	(void)user_data;

	switch (type) {
	case NLMON_EVENT_GENERIC: {
		struct nlmon_generic_msg *msg = data;
		snprintf(buf, sizeof(buf), "genetlink: family=%s cmd=%u version=%u",
		         msg->family_name, msg->cmd, msg->version);
		event_stats.generic_events++;
		log_event(buf);
		break;
	}
	case NLMON_EVENT_SOCK_DIAG: {
		struct nlmon_sock_diag *diag = data;
		snprintf(buf, sizeof(buf), "sock_diag: %s:%u -> %s:%u state=%u proto=%u",
		         diag->src_addr, diag->src_port,
		         diag->dst_addr, diag->dst_port,
		         diag->state, diag->protocol);
		event_stats.sock_diag_events++;
		log_event(buf);
		break;
	}
	default:
		break;
	}
}

/* Simple WMI filter matching function */
static int wmi_filter_match(const struct wmi_log_entry *entry, const char *filter_expr)
{
	char key[64], value[128];
	const char *eq_pos;

	if (!filter_expr || !entry)
		return 1;  /* No filter means match all */

	/* Parse simple "key=value" filter expression */
	eq_pos = strchr(filter_expr, '=');
	if (!eq_pos)
		return 1;  /* Invalid filter, match all */

	/* Extract key and value */
	size_t key_len = eq_pos - filter_expr;
	if (key_len >= sizeof(key))
		return 1;

	strncpy(key, filter_expr, key_len);
	key[key_len] = '\0';

	strncpy(value, eq_pos + 1, sizeof(value) - 1);
	value[sizeof(value) - 1] = '\0';

	/* Match against WMI fields */
	if (strcmp(key, "wmi.cmd") == 0) {
		return (strcmp(entry->command_name, value) == 0);
	} else if (strcmp(key, "wmi.vdev") == 0) {
		char vdev_str[32];
		snprintf(vdev_str, sizeof(vdev_str), "%u", entry->vdev_id);
		return (strcmp(vdev_str, value) == 0);
	} else if (strcmp(key, "wmi.stats") == 0) {
		return (strcmp(entry->stats_type, value) == 0);
	} else if (strcmp(key, "wmi.peer") == 0) {
		return (strcmp(entry->peer_mac, value) == 0);
	}

	/* Unknown key, match all */
	return 1;
}

/* WMI log line callback */
static int wmi_log_line_cb(const char *line, void *user_data)
{
	struct wmi_log_entry entry;
	char buf[512];
	int ret;

	(void)user_data;

	/* Parse the WMI log line */
	ret = wmi_parse_log_line(line, &entry);
	if (ret < 0) {
		/* Parsing failed - skip this line silently unless verbose */
		if (verbose_mode > 1) {
			snprintf(buf, sizeof(buf), "wmi: failed to parse line");
			log_event(buf);
		}
		return 0;  /* Continue processing */
	}
	
	/* Apply WMI filter if specified */
	if (wmi_filter_expr && !wmi_filter_match(&entry, wmi_filter_expr)) {
		return 0;  /* Filtered out */
	}
	
	/* Format and log the WMI event */
	ret = wmi_format_entry(&entry, buf, sizeof(buf));
	if (ret > 0) {
		log_event(buf);
		
		/* Submit to event bridge if available */
		wmi_bridge_submit(&entry);
	}
	
	return 0;
}

/* WMI reader thread function */
static void *wmi_reader_thread(void *arg)
{
	(void)arg;
	
	if (verbose_mode) {
		log_event("WMI reader thread started");
	}
	
	/* Start reading WMI logs - this blocks until stopped or EOF */
	wmi_log_reader_start();
	
	if (verbose_mode) {
		log_event("WMI reader thread stopped");
	}
	
	return NULL;
}

/* Generic netlink I/O callback */
static void genetlink_io_cb(struct ev_loop *loop, ev_io *w, int revents)
{
	(void)loop;
	(void)revents;
	
	nlmon_protocol_t proto = (nlmon_protocol_t)(intptr_t)w->data;
	
	if (g_multi_proto_ctx) {
		nlmon_multi_protocol_process(g_multi_proto_ctx, proto);
	}
}

static void nlmon_packet_cb(struct ev_loop *loop, ev_io *w, int revents)
{
	unsigned char buffer[MAX_NETLINK_PACKET_SIZE];
	ssize_t len;
	struct nlmsghdr *nlh;
	char msg[512];
	char msg_type_buf[32];
	static unsigned long packet_count = 0;
	const char *msg_type_str = "UNKNOWN";
	
	len = recv(nlmon_sock, buffer, sizeof(buffer), 0);
	if (len < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			warn("Failed to receive from nlmon socket");
		return;
	}
	
	if (len == 0)
		return;
	
	packet_count++;
	
	/* Parse netlink message */
	if (len >= (ssize_t)sizeof(struct nlmsghdr)) {
		nlh = (struct nlmsghdr *)buffer;
		
		/* Apply message type filter if set */
		if (filter_msg_type >= 0 && nlh->nlmsg_type != (unsigned)filter_msg_type)
			return;
		
		/* Determine message type description */
		switch (nlh->nlmsg_type) {
		case 0: msg_type_str = "NLMSG_NOOP"; break;
		case 1: msg_type_str = "NLMSG_ERROR"; break;
		case 2: msg_type_str = "NLMSG_DONE"; break;
		case 3: msg_type_str = "NLMSG_OVERRUN"; break;
		case 16: msg_type_str = "RTM_NEWLINK"; break;
		case 17: msg_type_str = "RTM_DELLINK"; break;
		case 18: msg_type_str = "RTM_GETLINK"; break;
		case 19: msg_type_str = "RTM_SETLINK"; break;
		case 20: msg_type_str = "RTM_NEWADDR"; break;
		case 21: msg_type_str = "RTM_DELADDR"; break;
		case 22: msg_type_str = "RTM_GETADDR"; break;
		case 24: msg_type_str = "RTM_NEWROUTE"; break;
		case 25: msg_type_str = "RTM_DELROUTE"; break;
		case 26: msg_type_str = "RTM_GETROUTE"; break;
		case 28: msg_type_str = "RTM_NEWNEIGH"; break;
		case 29: msg_type_str = "RTM_DELNEIGH"; break;
		case 30: msg_type_str = "RTM_GETNEIGH"; break;
		case 32: msg_type_str = "RTM_NEWRULE"; break;
		case 33: msg_type_str = "RTM_DELRULE"; break;
		case 34: msg_type_str = "RTM_GETRULE"; break;
		default:
			if (nlh->nlmsg_type >= 16)
				snprintf(msg_type_buf, sizeof(msg_type_buf), "RTM_%u", nlh->nlmsg_type);
			else
				snprintf(msg_type_buf, sizeof(msg_type_buf), "TYPE_%u", nlh->nlmsg_type);
			msg_type_str = msg_type_buf;
			break;
		}
	}
	
	/* Write to PCAP file if enabled */
	if (pcap_fp)
		write_pcap_packet(buffer, len);
	
	/* Log netlink message if verbose */
	if (verbose_mode && len >= (ssize_t)sizeof(struct nlmsghdr)) {
		nlh = (struct nlmsghdr *)buffer;
		snprintf(msg, sizeof(msg), 
		         "nlmon: pkt #%lu, len=%zd, %s, flags=0x%x, seq=%u, pid=%u",
		         packet_count, len, msg_type_str, nlh->nlmsg_flags, 
		         nlh->nlmsg_seq, nlh->nlmsg_pid);
		log_event(msg);
	}
}

/* Legacy cache-based callbacks - no longer used with new netlink manager
 * These functions used the old libnl3 cache manager approach.
 * The new implementation uses libnl-tiny with direct message processing.
 * Keeping these commented out for reference during migration.
 */
#if 0
static void addr_change_cb(struct nl_cache *acache,
			   struct nl_object *obj, int action, void *arg)
{
	/* ... legacy implementation ... */
}

static void neigh_change_cb(struct nl_cache *ncache,
			    struct nl_object *obj, int action, void *arg)
{
	/* ... legacy implementation ... */
}

static void rule_change_cb(struct nl_cache *rucache,
			   struct nl_object *obj, int action, void *arg)
{
	/* ... legacy implementation ... */
}

static void route_change_cb(struct nl_cache *rcache,
			    struct nl_object *obj, int action, void *arg)
{
	/* ... legacy implementation ... */
}

static void link_change_cb(struct nl_cache *lcache,
			   struct nl_object *obj, int action, void *arg)
{
	/* ... legacy implementation ... */
}
#endif /* Legacy cache callbacks */

static void nlroute_cb(struct ev_loop *loop, ev_io *w, int revents)
{
	(void)loop;
	(void)w;
	(void)revents;

	/* Process NETLINK_ROUTE messages using new manager */
	if (g_nl_manager) {
		nlmon_nl_process_route(g_nl_manager);
	}
}

static void nlgenl_cb(struct ev_loop *loop, ev_io *w, int revents)
{
	(void)loop;
	(void)w;
	(void)revents;

	/* Process NETLINK_GENERIC messages using new manager */
	if (g_nl_manager) {
		nlmon_nl_process_genl(g_nl_manager);
	}
}

static void nldiag_cb(struct ev_loop *loop, ev_io *w, int revents)
{
	(void)loop;
	(void)w;
	(void)revents;

	/* Process NETLINK_SOCK_DIAG messages using new manager */
	if (g_nl_manager) {
		nlmon_nl_process_diag(g_nl_manager);
	}
}

static void nlnf_cb(struct ev_loop *loop, ev_io *w, int revents)
{
	(void)loop;
	(void)w;
	(void)revents;

	/* Process NETLINK_NETFILTER messages using new manager */
	if (g_nl_manager) {
		nlmon_nl_process_nf(g_nl_manager);
	}
}

/* Removed old cache-based reconf iterator functions - no longer needed with new netlink manager */

static void refresh_cli_display(void)
{
	int i, start_idx, display_lines;
	int row, col;
	
	if (!cli_mode || !main_win)
		return;
	
	pthread_mutex_lock(&screen_mutex);
	
	/* Clear main window */
	werase(main_win);
	box(main_win, 0, 0);
	mvwprintw(main_win, 0, 2, " Network Events ");
	
	/* Calculate how many lines we can display */
	getmaxyx(main_win, row, col);
	display_lines = row - 2; /* Account for border */
	
	/* Calculate starting index for scrolling */
	start_idx = event_count - display_lines - event_scroll;
	if (start_idx < 0)
		start_idx = 0;
	
	/* Display events */
	for (i = 0; i < display_lines && (start_idx + i) < event_count; i++) {
		mvwprintw(main_win, i + 1, 2, "%.*s", col - 4, event_log[start_idx + i]);
	}
	
	wrefresh(main_win);
	
	/* Update status window */
	werase(status_win);
	box(status_win, 0, 0);
	mvwprintw(status_win, 0, 2, " Statistics ");
	mvwprintw(status_win, 1, 2, "Links: %-8lu Routes: %-8lu Addrs: %-8lu Genl: %-8lu", 
	          event_stats.link_events, event_stats.route_events, event_stats.addr_events,
	          event_stats.generic_events);
	mvwprintw(status_win, 2, 2, "Neigh: %-8lu Rules:  %-8lu Diag:  %-8lu Total: %-8lu",
	          event_stats.neigh_events, event_stats.rule_events, event_stats.sock_diag_events,
	          event_stats.link_events + event_stats.route_events + 
	          event_stats.addr_events + event_stats.neigh_events + event_stats.rule_events +
	          event_stats.generic_events + event_stats.sock_diag_events);
	wrefresh(status_win);
	
	/* Update command window */
	werase(cmd_win);
	box(cmd_win, 0, 0);
	mvwprintw(cmd_win, 0, 2, " Commands ");
	mvwprintw(cmd_win, 1, 2, "q:Quit  h:Help  c:Clear  UP/DOWN:Scroll  a:Addr  n:Neigh  r:Rules");
	wrefresh(cmd_win);
	
	pthread_mutex_unlock(&screen_mutex);
}

static void init_cli(void)
{
	int max_y, max_x;
	
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);
	curs_set(0);
	
	getmaxyx(stdscr, max_y, max_x);
	
	/* Main event window */
	main_win = newwin(max_y - 7, max_x, 0, 0);
	
	/* Status window */
	status_win = newwin(4, max_x, max_y - 7, 0);
	
	/* Command window */
	cmd_win = newwin(3, max_x, max_y - 3, 0);
	
	refresh_cli_display();
}

static void cleanup_cli(void)
{
	if (main_win)
		delwin(main_win);
	if (status_win)
		delwin(status_win);
	if (cmd_win)
		delwin(cmd_win);
	endwin();
}

/* Cleanup memory management and resource tracking */
static void cleanup_memory_management(void)
{
	/* Cleanup WMI monitoring */
	if (enable_wmi) {
		if (wmi_thread_started) {
			/* Stop WMI reader */
			wmi_log_reader_stop();
			
			/* Wait for thread to finish */
			pthread_join(wmi_thread, NULL);
			wmi_thread_started = 0;
		}
		
		/* Cleanup WMI components */
		wmi_log_reader_cleanup();
		wmi_bridge_cleanup();
	}
	
	/* Dump memory statistics if tracker is enabled */
	if (g_memory_tracker) {
		memory_tracker_update_system_stats(g_memory_tracker);
		if (verbose_mode) {
			fprintf(stderr, "\n");
			memory_tracker_dump(g_memory_tracker, STDERR_FILENO);
		}
		memory_tracker_destroy(g_memory_tracker);
		g_memory_tracker = NULL;
	}
	
	/* Cleanup all tracked resources */
	if (g_resource_tracker) {
		if (verbose_mode) {
			struct resource_stats stats;
			if (resource_tracker_get_stats(g_resource_tracker, &stats)) {
				fprintf(stderr, "Resource statistics:\n");
				fprintf(stderr, "  Events processed: %lu\n", stats.events_processed);
				fprintf(stderr, "  Events dropped: %lu\n", stats.events_dropped);
				fprintf(stderr, "  Peak memory RSS: %lu bytes\n", stats.memory_peak_rss_bytes);
			}
		}
		resource_tracker_destroy(g_resource_tracker);
		g_resource_tracker = NULL;
	}
	
	/* Cleanup signal handler */
	if (g_signal_handler) {
		signal_handler_cleanup(g_signal_handler);
		g_signal_handler = NULL;
	}
	
	/* Cleanup multi-protocol context */
	if (g_multi_proto_ctx) {
		nlmon_multi_protocol_destroy(g_multi_proto_ctx);
		g_multi_proto_ctx = NULL;
	}
}

static void show_help(void)
{
	WINDOW *help_win;
	int max_y, max_x;
	
	getmaxyx(stdscr, max_y, max_x);
	help_win = newwin(max_y - 4, max_x - 4, 2, 2);
	
	box(help_win, 0, 0);
	mvwprintw(help_win, 0, 2, " Help ");
	mvwprintw(help_win, 2, 4, "nlmon - Network Link Monitor");
	mvwprintw(help_win, 3, 4, "============================");
	mvwprintw(help_win, 5, 4, "Keys:");
	mvwprintw(help_win, 6, 6, "q       - Quit application");
	mvwprintw(help_win, 7, 6, "h       - Show this help");
	mvwprintw(help_win, 8, 6, "c       - Clear event log");
	mvwprintw(help_win, 9, 6, "UP      - Scroll up");
	mvwprintw(help_win, 10, 6, "DOWN    - Scroll down");
	mvwprintw(help_win, 11, 6, "a       - Toggle address monitoring");
	mvwprintw(help_win, 12, 6, "n       - Toggle neighbor monitoring");
	mvwprintw(help_win, 13, 6, "r       - Toggle rules monitoring");
	mvwprintw(help_win, 15, 4, "Monitoring:");
	mvwprintw(help_win, 16, 6, "- Link changes (interfaces up/down)");
	mvwprintw(help_win, 17, 6, "- Route changes (default route)");
	mvwprintw(help_win, 18, 6, "- Address changes (IP addresses)");
	mvwprintw(help_win, 19, 6, "- Neighbor changes (ARP table)");
	mvwprintw(help_win, 20, 6, "- Rule changes (routing rules)");
	mvwprintw(help_win, 22, 4, "Press any key to close...");
	
	wrefresh(help_win);
	nodelay(stdscr, FALSE);
	getch();
	nodelay(stdscr, TRUE);
	delwin(help_win);
	refresh_cli_display();
}

static void handle_cli_input(void)
{
	int ch;
	char msg[256];
	
	if (!cli_mode)
		return;
	
	ch = getch();
	if (ch == ERR)
		return;
	
	switch (ch) {
	case 'q':
	case 'Q':
		cleanup_cli();
		/* Cleanup netlink manager */
		if (g_nl_manager) {
			nlmon_nl_manager_destroy(g_nl_manager);
			g_nl_manager = NULL;
		}
		/* Cleanup nlmon resources */
		if (nlmon_sock >= 0)
			close(nlmon_sock);
		if (pcap_fp)
			fclose(pcap_fp);
		cleanup_memory_management();
		exit(0);
		break;
	case 'h':
	case 'H':
		show_help();
		break;
	case 'c':
	case 'C':
		pthread_mutex_lock(&screen_mutex);
		event_count = 0;
		event_scroll = 0;
		pthread_mutex_unlock(&screen_mutex);
		refresh_cli_display();
		break;
	case KEY_UP:
		if (event_scroll < event_count - 1)
			event_scroll++;
		refresh_cli_display();
		break;
	case KEY_DOWN:
		if (event_scroll > 0)
			event_scroll--;
		refresh_cli_display();
		break;
	case 'a':
	case 'A':
		monitor_addr = !monitor_addr;
		snprintf(msg, sizeof(msg), "Address monitoring %s", monitor_addr ? "enabled" : "disabled");
		log_event(msg);
		refresh_cli_display();
		break;
	case 'n':
	case 'N':
		monitor_neigh = !monitor_neigh;
		snprintf(msg, sizeof(msg), "Neighbor monitoring %s", monitor_neigh ? "enabled" : "disabled");
		log_event(msg);
		refresh_cli_display();
		break;
	case 'r':
	case 'R':
		monitor_rules = !monitor_rules;
		snprintf(msg, sizeof(msg), "Rules monitoring %s", monitor_rules ? "enabled" : "disabled");
		log_event(msg);
		refresh_cli_display();
		break;
	}
}

static void cli_update_cb(struct ev_loop *loop, ev_timer *w, int revents)
{
	refresh_cli_display();
	handle_cli_input();
}

/* reconf */
static void sighub_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
	(void)loop;
	(void)w;
	(void)revents;

	/* 
	 * With the new netlink manager, we don't use caches.
	 * SIGHUP could be used to reload configuration in the future.
	 */
	if (verbose_mode) {
		log_event("SIGHUP received");
	}
}

static void sigint_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
	if (cli_mode)
		cleanup_cli();
	ev_unloop(loop, EVUNLOOP_ALL);
}

static int init(struct context *ctx)
{
	(void)ctx;  /* Context no longer used with new netlink manager */

	/* 
	 * With the new nlmon_nl_manager, we don't use caches.
	 * Messages are processed directly via callbacks set up in the manager.
	 * The old cache-based approach has been replaced with direct message processing.
	 */

	if (verbose_mode) {
		log_event("Netlink manager initialized successfully");
	}

	return 0;
}

static int usage(int rc)
{
	printf("Usage: nlmon [-h?vciau] [-m device] [-p file] [-V] [-f type] [-g] [-w source] [-W expr]\n"
	       "\n"
	       "Options:\n"
	       "  -h    This help text\n"
	       "  -v    Show only events on VETH interfaces\n"
	       "  -c    Enable CLI mode (interactive ncurses interface)\n"
	       "  -i    Disable address monitoring\n"
	       "  -a    Disable neighbor/ARP monitoring\n"
	       "  -u    Disable rules monitoring\n"
	       "  -m    Bind to nlmon device (e.g., -m nlmon0) for raw packet capture\n"
	       "  -p    Write captured netlink packets to PCAP file (requires -m)\n"
	       "  -V    Verbose mode - show detailed netlink message information\n"
	       "  -f    Filter by netlink message type (e.g., -f 16 for RTM_NEWLINK)\n"
	       "  -g    Enable NETLINK_GENERIC protocol monitoring\n"
	       "  -A    Monitor all netlink protocols (not just NETLINK_ROUTE)\n"
	       "  -w    Enable WMI log monitoring from <source> (file path, '-' for stdin, or 'follow:path')\n"
	       "  -W    Filter WMI events with expression (e.g., 'wmi.cmd=REQUEST_STATS')\n"
	       "\n"
	       "nlmon Kernel Module Support:\n"
	       "  The -m option enables binding to a virtual nlmon network device to capture\n"
	       "  all netlink messages as raw packets. This requires the nlmon kernel module.\n"
	       "  \n"
	       "  Example: nlmon -m nlmon0 -p netlink.pcap -V\n"
	       "  Example: nlmon -m nlmon0 -V -f 16  # Filter only RTM_NEWLINK messages\n"
	       "  \n"
	       "  To manually create an nlmon device:\n"
	       "    sudo modprobe nlmon\n"
	       "    sudo ip link add nlmon0 type nlmon\n"
	       "    sudo ip link set nlmon0 up\n"
	       "\n"
	       "WMI Monitoring:\n"
	       "  The -w option enables monitoring of Qualcomm WMI commands from device logs.\n"
	       "  \n"
	       "  Example: nlmon -w /var/log/wlan.log\n"
	       "  Example: cat device.log | nlmon -w -\n"
	       "  Example: nlmon -w follow:/var/log/wlan.log  # Follow mode (like tail -f)\n"
	       "  Example: nlmon -g -w /var/log/wlan.log      # Combine with netlink monitoring\n"
	       "  Example: nlmon -w /var/log/wlan.log -W 'wmi.cmd=REQUEST_LINK_STATS'\n"
	       "\n"
	       "Common Message Types:\n"
	       "  RTM_NEWLINK=16, RTM_DELLINK=17, RTM_NEWADDR=20, RTM_DELADDR=21\n"
	       "  RTM_NEWROUTE=24, RTM_DELROUTE=25, RTM_NEWNEIGH=28, RTM_DELNEIGH=29\n"
	       "  RTM_NEWRULE=32, RTM_DELRULE=33\n"
	       "\n"
	       "CLI Mode Keys:\n"
	       "  q       Quit\n"
	       "  h       Help\n"
	       "  c       Clear event log\n"
	       "  UP/DOWN Scroll through events\n"
	       "  a       Toggle address monitoring\n"
	       "  n       Toggle neighbor monitoring\n"
	       "  r       Toggle rules monitoring\n"
	       "\n");

	return rc;
}

int main(int argc, char *argv[])
{
	struct ev_loop *loop;
	struct context ctx;
	ev_signal intw;
	ev_signal hupw;
	ev_io io;
	ev_io nlmon_io;
	ev_timer cli_timer;
	int err;
	int fd;
	int c;

	/* Initialize context */
	memset(&ctx, 0, sizeof(ctx));
	
	/* Initialize memory management and resource tracking */
	g_memory_tracker = memory_tracker_create(true);
	if (!g_memory_tracker) {
		warnx("Failed to create memory tracker");
	}
	
	g_resource_tracker = resource_tracker_create(true);
	if (!g_resource_tracker) {
		warnx("Failed to create resource tracker");
	}
	
	g_signal_handler = signal_handler_init();
	if (!g_signal_handler) {
		warnx("Failed to initialize signal handler");
	}

	while ((c = getopt(argc, argv, "h?vciauVm:p:f:gAw:W:")) != EOF) {
		switch (c) {
		case 'h':
		case '?':
			return usage(0);

		case 'v':
			veth_only = 1;
			break;

		case 'c':
			cli_mode = 1;
			break;

		case 'i':
			monitor_addr = 0;
			break;

		case 'a':
			monitor_neigh = 0;
			break;

		case 'u':
			monitor_rules = 0;
			break;
			
		case 'V':
			verbose_mode = 1;
			break;
			
		case 'm':
			use_nlmon = 1;
			nlmon_device = optarg;
			break;
			
		case 'p':
			pcap_file = optarg;
			break;
			
		case 'f':
			{
				char *endptr;
				long val = strtol(optarg, &endptr, 10);
				if (*endptr != '\0' || val < 0 || val > 65535) {
					warnx("Invalid message type filter: %s", optarg);
					return usage(1);
				}
				filter_msg_type = (int)val;
			}
			break;
			
		case 'g':
			show_generic_netlink = 1;
			break;
			
		case 'A':
			show_all_protocols = 1;
			break;
			
		case 'w':
			enable_wmi = 1;
			/* Check for "follow:" prefix */
			if (strncmp(optarg, "follow:", 7) == 0) {
				wmi_follow_mode = 1;
				wmi_source = optarg + 7;  /* Skip "follow:" prefix */
			} else {
				wmi_source = optarg;
			}
			
			/* Validate source */
			if (strcmp(wmi_source, "-") != 0) {
				/* Check if file exists and is readable */
				if (access(wmi_source, R_OK) != 0 && !wmi_follow_mode) {
					warn("Cannot access WMI log source: %s", wmi_source);
					return usage(1);
				}
			}
			break;
			
		case 'W':
			wmi_filter_expr = optarg;
			break;

		default:
			return usage(1);
		}
	}
	
	/* Validate options */
	if (pcap_file && !use_nlmon) {
		warnx("PCAP file output (-p) requires nlmon device (-m)");
		return usage(1);
	}
	
	/* Validate WMI options */
	if (wmi_filter_expr && !enable_wmi) {
		warnx("WMI filter (-W) requires WMI monitoring (-w)");
		return usage(1);
	}
	
	/* Setup nlmon if requested */
	if (use_nlmon) {
		if (!nlmon_device)
			nlmon_device = "nlmon0";
		
		if (create_nlmon_device(nlmon_device) < 0) {
			warnx("Failed to setup nlmon device, continuing without it");
			use_nlmon = 0;
		} else {
			nlmon_sock = bind_nlmon_socket(nlmon_device);
			if (nlmon_sock < 0) {
				warnx("Failed to bind to nlmon device, continuing without it");
				use_nlmon = 0;
			}
		}
		
		/* Setup PCAP file if requested */
		if (use_nlmon && pcap_file) {
			if (init_pcap_file(pcap_file) < 0) {
				warnx("Failed to initialize PCAP file");
				pcap_file = NULL;
			}
		}
	}

	/* Initialize CLI mode if requested */
	if (cli_mode)
		init_cli();

	/* Initialize multi-protocol support if requested */
	if (show_generic_netlink || show_all_protocols) {
		g_multi_proto_ctx = nlmon_multi_protocol_init();
		if (!g_multi_proto_ctx) {
			warnx("Failed to initialize multi-protocol support");
		} else {
			nlmon_multi_protocol_set_callback(g_multi_proto_ctx, genetlink_event_cb, NULL);
			
			if (show_generic_netlink || show_all_protocols) {
				if (nlmon_multi_protocol_enable(g_multi_proto_ctx, NLMON_PROTO_GENERIC) == 0) {
					if (verbose_mode)
						log_event("NETLINK_GENERIC monitoring enabled");
				} else {
					warnx("Failed to enable NETLINK_GENERIC monitoring");
				}
			}
			
			if (show_all_protocols) {
				if (nlmon_multi_protocol_enable(g_multi_proto_ctx, NLMON_PROTO_SOCK_DIAG) == 0) {
					if (verbose_mode)
						log_event("NETLINK_SOCK_DIAG monitoring enabled");
				} else {
					warnx("Failed to enable NETLINK_SOCK_DIAG monitoring");
				}
			}
		}
	}
	
	/* Initialize WMI monitoring if requested */
	if (enable_wmi) {
		struct wmi_log_config wmi_config;
		struct wmi_bridge_config bridge_config;
		
		/* Initialize WMI event bridge first */
		memset(&bridge_config, 0, sizeof(bridge_config));
		bridge_config.event_processor = NULL;  /* Not using event processor for now */
		bridge_config.verbose = verbose_mode;
		bridge_config.user_data = NULL;
		
		if (wmi_bridge_init(&bridge_config) < 0) {
			warnx("Failed to initialize WMI event bridge");
			enable_wmi = 0;
		} else {
			/* Configure WMI log reader */
			memset(&wmi_config, 0, sizeof(wmi_config));
			wmi_config.log_source = wmi_source;
			wmi_config.follow_mode = wmi_follow_mode;
			wmi_config.buffer_size = 4096;
			wmi_config.callback = wmi_log_line_cb;
			wmi_config.user_data = NULL;
			
			if (wmi_log_reader_init(&wmi_config) < 0) {
				warnx("Failed to initialize WMI log reader");
				wmi_bridge_cleanup();
				enable_wmi = 0;
			} else {
				if (verbose_mode) {
					char msg[256];
					snprintf(msg, sizeof(msg), "WMI monitoring enabled: source=%s%s",
					         wmi_follow_mode ? "follow:" : "", wmi_source);
					log_event(msg);
				}
				
				/* Start WMI reader thread */
				if (pthread_create(&wmi_thread, NULL, wmi_reader_thread, NULL) != 0) {
					warn("Failed to create WMI reader thread");
					wmi_log_reader_cleanup();
					wmi_bridge_cleanup();
					enable_wmi = 0;
				} else {
					wmi_thread_started = 1;
				}
			}
		}
	}

	/* Initialize new netlink manager */
	g_nl_manager = nlmon_nl_manager_init();
	if (!g_nl_manager) {
		warnx("Failed to initialize netlink manager");
		return 1;
	}

	/* Enable NETLINK_ROUTE protocol (always enabled) */
	err = nlmon_nl_enable_route(g_nl_manager);
	if (err < 0) {
		warnx("Failed to enable NETLINK_ROUTE: %d", err);
	fail:
		if (cli_mode)
			cleanup_cli();
		if (g_nl_manager)
			nlmon_nl_manager_destroy(g_nl_manager);
		cleanup_memory_management();
		return 1;
	}

	fd = nlmon_nl_get_route_fd(g_nl_manager);
	if (fd == -1) {
		warnx("Failed to get NETLINK_ROUTE file descriptor");
		goto fail;
	}

	/* Enable NETLINK_GENERIC if requested */
	if (show_generic_netlink || show_all_protocols) {
		err = nlmon_nl_enable_generic(g_nl_manager);
		if (err < 0) {
			warnx("Failed to enable NETLINK_GENERIC: %d", err);
			/* Non-fatal - continue without generic netlink */
		} else if (verbose_mode) {
			log_event("NETLINK_GENERIC enabled via netlink manager");
		}
	}

	/* Enable NETLINK_SOCK_DIAG if requested */
	if (show_all_protocols) {
		err = nlmon_nl_enable_diag(g_nl_manager);
		if (err < 0) {
			warnx("Failed to enable NETLINK_SOCK_DIAG: %d", err);
			/* Non-fatal - continue without socket diagnostics */
		} else if (verbose_mode) {
			log_event("NETLINK_SOCK_DIAG enabled via netlink manager");
		}
	}

	/* Enable NETLINK_NETFILTER if requested */
	if (show_all_protocols) {
		err = nlmon_nl_enable_netfilter(g_nl_manager);
		if (err < 0) {
			warnx("Failed to enable NETLINK_NETFILTER: %d", err);
			/* Non-fatal - continue without netfilter */
		} else if (verbose_mode) {
			log_event("NETLINK_NETFILTER enabled via netlink manager");
		}
	}

	/* Legacy init function - may need updating */
	if (init(&ctx))
		goto fail;

	loop = ev_default_loop(EVFLAG_NOENV);
	ev_set_userdata(loop, &ctx);

	ev_io_init(&io, nlroute_cb, fd, EV_READ);
	ev_io_start(loop, &io);

	ev_signal_init (&intw, sigint_cb, SIGINT);
	ev_signal_start (loop, &intw);

	ev_signal_init (&hupw, sighub_cb, SIGHUP);
	ev_signal_start (loop, &hupw);

	/* Initialize CLI update timer if in CLI mode */
	if (cli_mode) {
		ev_timer_init(&cli_timer, cli_update_cb, 0.0, 0.1);
		ev_timer_start(loop, &cli_timer);
	}
	
	/* Initialize nlmon watcher if enabled */
	if (use_nlmon && nlmon_sock >= 0) {
		ev_io_init(&nlmon_io, nlmon_packet_cb, nlmon_sock, EV_READ);
		ev_io_start(loop, &nlmon_io);
	}
	
	/* Initialize genetlink watchers if enabled (legacy multi-protocol support) */
	ev_io genetlink_io, sock_diag_io;
	if (g_multi_proto_ctx) {
		int genl_fd = nlmon_multi_protocol_get_fd(g_multi_proto_ctx, NLMON_PROTO_GENERIC);
		if (genl_fd >= 0) {
			ev_io_init(&genetlink_io, genetlink_io_cb, genl_fd, EV_READ);
			genetlink_io.data = (void *)(intptr_t)NLMON_PROTO_GENERIC;
			ev_io_start(loop, &genetlink_io);
		}
		
		int diag_fd = nlmon_multi_protocol_get_fd(g_multi_proto_ctx, NLMON_PROTO_SOCK_DIAG);
		if (diag_fd >= 0) {
			ev_io_init(&sock_diag_io, genetlink_io_cb, diag_fd, EV_READ);
			sock_diag_io.data = (void *)(intptr_t)NLMON_PROTO_SOCK_DIAG;
			ev_io_start(loop, &sock_diag_io);
		}
	}

	/* Initialize netlink manager watchers for additional protocols */
	ev_io nl_genl_io, nl_diag_io, nl_nf_io;
	if (g_nl_manager) {
		/* Add NETLINK_GENERIC watcher if enabled */
		int genl_fd = nlmon_nl_get_genl_fd(g_nl_manager);
		if (genl_fd >= 0) {
			ev_io_init(&nl_genl_io, nlgenl_cb, genl_fd, EV_READ);
			ev_io_start(loop, &nl_genl_io);
			if (verbose_mode)
				log_event("Added NETLINK_GENERIC to event loop");
		}
		
		/* Add NETLINK_SOCK_DIAG watcher if enabled */
		int diag_fd = nlmon_nl_get_diag_fd(g_nl_manager);
		if (diag_fd >= 0) {
			ev_io_init(&nl_diag_io, nldiag_cb, diag_fd, EV_READ);
			ev_io_start(loop, &nl_diag_io);
			if (verbose_mode)
				log_event("Added NETLINK_SOCK_DIAG to event loop");
		}
		
		/* Add NETLINK_NETFILTER watcher if enabled */
		int nf_fd = nlmon_nl_get_nf_fd(g_nl_manager);
		if (nf_fd >= 0) {
			ev_io_init(&nl_nf_io, nlnf_cb, nf_fd, EV_READ);
			ev_io_start(loop, &nl_nf_io);
			if (verbose_mode)
				log_event("Added NETLINK_NETFILTER to event loop");
		}
	}

	/* Start event loop, remain there until ev_unloop() is called. */
	ev_run(loop, 0);

	/* Cleanup new netlink manager */
	if (g_nl_manager) {
		nlmon_nl_manager_destroy(g_nl_manager);
		g_nl_manager = NULL;
	}
	
	/* Cleanup nlmon resources */
	if (nlmon_sock >= 0)
		close(nlmon_sock);
	if (pcap_fp)
		fclose(pcap_fp);
	
	/* Cleanup memory management and resource tracking */
	cleanup_memory_management();

	return 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
