/* QCA WMI command decoder */

#include "qca_wmi.h"
#include "wmi_error.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

/* Global error statistics for WMI parsing */
static struct wmi_error_stats g_wmi_parse_stats = {0};

/* Convert WMI command ID to string */
const char *wmi_cmd_to_string(unsigned int cmd_id)
{
	switch (cmd_id) {
	/* Scan commands */
	case WMI_START_SCAN_CMDID: return "START_SCAN";
	case WMI_STOP_SCAN_CMDID: return "STOP_SCAN";
	
	/* PDEV commands */
	case WMI_PDEV_SET_REGDOMAIN_CMDID: return "PDEV_SET_REGDOMAIN";
	case WMI_PDEV_SET_CHANNEL_CMDID: return "PDEV_SET_CHANNEL";
	case WMI_PDEV_SET_PARAM_CMDID: return "PDEV_SET_PARAM";
	case WMI_PDEV_PKTLOG_ENABLE_CMDID: return "PDEV_PKTLOG_ENABLE";
	case WMI_PDEV_PKTLOG_DISABLE_CMDID: return "PDEV_PKTLOG_DISABLE";
	case WMI_PDEV_SET_WMM_PARAMS_CMDID: return "PDEV_SET_WMM_PARAMS";
	case WMI_PDEV_SET_HT_CAP_IE_CMDID: return "PDEV_SET_HT_CAP_IE";
	case WMI_PDEV_SET_VHT_CAP_IE_CMDID: return "PDEV_SET_VHT_CAP_IE";
	case WMI_PDEV_SET_DSCP_TID_MAP_CMDID: return "PDEV_SET_DSCP_TID_MAP";
	case WMI_PDEV_SET_QUIET_MODE_CMDID: return "PDEV_SET_QUIET_MODE";
	case WMI_PDEV_GREEN_AP_PS_ENABLE_CMDID: return "PDEV_GREEN_AP_PS_ENABLE";
	case WMI_PDEV_GET_TPC_CONFIG_CMDID: return "PDEV_GET_TPC_CONFIG";
	case WMI_PDEV_SET_BASE_MACADDR_CMDID: return "PDEV_SET_BASE_MACADDR";
	
	/* VDEV commands */
	case WMI_VDEV_CREATE_CMDID: return "VDEV_CREATE";
	case WMI_VDEV_DELETE_CMDID: return "VDEV_DELETE";
	case WMI_VDEV_START_REQUEST_CMDID: return "VDEV_START_REQUEST";
	case WMI_VDEV_RESTART_REQUEST_CMDID: return "VDEV_RESTART_REQUEST";
	case WMI_VDEV_UP_CMDID: return "VDEV_UP";
	case WMI_VDEV_STOP_CMDID: return "VDEV_STOP";
	case WMI_VDEV_DOWN_CMDID: return "VDEV_DOWN";
	case WMI_VDEV_SET_PARAM_CMDID: return "VDEV_SET_PARAM";
	case WMI_VDEV_INSTALL_KEY_CMDID: return "VDEV_INSTALL_KEY";
	
	/* Peer commands */
	case WMI_PEER_CREATE_CMDID: return "PEER_CREATE";
	case WMI_PEER_DELETE_CMDID: return "PEER_DELETE";
	case WMI_PEER_FLUSH_TIDS_CMDID: return "PEER_FLUSH_TIDS";
	case WMI_PEER_SET_PARAM_CMDID: return "PEER_SET_PARAM";
	case WMI_PEER_ASSOC_CMDID: return "PEER_ASSOC";
	case WMI_PEER_ADD_WDS_ENTRY_CMDID: return "PEER_ADD_WDS_ENTRY";
	case WMI_PEER_REMOVE_WDS_ENTRY_CMDID: return "PEER_REMOVE_WDS_ENTRY";
	case WMI_PEER_MCAST_GROUP_CMDID: return "PEER_MCAST_GROUP";
	
	/* Beacon/Management commands */
	case WMI_BCN_TX_CMDID: return "BCN_TX";
	case WMI_PDEV_SEND_BCN_CMDID: return "PDEV_SEND_BCN";
	case WMI_BCN_TMPL_CMDID: return "BCN_TMPL";
	case WMI_BCN_FILTER_RX_CMDID: return "BCN_FILTER_RX";
	case WMI_PRB_REQ_FILTER_RX_CMDID: return "PRB_REQ_FILTER_RX";
	case WMI_MGMT_TX_CMDID: return "MGMT_TX";
	case WMI_PRB_TMPL_CMDID: return "PRB_TMPL";
	
	/* Statistics commands */
	case WMI_REQUEST_STATS_CMDID: return "REQUEST_STATS";
	case WMI_REQUEST_LINK_STATS_CMDID: return "REQUEST_LINK_STATS";
	case WMI_REQUEST_RCPI_CMDID: return "REQUEST_RCPI";
	case WMI_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID: return "VDEV_SPECTRAL_SCAN_CONFIGURE";
	case WMI_VDEV_SPECTRAL_SCAN_ENABLE_CMDID: return "VDEV_SPECTRAL_SCAN_ENABLE";
	
	/* Power save commands */
	case WMI_STA_POWERSAVE_MODE_CMDID: return "STA_POWERSAVE_MODE";
	case WMI_STA_POWERSAVE_PARAM_CMDID: return "STA_POWERSAVE_PARAM";
	case WMI_STA_MIMO_PS_MODE_CMDID: return "STA_MIMO_PS_MODE";
	
	/* P2P commands */
	case WMI_P2P_GO_SET_BEACON_IE: return "P2P_GO_SET_BEACON_IE";
	case WMI_P2P_GO_SET_PROBE_RESP_IE: return "P2P_GO_SET_PROBE_RESP_IE";
	case WMI_P2P_SET_VENDOR_IE_DATA_CMDID: return "P2P_SET_VENDOR_IE_DATA";
	
	/* AP commands */
	case WMI_AP_PS_PEER_PARAM_CMDID: return "AP_PS_PEER_PARAM";
	case WMI_AP_PS_PEER_UAPSD_COEX_CMDID: return "AP_PS_PEER_UAPSD_COEX";
	
	/* Roaming commands */
	case WMI_ROAM_SCAN_MODE: return "ROAM_SCAN_MODE";
	case WMI_ROAM_SCAN_RSSI_THRESHOLD: return "ROAM_SCAN_RSSI_THRESHOLD";
	case WMI_ROAM_SCAN_PERIOD: return "ROAM_SCAN_PERIOD";
	case WMI_ROAM_SCAN_RSSI_CHANGE_THRESHOLD: return "ROAM_SCAN_RSSI_CHANGE_THRESHOLD";
	case WMI_ROAM_AP_PROFILE: return "ROAM_AP_PROFILE";
	
	/* Offload commands */
	case WMI_SET_ARP_NS_OFFLOAD_CMDID: return "SET_ARP_NS_OFFLOAD";
	case WMI_SET_PASSPOINT_NETWORK_LIST_CMDID: return "SET_PASSPOINT_NETWORK_LIST";
	case WMI_SET_EPNO_NETWORK_LIST_CMDID: return "SET_EPNO_NETWORK_LIST";
	
	/* GTK offload */
	case WMI_GTK_OFFLOAD_CMDID: return "GTK_OFFLOAD";
	
	/* CSA */
	case WMI_CSA_OFFLOAD_ENABLE_CMDID: return "CSA_OFFLOAD_ENABLE";
	case WMI_CSA_OFFLOAD_CHANSWITCH_CMDID: return "CSA_OFFLOAD_CHANSWITCH";
	
	/* CHATTER */
	case WMI_CHATTER_SET_MODE_CMDID: return "CHATTER_SET_MODE";
	
	/* ADDBA */
	case WMI_ADDBA_CLEAR_RESP_CMDID: return "ADDBA_CLEAR_RESP";
	case WMI_ADDBA_SEND_CMDID: return "ADDBA_SEND";
	case WMI_ADDBA_STATUS_CMDID: return "ADDBA_STATUS";
	case WMI_DELBA_SEND_CMDID: return "DELBA_SEND";
	case WMI_ADDBA_SET_RESP_CMDID: return "ADDBA_SET_RESP";
	case WMI_SEND_SINGLEAMSDU_CMDID: return "SEND_SINGLEAMSDU";
	
	/* Station list */
	case WMI_STA_KEEPALIVE_CMD: return "STA_KEEPALIVE";
	case WMI_STA_KEEPALIVE_ARP_RESPONSE: return "STA_KEEPALIVE_ARP_RESPONSE";
	
	/* WOW */
	case WMI_WOW_ADD_WAKE_PATTERN_CMDID: return "WOW_ADD_WAKE_PATTERN";
	case WMI_WOW_DEL_WAKE_PATTERN_CMDID: return "WOW_DEL_WAKE_PATTERN";
	case WMI_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID: return "WOW_ENABLE_DISABLE_WAKE_EVENT";
	case WMI_WOW_ENABLE_CMDID: return "WOW_ENABLE";
	case WMI_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID: return "WOW_HOSTWAKEUP_FROM_SLEEP";
	
	/* RTT */
	case WMI_RTT_MEASREQ_CMDID: return "RTT_MEASREQ";
	case WMI_RTT_TSF_CMDID: return "RTT_TSF";
	
	/* Spectral scan */
	case WMI_PDEV_SPECTRAL_SCAN_ENABLE_CMDID: return "PDEV_SPECTRAL_SCAN_ENABLE";
	case WMI_PDEV_SPECTRAL_SCAN_DISABLE_CMDID: return "PDEV_SPECTRAL_SCAN_DISABLE";
	
	/* NAN */
	case WMI_NAN_CMDID: return "NAN";
	
	/* Coex */
	case WMI_COEX_CONFIG_CMDID: return "COEX_CONFIG";
	
	/* LPI */
	case WMI_LPI_MGMT_SNOOPING_CONFIG_CMDID: return "LPI_MGMT_SNOOPING_CONFIG";
	case WMI_LPI_START_SCAN_CMDID: return "LPI_START_SCAN";
	case WMI_LPI_STOP_SCAN_CMDID: return "LPI_STOP_SCAN";
	
	/* Thermal */
	case WMI_PDEV_SET_THERMAL_THROTTLING_CMDID: return "PDEV_SET_THERMAL_THROTTLING";
	
	/* Debug/logging */
	case WMI_DBGLOG_CFG_CMDID: return "DBGLOG_CFG";
	case WMI_DBGLOG_TIME_STAMP_SYNC_CMDID: return "DBGLOG_TIME_STAMP_SYNC";
	
	/* Firmware test */
	case WMI_PDEV_UTF_CMDID: return "PDEV_UTF";
	case WMI_PDEV_QVIT_CMDID: return "PDEV_QVIT";
	
	/* Misc */
	case WMI_ECHO_CMDID: return "ECHO";
	case WMI_PDEV_FTM_INTG_CMDID: return "PDEV_FTM_INTG";
	case WMI_VDEV_SET_KEEPALIVE_CMDID: return "VDEV_SET_KEEPALIVE";
	case WMI_VDEV_GET_KEEPALIVE_CMDID: return "VDEV_GET_KEEPALIVE";
	case WMI_FORCE_FW_HANG_CMDID: return "FORCE_FW_HANG";
	
	/* GPIO */
	case WMI_GPIO_CONFIG_CMDID: return "GPIO_CONFIG";
	case WMI_GPIO_OUTPUT_CMDID: return "GPIO_OUTPUT";
	
	/* Peer rate */
	case WMI_PEER_SET_RATE_REPORT_CONDITION_CMDID: return "PEER_SET_RATE_REPORT_CONDITION";
	
	/* TDLS */
	case WMI_TDLS_SET_STATE_CMDID: return "TDLS_SET_STATE";
	case WMI_TDLS_PEER_UPDATE_CMDID: return "TDLS_PEER_UPDATE";
	
	/* Host offload */
	case WMI_SET_DHCP_SERVER_OFFLOAD_CMDID: return "SET_DHCP_SERVER_OFFLOAD";
	case WMI_SET_LED_FLASHING_CMDID: return "SET_LED_FLASHING";
	case WMI_MDNS_OFFLOAD_ENABLE_CMDID: return "MDNS_OFFLOAD_ENABLE";
	case WMI_MDNS_SET_FQDN_CMDID: return "MDNS_SET_FQDN";
	case WMI_MDNS_SET_RESPONSE_CMDID: return "MDNS_SET_RESPONSE";
	case WMI_MDNS_GET_STATS_CMDID: return "MDNS_GET_STATS";
	
	/* OCB */
	case WMI_OCB_SET_SCHED_CMDID: return "OCB_SET_SCHED";
	case WMI_OCB_SET_CONFIG_CMDID: return "OCB_SET_CONFIG";
	case WMI_OCB_SET_UTC_TIME_CMDID: return "OCB_SET_UTC_TIME";
	case WMI_OCB_START_TIMING_ADVERT_CMDID: return "OCB_START_TIMING_ADVERT";
	case WMI_OCB_STOP_TIMING_ADVERT_CMDID: return "OCB_STOP_TIMING_ADVERT";
	case WMI_OCB_GET_TSF_TIMER_CMDID: return "OCB_GET_TSF_TIMER";
	
	/* System-level */
	case WMI_PDEV_GET_TEMPERATURE_CMDID: return "PDEV_GET_TEMPERATURE";
	case WMI_SET_ANTENNA_DIVERSITY_CMDID: return "SET_ANTENNA_DIVERSITY";
	
	/* DFS */
	case WMI_PDEV_DFS_ENABLE_CMDID: return "PDEV_DFS_ENABLE";
	case WMI_PDEV_DFS_DISABLE_CMDID: return "PDEV_DFS_DISABLE";
	case WMI_DFS_PHYERR_FILTER_ENA_CMDID: return "DFS_PHYERR_FILTER_ENA";
	case WMI_DFS_PHYERR_FILTER_DIS_CMDID: return "DFS_PHYERR_FILTER_DIS";
	
	/* Packet filtering */
	case WMI_PACKET_FILTER_CONFIG_CMDID: return "PACKET_FILTER_CONFIG";
	case WMI_PACKET_FILTER_ENABLE_CMDID: return "PACKET_FILTER_ENABLE";
	
	/* MAWC */
	case WMI_MAWC_SENSOR_REPORT_IND_CMDID: return "MAWC_SENSOR_REPORT_IND";
	
	/* BPF offload */
	case WMI_BPF_GET_CAPABILITY_CMDID: return "BPF_GET_CAPABILITY";
	case WMI_BPF_GET_VDEV_STATS_CMDID: return "BPF_GET_VDEV_STATS";
	case WMI_BPF_SET_VDEV_INSTRUCTIONS_CMDID: return "BPF_SET_VDEV_INSTRUCTIONS";
	case WMI_BPF_DEL_VDEV_INSTRUCTIONS_CMDID: return "BPF_DEL_VDEV_INSTRUCTIONS";
	
	/* Vendor specific */
	case WMI_PDEV_GET_ANI_CCK_CONFIG_CMDID: return "PDEV_GET_ANI_CCK_CONFIG";
	case WMI_PDEV_GET_ANI_OFDM_CONFIG_CMDID: return "PDEV_GET_ANI_OFDM_CONFIG";
	
	default:
		return "UNKNOWN";
	}
}

/* Convert stats type ID to string */
const char *wmi_stats_to_string(unsigned int stats_id)
{
	switch (stats_id) {
	case WMI_STATS_TYPE_BASIC:
		return "BASIC_STATS";
	case WMI_STATS_TYPE_LINK_LAYER:
		return "LINK_LAYER_STATS";
	case WMI_STATS_TYPE_CONGESTION:
		return "CONGESTION";
	default:
		return "UNKNOWN_STATS";
	}
}

/* Parse WMI command from log string
 * Example: "Send WMI command:WMI_REQUEST_LINK_STATS_CMDID command_id:90116"
 */
int parse_wmi_from_log(const char *log_line, unsigned int *cmd_id, char *cmd_name, size_t name_len)
{
	const char *cmd_start, *id_start;
	char *end;
	unsigned long id;
	
	if (!log_line || !cmd_id || !cmd_name)
		return -1;
	
	/* Look for "WMI command:" pattern */
	cmd_start = strstr(log_line, "WMI command:");
	if (!cmd_start)
		return -1;
	
	cmd_start += 12; /* Skip "WMI command:" */
	
	/* Look for "command_id:" pattern */
	id_start = strstr(cmd_start, "command_id:");
	if (!id_start)
		return -1;
	
	id_start += 11; /* Skip "command_id:" */
	
	/* Parse command ID */
	id = strtoul(id_start, &end, 10);
	if (end == id_start)
		return -1;
	
	*cmd_id = (unsigned int)id;
	
	/* Extract command name */
	const char *name_end = id_start - 12; /* Back to before " command_id:" */
	while (name_end > cmd_start && *name_end == ' ')
		name_end--;
	
	size_t len = name_end - cmd_start + 1;
	if (len >= name_len)
		len = name_len - 1;
	
	strncpy(cmd_name, cmd_start, len);
	cmd_name[len] = '\0';
	
	return 0;
}

/* Helper: Parse hex timestamp [0xXXXXXXXXXX] */
static int parse_hex_timestamp(const char *str, uint64_t *timestamp)
{
	const char *start = strchr(str, '[');
	if (!start)
		return -1;
	
	start++;
	/* Must start with 0x for hex format */
	if (strncmp(start, "0x", 2) != 0 && strncmp(start, "0X", 2) != 0)
		return -1;
	
	start += 2;
	char *end;
	*timestamp = strtoull(start, &end, 16);
	return (end > start) ? 0 : -1;
}

/* Helper: Parse time format [HH:MM:SS.microseconds] */
static int parse_time_format(const char *str, uint64_t *timestamp)
{
	const char *start = strchr(str, '[');
	if (!start)
		return -1;
	
	start++;
	int hour, min, sec, usec;
	if (sscanf(start, "%d:%d:%d.%d", &hour, &min, &sec, &usec) == 4) {
		/* Convert to microseconds (relative time) */
		*timestamp = ((uint64_t)hour * 3600 + min * 60 + sec) * 1000000ULL + usec;
		return 0;
	}
	
	return -1;
}

/* Helper: Validate MAC address format */
static int validate_mac_address(const char *mac)
{
	if (!mac)
		return 0;
	
	/* MAC should be XX:XX:XX:XX:XX:XX (17 chars) */
	if (strlen(mac) != 17)
		return 0;
	
	for (int i = 0; i < 17; i++) {
		if (i % 3 == 2) {
			if (mac[i] != ':')
				return 0;
		} else {
			if (!isxdigit(mac[i]))
				return 0;
		}
	}
	
	return 1;
}

/* Helper: Extract thread name from log line */
static void extract_thread_name(const char *log_line, char *thread_name, size_t len)
{
	/* Look for patterns like "schedu:", "wpa_su:", etc. */
	const char *p = log_line;
	while (*p) {
		if (isalpha(*p) || *p == '_') {
			const char *start = p;
			while (isalnum(*p) || *p == '_')
				p++;
			
			if (*p == ':' && (p - start) < (int)len) {
				size_t name_len = p - start;
				strncpy(thread_name, start, name_len);
				thread_name[name_len] = '\0';
				return;
			}
		}
		p++;
	}
	thread_name[0] = '\0';
}

/* Parse WMI log line into structured entry
 * Supports 5 log formats:
 * 1. "Send WMI command:WMI_*_CMDID command_id:XXXXX htc_tag:X"
 * 2. "LINK_LAYER_STATS - Get Request Params Request ID: X Stats Type: X Vdev ID: X Peer MAC Addr: XX:XX:XX:XX:XX:XX"
 * 3. "STATS REQ STATS_ID:XXXX VDEV_ID:X PDEV_ID:X-->"
 * 4. "RCPI REQ VDEV_ID:X-->"
 * 5. "send_time_stamp_sync_cmd_tlv: XXXX: WMA --> DBGLOG_TIME_STAMP_SYNC_CMDID mode X time_stamp low XXXX high XXXX"
 */
int wmi_parse_log_line(const char *log_line, struct wmi_log_entry *entry)
{
	if (!log_line) {
		WMI_LOG_ERROR(WMI_ERR_NULL_POINTER, "log_line is NULL");
		wmi_error_stats_record(&g_wmi_parse_stats, WMI_ERR_NULL_POINTER, NULL);
		return WMI_ERR_NULL_POINTER;
	}
	
	if (!entry) {
		WMI_LOG_ERROR(WMI_ERR_NULL_POINTER, "entry is NULL");
		wmi_error_stats_record(&g_wmi_parse_stats, WMI_ERR_NULL_POINTER, NULL);
		return WMI_ERR_NULL_POINTER;
	}
	
	/* Check for empty or very short lines */
	size_t line_len = strlen(log_line);
	if (line_len < 10) {
		WMI_LOG_WARNING_FMT(WMI_ERR_TRUNCATED_LINE, 
		                   "Line too short: %zu bytes", line_len);
		wmi_error_stats_record(&g_wmi_parse_stats, WMI_ERR_TRUNCATED_LINE,
		                      "Line too short");
		return WMI_ERR_TRUNCATED_LINE;
	}
	
	/* Track total operations */
	g_wmi_parse_stats.total_operations++;
	
	/* Initialize entry */
	memset(entry, 0, sizeof(*entry));
	
	/* Extract thread name */
	extract_thread_name(log_line, entry->thread_name, sizeof(entry->thread_name));
	
	/* Try to parse timestamp */
	if (parse_hex_timestamp(log_line, &entry->timestamp) == 0 ||
	    parse_time_format(log_line, &entry->timestamp) == 0) {
		entry->has_timestamp = 1;
	}
	
	/* Format 1: "Send WMI command:" */
	if (strstr(log_line, "Send WMI command:")) {
		const char *cmd_start = strstr(log_line, "WMI command:");
		if (cmd_start) {
			cmd_start += 12;
			
			/* Extract command name */
			const char *cmd_end = strstr(cmd_start, " command_id:");
			if (cmd_end) {
				size_t len = cmd_end - cmd_start;
				if (len >= sizeof(entry->command_name))
					len = sizeof(entry->command_name) - 1;
				strncpy(entry->command_name, cmd_start, len);
				entry->command_name[len] = '\0';
			}
			
			/* Parse command_id */
			const char *id_str = strstr(log_line, "command_id:");
			if (id_str) {
				entry->cmd_id = strtoul(id_str + 11, NULL, 10);
				
				/* Check if command ID is known */
				const char *cmd_name = wmi_cmd_to_string(entry->cmd_id);
				if (strcmp(cmd_name, "UNKNOWN") == 0) {
					WMI_LOG_WARNING_FMT(WMI_ERR_INVALID_CMD_ID,
					                   "Unknown command ID: 0x%x", entry->cmd_id);
					wmi_error_stats_record(&g_wmi_parse_stats, 
					                      WMI_ERR_INVALID_CMD_ID,
					                      "Unknown command ID");
				}
			}
			
			/* Parse htc_tag */
			const char *tag_str = strstr(log_line, "htc_tag:");
			if (tag_str) {
				entry->htc_tag = strtoul(tag_str + 8, NULL, 10);
			}
			
			return WMI_SUCCESS;
		}
	}
	
	/* Format 2: "LINK_LAYER_STATS - Get Request Params" */
	if (strstr(log_line, "LINK_LAYER_STATS - Get Request Params")) {
		entry->cmd_id = WMI_REQUEST_LINK_STATS_CMDID;
		snprintf(entry->command_name, sizeof(entry->command_name), "REQUEST_LINK_STATS");
		
		/* Parse Request ID */
		const char *req_str = strstr(log_line, "Request ID:");
		if (req_str) {
			entry->req_id = strtoul(req_str + 11, NULL, 10);
		}
		
		/* Parse Stats Type */
		const char *stats_str = strstr(log_line, "Stats Type:");
		if (stats_str) {
			entry->stats_id = strtoul(stats_str + 11, NULL, 10);
			snprintf(entry->stats_type, sizeof(entry->stats_type), "%s",
			         wmi_stats_to_string(entry->stats_id));
			entry->has_stats = 1;
		}
		
		/* Parse Vdev ID */
		const char *vdev_str = strstr(log_line, "Vdev ID:");
		if (vdev_str) {
			entry->vdev_id = strtoul(vdev_str + 8, NULL, 10);
		}
		
		/* Parse Peer MAC Addr */
		const char *mac_str = strstr(log_line, "Peer MAC Addr:");
		if (mac_str) {
			mac_str += 14;
			while (*mac_str == ' ') mac_str++;
			
			/* Extract MAC address */
			int i;
			for (i = 0; i < 17 && mac_str[i]; i++) {
				if (mac_str[i] == ':' || isxdigit(mac_str[i])) {
					entry->peer_mac[i] = mac_str[i];
				} else {
					break;
				}
			}
			entry->peer_mac[i] = '\0';
			
			/* Validate MAC address */
			if (i == 17) {
				if (validate_mac_address(entry->peer_mac)) {
					entry->has_peer = 1;
				} else {
					WMI_LOG_WARNING_FMT(WMI_ERR_INVALID_MAC,
					                   "Invalid MAC address: %s", entry->peer_mac);
					wmi_error_stats_record(&g_wmi_parse_stats, WMI_ERR_INVALID_MAC,
					                      entry->peer_mac);
					entry->peer_mac[0] = '\0';
				}
			} else if (i > 0) {
				WMI_LOG_WARNING_FMT(WMI_ERR_INVALID_MAC,
				                   "Incomplete MAC address: %d chars", i);
				wmi_error_stats_record(&g_wmi_parse_stats, WMI_ERR_INVALID_MAC,
				                      "Incomplete MAC");
				entry->peer_mac[0] = '\0';
			}
		}
		
		return WMI_SUCCESS;
	}
	
	/* Format 3: "STATS REQ STATS_ID:" */
	if (strstr(log_line, "STATS REQ STATS_ID:")) {
		entry->cmd_id = WMI_REQUEST_STATS_CMDID;
		snprintf(entry->command_name, sizeof(entry->command_name), "REQUEST_STATS");
		
		/* Parse STATS_ID */
		const char *stats_str = strstr(log_line, "STATS_ID:");
		if (stats_str) {
			entry->stats_id = strtoul(stats_str + 9, NULL, 10);
			snprintf(entry->stats_type, sizeof(entry->stats_type), "%s",
			         wmi_stats_to_string(entry->stats_id));
			entry->has_stats = 1;
		}
		
		/* Parse VDEV_ID */
		const char *vdev_str = strstr(log_line, "VDEV_ID:");
		if (vdev_str) {
			entry->vdev_id = strtoul(vdev_str + 8, NULL, 10);
		}
		
		/* Parse PDEV_ID */
		const char *pdev_str = strstr(log_line, "PDEV_ID:");
		if (pdev_str) {
			entry->pdev_id = strtoul(pdev_str + 8, NULL, 10);
		}
		
		return WMI_SUCCESS;
	}
	
	/* Format 4: "RCPI REQ VDEV_ID:" */
	if (strstr(log_line, "RCPI REQ VDEV_ID:")) {
		entry->cmd_id = WMI_REQUEST_RCPI_CMDID;
		snprintf(entry->command_name, sizeof(entry->command_name), "REQUEST_RCPI");
		
		/* Parse VDEV_ID */
		const char *vdev_str = strstr(log_line, "VDEV_ID:");
		if (vdev_str) {
			entry->vdev_id = strtoul(vdev_str + 8, NULL, 10);
		}
		
		return WMI_SUCCESS;
	}
	
	/* Format 5: "DBGLOG_TIME_STAMP_SYNC_CMDID" */
	if (strstr(log_line, "DBGLOG_TIME_STAMP_SYNC_CMDID")) {
		entry->cmd_id = WMI_DBGLOG_TIME_STAMP_SYNC_CMDID;
		snprintf(entry->command_name, sizeof(entry->command_name), "DBGLOG_TIME_STAMP_SYNC");
		
		/* Parse mode */
		const char *mode_str = strstr(log_line, "mode ");
		if (mode_str) {
			entry->mode = strtoul(mode_str + 5, NULL, 10);
		}
		
		/* Parse time_stamp low */
		const char *low_str = strstr(log_line, "time_stamp low ");
		if (low_str) {
			entry->timestamp_low = strtoul(low_str + 15, NULL, 10);
		}
		
		/* Parse time_stamp high */
		const char *high_str = strstr(log_line, "high ");
		if (high_str) {
			entry->timestamp_high = strtoul(high_str + 5, NULL, 10);
		}
		
		return WMI_SUCCESS;
	}
	
	/* No recognized format - log warning but don't fail completely */
	WMI_LOG_WARNING_FMT(WMI_ERR_UNKNOWN_FORMAT,
	                   "Unknown WMI log format: %.50s...", log_line);
	wmi_error_stats_record(&g_wmi_parse_stats, WMI_ERR_UNKNOWN_FORMAT,
	                      "Unrecognized log format");
	
	return WMI_ERR_UNKNOWN_FORMAT;
}

/* Format WMI entry for display */
int wmi_format_entry(const struct wmi_log_entry *entry, char *buf, size_t len)
{
	if (!entry || !buf || len == 0)
		return -1;
	
	int pos = 0;
	
	/* Add timestamp if available */
	if (entry->has_timestamp) {
		pos += snprintf(buf + pos, len - pos, "[%llu] ",
		                (unsigned long long)entry->timestamp);
	}
	
	/* Add thread name if available */
	if (entry->thread_name[0]) {
		pos += snprintf(buf + pos, len - pos, "%s: ", entry->thread_name);
	}
	
	/* Add command info */
	if (entry->command_name[0]) {
		pos += snprintf(buf + pos, len - pos, "WMI %s (0x%x)",
		                entry->command_name, entry->cmd_id);
	} else {
		pos += snprintf(buf + pos, len - pos, "WMI cmd_id=0x%x", entry->cmd_id);
	}
	
	/* Add vdev_id if present */
	if (entry->vdev_id != 0) {
		pos += snprintf(buf + pos, len - pos, " vdev=%u", entry->vdev_id);
	}
	
	/* Add stats info if present */
	if (entry->has_stats) {
		if (entry->stats_type[0]) {
			pos += snprintf(buf + pos, len - pos, " stats=%s(%u)",
			                entry->stats_type, entry->stats_id);
		} else {
			pos += snprintf(buf + pos, len - pos, " stats_id=%u", entry->stats_id);
		}
	}
	
	/* Add peer MAC if present */
	if (entry->has_peer && entry->peer_mac[0]) {
		pos += snprintf(buf + pos, len - pos, " peer=%s", entry->peer_mac);
	}
	
	/* Add request ID if present */
	if (entry->req_id != 0) {
		pos += snprintf(buf + pos, len - pos, " req_id=%u", entry->req_id);
	}
	
	/* Add pdev_id if present */
	if (entry->pdev_id != 0) {
		pos += snprintf(buf + pos, len - pos, " pdev=%u", entry->pdev_id);
	}
	
	/* Add htc_tag if present */
	if (entry->htc_tag != 0) {
		pos += snprintf(buf + pos, len - pos, " htc_tag=%u", entry->htc_tag);
	}
	
	/* Add timestamp sync specific info */
	if (entry->cmd_id == WMI_DBGLOG_TIME_STAMP_SYNC_CMDID) {
		pos += snprintf(buf + pos, len - pos, " mode=%u low=0x%x high=0x%x",
		                entry->mode, entry->timestamp_low, entry->timestamp_high);
	}
	
	return pos;
}

/**
 * Get WMI parsing error statistics
 */
int wmi_get_parse_stats(struct wmi_error_stats *stats)
{
	if (!stats) {
		return WMI_ERR_NULL_POINTER;
	}
	
	*stats = g_wmi_parse_stats;
	return WMI_SUCCESS;
}

/**
 * Reset WMI parsing error statistics
 */
void wmi_reset_parse_stats(void)
{
	wmi_error_stats_reset(&g_wmi_parse_stats);
}

/**
 * Print WMI parsing error statistics
 */
void wmi_print_parse_stats(FILE *fp)
{
	if (!fp) {
		fp = stderr;
	}
	
	wmi_error_stats_print(&g_wmi_parse_stats, fp);
}
