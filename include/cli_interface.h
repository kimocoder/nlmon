#ifndef CLI_INTERFACE_H
#define CLI_INTERFACE_H

#include <ncurses.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
struct nlmon_event;
struct nlmon_filter;

/* Panel types */
typedef enum {
    PANEL_EVENTS,
    PANEL_DETAILS,
    PANEL_FILTERS,
    PANEL_STATS,
    PANEL_STATUS,
    PANEL_COMMAND,
    PANEL_MAX
} panel_type_t;

/* Panel structure */
typedef struct cli_panel {
    WINDOW *win;
    panel_type_t type;
    int x, y, width, height;
    bool visible;
    bool has_focus;
    bool needs_refresh;
} cli_panel_t;

/* Event entry for display */
typedef struct cli_event_entry {
    char timestamp[32];
    char event_type[32];
    char interface[64];
    char message[256];
    char details_json[2048];
    int color_pair;
    struct cli_event_entry *next;
} cli_event_entry_t;

/* Sort order */
typedef enum {
    SORT_TIME_DESC,
    SORT_TIME_ASC,
    SORT_TYPE,
    SORT_INTERFACE
} sort_order_t;

/* Search state */
typedef struct {
    char query[256];
    bool active;
    bool regex_mode;
    int current_match;
    int total_matches;
    char history[10][256];
    int history_count;
    int history_index;
} search_state_t;

/* Filter state */
typedef struct {
    char expression[512];
    bool active;
    bool valid;
    char error_msg[256];
    struct nlmon_filter *compiled_filter;
} filter_state_t;

/* Export state */
typedef enum {
    EXPORT_FORMAT_TEXT,
    EXPORT_FORMAT_JSON,
    EXPORT_FORMAT_CSV
} export_format_t;

typedef struct {
    bool active;
    export_format_t format;
    char filename[256];
    int progress;
    int total;
} export_state_t;

/* CLI state */
typedef struct {
    cli_panel_t panels[PANEL_MAX];
    panel_type_t focused_panel;
    
    /* Event list */
    cli_event_entry_t *events;
    int event_count;
    int event_capacity;
    int event_scroll;
    int selected_event;
    
    /* Sort and filter */
    sort_order_t sort_order;
    filter_state_t filter;
    search_state_t search;
    export_state_t export;
    
    /* Color theme */
    int theme;
    bool colors_enabled;
    
    /* Statistics */
    unsigned long link_events;
    unsigned long route_events;
    unsigned long addr_events;
    unsigned long neigh_events;
    unsigned long rule_events;
    
    /* State flags */
    bool running;
    bool paused;
    bool show_help;
    bool show_filter_dialog;
    bool show_export_dialog;
    bool show_timestamps;
    
    /* Thread safety */
    pthread_mutex_t lock;
} cli_state_t;

/* CLI initialization and cleanup */
int cli_init(cli_state_t **state);
void cli_cleanup(cli_state_t *state);

/* Panel management */
int cli_create_panels(cli_state_t *state);
void cli_destroy_panels(cli_state_t *state);
void cli_resize_panels(cli_state_t *state);
void cli_set_focus(cli_state_t *state, panel_type_t panel);
void cli_refresh_panel(cli_state_t *state, panel_type_t panel);
void cli_refresh_all(cli_state_t *state);

/* Event management */
int cli_add_event(cli_state_t *state, const char *timestamp, const char *event_type,
                  const char *interface, const char *message, const char *details_json);
void cli_clear_events(cli_state_t *state);
void cli_sort_events(cli_state_t *state);
cli_event_entry_t *cli_get_selected_event(cli_state_t *state);

/* Display functions */
void cli_draw_events_panel(cli_state_t *state);
void cli_draw_details_panel(cli_state_t *state);
void cli_draw_filters_panel(cli_state_t *state);
void cli_draw_stats_panel(cli_state_t *state);
void cli_draw_status_panel(cli_state_t *state);
void cli_draw_command_panel(cli_state_t *state);

/* Input handling */
void cli_handle_input(cli_state_t *state, int ch);
void cli_handle_resize(cli_state_t *state);

/* Dialog functions */
void cli_show_help_dialog(cli_state_t *state);
void cli_show_context_help(cli_state_t *state);
void cli_show_general_help(WINDOW *help_win, int *line_ptr);
void cli_show_filter_dialog(cli_state_t *state);
void cli_show_export_dialog(cli_state_t *state);

/* Search functions */
void cli_start_search(cli_state_t *state);
void cli_search_next(cli_state_t *state);
void cli_search_prev(cli_state_t *state);
void cli_cancel_search(cli_state_t *state);

/* Filter functions */
int cli_apply_filter(cli_state_t *state, const char *expression);
void cli_clear_filter(cli_state_t *state);
void cli_save_filter(cli_state_t *state, const char *name);
void cli_load_filter(cli_state_t *state, const char *name);

/* Export functions */
int cli_export_events(cli_state_t *state, const char *filename, export_format_t format);

/* Color management */
void cli_init_colors(cli_state_t *state);
int cli_get_event_color(const char *event_type);
void cli_set_theme(cli_state_t *state, int theme);

/* Statistics update */
void cli_update_stats(cli_state_t *state, const char *event_type);

#endif /* CLI_INTERFACE_H */
