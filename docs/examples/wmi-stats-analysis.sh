#!/bin/bash
#
# WMI Statistics Analysis Script
#
# This script analyzes WMI command patterns and statistics from nlmon output.
# It provides insights into WiFi firmware behavior and performance.
#
# Usage: ./wmi-stats-analysis.sh [options]
#
# Options:
#   --capture DURATION    - Capture WMI events for DURATION seconds (default: 60)
#   --input FILE          - Analyze existing JSON file instead of capturing
#   --output DIR          - Output directory for reports (default: /tmp/wmi_analysis)
#   --log FILE            - WMI log source (default: /var/log/kern.log)
#   --format FORMAT       - Output format: text, html, csv (default: text)
#

set -e

# Configuration
NLMON="./nlmon"
LOG_FILE="/var/log/kern.log"
OUTPUT_DIR="/tmp/wmi_analysis"
CAPTURE_DURATION=60
INPUT_FILE=""
OUTPUT_FORMAT="text"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Helper functions
print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_section() {
    echo ""
    echo -e "${CYAN}--- $1 ---${NC}"
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

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --capture)
                CAPTURE_DURATION="$2"
                shift 2
                ;;
            --input)
                INPUT_FILE="$2"
                shift 2
                ;;
            --output)
                OUTPUT_DIR="$2"
                shift 2
                ;;
            --log)
                LOG_FILE="$2"
                shift 2
                ;;
            --format)
                OUTPUT_FORMAT="$2"
                shift 2
                ;;
            --help|-h)
                show_usage
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
}

show_usage() {
    cat << EOF
WMI Statistics Analysis Script

Usage: $0 [options]

Options:
  --capture DURATION    Capture WMI events for DURATION seconds (default: 60)
  --input FILE          Analyze existing JSON file instead of capturing
  --output DIR          Output directory for reports (default: /tmp/wmi_analysis)
  --log FILE            WMI log source (default: /var/log/kern.log)
  --format FORMAT       Output format: text, html, csv (default: text)
  --help, -h            Show this help message

Examples:
  # Capture and analyze for 60 seconds
  $0

  # Capture for 5 minutes
  $0 --capture 300

  # Analyze existing capture
  $0 --input /tmp/wmi_events.json

  # Generate HTML report
  $0 --format html --output /var/www/html/wmi_report

EOF
}

# Check requirements
check_requirements() {
    if [ ! -f "$NLMON" ]; then
        print_error "nlmon not found at $NLMON"
        exit 1
    fi

    if [ "$EUID" -ne 0 ]; then
        print_warning "Running without root privileges"
    fi

    mkdir -p "$OUTPUT_DIR"

    # Check for jq
    if ! command -v jq &> /dev/null; then
        print_warning "jq not found - some analysis features will be limited"
    fi
}

# Capture WMI events
capture_events() {
    local output_file="$OUTPUT_DIR/wmi_capture_$(date +%Y%m%d_%H%M%S).json"
    
    print_header "Capturing WMI Events"
    print_info "Duration: ${CAPTURE_DURATION}s"
    print_info "Source: $LOG_FILE"
    print_info "Output: $output_file"
    echo ""

    timeout "$CAPTURE_DURATION" sudo "$NLMON" --wmi "$LOG_FILE" --json "$output_file" 2>/dev/null || true

    if [ ! -f "$output_file" ] || [ ! -s "$output_file" ]; then
        print_error "No events captured"
        exit 1
    fi

    print_info "Captured $(wc -l < "$output_file") events"
    echo "$output_file"
}

# Analyze command frequency
analyze_command_frequency() {
    local data_file="$1"
    
    print_section "Command Frequency Analysis"

    if command -v jq &> /dev/null; then
        echo ""
        printf "%-30s %10s %10s\n" "Command" "Count" "Percentage"
        printf "%-30s %10s %10s\n" "-------" "-----" "----------"
        
        local total=$(jq -r '.wmi_cmd // empty' "$data_file" 2>/dev/null | wc -l)
        
        jq -r '.wmi_cmd // empty' "$data_file" 2>/dev/null | \
            sort | uniq -c | sort -rn | \
            while read count cmd; do
                local pct=$(awk "BEGIN {printf \"%.2f\", ($count/$total)*100}")
                printf "%-30s %10d %9.2f%%\n" "$cmd" "$count" "$pct"
            done
    else
        grep -o '"wmi_cmd":"[^"]*"' "$data_file" | cut -d'"' -f4 | sort | uniq -c | sort -rn
    fi
}

# Analyze statistics types
analyze_stats_types() {
    local data_file="$1"
    
    print_section "Statistics Type Analysis"

    if command -v jq &> /dev/null; then
        echo ""
        printf "%-30s %10s %10s\n" "Stats Type" "Count" "Percentage"
        printf "%-30s %10s %10s\n" "----------" "-----" "----------"
        
        local total=$(jq -r '.wmi_stats // empty' "$data_file" 2>/dev/null | grep -v '^$' | wc -l)
        
        if [ "$total" -gt 0 ]; then
            jq -r '.wmi_stats // empty' "$data_file" 2>/dev/null | \
                grep -v '^$' | sort | uniq -c | sort -rn | \
                while read count stats; do
                    local pct=$(awk "BEGIN {printf \"%.2f\", ($count/$total)*100}")
                    printf "%-30s %10d %9.2f%%\n" "$stats" "$count" "$pct"
                done
        else
            print_info "No statistics type data found"
        fi
    else
        grep -o '"wmi_stats":"[^"]*"' "$data_file" | cut -d'"' -f4 | sort | uniq -c | sort -rn
    fi
}

# Analyze VDEV distribution
analyze_vdev_distribution() {
    local data_file="$1"
    
    print_section "VDEV Distribution"

    if command -v jq &> /dev/null; then
        echo ""
        printf "%-10s %10s %10s\n" "VDEV ID" "Count" "Percentage"
        printf "%-10s %10s %10s\n" "-------" "-----" "----------"
        
        local total=$(jq -r '.wmi_vdev // empty' "$data_file" 2>/dev/null | grep -v '^$' | wc -l)
        
        if [ "$total" -gt 0 ]; then
            jq -r '.wmi_vdev // empty' "$data_file" 2>/dev/null | \
                grep -v '^$' | sort -n | uniq -c | \
                while read count vdev; do
                    local pct=$(awk "BEGIN {printf \"%.2f\", ($count/$total)*100}")
                    printf "%-10s %10d %9.2f%%\n" "$vdev" "$count" "$pct"
                done
        else
            print_info "No VDEV data found"
        fi
    else
        grep -o '"wmi_vdev":[0-9]*' "$data_file" | cut -d':' -f2 | sort -n | uniq -c
    fi
}

# Analyze temporal patterns
analyze_temporal_patterns() {
    local data_file="$1"
    
    print_section "Temporal Pattern Analysis"

    if command -v jq &> /dev/null; then
        echo ""
        
        # Commands per second
        local duration=$(jq -r '.timestamp // empty' "$data_file" 2>/dev/null | \
            awk 'NR==1{first=$1} END{print $1-first}')
        local total_events=$(wc -l < "$data_file")
        
        if [ -n "$duration" ] && [ "$duration" != "0" ]; then
            local rate=$(awk "BEGIN {printf \"%.2f\", $total_events/$duration}")
            echo "Average event rate: $rate events/second"
        fi
        
        echo ""
        echo "Events by second (first 10 seconds):"
        jq -r '.timestamp // empty' "$data_file" 2>/dev/null | \
            awk '{print int($1)}' | sort -n | uniq -c | head -10 | \
            while read count sec; do
                printf "Second %d: %d events\n" "$sec" "$count"
            done
    else
        print_info "Temporal analysis requires jq"
    fi
}

# Analyze peer MAC addresses
analyze_peer_macs() {
    local data_file="$1"
    
    print_section "Peer MAC Address Analysis"

    if command -v jq &> /dev/null; then
        echo ""
        local peer_count=$(jq -r '.wmi_peer // empty' "$data_file" 2>/dev/null | grep -v '^$' | sort -u | wc -l)
        
        if [ "$peer_count" -gt 0 ]; then
            echo "Unique peer MAC addresses: $peer_count"
            echo ""
            printf "%-20s %10s\n" "Peer MAC" "Count"
            printf "%-20s %10s\n" "--------" "-----"
            
            jq -r '.wmi_peer // empty' "$data_file" 2>/dev/null | \
                grep -v '^$' | sort | uniq -c | sort -rn | head -10 | \
                while read count mac; do
                    printf "%-20s %10d\n" "$mac" "$count"
                done
        else
            print_info "No peer MAC address data found"
        fi
    else
        grep -o '"wmi_peer":"[^"]*"' "$data_file" | cut -d'"' -f4 | sort | uniq -c | sort -rn
    fi
}

# Generate summary report
generate_summary() {
    local data_file="$1"
    
    print_header "WMI Analysis Summary"
    
    local total_events=$(wc -l < "$data_file")
    local unique_commands=$(jq -r '.wmi_cmd // empty' "$data_file" 2>/dev/null | sort -u | wc -l)
    local unique_vdevs=$(jq -r '.wmi_vdev // empty' "$data_file" 2>/dev/null | grep -v '^$' | sort -u | wc -l)
    
    echo ""
    echo "Total Events: $total_events"
    echo "Unique Commands: $unique_commands"
    echo "Unique VDEVs: $unique_vdevs"
    echo "Analysis Time: $(date)"
    echo "Data File: $data_file"
}

# Generate HTML report
generate_html_report() {
    local data_file="$1"
    local html_file="$OUTPUT_DIR/wmi_report_$(date +%Y%m%d_%H%M%S).html"
    
    print_info "Generating HTML report: $html_file"
    
    cat > "$html_file" << 'EOF'
<!DOCTYPE html>
<html>
<head>
    <title>WMI Analysis Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
        h1 { color: #333; border-bottom: 3px solid #4CAF50; padding-bottom: 10px; }
        h2 { color: #555; margin-top: 30px; border-bottom: 2px solid #ddd; padding-bottom: 5px; }
        table { width: 100%; border-collapse: collapse; margin: 20px 0; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background-color: #4CAF50; color: white; }
        tr:hover { background-color: #f5f5f5; }
        .summary { background: #e8f5e9; padding: 15px; border-radius: 5px; margin: 20px 0; }
        .summary-item { margin: 10px 0; font-size: 16px; }
        .chart { margin: 20px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>WMI Analysis Report</h1>
        <div class="summary">
            <div class="summary-item"><strong>Generated:</strong> <span id="timestamp"></span></div>
            <div class="summary-item"><strong>Total Events:</strong> <span id="total-events"></span></div>
            <div class="summary-item"><strong>Unique Commands:</strong> <span id="unique-commands"></span></div>
        </div>
        
        <h2>Command Frequency</h2>
        <div id="command-table"></div>
        
        <h2>Statistics Types</h2>
        <div id="stats-table"></div>
        
        <h2>VDEV Distribution</h2>
        <div id="vdev-table"></div>
    </div>
    
    <script>
        document.getElementById('timestamp').textContent = new Date().toLocaleString();
        // Add data processing here
    </script>
</body>
</html>
EOF

    print_info "HTML report generated: $html_file"
}

# Generate CSV report
generate_csv_report() {
    local data_file="$1"
    local csv_file="$OUTPUT_DIR/wmi_report_$(date +%Y%m%d_%H%M%S).csv"
    
    print_info "Generating CSV report: $csv_file"
    
    if command -v jq &> /dev/null; then
        echo "timestamp,command,command_id,vdev,pdev,stats_type,stats_id,peer_mac" > "$csv_file"
        jq -r '[.timestamp, .wmi_cmd, .wmi_cmd_id, .wmi_vdev, .wmi_pdev, .wmi_stats, .wmi_stats_id, .wmi_peer] | @csv' "$data_file" >> "$csv_file" 2>/dev/null
        print_info "CSV report generated: $csv_file"
    else
        print_error "CSV generation requires jq"
    fi
}

# Main analysis function
run_analysis() {
    local data_file="$1"
    
    generate_summary "$data_file"
    analyze_command_frequency "$data_file"
    analyze_stats_types "$data_file"
    analyze_vdev_distribution "$data_file"
    analyze_temporal_patterns "$data_file"
    analyze_peer_macs "$data_file"
    
    echo ""
    print_header "Analysis Complete"
    print_info "Results saved to: $OUTPUT_DIR"
    
    # Generate additional formats if requested
    case "$OUTPUT_FORMAT" in
        html)
            generate_html_report "$data_file"
            ;;
        csv)
            generate_csv_report "$data_file"
            ;;
        text)
            # Already done
            ;;
    esac
}

# Main script
main() {
    parse_args "$@"
    check_requirements
    
    local data_file
    
    if [ -n "$INPUT_FILE" ]; then
        if [ ! -f "$INPUT_FILE" ]; then
            print_error "Input file not found: $INPUT_FILE"
            exit 1
        fi
        data_file="$INPUT_FILE"
        print_info "Analyzing existing file: $data_file"
    else
        data_file=$(capture_events)
    fi
    
    run_analysis "$data_file"
}

main "$@"
