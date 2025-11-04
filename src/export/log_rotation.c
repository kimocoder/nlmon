/* log_rotation.c - Log file rotation and compression */

#include "log_rotation.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

struct log_rotator {
	char *base_filename;
	FILE *fp;
	struct log_rotation_policy policy;
	
	/* State tracking */
	uint64_t bytes_written;
	uint64_t current_file_size;
	uint32_t rotations;
	time_t last_rotation_time;
	time_t next_rotation_time;
};

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

static time_t calculate_next_rotation_time(struct log_rotation_policy *policy,
                                          time_t current_time)
{
	struct tm tm;
	time_t next_time;
	
	if (policy->trigger != ROTATION_TRIGGER_TIME &&
	    policy->trigger != ROTATION_TRIGGER_BOTH) {
		return 0;
	}
	
	localtime_r(&current_time, &tm);
	
	switch (policy->interval) {
	case ROTATION_HOURLY:
		tm.tm_min = policy->rotation_minute;
		tm.tm_sec = 0;
		next_time = mktime(&tm);
		if (next_time <= current_time) {
			next_time += 3600;  /* Add 1 hour */
		}
		break;
		
	case ROTATION_DAILY:
		tm.tm_hour = policy->rotation_hour;
		tm.tm_min = policy->rotation_minute;
		tm.tm_sec = 0;
		next_time = mktime(&tm);
		if (next_time <= current_time) {
			next_time += 86400;  /* Add 1 day */
		}
		break;
		
	case ROTATION_WEEKLY:
		tm.tm_wday = 0;  /* Sunday */
		tm.tm_hour = policy->rotation_hour;
		tm.tm_min = policy->rotation_minute;
		tm.tm_sec = 0;
		next_time = mktime(&tm);
		while (next_time <= current_time) {
			next_time += 604800;  /* Add 1 week */
		}
		break;
		
	case ROTATION_MONTHLY:
		tm.tm_mday = 1;
		tm.tm_hour = policy->rotation_hour;
		tm.tm_min = policy->rotation_minute;
		tm.tm_sec = 0;
		if (tm.tm_mon == 11) {
			tm.tm_mon = 0;
			tm.tm_year++;
		} else {
			tm.tm_mon++;
		}
		next_time = mktime(&tm);
		break;
		
	default:
		next_time = 0;
		break;
	}
	
	return next_time;
}

static bool rotate_files(struct log_rotator *rotator)
{
	char *old_name, *new_name;
	uint32_t i;
	
	/* Close current file */
	if (rotator->fp) {
		fflush(rotator->fp);
		fclose(rotator->fp);
		rotator->fp = NULL;
	}
	
	/* Delete oldest rotation if we've hit the limit */
	if (rotator->policy.max_rotations > 0) {
		old_name = generate_rotated_filename(rotator->base_filename,
		                                     rotator->policy.max_rotations);
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
	for (i = rotator->policy.max_rotations; i > 0; i--) {
		old_name = generate_rotated_filename(rotator->base_filename, i - 1);
		new_name = generate_rotated_filename(rotator->base_filename, i);
		
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
	old_name = generate_rotated_filename(rotator->base_filename, 0);
	if (old_name) {
		rename(rotator->base_filename, old_name);
		
		/* Compress if requested */
		if (rotator->policy.compress_rotated) {
			compress_file(old_name);
		}
		
		free(old_name);
	}
	
	/* Open new file */
	rotator->fp = fopen(rotator->base_filename, "a");
	if (!rotator->fp)
		return false;
	
	/* Set buffering mode */
	if (rotator->policy.sync_writes) {
		setvbuf(rotator->fp, NULL, _IONBF, 0);
	}
	
	rotator->current_file_size = 0;
	rotator->rotations++;
	rotator->last_rotation_time = time(NULL);
	rotator->next_rotation_time = calculate_next_rotation_time(&rotator->policy,
	                                                           rotator->last_rotation_time);
	
	return true;
}

struct log_rotator *log_rotator_create(const char *base_filename,
                                       struct log_rotation_policy *policy)
{
	struct log_rotator *rotator;
	struct stat st;
	
	if (!base_filename || !policy)
		return NULL;
	
	rotator = calloc(1, sizeof(*rotator));
	if (!rotator)
		return NULL;
	
	rotator->base_filename = strdup(base_filename);
	if (!rotator->base_filename) {
		free(rotator);
		return NULL;
	}
	
	rotator->policy = *policy;
	
	/* Open log file */
	rotator->fp = fopen(base_filename, "a");
	if (!rotator->fp) {
		free(rotator->base_filename);
		free(rotator);
		return NULL;
	}
	
	/* Set buffering mode */
	if (policy->sync_writes) {
		setvbuf(rotator->fp, NULL, _IONBF, 0);
	}
	
	/* Get current file size */
	if (stat(base_filename, &st) == 0) {
		rotator->current_file_size = st.st_size;
	}
	
	rotator->last_rotation_time = time(NULL);
	rotator->next_rotation_time = calculate_next_rotation_time(policy,
	                                                           rotator->last_rotation_time);
	
	return rotator;
}

void log_rotator_destroy(struct log_rotator *rotator)
{
	if (!rotator)
		return;
	
	if (rotator->fp) {
		fflush(rotator->fp);
		fclose(rotator->fp);
	}
	
	free(rotator->base_filename);
	free(rotator);
}

ssize_t log_rotator_write(struct log_rotator *rotator,
                          const void *data,
                          size_t len)
{
	size_t written;
	
	if (!rotator || !rotator->fp || !data)
		return -1;
	
	/* Check if rotation is needed */
	log_rotator_check_rotation(rotator);
	
	if (!rotator->fp)
		return -1;
	
	written = fwrite(data, 1, len, rotator->fp);
	if (written > 0) {
		rotator->bytes_written += written;
		rotator->current_file_size += written;
		
		if (rotator->policy.sync_writes) {
			fflush(rotator->fp);
		}
	}
	
	return written;
}

ssize_t log_rotator_printf(struct log_rotator *rotator,
                           const char *format, ...)
{
	va_list args;
	int written;
	
	if (!rotator || !rotator->fp || !format)
		return -1;
	
	/* Check if rotation is needed */
	log_rotator_check_rotation(rotator);
	
	if (!rotator->fp)
		return -1;
	
	va_start(args, format);
	written = vfprintf(rotator->fp, format, args);
	va_end(args);
	
	if (written > 0) {
		rotator->bytes_written += written;
		rotator->current_file_size += written;
		
		if (rotator->policy.sync_writes) {
			fflush(rotator->fp);
		}
	}
	
	return written;
}

bool log_rotator_flush(struct log_rotator *rotator)
{
	if (!rotator || !rotator->fp)
		return false;
	
	return fflush(rotator->fp) == 0;
}

bool log_rotator_check_rotation(struct log_rotator *rotator)
{
	bool should_rotate = false;
	time_t now;
	
	if (!rotator)
		return false;
	
	now = time(NULL);
	
	/* Check size-based rotation */
	if ((rotator->policy.trigger == ROTATION_TRIGGER_SIZE ||
	     rotator->policy.trigger == ROTATION_TRIGGER_BOTH) &&
	    rotator->policy.max_file_size > 0) {
		if (rotator->current_file_size >= rotator->policy.max_file_size) {
			should_rotate = true;
		}
	}
	
	/* Check time-based rotation */
	if ((rotator->policy.trigger == ROTATION_TRIGGER_TIME ||
	     rotator->policy.trigger == ROTATION_TRIGGER_BOTH) &&
	    rotator->next_rotation_time > 0) {
		if (now >= rotator->next_rotation_time) {
			should_rotate = true;
		}
	}
	
	if (should_rotate) {
		return rotate_files(rotator);
	}
	
	return false;
}

bool log_rotator_force_rotation(struct log_rotator *rotator)
{
	if (!rotator)
		return false;
	
	return rotate_files(rotator);
}

bool log_rotator_get_stats(struct log_rotator *rotator,
                           uint64_t *bytes_written,
                           uint64_t *current_file_size,
                           uint32_t *rotations,
                           time_t *last_rotation_time)
{
	if (!rotator)
		return false;
	
	if (bytes_written)
		*bytes_written = rotator->bytes_written;
	if (current_file_size)
		*current_file_size = rotator->current_file_size;
	if (rotations)
		*rotations = rotator->rotations;
	if (last_rotation_time)
		*last_rotation_time = rotator->last_rotation_time;
	
	return true;
}
