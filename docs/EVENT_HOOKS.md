# Event Hooks System

## Overview

The event hooks system allows you to execute custom scripts when specific network events occur. This enables automated responses to network changes, integration with external systems, and custom event processing workflows.

## Features

- **Script Execution**: Execute shell scripts or commands when events match conditions
- **Event Filtering**: Use filter expressions to trigger hooks only for specific events
- **Timeout Protection**: Configurable timeouts prevent hung scripts from blocking monitoring
- **Async Execution**: Run hooks asynchronously to avoid blocking event processing
- **Output Capture**: Optionally capture script stdout/stderr for logging
- **Statistics Tracking**: Monitor hook execution counts, success rates, and timing
- **Enable/Disable**: Dynamically enable or disable hooks without restarting

## Configuration

Event hooks are configured in the `integration.hooks` section of the YAML configuration file:

```yaml
nlmon:
  integration:
    hooks:
      - name: "interface_down_alert"
        script: "/usr/local/bin/alert-interface-down.sh"
        condition: "message_type == 17"  # RTM_DELLINK
        timeout_ms: 5000
        enabled: true
        async: true
```

### Configuration Parameters

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `name` | string | Yes | Unique name for the hook |
| `script` | string | Yes | Path to script or command to execute |
| `condition` | string | No | Filter expression (empty = match all events) |
| `timeout_ms` | integer | No | Execution timeout in milliseconds (default: 30000) |
| `enabled` | boolean | No | Whether hook is enabled (default: true) |
| `async` | boolean | No | Execute asynchronously (default: false) |

## Filter Expressions

Hooks can use filter expressions to match specific events. The filter language supports:

### Field References

- `interface` - Interface name (e.g., "eth0", "veth123")
- `message_type` - Netlink message type (e.g., 16 for RTM_NEWLINK)
- `event_type` - Event type identifier
- `namespace` - Network namespace name
- `timestamp` - Event timestamp (milliseconds since epoch)
- `sequence` - Event sequence number

### Operators

- **Comparison**: `==`, `!=`, `<`, `>`, `<=`, `>=`
- **Pattern Matching**: `=~` (regex match), `!~` (regex not match)
- **Logical**: `AND`, `OR`, `NOT`
- **Set Membership**: `IN`

### Examples

```yaml
# Match interface down events
condition: "message_type == 17"

# Match VETH interfaces
condition: "interface =~ 'veth.*'"

# Match specific interface
condition: "interface == 'eth0'"

# Complex condition
condition: "(interface =~ 'veth.*' AND message_type == 16) OR event_type == 1"

# Match any of multiple message types
condition: "message_type IN [16, 17, 20, 21]"
```

## Environment Variables

When a hook script is executed, the following environment variables are available:

| Variable | Description | Example |
|----------|-------------|---------|
| `NLMON_TIMESTAMP` | Event timestamp (ms since epoch) | `1234567890000` |
| `NLMON_SEQUENCE` | Event sequence number | `12345` |
| `NLMON_EVENT_TYPE` | Event type identifier | `1` |
| `NLMON_MESSAGE_TYPE` | Netlink message type | `16` |
| `NLMON_INTERFACE` | Interface name (if applicable) | `eth0` |
| `PATH` | Standard PATH for script execution | `/usr/local/bin:/usr/bin:/bin` |

## Example Hook Scripts

### Alert on Interface Down

```bash
#!/bin/bash
# /usr/local/bin/alert-interface-down.sh

INTERFACE="${NLMON_INTERFACE}"
TIMESTAMP="${NLMON_TIMESTAMP}"

# Convert timestamp to human-readable format
DATE=$(date -d "@$((TIMESTAMP/1000))" '+%Y-%m-%d %H:%M:%S')

# Log the event
logger -t nlmon "Interface $INTERFACE went down at $DATE"

# Send alert via webhook
curl -X POST https://alerts.example.com/webhook \
  -H "Content-Type: application/json" \
  -d "{\"interface\":\"$INTERFACE\",\"event\":\"down\",\"timestamp\":\"$DATE\"}"

# Send email alert
echo "Interface $INTERFACE went down at $DATE" | \
  mail -s "Network Alert: Interface Down" admin@example.com
```

### Container Event Monitor

```bash
#!/bin/bash
# /usr/local/bin/monitor-container.sh

INTERFACE="${NLMON_INTERFACE}"

# Check if this is a VETH interface
if [[ ! "$INTERFACE" =~ ^veth ]]; then
  exit 0
fi

# Try to find associated container
CONTAINER_ID=$(docker ps --filter "network=container:$INTERFACE" --format "{{.ID}}" 2>/dev/null)

if [ -n "$CONTAINER_ID" ]; then
  CONTAINER_NAME=$(docker inspect --format '{{.Name}}' "$CONTAINER_ID" | sed 's/^\///')
  logger -t nlmon "Container event: $CONTAINER_NAME ($CONTAINER_ID) on $INTERFACE"
  
  # Log to custom file
  echo "$(date '+%Y-%m-%d %H:%M:%S') - Container: $CONTAINER_NAME, Interface: $INTERFACE" \
    >> /var/log/nlmon/container-events.log
fi
```

### Custom Event Processor (Python)

```python
#!/usr/bin/env python3
# /opt/nlmon/scripts/process-event.py

import os
import sys
import json
import requests
from datetime import datetime

def main():
    # Read event data from environment
    event = {
        'timestamp': int(os.environ.get('NLMON_TIMESTAMP', 0)),
        'sequence': int(os.environ.get('NLMON_SEQUENCE', 0)),
        'event_type': int(os.environ.get('NLMON_EVENT_TYPE', 0)),
        'message_type': int(os.environ.get('NLMON_MESSAGE_TYPE', 0)),
        'interface': os.environ.get('NLMON_INTERFACE', ''),
    }
    
    # Convert timestamp to datetime
    dt = datetime.fromtimestamp(event['timestamp'] / 1000)
    event['datetime'] = dt.isoformat()
    
    # Process event
    print(f"Processing event: {json.dumps(event, indent=2)}")
    
    # Send to external system
    try:
        response = requests.post(
            'https://api.example.com/events',
            json=event,
            timeout=5
        )
        print(f"Sent to API: {response.status_code}")
    except Exception as e:
        print(f"Error sending to API: {e}", file=sys.stderr)
        return 1
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
```

## Hook Execution Modes

### Synchronous Execution

When `async: false`, the hook executes synchronously:

- Event processing waits for the hook to complete
- Useful for critical actions that must complete before continuing
- Can impact event processing throughput if hooks are slow
- Timeout protection prevents indefinite blocking

```yaml
- name: "critical_action"
  script: "/usr/local/bin/critical.sh"
  timeout_ms: 5000
  async: false  # Wait for completion
```

### Asynchronous Execution

When `async: true`, the hook executes asynchronously:

- Event processing continues immediately
- Hook runs in background thread
- Better for non-critical actions or slow operations
- Concurrent execution limit prevents resource exhaustion

```yaml
- name: "background_task"
  script: "/usr/local/bin/background.sh"
  timeout_ms: 30000
  async: true  # Run in background
```

## Timeout Handling

Hooks have configurable timeouts to prevent hung scripts:

- Default timeout: 30 seconds
- Timeout triggers SIGKILL to script process
- Timeout events are tracked in statistics
- Set appropriate timeouts based on expected script duration

```yaml
- name: "quick_action"
  script: "/usr/local/bin/quick.sh"
  timeout_ms: 1000  # 1 second timeout

- name: "slow_action"
  script: "/usr/local/bin/slow.sh"
  timeout_ms: 60000  # 1 minute timeout
```

## Statistics and Monitoring

Hook execution statistics are tracked and can be queried:

- **Executions**: Total number of times hook was triggered
- **Successes**: Number of successful executions (exit code 0)
- **Failures**: Number of failed executions (non-zero exit code)
- **Timeouts**: Number of executions that exceeded timeout
- **Timing**: Min, max, average, and total execution time

Statistics can be accessed via the web API or CLI interface.

## Best Practices

### 1. Use Specific Conditions

Avoid matching all events unless necessary:

```yaml
# Good - specific condition
condition: "interface == 'eth0' AND message_type == 17"

# Avoid - matches everything
condition: ""
```

### 2. Set Appropriate Timeouts

Match timeout to expected script duration:

```yaml
# Quick notification
timeout_ms: 5000

# Database operation
timeout_ms: 30000

# External API call
timeout_ms: 10000
```

### 3. Use Async for Non-Critical Actions

Use async execution for actions that don't need to complete immediately:

```yaml
# Critical - must complete before continuing
- name: "update_firewall"
  async: false

# Non-critical - can run in background
- name: "send_notification"
  async: true
```

### 4. Handle Errors Gracefully

Scripts should handle errors and exit with appropriate codes:

```bash
#!/bin/bash
set -e  # Exit on error

# Your script logic here

# Explicit success
exit 0
```

### 5. Log Important Events

Use logging for debugging and auditing:

```bash
#!/bin/bash
logger -t nlmon-hook "Processing event for $NLMON_INTERFACE"

# Your script logic

logger -t nlmon-hook "Completed processing for $NLMON_INTERFACE"
```

### 6. Test Hooks Independently

Test hook scripts independently before deploying:

```bash
# Set environment variables
export NLMON_INTERFACE="eth0"
export NLMON_MESSAGE_TYPE="17"
export NLMON_TIMESTAMP="1234567890000"

# Run script
/usr/local/bin/your-hook.sh
```

### 7. Limit Concurrent Executions

The hook manager limits concurrent executions to prevent resource exhaustion. Consider this when designing hooks:

- Default limit: 10 concurrent executions
- Async hooks count toward this limit
- Hooks wait if limit is reached

## Security Considerations

### Script Permissions

- Hook scripts should have appropriate file permissions
- Recommended: owned by root, executable by nlmon user
- Avoid world-writable scripts

```bash
chown root:nlmon /usr/local/bin/hook-script.sh
chmod 750 /usr/local/bin/hook-script.sh
```

### Input Validation

- Environment variables come from network events
- Validate and sanitize inputs in scripts
- Avoid shell injection vulnerabilities

```bash
# Bad - vulnerable to injection
eval "echo $NLMON_INTERFACE"

# Good - safe variable usage
echo "${NLMON_INTERFACE}"
```

### Privilege Separation

- Run nlmon with minimal required privileges
- Use sudo for specific privileged operations if needed
- Consider using dedicated service accounts

## Troubleshooting

### Hook Not Executing

1. Check if hook is enabled: `enabled: true`
2. Verify condition matches events
3. Check script path and permissions
4. Review nlmon logs for errors

### Hook Timing Out

1. Increase timeout value
2. Optimize script performance
3. Consider using async execution
4. Check for blocking operations

### High Resource Usage

1. Reduce number of hooks
2. Use more specific conditions
3. Optimize script performance
4. Increase async execution limit

### Script Errors

1. Test script independently
2. Check script exit codes
3. Review script logs
4. Verify environment variables

## API Reference

See `include/event_hooks.h` for the complete C API documentation.

### Key Functions

- `hook_manager_create()` - Create hook manager
- `hook_manager_register()` - Register a hook
- `hook_manager_execute()` - Execute hooks for an event
- `hook_manager_get_stats()` - Get hook statistics
- `hook_manager_enable/disable()` - Enable/disable hooks

## Examples

See `docs/examples/event-hooks.yaml` for complete configuration examples.

## Related Documentation

- [Filter Language Reference](FILTER_LANGUAGE.md)
- [Configuration Guide](README.md)
- [API Documentation](REST_API.md)
