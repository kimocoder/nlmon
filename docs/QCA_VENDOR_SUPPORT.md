# Qualcomm Vendor Command Support in nlmon

## Overview

nlmon now includes support for decoding Qualcomm (QCA) vendor-specific nl80211 commands. This allows monitoring and debugging of WiFi driver interactions that use QCA's proprietary vendor extensions.

## Features

- **Automatic QCA Vendor Detection**: Recognizes nl80211 vendor commands with QCA OUI (0x001374)
- **Command Name Decoding**: Translates vendor subcmd IDs to human-readable names
- **Generic Netlink Integration**: Works seamlessly with the existing `-g` flag
- **Comprehensive Command Coverage**: Supports 160+ QCA vendor commands

## Usage

### Basic Monitoring

Monitor all generic netlink events including QCA vendor commands:

```bash
sudo ./nlmon -g -V
```

### CLI Mode with QCA Vendor Support

Use the interactive CLI to see QCA vendor commands in real-time:

```bash
sudo ./nlmon -c -g
```

### Monitor All Protocols

Monitor all netlink protocols including QCA vendor commands:

```bash
sudo ./nlmon -A -V
```

### Capture to PCAP

Capture QCA vendor commands to a PCAP file for analysis:

```bash
sudo ./nlmon -m nlmon0 -g -p qca_vendor.pcap -V
```

## Supported QCA Vendor Commands

The following categories of QCA vendor commands are supported:

### WiFi Scanning & Roaming
- GSCAN (Google Scan) operations
- Roaming control and events
- PNO (Preferred Network Offload)
- Channel scanning and selection

### Link Layer Statistics
- LL_STATS_SET/GET/CLR
- Extended statistics (LL_STATS_EXT)
- WiFi firmware statistics

### Advanced Features
- NAN (Neighbor Awareness Networking)
- FTM (Fine Timing Measurement) for location
- AOA (Angle of Arrival) measurements
- Spectral scan operations
- TWT (Target Wake Time)

### Power Management
- SAR (Specific Absorption Rate) limits
- Power save configuration
- Thermal management

### Testing & Diagnostics
- OTA (Over The Air) testing
- WiFi test configuration
- Hardware capability queries
- Firmware state monitoring

### Network Optimization
- ACS (Automatic Channel Selection)
- TDLS (Tunneled Direct Link Setup)
- Coexistence configuration
- MCC (Multi-Channel Concurrency) quota

## Output Format

When a QCA vendor command is detected, nlmon displays it in the following format:

```
genetlink: family=nl80211/QCA:COMMAND_NAME cmd=SUBCMD_ID version=VERSION
```

Example output:
```
[12:34:56] genetlink: family=nl80211/QCA:GSCAN_START cmd=20 version=1
[12:34:57] genetlink: family=nl80211/QCA:GET_WIFI_INFO cmd=61 version=1
[12:34:58] genetlink: family=nl80211/QCA:ROAM_EVENTS cmd=163 version=1
```

## Implementation Details

### QCA OUI
The Qualcomm OUI (Organizationally Unique Identifier) is `0x001374`.

### nl80211 Attributes
QCA vendor commands use the following nl80211 attributes:
- `NL80211_ATTR_VENDOR_ID` (195): Contains the vendor OUI
- `NL80211_ATTR_VENDOR_SUBCMD` (196): Contains the vendor-specific command ID

### Source Files
- `include/qca_vendor.h`: QCA vendor command definitions
- `src/core/qca_vendor.c`: Command name lookup implementation
- `src/core/netlink_multi_protocol.c`: Generic netlink parsing with QCA vendor support

## Use Cases

### WiFi Driver Development
Monitor QCA vendor commands during WiFi driver development and debugging:
```bash
sudo ./nlmon -g -V | grep "QCA:"
```

### Roaming Analysis
Track roaming decisions and events:
```bash
sudo ./nlmon -g -V | grep "ROAM"
```

### Scan Debugging
Monitor GSCAN operations:
```bash
sudo ./nlmon -g -V | grep "GSCAN"
```

### Power Management Tuning
Observe SAR limit changes and power save events:
```bash
sudo ./nlmon -g -V | grep -E "SAR|PWRSAVE"
```

## Compatibility

- Works with Qualcomm Atheros WiFi chipsets (ath10k, ath11k, ath12k drivers)
- Compatible with Android WiFi HAL implementations
- Supports both kernel-to-userspace and userspace-to-kernel vendor commands

## References

- [hostap qca-vendor.h](https://git.w1.fi/cgit/hostap/tree/src/common/qca-vendor.h)
- [hostap qca-vendor-attr.h](https://git.w1.fi/cgit/hostap/tree/src/common/qca-vendor-attr.h)
- [nl80211 Vendor Commands](https://wireless.wiki.kernel.org/en/developers/documentation/nl80211)

## Troubleshooting

### No QCA Vendor Commands Visible

1. Ensure you're using the `-g` or `-A` flag to enable generic netlink monitoring
2. Verify your WiFi driver supports QCA vendor commands (check `dmesg | grep ath`)
3. Make sure you have root privileges (`sudo`)
4. Check that nl80211 events are being generated (trigger a WiFi scan or roam)

### Unknown Vendor Commands

If you see `nl80211/vendor:0xXXXXXX` instead of `nl80211/QCA:COMMAND_NAME`, it means:
- The vendor OUI is not QCA (0x001374)
- Another vendor's commands are being used
- The command ID is not in the known QCA command list

## Future Enhancements

Potential future additions:
- QCA vendor attribute parsing and display
- Vendor-specific data payload decoding
- Support for other vendor OUIs (Intel, Broadcom, etc.)
- Filtering by specific QCA vendor commands
