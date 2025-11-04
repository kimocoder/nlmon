#!/bin/bash
# run_unit_tests.sh - Run all unit tests and generate report

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
UNIT_TEST_DIR="$SCRIPT_DIR/unit"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Test results
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

echo -e "${BOLD}======================================${NC}"
echo -e "${BOLD}  nlmon Unit Test Suite${NC}"
echo -e "${BOLD}======================================${NC}"
echo ""

# Find all test executables
TEST_BINARIES=$(find "$PROJECT_ROOT" -maxdepth 1 -name "test_unit_*" -type f -executable 2>/dev/null || true)

if [ -z "$TEST_BINARIES" ]; then
    echo -e "${YELLOW}No unit test binaries found.${NC}"
    echo "Please build tests first with: make unit-tests"
    exit 1
fi

# Run each test
for test_bin in $TEST_BINARIES; do
    test_name=$(basename "$test_bin")
    echo -e "${BLUE}Running: $test_name${NC}"
    echo "--------------------------------------"
    
    if "$test_bin"; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
        echo -e "${GREEN}✓ $test_name PASSED${NC}"
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
        echo -e "${RED}✗ $test_name FAILED${NC}"
    fi
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo ""
done

# Print summary
echo -e "${BOLD}======================================${NC}"
echo -e "${BOLD}  Test Summary${NC}"
echo -e "${BOLD}======================================${NC}"
echo "Total test suites: $TOTAL_TESTS"
echo -e "${GREEN}Passed: $PASSED_TESTS${NC}"

if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "${RED}Failed: $FAILED_TESTS${NC}"
    exit 1
else
    echo "Failed: $FAILED_TESTS"
    echo ""
    echo -e "${GREEN}${BOLD}All tests passed!${NC}"
    exit 0
fi
