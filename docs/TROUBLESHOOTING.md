# nlmon Troubleshooting Guide

## Table of Contents

1. [Installation Issues](#installation-issues)
2. [Runtime Errors](#runtime-errors)
3. [Performance Problems](#performance-problems)
4. [Configuration Issues](#configuration-issues)
5. [Plugin Problems](#plugin-problems)
6. [Network Monitoring Issues](#network-monitoring-issues)
7. [CLI Interface Problems](#cli-interface-problems)
8. [Web Dashboard Issues](#web-dashboard-issues)
9. [Database and Storage Issues](#database-and-storage-issues)
10. [Common Error Messages](#common-error-messages)
11. [Debugging Techniques](#debugging-techniques)
12. [Getting Help](#getting-help)

## Installation Issues

### Problem: Compilation Fails with Missing Dependencies

**Symptoms:**
```
fatal error: yaml.h: No such file or directory
fatal error: ncurses.h: No such file or directory
```

**Solution:**

Install required development packages:

**Ubuntu/Debian:**
```bash
sudo apt-get install -y \
    libyaml-dev \
    libncurses-dev \
    libsqlite3-dev \
    libmicrohttpd-dev \
    libssl-dev
```

**RHEL/CentOS/Fedora:**
```bash
sudo yum install -y \
    libyaml-devel \
    ncurses-devel \
    sqlite-devel \
    libmicrohttpd-devel \
    openssl-devel
```

### Problem: Permission Denied When Installing

**Symptoms:**
```
make: *** [install] Error 1
Permission denied
```

**Solution:**

Use sudo for installation:
```bash
sudo make install
```

Or install to user directory:
```bash
make PREFIX=$HOME/.local install
```

### Problem: Binary Not Found After Installation

**Symptoms:**
```
bash: nlmon: command not found
```

**Solution:**

1. Check installation path:
```bash
which nlmon
```

2. Add to PATH if needed:
```bash
export PATH=$PATH:/usr/local/bin
```

3. Or use full path:
```bash
/usr/local/bin/nlmon
```

## Runtime Errors

### Problem: Permission Denied When Starting

**Symptoms:**
```
Error: Failed to open netlink socket: Permission denied
```

**Cause:**
nlmon requires CAP_NET_ADMIN capability to access netlink sockets.

**Solution:**

**Option 1:** Run as root (not recommended for production)
```bash
sudo nlmon
```

**Option 2:** Grant capability (recommended)
```bash
sudo setcap cap_net_admin+ep /usr/bin/nlmon
nlmon
```

**Option 3:** Run as specific user with capability
```bash
sudo setcap cap_net_admin+ep /usr/bin/nlmon
sudo chown nlmon:nlmon /usr/bin/nlmon
```

### Problem: Segmentation Fault on Startup

**Symptoms:**
```
Segmentation fault (core dumped)
```

**Diagnosis:**

1. Run with core dump enabled:
```bash
ulimit -c unlimited
nlmon
```

2. Analyze core dump:
```bash
gdb nlmon core
(gdb) bt
```

3. Check for common causes:
   - Corrupted configuration file
   - Missing required files
   - Incompatible plugin

**Solution:**

1. Try with minimal configuration:
```bash
nlmon --no-cli --config /dev/null
```

2. Disable plugins:
```bash
nlmon --disable-plugin=all
```

3. Check logs:
```bash
journalctl -u nlmon -n 50
```

### Problem: High CPU Usage

**Symptoms:**
- nlmon process consuming 100% CPU
- System becomes unresponsive

**Diagnosis:**

Check event rate:
```bash
# In another terminal
curl http://localhost:9090/metrics | grep nlmon_events_total
```

**Solution:**

1. Enable rate limiting in configuration:
```yaml
nlmon:
  core:
    rate_limit: 1000  # Events per second
```

2. Filter events to reduce load:
```yaml
nlmon:
  monitoring:
    interfaces:
      include: ["eth0", "eth1"]  # Only monitor specific interfaces
```

3. Reduce worker threads:
```yaml
nlmon:
  core:
    worker_threads: 2  # Reduce from default
```

### Problem: Memory Leak

**Symptoms:**
- Memory usage grows continuously
- System runs out of memory
- OOM killer terminates nlmon

**Diagnosis:**

1. Monitor memory usage:
```bash
watch -n 1 'ps aux | grep nlmon'
```

2. Check with valgrind:
```bash
valgrind --leak-check=full --show-leak-kinds=all nlmon
```

**Solution:**

1. Limit event buffer size:
```yaml
nlmon:
  core:
    max_events: 1000  # Reduce buffer size
```

2. Enable database retention:
```yaml
nlmon:
  output:
    database:
      retention_days: 7  # Auto-delete old events
```

3. Disable problematic plugins:
```bash
nlmon --disable-plugin=problematic_plugin
```

4. Report bug with valgrind output

## Performance Problems

### Problem: Slow Event Processing

**Symptoms:**
- Events appear delayed in CLI
- High latency between event and display
- Queue depth increasing

**Diagnosis:**

Check performance metrics:
```bash
curl http://localhost:9090/metrics | grep processing_duration
```

**Solution:**

1. Increase worker threads:
```yaml
nlmon:
  core:
    worker_threads: 8  # Increase for multi-core systems
```

2. Optimize filters:
```yaml
# Use simple filters instead of complex regex
filters:
  - name: "eth_only"
    expression: "interface == 'eth0'"  # Fast
    # Instead of: "interface =~ '.*'"  # Slow
```

3. Disable expensive features:
```yaml
nlmon:
  correlation:
    enabled: false  # Disable if not needed
  security:
    detectors:
      enabled: false  # Disable if not needed
```

### Problem: Database Writes Are Slow

**Symptoms:**
- High I/O wait
- Database file growing slowly
- Events backing up

**Solution:**

1. Increase batch size:
```yaml
nlmon:
  output:
    database:
      batch_size: 1000  # Increase from default
```

2. Use faster storage:
```yaml
nlmon:
  output:
    database:
      path: "/dev/shm/nlmon.db"  # Use RAM disk (temporary)
```

3. Enable WAL mode (should be default):
```bash
sqlite3 /var/lib/nlmon/events.db "PRAGMA journal_mode=WAL;"
```

### Problem: High Network Bandwidth Usage

**Symptoms:**
- High network traffic from nlmon
- Bandwidth saturation

**Cause:**
Usually caused by syslog forwarding or webhook exports.

**Solution:**

1. Reduce syslog verbosity:
```yaml
nlmon:
  output:
    syslog:
      enabled: true
      # Only send critical events
      min_severity: error
```

2. Add event filtering:
```yaml
nlmon:
  filters:
    - name: "critical_only"
      expression: "severity >= 'error'"
```

3. Use compression:
```yaml
nlmon:
  output:
    syslog:
      protocol: tls  # Enables compression
```

## Configuration Issues

### Problem: Configuration File Not Found

**Symptoms:**
```
Warning: Configuration file not found, using defaults
```

**Solution:**

1. Create configuration file:
```bash
sudo mkdir -p /etc/nlmon
sudo cp /usr/share/doc/nlmon/examples/basic-config.yaml /etc/nlmon/nlmon.yaml
```

2. Or specify config file:
```bash
nlmon --config /path/to/config.yaml
```

### Problem: Invalid Configuration

**Symptoms:**
```
Error: Invalid configuration: ...
```

**Solution:**

1. Validate configuration:
```bash
nlmon --validate-config --config /etc/nlmon/nlmon.yaml
```

2. Check YAML syntax:
```bash
yamllint /etc/nlmon/nlmon.yaml
```

3. Review error message for specific issue

4. Compare with example configuration:
```bash
diff /etc/nlmon/nlmon.yaml /usr/share/doc/nlmon/examples/basic-config.yaml
```

### Problem: Configuration Changes Not Applied

**Symptoms:**
- Modified configuration but behavior unchanged
- Old settings still in effect

**Solution:**

1. Reload configuration:
```bash
# Send SIGHUP to reload
sudo killall -HUP nlmon
```

2. Or restart service:
```bash
sudo systemctl restart nlmon
```

3. Verify configuration is loaded:
```bash
# Check logs
journalctl -u nlmon | grep "Configuration loaded"
```

### Problem: Environment Variables Not Working

**Symptoms:**
```
NLMON_WEB_PORT=9000 nlmon
# But web server still on port 8080
```

**Solution:**

1. Check variable name format:
```bash
# Correct format: NLMON_<section>_<key>
export NLMON_WEB_PORT=9000
export NLMON_CORE_LOG_LEVEL=debug
```

2. Verify variable is set:
```bash
env | grep NLMON
```

3. Check configuration precedence:
   - Command-line options override environment variables
   - Environment variables override config file

## Plugin Problems

### Problem: Plugin Not Loading

**Symptoms:**
```
Warning: Failed to load plugin: my_plugin
```

**Diagnosis:**

1. Check plugin exists:
```bash
ls -l /usr/lib/nlmon/plugins/my_plugin.so
```

2. Check plugin symbols:
```bash
nm -D /usr/lib/nlmon/plugins/my_plugin.so | grep nlmon_plugin_register
```

3. Check dependencies:
```bash
ldd /usr/lib/nlmon/plugins/my_plugin.so
```

**Solution:**

1. Verify plugin is enabled:
```yaml
nlmon:
  plugins:
    enabled:
      - my_plugin
```

2. Check file permissions:
```bash
sudo chmod 644 /usr/lib/nlmon/plugins/my_plugin.so
```

3. Check for API version mismatch:
```bash
# Rebuild plugin with correct API version
```

### Problem: Plugin Crashes nlmon

**Symptoms:**
- nlmon crashes when plugin is enabled
- Segmentation fault in plugin code

**Solution:**

1. Disable problematic plugin:
```yaml
nlmon:
  plugins:
    disabled:
      - problematic_plugin
```

2. Debug plugin:
```bash
gdb nlmon
(gdb) run
# Trigger crash
(gdb) bt
```

3. Check plugin logs:
```bash
journalctl -u nlmon | grep "plugin"
```

4. Report bug to plugin author

### Problem: Plugin Performance Issues

**Symptoms:**
- Slow event processing with plugin enabled
- High CPU usage from plugin

**Diagnosis:**

Check plugin statistics:
```bash
# In CLI mode, if plugin provides stats command
# Or check metrics endpoint
curl http://localhost:9090/metrics | grep plugin
```

**Solution:**

1. Disable plugin temporarily:
```bash
nlmon --disable-plugin=slow_plugin
```

2. Configure plugin for better performance:
```yaml
nlmon:
  plugins:
    config:
      slow_plugin:
        batch_size: 100  # Process events in batches
        async: true      # Enable async processing
```

3. Contact plugin author for optimization

## Network Monitoring Issues

### Problem: No Events Captured

**Symptoms:**
- CLI shows no events
- Event counter stays at zero

**Diagnosis:**

1. Check netlink socket:
```bash
# Verify nlmon is running
ps aux | grep nlmon

# Check for netlink errors in logs
journalctl -u nlmon | grep netlink
```

2. Verify network activity:
```bash
# Generate test events
sudo ip link set eth0 down
sudo ip link set eth0 up
```

**Solution:**

1. Check interface filters:
```yaml
nlmon:
  monitoring:
    interfaces:
      include: ["*"]  # Monitor all interfaces
      exclude: []     # No exclusions
```

2. Verify netlink protocols:
```yaml
nlmon:
  monitoring:
    protocols:
      - NETLINK_ROUTE  # Required for basic monitoring
```

3. Check permissions:
```bash
sudo setcap cap_net_admin+ep /usr/bin/nlmon
```

### Problem: Missing Events for Specific Interface

**Symptoms:**
- Events captured for some interfaces but not others
- Specific interface not monitored

**Solution:**

1. Check interface filter:
```yaml
nlmon:
  monitoring:
    interfaces:
      include: ["eth*", "veth*", "docker*"]  # Add your interface pattern
```

2. Verify interface exists:
```bash
ip link show
```

3. Check namespace:
```yaml
nlmon:
  monitoring:
    namespaces:
      enabled: true  # Enable if interface is in namespace
```

### Problem: Duplicate Events

**Symptoms:**
- Same event appears multiple times
- Event counter higher than expected

**Cause:**
- Multiple netlink groups subscribed
- Plugin emitting duplicate events

**Solution:**

1. Check netlink groups configuration
2. Disable duplicate plugins
3. Add event deduplication filter

## CLI Interface Problems

### Problem: CLI Not Displaying

**Symptoms:**
- Terminal shows nothing
- Blank screen
- Garbled output

**Solution:**

1. Check terminal compatibility:
```bash
echo $TERM
# Should be xterm, xterm-256color, or similar
```

2. Verify ncurses:
```bash
# Test with other ncurses programs
top
htop
```

3. Reset terminal:
```bash
reset
```

4. Try different terminal emulator

### Problem: Colors Not Working

**Symptoms:**
- All text appears same color
- No color coding for event types

**Solution:**

1. Check terminal color support:
```bash
tput colors
# Should return 256 or higher
```

2. Enable colors in configuration:
```yaml
nlmon:
  cli:
    color: true
```

3. Set TERM variable:
```bash
export TERM=xterm-256color
nlmon
```

### Problem: CLI Freezes or Hangs

**Symptoms:**
- CLI becomes unresponsive
- Cannot quit with 'q'
- Terminal frozen

**Solution:**

1. Force quit:
```bash
# Press Ctrl+C
# Or from another terminal:
killall nlmon
```

2. Check for deadlock:
```bash
# Attach debugger
sudo gdb -p $(pgrep nlmon)
(gdb) thread apply all bt
```

3. Disable problematic features:
```bash
nlmon --no-cli  # Run without CLI
```

### Problem: Search Not Working

**Symptoms:**
- Search finds no results
- Search highlights wrong events

**Solution:**

1. Check search syntax:
   - Search is case-sensitive
   - Searches in message, event type, and interface fields

2. Try simpler search term

3. Clear search and try again (press ESC)

## Web Dashboard Issues

### Problem: Cannot Access Web Dashboard

**Symptoms:**
```
curl: (7) Failed to connect to localhost port 8080: Connection refused
```

**Diagnosis:**

1. Check if web server is enabled:
```yaml
nlmon:
  web:
    enabled: true
    port: 8080
```

2. Verify nlmon is running:
```bash
ps aux | grep nlmon
```

3. Check port binding:
```bash
sudo netstat -tlnp | grep 8080
```

**Solution:**

1. Enable web dashboard:
```yaml
nlmon:
  web:
    enabled: true
    host: "0.0.0.0"  # Listen on all interfaces
    port: 8080
```

2. Check firewall:
```bash
sudo ufw allow 8080/tcp
# Or for firewalld:
sudo firewall-cmd --add-port=8080/tcp --permanent
sudo firewall-cmd --reload
```

3. Verify no port conflict:
```bash
sudo lsof -i :8080
```

### Problem: Web Dashboard Shows No Data

**Symptoms:**
- Dashboard loads but shows no events
- Empty event list
- No statistics

**Solution:**

1. Check WebSocket connection:
   - Open browser developer console
   - Look for WebSocket errors

2. Verify events are being captured:
```bash
# Check metrics endpoint
curl http://localhost:9090/metrics | grep nlmon_events_total
```

3. Check CORS settings:
```yaml
nlmon:
  web:
    cors:
      enabled: true
      origins: ["*"]
```

### Problem: TLS/HTTPS Not Working

**Symptoms:**
```
Error: Failed to load certificate
```

**Solution:**

1. Verify certificate files exist:
```bash
ls -l /etc/nlmon/cert.pem /etc/nlmon/key.pem
```

2. Check certificate permissions:
```bash
sudo chmod 600 /etc/nlmon/key.pem
sudo chmod 644 /etc/nlmon/cert.pem
```

3. Generate self-signed certificate:
```bash
openssl req -x509 -newkey rsa:4096 -keyout /etc/nlmon/key.pem \
    -out /etc/nlmon/cert.pem -days 365 -nodes
```

4. Configure TLS:
```yaml
nlmon:
  web:
    tls:
      enabled: true
      cert: "/etc/nlmon/cert.pem"
      key: "/etc/nlmon/key.pem"
```

### Problem: Authentication Fails

**Symptoms:**
- Cannot log in to web dashboard
- 401 Unauthorized errors

**Solution:**

1. Check authentication configuration:
```yaml
nlmon:
  web:
    auth:
      enabled: true
      type: jwt
      users:
        - username: admin
          password_hash: "$2a$10$..."  # bcrypt hash
```

2. Generate password hash:
```bash
htpasswd -bnBC 10 "" password | tr -d ':\n'
```

3. Verify credentials

4. Check JWT secret is configured

## Database and Storage Issues

### Problem: Database File Locked

**Symptoms:**
```
Error: database is locked
```

**Cause:**
- Another process accessing database
- Stale lock file
- Filesystem issue

**Solution:**

1. Check for other nlmon instances:
```bash
ps aux | grep nlmon
```

2. Remove stale lock:
```bash
rm /var/lib/nlmon/events.db-shm
rm /var/lib/nlmon/events.db-wal
```

3. Enable WAL mode:
```bash
sqlite3 /var/lib/nlmon/events.db "PRAGMA journal_mode=WAL;"
```

### Problem: Database Growing Too Large

**Symptoms:**
- Database file size increasing rapidly
- Disk space running out

**Solution:**

1. Enable retention policy:
```yaml
nlmon:
  output:
    database:
      retention_days: 30  # Auto-delete old events
```

2. Manual cleanup:
```bash
sqlite3 /var/lib/nlmon/events.db "DELETE FROM events WHERE timestamp < strftime('%s', 'now', '-30 days');"
sqlite3 /var/lib/nlmon/events.db "VACUUM;"
```

3. Reduce event storage:
```yaml
nlmon:
  output:
    database:
      enabled: false  # Disable if not needed
```

### Problem: PCAP File Rotation Not Working

**Symptoms:**
- PCAP file grows indefinitely
- No rotated files created

**Solution:**

1. Check rotation configuration:
```yaml
nlmon:
  output:
    pcap:
      enabled: true
      file: "/var/log/nlmon/capture.pcap"
      rotate_size: 100MB
      rotate_count: 5
```

2. Verify write permissions:
```bash
ls -ld /var/log/nlmon/
sudo chown nlmon:nlmon /var/log/nlmon/
```

3. Check disk space:
```bash
df -h /var/log/nlmon/
```

### Problem: Audit Log Tamper Detection False Positives

**Symptoms:**
```
Warning: Audit log integrity check failed
```

**Cause:**
- File manually edited
- Corruption
- Bug in hash chain

**Solution:**

1. Verify file integrity:
```bash
/usr/bin/audit_verify /var/log/nlmon/audit.log
```

2. Check for corruption:
```bash
tail -n 100 /var/log/nlmon/audit.log
```

3. If false positive, report bug

4. Start new audit log:
```bash
sudo mv /var/log/nlmon/audit.log /var/log/nlmon/audit.log.old
sudo systemctl restart nlmon
```

## Common Error Messages

### "Failed to allocate memory"

**Cause:** System out of memory or memory limit reached

**Solution:**
1. Increase system memory
2. Reduce buffer sizes in configuration
3. Check for memory leaks

### "Invalid filter expression"

**Cause:** Syntax error in filter

**Solution:**
1. Check filter syntax in documentation
2. Test filter with simple expression first
3. Use filter validation tool

### "Plugin API version mismatch"

**Cause:** Plugin compiled for different nlmon version

**Solution:**
1. Rebuild plugin with current API version
2. Update nlmon to match plugin version
3. Disable incompatible plugin

### "Failed to bind to port"

**Cause:** Port already in use or permission denied

**Solution:**
1. Check for port conflicts: `sudo lsof -i :<port>`
2. Use different port in configuration
3. Stop conflicting service
4. Run with appropriate permissions

### "Configuration file syntax error"

**Cause:** Invalid YAML syntax

**Solution:**
1. Validate YAML: `yamllint config.yaml`
2. Check indentation (use spaces, not tabs)
3. Quote special characters
4. Compare with example configuration

## Debugging Techniques

### Enable Debug Logging

```yaml
nlmon:
  core:
    log_level: debug
```

Or via environment variable:
```bash
NLMON_LOG_LEVEL=debug nlmon
```

### Use GDB for Crashes

```bash
# Run under debugger
sudo gdb nlmon
(gdb) run
# When crash occurs:
(gdb) bt
(gdb) info locals
(gdb) print variable_name
```

### Attach to Running Process

```bash
sudo gdb -p $(pgrep nlmon)
(gdb) bt
(gdb) thread apply all bt
```

### Memory Debugging with Valgrind

```bash
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --verbose \
         --log-file=valgrind.log \
         nlmon
```

### System Call Tracing

```bash
sudo strace -f -e trace=network,file nlmon 2>&1 | tee strace.log
```

### Performance Profiling

```bash
# CPU profiling
sudo perf record -g nlmon
sudo perf report

# Or use built-in profiler
nlmon --enable-profiler
```

### Network Debugging

```bash
# Monitor netlink messages
sudo ip monitor

# Capture netlink with tcpdump
sudo tcpdump -i any -w netlink.pcap netlink

# Check netlink statistics
cat /proc/net/netlink
```

### Check System Resources

```bash
# Memory usage
ps aux | grep nlmon
pmap $(pgrep nlmon)

# File descriptors
ls -l /proc/$(pgrep nlmon)/fd/

# Threads
ps -eLf | grep nlmon

# CPU usage
top -p $(pgrep nlmon)
```

## Getting Help

### Before Asking for Help

1. **Check documentation:**
   - Man pages: `man nlmon`, `man nlmon.conf`
   - Online documentation
   - This troubleshooting guide

2. **Search existing issues:**
   - GitHub issues
   - Mailing list archives

3. **Gather information:**
   - nlmon version: `nlmon --version`
   - OS and kernel: `uname -a`
   - Configuration file
   - Error messages and logs
   - Steps to reproduce

### Reporting Bugs

Include in bug report:

1. **System information:**
```bash
nlmon --version
uname -a
cat /etc/os-release
```

2. **Configuration:**
```bash
cat /etc/nlmon/nlmon.yaml
```

3. **Logs:**
```bash
journalctl -u nlmon -n 100
```

4. **Steps to reproduce:**
   - Exact commands run
   - Expected vs actual behavior
   - Frequency (always, sometimes, once)

5. **Additional context:**
   - Recent changes
   - Related software
   - Workarounds attempted

### Where to Get Help

- **GitHub Issues:** https://github.com/nlmon/nlmon/issues
- **Discussions:** https://github.com/nlmon/nlmon/discussions
- **Mailing List:** nlmon-users@lists.example.com
- **IRC:** #nlmon on irc.freenode.net
- **Stack Overflow:** Tag questions with `nlmon`

### Commercial Support

For commercial support options, contact: support@nlmon.example.com

## Appendix: Diagnostic Commands

### Quick Health Check

```bash
#!/bin/bash
# nlmon-health-check.sh

echo "=== nlmon Health Check ==="
echo

echo "1. Process Status:"
ps aux | grep nlmon | grep -v grep
echo

echo "2. Listening Ports:"
sudo netstat -tlnp | grep nlmon
echo

echo "3. Memory Usage:"
ps -o pid,vsz,rss,comm -p $(pgrep nlmon)
echo

echo "4. Open Files:"
sudo lsof -p $(pgrep nlmon) | wc -l
echo

echo "5. Recent Errors:"
journalctl -u nlmon --since "10 minutes ago" | grep -i error
echo

echo "6. Event Statistics:"
curl -s http://localhost:9090/metrics | grep nlmon_events_total
echo

echo "=== Health Check Complete ==="
```

### Configuration Validator

```bash
#!/bin/bash
# validate-config.sh

CONFIG_FILE="${1:-/etc/nlmon/nlmon.yaml}"

echo "Validating: $CONFIG_FILE"

# Check file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "ERROR: File not found"
    exit 1
fi

# Check YAML syntax
if command -v yamllint &> /dev/null; then
    yamllint "$CONFIG_FILE"
else
    echo "WARNING: yamllint not installed"
fi

# Validate with nlmon
nlmon --validate-config --config "$CONFIG_FILE"

echo "Validation complete"
```

### Log Analyzer

```bash
#!/bin/bash
# analyze-logs.sh

echo "=== nlmon Log Analysis ==="
echo

echo "Error Summary:"
journalctl -u nlmon | grep -i error | sort | uniq -c | sort -rn
echo

echo "Warning Summary:"
journalctl -u nlmon | grep -i warning | sort | uniq -c | sort -rn
echo

echo "Recent Restarts:"
journalctl -u nlmon | grep "Started\|Stopped"
echo

echo "=== Analysis Complete ==="
```

## Conclusion

This troubleshooting guide covers the most common issues encountered when using nlmon. If you encounter an issue not covered here, please:

1. Check the documentation
2. Search existing issues
3. Ask for help in the community
4. Report bugs with detailed information

For the latest troubleshooting tips and known issues, visit:
https://github.com/nlmon/nlmon/wiki/Troubleshooting

