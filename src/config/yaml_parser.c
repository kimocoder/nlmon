/* yaml_parser.c - YAML configuration file parser
 *
 * Copyright (C) 2025  nlmon contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>
#include "nlmon_config.h"

/* Parser context */
struct yaml_parse_ctx {
	struct nlmon_config *config;
	yaml_parser_t parser;
	FILE *file;
	
	/* Current parsing state */
	char section[64];
	char subsection[64];
	char subsubsection[64];
	char key[128];
	int array_index;
	bool in_array;
};

/* Helper function to expand environment variables in strings */
static void expand_env_vars(char *dest, size_t dest_size, const char *src)
{
	const char *p = src;
	char *d = dest;
	char *end = dest + dest_size - 1;
	
	while (*p && d < end) {
		if (*p == '$' && *(p + 1) == '{') {
			/* Found ${VAR} pattern */
			const char *var_start = p + 2;
			const char *var_end = strchr(var_start, '}');
			
			if (var_end) {
				char var_name[128];
				size_t var_len = var_end - var_start;
				
				if (var_len < sizeof(var_name)) {
					memcpy(var_name, var_start, var_len);
					var_name[var_len] = '\0';
					
					const char *env_val = getenv(var_name);
					if (env_val) {
						/* Copy environment variable value */
						while (*env_val && d < end)
							*d++ = *env_val++;
					}
					
					p = var_end + 1;
					continue;
				}
			}
		}
		
		*d++ = *p++;
	}
	
	*d = '\0';
}

/* Parse size value with unit suffix (KB, MB, GB) */
static size_t parse_size(const char *value)
{
	char *endptr;
	long long num = strtoll(value, &endptr, 10);
	
	if (num < 0)
		return 0;
	
	/* Check for unit suffix */
	if (*endptr != '\0') {
		if (strcasecmp(endptr, "KB") == 0)
			return (size_t)num * 1024;
		else if (strcasecmp(endptr, "MB") == 0)
			return (size_t)num * 1024 * 1024;
		else if (strcasecmp(endptr, "GB") == 0)
			return (size_t)num * 1024 * 1024 * 1024;
	}
	
	return (size_t)num;
}

/* Parse boolean value */
static bool parse_bool(const char *value)
{
	return (strcmp(value, "true") == 0 ||
	        strcmp(value, "yes") == 0 ||
	        strcmp(value, "1") == 0);
}

/* Parse scalar value based on current context */
static int parse_scalar_value(struct yaml_parse_ctx *ctx, const char *value)
{
	struct nlmon_config *cfg = ctx->config;
	char expanded[NLMON_MAX_PATH];
	const char *section;
	
	/* Expand environment variables */
	expand_env_vars(expanded, sizeof(expanded), value);
	
	/* Determine the actual section to use
	 * If section is "nlmon", use subsection as the section
	 * This handles the "nlmon:" wrapper in YAML files
	 */
	if (strcmp(ctx->section, "nlmon") == 0) {
		section = ctx->subsection;
	} else {
		section = ctx->section;
	}
	
	/* Core configuration */
	if (strcmp(section, "core") == 0) {
		if (strcmp(ctx->key, "buffer_size") == 0) {
			cfg->core.buffer_size = parse_size(expanded);
		} else if (strcmp(ctx->key, "max_events") == 0) {
			cfg->core.max_events = atoi(expanded);
		} else if (strcmp(ctx->key, "rate_limit") == 0) {
			cfg->core.rate_limit = atoi(expanded);
		} else if (strcmp(ctx->key, "worker_threads") == 0) {
			cfg->core.worker_threads = atoi(expanded);
		}
	}
	/* Monitoring configuration */
	else if (strcmp(section, "monitoring") == 0) {
		if (strcmp(ctx->subsubsection, "interfaces") == 0) {
			if (strcmp(ctx->key, "include") == 0 && ctx->in_array) {
				if (cfg->monitoring.include_count < NLMON_MAX_INTERFACES) {
					strncpy(cfg->monitoring.include_patterns[cfg->monitoring.include_count],
					        expanded, NLMON_MAX_PATTERN - 1);
					cfg->monitoring.include_count++;
				}
			} else if (strcmp(ctx->key, "exclude") == 0 && ctx->in_array) {
				if (cfg->monitoring.exclude_count < NLMON_MAX_INTERFACES) {
					strncpy(cfg->monitoring.exclude_patterns[cfg->monitoring.exclude_count],
					        expanded, NLMON_MAX_PATTERN - 1);
					cfg->monitoring.exclude_count++;
				}
			}
		} else if (strcmp(ctx->subsubsection, "message_types") == 0 && ctx->in_array) {
			if (cfg->monitoring.msg_type_count < NLMON_MAX_MSG_TYPES) {
				cfg->monitoring.msg_types[cfg->monitoring.msg_type_count] = atoi(expanded);
				cfg->monitoring.msg_type_count++;
			}
		} else if (strcmp(ctx->subsubsection, "protocols") == 0 && ctx->in_array) {
			if (cfg->monitoring.protocol_count < 8) {
				cfg->monitoring.protocols[cfg->monitoring.protocol_count] = atoi(expanded);
				cfg->monitoring.protocol_count++;
			}
		} else if (strcmp(ctx->subsubsection, "namespaces") == 0) {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->monitoring.namespaces_enabled = parse_bool(expanded);
			}
		}
	}
	/* Output configuration */
	else if (strcmp(section, "output") == 0) {
		if (strcmp(ctx->subsubsection, "console") == 0) {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->output.console.enabled = parse_bool(expanded);
			} else if (strcmp(ctx->key, "format") == 0) {
				strncpy(cfg->output.console.format, expanded,
				        sizeof(cfg->output.console.format) - 1);
			}
		} else if (strcmp(ctx->subsubsection, "pcap") == 0) {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->output.pcap.enabled = parse_bool(expanded);
			} else if (strcmp(ctx->key, "file") == 0) {
				strncpy(cfg->output.pcap.file, expanded,
				        sizeof(cfg->output.pcap.file) - 1);
			} else if (strcmp(ctx->key, "rotate_size") == 0) {
				cfg->output.pcap.rotate_size = parse_size(expanded);
			}
		} else if (strcmp(ctx->subsubsection, "database") == 0) {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->output.database.enabled = parse_bool(expanded);
			} else if (strcmp(ctx->key, "path") == 0) {
				strncpy(cfg->output.database.path, expanded,
				        sizeof(cfg->output.database.path) - 1);
			} else if (strcmp(ctx->key, "retention_days") == 0) {
				cfg->output.database.retention_days = atoi(expanded);
			}
		}
	}
	/* CLI configuration */
	else if (strcmp(section, "cli") == 0) {
		if (strcmp(ctx->key, "enabled") == 0) {
			cfg->cli.enabled = parse_bool(expanded);
		} else if (strcmp(ctx->key, "refresh_rate") == 0) {
			/* Parse milliseconds from string like "100ms" */
			cfg->cli.refresh_rate_ms = atoi(expanded);
		} else if (strcmp(ctx->key, "max_history") == 0) {
			cfg->cli.max_history = atoi(expanded);
		}
	}
	/* Web configuration */
	else if (strcmp(section, "web") == 0) {
		if (strcmp(ctx->subsubsection, "tls") == 0) {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->web.tls.enabled = parse_bool(expanded);
			} else if (strcmp(ctx->key, "cert") == 0) {
				strncpy(cfg->web.tls.cert_file, expanded,
				        sizeof(cfg->web.tls.cert_file) - 1);
			} else if (strcmp(ctx->key, "key") == 0) {
				strncpy(cfg->web.tls.key_file, expanded,
				        sizeof(cfg->web.tls.key_file) - 1);
			}
		} else {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->web.enabled = parse_bool(expanded);
			} else if (strcmp(ctx->key, "port") == 0) {
				cfg->web.port = atoi(expanded);
			}
		}
	}
	/* Metrics configuration */
	else if (strcmp(section, "metrics") == 0) {
		if (strcmp(ctx->key, "enabled") == 0) {
			cfg->metrics.enabled = parse_bool(expanded);
		} else if (strcmp(ctx->key, "port") == 0) {
			cfg->metrics.port = atoi(expanded);
		} else if (strcmp(ctx->key, "path") == 0) {
			strncpy(cfg->metrics.path, expanded,
			        sizeof(cfg->metrics.path) - 1);
		}
	}
	/* Plugins configuration */
	else if (strcmp(section, "plugins") == 0) {
		if (strcmp(ctx->key, "directory") == 0) {
			strncpy(cfg->plugins.directory, expanded,
			        sizeof(cfg->plugins.directory) - 1);
		} else if (strcmp(ctx->key, "enabled") == 0 && ctx->in_array) {
			if (cfg->plugins.enabled_count < 16) {
				strncpy(cfg->plugins.enabled_plugins[cfg->plugins.enabled_count],
				        expanded, NLMON_MAX_NAME - 1);
				cfg->plugins.enabled_count++;
			}
		}
	}
	/* Netlink configuration */
	else if (strcmp(section, "netlink") == 0) {
		/* Top-level netlink options (no subsubsection) */
		if (ctx->subsubsection[0] == '\0') {
			if (strcmp(ctx->key, "use_libnl") == 0) {
				cfg->netlink.use_libnl = parse_bool(expanded);
			}
		} else if (strcmp(ctx->subsubsection, "protocols") == 0) {
			if (strcmp(ctx->key, "route") == 0) {
				cfg->netlink.protocols.route = parse_bool(expanded);
			} else if (strcmp(ctx->key, "generic") == 0) {
				cfg->netlink.protocols.generic = parse_bool(expanded);
			} else if (strcmp(ctx->key, "sock_diag") == 0) {
				cfg->netlink.protocols.sock_diag = parse_bool(expanded);
			} else if (strcmp(ctx->key, "netfilter") == 0) {
				cfg->netlink.protocols.netfilter = parse_bool(expanded);
			}
		} else if (strcmp(ctx->subsubsection, "buffer_size") == 0) {
			if (strcmp(ctx->key, "receive") == 0) {
				cfg->netlink.buffer_size.receive = parse_size(expanded);
			} else if (strcmp(ctx->key, "send") == 0) {
				cfg->netlink.buffer_size.send = parse_size(expanded);
			}
		} else if (strcmp(ctx->subsubsection, "caching") == 0) {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->netlink.caching.enabled = parse_bool(expanded);
			} else if (strcmp(ctx->key, "link_cache") == 0) {
				cfg->netlink.caching.link_cache = parse_bool(expanded);
			} else if (strcmp(ctx->key, "addr_cache") == 0) {
				cfg->netlink.caching.addr_cache = parse_bool(expanded);
			} else if (strcmp(ctx->key, "route_cache") == 0) {
				cfg->netlink.caching.route_cache = parse_bool(expanded);
			}
		} else if (strcmp(ctx->subsubsection, "multicast_groups") == 0 && ctx->in_array) {
			if (cfg->netlink.multicast_groups.group_count < NLMON_MAX_MCAST_GROUPS) {
				strncpy(cfg->netlink.multicast_groups.groups[cfg->netlink.multicast_groups.group_count],
				        expanded, NLMON_MAX_NAME - 1);
				cfg->netlink.multicast_groups.group_count++;
			}
		} else if (strcmp(ctx->subsubsection, "generic_families") == 0 && ctx->in_array) {
			if (cfg->netlink.generic_families.family_count < NLMON_MAX_GENL_FAMILIES) {
				strncpy(cfg->netlink.generic_families.families[cfg->netlink.generic_families.family_count],
				        expanded, NLMON_MAX_NAME - 1);
				cfg->netlink.generic_families.family_count++;
			}
		}
	}
	/* Integration configuration */
	else if (strcmp(section, "integration") == 0) {
		if (strcmp(ctx->subsubsection, "kubernetes") == 0) {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->integration.kubernetes.enabled = parse_bool(expanded);
			} else if (strcmp(ctx->key, "kubeconfig") == 0) {
				strncpy(cfg->integration.kubernetes.kubeconfig, expanded,
				        sizeof(cfg->integration.kubernetes.kubeconfig) - 1);
			}
		} else if (strcmp(ctx->subsubsection, "docker") == 0) {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->integration.docker.enabled = parse_bool(expanded);
			} else if (strcmp(ctx->key, "socket") == 0) {
				strncpy(cfg->integration.docker.socket, expanded,
				        sizeof(cfg->integration.docker.socket) - 1);
			}
		} else if (strcmp(ctx->subsubsection, "syslog") == 0) {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->integration.syslog.enabled = parse_bool(expanded);
			} else if (strcmp(ctx->key, "server") == 0) {
				strncpy(cfg->integration.syslog.server, expanded,
				        sizeof(cfg->integration.syslog.server) - 1);
			} else if (strcmp(ctx->key, "protocol") == 0) {
				strncpy(cfg->integration.syslog.protocol, expanded,
				        sizeof(cfg->integration.syslog.protocol) - 1);
			}
		} else if (strcmp(ctx->subsubsection, "snmp") == 0) {
			if (strcmp(ctx->key, "enabled") == 0) {
				cfg->integration.snmp.enabled = parse_bool(expanded);
			} else if (strcmp(ctx->key, "trap_receivers") == 0 && ctx->in_array) {
				if (cfg->integration.snmp.receiver_count < NLMON_MAX_TRAP_RECEIVERS) {
					strncpy(cfg->integration.snmp.trap_receivers[cfg->integration.snmp.receiver_count],
					        expanded, 255);
					cfg->integration.snmp.receiver_count++;
				}
			}
		} else if (strcmp(ctx->subsubsection, "hooks") == 0 && ctx->in_array) {
			/* Parse hook configuration */
			if (cfg->integration.hook_count < NLMON_MAX_HOOKS) {
				struct nlmon_hook_config *hook = &cfg->integration.hooks[cfg->integration.hook_count];
				
				if (strcmp(ctx->key, "name") == 0) {
					strncpy(hook->name, expanded, NLMON_MAX_NAME - 1);
				} else if (strcmp(ctx->key, "script") == 0) {
					strncpy(hook->script, expanded, NLMON_MAX_PATH - 1);
				} else if (strcmp(ctx->key, "condition") == 0) {
					strncpy(hook->condition, expanded, NLMON_MAX_EXPRESSION - 1);
				} else if (strcmp(ctx->key, "timeout_ms") == 0) {
					hook->timeout_ms = (uint32_t)atoi(expanded);
				} else if (strcmp(ctx->key, "enabled") == 0) {
					hook->enabled = parse_bool(expanded);
				} else if (strcmp(ctx->key, "async") == 0) {
					hook->async = parse_bool(expanded);
				}
			}
		}
	}
	
	return NLMON_CONFIG_OK;
}

/* Parse YAML document */
static int parse_yaml_document(struct yaml_parse_ctx *ctx)
{
	yaml_event_t event;
	int done = 0;
	int in_mapping = 0;
	int mapping_level = 0;
	char last_key[128] = {0};
	
	while (!done) {
		if (!yaml_parser_parse(&ctx->parser, &event)) {
			fprintf(stderr, "YAML parser error: %s\n", ctx->parser.problem);
			return NLMON_CONFIG_ERR_PARSE_ERROR;
		}
		
		switch (event.type) {
		case YAML_STREAM_START_EVENT:
		case YAML_DOCUMENT_START_EVENT:
			break;
			
		case YAML_MAPPING_START_EVENT:
			in_mapping++;
			mapping_level++;
			/* If we have a pending key, it becomes the section/subsection/subsubsection */
			if (last_key[0] != '\0') {
				if (mapping_level == 2) {
					strncpy(ctx->subsection, last_key, sizeof(ctx->subsection) - 1);
					ctx->subsubsection[0] = '\0';
				} else if (mapping_level == 3) {
					strncpy(ctx->subsubsection, last_key, sizeof(ctx->subsubsection) - 1);
				}
				last_key[0] = '\0';
			}
			break;
			
		case YAML_MAPPING_END_EVENT:
			in_mapping--;
			mapping_level--;
			if (mapping_level == 2) {
				/* Exiting subsubsection */
				ctx->subsubsection[0] = '\0';
			} else if (mapping_level == 1) {
				/* Exiting subsection */
				ctx->subsection[0] = '\0';
			} else if (mapping_level == 0) {
				/* Exiting section */
				ctx->section[0] = '\0';
			} else if (mapping_level == 3 && ctx->in_array) {
				/* Exiting array item (e.g., hook object) */
				if (strcmp(ctx->subsubsection, "hooks") == 0) {
					if (ctx->config->integration.hook_count < NLMON_MAX_HOOKS)
						ctx->config->integration.hook_count++;
				}
			}
			break;
			
		case YAML_SEQUENCE_START_EVENT:
			ctx->in_array = true;
			ctx->array_index = 0;
			/* If we have a pending key, it becomes the subsubsection for array items */
			if (last_key[0] != '\0') {
				if (mapping_level == 2) {
					strncpy(ctx->subsubsection, last_key, sizeof(ctx->subsubsection) - 1);
				}
				last_key[0] = '\0';
			}
			break;
			
		case YAML_SEQUENCE_END_EVENT:
			ctx->in_array = false;
			ctx->array_index = 0;
			break;
			
		case YAML_SCALAR_EVENT:
			{
				const char *value = (const char *)event.data.scalar.value;
				
				/* If we're in an array and there's no pending key,
				 * treat scalars as array item values */
				if (ctx->in_array && last_key[0] == '\0') {
					/* This is an array item value */
					parse_scalar_value(ctx, value);
					ctx->array_index++;
				} else if (in_mapping && last_key[0] == '\0') {
					/* This is a key */
					strncpy(last_key, value, sizeof(last_key) - 1);
					
					if (mapping_level == 1) {
						/* Top-level section */
						strncpy(ctx->section, value, sizeof(ctx->section) - 1);
						ctx->subsection[0] = '\0';
						ctx->subsubsection[0] = '\0';
					} else if (mapping_level == 2) {
						/* Subsection */
						strncpy(ctx->subsection, value, sizeof(ctx->subsection) - 1);
						ctx->subsubsection[0] = '\0';
					} else if (mapping_level == 3) {
						/* Sub-subsection */
						strncpy(ctx->subsubsection, value, sizeof(ctx->subsubsection) - 1);
					} else if (mapping_level >= 4) {
						/* Key in sub-subsection */
						strncpy(ctx->key, value, sizeof(ctx->key) - 1);
					}
				} else {
					/* This is a value */
					if (mapping_level >= 2) {
						strncpy(ctx->key, last_key, sizeof(ctx->key) - 1);
					}
					
					parse_scalar_value(ctx, value);
					
					if (ctx->in_array)
						ctx->array_index++;
					
					last_key[0] = '\0';
				}
			}
			break;
			
		case YAML_STREAM_END_EVENT:
		case YAML_DOCUMENT_END_EVENT:
			done = 1;
			break;
			
		default:
			break;
		}
		
		yaml_event_delete(&event);
	}
	
	return NLMON_CONFIG_OK;
}

/* Load configuration from YAML file */
int nlmon_config_load(struct nlmon_config *config, const char *filename)
{
	struct yaml_parse_ctx ctx;
	int ret;
	
	if (!config || !filename)
		return NLMON_CONFIG_ERR_INVALID_VALUE;
	
	/* Initialize parser context */
	memset(&ctx, 0, sizeof(ctx));
	ctx.config = config;
	
	/* Open configuration file */
	ctx.file = fopen(filename, "r");
	if (!ctx.file) {
		fprintf(stderr, "Failed to open config file: %s\n", filename);
		return NLMON_CONFIG_ERR_FILE_NOT_FOUND;
	}
	
	/* Initialize YAML parser */
	if (!yaml_parser_initialize(&ctx.parser)) {
		fprintf(stderr, "Failed to initialize YAML parser\n");
		fclose(ctx.file);
		return NLMON_CONFIG_ERR_PARSE_ERROR;
	}
	
	yaml_parser_set_input_file(&ctx.parser, ctx.file);
	
	/* Parse YAML document */
	ret = parse_yaml_document(&ctx);
	
	/* Cleanup */
	yaml_parser_delete(&ctx.parser);
	fclose(ctx.file);
	
	if (ret != NLMON_CONFIG_OK) {
		fprintf(stderr, "Failed to parse configuration file: %s\n",
		        nlmon_config_error_string(ret));
		return ret;
	}
	
	return NLMON_CONFIG_OK;
}
