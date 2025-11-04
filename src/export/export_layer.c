/* export_layer.c - Unified export layer implementation */

#include "export_layer.h"
#include <stdlib.h>
#include <string.h>

struct export_layer {
	struct pcap_exporter *pcap;
	struct json_exporter *json;
	struct prometheus_exporter *prometheus;
	struct syslog_forwarder *syslog;
	struct log_rotator *log_rotator;
};

struct export_layer *export_layer_create(struct export_layer_config *config)
{
	struct export_layer *layer;
	
	if (!config)
		return NULL;
	
	layer = calloc(1, sizeof(*layer));
	if (!layer)
		return NULL;
	
	/* Initialize PCAP exporter */
	if (config->enable_pcap && config->pcap_filename) {
		layer->pcap = pcap_exporter_create(config->pcap_filename,
		                                   &config->pcap_policy);
		if (!layer->pcap) {
			export_layer_destroy(layer);
			return NULL;
		}
	}
	
	/* Initialize JSON exporter */
	if (config->enable_json) {
		layer->json = json_exporter_create(config->json_filename,
		                                   config->json_format,
		                                   config->json_streaming,
		                                   config->json_filename ? &config->json_policy : NULL);
		if (!layer->json) {
			export_layer_destroy(layer);
			return NULL;
		}
	}
	
	/* Initialize Prometheus exporter */
	if (config->enable_prometheus) {
		layer->prometheus = prometheus_exporter_create(config->prometheus_port,
		                                              config->prometheus_path);
		if (!layer->prometheus) {
			export_layer_destroy(layer);
			return NULL;
		}
	}
	
	/* Initialize syslog forwarder */
	if (config->enable_syslog) {
		layer->syslog = syslog_forwarder_create(&config->syslog_config);
		if (!layer->syslog) {
			export_layer_destroy(layer);
			return NULL;
		}
	}
	
	/* Initialize log rotator */
	if (config->enable_log_rotation && config->log_filename) {
		layer->log_rotator = log_rotator_create(config->log_filename,
		                                        &config->log_policy);
		if (!layer->log_rotator) {
			export_layer_destroy(layer);
			return NULL;
		}
	}
	
	return layer;
}

void export_layer_destroy(struct export_layer *layer)
{
	if (!layer)
		return;
	
	if (layer->pcap)
		pcap_exporter_destroy(layer->pcap);
	if (layer->json)
		json_exporter_destroy(layer->json);
	if (layer->prometheus)
		prometheus_exporter_destroy(layer->prometheus);
	if (layer->syslog)
		syslog_forwarder_destroy(layer->syslog);
	if (layer->log_rotator)
		log_rotator_destroy(layer->log_rotator);
	
	free(layer);
}

struct pcap_exporter *export_layer_get_pcap(struct export_layer *layer)
{
	return layer ? layer->pcap : NULL;
}

struct json_exporter *export_layer_get_json(struct export_layer *layer)
{
	return layer ? layer->json : NULL;
}

struct prometheus_exporter *export_layer_get_prometheus(struct export_layer *layer)
{
	return layer ? layer->prometheus : NULL;
}

struct syslog_forwarder *export_layer_get_syslog(struct export_layer *layer)
{
	return layer ? layer->syslog : NULL;
}

struct log_rotator *export_layer_get_log_rotator(struct export_layer *layer)
{
	return layer ? layer->log_rotator : NULL;
}

bool export_layer_flush_all(struct export_layer *layer)
{
	bool success = true;
	
	if (!layer)
		return false;
	
	if (layer->pcap && !pcap_exporter_flush(layer->pcap))
		success = false;
	
	if (layer->json && !json_exporter_flush(layer->json))
		success = false;
	
	if (layer->log_rotator && !log_rotator_flush(layer->log_rotator))
		success = false;
	
	return success;
}
