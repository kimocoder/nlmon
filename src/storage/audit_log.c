/* audit_log.c - Cryptographic audit log implementation
 *
 * Implements tamper-evident audit logging using SHA-256 hash chaining.
 * Each log entry includes a hash of the previous entry, making tampering
 * detectable.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <pthread.h>
#include "audit_log.h"
#include "event_processor.h"

/* Suppress OpenSSL 3.0 deprecation warnings for SHA256 functions */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

/* Hash size for SHA-256 */
#define HASH_SIZE 32
#define HASH_HEX_SIZE (HASH_SIZE * 2 + 1)

/* Audit log structure */
struct audit_log {
	FILE *log_file;
	FILE *security_file;
	char *log_path;
	char *security_log_path;
	size_t max_file_size;
	size_t max_rotations;
	bool sync_writes;
	
	/* Current hash chain */
	unsigned char prev_hash[HASH_SIZE];
	
	/* Statistics */
	uint64_t total_entries;
	uint64_t security_entries;
	uint64_t file_size;
	uint64_t rotations;
	uint64_t sequence;
	
	pthread_mutex_t lock;
};

/* Convert hash to hex string */
static void hash_to_hex(const unsigned char *hash, char *hex)
{
	for (int i = 0; i < HASH_SIZE; i++) {
		sprintf(hex + (i * 2), "%02x", hash[i]);
	}
	hex[HASH_HEX_SIZE - 1] = '\0';
}

/* Convert hex string to hash */
static bool hex_to_hash(const char *hex, unsigned char *hash)
{
	if (strlen(hex) != HASH_SIZE * 2)
		return false;
	
	for (int i = 0; i < HASH_SIZE; i++) {
		if (sscanf(hex + (i * 2), "%2hhx", &hash[i]) != 1)
			return false;
	}
	
	return true;
}

/* Compute SHA-256 hash of data */
static void compute_hash(const void *data, size_t len, unsigned char *hash)
{
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data, len);
	SHA256_Final(hash, &ctx);
}

#pragma GCC diagnostic pop

/* Get file size */
static size_t get_file_size(FILE *fp)
{
	struct stat st;
	
	if (!fp)
		return 0;
	
	if (fstat(fileno(fp), &st) != 0)
		return 0;
	
	return st.st_size;
}

/* Read last hash from log file */
static bool read_last_hash(FILE *fp, unsigned char *hash)
{
	char line[2048];
	char hash_hex[HASH_HEX_SIZE];
	bool found = false;
	
	if (!fp)
		return false;
	
	/* Seek to end and read backwards to find last hash */
	fseek(fp, 0, SEEK_END);
	long pos = ftell(fp);
	
	/* Read last line */
	if (pos > 0) {
		while (pos > 0) {
			fseek(fp, --pos, SEEK_SET);
			if (fgetc(fp) == '\n') {
				if (fgets(line, sizeof(line), fp)) {
					/* Parse hash from line format: [TIMESTAMP] [SEQ] [HASH] ... */
					if (sscanf(line, "%*s %*s %64s", hash_hex) == 1) {
						if (hex_to_hash(hash_hex, hash)) {
							found = true;
							break;
						}
					}
				}
				break;
			}
		}
	} else {
		/* File is empty or very small, try reading from start */
		rewind(fp);
		if (fgets(line, sizeof(line), fp)) {
			if (sscanf(line, "%*s %*s %64s", hash_hex) == 1) {
				hex_to_hash(hash_hex, hash);
			}
		}
	}
	
	/* If no hash found, use zero hash */
	if (!found)
		memset(hash, 0, HASH_SIZE);
	
	return true;
}

/* Rotate log file */
static bool rotate_log_file(struct audit_log *log, const char *path)
{
	char old_path[512];
	char new_path[512];
	
	/* Close current file */
	FILE *fp = NULL;
	if (strcmp(path, log->log_path) == 0) {
		fp = log->log_file;
		log->log_file = NULL;
	} else if (log->security_log_path && strcmp(path, log->security_log_path) == 0) {
		fp = log->security_file;
		log->security_file = NULL;
	}
	
	if (fp)
		fclose(fp);
	
	/* Rotate existing files */
	for (int i = log->max_rotations - 1; i > 0; i--) {
		snprintf(old_path, sizeof(old_path), "%s.%d", path, i - 1);
		snprintf(new_path, sizeof(new_path), "%s.%d", path, i);
		rename(old_path, new_path);
	}
	
	/* Rotate current file to .0 */
	snprintf(new_path, sizeof(new_path), "%s.0", path);
	rename(path, new_path);
	
	/* Open new file */
	fp = fopen(path, "a");
	if (!fp)
		return false;
	
	if (strcmp(path, log->log_path) == 0) {
		log->log_file = fp;
	} else if (log->security_log_path && strcmp(path, log->security_log_path) == 0) {
		log->security_file = fp;
	}
	
	log->rotations++;
	
	return true;
}

struct audit_log *audit_log_open(struct audit_log_config *config)
{
	struct audit_log *log;
	
	if (!config || !config->log_path)
		return NULL;
	
	log = calloc(1, sizeof(*log));
	if (!log)
		return NULL;
	
	log->log_path = strdup(config->log_path);
	if (!log->log_path) {
		free(log);
		return NULL;
	}
	
	if (config->security_log_path) {
		log->security_log_path = strdup(config->security_log_path);
		if (!log->security_log_path) {
			free(log->log_path);
			free(log);
			return NULL;
		}
	}
	
	log->max_file_size = config->max_file_size > 0 ? config->max_file_size : 100 * 1024 * 1024;
	log->max_rotations = config->max_rotations > 0 ? config->max_rotations : 10;
	log->sync_writes = config->sync_writes;
	
	/* Open log file */
	log->log_file = fopen(log->log_path, "a");
	if (!log->log_file) {
		free(log->security_log_path);
		free(log->log_path);
		free(log);
		return NULL;
	}
	
	/* Open security log if separate */
	if (log->security_log_path) {
		log->security_file = fopen(log->security_log_path, "a");
		if (!log->security_file) {
			fclose(log->log_file);
			free(log->security_log_path);
			free(log->log_path);
			free(log);
			return NULL;
		}
	}
	
	/* Read last hash from log */
	read_last_hash(log->log_file, log->prev_hash);
	
	/* Verify log integrity if requested */
	if (config->verify_on_open) {
		size_t error_line;
		if (!audit_log_verify(log->log_path, &error_line)) {
			fprintf(stderr, "Warning: Audit log integrity check failed at line %zu\n",
			        error_line);
		}
	}
	
	log->file_size = get_file_size(log->log_file);
	
	pthread_mutex_init(&log->lock, NULL);
	
	return log;
}

void audit_log_close(struct audit_log *log)
{
	if (!log)
		return;
	
	pthread_mutex_lock(&log->lock);
	
	if (log->log_file)
		fclose(log->log_file);
	
	if (log->security_file)
		fclose(log->security_file);
	
	pthread_mutex_unlock(&log->lock);
	pthread_mutex_destroy(&log->lock);
	
	free(log->security_log_path);
	free(log->log_path);
	free(log);
}

/* Internal write function */
static bool write_entry(struct audit_log *log, FILE *fp,
                       enum audit_severity severity,
                       const char *entry_text)
{
	char timestamp[32];
	char prev_hash_hex[HASH_HEX_SIZE];
	unsigned char curr_hash[HASH_SIZE];
	char log_line[4096];
	time_t now;
	struct tm *tm_info;
	
	if (!log || !fp || !entry_text)
		return false;
	
	/* Get timestamp */
	time(&now);
	tm_info = gmtime(&now);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
	
	/* Convert previous hash to hex */
	hash_to_hex(log->prev_hash, prev_hash_hex);
	
	/* Build log line */
	snprintf(log_line, sizeof(log_line), "[%s] [%lu] [%s] [%d] %s\n",
	        timestamp, log->sequence, prev_hash_hex, severity, entry_text);
	
	/* Compute hash of this entry */
	compute_hash(log_line, strlen(log_line), curr_hash);
	
	/* Write to file */
	if (fputs(log_line, fp) == EOF)
		return false;
	
	if (log->sync_writes)
		fflush(fp);
	
	/* Update state */
	memcpy(log->prev_hash, curr_hash, HASH_SIZE);
	log->sequence++;
	log->file_size += strlen(log_line);
	
	/* Check if rotation needed */
	if (log->file_size >= log->max_file_size) {
		const char *path = (fp == log->log_file) ? log->log_path : log->security_log_path;
		if (path)
			rotate_log_file(log, path);
		log->file_size = 0;
	}
	
	return true;
}

bool audit_log_write(struct audit_log *log,
                     struct nlmon_event *event,
                     enum audit_severity severity,
                     const char *message)
{
	char entry[2048];
	bool result;
	
	if (!log || !event)
		return false;
	
	/* Format event entry */
	snprintf(entry, sizeof(entry),
	        "EVENT type=%u msg_type=%u interface=%s seq=%lu %s",
	        event->event_type, event->message_type, event->interface,
	        event->sequence, message ? message : "");
	
	pthread_mutex_lock(&log->lock);
	
	/* Write to main log */
	result = write_entry(log, log->log_file, severity, entry);
	
	if (result) {
		log->total_entries++;
		
		/* Write to security log if this is a security event */
		if (severity == AUDIT_SECURITY || severity == AUDIT_CRITICAL) {
			FILE *sec_fp = log->security_file ? log->security_file : log->log_file;
			write_entry(log, sec_fp, severity, entry);
			log->security_entries++;
		}
	}
	
	pthread_mutex_unlock(&log->lock);
	
	return result;
}

bool audit_log_write_message(struct audit_log *log,
                             enum audit_severity severity,
                             const char *format, ...)
{
	char message[2048];
	va_list args;
	bool result;
	
	if (!log || !format)
		return false;
	
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	
	pthread_mutex_lock(&log->lock);
	
	result = write_entry(log, log->log_file, severity, message);
	
	if (result) {
		log->total_entries++;
		
		if (severity == AUDIT_SECURITY || severity == AUDIT_CRITICAL) {
			FILE *sec_fp = log->security_file ? log->security_file : log->log_file;
			write_entry(log, sec_fp, severity, message);
			log->security_entries++;
		}
	}
	
	pthread_mutex_unlock(&log->lock);
	
	return result;
}

bool audit_log_verify(const char *log_path, size_t *error_line)
{
	FILE *fp;
	char line[4096];
	unsigned char expected_hash[HASH_SIZE];
	char hash_hex[HASH_HEX_SIZE];
	size_t line_num = 0;
	bool valid = true;
	
	if (!log_path)
		return false;
	
	fp = fopen(log_path, "r");
	if (!fp)
		return false;
	
	/* Initialize with zero hash */
	memset(expected_hash, 0, HASH_SIZE);
	
	while (fgets(line, sizeof(line), fp)) {
		line_num++;
		
		/* Parse hash from line */
		if (sscanf(line, "%*s %*s %64s", hash_hex) != 1) {
			valid = false;
			if (error_line)
				*error_line = line_num;
			break;
		}
		
		/* Verify hash matches expected */
		char expected_hex[HASH_HEX_SIZE];
		hash_to_hex(expected_hash, expected_hex);
		
		if (strcmp(hash_hex, expected_hex) != 0) {
			valid = false;
			if (error_line)
				*error_line = line_num;
			break;
		}
		
		/* Compute hash of this line for next iteration */
		compute_hash(line, strlen(line), expected_hash);
	}
	
	fclose(fp);
	
	return valid;
}

bool audit_log_rotate(struct audit_log *log)
{
	bool result;
	
	if (!log)
		return false;
	
	pthread_mutex_lock(&log->lock);
	result = rotate_log_file(log, log->log_path);
	pthread_mutex_unlock(&log->lock);
	
	return result;
}

bool audit_log_get_stats(struct audit_log *log,
                        uint64_t *total_entries,
                        uint64_t *security_entries,
                        uint64_t *file_size,
                        uint64_t *rotations)
{
	if (!log)
		return false;
	
	pthread_mutex_lock(&log->lock);
	
	if (total_entries)
		*total_entries = log->total_entries;
	if (security_entries)
		*security_entries = log->security_entries;
	if (file_size)
		*file_size = log->file_size;
	if (rotations)
		*rotations = log->rotations;
	
	pthread_mutex_unlock(&log->lock);
	
	return true;
}
