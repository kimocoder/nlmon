#!/bin/bash
# Test script to demonstrate enhanced netlink event output

echo "=== Testing Enhanced Netlink Output ==="
echo ""
echo "Starting nlmon in background..."
sudo ./nlmon -V -A > /tmp/nlmon_test.log 2>&1 &
NLMON_PID=$!
sleep 2

echo "Generating network events..."
echo ""

echo "1. Interface events (link up/down)..."
sudo ip link set lo down
sleep 1
sudo ip link set lo up
sleep 1

echo "2. Address events (add/remove IP)..."
sudo ip addr add 10.99.99.1/32 dev lo
sleep 1
sudo ip addr del 10.99.99.1/32 dev lo
sleep 1

echo "3. Route events (add/remove route)..."
sudo ip route add 192.0.2.0/24 via 127.0.0.1 dev lo
sleep 1
sudo ip route del 192.0.2.0/24 via 127.0.0.1 dev lo
sleep 1

echo "4. Neighbor events (flush ARP cache)..."
sudo ip neigh flush all
sleep 1

echo ""
echo "Stopping nlmon..."
sudo kill $NLMON_PID
sleep 1

echo ""
echo "=== Captured Events ==="
cat /tmp/nlmon_test.log | grep -E "ROUTE:|NL80211:|GENL:" | tail -20

echo ""
echo "Test complete! Full log saved to /tmp/nlmon_test.log"
