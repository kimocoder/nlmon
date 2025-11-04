#ifndef INTERFACE_DETECTOR_H
#define INTERFACE_DETECTOR_H

#include <stdint.h>
#include <linux/if.h>
#include <linux/netlink.h>

/* Interface types */
typedef enum {
	IFACE_TYPE_UNKNOWN,
	IFACE_TYPE_ETHERNET,
	IFACE_TYPE_LOOPBACK,
	IFACE_TYPE_VETH,
	IFACE_TYPE_BRIDGE,
	IFACE_TYPE_VLAN,
	IFACE_TYPE_BOND,
	IFACE_TYPE_TUN,
	IFACE_TYPE_TAP,
	IFACE_TYPE_VXLAN,
	IFACE_TYPE_MACVLAN,
	IFACE_TYPE_IPVLAN,
} interface_type_t;

/* Bridge interface information */
struct bridge_info {
	char name[IFNAMSIZ];
	int ifindex;
	uint32_t forward_delay;
	uint32_t hello_time;
	uint32_t max_age;
	uint16_t priority;
	uint8_t stp_enabled;
	
	/* Member ports */
	int num_ports;
	int port_ifindices[256];
	char port_names[256][IFNAMSIZ];
};

/* VLAN interface information */
struct vlan_info {
	char name[IFNAMSIZ];
	int ifindex;
	uint16_t vlan_id;
	int parent_ifindex;
	char parent_name[IFNAMSIZ];
	uint16_t protocol;  /* 0x8100 for 802.1Q, 0x88a8 for 802.1ad */
};

/* Bond interface information */
struct bond_info {
	char name[IFNAMSIZ];
	int ifindex;
	uint8_t mode;  /* bonding mode */
	
	/* Slave interfaces */
	int num_slaves;
	int slave_ifindices[32];
	char slave_names[32][IFNAMSIZ];
};

/* Generic interface information */
struct interface_info {
	char name[IFNAMSIZ];
	int ifindex;
	interface_type_t type;
	unsigned int flags;
	uint8_t mac_addr[6];
	int mtu;
	
	/* Type-specific info */
	union {
		struct bridge_info bridge;
		struct vlan_info vlan;
		struct bond_info bond;
	} type_info;
};

/* Interface detector context */
struct interface_detector;

/* Initialize interface detector */
struct interface_detector *interface_detector_init(void);

/* Detect interface type from netlink message */
interface_type_t interface_detector_detect_type(struct interface_detector *detector,
                                                struct nlmsghdr *nlh);

/* Parse interface information from netlink message */
int interface_detector_parse_info(struct interface_detector *detector,
                                  struct nlmsghdr *nlh,
                                  struct interface_info *info);

/* Parse bridge-specific information */
int interface_detector_parse_bridge(struct interface_detector *detector,
                                    struct nlmsghdr *nlh,
                                    struct bridge_info *bridge);

/* Parse VLAN-specific information */
int interface_detector_parse_vlan(struct interface_detector *detector,
                                  struct nlmsghdr *nlh,
                                  struct vlan_info *vlan);

/* Parse bond-specific information */
int interface_detector_parse_bond(struct interface_detector *detector,
                                  struct nlmsghdr *nlh,
                                  struct bond_info *bond);

/* Get interface hierarchy (parent/child relationships) */
int interface_detector_get_hierarchy(struct interface_detector *detector,
                                     int ifindex,
                                     int *parent_ifindex,
                                     int *child_ifindices,
                                     int max_children);

/* Convert interface type to string */
const char *interface_type_to_string(interface_type_t type);

/* Cleanup */
void interface_detector_destroy(struct interface_detector *detector);

#endif /* INTERFACE_DETECTOR_H */
