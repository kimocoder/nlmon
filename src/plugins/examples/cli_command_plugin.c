/*
 * CLI Command Plugin
 * 
 * Demonstrates how to add custom CLI commands to nlmon.
 * Provides utility commands for system information and diagnostics.
 * 
 * Commands:
 *   sysinfo - Display system information
 *   netstat - Show network statistics
 *   meminfo - Display memory usage
 *   uptime  - Show nlmon uptime
 */

#include "plugin_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>

/* Plugin state */
static struct {
    time_t start_time;
    uint64_t commands_executed;
    nlmon_plugin_context_t *ctx;
} state;

/* Format uptime string */
static void format_uptime(time_t seconds, char *buf, size_t len) {
    int days = seconds / 86400;
    int hours = (seconds % 86400) / 3600;
    int mins = (seconds % 3600) / 60;
    int secs = seconds % 60;
    
    if (days > 0) {
        snprintf(buf, len, "%d days, %02d:%02d:%02d", days, hours, mins, secs);
    } else {
        snprintf(buf, len, "%02d:%02d:%02d", hours, mins, secs);
    }
}

/* System info command */
static int cmd_sysinfo(const char *args, char *response, size_t resp_len) {
    struct utsname uts;
    if (uname(&uts) != 0) {
        snprintf(response, resp_len, "Failed to get system information");
        return -1;
    }
    
    struct sysinfo si;
    if (sysinfo(&si) != 0) {
        snprintf(response, resp_len, "Failed to get system info");
        return -1;
    }
    
    char uptime_str[64];
    format_uptime(si.uptime, uptime_str, sizeof(uptime_str));
    
    snprintf(response, resp_len,
            "System Information:\n"
            "  Hostname: %s\n"
            "  OS: %s %s\n"
            "  Kernel: %s\n"
            "  Architecture: %s\n"
            "  System uptime: %s\n"
            "  Load average: %.2f %.2f %.2f\n"
            "  Total RAM: %lu MB\n"
            "  Free RAM: %lu MB\n",
            uts.nodename,
            uts.sysname,
            uts.release,
            uts.version,
            uts.machine,
            uptime_str,
            si.loads[0] / 65536.0,
            si.loads[1] / 65536.0,
            si.loads[2] / 65536.0,
            si.totalram / (1024 * 1024),
            si.freeram / (1024 * 1024));
    
    state.commands_executed++;
    return 0;
}

/* Network statistics command */
static int cmd_netstat(const char *args, char *response, size_t resp_len) {
    /* Read network statistics from /proc/net/dev */
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        snprintf(response, resp_len, "Failed to read network statistics");
        return -1;
    }
    
    char line[256];
    int count = 0;
    size_t offset = 0;
    
    offset += snprintf(response + offset, resp_len - offset, 
                      "Network Statistics:\n");
    
    /* Skip header lines */
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);
    
    /* Read interface statistics */
    while (fgets(line, sizeof(line), fp) && count < 5) {
        char iface[32];
        unsigned long rx_bytes, tx_bytes;
        
        if (sscanf(line, " %31[^:]: %lu %*u %*u %*u %*u %*u %*u %*u %lu",
                   iface, &rx_bytes, &tx_bytes) == 3) {
            offset += snprintf(response + offset, resp_len - offset,
                             "  %s: RX=%lu KB, TX=%lu KB\n",
                             iface, rx_bytes / 1024, tx_bytes / 1024);
            count++;
        }
    }
    
    fclose(fp);
    state.commands_executed++;
    return 0;
}

/* Memory info command */
static int cmd_meminfo(const char *args, char *response, size_t resp_len) {
    /* Read memory info from /proc/meminfo */
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        snprintf(response, resp_len, "Failed to read memory information");
        return -1;
    }
    
    char line[256];
    unsigned long mem_total = 0, mem_free = 0, mem_available = 0;
    unsigned long buffers = 0, cached = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %lu", &mem_total) == 1) continue;
        if (sscanf(line, "MemFree: %lu", &mem_free) == 1) continue;
        if (sscanf(line, "MemAvailable: %lu", &mem_available) == 1) continue;
        if (sscanf(line, "Buffers: %lu", &buffers) == 1) continue;
        if (sscanf(line, "Cached: %lu", &cached) == 1) continue;
    }
    
    fclose(fp);
    
    unsigned long mem_used = mem_total - mem_free - buffers - cached;
    int mem_percent = mem_total > 0 ? (mem_used * 100) / mem_total : 0;
    
    snprintf(response, resp_len,
            "Memory Information:\n"
            "  Total: %lu MB\n"
            "  Used: %lu MB (%d%%)\n"
            "  Free: %lu MB\n"
            "  Available: %lu MB\n"
            "  Buffers: %lu MB\n"
            "  Cached: %lu MB\n",
            mem_total / 1024,
            mem_used / 1024,
            mem_percent,
            mem_free / 1024,
            mem_available / 1024,
            buffers / 1024,
            cached / 1024);
    
    state.commands_executed++;
    return 0;
}

/* Uptime command */
static int cmd_uptime(const char *args, char *response, size_t resp_len) {
    time_t now = time(NULL);
    time_t uptime = now - state.start_time;
    
    char uptime_str[64];
    format_uptime(uptime, uptime_str, sizeof(uptime_str));
    
    struct tm *start_tm = localtime(&state.start_time);
    char start_str[64];
    strftime(start_str, sizeof(start_str), "%Y-%m-%d %H:%M:%S", start_tm);
    
    snprintf(response, resp_len,
            "nlmon Uptime:\n"
            "  Started: %s\n"
            "  Uptime: %s\n"
            "  Commands executed: %lu\n",
            start_str,
            uptime_str,
            state.commands_executed);
    
    state.commands_executed++;
    return 0;
}

/* Initialize plugin */
static int cli_command_init(nlmon_plugin_context_t *ctx) {
    memset(&state, 0, sizeof(state));
    state.ctx = ctx;
    state.start_time = time(NULL);
    
    /* Register custom commands */
    ctx->register_command("sysinfo", cmd_sysinfo, "Display system information");
    ctx->register_command("netstat", cmd_netstat, "Show network statistics");
    ctx->register_command("meminfo", cmd_meminfo, "Display memory usage");
    ctx->register_command("uptime", cmd_uptime, "Show nlmon uptime");
    
    ctx->log(NLMON_LOG_INFO, "CLI command plugin initialized (4 commands registered)");
    
    return 0;
}

/* Cleanup plugin */
static void cli_command_cleanup(void) {
    if (state.ctx) {
        state.ctx->log(NLMON_LOG_INFO, 
                      "CLI command plugin cleaned up (%lu commands executed)", 
                      state.commands_executed);
    }
}

/* Handle custom command */
static int cli_command_on_command(const char *cmd, const char *args, 
                                  char *response, size_t resp_len) {
    if (!cmd || !response) {
        return -1;
    }
    
    /* Dispatch to appropriate handler */
    if (strcmp(cmd, "sysinfo") == 0) {
        return cmd_sysinfo(args, response, resp_len);
    } else if (strcmp(cmd, "netstat") == 0) {
        return cmd_netstat(args, response, resp_len);
    } else if (strcmp(cmd, "meminfo") == 0) {
        return cmd_meminfo(args, response, resp_len);
    } else if (strcmp(cmd, "uptime") == 0) {
        return cmd_uptime(args, response, resp_len);
    }
    
    snprintf(response, resp_len, "Unknown command: %s", cmd);
    return -1;
}

/* Plugin descriptor */
static nlmon_plugin_t cli_command_plugin = {
    .name = "cli_commands",
    .version = "1.0.0",
    .description = "Adds utility CLI commands (sysinfo, netstat, meminfo, uptime)",
    .api_version = NLMON_PLUGIN_API_VERSION,
    .callbacks = {
        .init = cli_command_init,
        .cleanup = cli_command_cleanup,
        .on_command = cli_command_on_command,
    },
    .event_filter = NULL,
    .flags = NLMON_PLUGIN_FLAG_NONE,
    .dependencies = NULL,
};

/* Plugin registration */
NLMON_PLUGIN_DEFINE(cli_command_plugin);
