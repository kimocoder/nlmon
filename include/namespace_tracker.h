#ifndef NAMESPACE_TRACKER_H
#define NAMESPACE_TRACKER_H

#include <stdint.h>
#include <sys/types.h>

#define NAMESPACE_NAME_MAX 256

/* Network namespace information */
struct netns_info {
	ino_t nsid;                      /* Namespace inode number */
	char name[NAMESPACE_NAME_MAX];   /* Namespace name (if available) */
	pid_t pid;                       /* Associated PID (if known) */
	int valid;                       /* Whether this entry is valid */
};

/* Namespace tracker context */
struct namespace_tracker;

/* Initialize namespace tracker */
struct namespace_tracker *namespace_tracker_init(void);

/* Detect namespace from netlink message attributes */
int namespace_tracker_detect_from_msg(struct namespace_tracker *tracker,
                                      struct nlmsghdr *nlh,
                                      struct netns_info *nsinfo);

/* Get namespace info by interface index */
int namespace_tracker_get_by_ifindex(struct namespace_tracker *tracker,
                                     int ifindex,
                                     struct netns_info *nsinfo);

/* Resolve namespace name from inode */
int namespace_tracker_resolve_name(struct namespace_tracker *tracker,
                                   ino_t nsid,
                                   char *name,
                                   size_t name_len);

/* Get current process namespace */
int namespace_tracker_get_current(struct netns_info *nsinfo);

/* Check if namespace matches filter */
int namespace_tracker_matches_filter(struct namespace_tracker *tracker,
                                     const struct netns_info *nsinfo,
                                     const char *filter);

/* Update namespace cache */
void namespace_tracker_update_cache(struct namespace_tracker *tracker);

/* Cleanup */
void namespace_tracker_destroy(struct namespace_tracker *tracker);

#endif /* NAMESPACE_TRACKER_H */
