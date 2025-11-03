nlmon
=====

Simple example of how to use libnl and libev to monitor kernel netlink
events.  Enhanced with support for multiple NETLINK features and an optional
interactive CLI interface. Now includes support for the nlmon kernel module
for raw netlink packet capture and analysis.

Features
--------

* Monitor link changes (interfaces up/down, added/removed)
* Monitor route changes (default route changes)
* Monitor address changes (IP addresses added/removed)
* Monitor neighbor/ARP table changes
* Monitor routing rule changes
* Optional interactive ncurses-based CLI mode
* Real-time event logging with timestamps
* Event statistics tracking
* Scrollable event history in CLI mode
* Runtime toggling of monitoring features
* **NEW:** nlmon kernel module support for raw packet capture
* **NEW:** PCAP file writing for Wireshark analysis
* **NEW:** Verbose mode with detailed netlink message inspection
* **NEW:** Message type filtering for focused monitoring
* **NEW:** Support for multiple netlink protocol families


Building
--------

To build and use nlmon, you need the libnl, libev, and ncurses libraries.
On Debian/Ubuntu and Linux Mint, the following command installs the
required development libraries and their runtime equivalents:

    sudo apt install libnl-route-3-dev libnl-3-dev libev-dev libncursesw5-dev

Provided the code of this project is cloned to `~/nlmon`, do:

    cd ~/nlmon
    make

Or manually with:

    gcc -o nlmon nlmon.c -I/usr/include/libnl3 -lnl-3 -lnl-route-3 -lev -lncursesw -lpthread


Usage
-----

Basic mode (output to console):

    ./nlmon

Monitor only VETH interfaces:

    ./nlmon -v

Interactive CLI mode:

    ./nlmon -c

Disable specific monitoring:

    ./nlmon -i    # Disable address monitoring
    ./nlmon -a    # Disable neighbor monitoring
    ./nlmon -u    # Disable rules monitoring

Combine options:

    ./nlmon -c -v  # CLI mode, VETH interfaces only

**NEW: nlmon Kernel Module Support**

Capture all netlink messages using the nlmon kernel module:

    sudo modprobe nlmon
    sudo ip link add nlmon0 type nlmon
    sudo ip link set nlmon0 up
    sudo ./nlmon -m nlmon0 -V

Write captured netlink traffic to PCAP file for Wireshark:

    sudo ./nlmon -m nlmon0 -p netlink.pcap

Filter by specific message type (e.g., only link changes):

    sudo ./nlmon -m nlmon0 -V -f 16  # RTM_NEWLINK only

Verbose mode with detailed message information:

    ./nlmon -V


CLI Mode Commands
-----------------

When running in CLI mode (-c), the following keyboard commands are available:

* `q` - Quit the application
* `h` - Show help screen
* `c` - Clear event log
* `UP` - Scroll up through event history
* `DOWN` - Scroll down through event history
* `a` - Toggle address monitoring on/off
* `n` - Toggle neighbor monitoring on/off
* `r` - Toggle rules monitoring on/off

The CLI displays three panels:
1. **Event Log** - Shows network events with timestamps
2. **Statistics** - Shows counters for different event types
3. **Command Bar** - Shows available keyboard shortcuts


Testing
-------

In one terminal window, run nlmon:

    ./nlmon -v

Or in CLI mode:

    ./nlmon -c

In another terminal window, create and delete a VETH pair:

    sudo ip link add veth1 type veth peer name veth1-peer
    sudo ip link del veth1

Test address monitoring:

    sudo ip link add dummy0 type dummy
    sudo ip addr add 192.168.100.1/24 dev dummy0
    sudo ip link set dummy0 up
    sudo ip link del dummy0

There should be output from nlmon showing the various events.

Example output (non-CLI mode):

    nlmon: veth iface veth1-peer added
    nlmon: veth iface veth1 added
    nlmon: addr 192.168.100.1/24 added to dummy0
    nlmon: iface dummy0 changed state UP link ON
    nlmon: veth iface veth1 deleted
    nlmon: veth iface veth1-peer deleted


Monitored NETLINK Features
---------------------------

* **LINK** - Network interface changes (RTM_NEWLINK, RTM_DELLINK)
  - Interface creation and deletion
  - Interface state changes (UP/DOWN)
  - Link status changes (RUNNING/NOT RUNNING)
  - VETH interface detection

* **ROUTE** - Routing table changes (RTM_NEWROUTE, RTM_DELROUTE)
  - Default route additions and deletions

* **ADDR** - Address changes (RTM_NEWADDR, RTM_DELADDR)
  - IPv4 and IPv6 address assignments
  - Address removals from interfaces

* **NEIGH** - Neighbor/ARP table changes (RTM_NEWNEIGH, RTM_DELNEIGH)
  - ARP entries additions and deletions
  - Neighbor state changes

* **RULE** - Routing rule changes (RTM_NEWRULE, RTM_DELRULE)
  - Policy routing rule modifications

* **nlmon RAW CAPTURE** - When using -m option (requires nlmon kernel module)
  - Captures ALL netlink messages as raw packets
  - Supports writing to PCAP format for Wireshark analysis
  - Message type filtering available
  - Detailed message inspection in verbose mode


nlmon Kernel Module Details
----------------------------

The nlmon kernel module creates a virtual network device that mirrors all 
netlink messages exchanged between the kernel and userspace. This enables:

1. **Complete Netlink Visibility**: Capture messages from ALL netlink protocols,
   not just NETLINK_ROUTE (including NETLINK_GENERIC, NETLINK_SOCK_DIAG, etc.)

2. **PCAP Export**: Write captured traffic to standard PCAP format files that
   can be analyzed with Wireshark or other packet analysis tools

3. **Message Filtering**: Filter by specific message types to focus on 
   particular events of interest

4. **Protocol Analysis**: Detailed inspection of netlink message headers,
   flags, sequence numbers, and payload data

To use the nlmon capture feature:

```bash
# Load the nlmon kernel module (if available)
sudo modprobe nlmon

# Create an nlmon device
sudo ip link add nlmon0 type nlmon
sudo ip link set nlmon0 up

# Run nlmon with capture enabled
sudo ./nlmon -m nlmon0 -V -p capture.pcap

# In another terminal, trigger netlink events
sudo ip link add dummy0 type dummy
sudo ip addr add 192.168.1.1/24 dev dummy0
sudo ip link set dummy0 up

# View in Wireshark or analyze the PCAP file
wireshark capture.pcap
```


Origin & References
-------------------

This project is based on a deprecated external netlink plugin for the
[finit][] init system.  It was developed by various people at Westermo
over the years.  It has since been replaced by a native plugin.

Brought back to life by [Joachim Wiberg][] for use as an example of how
to utilize libnl and libev.

Enhanced with additional NETLINK features and interactive CLI by the
community.

[finit]:          https://github.com/troglobit/finit
[Joachim Wiberg]: http://troglobit.com
