# nlmon Configuration Examples

This directory contains example configuration files for various nlmon use cases. These examples demonstrate different features and can be used as starting points for your own configurations.

## Available Examples

### 1. basic-config.yaml
**Use Case:** Getting started with nlmon

A minimal configuration suitable for learning nlmon basics. This configuration:
- Monitors all interfaces
- Uses console output only
- Disables advanced features
- Suitable for development and testing

**Usage:**
```bash
nlmon -c docs/examples/basic-config.yaml
```

### 2. advanced-config.yaml
**Use Case:** Production environment with full features

A comprehensive configuration demonstrating all nlmon capabilities:
- Multiple output formats (PCAP, JSON, database, syslog)
- Web dashboard with TLS and authentication
- Prometheus metrics export
- Plugin system integration
- Alert rules and notifications
- Container platform integration (Kubernetes, Docker)
- Security monitoring

**Usage:**
```bash
nlmon -c docs/examples/advanced-config.yaml
```

**Prerequisites:**
- TLS certificates for web dashboard
- Kubernetes/Docker access (if using integrations)
- Webhook URLs configured in environment variables

### 3. container-monitoring.yaml
**Use Case:** Docker and Kubernetes container networking

Optimized for monitoring container networking events:
- Focuses on container interfaces (veth*, docker*, br-*)
- Kubernetes and Docker plugin integration
- Event enrichment with pod/container metadata
- Container lifecycle alerts
- JSON export for analysis

**Usage:**
```bash
nlmon -c docs/examples/container-monitoring.yaml
```

**Prerequisites:**
- Docker socket access: `/var/run/docker.sock`
- Kubernetes config: `~/.kube/config`
- Appropriate permissions for container APIs

### 4. security-monitoring.yaml
**Use Case:** Security event detection and forensics

Focused on detecting and logging security-relevant events:
- Promiscuous mode detection
- ARP flood detection
- Route hijacking detection
- Tamper-evident audit logging
- SIEM integration via syslog
- Long-term event retention (1 year)
- Strict access controls

**Usage:**
```bash
nlmon -c docs/examples/security-monitoring.yaml
```

**Prerequisites:**
- TLS certificates for secure web access
- SIEM/syslog server configured
- SNMP trap receiver (optional)
- Security webhook endpoints

### 5. production-server.yaml
**Use Case:** High-performance production servers

Optimized for production environments:
- High throughput (10K events/sec)
- Large event buffers
- Aggressive log rotation
- Daemon mode (no CLI)
- Comprehensive alerting (PagerDuty integration)
- Prometheus metrics
- Long retention periods

**Usage:**
```bash
# Run as daemon
nlmon --daemon -c docs/examples/production-server.yaml --pid-file /var/run/nlmon.pid

# With systemd
sudo systemctl start nlmon
```

**Prerequisites:**
- PagerDuty or similar alerting system
- Prometheus monitoring
- Centralized logging infrastructure
- Production TLS certificates

## Configuration Tips

### Environment Variables

All configurations support environment variable substitution:

```yaml
web:
  port: ${NLMON_WEB_PORT:-8080}  # Default to 8080
  
integration:
  kubernetes:
    kubeconfig: "${HOME}/.kube/config"
```

Set environment variables before starting nlmon:
```bash
export NLMON_WEB_PORT=9000
export WEBHOOK_TOKEN="your-secret-token"
nlmon -c your-config.yaml
```

### Validation

Validate your configuration before running:
```bash
nlmon --validate-config -c your-config.yaml
```

### Incremental Configuration

Start with `basic-config.yaml` and gradually add features:

1. Start with basic monitoring
2. Add output formats (JSON, PCAP)
3. Enable web dashboard
4. Add filters and alerts
5. Integrate with external systems

### Performance Tuning

For high-traffic environments:

- Increase `buffer_size` and `max_events`
- Raise `rate_limit`
- Add more `worker_threads`
- Use `batch_size` for database writes
- Disable console output in daemon mode
- Reduce `log_level` to warning or error

### Security Hardening

For production deployments:

- Enable TLS for web dashboard
- Use strong authentication (JWT)
- Enable audit logging with hash chaining
- Restrict CORS origins
- Use TLS 1.3 minimum
- Enable rate limiting
- Run with minimal privileges

## Common Patterns

### Filtering by Interface

```yaml
monitoring:
  interfaces:
    include: ["eth*", "bond*"]
    exclude: ["lo", "docker*"]
```

### Alert on Critical Events

```yaml
alerts:
  - name: "interface_down"
    condition: "event.type == 'RTM_DELLINK' AND interface == 'eth0'"
    severity: critical
    actions:
      - type: webhook
        url: "${ALERT_WEBHOOK_URL}"
```

### Export to Multiple Formats

```yaml
output:
  pcap:
    enabled: true
    file: "/var/log/nlmon/capture.pcap"
  json:
    enabled: true
    file: "/var/log/nlmon/events.json"
  database:
    enabled: true
    path: "/var/lib/nlmon/events.db"
```

### Container Integration

```yaml
plugins:
  enabled:
    - kubernetes
    - docker
  config:
    kubernetes:
      kubeconfig: "~/.kube/config"
    docker:
      socket: "/var/run/docker.sock"
```

## Troubleshooting

### Configuration Errors

If nlmon fails to start:

1. Validate configuration:
   ```bash
   nlmon --validate-config -c your-config.yaml
   ```

2. Check log level:
   ```yaml
   core:
     log_level: debug
   ```

3. Verify file permissions:
   ```bash
   ls -la /var/log/nlmon/
   ls -la /var/lib/nlmon/
   ```

### Permission Issues

nlmon requires `CAP_NET_ADMIN` capability:

```bash
# Run as root
sudo nlmon -c your-config.yaml

# Or set capability
sudo setcap cap_net_admin+ep /usr/bin/nlmon
nlmon -c your-config.yaml
```

### Plugin Loading Failures

Check plugin directory and permissions:

```bash
ls -la /usr/lib/nlmon/plugins/
ldd /usr/lib/nlmon/plugins/your-plugin.so
```

Enable debug logging to see plugin load errors:

```yaml
core:
  log_level: debug
```

## Additional Resources

- Main documentation: `man nlmon`
- Configuration reference: `man nlmon.conf`
- Plugin development: `man nlmon-plugins`
- Online wiki: https://github.com/yourusername/nlmon/wiki
- Example plugins: `src/plugins/examples/`

## Contributing

Have a useful configuration pattern? Submit a pull request with:

1. The configuration file
2. Description of the use case
3. Prerequisites and setup instructions
4. Any special considerations

## License

These configuration examples are provided under the same license as nlmon (MIT License).
