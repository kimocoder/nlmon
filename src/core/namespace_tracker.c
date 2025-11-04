#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

#include "namespace_tracker.h"

#define MAX_CACHED_NAMESPACES 256
#define NETNS_RUN_DIR "/var/run/netns"
#define PROC_NET_NS_PATH "/proc/%d/ns/net"

/* Namespace cache entry */
struct ns_cache_entry {
	ino_t nsid;
	char name[NAMESPACE_NAME_MAX];
	time_t last_update;
	int valid;
};

struct namespace_tracker {
	struct ns_cache_entry cache[MAX_CACHED_NAMESPACES];
	int cache_count;
	time_t last_scan;
};

struct namespace_tracker *namespace_tracker_init(void)
{
	struct namespace_tracker *tracker;
	
	tracker = calloc(1, sizeof(*tracker));
	if (!tracker)
		return NULL;
	
	/* Initial scan of namespaces */
	namespace_tracker_update_cache(tracker);
	
	return tracker;
}

/* Get namespace inode from a file descriptor or path */
static ino_t get_namespace_inode(const char *path)
{
	struct stat st;
	
	if (stat(path, &st) < 0)
		return 0;
	
	return st.st_ino;
}

/* Scan /var/run/netns for named namespaces */
static void scan_named_namespaces(struct namespace_tracker *tracker)
{
	DIR *dir;
	struct dirent *entry;
	char path[512];
	ino_t nsid;
	int idx;
	
	dir = opendir(NETNS_RUN_DIR);
	if (!dir)
		return;  /* Directory doesn't exist or no permission */
	
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		
		snprintf(path, sizeof(path), "%s/%s", NETNS_RUN_DIR, entry->d_name);
		nsid = get_namespace_inode(path);
		
		if (nsid == 0)
			continue;
		
		/* Check if already in cache */
		int found = 0;
		for (idx = 0; idx < tracker->cache_count; idx++) {
			if (tracker->cache[idx].nsid == nsid) {
				/* Update name if changed */
				strncpy(tracker->cache[idx].name, entry->d_name, NAMESPACE_NAME_MAX - 1);
				tracker->cache[idx].name[NAMESPACE_NAME_MAX - 1] = '\0';
				tracker->cache[idx].valid = 1;
				found = 1;
				break;
			}
		}
		
		/* Add new entry */
		if (!found && tracker->cache_count < MAX_CACHED_NAMESPACES) {
			idx = tracker->cache_count++;
			tracker->cache[idx].nsid = nsid;
			strncpy(tracker->cache[idx].name, entry->d_name, NAMESPACE_NAME_MAX - 1);
			tracker->cache[idx].name[NAMESPACE_NAME_MAX - 1] = '\0';
			tracker->cache[idx].last_update = time(NULL);
			tracker->cache[idx].valid = 1;
		}
	}
	
	closedir(dir);
}

/* Scan /proc for process namespaces */
static void scan_proc_namespaces(struct namespace_tracker *tracker)
{
	DIR *dir;
	struct dirent *entry;
	char path[512];
	ino_t nsid;
	pid_t pid;
	int idx;
	
	dir = opendir("/proc");
	if (!dir)
		return;
	
	while ((entry = readdir(dir)) != NULL) {
		/* Check if directory name is a number (PID) */
		if (entry->d_type != DT_DIR)
			continue;
		
		pid = atoi(entry->d_name);
		if (pid <= 0)
			continue;
		
		snprintf(path, sizeof(path), PROC_NET_NS_PATH, pid);
		nsid = get_namespace_inode(path);
		
		if (nsid == 0)
			continue;
		
		/* Check if already in cache */
		int found = 0;
		for (idx = 0; idx < tracker->cache_count; idx++) {
			if (tracker->cache[idx].nsid == nsid) {
				found = 1;
				/* If no name set, use PID */
				if (tracker->cache[idx].name[0] == '\0') {
					snprintf(tracker->cache[idx].name, NAMESPACE_NAME_MAX, "pid-%d", pid);
				}
				break;
			}
		}
		
		/* Add new entry with PID-based name */
		if (!found && tracker->cache_count < MAX_CACHED_NAMESPACES) {
			idx = tracker->cache_count++;
			tracker->cache[idx].nsid = nsid;
			snprintf(tracker->cache[idx].name, NAMESPACE_NAME_MAX, "pid-%d", pid);
			tracker->cache[idx].last_update = time(NULL);
			tracker->cache[idx].valid = 1;
		}
	}
	
	closedir(dir);
}

void namespace_tracker_update_cache(struct namespace_tracker *tracker)
{
	if (!tracker)
		return;
	
	/* Scan named namespaces first (they have priority) */
	scan_named_namespaces(tracker);
	
	/* Then scan process namespaces */
	scan_proc_namespaces(tracker);
	
	tracker->last_scan = time(NULL);
}

int namespace_tracker_resolve_name(struct namespace_tracker *tracker,
                                   ino_t nsid,
                                   char *name,
                                   size_t name_len)
{
	int i;
	
	if (!tracker || !name || name_len == 0)
		return -1;
	
	/* Refresh cache if it's old (> 5 seconds) */
	if (time(NULL) - tracker->last_scan > 5) {
		namespace_tracker_update_cache(tracker);
	}
	
	/* Search cache */
	for (i = 0; i < tracker->cache_count; i++) {
		if (tracker->cache[i].valid && tracker->cache[i].nsid == nsid) {
			strncpy(name, tracker->cache[i].name, name_len - 1);
			name[name_len - 1] = '\0';
			return 0;
		}
	}
	
	/* Not found, use inode number */
	snprintf(name, name_len, "ns-%lu", (unsigned long)nsid);
	return -1;
}

int namespace_tracker_get_current(struct netns_info *nsinfo)
{
	char path[256];
	ino_t nsid;
	
	if (!nsinfo)
		return -1;
	
	snprintf(path, sizeof(path), "/proc/self/ns/net");
	nsid = get_namespace_inode(path);
	
	if (nsid == 0)
		return -1;
	
	memset(nsinfo, 0, sizeof(*nsinfo));
	nsinfo->nsid = nsid;
	nsinfo->pid = getpid();
	snprintf(nsinfo->name, sizeof(nsinfo->name), "default");
	nsinfo->valid = 1;
	
	return 0;
}

/* Extract namespace info from netlink message attributes */
int namespace_tracker_detect_from_msg(struct namespace_tracker *tracker,
                                      struct nlmsghdr *nlh,
                                      struct netns_info *nsinfo)
{
	struct rtattr *rta;
	int rtalen;
	struct ifinfomsg *ifi;
	
	if (!tracker || !nlh || !nsinfo)
		return -1;
	
	memset(nsinfo, 0, sizeof(*nsinfo));
	
	/* For RTM_NEWLINK/RTM_DELLINK messages, extract interface index */
	if (nlh->nlmsg_type == RTM_NEWLINK || nlh->nlmsg_type == RTM_DELLINK ||
	    nlh->nlmsg_type == RTM_GETLINK || nlh->nlmsg_type == RTM_SETLINK) {
		
		if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*ifi)))
			return -1;
		
		ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
		
		/* Parse attributes to look for namespace info */
		rta = IFLA_RTA(ifi);
		rtalen = IFLA_PAYLOAD(nlh);
		
		while (RTA_OK(rta, rtalen)) {
			/* Check for IFLA_NET_NS_PID or IFLA_NET_NS_FD attributes */
			if (rta->rta_type == IFLA_NET_NS_PID) {
				pid_t pid = *(pid_t *)RTA_DATA(rta);
				char path[256];
				
				snprintf(path, sizeof(path), PROC_NET_NS_PATH, pid);
				nsinfo->nsid = get_namespace_inode(path);
				nsinfo->pid = pid;
				
				if (nsinfo->nsid != 0) {
					namespace_tracker_resolve_name(tracker, nsinfo->nsid,
					                              nsinfo->name, sizeof(nsinfo->name));
					nsinfo->valid = 1;
					return 0;
				}
			}
			
			rta = RTA_NEXT(rta, rtalen);
		}
	}
	
	/* If we couldn't extract namespace from message, use current namespace */
	return namespace_tracker_get_current(nsinfo);
}

int namespace_tracker_get_by_ifindex(struct namespace_tracker *tracker,
                                     int ifindex,
                                     struct netns_info *nsinfo)
{
	if (!tracker || !nsinfo || ifindex < 0)
		return -1;
	
	/* Try to find namespace by reading /sys/class/net/<ifname>/ifindex */
	/* This is a simplified approach - in reality, we'd need to track
	 * interface-to-namespace mappings more carefully */
	
	/* For now, return current namespace */
	return namespace_tracker_get_current(nsinfo);
}

int namespace_tracker_matches_filter(struct namespace_tracker *tracker,
                                     const struct netns_info *nsinfo,
                                     const char *filter)
{
	if (!nsinfo || !filter)
		return 1;  /* No filter means match all */
	
	if (!nsinfo->valid)
		return 0;
	
	/* Simple string matching on namespace name */
	if (strstr(nsinfo->name, filter) != NULL)
		return 1;
	
	/* Match on namespace ID */
	char nsid_str[64];
	snprintf(nsid_str, sizeof(nsid_str), "%lu", (unsigned long)nsinfo->nsid);
	if (strstr(nsid_str, filter) != NULL)
		return 1;
	
	return 0;
}

void namespace_tracker_destroy(struct namespace_tracker *tracker)
{
	if (!tracker)
		return;
	
	free(tracker);
}
