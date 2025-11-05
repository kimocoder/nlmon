#!/bin/bash
# Test script to generate netlink events for nlmon to capture

echo "Generating netlink events..."
echo "Watch nlmon output for NETLINK_ROUTE events"
echo ""

# Generate link events
echo "[1] Bringing lo interface down and up..."
ip link set lo down
sleep 1
ip link set lo up
sleep 1

# Generate address events
echo "[2] Adding and removing IP address..."
ip addr add 10.99.99.1/32 dev lo
sleep 1
ip addr del 10.99.99.1/32 dev lo
sleep 1

# Generate route events
echo "[3] Adding and removing route..."
ip route add 192.0.2.0/24 via 127.0.0.1 dev lo
sleep 1
ip route del 192.0.2.0/24 via 127.0.0.1 dev lo
sleep 1

# Generate neighbor events (if possible)
echo "[4] Flushing ARP cache..."
ip neigh flush all
sleep 1

echo ""
echo "Done! Check nlmon output for captured events."
