# Makefile for nlmon example plugins

CC = gcc
CFLAGS = -Wall -Wextra -fPIC -O2
INCLUDES = -I../../../include
LDFLAGS = -shared

# Plugin directory
PLUGIN_DIR = /usr/lib/nlmon/plugins

# Example plugins
PLUGINS = event_logger.so csv_exporter.so cli_commands.so

.PHONY: all clean install

all: $(PLUGINS)

event_logger.so: event_logger_plugin.c
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $<

csv_exporter.so: csv_exporter_plugin.c
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $<

cli_commands.so: cli_command_plugin.c
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(PLUGINS)

install: $(PLUGINS)
	@echo "Installing example plugins to $(PLUGIN_DIR)..."
	@mkdir -p $(PLUGIN_DIR)
	@install -m 755 $(PLUGINS) $(PLUGIN_DIR)/
	@echo "Example plugins installed successfully"

uninstall:
	@echo "Removing example plugins from $(PLUGIN_DIR)..."
	@cd $(PLUGIN_DIR) && rm -f $(PLUGINS)
	@echo "Example plugins removed"

.PHONY: help
help:
	@echo "nlmon Example Plugins Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build all example plugins (default)"
	@echo "  clean     - Remove built plugins"
	@echo "  install   - Install plugins to $(PLUGIN_DIR)"
	@echo "  uninstall - Remove plugins from $(PLUGIN_DIR)"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Example plugins:"
	@echo "  event_logger.so  - Logs all events to a file"
	@echo "  csv_exporter.so  - Exports events to CSV format"
	@echo "  cli_commands.so  - Adds utility CLI commands"
