#ifndef CLI_INTEGRATION_H
#define CLI_INTEGRATION_H

#include <stdbool.h>

/* Initialize enhanced CLI interface */
int cli_enhanced_init(void);

/* Cleanup enhanced CLI interface */
void cli_enhanced_cleanup(void);

/* Log an event to the enhanced CLI */
void cli_enhanced_log_event(const char *event_type, const char *interface,
                            const char *message, const char *details_json);

/* Check if CLI is running */
bool cli_enhanced_is_running(void);

/* Check if CLI is paused */
bool cli_enhanced_is_paused(void);

/* Refresh CLI display */
void cli_enhanced_refresh(void);

#endif /* CLI_INTEGRATION_H */
