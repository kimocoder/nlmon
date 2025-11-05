#!/bin/bash
# Demonstration of WMI CLI integration

echo "=== WMI CLI Integration Demo ==="
echo

# Create sample WMI log
DEMO_LOG="/tmp/demo_wmi.log"
cat > "$DEMO_LOG" << 'EOF'
[0x5F8A2B3C4D] [schedu] Send WMI command:WMI_REQUEST_STATS_CMDID command_id:90113 htc_tag:1
[12:34:56.789012] [wpa_su] LINK_LAYER_STATS - Get Request Params Request ID: 1 Stats Type: 7 Vdev ID: 0 Peer MAC Addr: AA:BB:CC:DD:EE:FF
[0x5F8A2B3C50] [schedu] STATS REQ STATS_ID:8463 VDEV_ID:0 PDEV_ID:0-->
[12:34:57.123456] [wpa_su] RCPI REQ VDEV_ID:0-->
[0x5F8A2B3C60] [schedu] send_time_stamp_sync_cmd_tlv: 1234: WMA --> DBGLOG_TIME_STAMP_SYNC_CMDID mode 1 time_stamp low 0x12345678 high 0x9ABCDEF0
[0x5F8A2B3C70] [schedu] Send WMI command:WMI_REQUEST_LINK_STATS_CMDID command_id:90116 htc_tag:2
EOF

echo "Sample WMI log created at: $DEMO_LOG"
echo
echo "Contents:"
cat "$DEMO_LOG"
echo
echo "---"
echo

echo "Demo 1: Basic WMI monitoring"
echo "Command: ./nlmon -w $DEMO_LOG"
echo "Output:"
timeout 0.5 ./nlmon -w "$DEMO_LOG" 2>&1 | grep "WMI" || echo "(WMI events displayed above)"
echo

echo "Demo 2: WMI with filter (only REQUEST_LINK_STATS)"
echo "Command: ./nlmon -w $DEMO_LOG -W 'wmi.cmd=REQUEST_LINK_STATS'"
echo "Output:"
timeout 0.5 ./nlmon -w "$DEMO_LOG" -W 'wmi.cmd=REQUEST_LINK_STATS' 2>&1 | grep "WMI" || echo "(Filtered WMI events displayed above)"
echo

echo "Demo 3: Help text showing WMI options"
echo "Command: ./nlmon -h | grep -A 6 'WMI Monitoring:'"
./nlmon -h 2>&1 | grep -A 6 "WMI Monitoring:"
echo

# Cleanup
rm -f "$DEMO_LOG"

echo "=== Demo Complete ==="
