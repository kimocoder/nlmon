/* nlmon - monitor kernel netlink events for interface changes
 *
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

#include <net/if.h>

#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/route/link.h>
#include <netlink/route/link/veth.h>
#include <netlink/route/route.h>
#include <netlink/route/addr.h>
#include <netlink/route/neighbour.h>
#include <netlink/route/rule.h>
#include <linux/netlink.h>

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
};

static struct stats event_stats = {0};

struct context {
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
	char cmd[256];
	int ret;
	
	/* Check if device already exists */
	snprintf(cmd, sizeof(cmd), "ip link show %s > /dev/null 2>&1", dev_name);
	ret = system(cmd);
	
	if (ret != 0) {
		/* Device doesn't exist, create it */
		snprintf(cmd, sizeof(cmd), "ip link add %s type nlmon", dev_name);
		ret = system(cmd);
		if (ret != 0) {
			warnx("Failed to create nlmon device %s", dev_name);
			return -1;
		}
		if (verbose_mode)
			log_event("Created nlmon device");
	}
	
	/* Bring device up */
	snprintf(cmd, sizeof(cmd), "ip link set %s up", dev_name);
	ret = system(cmd);
	if (ret != 0) {
		warnx("Failed to bring up nlmon device %s", dev_name);
		return -1;
	}
	
	if (verbose_mode)
		log_event("nlmon device is up");
	
	return 0;
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

static void nlmon_packet_cb(struct ev_loop *loop, ev_io *w, int revents)
{
	unsigned char buffer[65536];
	ssize_t len;
	struct nlmsghdr *nlh;
	char msg[512];
	static unsigned long packet_count = 0;
	
	len = recv(nlmon_sock, buffer, sizeof(buffer), 0);
	if (len < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			warn("Failed to receive from nlmon socket");
		return;
	}
	
	if (len == 0)
		return;
	
	packet_count++;
	
	/* Write to PCAP file if enabled */
	if (pcap_fp)
		write_pcap_packet(buffer, len);
	
	/* Parse and log netlink message if verbose */
	if (verbose_mode && len >= (ssize_t)sizeof(struct nlmsghdr)) {
		nlh = (struct nlmsghdr *)buffer;
		snprintf(msg, sizeof(msg), 
		         "nlmon: packet #%lu, len=%zd, type=%u, flags=0x%x, seq=%u, pid=%u",
		         packet_count, len, nlh->nlmsg_type, nlh->nlmsg_flags, 
		         nlh->nlmsg_seq, nlh->nlmsg_pid);
		log_event(msg);
	}
}

static void addr_change_cb(struct nl_cache *acache,
			   struct nl_object *obj, int action, void *arg)
{
	struct rtnl_addr *addr = (void *)obj;
	struct nl_addr *local;
	char buf[256];
	char addr_str[64];
	int ifindex;
	char ifname[IF_NAMESIZE];
	
	if (!monitor_addr)
		return;
	
	local = rtnl_addr_get_local(addr);
	ifindex = rtnl_addr_get_ifindex(addr);
	
	if (if_indextoname(ifindex, ifname) == NULL)
		snprintf(ifname, sizeof(ifname), "if%d", ifindex);
	
	if (local)
		nl_addr2str(local, addr_str, sizeof(addr_str));
	else
		snprintf(addr_str, sizeof(addr_str), "unknown");
	
	switch (action) {
	case NL_ACT_DEL:
		snprintf(buf, sizeof(buf), "addr %s removed from %s", addr_str, ifname);
		break;
	case NL_ACT_NEW:
		snprintf(buf, sizeof(buf), "addr %s added to %s", addr_str, ifname);
		break;
	case NL_ACT_CHANGE:
		snprintf(buf, sizeof(buf), "addr %s changed on %s", addr_str, ifname);
		break;
	default:
		return;
	}
	
	event_stats.addr_events++;
	log_event(buf);
}

static void neigh_change_cb(struct nl_cache *ncache,
			    struct nl_object *obj, int action, void *arg)
{
	struct rtnl_neigh *neigh = (void *)obj;
	struct nl_addr *dst;
	char buf[256];
	char addr_str[64];
	int ifindex;
	char ifname[IF_NAMESIZE];
	
	if (!monitor_neigh)
		return;
	
	dst = rtnl_neigh_get_dst(neigh);
	ifindex = rtnl_neigh_get_ifindex(neigh);
	
	if (if_indextoname(ifindex, ifname) == NULL)
		snprintf(ifname, sizeof(ifname), "if%d", ifindex);
	
	if (dst)
		nl_addr2str(dst, addr_str, sizeof(addr_str));
	else
		snprintf(addr_str, sizeof(addr_str), "unknown");
	
	switch (action) {
	case NL_ACT_DEL:
		snprintf(buf, sizeof(buf), "neighbor %s removed from %s", addr_str, ifname);
		break;
	case NL_ACT_NEW:
		snprintf(buf, sizeof(buf), "neighbor %s added to %s", addr_str, ifname);
		break;
	case NL_ACT_CHANGE:
		snprintf(buf, sizeof(buf), "neighbor %s changed on %s", addr_str, ifname);
		break;
	default:
		return;
	}
	
	event_stats.neigh_events++;
	log_event(buf);
}

static void rule_change_cb(struct nl_cache *rucache,
			   struct nl_object *obj, int action, void *arg)
{
	struct rtnl_rule *rule = (void *)obj;
	char buf[256];
	uint32_t prio = rtnl_rule_get_prio(rule);
	
	if (!monitor_rules)
		return;
	
	switch (action) {
	case NL_ACT_DEL:
		snprintf(buf, sizeof(buf), "rule priority %u removed", prio);
		break;
	case NL_ACT_NEW:
		snprintf(buf, sizeof(buf), "rule priority %u added", prio);
		break;
	case NL_ACT_CHANGE:
		snprintf(buf, sizeof(buf), "rule priority %u changed", prio);
		break;
	default:
		return;
	}
	
	event_stats.rule_events++;
	log_event(buf);
}

static void route_change_cb(struct nl_cache *rcache,
			    struct nl_object *obj, int action, void *arg)
{
	struct rtnl_route *r = (void *)obj;
	char buf[256];

	if (veth_only)
		return;
	if (!nl_addr_iszero(rtnl_route_get_dst(r)))
		return;

	if (action == NL_ACT_DEL)
		snprintf(buf, sizeof(buf), "default route removed");
	else
		snprintf(buf, sizeof(buf), "default route added");

	event_stats.route_events++;
	log_event(buf);
}

static void link_change_cb(struct nl_cache *lcache,
			   struct nl_object *obj, int action, void *arg)
{
	struct rtnl_link *link = (void *)obj;
	const char *ifname;
	unsigned int flags;
	int isveth;
	char buf[256];

	ifname = rtnl_link_get_name(link);
	isveth = rtnl_link_is_veth(link);

	if (veth_only && !isveth)
		return;

	switch (action) {
	case NL_ACT_DEL:
		snprintf(buf, sizeof(buf), "%siface %s deleted", isveth ? "veth ": "", ifname);
		break;

	case NL_ACT_NEW:
		snprintf(buf, sizeof(buf), "%siface %s added", isveth ? "veth ": "", ifname);
		break;

	case NL_ACT_CHANGE:
		flags = rtnl_link_get_flags(link);
		snprintf(buf, sizeof(buf), "%siface %s changed state %s link %s",
		      isveth ? "veth ": "", ifname,
		      flags & IFF_UP ? "UP" : "DOWN",
		      flags & IFF_RUNNING ? "ON": "OFF");
		break;

	default:
		return;
	}
	
	event_stats.link_events++;
	log_event(buf);
}

static void nlroute_cb(struct ev_loop *loop, ev_io *w, int revents)
{
	struct context *ctx = ev_userdata(loop);

	assert(ctx);
	assert(ctx->mngr);
//	warnx("We got signal");

	nl_cache_mngr_data_ready(ctx->mngr);
}

static void reconf_link_iter(struct nl_object *obj, void *arg)
{
	link_change_cb(NULL, obj, NL_ACT_NEW, NULL);
}

static void reconf_route_iter(struct nl_object *obj, void *arg)
{
	route_change_cb(NULL, obj, NL_ACT_NEW, NULL);
}

static void reconf_addr_iter(struct nl_object *obj, void *arg)
{
	addr_change_cb(NULL, obj, NL_ACT_NEW, NULL);
}

static void reconf_neigh_iter(struct nl_object *obj, void *arg)
{
	neigh_change_cb(NULL, obj, NL_ACT_NEW, NULL);
}

static void reconf_rule_iter(struct nl_object *obj, void *arg)
{
	rule_change_cb(NULL, obj, NL_ACT_NEW, NULL);
}

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
	mvwprintw(status_win, 1, 2, "Links: %-8lu Routes: %-8lu Addrs: %-8lu", 
	          event_stats.link_events, event_stats.route_events, event_stats.addr_events);
	mvwprintw(status_win, 2, 2, "Neigh: %-8lu Rules:  %-8lu Total: %-8lu",
	          event_stats.neigh_events, event_stats.rule_events,
	          event_stats.link_events + event_stats.route_events + 
	          event_stats.addr_events + event_stats.neigh_events + event_stats.rule_events);
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
	struct context *ctx = ev_userdata(loop);

	nl_cache_refill(ctx->ns, ctx->lcache);
	nl_cache_foreach(ctx->lcache, reconf_link_iter, NULL);

	nl_cache_refill(ctx->ns, ctx->rcache);
	nl_cache_foreach(ctx->rcache, reconf_route_iter, NULL);
	
	if (ctx->acache) {
		nl_cache_refill(ctx->ns, ctx->acache);
		nl_cache_foreach(ctx->acache, reconf_addr_iter, NULL);
	}
	
	if (ctx->ncache) {
		nl_cache_refill(ctx->ns, ctx->ncache);
		nl_cache_foreach(ctx->ncache, reconf_neigh_iter, NULL);
	}
	
	if (ctx->rucache) {
		nl_cache_refill(ctx->ns, ctx->rucache);
		nl_cache_foreach(ctx->rucache, reconf_rule_iter, NULL);
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
	int err;

	nl_socket_set_buffer_size(ctx->ns, 320 << 10, 0);

	err = rtnl_link_alloc_cache(ctx->ns, AF_UNSPEC, &ctx->lcache);
	if (err)
		goto err_free_mngr;

	err = rtnl_route_alloc_cache(ctx->ns, AF_UNSPEC, 0, &ctx->rcache);
	if (err)
		goto err_free_mngr;

	err = nl_cache_mngr_add_cache(ctx->mngr, ctx->lcache,
				      link_change_cb, ctx);
	if (err)
		goto err_free_mngr;

	err = nl_cache_mngr_add_cache(ctx->mngr, ctx->rcache,
				      route_change_cb, ctx);
	if (err)
		goto err_free_mngr;

	/* Add address cache */
	if (monitor_addr) {
		err = rtnl_addr_alloc_cache(ctx->ns, &ctx->acache);
		if (err) {
			warnx("Failed to allocate address cache: %d", err);
			ctx->acache = NULL;
		} else {
			err = nl_cache_mngr_add_cache(ctx->mngr, ctx->acache,
						      addr_change_cb, ctx);
			if (err) {
				warnx("Failed to add address cache: %d", err);
				nl_cache_free(ctx->acache);
				ctx->acache = NULL;
			}
		}
	}

	/* Add neighbor cache */
	if (monitor_neigh) {
		err = rtnl_neigh_alloc_cache(ctx->ns, &ctx->ncache);
		if (err) {
			warnx("Failed to allocate neighbor cache: %d", err);
			ctx->ncache = NULL;
		} else {
			err = nl_cache_mngr_add_cache(ctx->mngr, ctx->ncache,
						      neigh_change_cb, ctx);
			if (err) {
				warnx("Failed to add neighbor cache: %d", err);
				nl_cache_free(ctx->ncache);
				ctx->ncache = NULL;
			}
		}
	}

	/* Add rule cache */
	if (monitor_rules) {
		err = rtnl_rule_alloc_cache(ctx->ns, AF_UNSPEC, &ctx->rucache);
		if (err) {
			warnx("Failed to allocate rule cache: %d", err);
			ctx->rucache = NULL;
		} else {
			err = nl_cache_mngr_add_cache(ctx->mngr, ctx->rucache,
						      rule_change_cb, ctx);
			if (err) {
				warnx("Failed to add rule cache: %d", err);
				nl_cache_free(ctx->rucache);
				ctx->rucache = NULL;
			}
		}
	}

	return 0;

err_free_mngr:
	nl_cache_mngr_free(ctx->mngr);
	warnx("init, nle:%d", err);

	return 1;
}

static int usage(int rc)
{
	printf("Usage: nlmon [-h?vciau] [-m device] [-p file] [-V]\n"
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
	       "\n"
	       "nlmon Kernel Module Support:\n"
	       "  The -m option enables binding to a virtual nlmon network device to capture\n"
	       "  all netlink messages as raw packets. This requires the nlmon kernel module.\n"
	       "  \n"
	       "  Example: nlmon -m nlmon0 -p netlink.pcap -V\n"
	       "  \n"
	       "  To manually create an nlmon device:\n"
	       "    sudo modprobe nlmon\n"
	       "    sudo ip link add nlmon0 type nlmon\n"
	       "    sudo ip link set nlmon0 up\n"
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

	while ((c = getopt(argc, argv, "h?vciauVm:p:")) != EOF) {
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

		default:
			return usage(1);
		}
	}
	
	/* Validate options */
	if (pcap_file && !use_nlmon) {
		warnx("PCAP file output (-p) requires nlmon device (-m)");
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

	ctx.ns = nl_socket_alloc();
	assert(ctx.ns);
	nl_socket_set_nonblocking(ctx.ns);

	err = nl_cache_mngr_alloc(ctx.ns, NETLINK_ROUTE, NL_AUTO_PROVIDE, &ctx.mngr);
	if (err)
		return 1;

	fd = nl_cache_mngr_get_fd(ctx.mngr);
	if (fd == -1) {
	fail:
		if (cli_mode)
			cleanup_cli();
		nl_cache_mngr_free(ctx.mngr);
		nl_socket_free(ctx.ns);
		return 1;
	}

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

	/* Start event loop, remain there until ev_unloop() is called. */
	ev_run(loop, 0);

	nl_cache_mngr_free(ctx.mngr);
	nl_socket_free(ctx.ns);
	
	/* Cleanup nlmon resources */
	if (nlmon_sock >= 0)
		close(nlmon_sock);
	if (pcap_fp)
		fclose(pcap_fp);

	return 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
