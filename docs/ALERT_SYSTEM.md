# Alert System Documentation

## Overview

The nlmon alert system provides automated monitoring and notification capabilities for network events. It allows you to define rules that trigger actions when specific network conditions occur, with support for rate limiting, suppression, and acknowledgment workflows.

## Features

- **Rule-based alerting**: Define conditions using filter expressions
- **Multiple action types**: Execute scripts, write to logs, or send webhooks
- **Rate limiting**: Prevent alert storms with configurable rate limits
- **Alert suppression**: Temporarily suppress alerts after triggering
- **State management**: Track alert lifecycle (active, acknowledged, resolved)
- **Alert history**: Maintain history of triggered alerts
- **Statistics tracking**: Monitor alert system performance

## Alert Rule Configuration

### Basic Structure

```yaml
alerts:
  - name: "interface_down"
    condition: "message_type == 17 AND interface =~ 'eth.*'"
    severity: "warning"
    action:
      type: "log"
      log_file: "/var/log/nlmon/alerts.log"
      append: true
    enabled: true
```

### Alert Severity Levels

- `info`: Informational alerts
- `warning`: Warning conditions
- `error`: Error conditions
- `critical`: Critical conditions requiring immediate attention

### Condition Expressions

Alert conditions use the same filter expression language as event filtering:

```yaml
# Simple conditions
condition: "interface == 'eth0'"
condition: "message_type == 16"

# Pattern matching
condition: "interface =~ 'veth.*'"

# Logical operators
condition: "interface == 'eth0' AND message_type == 16"
condition: "interface =~ 'eth.*' OR interface =~ 'wlan.*'"
condition: "NOT (interface == 'lo')"

# Complex expressions
condition: "(interface =~ 'veth.*' AND message_type IN [16, 17]) OR severity == 'critical'"
```

## Action Types

### 1. Execute Script (exec)

Execute a shell script when the alert triggers.

```yaml
action:
  type: "exec"
  script: "/usr/local/bin/alert-handler.sh"
  timeout_ms: 30000
```

The script receives event information via environment variables:
- `NLMON_ALERT_NAME`: Name of the triggered alert
- `NLMON_TIMESTAMP`: Event timestamp
- `NLMON_SEQUENCE`: Event sequence number
- `NLMON_EVENT_TYPE`: Event type
- `NLMON_MESSAGE_TYPE`: Netlink message type
- `NLMON_INTERFACE`: Interface name

Example script:
```bash
#!/bin/bash
echo "Alert: $NLMON_ALERT_NAME"
echo "Interface: $NLMON_INTERFACE"
echo "Message Type: $NLMON_MESSAGE_TYPE"
# Send notification, update monitoring system, etc.
```

### 2. Write to Log (log)

Write alert information to a log file.

```yaml
action:
  type: "log"
  log_file: "/var/log/nlmon/alerts.log"
  append: true
```

Log format:
```
[2025-11-04 12:34:56] [WARNING] Alert 'interface_down' triggered by event seq=12345 type=17 interface=eth0
```

### 3. HTTP Webhook (webhook)

Send alert data to an HTTP endpoint.

```yaml
action:
  type: "webhook"
  url: "https://monitoring.example.com/webhook"
  method: "POST"
  timeout_ms: 10000
```

Webhook payload (JSON):
```json
{
  "alert_name": "interface_down",
  "severity": "warning",
  "timestamp": 1730728496,
  "event": {
    "sequence": 12345,
    "type": 1,
    "message_type": 17,
    "interface": "eth0"
  }
}
```

## Rate Limiting

Prevent alert storms by limiting how often an alert can trigger:

```yaml
alerts:
  - name: "arp_flood"
    condition: "message_type == 28"
    rate_limit_count: 5
    rate_limit_window_s: 60
    action:
      type: "log"
      log_file: "/var/log/nlmon/security.log"
```

This configuration allows maximum 5 alerts within a 60-second window. Additional triggers are counted as rate-limited.

## Alert Suppression

Automatically suppress alerts for a period after triggering:

```yaml
alerts:
  - name: "interface_flapping"
    condition: "message_type IN [16, 17]"
    suppress_duration_s: 300
    action:
      type: "exec"
      script: "/usr/local/bin/notify-admin.sh"
```

After the alert triggers, it will be suppressed for 300 seconds (5 minutes) to prevent repeated notifications for the same issue.

## Alert Management

### Programmatic API

```c
#include "alert_manager.h"

/* Create alert manager */
struct alert_manager *am = alert_manager_create(32, 1000);

/* Define alert rule */
struct alert_rule rule = {
    .name = "interface_down",
    .condition = "message_type == 17",
    .severity = ALERT_SEVERITY_WARNING,
    .action = {
        .type = ALERT_ACTION_LOG,
        .params.log = {
            .log_file = "/var/log/nlmon/alerts.log",
            .append = true
        }
    },
    .enabled = true,
    .rate_limit_count = 0,
    .rate_limit_window_s = 0,
    .suppress_duration_s = 0
};

/* Add rule */
int rule_id = alert_manager_add_rule(am, &rule);

/* Evaluate event */
alert_manager_evaluate(am, event);

/* Get active alerts */
struct alert_instance alerts[10];
size_t count = alert_manager_get_active(am, alerts, 10);

/* Acknowledge alert */
alert_manager_acknowledge(am, alerts[0].id, "admin");

/* Resolve alert */
alert_manager_resolve(am, alerts[0].id);

/* Get statistics */
struct alert_stats stats;
alert_manager_get_stats(am, &stats);

/* Cleanup */
alert_manager_destroy(am);
```

### Alert States

1. **INACTIVE**: Alert has been resolved
2. **ACTIVE**: Alert is currently active
3. **ACKNOWLEDGED**: Alert has been acknowledged but not resolved
4. **SUPPRESSED**: Alert rule is temporarily suppressed

### Alert Lifecycle

```
[Trigger] → ACTIVE → [Acknowledge] → ACKNOWLEDGED → [Resolve] → INACTIVE
                  ↘ [Resolve] → INACTIVE
```

## Example Configurations

### Security Monitoring

```yaml
alerts:
  - name: "promiscuous_mode"
    condition: "flags CONTAINS 'PROMISC'"
    severity: "critical"
    action:
      type: "webhook"
      url: "https://security.example.com/alert"
    enabled: true

  - name: "arp_flood"
    condition: "message_type == 28"
    severity: "error"
    rate_limit_count: 10
    rate_limit_window_s: 60
    action:
      type: "exec"
      script: "/usr/local/bin/block-arp-flood.sh"
    enabled: true
```

### Container Monitoring

```yaml
alerts:
  - name: "container_network_down"
    condition: "interface =~ 'veth.*' AND message_type == 17"
    severity: "warning"
    suppress_duration_s: 60
    action:
      type: "log"
      log_file: "/var/log/nlmon/container-alerts.log"
    enabled: true

  - name: "pod_network_issue"
    condition: "namespace != 'default' AND message_type == 17"
    severity: "error"
    action:
      type: "webhook"
      url: "https://k8s-monitor.example.com/webhook"
    enabled: true
```

### Production Server Monitoring

```yaml
alerts:
  - name: "critical_interface_down"
    condition: "interface IN ['eth0', 'eth1'] AND message_type == 17"
    severity: "critical"
    action:
      type: "exec"
      script: "/usr/local/bin/page-oncall.sh"
      timeout_ms: 5000
    enabled: true

  - name: "route_change"
    condition: "message_type IN [24, 25]"
    severity: "info"
    rate_limit_count: 5
    rate_limit_window_s: 300
    action:
      type: "log"
      log_file: "/var/log/nlmon/route-changes.log"
    enabled: true
```

## Statistics and Monitoring

### Available Statistics

```c
struct alert_stats {
    unsigned long total_triggered;      /* Total alerts triggered */
    unsigned long total_executed;       /* Actions successfully executed */
    unsigned long total_failed;         /* Action execution failures */
    unsigned long total_suppressed;     /* Alerts suppressed */
    unsigned long total_rate_limited;   /* Alerts rate limited */
    unsigned long active_count;         /* Currently active alerts */
    unsigned long acknowledged_count;   /* Acknowledged alerts */
};
```

### Monitoring Alert System Health

```bash
# Check alert statistics via API
curl http://localhost:8080/api/alerts/stats

# View active alerts
curl http://localhost:8080/api/alerts/active

# View alert history
curl http://localhost:8080/api/alerts/history?since=1730700000
```

## Best Practices

### 1. Use Appropriate Severity Levels

- **INFO**: Routine events that don't require action
- **WARNING**: Conditions that should be investigated
- **ERROR**: Problems that need attention
- **CRITICAL**: Urgent issues requiring immediate response

### 2. Implement Rate Limiting

Always use rate limiting for alerts that could trigger frequently:

```yaml
rate_limit_count: 5
rate_limit_window_s: 60
```

### 3. Use Suppression for Transient Issues

For issues that may resolve themselves, use suppression:

```yaml
suppress_duration_s: 300  # 5 minutes
```

### 4. Test Alert Actions

Test your alert scripts and webhooks before deploying:

```bash
# Test script execution
NLMON_ALERT_NAME="test" \
NLMON_INTERFACE="eth0" \
/usr/local/bin/alert-handler.sh

# Test webhook
curl -X POST https://monitoring.example.com/webhook \
  -H "Content-Type: application/json" \
  -d '{"alert_name":"test","severity":"info"}'
```

### 5. Monitor Alert System Performance

Regularly check alert statistics to ensure the system is working correctly:

```c
struct alert_stats stats;
alert_manager_get_stats(am, &stats);

if (stats.total_failed > stats.total_executed * 0.1) {
    /* More than 10% failure rate - investigate */
}
```

### 6. Use Meaningful Alert Names

Choose descriptive names that clearly indicate the condition:

```yaml
# Good
name: "production_interface_down"
name: "container_network_error"

# Bad
name: "alert1"
name: "test"
```

### 7. Document Alert Responses

Include documentation in alert scripts:

```bash
#!/bin/bash
# Alert: Interface Down Handler
# Purpose: Notify operations team when critical interface goes down
# Escalation: Page on-call engineer if eth0 or eth1
# Contact: ops-team@example.com
```

## Troubleshooting

### Alerts Not Triggering

1. Check if rule is enabled:
   ```c
   struct alert_rule rule;
   alert_manager_get_rule(am, rule_id, &rule);
   if (!rule.enabled) {
       alert_manager_enable_rule(am, rule_id);
   }
   ```

2. Verify condition syntax:
   ```yaml
   # Test condition with simple event
   condition: "message_type == 16"
   ```

3. Check rate limiting:
   ```c
   struct alert_stats stats;
   alert_manager_get_stats(am, &stats);
   printf("Rate limited: %lu\n", stats.total_rate_limited);
   ```

### Action Execution Failures

1. Check script permissions:
   ```bash
   chmod +x /usr/local/bin/alert-handler.sh
   ```

2. Verify script path:
   ```bash
   which alert-handler.sh
   ```

3. Test script manually:
   ```bash
   /usr/local/bin/alert-handler.sh
   ```

4. Check timeout settings:
   ```yaml
   action:
     type: "exec"
     timeout_ms: 30000  # Increase if needed
   ```

### High Alert Volume

1. Implement rate limiting:
   ```yaml
   rate_limit_count: 10
   rate_limit_window_s: 60
   ```

2. Use suppression:
   ```yaml
   suppress_duration_s: 300
   ```

3. Refine conditions:
   ```yaml
   # Too broad
   condition: "message_type == 16"
   
   # More specific
   condition: "message_type == 16 AND interface =~ 'eth.*'"
   ```

## Performance Considerations

- Alert evaluation is performed for each event, so keep conditions simple
- Use rate limiting to prevent excessive action executions
- Webhook actions are non-blocking but have timeouts
- Script execution is synchronous - use short-running scripts
- Alert history is stored in memory with configurable size limit

## Security Considerations

- Alert scripts run with nlmon process privileges
- Validate webhook URLs to prevent SSRF attacks
- Sanitize event data before passing to scripts
- Use HTTPS for webhook endpoints
- Implement authentication for webhook receivers
- Restrict file permissions on alert log files

## Integration Examples

### Slack Webhook

```yaml
alerts:
  - name: "critical_alert"
    condition: "severity == 'critical'"
    action:
      type: "webhook"
      url: "https://hooks.slack.com/services/YOUR/WEBHOOK/URL"
```

### PagerDuty Integration

```bash
#!/bin/bash
# /usr/local/bin/pagerduty-alert.sh

curl -X POST https://events.pagerduty.com/v2/enqueue \
  -H "Content-Type: application/json" \
  -d "{
    \"routing_key\": \"$PAGERDUTY_KEY\",
    \"event_action\": \"trigger\",
    \"payload\": {
      \"summary\": \"$NLMON_ALERT_NAME\",
      \"severity\": \"critical\",
      \"source\": \"nlmon\",
      \"custom_details\": {
        \"interface\": \"$NLMON_INTERFACE\",
        \"message_type\": \"$NLMON_MESSAGE_TYPE\"
      }
    }
  }"
```

### Email Notification

```bash
#!/bin/bash
# /usr/local/bin/email-alert.sh

echo "Alert: $NLMON_ALERT_NAME
Interface: $NLMON_INTERFACE
Message Type: $NLMON_MESSAGE_TYPE
Timestamp: $(date -d @$NLMON_TIMESTAMP)" | \
mail -s "nlmon Alert: $NLMON_ALERT_NAME" ops@example.com
```

## API Reference

See `include/alert_manager.h` for complete API documentation.

### Key Functions

- `alert_manager_create()` - Create alert manager
- `alert_manager_add_rule()` - Add alert rule
- `alert_manager_evaluate()` - Evaluate event against rules
- `alert_manager_acknowledge()` - Acknowledge alert
- `alert_manager_resolve()` - Resolve alert
- `alert_manager_get_active()` - Get active alerts
- `alert_manager_get_history()` - Get alert history
- `alert_manager_get_stats()` - Get statistics

## See Also

- [Event Hooks Documentation](EVENT_HOOKS.md)
- [Filter Expression Language](ARCHITECTURE.md#filtering-system)
- [REST API Documentation](REST_API.md)
- [Configuration Guide](nlmon.conf.5)
