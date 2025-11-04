#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>

#include "interface_detector.h"

#define MAX_INTERFACES 1024

/* Interface cache entry */
struct iface_cache_entry {
	int ifindex;
	interface_type_t type;
	char name[IFNAMSIZ];
	int parent_ifindex;
	int valid;
};

struct interface_detector {
	struct iface_cache_entry cache[MAX_INTERFACES];
	int cache_count;
};

struct interface_detector *interface_detector_init(void)
{
	struct interface_detector *detector;

	detector = calloc(1, sizeof(*detector));
	if (!detector)
		return NULL;

	return detector;
}

/* Determine interface type from kind string */
static interface_type_t get_type_from_kind(const char *kind)
{
	if (!kind)
		return IFACE_TYPE_UNKNOWN;

	if (strcmp(kind, "veth") == 0)
		return IFACE_TYPE_VETH;
	else if (strcmp(kind, "bridge") == 0)
		return IFACE_TYPE_BRIDGE;
	else if (strcmp(kind, "vlan") == 0)
		return IFACE_TYPE_VLAN;
	else if (strcmp(kind, "bond") == 0)
		return IFACE_TYPE_BOND;
	else if (strcmp(kind, "tun") == 0)
		return IFACE_TYPE_TUN;
	else if (strcmp(kind, "tap") == 0)
		return IFACE_TYPE_TAP;
	else if (strcmp(kind, "vxlan") == 0)
		return IFACE_TYPE_VXLAN;
	else if (strcmp(kind, "macvlan") == 0)
		return IFACE_TYPE_MACVLAN;
	else if (strcmp(kind, "ipvlan") == 0)
		return IFACE_TYPE_IPVLAN;

	return IFACE_TYPE_UNKNOWN;
}

interface_type_t interface_detector_detect_type(struct interface_detector *detector,
                                                struct nlmsghdr *nlh)
{
	struct ifinfomsg *ifi;
	struct rtattr *rta;
	int rtalen;
	interface_type_t type = IFACE_TYPE_UNKNOWN;

	if (!detector || !nlh)
		return IFACE_TYPE_UNKNOWN;

	if (nlh->nlmsg_type != RTM_NEWLINK && nlh->nlmsg_type != RTM_DELLINK &&
	    nlh->nlmsg_type != RTM_GETLINK && nlh->nlmsg_type != RTM_SETLINK)
		return IFACE_TYPE_UNKNOWN;

	if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*ifi)))
		return IFACE_TYPE_UNKNOWN;

	ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);

	/* Check for loopback */
	if (ifi->ifi_flags & IFF_LOOPBACK)
		return IFACE_TYPE_LOOPBACK;

	/* Parse attributes to find IFLA_LINKINFO */
	rta = IFLA_RTA(ifi);
	rtalen = IFLA_PAYLOAD(nlh);

	while (RTA_OK(rta, rtalen)) {
		if (rta->rta_type == IFLA_LINKINFO) {
			struct rtattr *linkinfo = RTA_DATA(rta);
			int linkinfo_len = RTA_PAYLOAD(rta);

			/* Parse IFLA_LINKINFO attributes */
			while (RTA_OK(linkinfo, linkinfo_len)) {
				if (linkinfo->rta_type == IFLA_INFO_KIND) {
					const char *kind = (const char *)RTA_DATA(linkinfo);
					type = get_type_from_kind(kind);
					break;
				}
				linkinfo = RTA_NEXT(linkinfo, linkinfo_len);
			}

			if (type != IFACE_TYPE_UNKNOWN)
				break;
		}

		rta = RTA_NEXT(rta, rtalen);
	}

	/* Default to ethernet if no specific type found */
	if (type == IFACE_TYPE_UNKNOWN && !(ifi->ifi_flags & IFF_LOOPBACK))
		type = IFACE_TYPE_ETHERNET;

	return type;
}

int interface_detector_parse_info(struct interface_detector *detector,
                                  struct nlmsghdr *nlh,
                                  struct interface_info *info)
{
	struct ifinfomsg *ifi;
	struct rtattr *rta;
	int rtalen;

	if (!detector || !nlh || !info)
		return -1;

	if (nlh->nlmsg_type != RTM_NEWLINK && nlh->nlmsg_type != RTM_DELLINK &&
	    nlh->nlmsg_type != RTM_GETLINK && nlh->nlmsg_type != RTM_SETLINK)
		return -1;

	if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*ifi)))
		return -1;

	ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);

	memset(info, 0, sizeof(*info));
	info->ifindex = ifi->ifi_index;
	info->flags = ifi->ifi_flags;
	info->type = interface_detector_detect_type(detector, nlh);

	/* Parse attributes */
	rta = IFLA_RTA(ifi);
	rtalen = IFLA_PAYLOAD(nlh);

	while (RTA_OK(rta, rtalen)) {
		switch (rta->rta_type) {
		case IFLA_IFNAME:
			strncpy(info->name, (char *)RTA_DATA(rta), IFNAMSIZ - 1);
			info->name[IFNAMSIZ - 1] = '\0';
			break;

		case IFLA_ADDRESS:
			if (RTA_PAYLOAD(rta) == 6) {
				memcpy(info->mac_addr, RTA_DATA(rta), 6);
			}
			break;

		case IFLA_MTU:
			info->mtu = *(int *)RTA_DATA(rta);
			break;
		}

		rta = RTA_NEXT(rta, rtalen);
	}

	/* Parse type-specific info */
	switch (info->type) {
	case IFACE_TYPE_BRIDGE:
		interface_detector_parse_bridge(detector, nlh, &info->type_info.bridge);
		break;
	case IFACE_TYPE_VLAN:
		interface_detector_parse_vlan(detector, nlh, &info->type_info.vlan);
		break;
	case IFACE_TYPE_BOND:
		interface_detector_parse_bond(detector, nlh, &info->type_info.bond);
		break;
	default:
		break;
	}

	return 0;
}

int interface_detector_parse_bridge(struct interface_detector *detector,
                                    struct nlmsghdr *nlh,
                                    struct bridge_info *bridge)
{
	struct ifinfomsg *ifi;
	struct rtattr *rta;
	int rtalen;

	if (!detector || !nlh || !bridge)
		return -1;

	if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*ifi)))
		return -1;

	ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);

	memset(bridge, 0, sizeof(*bridge));
	bridge->ifindex = ifi->ifi_index;

	/* Parse attributes */
	rta = IFLA_RTA(ifi);
	rtalen = IFLA_PAYLOAD(nlh);

	while (RTA_OK(rta, rtalen)) {
		switch (rta->rta_type) {
		case IFLA_IFNAME:
			strncpy(bridge->name, (char *)RTA_DATA(rta), IFNAMSIZ - 1);
			bridge->name[IFNAMSIZ - 1] = '\0';
			break;

		case IFLA_LINKINFO: {
			struct rtattr *linkinfo = RTA_DATA(rta);
			int linkinfo_len = RTA_PAYLOAD(rta);

			while (RTA_OK(linkinfo, linkinfo_len)) {
				if (linkinfo->rta_type == IFLA_INFO_DATA) {
					struct rtattr *data = RTA_DATA(linkinfo);
					int data_len = RTA_PAYLOAD(linkinfo);

					/* Parse bridge-specific attributes */
					while (RTA_OK(data, data_len)) {
						switch (data->rta_type) {
						case IFLA_BR_FORWARD_DELAY:
							bridge->forward_delay = *(uint32_t *)RTA_DATA(data);
							break;
						case IFLA_BR_HELLO_TIME:
							bridge->hello_time = *(uint32_t *)RTA_DATA(data);
							break;
						case IFLA_BR_MAX_AGE:
							bridge->max_age = *(uint32_t *)RTA_DATA(data);
							break;
						case IFLA_BR_PRIORITY:
							bridge->priority = *(uint16_t *)RTA_DATA(data);
							break;
						case IFLA_BR_STP_STATE:
							bridge->stp_enabled = *(uint8_t *)RTA_DATA(data);
							break;
						}
						data = RTA_NEXT(data, data_len);
					}
				}
				linkinfo = RTA_NEXT(linkinfo, linkinfo_len);
			}
			break;
		}

		case IFLA_MASTER:
			/* This interface is a slave of a bridge */
			/* We'd need to track this separately */
			break;
		}

		rta = RTA_NEXT(rta, rtalen);
	}

	return 0;
}

int interface_detector_parse_vlan(struct interface_detector *detector,
                                  struct nlmsghdr *nlh,
                                  struct vlan_info *vlan)
{
	struct ifinfomsg *ifi;
	struct rtattr *rta;
	int rtalen;

	if (!detector || !nlh || !vlan)
		return -1;

	if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*ifi)))
		return -1;

	ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);

	memset(vlan, 0, sizeof(*vlan));
	vlan->ifindex = ifi->ifi_index;
	vlan->protocol = 0x8100;  /* Default to 802.1Q */

	/* Parse attributes */
	rta = IFLA_RTA(ifi);
	rtalen = IFLA_PAYLOAD(nlh);

	while (RTA_OK(rta, rtalen)) {
		switch (rta->rta_type) {
		case IFLA_IFNAME:
			strncpy(vlan->name, (char *)RTA_DATA(rta), IFNAMSIZ - 1);
			vlan->name[IFNAMSIZ - 1] = '\0';
			break;
			
		case IFLA_LINK:
			vlan->parent_ifindex = *(int *)RTA_DATA(rta);
			if_indextoname(vlan->parent_ifindex, vlan->parent_name);
			break;
			
		case IFLA_LINKINFO: {
			struct rtattr *linkinfo = RTA_DATA(rta);
			int linkinfo_len = RTA_PAYLOAD(rta);
			
			while (RTA_OK(linkinfo, linkinfo_len)) {
				if (linkinfo->rta_type == IFLA_INFO_DATA) {
					struct rtattr *data = RTA_DATA(linkinfo);
					int data_len = RTA_PAYLOAD(linkinfo);
					
					/* Parse VLAN-specific attributes */
					while (RTA_OK(data, data_len)) {
						switch (data->rta_type) {
						case IFLA_VLAN_ID:
							vlan->vlan_id = *(uint16_t *)RTA_DATA(data);
							break;
						case IFLA_VLAN_PROTOCOL:
							vlan->protocol = *(uint16_t *)RTA_DATA(data);
							break;
						}
						data = RTA_NEXT(data, data_len);
					}
				}
				linkinfo = RTA_NEXT(linkinfo, linkinfo_len);
			}
			break;
		}
		}
		
		rta = RTA_NEXT(rta, rtalen);
	}
	
	return 0;
}

int interface_detector_parse_bond(struct interface_detector *detector,
                                  struct nlmsghdr *nlh,
                                  struct bond_info *bond)
{
	struct ifinfomsg *ifi;
	struct rtattr *rta;
	int rtalen;
	
	if (!detector || !nlh || !bond)
		return -1;
	
	if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*ifi)))
		return -1;
	
	ifi = (struct ifinfomsg *)NLMSG_DATA(nlh);
	
	memset(bond, 0, sizeof(*bond));
	bond->ifindex = ifi->ifi_index;
	
	/* Parse attributes */
	rta = IFLA_RTA(ifi);
	rtalen = IFLA_PAYLOAD(nlh);
	
	while (RTA_OK(rta, rtalen)) {
		switch (rta->rta_type) {
		case IFLA_IFNAME:
			strncpy(bond->name, (char *)RTA_DATA(rta), IFNAMSIZ - 1);
			bond->name[IFNAMSIZ - 1] = '\0';
			break;
			
		case IFLA_LINKINFO: {
			struct rtattr *linkinfo = RTA_DATA(rta);
			int linkinfo_len = RTA_PAYLOAD(rta);
			
			while (RTA_OK(linkinfo, linkinfo_len)) {
				if (linkinfo->rta_type == IFLA_INFO_DATA) {
					struct rtattr *data = RTA_DATA(linkinfo);
					int data_len = RTA_PAYLOAD(linkinfo);
					
					/* Parse bond-specific attributes */
					while (RTA_OK(data, data_len)) {
						switch (data->rta_type) {
						case IFLA_BOND_MODE:
							bond->mode = *(uint8_t *)RTA_DATA(data);
							break;
						}
						data = RTA_NEXT(data, data_len);
					}
				}
				linkinfo = RTA_NEXT(linkinfo, linkinfo_len);
			}
			break;
		}
		}
		
		rta = RTA_NEXT(rta, rtalen);
	}
	
	return 0;
}

int interface_detector_get_hierarchy(struct interface_detector *detector,
                                     int ifindex,
                                     int *parent_ifindex,
                                     int *child_ifindices,
                                     int max_children)
{
	int i, child_count = 0;
	
	if (!detector)
		return -1;
	
	if (parent_ifindex)
		*parent_ifindex = -1;
	
	/* Search cache for parent and children */
	for (i = 0; i < detector->cache_count; i++) {
		if (!detector->cache[i].valid)
			continue;
		
		/* Check if this is the interface we're looking for */
		if (detector->cache[i].ifindex == ifindex) {
			if (parent_ifindex)
				*parent_ifindex = detector->cache[i].parent_ifindex;
		}
		
		/* Check if this is a child of our interface */
		if (detector->cache[i].parent_ifindex == ifindex) {
			if (child_ifindices && child_count < max_children) {
				child_ifindices[child_count++] = detector->cache[i].ifindex;
			}
		}
	}
	
	return child_count;
}

const char *interface_type_to_string(interface_type_t type)
{
	switch (type) {
	case IFACE_TYPE_ETHERNET:
		return "ethernet";
	case IFACE_TYPE_LOOPBACK:
		return "loopback";
	case IFACE_TYPE_VETH:
		return "veth";
	case IFACE_TYPE_BRIDGE:
		return "bridge";
	case IFACE_TYPE_VLAN:
		return "vlan";
	case IFACE_TYPE_BOND:
		return "bond";
	case IFACE_TYPE_TUN:
		return "tun";
	case IFACE_TYPE_TAP:
		return "tap";
	case IFACE_TYPE_VXLAN:
		return "vxlan";
	case IFACE_TYPE_MACVLAN:
		return "macvlan";
	case IFACE_TYPE_IPVLAN:
		return "ipvlan";
	default:
		return "unknown";
	}
}

void interface_detector_destroy(struct interface_detector *detector)
{
	if (!detector)
		return;
	
	free(detector);
}
