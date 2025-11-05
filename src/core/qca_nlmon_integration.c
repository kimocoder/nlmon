/*
 * QCA-nlmon Integration Module
 * 
 * Integrates QCA vendor command control with nlmon event monitoring
 * Allows automatic responses to nl80211 events
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <linux/netlink.h>

#include "qca_control.h"
#include "event_processor.h"
#include "nlmon_nl_genl.h"

/* Integration configuration */
struct qca_nlmon_config {
	int auto_adjust_roaming;     /* Automatically adjust roaming based on RSSI */
	int roam_rssi_weak;          /* RSSI threshold for weak signal (dBm) */
	int roam_rssi_strong;        /* RSSI threshold for strong signal (dBm) */
	int roam_rssi_normal;        /* RSSI threshold for normal signal (dBm) */
	int collect_stats_on_roam;   /* Collect stats when roaming occurs */
	int verbose;                 /* Verbose logging */
	char target_interface[32];   /* Target interface for commands (e.g., "wlan0") */
};

static struct qca_nlmon_config g_qca_config = {
	.auto_adjust_roaming = 0,
	.roam_rssi_weak = 75,
	.roam_rssi_strong = 65,
	.roam_rssi_normal = 70,
	.collect_stats_on_roam = 0,
	.verbose = 0,
	.target_interface = "wlan0"
};

/**
 * Initialize QCA-nlmon integration
 */
int qca_nlmon_init(const char *interface, int verbose)
{
	if (interface) {
		strncpy(g_qca_config.target_interface, interface, 
		        sizeof(g_qca_config.target_interface) - 1);
		g_qca_config.target_interface[sizeof(g_qca_config.target_interface) - 1] = '\0';
	}
	
	g_qca_config.verbose = verbose;
	
	if (verbose) {
		syslog(LOG_INFO, "QCA-nlmon integration initialized for interface %s", 
		       g_qca_config.target_interface);
	}
	
	return 0;
}

/**
 * Enable automatic roaming adjustment
 */
void qca_nlmon_enable_auto_roaming(int enable)
{
	g_qca_config.auto_adjust_roaming = enable;
	
	if (g_qca_config.verbose) {
		syslog(LOG_INFO, "Auto roaming adjustment %s", 
		       enable ? "enabled" : "disabled");
	}
}

/**
 * Set roaming RSSI thresholds
 */
void qca_nlmon_set_roaming_thresholds(int weak, int normal, int strong)
{
	g_qca_config.roam_rssi_weak = weak;
	g_qca_config.roam_rssi_normal = normal;
	g_qca_config.roam_rssi_strong = strong;
	
	if (g_qca_config.verbose) {
		syslog(LOG_INFO, "Roaming thresholds: weak=%d normal=%d strong=%d",
		       weak, normal, strong);
	}
}

/**
 * Enable stats collection on roam events
 */
void qca_nlmon_enable_stats_on_roam(int enable)
{
	g_qca_config.collect_stats_on_roam = enable;
	
	if (g_qca_config.verbose) {
		syslog(LOG_INFO, "Stats collection on roam %s",
		       enable ? "enabled" : "disabled");
	}
}

/**
 * Handle nl80211 roam event
 */
static void handle_roam_event(const char *ifname)
{
	if (g_qca_config.verbose) {
		syslog(LOG_INFO, "Roam event detected on %s", ifname);
	}
	
	/* Collect stats if enabled */
	if (g_qca_config.collect_stats_on_roam) {
		if (g_qca_config.verbose) {
			syslog(LOG_INFO, "Collecting link stats after roam");
		}
		qca_get_link_stats(ifname, NULL, NULL);
	}
	
	/* Adjust roaming parameters if enabled */
	if (g_qca_config.auto_adjust_roaming) {
		/* After roaming, use normal threshold */
		if (g_qca_config.verbose) {
			syslog(LOG_INFO, "Adjusting roaming threshold to normal (%d)",
			       g_qca_config.roam_rssi_normal);
		}
		qca_set_roam_rssi(ifname, g_qca_config.roam_rssi_normal);
	}
}

/**
 * Handle nl80211 disconnect event
 */
static void handle_disconnect_event(const char *ifname)
{
	if (g_qca_config.verbose) {
		syslog(LOG_INFO, "Disconnect event detected on %s", ifname);
	}
	
	/* Enable aggressive roaming after disconnect */
	if (g_qca_config.auto_adjust_roaming) {
		if (g_qca_config.verbose) {
			syslog(LOG_INFO, "Enabling aggressive roaming (%d)",
			       g_qca_config.roam_rssi_weak);
		}
		qca_set_roam_rssi(ifname, g_qca_config.roam_rssi_weak);
	}
}

/**
 * Handle nl80211 connect event
 */
static void handle_connect_event(const char *ifname)
{
	if (g_qca_config.verbose) {
		syslog(LOG_INFO, "Connect event detected on %s", ifname);
	}
	
	/* Use normal roaming threshold after connection */
	if (g_qca_config.auto_adjust_roaming) {
		if (g_qca_config.verbose) {
			syslog(LOG_INFO, "Setting normal roaming threshold (%d)",
			       g_qca_config.roam_rssi_normal);
		}
		qca_set_roam_rssi(ifname, g_qca_config.roam_rssi_normal);
	}
}

/**
 * Handle nl80211 vendor event
 */
static void handle_vendor_event(const char *ifname, uint32_t vendor_id, uint32_t subcmd)
{
	if (g_qca_config.verbose) {
		syslog(LOG_INFO, "Vendor event: if=%s vendor_id=0x%06x subcmd=0x%02x",
		       ifname, vendor_id, subcmd);
	}
	
	/* Handle QCA vendor events */
	if (vendor_id == QCA_NL80211_VENDOR_ID) {
		/* Could add specific handling for QCA vendor events here */
	}
}

/**
 * Process nl80211 event from nlmon
 * 
 * This is called by nlmon when an nl80211 event is received
 */
void qca_nlmon_process_nl80211_event(struct nlmon_event *evt)
{
	if (!evt || evt->netlink.protocol != NETLINK_GENERIC)
		return;
	
	if (strcmp(evt->netlink.genl_family_name, "nl80211") != 0)
		return;
	
	if (!evt->netlink.data.nl80211)
		return;
	
	const char *ifname = evt->netlink.data.nl80211->ifname;
	
	/* Only process events for our target interface */
	if (g_qca_config.target_interface[0] != '\0' &&
	    strcmp(ifname, g_qca_config.target_interface) != 0) {
		return;
	}
	
	/* Handle different nl80211 commands */
	switch (evt->netlink.genl_cmd) {
	case 39:  /* NL80211_CMD_ROAM */
		handle_roam_event(ifname);
		break;
		
	case 40:  /* NL80211_CMD_DISCONNECT */
	case 48:  /* NL80211_CMD_DEAUTHENTICATE */
	case 49:  /* NL80211_CMD_DISASSOCIATE */
		handle_disconnect_event(ifname);
		break;
		
	case 38:  /* NL80211_CMD_CONNECT */
	case 47:  /* NL80211_CMD_ASSOCIATE */
		handle_connect_event(ifname);
		break;
		
	case 103: /* NL80211_CMD_VENDOR */
		/* Vendor events are handled separately via qca_vendor data */
		if (evt->netlink.data.qca_vendor) {
			handle_vendor_event(ifname,
			                   evt->netlink.data.qca_vendor->vendor_id,
			                   evt->netlink.data.qca_vendor->subcmd);
		}
		break;
	}
}

/**
 * Send QCA command from nlmon context
 * 
 * Convenience function to send commands to the configured interface
 */
int qca_nlmon_send_command(uint32_t subcmd, const void *data, size_t data_len)
{
	return qca_send_vendor_command(g_qca_config.target_interface,
	                               subcmd, data, data_len,
	                               NULL, NULL);
}

/**
 * Get link stats for configured interface
 */
int qca_nlmon_get_stats(void)
{
	return qca_get_link_stats(g_qca_config.target_interface, NULL, NULL);
}

/**
 * Clear link stats for configured interface
 */
int qca_nlmon_clear_stats(void)
{
	return qca_clear_link_stats(g_qca_config.target_interface);
}

/**
 * Get WiFi info for configured interface
 */
int qca_nlmon_get_wifi_info(void)
{
	return qca_get_wifi_info(g_qca_config.target_interface, NULL, NULL);
}

/**
 * Cleanup QCA-nlmon integration
 */
void qca_nlmon_cleanup(void)
{
	if (g_qca_config.verbose) {
		syslog(LOG_INFO, "QCA-nlmon integration cleanup");
	}
}
