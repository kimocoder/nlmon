/* export_layer.h - Unified export layer interface
 *
 * Provides a unified interface to all export modules including
 * PCAP, JSON, Prometheus metrics, syslog, and log rotation.
 */

#ifndef EXPORT_LAYER_H
#define EXPORT_LAYER_H

#include "pcap_export.h"
#include "json_export.h"
#include "prometheus_exporter.h"
#include "syslog_forwarder.h"
#include "log_rotation.h"

/* Export layer configuration */
struct export_layer_config {
	/* PCAP export */
	bool enable_pcap;
	const char *pcap_filename;
	struct pcap_rotation_policy pcap_policy;
	
	/* JSON export */
	bool enable_json;
	const char *json_filename;
	enum json_format json_format;
	bool json_streaming;
	struct json_rotation_policy json_policy;
	
	/* Prometheus metrics */
	bool enable_prometheus;
	uint16_t prometheus_port;
	const char *prometheus_path;
	
	/* Syslog forwarding */
	bool enable_syslog;
	struct syslog_config syslog_config;
	
	/* Generic log rotation */
	bool enable_log_rotation;
	const char *log_filename;
	struct log_rotation_policy log_policy;
};

/* Export layer handle (opaque) */
struct export_layer;

/**
 * export_layer_create() - Create export layer
 * @config: Export layer configuration
 *
 * Returns: Export layer handle or NULL on error
 */
struct export_layer *export_layer_create(struct export_layer_config *config);

/**
 * export_layer_destroy() - Destroy export layer
 * @layer: Export layer handle
 */
void export_layer_destroy(struct export_layer *layer);

/**
 * export_layer_get_pcap() - Get PCAP exporter handle
 * @layer: Export layer handle
 *
 * Returns: PCAP exporter handle or NULL if not enabled
 */
struct pcap_exporter *export_layer_get_pcap(struct export_layer *layer);

/**
 * export_layer_get_json() - Get JSON exporter handle
 * @layer: Export layer handle
 *
 * Returns: JSON exporter handle or NULL if not enabled
 */
struct json_exporter *export_layer_get_json(struct export_layer *layer);

/**
 * export_layer_get_prometheus() - Get Prometheus exporter handle
 * @layer: Export layer handle
 *
 * Returns: Prometheus exporter handle or NULL if not enabled
 */
struct prometheus_exporter *export_layer_get_prometheus(struct export_layer *layer);

/**
 * export_layer_get_syslog() - Get syslog forwarder handle
 * @layer: Export layer handle
 *
 * Returns: Syslog forwarder handle or NULL if not enabled
 */
struct syslog_forwarder *export_layer_get_syslog(struct export_layer *layer);

/**
 * export_layer_get_log_rotator() - Get log rotator handle
 * @layer: Export layer handle
 *
 * Returns: Log rotator handle or NULL if not enabled
 */
struct log_rotator *export_layer_get_log_rotator(struct export_layer *layer);

/**
 * export_layer_flush_all() - Flush all exporters
 * @layer: Export layer handle
 *
 * Returns: true on success, false on error
 */
bool export_layer_flush_all(struct export_layer *layer);

#endif /* EXPORT_LAYER_H */
