/* pcap_export.h - Enhanced PCAP export with rotation support
 *
 * Provides PCAP file export with automatic rotation based on size
 * and configurable rotation policies.
 */

#ifndef PCAP_EXPORT_H
#define PCAP_EXPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* PCAP rotation policy */
struct pcap_rotation_policy {
	size_t max_file_size;      /* Maximum file size before rotation (bytes) */
	size_t max_rotations;      /* Maximum number of rotated files to keep */
	bool compress_rotated;     /* Compress rotated files with gzip */
};

/* PCAP exporter handle (opaque) */
struct pcap_exporter;

/**
 * pcap_exporter_create() - Create PCAP exporter
 * @base_filename: Base filename for PCAP files
 * @policy: Rotation policy (NULL for no rotation)
 *
 * Returns: PCAP exporter handle or NULL on error
 */
struct pcap_exporter *pcap_exporter_create(const char *base_filename,
                                           struct pcap_rotation_policy *policy);

/**
 * pcap_exporter_destroy() - Destroy PCAP exporter
 * @exporter: PCAP exporter handle
 */
void pcap_exporter_destroy(struct pcap_exporter *exporter);

/**
 * pcap_exporter_write_packet() - Write packet to PCAP file
 * @exporter: PCAP exporter handle
 * @data: Packet data
 * @len: Packet length
 *
 * Returns: true on success, false on error
 */
bool pcap_exporter_write_packet(struct pcap_exporter *exporter,
                                const unsigned char *data,
                                size_t len);

/**
 * pcap_exporter_flush() - Flush pending writes
 * @exporter: PCAP exporter handle
 *
 * Returns: true on success, false on error
 */
bool pcap_exporter_flush(struct pcap_exporter *exporter);

/**
 * pcap_exporter_get_stats() - Get exporter statistics
 * @exporter: PCAP exporter handle
 * @packets_written: Output for packets written
 * @bytes_written: Output for bytes written
 * @current_file_size: Output for current file size
 * @rotations: Output for number of rotations
 *
 * Returns: true on success, false on error
 */
bool pcap_exporter_get_stats(struct pcap_exporter *exporter,
                             uint64_t *packets_written,
                             uint64_t *bytes_written,
                             uint64_t *current_file_size,
                             uint32_t *rotations);

#endif /* PCAP_EXPORT_H */
