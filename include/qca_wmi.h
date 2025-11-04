/* QCA WMI (Wireless Module Interface) commands
 * Based on ath10k/ath11k WMI command IDs
 */

#ifndef QCA_WMI_H
#define QCA_WMI_H

#include <stddef.h>
#include <stdint.h>

/* WMI Command IDs - commonly seen in logs */
enum wmi_cmd_id {
	/* Scan commands */
	WMI_START_SCAN_CMDID = 0x9000,
	WMI_STOP_SCAN_CMDID = 0x9001,
	
	/* PDEV (Physical Device) commands */
	WMI_PDEV_SET_REGDOMAIN_CMDID = 0x9002,
	WMI_PDEV_SET_CHANNEL_CMDID = 0x9003,
	WMI_PDEV_SET_PARAM_CMDID = 0x9004,
	WMI_PDEV_PKTLOG_ENABLE_CMDID = 0x9005,
	WMI_PDEV_PKTLOG_DISABLE_CMDID = 0x9006,
	WMI_PDEV_SET_WMM_PARAMS_CMDID = 0x9007,
	WMI_PDEV_SET_HT_CAP_IE_CMDID = 0x9008,
	WMI_PDEV_SET_VHT_CAP_IE_CMDID = 0x9009,
	WMI_PDEV_SET_DSCP_TID_MAP_CMDID = 0x900A,
	WMI_PDEV_SET_QUIET_MODE_CMDID = 0x900B,
	WMI_PDEV_GREEN_AP_PS_ENABLE_CMDID = 0x900C,
	WMI_PDEV_GET_TPC_CONFIG_CMDID = 0x900D,
	WMI_PDEV_SET_BASE_MACADDR_CMDID = 0x900E,
	
	/* VDEV (Virtual Device) commands */
	WMI_VDEV_CREATE_CMDID = 0x9100,
	WMI_VDEV_DELETE_CMDID = 0x9101,
	WMI_VDEV_START_REQUEST_CMDID = 0x9102,
	WMI_VDEV_RESTART_REQUEST_CMDID = 0x9103,
	WMI_VDEV_UP_CMDID = 0x9104,
	WMI_VDEV_STOP_CMDID = 0x9105,
	WMI_VDEV_DOWN_CMDID = 0x9106,
	WMI_VDEV_SET_PARAM_CMDID = 0x9107,
	WMI_VDEV_INSTALL_KEY_CMDID = 0x9108,
	
	/* Peer commands */
	WMI_PEER_CREATE_CMDID = 0x9200,
	WMI_PEER_DELETE_CMDID = 0x9201,
	WMI_PEER_FLUSH_TIDS_CMDID = 0x9202,
	WMI_PEER_SET_PARAM_CMDID = 0x9203,
	WMI_PEER_ASSOC_CMDID = 0x9204,
	WMI_PEER_ADD_WDS_ENTRY_CMDID = 0x9205,
	WMI_PEER_REMOVE_WDS_ENTRY_CMDID = 0x9206,
	WMI_PEER_MCAST_GROUP_CMDID = 0x9207,
	
	/* Beacon/Management commands */
	WMI_BCN_TX_CMDID = 0x9300,
	WMI_PDEV_SEND_BCN_CMDID = 0x9301,
	WMI_BCN_TMPL_CMDID = 0x9302,
	WMI_BCN_FILTER_RX_CMDID = 0x9303,
	WMI_PRB_REQ_FILTER_RX_CMDID = 0x9304,
	WMI_MGMT_TX_CMDID = 0x9305,
	WMI_PRB_TMPL_CMDID = 0x9306,
	
	/* Statistics commands */
	WMI_REQUEST_STATS_CMDID = 0x15F01,  /* 90113 decimal */
	WMI_REQUEST_LINK_STATS_CMDID = 0x16004,  /* 90116 decimal */
	WMI_REQUEST_RCPI_CMDID = 0x1600B,  /* 90123 decimal */
	WMI_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID = 0x9400,
	WMI_VDEV_SPECTRAL_SCAN_ENABLE_CMDID = 0x9401,
	
	/* Power save commands */
	WMI_STA_POWERSAVE_MODE_CMDID = 0x9500,
	WMI_STA_POWERSAVE_PARAM_CMDID = 0x9501,
	WMI_STA_MIMO_PS_MODE_CMDID = 0x9502,
	
	/* P2P commands */
	WMI_P2P_GO_SET_BEACON_IE = 0x9600,
	WMI_P2P_GO_SET_PROBE_RESP_IE = 0x9601,
	WMI_P2P_SET_VENDOR_IE_DATA_CMDID = 0x9602,
	
	/* AP commands */
	WMI_AP_PS_PEER_PARAM_CMDID = 0x9700,
	WMI_AP_PS_PEER_UAPSD_COEX_CMDID = 0x9701,
	
	/* Roaming commands */
	WMI_ROAM_SCAN_MODE = 0x9800,
	WMI_ROAM_SCAN_RSSI_THRESHOLD = 0x9801,
	WMI_ROAM_SCAN_PERIOD = 0x9802,
	WMI_ROAM_SCAN_RSSI_CHANGE_THRESHOLD = 0x9803,
	WMI_ROAM_AP_PROFILE = 0x9804,
	
	/* Offload commands */
	WMI_SET_ARP_NS_OFFLOAD_CMDID = 0x9900,
	WMI_SET_PASSPOINT_NETWORK_LIST_CMDID = 0x9901,
	WMI_SET_EPNO_NETWORK_LIST_CMDID = 0x9902,
	
	/* GTK offload */
	WMI_GTK_OFFLOAD_CMDID = 0x9A00,
	
	/* CSA (Channel Switch Announcement) */
	WMI_CSA_OFFLOAD_ENABLE_CMDID = 0x9B00,
	WMI_CSA_OFFLOAD_CHANSWITCH_CMDID = 0x9B01,
	
	/* CHATTER commands */
	WMI_CHATTER_SET_MODE_CMDID = 0x9C00,
	
	/* ADDBA (Add Block Ack) commands */
	WMI_ADDBA_CLEAR_RESP_CMDID = 0x9D00,
	WMI_ADDBA_SEND_CMDID = 0x9D01,
	WMI_ADDBA_STATUS_CMDID = 0x9D02,
	WMI_DELBA_SEND_CMDID = 0x9D03,
	WMI_ADDBA_SET_RESP_CMDID = 0x9D04,
	WMI_SEND_SINGLEAMSDU_CMDID = 0x9D05,
	
	/* Station list commands */
	WMI_STA_KEEPALIVE_CMD = 0x9E00,
	WMI_STA_KEEPALIVE_ARP_RESPONSE = 0x9E01,
	
	/* WOW (Wake on Wireless) commands */
	WMI_WOW_ADD_WAKE_PATTERN_CMDID = 0x9F00,
	WMI_WOW_DEL_WAKE_PATTERN_CMDID = 0x9F01,
	WMI_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID = 0x9F02,
	WMI_WOW_ENABLE_CMDID = 0x9F03,
	WMI_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID = 0x9F04,
	
	/* RTT (Round Trip Time) commands */
	WMI_RTT_MEASREQ_CMDID = 0xA000,
	WMI_RTT_TSF_CMDID = 0xA001,
	
	/* Spectral scan commands */
	WMI_PDEV_SPECTRAL_SCAN_ENABLE_CMDID = 0xA100,
	WMI_PDEV_SPECTRAL_SCAN_DISABLE_CMDID = 0xA101,
	
	/* NAN (Neighbor Awareness Networking) */
	WMI_NAN_CMDID = 0xA200,
	
	/* Coex (Coexistence) commands */
	WMI_COEX_CONFIG_CMDID = 0xA300,
	
	/* LPI (Low Power Indoor) commands */
	WMI_LPI_MGMT_SNOOPING_CONFIG_CMDID = 0xA400,
	WMI_LPI_START_SCAN_CMDID = 0xA401,
	WMI_LPI_STOP_SCAN_CMDID = 0xA402,
	
	/* Thermal management */
	WMI_PDEV_SET_THERMAL_THROTTLING_CMDID = 0xA500,
	
	/* Debug/logging commands */
	WMI_DBGLOG_CFG_CMDID = 0x1CF04,  /* 118532 decimal */
	WMI_DBGLOG_TIME_STAMP_SYNC_CMDID = 0x1CFF4,  /* 118804 decimal */
	
	/* Firmware test commands */
	WMI_PDEV_UTF_CMDID = 0xA600,
	WMI_PDEV_QVIT_CMDID = 0xA601,
	
	/* Misc commands */
	WMI_ECHO_CMDID = 0xA700,
	WMI_PDEV_FTM_INTG_CMDID = 0xA701,
	WMI_VDEV_SET_KEEPALIVE_CMDID = 0xA702,
	WMI_VDEV_GET_KEEPALIVE_CMDID = 0xA703,
	WMI_FORCE_FW_HANG_CMDID = 0xA704,
	
	/* GPIO commands */
	WMI_GPIO_CONFIG_CMDID = 0xA800,
	WMI_GPIO_OUTPUT_CMDID = 0xA801,
	
	/* Peer rate commands */
	WMI_PEER_SET_RATE_REPORT_CONDITION_CMDID = 0xA900,
	
	/* TDLS commands */
	WMI_TDLS_SET_STATE_CMDID = 0xAA00,
	WMI_TDLS_PEER_UPDATE_CMDID = 0xAA01,
	
	/* Host offload commands */
	WMI_SET_DHCP_SERVER_OFFLOAD_CMDID = 0xAB00,
	WMI_SET_LED_FLASHING_CMDID = 0xAB01,
	WMI_MDNS_OFFLOAD_ENABLE_CMDID = 0xAB02,
	WMI_MDNS_SET_FQDN_CMDID = 0xAB03,
	WMI_MDNS_SET_RESPONSE_CMDID = 0xAB04,
	WMI_MDNS_GET_STATS_CMDID = 0xAB05,
	
	/* OCB (Outside Context of BSS) commands */
	WMI_OCB_SET_SCHED_CMDID = 0xAC00,
	WMI_OCB_SET_CONFIG_CMDID = 0xAC01,
	WMI_OCB_SET_UTC_TIME_CMDID = 0xAC02,
	WMI_OCB_START_TIMING_ADVERT_CMDID = 0xAC03,
	WMI_OCB_STOP_TIMING_ADVERT_CMDID = 0xAC04,
	WMI_OCB_GET_TSF_TIMER_CMDID = 0xAC05,
	
	/* System-level commands */
	WMI_PDEV_GET_TEMPERATURE_CMDID = 0xAD00,
	WMI_SET_ANTENNA_DIVERSITY_CMDID = 0xAD01,
	
	/* DFS (Dynamic Frequency Selection) commands */
	WMI_PDEV_DFS_ENABLE_CMDID = 0xAE00,
	WMI_PDEV_DFS_DISABLE_CMDID = 0xAE01,
	WMI_DFS_PHYERR_FILTER_ENA_CMDID = 0xAE02,
	WMI_DFS_PHYERR_FILTER_DIS_CMDID = 0xAE03,
	
	/* Packet filtering */
	WMI_PACKET_FILTER_CONFIG_CMDID = 0xAF00,
	WMI_PACKET_FILTER_ENABLE_CMDID = 0xAF01,
	
	/* MAWC (Motion Aided WiFi Connectivity) */
	WMI_MAWC_SENSOR_REPORT_IND_CMDID = 0xB000,
	
	/* BPF (Berkeley Packet Filter) offload */
	WMI_BPF_GET_CAPABILITY_CMDID = 0xB100,
	WMI_BPF_GET_VDEV_STATS_CMDID = 0xB101,
	WMI_BPF_SET_VDEV_INSTRUCTIONS_CMDID = 0xB102,
	WMI_BPF_DEL_VDEV_INSTRUCTIONS_CMDID = 0xB103,
	
	/* Vendor specific commands */
	WMI_PDEV_GET_ANI_CCK_CONFIG_CMDID = 0xB200,
	WMI_PDEV_GET_ANI_OFDM_CONFIG_CMDID = 0xB201,
};

/* WMI Statistics Types */
enum wmi_stats_type {
	WMI_STATS_TYPE_BASIC = 4,
	WMI_STATS_TYPE_LINK_LAYER = 7,
	WMI_STATS_TYPE_CONGESTION = 8463,
};

/* WMI log entry structure */
struct wmi_log_entry {
	/* Timing information */
	uint64_t timestamp;          /* Microseconds since epoch */
	char timestamp_str[32];      /* Original timestamp string */
	
	/* Command information */
	uint32_t cmd_id;             /* WMI command ID (e.g., 90116) */
	char command_name[64];       /* Decoded name (e.g., "REQUEST_LINK_STATS") */
	
	/* Device/Interface information */
	uint32_t vdev_id;            /* Virtual device ID */
	uint32_t pdev_id;            /* Physical device ID */
	
	/* Statistics information */
	uint32_t stats_id;           /* Statistics type ID */
	char stats_type[32];         /* Decoded stats type */
	uint32_t req_id;             /* Request ID */
	
	/* Peer information */
	char peer_mac[18];           /* MAC address (XX:XX:XX:XX:XX:XX) */
	
	/* Protocol information */
	uint32_t htc_tag;            /* HTC tag */
	
	/* Context information */
	char thread_name[16];        /* Thread/process name (e.g., "schedu", "wpa_su") */
	uint64_t thread_id;          /* Thread ID from log */
	
	/* Timestamp sync specific */
	uint32_t mode;               /* Mode for timestamp sync */
	uint32_t timestamp_low;      /* Low 32 bits of timestamp */
	uint32_t timestamp_high;     /* High 32 bits of timestamp */
	
	/* Flags */
	uint8_t has_stats:1;         /* Entry contains stats information */
	uint8_t has_peer:1;          /* Entry contains peer information */
	uint8_t has_timestamp:1;     /* Entry contains timestamp */
	uint8_t reserved:5;
};

/* Helper function to get WMI command name */
const char *wmi_cmd_to_string(unsigned int cmd_id);

/* Convert stats type ID to string */
const char *wmi_stats_to_string(unsigned int stats_id);

/* Parse WMI command from log string */
int parse_wmi_from_log(const char *log_line, unsigned int *cmd_id, char *cmd_name, size_t name_len);

/* Parse WMI log line into structured entry */
int wmi_parse_log_line(const char *log_line, struct wmi_log_entry *entry);

/* Format WMI entry for display */
int wmi_format_entry(const struct wmi_log_entry *entry, char *buf, size_t len);

#endif /* QCA_WMI_H */
