/* pcap_export.c - Enhanced PCAP export with rotation support */

#include "pcap_export.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

/* PCAP file format structures */
struct pcap_file_header {
	uint32_t magic_number;   /* magic number */
	uint16_t version_major;  /* major version number */
	uint16_t version_minor;  /* minor version number */
	int32_t  thiszone;       /* GMT to local correction */
	uint32_t sigfigs;        /* accuracy of timestamps */
	uint32_t snaplen;        /* max length of captured packets, in octets */
	uint32_t network;        /* data link type */
};

struct pcap_packet_header {
	uint32_t ts_sec;         /* timestamp seconds */
	uint32_t ts_usec;        /* timestamp microseconds */
	uint32_t incl_len;       /* number of octets of packet saved in file */
	uint32_t orig_len;       /* actual length of packet */
};

struct pcap_exporter {
	char *base_filename;
	FILE *fp;
	struct pcap_rotation_policy policy;
	bool has_policy;
	
	/* Statistics */
	uint64_t packets_written;
	uint64_t bytes_written;
	uint64_t current_file_size;
	uint32_t rotations;
};

static bool write_pcap_header(FILE *fp)
{
	struct pcap_file_header fh;
	
	fh.magic_number = 0xa1b2c3d4;
	fh.version_major = 2;
	fh.version_minor = 4;
	fh.thiszone = 0;
	fh.sigfigs = 0;
	fh.snaplen = 65535;
	fh.network = 253;  /* DLT_NETLINK for netlink messages */
	
	if (fwrite(&fh, sizeof(fh), 1, fp) != 1)
		return false;
	
	return true;
}

static char *generate_rotated_filename(const char *base, uint32_t rotation)
{
	char *filename;
	size_t len = strlen(base) + 32;
	
	filename = malloc(len);
	if (!filename)
		return NULL;
	
	snprintf(filename, len, "%s.%u", base, rotation);
	return filename;
}

static bool compress_file(const char *filename)
{
	char cmd[1024];
	int ret;
	
	/* Use gzip to compress the file */
	snprintf(cmd, sizeof(cmd), "gzip -f '%s' 2>/dev/null", filename);
	ret = system(cmd);
	
	return ret == 0;
}

static bool rotate_files(struct pcap_exporter *exporter)
{
	char *old_name, *new_name;
	uint32_t i;
	
	/* Close current file */
	if (exporter->fp) {
		fclose(exporter->fp);
		exporter->fp = NULL;
	}
	
	/* Delete oldest rotation if we've hit the limit */
	if (exporter->policy.max_rotations > 0) {
		old_name = generate_rotated_filename(exporter->base_filename,
		                                     exporter->policy.max_rotations);
		if (old_name) {
			unlink(old_name);
			
			/* Also try to delete compressed version */
			char *gz_name = malloc(strlen(old_name) + 4);
			if (gz_name) {
				snprintf(gz_name, strlen(old_name) + 4, "%s.gz", old_name);
				unlink(gz_name);
				free(gz_name);
			}
			free(old_name);
		}
	}
	
	/* Rotate existing files */
	for (i = exporter->policy.max_rotations; i > 0; i--) {
		old_name = generate_rotated_filename(exporter->base_filename, i - 1);
		new_name = generate_rotated_filename(exporter->base_filename, i);
		
		if (old_name && new_name) {
			rename(old_name, new_name);
			
			/* Also try to rename compressed version */
			char *old_gz = malloc(strlen(old_name) + 4);
			char *new_gz = malloc(strlen(new_name) + 4);
			if (old_gz && new_gz) {
				snprintf(old_gz, strlen(old_name) + 4, "%s.gz", old_name);
				snprintf(new_gz, strlen(new_name) + 4, "%s.gz", new_name);
				rename(old_gz, new_gz);
			}
			free(old_gz);
			free(new_gz);
		}
		
		free(old_name);
		free(new_name);
	}
	
	/* Rename current file to .0 */
	old_name = generate_rotated_filename(exporter->base_filename, 0);
	if (old_name) {
		rename(exporter->base_filename, old_name);
		
		/* Compress if requested */
		if (exporter->policy.compress_rotated) {
			compress_file(old_name);
		}
		
		free(old_name);
	}
	
	/* Open new file */
	exporter->fp = fopen(exporter->base_filename, "wb");
	if (!exporter->fp)
		return false;
	
	if (!write_pcap_header(exporter->fp)) {
		fclose(exporter->fp);
		exporter->fp = NULL;
		return false;
	}
	
	exporter->current_file_size = sizeof(struct pcap_file_header);
	exporter->rotations++;
	
	return true;
}

struct pcap_exporter *pcap_exporter_create(const char *base_filename,
                                           struct pcap_rotation_policy *policy)
{
	struct pcap_exporter *exporter;
	
	if (!base_filename)
		return NULL;
	
	exporter = calloc(1, sizeof(*exporter));
	if (!exporter)
		return NULL;
	
	exporter->base_filename = strdup(base_filename);
	if (!exporter->base_filename) {
		free(exporter);
		return NULL;
	}
	
	if (policy) {
		exporter->policy = *policy;
		exporter->has_policy = true;
	}
	
	/* Open initial file */
	exporter->fp = fopen(base_filename, "wb");
	if (!exporter->fp) {
		free(exporter->base_filename);
		free(exporter);
		return NULL;
	}
	
	if (!write_pcap_header(exporter->fp)) {
		fclose(exporter->fp);
		free(exporter->base_filename);
		free(exporter);
		return NULL;
	}
	
	exporter->current_file_size = sizeof(struct pcap_file_header);
	
	return exporter;
}

void pcap_exporter_destroy(struct pcap_exporter *exporter)
{
	if (!exporter)
		return;
	
	if (exporter->fp) {
		fflush(exporter->fp);
		fclose(exporter->fp);
	}
	
	free(exporter->base_filename);
	free(exporter);
}

bool pcap_exporter_write_packet(struct pcap_exporter *exporter,
                                const unsigned char *data,
                                size_t len)
{
	struct pcap_packet_header ph;
	struct timeval tv;
	size_t packet_size;
	
	if (!exporter || !exporter->fp || !data)
		return false;
	
	gettimeofday(&tv, NULL);
	
	ph.ts_sec = tv.tv_sec;
	ph.ts_usec = tv.tv_usec;
	ph.incl_len = len;
	ph.orig_len = len;
	
	packet_size = sizeof(ph) + len;
	
	/* Check if rotation is needed */
	if (exporter->has_policy && exporter->policy.max_file_size > 0) {
		if (exporter->current_file_size + packet_size > exporter->policy.max_file_size) {
			if (!rotate_files(exporter))
				return false;
		}
	}
	
	/* Write packet header */
	if (fwrite(&ph, sizeof(ph), 1, exporter->fp) != 1)
		return false;
	
	/* Write packet data */
	if (fwrite(data, 1, len, exporter->fp) != len)
		return false;
	
	exporter->packets_written++;
	exporter->bytes_written += len;
	exporter->current_file_size += packet_size;
	
	return true;
}

bool pcap_exporter_flush(struct pcap_exporter *exporter)
{
	if (!exporter || !exporter->fp)
		return false;
	
	return fflush(exporter->fp) == 0;
}

bool pcap_exporter_get_stats(struct pcap_exporter *exporter,
                             uint64_t *packets_written,
                             uint64_t *bytes_written,
                             uint64_t *current_file_size,
                             uint32_t *rotations)
{
	if (!exporter)
		return false;
	
	if (packets_written)
		*packets_written = exporter->packets_written;
	if (bytes_written)
		*bytes_written = exporter->bytes_written;
	if (current_file_size)
		*current_file_size = exporter->current_file_size;
	if (rotations)
		*rotations = exporter->rotations;
	
	return true;
}
