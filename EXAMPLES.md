# nlmon Usage Examples

This document provides practical examples of using nlmon for various networking monitoring and debugging tasks.

## Basic Monitoring

### Monitor All Network Events

```bash
./nlmon
```

Output:
```
nlmon: iface eth0 changed state UP link ON
nlmon: default route added
nlmon: addr 192.168.1.100/24 added to eth0
```

### Monitor Only VETH Interfaces

Useful for container networking debugging:

```bash
./nlmon -v
```

### Interactive CLI Mode

```bash
./nlmon -c
```

Navigate with arrow keys, press 'h' for help, 'q' to quit.

## Selective Monitoring

### Monitor Only Address Changes

```bash
./nlmon -a -u  # Disable neighbor and rules monitoring
```

### Monitor Only Link Changes

```bash
./nlmon -i -a -u  # Disable address, neighbor, and rules
```

## nlmon Kernel Module Features

### Capture All Netlink Messages

Requires root privileges and nlmon kernel module:

```bash
# Setup nlmon device
sudo modprobe nlmon
sudo ip link add nlmon0 type nlmon
sudo ip link set nlmon0 up

# Start capturing
sudo ./nlmon -m nlmon0 -V
```

Output:
```
[12:34:56] Created nlmon device
[12:34:56] Bound to nlmon device nlmon0 (ifindex=8)
[12:34:56] nlmon device is up
[12:34:57] nlmon: pkt #1, len=1064, RTM_NEWLINK, flags=0x0, seq=0, pid=1234
[12:34:58] nlmon: pkt #2, len=52, RTM_NEWADDR, flags=0x600, seq=1, pid=5678
```

### Write to PCAP File for Wireshark Analysis

```bash
sudo ./nlmon -m nlmon0 -p /tmp/netlink_capture.pcap
```

Then analyze with Wireshark:
```bash
wireshark /tmp/netlink_capture.pcap
```

Or with tcpdump:
```bash
tcpdump -r /tmp/netlink_capture.pcap -v
```

### Filter Specific Message Types

#### Monitor Only Link Addition/Deletion

```bash
# RTM_NEWLINK = 16, RTM_DELLINK = 17
sudo ./nlmon -m nlmon0 -V -f 16
```

#### Monitor Only Address Changes

```bash
# RTM_NEWADDR = 20, RTM_DELADDR = 21
sudo ./nlmon -m nlmon0 -V -f 20
```

#### Monitor Only Route Changes

```bash
# RTM_NEWROUTE = 24, RTM_DELROUTE = 25
sudo ./nlmon -m nlmon0 -V -f 24
```

## Debugging Scenarios

### Container Networking Issues

Monitor veth pair creation and traffic:

```bash
# Terminal 1: Start monitoring
sudo ./nlmon -v -V

# Terminal 2: Create container
docker run --rm -it alpine sh

# Terminal 1 will show:
# nlmon: veth iface veth1234567 added
# nlmon: veth iface veth1234567 changed state UP link ON
```

### Network Manager Debugging

Capture all netlink messages during NetworkManager operations:

```bash
# Start capture
sudo ./nlmon -m nlmon0 -p /tmp/nm_debug.pcap -V

# In another terminal, trigger NetworkManager action
sudo systemctl restart NetworkManager

# Stop capture and analyze
wireshark /tmp/nm_debug.pcap
```

### IP Address Configuration Debugging

```bash
# Terminal 1: Monitor address changes
./nlmon -V

# Terminal 2: Add/remove addresses
sudo ip link add dummy0 type dummy
sudo ip addr add 192.168.100.1/24 dev dummy0
sudo ip addr add 192.168.100.2/24 dev dummy0
sudo ip addr del 192.168.100.1/24 dev dummy0
sudo ip link del dummy0
```

### Route Table Monitoring

```bash
# Monitor route changes
./nlmon

# In another terminal, manipulate routes
sudo ip route add 10.0.0.0/8 via 192.168.1.1
sudo ip route del 10.0.0.0/8
```

### ARP/Neighbor Table Monitoring

```bash
# Monitor neighbor changes
./nlmon

# In another terminal
sudo ip neigh add 192.168.1.50 lladdr 00:11:22:33:44:55 dev eth0
sudo ip neigh del 192.168.1.50 dev eth0
```

## Advanced Use Cases

### Continuous Logging to File

```bash
./nlmon -V 2>&1 | tee /tmp/netlink_events.log
```

### Combine with CLI Mode and Selective Monitoring

```bash
./nlmon -c -v -i  # CLI mode, VETH only, no address monitoring
```

### PCAP Capture with Rotation

```bash
#!/bin/bash
while true; do
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    timeout 300 sudo ./nlmon -m nlmon0 -p /tmp/netlink_${TIMESTAMP}.pcap
    sleep 1
done
```

### Filter and Log Specific Events

```bash
# Capture only link events to separate file
sudo ./nlmon -m nlmon0 -f 16 -p /tmp/link_events.pcap &

# Capture only address events to separate file
sudo ./nlmon -m nlmon0 -f 20 -p /tmp/addr_events.pcap &
```

## Integration Examples

### Systemd Service

Create `/etc/systemd/system/nlmon.service`:

```ini
[Unit]
Description=Netlink Monitor
After=network.target

[Service]
Type=simple
ExecStartPre=/sbin/modprobe nlmon
ExecStartPre=/sbin/ip link add nlmon0 type nlmon
ExecStartPre=/sbin/ip link set nlmon0 up
ExecStart=/usr/local/bin/nlmon -m nlmon0 -p /var/log/netlink.pcap
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

### Docker Container Monitoring Script

```bash
#!/bin/bash
# monitor_container_network.sh

echo "Starting container network monitor..."
./nlmon -v -V > /tmp/container_events.log 2>&1 &
NLMON_PID=$!

echo "nlmon running (PID: $NLMON_PID)"
echo "Container network events logged to /tmp/container_events.log"
echo "Press Ctrl+C to stop"

trap "kill $NLMON_PID; exit" INT TERM
wait $NLMON_PID
```

## Troubleshooting

### nlmon Device Creation Fails

If you get "Failed to create nlmon device":

```bash
# Check if module is available
modinfo nlmon

# If not available, your kernel may not have CONFIG_NLMON enabled
zcat /proc/config.gz | grep CONFIG_NLMON

# Alternative: Use without nlmon module (libnl monitoring only)
./nlmon -V
```

### Permission Denied

Most nlmon operations require root:

```bash
sudo ./nlmon -m nlmon0
```

Or add CAP_NET_ADMIN capability:

```bash
sudo setcap cap_net_admin+ep ./nlmon
./nlmon -m nlmon0
```

### No Events Appearing

Check if the nlmon device is up:

```bash
ip link show nlmon0
```

Verify netlink events are being generated:

```bash
# Generate test events
sudo ip link add dummy_test type dummy
sudo ip link del dummy_test
```

## Message Type Reference

Common netlink message types for filtering:

| Type | Value | Description |
|------|-------|-------------|
| RTM_NEWLINK | 16 | New network interface |
| RTM_DELLINK | 17 | Delete network interface |
| RTM_GETLINK | 18 | Get network interface |
| RTM_NEWADDR | 20 | New IP address |
| RTM_DELADDR | 21 | Delete IP address |
| RTM_GETADDR | 22 | Get IP address |
| RTM_NEWROUTE | 24 | New route |
| RTM_DELROUTE | 25 | Delete route |
| RTM_GETROUTE | 26 | Get route |
| RTM_NEWNEIGH | 28 | New neighbor/ARP entry |
| RTM_DELNEIGH | 29 | Delete neighbor/ARP entry |
| RTM_GETNEIGH | 30 | Get neighbor/ARP entry |
| RTM_NEWRULE | 32 | New routing rule |
| RTM_DELRULE | 33 | Delete routing rule |
| RTM_GETRULE | 34 | Get routing rule |

Use these values with the `-f` option to filter specific message types.
