# nlmon REST API Documentation

## Overview

The nlmon REST API provides programmatic access to network event data, statistics, configuration, and management functions. The API is available when the web dashboard is enabled.

## Base URL

```
http://localhost:8080/api
```

For HTTPS (when TLS is enabled):
```
https://localhost:8080/api
```

## Authentication

### JWT Authentication

When authentication is enabled, requests must include a JWT token in the Authorization header:

```http
Authorization: Bearer <token>
```

### Obtaining a Token

```http
POST /api/auth/login
Content-Type: application/json

{
  "username": "admin",
  "password": "password"
}
```

**Response:**
```json
{
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "expires_in": 3600
}
```

### API Key Authentication

Alternatively, use an API key:

```http
X-API-Key: your-api-key-here
```

## Response Format

All responses are in JSON format.

### Success Response

```json
{
  "status": "success",
  "data": { ... }
}
```

### Error Response

```json
{
  "status": "error",
  "error": {
    "code": "ERROR_CODE",
    "message": "Human-readable error message"
  }
}
```

## API Endpoints

### Events

#### List Events

Get a list of recent network events.

```http
GET /api/events
```

**Query Parameters:**

| Parameter | Type | Description | Default |
|-----------|------|-------------|---------|
| limit | integer | Maximum number of events to return | 100 |
| offset | integer | Number of events to skip | 0 |
| since | integer | Unix timestamp - events after this time | - |
| until | integer | Unix timestamp - events before this time | - |
| interface | string | Filter by interface name | - |
| type | string | Filter by event type | - |
| namespace | string | Filter by network namespace | - |

**Example Request:**

```http
GET /api/events?limit=50&interface=eth0&type=RTM_NEWLINK
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "events": [
      {
        "id": 12345,
        "timestamp": 1699104896789,
        "sequence": 12345,
        "type": "link_change",
        "message_type": "RTM_NEWLINK",
        "interface": "eth0",
        "namespace": "default",
        "details": {
          "state": "UP",
          "flags": ["UP", "RUNNING", "BROADCAST"],
          "mtu": 1500
        },
        "correlation_id": "abc123"
      }
    ],
    "total": 1234,
    "limit": 50,
    "offset": 0
  }
}
```

#### Get Event by ID

Get details of a specific event.

```http
GET /api/events/:id
```

**Example Request:**

```http
GET /api/events/12345
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "id": 12345,
    "timestamp": 1699104896789,
    "sequence": 12345,
    "type": "link_change",
    "message_type": "RTM_NEWLINK",
    "interface": "eth0",
    "namespace": "default",
    "details": {
      "state": "UP",
      "flags": ["UP", "RUNNING", "BROADCAST"],
      "mtu": 1500,
      "mac_address": "00:11:22:33:44:55"
    },
    "correlation_id": "abc123",
    "related_events": [12344, 12346]
  }
}
```

### Statistics

#### Get Statistics

Get aggregated statistics about network events.

```http
GET /api/stats
```

**Query Parameters:**

| Parameter | Type | Description | Default |
|-----------|------|-------------|---------|
| period | string | Time period: 1h, 24h, 7d, 30d | 24h |
| group_by | string | Group by: type, interface, namespace | type |

**Example Request:**

```http
GET /api/stats?period=24h&group_by=type
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "period": "24h",
    "total_events": 12345,
    "by_type": {
      "link": 5000,
      "route": 3000,
      "addr": 2000,
      "neigh": 1500,
      "rule": 845
    },
    "by_interface": {
      "eth0": 6000,
      "eth1": 4000,
      "lo": 2345
    },
    "event_rate": {
      "current": 10.5,
      "average": 8.7,
      "peak": 25.3
    },
    "timestamp": 1699104896
  }
}
```

#### Get System Statistics

Get nlmon system performance statistics.

```http
GET /api/stats/system
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "uptime": 86400,
    "memory": {
      "rss": 52428800,
      "vms": 104857600,
      "peak_rss": 62914560
    },
    "cpu": {
      "usage_percent": 5.2,
      "user_time": 120.5,
      "system_time": 45.3
    },
    "events": {
      "processed": 1234567,
      "dropped": 123,
      "rate_limited": 456
    },
    "queue": {
      "depth": 42,
      "capacity": 10000,
      "overflow_count": 5
    }
  }
}
```

### Configuration

#### Get Configuration

Get current configuration.

```http
GET /api/config
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "core": {
      "buffer_size": "320KB",
      "max_events": 10000,
      "rate_limit": 1000,
      "worker_threads": 4
    },
    "monitoring": {
      "protocols": ["NETLINK_ROUTE"],
      "interfaces": {
        "include": ["*"],
        "exclude": []
      }
    },
    "output": {
      "console": {
        "enabled": true,
        "format": "text"
      }
    }
  }
}
```

#### Update Configuration

Update configuration (requires admin role).

```http
PUT /api/config
Content-Type: application/json

{
  "core": {
    "rate_limit": 2000
  }
}
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "message": "Configuration updated successfully",
    "reload_required": false
  }
}
```

#### Reload Configuration

Reload configuration from file.

```http
POST /api/config/reload
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "message": "Configuration reloaded successfully"
  }
}
```

### Filters

#### List Filters

Get all configured filters.

```http
GET /api/filters
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "filters": [
      {
        "id": 1,
        "name": "container_events",
        "expression": "interface =~ 'veth.*'",
        "enabled": true,
        "hit_count": 1234
      },
      {
        "id": 2,
        "name": "link_changes",
        "expression": "message_type IN [RTM_NEWLINK, RTM_DELLINK]",
        "enabled": true,
        "hit_count": 5678
      }
    ]
  }
}
```

#### Create Filter

Create a new filter.

```http
POST /api/filters
Content-Type: application/json

{
  "name": "my_filter",
  "expression": "interface == 'eth0' AND state == 'UP'",
  "enabled": true
}
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "id": 3,
    "name": "my_filter",
    "expression": "interface == 'eth0' AND state == 'UP'",
    "enabled": true
  }
}
```

#### Update Filter

Update an existing filter.

```http
PUT /api/filters/:id
Content-Type: application/json

{
  "enabled": false
}
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "id": 3,
    "name": "my_filter",
    "expression": "interface == 'eth0' AND state == 'UP'",
    "enabled": false
  }
}
```

#### Delete Filter

Delete a filter.

```http
DELETE /api/filters/:id
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "message": "Filter deleted successfully"
  }
}
```

### Alerts

#### List Alerts

Get all alerts.

```http
GET /api/alerts
```

**Query Parameters:**

| Parameter | Type | Description | Default |
|-----------|------|-------------|---------|
| status | string | Filter by status: active, acknowledged, resolved | - |
| severity | string | Filter by severity: critical, high, warning, info | - |
| limit | integer | Maximum number of alerts | 100 |

**Response:**

```json
{
  "status": "success",
  "data": {
    "alerts": [
      {
        "id": 1,
        "name": "interface_down",
        "severity": "critical",
        "status": "active",
        "triggered_at": 1699104896,
        "event_id": 12345,
        "details": {
          "interface": "eth0",
          "message": "Critical interface eth0 went down"
        }
      }
    ],
    "total": 5
  }
}
```

#### Acknowledge Alert

Acknowledge an alert.

```http
POST /api/alerts/:id/acknowledge
Content-Type: application/json

{
  "comment": "Investigating the issue"
}
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "id": 1,
    "status": "acknowledged",
    "acknowledged_at": 1699104900,
    "acknowledged_by": "admin"
  }
}
```

#### Resolve Alert

Mark an alert as resolved.

```http
POST /api/alerts/:id/resolve
Content-Type: application/json

{
  "comment": "Issue fixed"
}
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "id": 1,
    "status": "resolved",
    "resolved_at": 1699105000,
    "resolved_by": "admin"
  }
}
```

### Plugins

#### List Plugins

Get all loaded plugins.

```http
GET /api/plugins
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "plugins": [
      {
        "name": "kubernetes",
        "version": "1.0.0",
        "description": "Kubernetes integration plugin",
        "enabled": true,
        "status": "active",
        "events_processed": 12345
      },
      {
        "name": "docker",
        "version": "1.0.0",
        "description": "Docker integration plugin",
        "enabled": false,
        "status": "disabled"
      }
    ]
  }
}
```

#### Get Plugin Details

Get details of a specific plugin.

```http
GET /api/plugins/:name
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "name": "kubernetes",
    "version": "1.0.0",
    "description": "Kubernetes integration plugin",
    "api_version": 1,
    "enabled": true,
    "status": "active",
    "statistics": {
      "events_processed": 12345,
      "events_filtered": 123,
      "errors": 5
    },
    "configuration": {
      "kubeconfig": "~/.kube/config",
      "cache_ttl": 300
    }
  }
}
```

#### Enable/Disable Plugin

Enable or disable a plugin.

```http
POST /api/plugins/:name/enable
POST /api/plugins/:name/disable
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "name": "kubernetes",
    "enabled": true,
    "message": "Plugin enabled successfully"
  }
}
```

### Export

#### Export Events

Export events in various formats.

```http
POST /api/export
Content-Type: application/json

{
  "format": "json",
  "filter": {
    "since": 1699104000,
    "until": 1699104900,
    "interface": "eth0"
  },
  "options": {
    "pretty": true
  }
}
```

**Query Parameters:**

| Parameter | Type | Description | Default |
|-----------|------|-------------|---------|
| format | string | Export format: json, csv, pcap | json |

**Response:**

Returns the exported data in the requested format.

For JSON:
```json
{
  "events": [
    { ... }
  ],
  "metadata": {
    "exported_at": 1699105000,
    "total_events": 100,
    "filter": { ... }
  }
}
```

### Health

#### Health Check

Check if the API is healthy.

```http
GET /api/health
```

**Response:**

```json
{
  "status": "success",
  "data": {
    "healthy": true,
    "version": "2.0.0",
    "uptime": 86400,
    "components": {
      "netlink": "ok",
      "database": "ok",
      "plugins": "ok"
    }
  }
}
```

## WebSocket API

### Event Streaming

Connect to WebSocket for real-time event streaming.

```
ws://localhost:8080/api/stream
```

#### Connection

```javascript
const ws = new WebSocket('ws://localhost:8080/api/stream');

ws.onopen = () => {
  console.log('Connected');
  
  // Subscribe to events
  ws.send(JSON.stringify({
    type: 'subscribe',
    filters: ['interface =~ "eth.*"'],
    format: 'json'
  }));
};

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log('Event:', data);
};
```

#### Message Types

**Subscribe:**
```json
{
  "type": "subscribe",
  "filters": ["interface == 'eth0'"],
  "format": "json"
}
```

**Unsubscribe:**
```json
{
  "type": "unsubscribe"
}
```

**Event Message:**
```json
{
  "type": "event",
  "data": {
    "timestamp": 1699104896789,
    "interface": "eth0",
    "message_type": "RTM_NEWLINK",
    "details": { ... }
  }
}
```

**Statistics Update:**
```json
{
  "type": "stats",
  "data": {
    "total_events": 12345,
    "event_rate": 10.5
  }
}
```

## Rate Limiting

API requests are rate-limited to prevent abuse.

**Default Limits:**
- 100 requests per minute per IP
- 1000 requests per hour per IP

**Rate Limit Headers:**
```http
X-RateLimit-Limit: 100
X-RateLimit-Remaining: 95
X-RateLimit-Reset: 1699105000
```

**Rate Limit Exceeded:**
```http
HTTP/1.1 429 Too Many Requests
Content-Type: application/json

{
  "status": "error",
  "error": {
    "code": "RATE_LIMIT_EXCEEDED",
    "message": "Rate limit exceeded. Try again in 60 seconds."
  }
}
```

## Error Codes

| Code | HTTP Status | Description |
|------|-------------|-------------|
| INVALID_REQUEST | 400 | Invalid request format or parameters |
| UNAUTHORIZED | 401 | Authentication required or failed |
| FORBIDDEN | 403 | Insufficient permissions |
| NOT_FOUND | 404 | Resource not found |
| CONFLICT | 409 | Resource conflict (e.g., duplicate name) |
| RATE_LIMIT_EXCEEDED | 429 | Too many requests |
| INTERNAL_ERROR | 500 | Internal server error |
| SERVICE_UNAVAILABLE | 503 | Service temporarily unavailable |

## Examples

### Python

```python
import requests

# Get events
response = requests.get('http://localhost:8080/api/events', params={
    'limit': 50,
    'interface': 'eth0'
})

events = response.json()['data']['events']
for event in events:
    print(f"{event['timestamp']}: {event['interface']} - {event['type']}")
```

### cURL

```bash
# Get events
curl -X GET 'http://localhost:8080/api/events?limit=50&interface=eth0'

# Create filter
curl -X POST 'http://localhost:8080/api/filters' \
  -H 'Content-Type: application/json' \
  -d '{
    "name": "my_filter",
    "expression": "interface == \"eth0\"",
    "enabled": true
  }'

# Get statistics
curl -X GET 'http://localhost:8080/api/stats?period=24h'
```

### JavaScript

```javascript
// Fetch events
fetch('http://localhost:8080/api/events?limit=50')
  .then(response => response.json())
  .then(data => {
    console.log('Events:', data.data.events);
  });

// WebSocket streaming
const ws = new WebSocket('ws://localhost:8080/api/stream');

ws.onopen = () => {
  ws.send(JSON.stringify({
    type: 'subscribe',
    filters: ['interface =~ "eth.*"']
  }));
};

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  if (data.type === 'event') {
    console.log('New event:', data.data);
  }
};
```

## Best Practices

1. **Use Filters**: Apply filters to reduce data transfer
2. **Pagination**: Use limit and offset for large result sets
3. **WebSocket for Real-time**: Use WebSocket API for real-time monitoring
4. **Cache Responses**: Cache statistics and configuration data
5. **Handle Errors**: Implement proper error handling
6. **Rate Limiting**: Respect rate limits and implement backoff
7. **Authentication**: Always use authentication in production
8. **HTTPS**: Use TLS encryption for sensitive data

## Versioning

The API version is included in the response headers:

```http
X-API-Version: 1.0
```

Breaking changes will result in a new API version. The current version will be supported for at least one major release cycle.

## Support

For API support:
- Documentation: https://nlmon.readthedocs.io/api
- Issues: https://github.com/nlmon/nlmon/issues
- Discussions: https://github.com/nlmon/nlmon/discussions

