# WMI Monitoring Guide

## Table of Contents

1. [Introduction](#introduction)
2. [WMI Architecture](#wmi-architecture)
3. [Getting Started](#getting-started)
4. [Supported Log Formats](#supported-log-formats)
5. [Command Reference](#command-reference)
6. [Usage Examples](#usage-examples)
7. [Filtering WMI Events](#filtering-wmi-events)
8. [Integration with nlmon Features](#integration-with-nlmon-features)
9. [Troubleshooting](#troubleshooting)
10. [Performance Considerations](#performance-considerations)
11. [Advanced Topics](#advanced-topics)

## Introduction

### What is WMI?

WMI (Wireless Management Interface) is Qualcomm's proprietary low-level interface for communication between WiFi drivers and firmware. It provides direct access to firmware operations, statistics collection, and debugging capabilities that are not exposed through standard nl80211 interfaces.

### Why Monitor WMI?

Monitoring WMI commands provides several benefits:

- **Deep Visibility**: See firmware-level operations not visible in nl80211
- **Performance Analysis**: Track statistics collection and firmware behavior
- **Debugging**: Diagnose driver-firmware communication issues
- **Correlation**: Connect high-level WiFi operations to low-level firmware commands
- **Development**: Understand firmware behavior during driver development

### WMI vs nl80211 Vendor Commands

| Aspect | nl80211 Vendor Commands | WMI Commands |
|--------|------------------------|--------------|
| Level | Driver-kernel interface | Driver-firmware interface |
| Protocol | Netlink | Proprietary |
| Visibility | Netlink monitoring | Log file parsing |
| Granularity | High-level operations | Low-level firmware commands |
| Use Case | Driver behavior | Firmware behavior |

## WMI Architecture

### System Overview

```
┌─────────────────────────────────────────────────────────┐
│                    User Space                            │
│  ┌──────────────┐         ┌──────────────┐             │
│  │ wpa_supplicant│         │    nlmon     │             │
│  └──────┬───────┘         └──────┬───────┘             │
│         │                         │                      │
└─────────┼─────────────────────────┼──────────────────────┘
          │                         │
          │ nl80211                 │ reads logs
          ▼                         ▼
┌─────────────────────────────────────────────────────────┐
│                    Kernel Space                          │
│  ┌──────────────┐         ┌──────────────┐             │
│  │  cfg80211    │         │  Device Logs │             │
│  └──────┬───────┘         └──────▲───────┘             │
│         │                         │                      │
│         │ nl80211 vendor          │ logs WMI             │
│         ▼                         │                      │
│  ┌──────────────┐                │                      │
│  │ ath10k/ath11k│────────────────┘                      │
│  │    Driver    │                                        │
│  └──────┬───────┘                                        │
│         │                                                 │
│         │ WMI                                            │
│         ▼                                                 │
│  ┌──────────────┐                                        │
│  │   Firmware   │                                        │
│  └──────────────┘                                        │
└─────────────────────────────────────────────────────────┘
```

### nlmon WMI Processing Pipeline

```
Device Log File/Stream
        │
        ▼
┌───────────────────┐
│  WMI Log Reader   │ ← Reads lines, buffers input
│                   │   (wmi_log_reader.c)
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│   Log Parser      │ ← Parses WMI log formats
│                   │   (qca_wmi.c: wmi_parse_log_line)
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  WMI Decoder      │ ← Translates IDs to names
│                   │   (qca_wmi.c: wmi_cmd_to_string)
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│ Event Bridge      │ ← Creates nlmon_event
│                   │   (wmi_event_bridge.c)
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│ Event Processor   │ ← Existing nlmon pipeline
│                   │   (filters, export, display)
└───────────────────┘
```

## Getting Started

### Prerequisites

- nlmon compiled with WMI support (enabled by default)
- Access to device logs containing WMI commands
- Root privileges (for some log file access)

### Quick Start

1. **Find your WMI log source**:
   ```bash
   # Common locations for QCA driver logs
   dmesg | grep -i wmi
   journalctl -k | grep -i wmi
   cat /var/log/kern.log | grep -i wmi
   ```

2. **Start monitoring**:
   ```bash
   sudo ./nlmon --wmi /var/log/kern.log
   ```

3. **Trigger some WiFi activity** (scan, connect, etc.) and observe WMI commands

### Basic Usage

```bash
# Monitor from file
sudo ./nlmon --wmi /var/log/wlan.log

# Monitor from stdin
cat device.log | sudo ./nlmon --wmi -

# Follow log in real-time (like tail -f)
sudo ./nlmon --wmi follow:/var/log/wlan.log

# Combine with netlink monitoring
sudo ./nlmon -g --wmi /var/log/wlan.log
```

## Supported Log Formats

nlmon can parse five different WMI log formats commonly found in QCA driver logs.

### Format 1: Basic WMI Command

**Pattern**: `Send WMI command:WMI_*_CMDID command_id:XXXXX htc_tag:X`

**Example**:
```
Send WMI command:WMI_REQUEST_STATS_CMDID command_id:90113 htc_tag:1
```

**Extracted Fields**:
- Command ID: 90113
- Command Name: REQUEST_STATS
- HTC Tag: 1

**nlmon Output**:
```
[timestamp] WMI: cmd=REQUEST_STATS(90113) htc_tag=1
```

### Format 2: Link Layer Statistics Request

**Pattern**: `LINK_LAYER_STATS - Get Request Params Request ID: X Stats Type: X Vdev ID: X Peer MAC Addr: XX:XX:XX:XX:XX:XX`

**Example**:
```
LINK_LAYER_STATS - Get Request Params Request ID: 1 Stats Type: 7 Vdev ID: 0 Peer MAC Addr: aa:bb:cc:dd:ee:ff
```

**Extracted Fields**:
- Request ID: 1
- Stats Type: 7 (LINK_LAYER_STATS_TYPE)
- Vdev ID: 0
- Peer MAC: aa:bb:cc:dd:ee:ff

**nlmon Output**:
```
[timestamp] WMI: cmd=REQUEST_LINK_STATS(90116) vdev=0 stats=LINK_LAYER_STATS_TYPE(7) peer=aa:bb:cc:dd:ee:ff req_id=1
```

### Format 3: Statistics Request

**Pattern**: `STATS REQ STATS_ID:XXXX VDEV_ID:X PDEV_ID:X-->`

**Example**:
```
STATS REQ STATS_ID:8463 VDEV_ID:0 PDEV_ID:0-->
```

**Extracted Fields**:
- Stats ID: 8463 (CONGESTION)
- Vdev ID: 0
- Pdev ID: 0

**nlmon Output**:
```
[timestamp] WMI: cmd=REQUEST_STATS(90113) vdev=0 pdev=0 stats=CONGESTION(8463)
```

### Format 4: RCPI Request

**Pattern**: `RCPI REQ VDEV_ID:X-->`

**Example**:
```
RCPI REQ VDEV_ID:0-->
```

**Extracted Fields**:
- Vdev ID: 0

**nlmon Output**:
```
[timestamp] WMI: cmd=REQUEST_RCPI(90123) vdev=0
```

### Format 5: Timestamp Synchronization

**Pattern**: `send_time_stamp_sync_cmd_tlv: XXXXX: WMA --> DBGLOG_TIME_STAMP_SYNC_CMDID mode X time_stamp low XXXX high XXXX`

**Example**:
```
send_time_stamp_sync_cmd_tlv: 12345: WMA --> DBGLOG_TIME_STAMP_SYNC_CMDID mode 1 time_stamp low 0x12345678 high 0x0
```

**Extracted Fields**:
- Thread ID: 12345
- Mode: 1
- Timestamp Low: 0x12345678
- Timestamp High: 0x0

**nlmon Output**:
```
[timestamp] WMI: cmd=DBGLOG_TIME_STAMP_SYNC(118804) mode=1 ts_low=0x12345678 ts_high=0x0
```

## Command Reference

### WMI Command IDs

| Command ID | Command Name | Description |
|-----------|--------------|-------------|
| 90113 | REQUEST_STATS | General statistics request |
| 90116 | REQUEST_LINK_STATS | Link layer statistics request |
| 90123 | REQUEST_RCPI | Received Channel Power Indicator request |
| 118804 | DBGLOG_TIME_STAMP_SYNC | Firmware timestamp synchronization |

### Statistics Type IDs

| Stats ID | Stats Type Name | Description |
|---------|-----------------|-------------|
| 4 | BASIC_STATS | Basic performance metrics |
| 7 | LINK_LAYER_STATS_TYPE | Comprehensive link layer statistics |
| 8463 | CONGESTION | Network congestion data |

### Device Identifiers

- **VDEV ID**: Virtual Device ID (0-based index for WiFi interfaces)
  - 0: Usually the primary STA interface
  - 1+: Additional interfaces (AP, P2P, etc.)

- **PDEV ID**: Physical Device ID (radio index)
  - 0: Primary radio
  - 1+: Additional radios (dual-band devices)

## Usage Examples

### Example 1: Basic WMI Monitoring

Monitor all WMI commands from a log file:

```bash
sudo ./nlmon --wmi /var/log/kern.log
```

**Output**:
```
[12:34:56.123456] WMI: cmd=REQUEST_STATS(90113) vdev=0 stats=CONGESTION(8463)
[12:34:56.234567] WMI: cmd=REQUEST_LINK_STATS(90116) vdev=0 stats=LINK_LAYER_STATS_TYPE(7)
[12:34:56.345678] WMI: cmd=REQUEST_RCPI(90123) vdev=0
```

### Example 2: Real-Time Monitoring

Follow WMI commands as they occur:

```bash
sudo ./nlmon --wmi follow:/var/log/kern.log
```

This will continuously monitor the log file and display new WMI commands in real-time.

### Example 3: Combined Netlink and WMI Monitoring

See both nl80211 vendor commands and WMI commands together:

```bash
sudo ./nlmon -g --wmi /var/log/kern.log
```

**Output**:
```
[12:34:56.100000] genetlink: family=nl80211/QCA:GSCAN_START cmd=20 version=1
[12:34:56.123456] WMI: cmd=REQUEST_STATS(90113) vdev=0 stats=CONGESTION(8463)
[12:34:56.200000] genetlink: family=nl80211/QCA:GET_WIFI_INFO cmd=61 version=1
[12:34:56.234567] WMI: cmd=REQUEST_LINK_STATS(90116) vdev=0 stats=LINK_LAYER_STATS_TYPE(7)
```

### Example 4: Monitoring from stdin

Process WMI logs from stdin (useful for piping):

```bash
cat device.log | sudo ./nlmon --wmi -
```

Or with live streaming:

```bash
tail -f /var/log/kern.log | sudo ./nlmon --wmi -
```

### Example 5: Filter Specific Commands

Monitor only link layer statistics requests:

```bash
sudo ./nlmon --wmi /var/log/kern.log --filter "protocol=WMI && wmi.cmd=REQUEST_LINK_STATS"
```

### Example 6: Export to JSON

Capture WMI commands to JSON for analysis:

```bash
sudo ./nlmon --wmi /var/log/kern.log --json wmi_events.json
```

### Example 7: Monitor Specific VDEV

Track WMI commands for a specific virtual device:

```bash
sudo ./nlmon --wmi /var/log/kern.log --filter "wmi.vdev=0"
```

## Filtering WMI Events

### Filter Syntax

WMI events support the following filter keys:

| Filter Key | Description | Example |
|-----------|-------------|---------|
| `protocol` | Protocol name | `protocol=WMI` |
| `wmi.cmd` | Command name | `wmi.cmd=REQUEST_STATS` |
| `wmi.cmd_id` | Command ID | `wmi.cmd_id=90113` |
| `wmi.vdev` | Virtual device ID | `wmi.vdev=0` |
| `wmi.pdev` | Physical device ID | `wmi.pdev=0` |
| `wmi.stats` | Statistics type name | `wmi.stats=LINK_LAYER_STATS_TYPE` |
| `wmi.stats_id` | Statistics type ID | `wmi.stats_id=7` |
| `wmi.peer` | Peer MAC address | `wmi.peer=aa:bb:cc:dd:ee:ff` |

### Filter Examples

**Filter by command**:
```bash
sudo ./nlmon --wmi /var/log/kern.log --filter "wmi.cmd=REQUEST_LINK_STATS"
```

**Filter by statistics type**:
```bash
sudo ./nlmon --wmi /var/log/kern.log --filter "wmi.stats=CONGESTION"
```

**Filter by VDEV**:
```bash
sudo ./nlmon --wmi /var/log/kern.log --filter "wmi.vdev=0"
```

**Complex filter** (statistics requests on vdev 0):
```bash
sudo ./nlmon --wmi /var/log/kern.log --filter "protocol=WMI && wmi.cmd=REQUEST_STATS && wmi.vdev=0"
```

**Exclude specific commands**:
```bash
sudo ./nlmon --wmi /var/log/kern.log --filter "protocol=WMI && wmi.cmd!=DBGLOG_TIME_STAMP_SYNC"
```

## Integration with nlmon Features

### Event Hooks

Trigger actions based on WMI command patterns:

```yaml
event_hooks:
  - name: "wmi_congestion_alert"
    trigger:
      filter: "wmi.stats=CONGESTION"
    action:
      type: "exec"
      command: "/usr/local/bin/alert_congestion.sh"
```

### Correlation Engine

Correlate WMI commands with netlink events:

```yaml
correlation:
  rules:
    - name: "scan_to_stats"
      events:
        - type: "nl80211"
          filter: "cmd=TRIGGER_SCAN"
        - type: "WMI"
          filter: "wmi.cmd=REQUEST_STATS"
      window: 5s
```

### Alert System

Set up alerts for specific WMI patterns:

```yaml
alerts:
  - name: "excessive_stats_requests"
    condition:
      filter: "wmi.cmd=REQUEST_STATS"
      rate: "> 100/s"
    action:
      type: "log"
      severity: "warning"
```

### Export Formats

WMI events can be exported in multiple formats:

**JSON Export**:
```bash
sudo ./nlmon --wmi /var/log/kern.log --json wmi_events.json
```

**PCAP Export** (for Wireshark analysis):
```bash
sudo ./nlmon --wmi /var/log/kern.log --pcap wmi_events.pcap
```

**Syslog Forwarding**:
```bash
sudo ./nlmon --wmi /var/log/kern.log --syslog localhost:514
```

## Troubleshooting

### No WMI Commands Visible

**Problem**: Running nlmon with `--wmi` but no WMI commands appear.

**Solutions**:

1. **Verify log file contains WMI commands**:
   ```bash
   grep -i "wmi\|STATS REQ\|RCPI REQ" /var/log/kern.log
   ```

2. **Check file permissions**:
   ```bash
   ls -l /var/log/kern.log
   sudo ./nlmon --wmi /var/log/kern.log
   ```

3. **Try different log sources**:
   ```bash
   # Try dmesg
   sudo dmesg | sudo ./nlmon --wmi -
   
   # Try journalctl
   sudo journalctl -k -f | sudo ./nlmon --wmi -
   ```

4. **Enable driver debug logging**:
   ```bash
   # For ath10k
   echo 0xffffffff > /sys/module/ath10k_core/parameters/debug_mask
   
   # For ath11k
   echo 0xffffffff > /sys/module/ath11k/parameters/debug_mask
   ```

### Malformed Log Lines

**Problem**: Seeing "Failed to parse WMI log line" errors.

**Solutions**:

1. **Check log format**: Ensure logs match one of the supported formats
2. **Verify log encoding**: Logs should be UTF-8 text
3. **Check for truncation**: Long lines may be truncated by syslog

**Workaround**: nlmon will skip malformed lines and continue processing.

### Performance Issues

**Problem**: nlmon is slow or using too much CPU when processing WMI logs.

**Solutions**:

1. **Use filtering** to reduce processing:
   ```bash
   sudo ./nlmon --wmi /var/log/kern.log --filter "wmi.cmd=REQUEST_LINK_STATS"
   ```

2. **Limit log file size**: Rotate logs regularly

3. **Use follow mode** instead of processing entire file:
   ```bash
   sudo ./nlmon --wmi follow:/var/log/kern.log
   ```

### Missing Timestamps

**Problem**: WMI events don't show timestamps.

**Cause**: Some log formats don't include timestamps in the WMI log line itself.

**Solution**: nlmon will use the system timestamp when the line is read. For accurate timestamps, ensure your syslog includes timestamps.

### Unknown Command IDs

**Problem**: Seeing `UNKNOWN(XXXXX)` instead of command names.

**Cause**: The command ID is not in nlmon's known command list.

**Solution**: This is normal for newer or undocumented WMI commands. The numeric ID is still displayed for reference.

## Performance Considerations

### Parsing Performance

nlmon's WMI parser is optimized for high throughput:

- **Parse rate**: >10,000 lines/second on modern hardware
- **Latency**: <1ms per line
- **Memory overhead**: <10MB for buffers

### Optimization Tips

1. **Use filtering** to reduce processing overhead
2. **Limit buffer sizes** in high-volume scenarios
3. **Use follow mode** for real-time monitoring (more efficient than processing entire files)
4. **Disable verbose output** if not needed

### Resource Limits

Default resource limits:

- **Line buffer size**: 4096 bytes
- **Event queue size**: 1024 events
- **Max line length**: 8192 bytes

These can be adjusted if needed for specific use cases.

## Advanced Topics

### Custom Log Formats

If you have custom WMI log formats, you can extend the parser by modifying `src/core/qca_wmi.c`:

1. Add pattern matching in `wmi_parse_log_line()`
2. Extract relevant fields
3. Populate `wmi_log_entry` structure
4. Rebuild nlmon

### WMI Command Correlation

Correlate WMI commands with application behavior:

```bash
# Terminal 1: Monitor WMI
sudo ./nlmon --wmi follow:/var/log/kern.log

# Terminal 2: Trigger WiFi operations
sudo iw dev wlan0 scan
```

Observe the WMI commands generated by the scan operation.

### Scripting and Automation

Process WMI events programmatically:

```bash
#!/bin/bash
# Count WMI commands by type

sudo ./nlmon --wmi /var/log/kern.log --json - | \
  jq -r '.wmi_cmd' | \
  sort | uniq -c | sort -rn
```

### Integration with Monitoring Systems

Forward WMI events to monitoring systems:

```bash
# Forward to Prometheus
sudo ./nlmon --wmi follow:/var/log/kern.log --prometheus :9090

# Forward to syslog
sudo ./nlmon --wmi follow:/var/log/kern.log --syslog localhost:514

# Forward to custom endpoint
sudo ./nlmon --wmi follow:/var/log/kern.log --json - | \
  curl -X POST -d @- http://monitoring.example.com/wmi
```

### Debugging Driver Issues

Use WMI monitoring to debug driver-firmware communication:

1. **Enable verbose logging**:
   ```bash
   echo 0xffffffff > /sys/module/ath10k_core/parameters/debug_mask
   ```

2. **Monitor WMI commands**:
   ```bash
   sudo ./nlmon --wmi follow:/var/log/kern.log -V
   ```

3. **Reproduce the issue** and observe WMI command patterns

4. **Analyze patterns** for anomalies or unexpected sequences

### Performance Profiling

Profile WMI command frequency and patterns:

```bash
# Monitor for 60 seconds and analyze
timeout 60 sudo ./nlmon --wmi follow:/var/log/kern.log --json wmi_profile.json

# Analyze command frequency
jq -r '.wmi_cmd' wmi_profile.json | sort | uniq -c | sort -rn

# Analyze by VDEV
jq -r '.wmi_vdev' wmi_profile.json | sort | uniq -c

# Analyze statistics types
jq -r '.wmi_stats' wmi_profile.json | sort | uniq -c | sort -rn
```

## See Also

- [QCA_VENDOR_SUPPORT.md](QCA_VENDOR_SUPPORT.md) - QCA vendor command monitoring
- [ARCHITECTURE.md](ARCHITECTURE.md) - nlmon architecture overview
- [EVENT_HOOKS.md](EVENT_HOOKS.md) - Event hook configuration
- [REST_API.md](REST_API.md) - REST API for programmatic access

## References

- [Qualcomm Atheros WiFi Drivers](https://wireless.wiki.kernel.org/en/users/drivers/ath10k)
- [nl80211 Documentation](https://wireless.wiki.kernel.org/en/developers/documentation/nl80211)
- [hostap Project](https://w1.fi/hostap/)

