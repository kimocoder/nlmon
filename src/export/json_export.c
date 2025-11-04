/* json_export.c - JSON export format with rotation support */

#include "json_export.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

struct json_exporter {
	char *filename;
	FILE *fp;
	enum json_format format;
	bool streaming;
	bool owns_fp;
	struct json_rotation_policy policy;
	bool has_policy;
	bool first_event;
	
	/* Statistics */
	uint64_t events_written;
	uint64_t bytes_written;
	uint64_t current_file_size;
	uint32_t rotations;
};

static void json_escape_string(FILE *fp, const char *str)
{
	if (!str) {
		fprintf(fp, "null");
		return;
	}
	
	fputc('"', fp);
	while (*str) {
		switch (*str) {
		case '"':  fprintf(fp, "\\\""); break;
		case '\\': fprintf(fp, "\\\\"); break;
		case '\b': fprintf(fp, "\\b"); break;
		case '\f': fprintf(fp, "\\f"); break;
		case '\n': fprintf(fp, "\\n"); break;
		case '\r': fprintf(fp, "\\r"); break;
		case '\t': fprintf(fp, "\\t"); break;
		default:
			if ((unsigned char)*str < 32) {
				fprintf(fp, "\\u%04x", (unsigned char)*str);
			} else {
				fputc(*str, fp);
			}
			break;
		}
		str++;
	}
	fputc('"', fp);
}

static bool compress_file(const char *filename)
{
	char cmd[1024];
	int ret;
	
	snprintf(cmd, sizeof(cmd), "gzip -f '%s' 2>/dev/null", filename);
	ret = system(cmd);
	
	return ret == 0;
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

static bool rotate_files(struct json_exporter *exporter)
{
	char *old_name, *new_name;
	uint32_t i;
	
	/* Close array if not streaming */
	if (!exporter->streaming && exporter->fp) {
		fprintf(exporter->fp, "\n]\n");
	}
	
	/* Close current file */
	if (exporter->fp && exporter->owns_fp) {
		fclose(exporter->fp);
		exporter->fp = NULL;
	}
	
	/* Delete oldest rotation if we've hit the limit */
	if (exporter->policy.max_rotations > 0) {
		old_name = generate_rotated_filename(exporter->filename,
		                                     exporter->policy.max_rotations);
		if (old_name) {
			unlink(old_name);
			
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
		old_name = generate_rotated_filename(exporter->filename, i - 1);
		new_name = generate_rotated_filename(exporter->filename, i);
		
		if (old_name && new_name) {
			rename(old_name, new_name);
			
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
	old_name = generate_rotated_filename(exporter->filename, 0);
	if (old_name) {
		rename(exporter->filename, old_name);
		
		if (exporter->policy.compress_rotated) {
			compress_file(old_name);
		}
		
		free(old_name);
	}
	
	/* Open new file */
	exporter->fp = fopen(exporter->filename, "w");
	if (!exporter->fp)
		return false;
	
	exporter->owns_fp = true;
	exporter->first_event = true;
	exporter->current_file_size = 0;
	exporter->rotations++;
	
	/* Start array if not streaming */
	if (!exporter->streaming) {
		fprintf(exporter->fp, "[\n");
		exporter->current_file_size += 2;
	}
	
	return true;
}

struct json_exporter *json_exporter_create(const char *filename,
                                           enum json_format format,
                                           bool streaming,
                                           struct json_rotation_policy *policy)
{
	struct json_exporter *exporter;
	
	exporter = calloc(1, sizeof(*exporter));
	if (!exporter)
		return NULL;
	
	exporter->format = format;
	exporter->streaming = streaming;
	exporter->first_event = true;
	
	if (filename) {
		exporter->filename = strdup(filename);
		if (!exporter->filename) {
			free(exporter);
			return NULL;
		}
		
		exporter->fp = fopen(filename, "w");
		if (!exporter->fp) {
			free(exporter->filename);
			free(exporter);
			return NULL;
		}
		exporter->owns_fp = true;
	} else {
		exporter->fp = stdout;
		exporter->owns_fp = false;
	}
	
	if (policy && filename) {
		exporter->policy = *policy;
		exporter->has_policy = true;
	}
	
	/* Start array if not streaming */
	if (!streaming) {
		fprintf(exporter->fp, "[\n");
		exporter->current_file_size += 2;
	}
	
	return exporter;
}

void json_exporter_destroy(struct json_exporter *exporter)
{
	if (!exporter)
		return;
	
	/* Close array if not streaming */
	if (!exporter->streaming && exporter->fp) {
		fprintf(exporter->fp, "\n]\n");
	}
	
	if (exporter->fp && exporter->owns_fp) {
		fflush(exporter->fp);
		fclose(exporter->fp);
	}
	
	free(exporter->filename);
	free(exporter);
}

bool json_exporter_write_event(struct json_exporter *exporter,
                               struct json_event *event)
{
	size_t start_pos, end_pos;
	
	if (!exporter || !exporter->fp || !event)
		return false;
	
	start_pos = ftell(exporter->fp);
	
	/* Add comma separator if not first event and not streaming */
	if (!exporter->streaming && !exporter->first_event) {
		fprintf(exporter->fp, ",\n");
	}
	
	/* Check if rotation is needed */
	if (exporter->has_policy && exporter->policy.max_file_size > 0 &&
	    exporter->current_file_size > exporter->policy.max_file_size) {
		if (!rotate_files(exporter))
			return false;
		start_pos = ftell(exporter->fp);
	}
	
	/* Write event */
	if (exporter->format == JSON_FORMAT_PRETTY) {
		fprintf(exporter->fp, "  {\n");
		fprintf(exporter->fp, "    \"timestamp\": \"%lu.%06lu\",\n",
		        event->timestamp_sec, event->timestamp_usec);
		fprintf(exporter->fp, "    \"sequence\": %lu,\n", event->sequence);
		fprintf(exporter->fp, "    \"event_type\": ");
		json_escape_string(exporter->fp, event->event_type);
		fprintf(exporter->fp, ",\n");
		fprintf(exporter->fp, "    \"message_type\": %u,\n", event->message_type);
		fprintf(exporter->fp, "    \"message_type_str\": ");
		json_escape_string(exporter->fp, event->message_type_str);
		fprintf(exporter->fp, ",\n");
		
		if (event->interface) {
			fprintf(exporter->fp, "    \"interface\": ");
			json_escape_string(exporter->fp, event->interface);
			fprintf(exporter->fp, ",\n");
		}
		
		if (event->namespace) {
			fprintf(exporter->fp, "    \"namespace\": ");
			json_escape_string(exporter->fp, event->namespace);
			fprintf(exporter->fp, ",\n");
		}
		
		if (event->correlation_id) {
			fprintf(exporter->fp, "    \"correlation_id\": ");
			json_escape_string(exporter->fp, event->correlation_id);
			fprintf(exporter->fp, ",\n");
		}
		
		if (event->details) {
			fprintf(exporter->fp, "    \"details\": %s\n", event->details);
		} else {
			fprintf(exporter->fp, "    \"details\": null\n");
		}
		
		fprintf(exporter->fp, "  }");
	} else {
		/* Compact format */
		fprintf(exporter->fp, "{\"timestamp\":\"%lu.%06lu\",\"sequence\":%lu,\"event_type\":",
		        event->timestamp_sec, event->timestamp_usec, event->sequence);
		json_escape_string(exporter->fp, event->event_type);
		fprintf(exporter->fp, ",\"message_type\":%u,\"message_type_str\":",
		        event->message_type);
		json_escape_string(exporter->fp, event->message_type_str);
		
		if (event->interface) {
			fprintf(exporter->fp, ",\"interface\":");
			json_escape_string(exporter->fp, event->interface);
		}
		
		if (event->namespace) {
			fprintf(exporter->fp, ",\"namespace\":");
			json_escape_string(exporter->fp, event->namespace);
		}
		
		if (event->correlation_id) {
			fprintf(exporter->fp, ",\"correlation_id\":");
			json_escape_string(exporter->fp, event->correlation_id);
		}
		
		fprintf(exporter->fp, ",\"details\":");
		if (event->details) {
			fprintf(exporter->fp, "%s", event->details);
		} else {
			fprintf(exporter->fp, "null");
		}
		
		fprintf(exporter->fp, "}");
	}
	
	/* Add newline in streaming mode */
	if (exporter->streaming) {
		fprintf(exporter->fp, "\n");
	}
	
	end_pos = ftell(exporter->fp);
	
	exporter->first_event = false;
	exporter->events_written++;
	exporter->bytes_written += (end_pos - start_pos);
	exporter->current_file_size += (end_pos - start_pos);
	
	return true;
}

bool json_exporter_flush(struct json_exporter *exporter)
{
	if (!exporter || !exporter->fp)
		return false;
	
	return fflush(exporter->fp) == 0;
}

bool json_exporter_get_stats(struct json_exporter *exporter,
                             uint64_t *events_written,
                             uint64_t *bytes_written,
                             uint64_t *current_file_size,
                             uint32_t *rotations)
{
	if (!exporter)
		return false;
	
	if (events_written)
		*events_written = exporter->events_written;
	if (bytes_written)
		*bytes_written = exporter->bytes_written;
	if (current_file_size)
		*current_file_size = exporter->current_file_size;
	if (rotations)
		*rotations = exporter->rotations;
	
	return true;
}
