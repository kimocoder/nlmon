#!/bin/bash
# profile_memory.sh - Profile memory usage with massif

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

# Check if valgrind is installed
if ! command -v valgrind &> /dev/null; then
    echo -e "${RED}Error: valgrind is not installed${NC}"
    echo "Install with: sudo apt-get install valgrind"
    exit 1
fi

echo -e "${BOLD}======================================${NC}"
echo -e "${BOLD}  Memory Profiling with Massif${NC}"
echo -e "${BOLD}======================================${NC}"
echo ""

# Check for test binary
if [ $# -lt 1 ]; then
    echo "Usage: $0 <test_binary> [args...]"
    echo ""
    echo "Example:"
    echo "  $0 test_stability 30"
    exit 1
fi

TEST_BIN="$1"
shift

if [ ! -f "$PROJECT_ROOT/$TEST_BIN" ]; then
    echo -e "${RED}Error: Test binary not found: $TEST_BIN${NC}"
    exit 1
fi

OUTPUT_FILE="massif.out.$$"

echo -e "${BLUE}Profiling: $TEST_BIN${NC}"
echo "Output file: $OUTPUT_FILE"
echo ""

# Run massif
valgrind \
    --tool=massif \
    --massif-out-file="$OUTPUT_FILE" \
    --time-unit=ms \
    --detailed-freq=1 \
    --max-snapshots=100 \
    "$PROJECT_ROOT/$TEST_BIN" "$@"

echo ""
echo -e "${GREEN}Profiling complete!${NC}"
echo ""

# Check if ms_print is available
if command -v ms_print &> /dev/null; then
    echo "Memory profile summary:"
    echo "--------------------------------------"
    ms_print "$OUTPUT_FILE" | head -50
    echo ""
    echo "Full report saved to: $OUTPUT_FILE"
    echo "View with: ms_print $OUTPUT_FILE"
else
    echo "Install valgrind-dbg for ms_print tool"
    echo "Report saved to: $OUTPUT_FILE"
fi
