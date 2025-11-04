/* prometheus_exporter.c - Prometheus metrics exporter */

#include "prometheus_exporter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#define MAX_METRICS 1024
#define MAX_METRIC_NAME 128
#define MAX_METRIC_LABELS 256
#define RESPONSE_BUFFER_SIZE 65536

struct metric {
	char name[MAX_METRIC_NAME];
	char labels[MAX_METRIC_LABELS];
	enum metric_type type;
	union {
		uint64_t counter;
		double gauge;
		struct {
			double sum;
			uint64_t count;
			uint64_t buckets[10];  /* Predefined buckets */
		} histogram;
	} value;
	bool in_use;
};

struct prometheus_exporter {
	int listen_sock;
	uint16_t port;
	char *path;
	pthread_t thread;
	bool running;
	
	/* Metrics storage */
	struct metric metrics[MAX_METRICS];
	pthread_mutex_t metrics_lock;
	
	/* Statistics */
	uint64_t requests_served;
	uint64_t last_scrape_time;
	
	/* System metrics */
	double cpu_usage;
	uint64_t memory_rss;
	uint64_t memory_vms;
};

static const double histogram_buckets[] = {
	0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0, 10.0, +INFINITY
};

static struct metric *find_or_create_metric(struct prometheus_exporter *exporter,
                                           const char *name,
                                           const char *labels,
                                           enum metric_type type)
{
	struct metric *metric = NULL;
	int i;
	
	/* Find existing metric */
	for (i = 0; i < MAX_METRICS; i++) {
		if (exporter->metrics[i].in_use &&
		    strcmp(exporter->metrics[i].name, name) == 0 &&
		    strcmp(exporter->metrics[i].labels, labels ? labels : "") == 0) {
			return &exporter->metrics[i];
		}
	}
	
	/* Create new metric */
	for (i = 0; i < MAX_METRICS; i++) {
		if (!exporter->metrics[i].in_use) {
			metric = &exporter->metrics[i];
			strncpy(metric->name, name, MAX_METRIC_NAME - 1);
			metric->name[MAX_METRIC_NAME - 1] = '\0';
			strncpy(metric->labels, labels ? labels : "", MAX_METRIC_LABELS - 1);
			metric->labels[MAX_METRIC_LABELS - 1] = '\0';
			metric->type = type;
			memset(&metric->value, 0, sizeof(metric->value));
			metric->in_use = true;
			return metric;
		}
	}
	
	return NULL;
}

static void generate_metrics_response(struct prometheus_exporter *exporter,
                                     char *buffer, size_t buffer_size)
{
	size_t offset = 0;
	int i, j;
	
	pthread_mutex_lock(&exporter->metrics_lock);
	
	/* Generate metrics in Prometheus text format */
	for (i = 0; i < MAX_METRICS && offset < buffer_size - 1024; i++) {
		struct metric *m = &exporter->metrics[i];
		
		if (!m->in_use)
			continue;
		
		switch (m->type) {
		case METRIC_TYPE_COUNTER:
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# HELP %s Counter metric\n", m->name);
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# TYPE %s counter\n", m->name);
			if (m->labels[0]) {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s{%s} %lu\n", m->name, m->labels,
				                  m->value.counter);
			} else {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s %lu\n", m->name, m->value.counter);
			}
			break;
			
		case METRIC_TYPE_GAUGE:
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# HELP %s Gauge metric\n", m->name);
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# TYPE %s gauge\n", m->name);
			if (m->labels[0]) {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s{%s} %.6f\n", m->name, m->labels,
				                  m->value.gauge);
			} else {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s %.6f\n", m->name, m->value.gauge);
			}
			break;
			
		case METRIC_TYPE_HISTOGRAM:
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# HELP %s Histogram metric\n", m->name);
			offset += snprintf(buffer + offset, buffer_size - offset,
			                  "# TYPE %s histogram\n", m->name);
			
			/* Buckets */
			for (j = 0; j < 10; j++) {
				char le_str[32];
				if (j == 9) {
					snprintf(le_str, sizeof(le_str), "+Inf");
				} else {
					snprintf(le_str, sizeof(le_str), "%.3f", histogram_buckets[j]);
				}
				
				if (m->labels[0]) {
					offset += snprintf(buffer + offset, buffer_size - offset,
					                  "%s_bucket{%s,le=\"%s\"} %lu\n",
					                  m->name, m->labels, le_str,
					                  m->value.histogram.buckets[j]);
				} else {
					offset += snprintf(buffer + offset, buffer_size - offset,
					                  "%s_bucket{le=\"%s\"} %lu\n",
					                  m->name, le_str,
					                  m->value.histogram.buckets[j]);
				}
			}
			
			/* Sum and count */
			if (m->labels[0]) {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s_sum{%s} %.6f\n", m->name, m->labels,
				                  m->value.histogram.sum);
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s_count{%s} %lu\n", m->name, m->labels,
				                  m->value.histogram.count);
			} else {
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s_sum %.6f\n", m->name,
				                  m->value.histogram.sum);
				offset += snprintf(buffer + offset, buffer_size - offset,
				                  "%s_count %lu\n", m->name,
				                  m->value.histogram.count);
			}
			break;
		}
	}
	
	pthread_mutex_unlock(&exporter->metrics_lock);
}

static void handle_http_request(struct prometheus_exporter *exporter, int client_sock)
{
	char request[4096];
	char response[RESPONSE_BUFFER_SIZE];
	char *metrics_buffer;
	ssize_t n;
	
	/* Read request */
	n = recv(client_sock, request, sizeof(request) - 1, 0);
	if (n <= 0) {
		close(client_sock);
		return;
	}
	request[n] = '\0';
	
	/* Check if it's a GET request for our metrics path */
	if (strncmp(request, "GET ", 4) == 0) {
		char *path_start = request + 4;
		char *path_end = strchr(path_start, ' ');
		
		if (path_end) {
			size_t path_len = path_end - path_start;
			if (path_len == strlen(exporter->path) &&
			    strncmp(path_start, exporter->path, path_len) == 0) {
				/* Generate metrics */
				metrics_buffer = malloc(RESPONSE_BUFFER_SIZE);
				if (metrics_buffer) {
					generate_metrics_response(exporter, metrics_buffer,
					                         RESPONSE_BUFFER_SIZE);
					
					/* Send HTTP response */
					snprintf(response, sizeof(response),
					        "HTTP/1.1 200 OK\r\n"
					        "Content-Type: text/plain; version=0.0.4\r\n"
					        "Content-Length: %zu\r\n"
					        "Connection: close\r\n"
					        "\r\n%s",
					        strlen(metrics_buffer), metrics_buffer);
					
					send(client_sock, response, strlen(response), 0);
					free(metrics_buffer);
					
					exporter->requests_served++;
					exporter->last_scrape_time = time(NULL);
				}
			} else {
				/* 404 Not Found */
				snprintf(response, sizeof(response),
				        "HTTP/1.1 404 Not Found\r\n"
				        "Content-Length: 0\r\n"
				        "Connection: close\r\n\r\n");
				send(client_sock, response, strlen(response), 0);
			}
		}
	}
	
	close(client_sock);
}

static void *http_server_thread(void *arg)
{
	struct prometheus_exporter *exporter = arg;
	struct sockaddr_in client_addr;
	socklen_t client_len;
	int client_sock;
	
	while (exporter->running) {
		client_len = sizeof(client_addr);
		client_sock = accept(exporter->listen_sock,
		                    (struct sockaddr *)&client_addr,
		                    &client_len);
		
		if (client_sock < 0) {
			if (errno == EINTR || !exporter->running)
				break;
			continue;
		}
		
		handle_http_request(exporter, client_sock);
	}
	
	return NULL;
}

struct prometheus_exporter *prometheus_exporter_create(uint16_t port,
                                                       const char *path)
{
	struct prometheus_exporter *exporter;
	struct sockaddr_in addr;
	int opt = 1;
	
	if (!path)
		path = "/metrics";
	
	exporter = calloc(1, sizeof(*exporter));
	if (!exporter)
		return NULL;
	
	exporter->port = port;
	exporter->path = strdup(path);
	if (!exporter->path) {
		free(exporter);
		return NULL;
	}
	
	pthread_mutex_init(&exporter->metrics_lock, NULL);
	
	/* Create listening socket */
	exporter->listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (exporter->listen_sock < 0) {
		free(exporter->path);
		free(exporter);
		return NULL;
	}
	
	setsockopt(exporter->listen_sock, SOL_SOCKET, SO_REUSEADDR,
	          &opt, sizeof(opt));
	
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);
	
	if (bind(exporter->listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(exporter->listen_sock);
		free(exporter->path);
		free(exporter);
		return NULL;
	}
	
	if (listen(exporter->listen_sock, 5) < 0) {
		close(exporter->listen_sock);
		free(exporter->path);
		free(exporter);
		return NULL;
	}
	
	/* Start HTTP server thread */
	exporter->running = true;
	if (pthread_create(&exporter->thread, NULL, http_server_thread, exporter) != 0) {
		close(exporter->listen_sock);
		free(exporter->path);
		free(exporter);
		return NULL;
	}
	
	return exporter;
}

void prometheus_exporter_destroy(struct prometheus_exporter *exporter)
{
	if (!exporter)
		return;
	
	exporter->running = false;
	
	if (exporter->listen_sock >= 0) {
		shutdown(exporter->listen_sock, SHUT_RDWR);
		close(exporter->listen_sock);
	}
	
	pthread_join(exporter->thread, NULL);
	pthread_mutex_destroy(&exporter->metrics_lock);
	
	free(exporter->path);
	free(exporter);
}

void prometheus_exporter_inc_counter(struct prometheus_exporter *exporter,
                                     const char *name,
                                     const char *labels,
                                     uint64_t value)
{
	struct metric *metric;
	
	if (!exporter || !name)
		return;
	
	pthread_mutex_lock(&exporter->metrics_lock);
	metric = find_or_create_metric(exporter, name, labels, METRIC_TYPE_COUNTER);
	if (metric) {
		metric->value.counter += value;
	}
	pthread_mutex_unlock(&exporter->metrics_lock);
}

void prometheus_exporter_set_gauge(struct prometheus_exporter *exporter,
                                   const char *name,
                                   const char *labels,
                                   double value)
{
	struct metric *metric;
	
	if (!exporter || !name)
		return;
	
	pthread_mutex_lock(&exporter->metrics_lock);
	metric = find_or_create_metric(exporter, name, labels, METRIC_TYPE_GAUGE);
	if (metric) {
		metric->value.gauge = value;
	}
	pthread_mutex_unlock(&exporter->metrics_lock);
}

void prometheus_exporter_observe_histogram(struct prometheus_exporter *exporter,
                                          const char *name,
                                          const char *labels,
                                          double value)
{
	struct metric *metric;
	int i;
	
	if (!exporter || !name)
		return;
	
	pthread_mutex_lock(&exporter->metrics_lock);
	metric = find_or_create_metric(exporter, name, labels, METRIC_TYPE_HISTOGRAM);
	if (metric) {
		metric->value.histogram.sum += value;
		metric->value.histogram.count++;
		
		/* Update buckets */
		for (i = 0; i < 10; i++) {
			if (value <= histogram_buckets[i]) {
				metric->value.histogram.buckets[i]++;
			}
		}
	}
	pthread_mutex_unlock(&exporter->metrics_lock);
}

void prometheus_exporter_update_system_metrics(struct prometheus_exporter *exporter)
{
	FILE *fp;
	char line[256];
	unsigned long utime, stime;
	static unsigned long last_utime = 0, last_stime = 0;
	static time_t last_time = 0;
	time_t now;
	
	if (!exporter)
		return;
	
	/* Read CPU usage from /proc/self/stat */
	fp = fopen("/proc/self/stat", "r");
	if (fp) {
		if (fgets(line, sizeof(line), fp)) {
			sscanf(line, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
			       &utime, &stime);
			
			now = time(NULL);
			if (last_time > 0) {
				double cpu_time = (utime + stime) - (last_utime + last_stime);
				double elapsed = now - last_time;
				if (elapsed > 0) {
					exporter->cpu_usage = (cpu_time / sysconf(_SC_CLK_TCK)) / elapsed * 100.0;
					prometheus_exporter_set_gauge(exporter, "nlmon_cpu_usage_percent",
					                             NULL, exporter->cpu_usage);
				}
			}
			
			last_utime = utime;
			last_stime = stime;
			last_time = now;
		}
		fclose(fp);
	}
	
	/* Read memory usage from /proc/self/status */
	fp = fopen("/proc/self/status", "r");
	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "VmRSS:", 6) == 0) {
				sscanf(line + 6, "%lu", &exporter->memory_rss);
				prometheus_exporter_set_gauge(exporter, "nlmon_memory_rss_bytes",
				                             NULL, exporter->memory_rss * 1024);
			} else if (strncmp(line, "VmSize:", 7) == 0) {
				sscanf(line + 7, "%lu", &exporter->memory_vms);
				prometheus_exporter_set_gauge(exporter, "nlmon_memory_vms_bytes",
				                             NULL, exporter->memory_vms * 1024);
			}
		}
		fclose(fp);
	}
}

bool prometheus_exporter_get_stats(struct prometheus_exporter *exporter,
                                   uint64_t *requests_served,
                                   uint64_t *last_scrape_time)
{
	if (!exporter)
		return false;
	
	if (requests_served)
		*requests_served = exporter->requests_served;
	if (last_scrape_time)
		*last_scrape_time = exporter->last_scrape_time;
	
	return true;
}
