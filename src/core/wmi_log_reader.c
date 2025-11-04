/**
 * @file wmi_log_reader.c
 * @brief WMI log reader implementation
 */

#include "wmi_log_reader.h"
#include "wmi_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>

#define DEFAULT_BUFFER_SIZE 4096
#define MAX_LINE_LENGTH 8192
#define FOLLOW_POLL_INTERVAL_MS 100

/**
 * Internal reader state
 */
struct wmi_reader_state {
    struct wmi_log_config config;
    FILE *fp;
    int fd;
    int is_stdin;
    int is_running;
    char *line_buffer;
    size_t buffer_capacity;
    size_t buffer_used;
    struct wmi_log_stats stats;
    struct wmi_error_stats error_stats;  /* Error statistics */
    ino_t inode;                 /* For file rotation detection */
    off_t last_pos;              /* Last read position */
};

static struct wmi_reader_state *g_reader = NULL;

/**
 * Check if file has been rotated
 */
static int check_file_rotation(struct wmi_reader_state *reader)
{
    struct stat st;
    
    if (reader->is_stdin || !reader->config.follow_mode) {
        return 0;
    }
    
    if (stat(reader->config.log_source, &st) < 0) {
        return 0; /* File might be temporarily unavailable */
    }
    
    /* Check if inode changed (file was rotated) */
    if (st.st_ino != reader->inode) {
        return 1;
    }
    
    /* Check if file was truncated */
    if (st.st_size < reader->last_pos) {
        return 1;
    }
    
    return 0;
}

/**
 * Reopen file after rotation
 */
static int reopen_file(struct wmi_reader_state *reader)
{
    struct stat st;
    
    if (reader->fp) {
        fclose(reader->fp);
        reader->fp = NULL;
    }
    
    reader->fp = fopen(reader->config.log_source, "r");
    if (!reader->fp) {
        int err_code = (errno == ENOENT) ? WMI_ERR_FILE_NOT_FOUND :
                       (errno == EACCES) ? WMI_ERR_PERMISSION_DENIED :
                       WMI_ERR_IO_ERROR;
        WMI_LOG_ERROR_FMT(err_code, "Failed to reopen file: %s", 
                         reader->config.log_source);
        wmi_error_stats_record(&reader->error_stats, err_code, 
                              reader->config.log_source);
        return err_code;
    }
    
    reader->fd = fileno(reader->fp);
    
    if (fstat(reader->fd, &st) == 0) {
        reader->inode = st.st_ino;
    }
    
    reader->last_pos = 0;
    
    WMI_LOG_INFO(WMI_SUCCESS, "File reopened after rotation");
    
    return 0;
}

/**
 * Process a complete line through callback
 */
static int process_line(struct wmi_reader_state *reader, const char *line)
{
    int ret;
    
    if (!reader->config.callback) {
        return 0;
    }
    
    ret = reader->config.callback(line, reader->config.user_data);
    if (ret < 0) {
        reader->stats.parse_errors++;
        return ret;
    }
    
    reader->stats.lines_read++;
    return 0;
}

/**
 * Read and process lines from file
 */
static int read_file_lines(struct wmi_reader_state *reader)
{
    char line[MAX_LINE_LENGTH];
    int ret = 0;
    
    while (reader->is_running) {
        /* Check for file rotation in follow mode */
        if (reader->config.follow_mode && check_file_rotation(reader)) {
            int reopen_ret = reopen_file(reader);
            if (reopen_ret < 0) {
                /* Wait and retry */
                usleep(FOLLOW_POLL_INTERVAL_MS * 1000);
                continue;
            }
        }
        
        /* Try to read a line */
        if (fgets(line, sizeof(line), reader->fp) == NULL) {
            if (feof(reader->fp)) {
                if (reader->config.follow_mode) {
                    /* In follow mode, wait for more data */
                    clearerr(reader->fp);
                    usleep(FOLLOW_POLL_INTERVAL_MS * 1000);
                    continue;
                } else {
                    /* Normal EOF, we're done */
                    break;
                }
            } else if (ferror(reader->fp)) {
                /* I/O error */
                WMI_LOG_ERROR_FMT(WMI_ERR_IO_ERROR, 
                                 "I/O error reading from %s", 
                                 reader->config.log_source);
                wmi_error_stats_record(&reader->error_stats, WMI_ERR_IO_ERROR,
                                      "File read error");
                ret = WMI_ERR_IO_ERROR;
                break;
            }
        } else {
            /* Successfully read a line */
            size_t len = strlen(line);
            reader->stats.bytes_read += len;
            reader->error_stats.total_operations++;
            reader->last_pos = ftell(reader->fp);
            
            /* Check for truncated line */
            if (len >= MAX_LINE_LENGTH - 1 && line[len - 1] != '\n') {
                WMI_LOG_WARNING_FMT(WMI_ERR_TRUNCATED_LINE,
                                   "Line truncated at %zu bytes", len);
                wmi_error_stats_record(&reader->error_stats, 
                                      WMI_ERR_TRUNCATED_LINE,
                                      "Line exceeds maximum length");
            }
            
            /* Remove trailing newline */
            if (len > 0 && line[len - 1] == '\n') {
                line[len - 1] = '\0';
                len--;
            }
            if (len > 0 && line[len - 1] == '\r') {
                line[len - 1] = '\0';
                len--;
            }
            
            /* Process the line */
            if (len > 0) {
                ret = process_line(reader, line);
                if (ret < 0) {
                    /* Callback error, but continue processing */
                    WMI_LOG_WARNING(WMI_ERR_CALLBACK_FAILED, 
                                   "Callback failed, continuing");
                    wmi_error_stats_record(&reader->error_stats,
                                          WMI_ERR_CALLBACK_FAILED,
                                          "Line processing callback failed");
                    ret = 0;
                }
            }
        }
    }
    
    return ret;
}

/**
 * Read and process lines from stdin with non-blocking I/O
 */
static int read_stdin_lines(struct wmi_reader_state *reader)
{
    fd_set readfds;
    struct timeval tv;
    int ret = 0;
    
    /* Set stdin to non-blocking mode */
    int flags = fcntl(reader->fd, F_GETFL, 0);
    fcntl(reader->fd, F_SETFL, flags | O_NONBLOCK);
    
    while (reader->is_running) {
        FD_ZERO(&readfds);
        FD_SET(reader->fd, &readfds);
        
        /* Timeout for select */
        tv.tv_sec = 0;
        tv.tv_usec = FOLLOW_POLL_INTERVAL_MS * 1000;
        
        ret = select(reader->fd + 1, &readfds, NULL, NULL, &tv);
        
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            WMI_LOG_ERROR(WMI_ERR_IO_ERROR, "select() failed on stdin");
            wmi_error_stats_record(&reader->error_stats, WMI_ERR_IO_ERROR,
                                  "select() failed");
            return WMI_ERR_IO_ERROR;
        }
        
        if (ret == 0) {
            /* Timeout, continue */
            continue;
        }
        
        if (FD_ISSET(reader->fd, &readfds)) {
            char chunk[1024];
            ssize_t n = read(reader->fd, chunk, sizeof(chunk) - 1);
            
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                WMI_LOG_ERROR(WMI_ERR_IO_ERROR, "read() failed on stdin");
                wmi_error_stats_record(&reader->error_stats, WMI_ERR_IO_ERROR,
                                      "read() failed");
                return WMI_ERR_IO_ERROR;
            }
            
            if (n == 0) {
                /* EOF on stdin */
                break;
            }
            
            chunk[n] = '\0';
            reader->stats.bytes_read += n;
            reader->error_stats.total_operations++;
            
            /* Process chunk character by character, building lines */
            for (ssize_t i = 0; i < n; i++) {
                char c = chunk[i];
                
                if (c == '\n' || c == '\r') {
                    /* Complete line */
                    if (reader->buffer_used > 0) {
                        reader->line_buffer[reader->buffer_used] = '\0';
                        ret = process_line(reader, reader->line_buffer);
                        reader->buffer_used = 0;
                        
                        if (ret < 0) {
                            WMI_LOG_WARNING(WMI_ERR_CALLBACK_FAILED,
                                           "Callback failed, continuing");
                            wmi_error_stats_record(&reader->error_stats,
                                                  WMI_ERR_CALLBACK_FAILED,
                                                  "Line processing callback failed");
                            ret = 0; /* Continue on callback error */
                        }
                    }
                } else {
                    /* Add character to buffer */
                    if (reader->buffer_used < reader->buffer_capacity - 1) {
                        reader->line_buffer[reader->buffer_used++] = c;
                    } else {
                        /* Buffer overflow, drop the line */
                        if (reader->stats.lines_dropped == 0) {
                            WMI_LOG_WARNING(WMI_ERR_BUFFER_FULL,
                                           "Line buffer full, dropping line");
                        }
                        reader->stats.lines_dropped++;
                        wmi_error_stats_record(&reader->error_stats,
                                              WMI_ERR_BUFFER_FULL,
                                              "Line buffer overflow");
                        reader->buffer_used = 0;
                    }
                }
            }
        }
    }
    
    /* Process any remaining buffered data */
    if (reader->buffer_used > 0) {
        reader->line_buffer[reader->buffer_used] = '\0';
        process_line(reader, reader->line_buffer);
        reader->buffer_used = 0;
    }
    
    return ret;
}

int wmi_log_reader_init(struct wmi_log_config *config)
{
    struct stat st;
    
    if (!config) {
        WMI_LOG_ERROR(WMI_ERR_NULL_POINTER, "config is NULL");
        return WMI_ERR_NULL_POINTER;
    }
    
    if (!config->log_source) {
        WMI_LOG_ERROR(WMI_ERR_INVALID_CONFIG, "log_source is NULL");
        return WMI_ERR_INVALID_CONFIG;
    }
    
    if (!config->callback) {
        WMI_LOG_ERROR(WMI_ERR_INVALID_CONFIG, "callback is NULL");
        return WMI_ERR_INVALID_CONFIG;
    }
    
    /* Cleanup any existing reader */
    if (g_reader) {
        wmi_log_reader_cleanup();
    }
    
    /* Allocate reader state */
    g_reader = calloc(1, sizeof(struct wmi_reader_state));
    if (!g_reader) {
        WMI_LOG_ERROR(WMI_ERR_NO_MEMORY, "Failed to allocate reader state");
        return WMI_ERR_NO_MEMORY;
    }
    
    /* Initialize error statistics */
    wmi_error_stats_init(&g_reader->error_stats);
    
    /* Copy configuration */
    g_reader->config = *config;
    g_reader->config.log_source = strdup(config->log_source);
    if (!g_reader->config.log_source) {
        WMI_LOG_ERROR(WMI_ERR_NO_MEMORY, "Failed to duplicate log_source");
        free(g_reader);
        g_reader = NULL;
        return WMI_ERR_NO_MEMORY;
    }
    
    /* Set default buffer size if not specified */
    if (g_reader->config.buffer_size == 0) {
        g_reader->config.buffer_size = DEFAULT_BUFFER_SIZE;
    }
    
    /* Allocate line buffer */
    g_reader->buffer_capacity = g_reader->config.buffer_size;
    g_reader->line_buffer = malloc(g_reader->buffer_capacity);
    if (!g_reader->line_buffer) {
        WMI_LOG_ERROR(WMI_ERR_NO_MEMORY, "Failed to allocate line buffer");
        free((void *)g_reader->config.log_source);
        free(g_reader);
        g_reader = NULL;
        return WMI_ERR_NO_MEMORY;
    }
    
    /* Open log source */
    if (strcmp(g_reader->config.log_source, "-") == 0) {
        /* stdin */
        g_reader->fp = stdin;
        g_reader->fd = STDIN_FILENO;
        g_reader->is_stdin = 1;
        WMI_LOG_INFO(WMI_SUCCESS, "Initialized WMI log reader for stdin");
    } else {
        /* File */
        g_reader->fp = fopen(g_reader->config.log_source, "r");
        if (!g_reader->fp) {
            int err_code = (errno == ENOENT) ? WMI_ERR_FILE_NOT_FOUND :
                           (errno == EACCES) ? WMI_ERR_PERMISSION_DENIED :
                           WMI_ERR_IO_ERROR;
            WMI_LOG_ERROR_FMT(err_code, "Failed to open file: %s",
                             g_reader->config.log_source);
            wmi_error_stats_record(&g_reader->error_stats, err_code,
                                  g_reader->config.log_source);
            free(g_reader->line_buffer);
            free((void *)g_reader->config.log_source);
            free(g_reader);
            g_reader = NULL;
            return err_code;
        }
        
        g_reader->fd = fileno(g_reader->fp);
        g_reader->is_stdin = 0;
        
        /* Get inode for rotation detection */
        if (fstat(g_reader->fd, &st) == 0) {
            g_reader->inode = st.st_ino;
        }
        
        WMI_LOG_INFO(WMI_SUCCESS, "Initialized WMI log reader for file");
    }
    
    g_reader->is_running = 0;
    g_reader->buffer_used = 0;
    memset(&g_reader->stats, 0, sizeof(g_reader->stats));
    
    return WMI_SUCCESS;
}

int wmi_log_reader_start(void)
{
    int ret;
    
    if (!g_reader) {
        WMI_LOG_ERROR(WMI_ERR_NOT_INITIALIZED, "Reader not initialized");
        return WMI_ERR_NOT_INITIALIZED;
    }
    
    if (g_reader->is_running) {
        WMI_LOG_ERROR(WMI_ERR_ALREADY_RUNNING, "Reader already running");
        return WMI_ERR_ALREADY_RUNNING;
    }
    
    g_reader->is_running = 1;
    
    if (g_reader->is_stdin) {
        ret = read_stdin_lines(g_reader);
    } else {
        ret = read_file_lines(g_reader);
    }
    
    g_reader->is_running = 0;
    
    /* Print error statistics if there were errors */
    if (g_reader->error_stats.total_errors > 0) {
        wmi_error_stats_print(&g_reader->error_stats, stderr);
    }
    
    return ret;
}

void wmi_log_reader_stop(void)
{
    if (g_reader) {
        g_reader->is_running = 0;
    }
}

int wmi_log_reader_get_stats(struct wmi_log_stats *stats)
{
    if (!g_reader || !stats) {
        return -1;
    }
    
    *stats = g_reader->stats;
    return 0;
}

/**
 * Get current error statistics
 *
 * @param stats Pointer to error statistics structure to fill
 * @return 0 on success, -1 if reader not initialized
 */
int wmi_log_reader_get_error_stats(struct wmi_error_stats *stats)
{
    if (!g_reader || !stats) {
        return -1;
    }
    
    *stats = g_reader->error_stats;
    return 0;
}

void wmi_log_reader_cleanup(void)
{
    if (!g_reader) {
        return;
    }
    
    g_reader->is_running = 0;
    
    if (g_reader->fp && !g_reader->is_stdin) {
        fclose(g_reader->fp);
    }
    
    if (g_reader->line_buffer) {
        free(g_reader->line_buffer);
    }
    
    if (g_reader->config.log_source) {
        free((void *)g_reader->config.log_source);
    }
    
    free(g_reader);
    g_reader = NULL;
}
