#!/bin/bash
# valgrind_test.sh - Run memory leak detection with valgrind

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
echo -e "${BOLD}  Memory Leak Detection with Valgrind${NC}"
echo -e "${BOLD}======================================${NC}"
echo ""

# Find test executables
TEST_BINARIES=$(find "$PROJECT_ROOT" -maxdepth 1 -name "test_unit_*" -o -name "test_integration_*" 2>/dev/null | head -5)

if [ -z "$TEST_BINARIES" ]; then
    echo -e "${YELLOW}No test binaries found.${NC}"
    echo "Please build tests first."
    exit 1
fi

TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Run valgrind on each test
for test_bin in $TEST_BINARIES; do
    test_name=$(basename "$test_bin")
    echo -e "${BLUE}Testing: $test_name${NC}"
    echo "--------------------------------------"
    
    # Run valgrind
    valgrind \
        --leak-check=full \
        --show-leak-kinds=all \
        --track-origins=yes \
        --error-exitcode=1 \
        --suppressions="$SCRIPT_DIR/valgrind.supp" \
        "$test_bin" > /dev/null 2>&1
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ No memory leaks detected${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ Memory leaks detected${NC}"
        echo "Run manually for details:"
        echo "  valgrind --leak-check=full $test_bin"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo ""
done

# Print summary
echo -e "${BOLD}======================================${NC}"
echo -e "${BOLD}  Memory Test Summary${NC}"
echo -e "${BOLD}======================================${NC}"
echo "Total tests: $TOTAL_TESTS"
echo -e "${GREEN}Passed: $PASSED_TESTS${NC}"

if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "${RED}Failed: $FAILED_TESTS${NC}"
    exit 1
else
    echo "Failed: $FAILED_TESTS"
    echo ""
    echo -e "${GREEN}${BOLD}All memory tests passed!${NC}"
    exit 0
fi
