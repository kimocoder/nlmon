#!/bin/bash
#
# WMI Monitoring Example Script
#
# This script demonstrates various ways to monitor Qualcomm WMI
# (Wireless Management Interface) commands using nlmon.
#
# Usage: ./wmi-monitoring.sh [option]
#
# Options:
#   basic       - Basic WMI monitoring from kernel log
#   follow      - Real-time WMI monitoring (follow mode)
#   combined    - WMI + netlink monitoring
#   filtered    - Monitor specific WMI commands
#   stats       - Monitor only statistics requests
#   export      - Export WMI events to JSON
#   analyze     - Analyze WMI command patterns
#

set -e

# Configuration
NLMON="./nlmon"
LOG_FILE="/var/log/kern.log"
OUTPUT_DIR="/tmp/nlmon_wmi"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_requirements() {
    # Check if nlmon exists
    if [ ! -f "$NLMON" ]; then
        print_error "nlmon not found at $NLMON"
        print_info "Please build nlmon first: make"
        exit 1
    fi

    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        print_warning "This script should be run as root for full functionality"
        print_info "Some features may not work without root privileges"
    fi

    # Check if log file exists
    if [ ! -f "$LOG_FILE" ]; then
        print_warning "Log file $LOG_FILE not found"
        print_info "You may need to adjust LOG_FILE variable or use dmesg"
    fi

    # Create output directory
    mkdir -p "$OUTPUT_DIR"
}

# Example 1: Basic WMI Monitoring
basic_monitoring() {
    print_header "Example 1: Basic WMI Monitoring"
    print_info "Monitoring WMI commands from $LOG_FILE"
    print_info "Press Ctrl+C to stop"
    echo ""

    sudo "$NLMON" --wmi "$LOG_FILE"
}

# Example 2: Real-time WMI Monitoring (Follow Mode)
follow_monitoring() {
    print_header "Example 2: Real-time WMI Monitoring"
    print_info "Following WMI commands in real-time (like tail -f)"
    print_info "Press Ctrl+C to stop"
    echo ""

    sudo "$NLMON" --wmi "follow:$LOG_FILE"
}

# Example 3: Combined Netlink and WMI Monitoring
combined_monitoring() {
    print_header "Example 3: Combined Netlink + WMI Monitoring"
    print_info "Monitoring both nl80211 vendor commands and WMI commands"
    print_info "This shows the correlation between high-level and low-level operations"
    print_info "Press Ctrl+C to stop"
    echo ""

    sudo "$NLMON" -g --wmi "$LOG_FILE"
}

# Example 4: Filtered WMI Monitoring
filtered_monitoring() {
    print_header "Example 4: Filtered WMI Monitoring"
    print_info "Monitoring only REQUEST_LINK_STATS commands"
    print_info "Press Ctrl+C to stop"
    echo ""

    sudo "$NLMON" --wmi "$LOG_FILE" --filter "protocol=WMI && wmi.cmd=REQUEST_LINK_STATS"
}

# Example 5: Statistics Monitoring
stats_monitoring() {
    print_header "Example 5: Statistics Request Monitoring"
    print_info "Monitoring all WMI statistics requests"
    print_info "Press Ctrl+C to stop"
    echo ""

    sudo "$NLMON" --wmi "$LOG_FILE" --filter "protocol=WMI && (wmi.cmd=REQUEST_STATS || wmi.cmd=REQUEST_LINK_STATS)"
}

# Example 6: Export to JSON
export_monitoring() {
    print_header "Example 6: Export WMI Events to JSON"
    
    OUTPUT_FILE="$OUTPUT_DIR/wmi_events_$(date +%Y%m%d_%H%M%S).json"
    
    print_info "Capturing WMI events to $OUTPUT_FILE"
    print_info "Monitoring for 30 seconds..."
    echo ""

    timeout 30 sudo "$NLMON" --wmi "$LOG_FILE" --json "$OUTPUT_FILE" || true

    if [ -f "$OUTPUT_FILE" ]; then
        print_info "Capture complete!"
        print_info "Event count: $(wc -l < "$OUTPUT_FILE")"
        print_info "File: $OUTPUT_FILE"
        echo ""
        print_info "Sample events:"
        head -n 5 "$OUTPUT_FILE" | jq '.' 2>/dev/null || head -n 5 "$OUTPUT_FILE"
    else
        print_warning "No events captured"
    fi
}

# Example 7: Analyze WMI Command Patterns
analyze_monitoring() {
    print_header "Example 7: Analyze WMI Command Patterns"
    
    TEMP_FILE="$OUTPUT_DIR/wmi_analysis_$(date +%Y%m%d_%H%M%S).json"
    
    print_info "Capturing WMI events for 60 seconds..."
    timeout 60 sudo "$NLMON" --wmi "$LOG_FILE" --json "$TEMP_FILE" || true

    if [ ! -f "$TEMP_FILE" ] || [ ! -s "$TEMP_FILE" ]; then
        print_warning "No events captured for analysis"
        return
    fi

    echo ""
    print_info "Analysis Results:"
    echo ""

    # Command frequency
    echo -e "${GREEN}Command Frequency:${NC}"
    jq -r '.wmi_cmd // empty' "$TEMP_FILE" 2>/dev/null | sort | uniq -c | sort -rn || \
        grep -o '"wmi_cmd":"[^"]*"' "$TEMP_FILE" | cut -d'"' -f4 | sort | uniq -c | sort -rn

    echo ""

    # Statistics type frequency
    echo -e "${GREEN}Statistics Type Frequency:${NC}"
    jq -r '.wmi_stats // empty' "$TEMP_FILE" 2>/dev/null | sort | uniq -c | sort -rn || \
        grep -o '"wmi_stats":"[^"]*"' "$TEMP_FILE" | cut -d'"' -f4 | sort | uniq -c | sort -rn

    echo ""

    # VDEV distribution
    echo -e "${GREEN}VDEV Distribution:${NC}"
    jq -r '.wmi_vdev // empty' "$TEMP_FILE" 2>/dev/null | sort | uniq -c || \
        grep -o '"wmi_vdev":[0-9]*' "$TEMP_FILE" | cut -d':' -f2 | sort | uniq -c

    echo ""
    print_info "Full data saved to: $TEMP_FILE"
}

# Example 8: Monitor from dmesg
dmesg_monitoring() {
    print_header "Example 8: Monitor WMI from dmesg"
    print_info "Monitoring WMI commands from kernel ring buffer"
    print_info "Press Ctrl+C to stop"
    echo ""

    sudo dmesg -w | sudo "$NLMON" --wmi -
}

# Example 9: Monitor specific VDEV
vdev_monitoring() {
    print_header "Example 9: Monitor Specific VDEV"
    print_info "Monitoring WMI commands for VDEV 0 only"
    print_info "Press Ctrl+C to stop"
    echo ""

    sudo "$NLMON" --wmi "$LOG_FILE" --filter "protocol=WMI && wmi.vdev=0"
}

# Example 10: Monitor with correlation
correlation_monitoring() {
    print_header "Example 10: WMI with Event Correlation"
    print_info "Monitoring WMI with correlation to detect patterns"
    print_info "Press Ctrl+C to stop"
    echo ""

    # This assumes nlmon has correlation features enabled
    sudo "$NLMON" --wmi "$LOG_FILE" -g --correlation
}

# Show usage
show_usage() {
    cat << EOF
WMI Monitoring Example Script

Usage: $0 [option]

Options:
  basic       - Basic WMI monitoring from kernel log
  follow      - Real-time WMI monitoring (follow mode)
  combined    - WMI + netlink monitoring
  filtered    - Monitor specific WMI commands
  stats       - Monitor only statistics requests
  export      - Export WMI events to JSON
  analyze     - Analyze WMI command patterns
  dmesg       - Monitor WMI from dmesg
  vdev        - Monitor specific VDEV
  correlation - WMI with event correlation
  all         - Run all examples (interactive)

Examples:
  $0 basic
  $0 follow
  $0 analyze

EOF
}

# Run all examples interactively
run_all() {
    print_header "WMI Monitoring Examples - Interactive Mode"
    
    examples=(
        "basic:Basic WMI Monitoring"
        "follow:Real-time Monitoring"
        "combined:Combined Netlink + WMI"
        "filtered:Filtered Monitoring"
        "stats:Statistics Monitoring"
        "export:Export to JSON"
        "analyze:Analyze Patterns"
        "dmesg:Monitor from dmesg"
        "vdev:Monitor Specific VDEV"
    )

    for example in "${examples[@]}"; do
        IFS=':' read -r cmd desc <<< "$example"
        echo ""
        print_info "Next: $desc"
        read -p "Press Enter to continue or 'q' to quit: " response
        if [ "$response" = "q" ]; then
            break
        fi
        eval "${cmd}_monitoring"
    done

    print_info "All examples completed!"
}

# Main script
main() {
    check_requirements

    case "${1:-}" in
        basic)
            basic_monitoring
            ;;
        follow)
            follow_monitoring
            ;;
        combined)
            combined_monitoring
            ;;
        filtered)
            filtered_monitoring
            ;;
        stats)
            stats_monitoring
            ;;
        export)
            export_monitoring
            ;;
        analyze)
            analyze_monitoring
            ;;
        dmesg)
            dmesg_monitoring
            ;;
        vdev)
            vdev_monitoring
            ;;
        correlation)
            correlation_monitoring
            ;;
        all)
            run_all
            ;;
        help|--help|-h)
            show_usage
            ;;
        *)
            show_usage
            exit 1
            ;;
    esac
}

main "$@"
