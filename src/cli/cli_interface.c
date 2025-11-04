#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <pthread.h>
#include <regex.h>
#include <time.h>
#include "cli_interface.h"

/* Color pairs */
#define COLOR_PAIR_NORMAL     1
#define COLOR_PAIR_LINK       2
#define COLOR_PAIR_ROUTE      3
#define COLOR_PAIR_ADDR       4
#define COLOR_PAIR_NEIGH      5
#define COLOR_PAIR_RULE       6
#define COLOR_PAIR_SELECTED   7
#define COLOR_PAIR_HIGHLIGHT  8
#define COLOR_PAIR_ERROR      9
#define COLOR_PAIR_SUCCESS    10

/* Initialize CLI state */
int cli_init(cli_state_t **state)
{
    cli_state_t *s;
    
    if (!state)
        return -1;
    
    s = calloc(1, sizeof(cli_state_t));
    if (!s)
        return -1;
    
    /* Initialize ncurses */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    
    /* Initialize colors */
    if (has_colors()) {
        start_color();
        s->colors_enabled = true;
        cli_init_colors(s);
    }
    
    /* Initialize mutex */
    pthread_mutex_init(&s->lock, NULL);
    
    /* Set defaults */
    s->event_capacity = 1000;
    s->sort_order = SORT_TIME_DESC;
    s->show_timestamps = true;
    s->running = true;
    s->focused_panel = PANEL_EVENTS;
    
    /* Create panels */
    if (cli_create_panels(s) < 0) {
        cli_cleanup(s);
        return -1;
    }
    
    *state = s;
    return 0;
}

/* Cleanup CLI */
void cli_cleanup(cli_state_t *state)
{
    if (!state)
        return;
    
    cli_destroy_panels(state);
    
    /* Free events */
    cli_event_entry_t *event = state->events;
    while (event) {
        cli_event_entry_t *next = event->next;
        free(event);
        event = next;
    }
    
    pthread_mutex_destroy(&state->lock);
    endwin();
    free(state);
}

/* Initialize color pairs */
void cli_init_colors(cli_state_t *state)
{
    init_pair(COLOR_PAIR_NORMAL, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_PAIR_LINK, COLOR_CYAN, COLOR_BLACK);
    init_pair(COLOR_PAIR_ROUTE, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_PAIR_ADDR, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_PAIR_NEIGH, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_PAIR_RULE, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_PAIR_SELECTED, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_PAIR_HIGHLIGHT, COLOR_BLACK, COLOR_YELLOW);
    init_pair(COLOR_PAIR_ERROR, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_PAIR_SUCCESS, COLOR_GREEN, COLOR_BLACK);
}

/* Get color for event type */
int cli_get_event_color(const char *event_type)
{
    if (!event_type)
        return COLOR_PAIR_NORMAL;
    
    if (strstr(event_type, "link") || strstr(event_type, "LINK"))
        return COLOR_PAIR_LINK;
    if (strstr(event_type, "route") || strstr(event_type, "ROUTE"))
        return COLOR_PAIR_ROUTE;
    if (strstr(event_type, "addr") || strstr(event_type, "ADDR"))
        return COLOR_PAIR_ADDR;
    if (strstr(event_type, "neigh") || strstr(event_type, "NEIGH"))
        return COLOR_PAIR_NEIGH;
    if (strstr(event_type, "rule") || strstr(event_type, "RULE"))
        return COLOR_PAIR_RULE;
    
    return COLOR_PAIR_NORMAL;
}

/* Create panels */
int cli_create_panels(cli_state_t *state)
{
    int max_y, max_x;
    
    getmaxyx(stdscr, max_y, max_x);
    
    /* Events panel (main area) */
    state->panels[PANEL_EVENTS].type = PANEL_EVENTS;
    state->panels[PANEL_EVENTS].x = 0;
    state->panels[PANEL_EVENTS].y = 0;
    state->panels[PANEL_EVENTS].width = max_x * 2 / 3;
    state->panels[PANEL_EVENTS].height = max_y - 7;
    state->panels[PANEL_EVENTS].visible = true;
    state->panels[PANEL_EVENTS].has_focus = true;
    state->panels[PANEL_EVENTS].win = newwin(
        state->panels[PANEL_EVENTS].height,
        state->panels[PANEL_EVENTS].width,
        state->panels[PANEL_EVENTS].y,
        state->panels[PANEL_EVENTS].x
    );
    
    /* Details panel (right side) */
    state->panels[PANEL_DETAILS].type = PANEL_DETAILS;
    state->panels[PANEL_DETAILS].x = max_x * 2 / 3;
    state->panels[PANEL_DETAILS].y = 0;
    state->panels[PANEL_DETAILS].width = max_x - (max_x * 2 / 3);
    state->panels[PANEL_DETAILS].height = max_y - 7;
    state->panels[PANEL_DETAILS].visible = true;
    state->panels[PANEL_DETAILS].win = newwin(
        state->panels[PANEL_DETAILS].height,
        state->panels[PANEL_DETAILS].width,
        state->panels[PANEL_DETAILS].y,
        state->panels[PANEL_DETAILS].x
    );
    
    /* Stats panel */
    state->panels[PANEL_STATS].type = PANEL_STATS;
    state->panels[PANEL_STATS].x = 0;
    state->panels[PANEL_STATS].y = max_y - 7;
    state->panels[PANEL_STATS].width = max_x;
    state->panels[PANEL_STATS].height = 4;
    state->panels[PANEL_STATS].visible = true;
    state->panels[PANEL_STATS].win = newwin(
        state->panels[PANEL_STATS].height,
        state->panels[PANEL_STATS].width,
        state->panels[PANEL_STATS].y,
        state->panels[PANEL_STATS].x
    );
    
    /* Command panel */
    state->panels[PANEL_COMMAND].type = PANEL_COMMAND;
    state->panels[PANEL_COMMAND].x = 0;
    state->panels[PANEL_COMMAND].y = max_y - 3;
    state->panels[PANEL_COMMAND].width = max_x;
    state->panels[PANEL_COMMAND].height = 3;
    state->panels[PANEL_COMMAND].visible = true;
    state->panels[PANEL_COMMAND].win = newwin(
        state->panels[PANEL_COMMAND].height,
        state->panels[PANEL_COMMAND].width,
        state->panels[PANEL_COMMAND].y,
        state->panels[PANEL_COMMAND].x
    );
    
    /* Check if all windows were created */
    for (int i = 0; i < PANEL_MAX; i++) {
        if (state->panels[i].visible && !state->panels[i].win)
            return -1;
    }
    
    return 0;
}

/* Destroy panels */
void cli_destroy_panels(cli_state_t *state)
{
    if (!state)
        return;
    
    for (int i = 0; i < PANEL_MAX; i++) {
        if (state->panels[i].win) {
            delwin(state->panels[i].win);
            state->panels[i].win = NULL;
        }
    }
}

/* Resize panels */
void cli_resize_panels(cli_state_t *state)
{
    if (!state)
        return;
    
    cli_destroy_panels(state);
    cli_create_panels(state);
    cli_refresh_all(state);
}

/* Set focus to a panel */
void cli_set_focus(cli_state_t *state, panel_type_t panel)
{
    if (!state || panel >= PANEL_MAX)
        return;
    
    /* Remove focus from current panel */
    if (state->focused_panel < PANEL_MAX)
        state->panels[state->focused_panel].has_focus = false;
    
    /* Set focus to new panel */
    state->focused_panel = panel;
    state->panels[panel].has_focus = true;
    
    cli_refresh_all(state);
}

/* Refresh a single panel */
void cli_refresh_panel(cli_state_t *state, panel_type_t panel)
{
    if (!state || panel >= PANEL_MAX)
        return;
    
    if (!state->panels[panel].visible || !state->panels[panel].win)
        return;
    
    switch (panel) {
    case PANEL_EVENTS:
        cli_draw_events_panel(state);
        break;
    case PANEL_DETAILS:
        cli_draw_details_panel(state);
        break;
    case PANEL_STATS:
        cli_draw_stats_panel(state);
        break;
    case PANEL_COMMAND:
        cli_draw_command_panel(state);
        break;
    default:
        break;
    }
    
    state->panels[panel].needs_refresh = false;
}

/* Refresh all panels */
void cli_refresh_all(cli_state_t *state)
{
    if (!state)
        return;
    
    pthread_mutex_lock(&state->lock);
    
    for (int i = 0; i < PANEL_MAX; i++) {
        if (state->panels[i].visible)
            cli_refresh_panel(state, i);
    }
    
    pthread_mutex_unlock(&state->lock);
}

/* Add event to list */
int cli_add_event(cli_state_t *state, const char *timestamp, const char *event_type,
                  const char *interface, const char *message, const char *details_json)
{
    cli_event_entry_t *event;
    
    if (!state)
        return -1;
    
    pthread_mutex_lock(&state->lock);
    
    /* Check capacity */
    if (state->event_count >= state->event_capacity) {
        /* Remove oldest event */
        cli_event_entry_t *oldest = state->events;
        if (oldest) {
            state->events = oldest->next;
            free(oldest);
            state->event_count--;
        }
    }
    
    /* Create new event */
    event = calloc(1, sizeof(cli_event_entry_t));
    if (!event) {
        pthread_mutex_unlock(&state->lock);
        return -1;
    }
    
    /* Copy data */
    snprintf(event->timestamp, sizeof(event->timestamp), "%s", timestamp ? timestamp : "");
    snprintf(event->event_type, sizeof(event->event_type), "%s", event_type ? event_type : "");
    snprintf(event->interface, sizeof(event->interface), "%s", interface ? interface : "");
    snprintf(event->message, sizeof(event->message), "%s", message ? message : "");
    snprintf(event->details_json, sizeof(event->details_json), "%s", details_json ? details_json : "{}");
    
    event->color_pair = cli_get_event_color(event_type);
    
    /* Add to end of list */
    if (!state->events) {
        state->events = event;
    } else {
        cli_event_entry_t *last = state->events;
        while (last->next)
            last = last->next;
        last->next = event;
    }
    
    state->event_count++;
    
    /* Update statistics */
    cli_update_stats(state, event_type);
    
    /* Mark panels for refresh */
    state->panels[PANEL_EVENTS].needs_refresh = true;
    state->panels[PANEL_STATS].needs_refresh = true;
    
    pthread_mutex_unlock(&state->lock);
    
    return 0;
}

/* Clear all events */
void cli_clear_events(cli_state_t *state)
{
    if (!state)
        return;
    
    pthread_mutex_lock(&state->lock);
    
    cli_event_entry_t *event = state->events;
    while (event) {
        cli_event_entry_t *next = event->next;
        free(event);
        event = next;
    }
    
    state->events = NULL;
    state->event_count = 0;
    state->event_scroll = 0;
    state->selected_event = 0;
    
    pthread_mutex_unlock(&state->lock);
    
    cli_refresh_all(state);
}

/* Get selected event */
cli_event_entry_t *cli_get_selected_event(cli_state_t *state)
{
    if (!state || state->selected_event < 0)
        return NULL;
    
    cli_event_entry_t *event = state->events;
    int index = 0;
    
    while (event && index < state->selected_event) {
        event = event->next;
        index++;
    }
    
    return event;
}

/* Update statistics */
void cli_update_stats(cli_state_t *state, const char *event_type)
{
    if (!state || !event_type)
        return;
    
    if (strstr(event_type, "link") || strstr(event_type, "LINK"))
        state->link_events++;
    else if (strstr(event_type, "route") || strstr(event_type, "ROUTE"))
        state->route_events++;
    else if (strstr(event_type, "addr") || strstr(event_type, "ADDR"))
        state->addr_events++;
    else if (strstr(event_type, "neigh") || strstr(event_type, "NEIGH"))
        state->neigh_events++;
    else if (strstr(event_type, "rule") || strstr(event_type, "RULE"))
        state->rule_events++;
}

/* Draw events panel */
void cli_draw_events_panel(cli_state_t *state)
{
    WINDOW *win;
    int height, width;
    int display_lines;
    int start_idx;
    cli_event_entry_t *event;
    int index, line;
    
    if (!state || !state->panels[PANEL_EVENTS].win)
        return;
    
    win = state->panels[PANEL_EVENTS].win;
    getmaxyx(win, height, width);
    
    werase(win);
    box(win, 0, 0);
    
    /* Draw title with focus indicator */
    if (state->panels[PANEL_EVENTS].has_focus)
        wattron(win, A_BOLD);
    mvwprintw(win, 0, 2, " Network Events ");
    if (state->panels[PANEL_EVENTS].has_focus)
        wattroff(win, A_BOLD);
    
    /* Show filter status */
    if (state->filter.active) {
        wattron(win, COLOR_PAIR(COLOR_PAIR_SUCCESS));
        mvwprintw(win, 0, width - 12, " [FILTER] ");
        wattroff(win, COLOR_PAIR(COLOR_PAIR_SUCCESS));
    }
    
    /* Show search status */
    if (state->search.active) {
        wattron(win, COLOR_PAIR(COLOR_PAIR_HIGHLIGHT));
        mvwprintw(win, 0, width - 22, " [SEARCH] ");
        wattroff(win, COLOR_PAIR(COLOR_PAIR_HIGHLIGHT));
    }
    
    /* Show paused status */
    if (state->paused) {
        wattron(win, COLOR_PAIR(COLOR_PAIR_ERROR));
        mvwprintw(win, 0, width - 32, " [PAUSED] ");
        wattroff(win, COLOR_PAIR(COLOR_PAIR_ERROR));
    }
    
    display_lines = height - 2;
    
    /* Calculate starting index for scrolling */
    start_idx = state->event_count - display_lines - state->event_scroll;
    if (start_idx < 0)
        start_idx = 0;
    
    /* Find starting event */
    event = state->events;
    index = 0;
    while (event && index < start_idx) {
        event = event->next;
        index++;
    }
    
    /* Draw events */
    line = 1;
    while (event && line < height - 1) {
        bool is_selected = (index == state->selected_event);
        
        if (is_selected) {
            wattron(win, COLOR_PAIR(COLOR_PAIR_SELECTED));
        } else if (state->colors_enabled) {
            wattron(win, COLOR_PAIR(event->color_pair));
        }
        
        /* Format event line */
        char line_buf[512];
        if (state->show_timestamps) {
            snprintf(line_buf, sizeof(line_buf), "%-10s %-12s %-15s %s",
                    event->timestamp, event->event_type, event->interface, event->message);
        } else {
            snprintf(line_buf, sizeof(line_buf), "%-12s %-15s %s",
                    event->event_type, event->interface, event->message);
        }
        
        mvwprintw(win, line, 2, "%.*s", width - 4, line_buf);
        
        if (is_selected) {
            wattroff(win, COLOR_PAIR(COLOR_PAIR_SELECTED));
        } else if (state->colors_enabled) {
            wattroff(win, COLOR_PAIR(event->color_pair));
        }
        
        event = event->next;
        index++;
        line++;
    }
    
    /* Show scroll indicator */
    if (state->event_count > display_lines) {
        mvwprintw(win, height - 1, width - 15, " %d/%d ", 
                 state->event_count - state->event_scroll, state->event_count);
    }
    
    wrefresh(win);
}

/* Draw details panel */
void cli_draw_details_panel(cli_state_t *state)
{
    WINDOW *win;
    int height, width;
    cli_event_entry_t *event;
    
    if (!state || !state->panels[PANEL_DETAILS].win)
        return;
    
    win = state->panels[PANEL_DETAILS].win;
    getmaxyx(win, height, width);
    
    werase(win);
    box(win, 0, 0);
    
    /* Draw title with focus indicator */
    if (state->panels[PANEL_DETAILS].has_focus)
        wattron(win, A_BOLD);
    mvwprintw(win, 0, 2, " Event Details ");
    if (state->panels[PANEL_DETAILS].has_focus)
        wattroff(win, A_BOLD);
    
    /* Get selected event */
    event = cli_get_selected_event(state);
    if (!event) {
        mvwprintw(win, height / 2, (width - 20) / 2, "No event selected");
        wrefresh(win);
        return;
    }
    
    /* Display event details */
    int line = 2;
    
    wattron(win, A_BOLD);
    mvwprintw(win, line++, 2, "Timestamp:");
    wattroff(win, A_BOLD);
    mvwprintw(win, line++, 4, "%s", event->timestamp);
    line++;
    
    wattron(win, A_BOLD);
    mvwprintw(win, line++, 2, "Event Type:");
    wattroff(win, A_BOLD);
    mvwprintw(win, line++, 4, "%s", event->event_type);
    line++;
    
    wattron(win, A_BOLD);
    mvwprintw(win, line++, 2, "Interface:");
    wattroff(win, A_BOLD);
    mvwprintw(win, line++, 4, "%s", event->interface);
    line++;
    
    wattron(win, A_BOLD);
    mvwprintw(win, line++, 2, "Message:");
    wattroff(win, A_BOLD);
    
    /* Word wrap message */
    char *msg = event->message;
    int msg_width = width - 6;
    while (*msg && line < height - 2) {
        mvwprintw(win, line++, 4, "%.*s", msg_width, msg);
        msg += msg_width;
        if (*msg == '\0')
            break;
    }
    line++;
    
    /* Display JSON details */
    if (line < height - 2) {
        wattron(win, A_BOLD);
        mvwprintw(win, line++, 2, "Details (JSON):");
        wattroff(win, A_BOLD);
        
        /* Simple JSON formatting - display line by line */
        char *json = event->details_json;
        int json_width = width - 6;
        while (*json && line < height - 2) {
            mvwprintw(win, line++, 4, "%.*s", json_width, json);
            json += json_width;
            if (*json == '\0')
                break;
        }
    }
    
    wrefresh(win);
}

/* Draw stats panel */
void cli_draw_stats_panel(cli_state_t *state)
{
    WINDOW *win;
    int width;
    unsigned long total;
    
    if (!state || !state->panels[PANEL_STATS].win)
        return;
    
    win = state->panels[PANEL_STATS].win;
    getmaxyx(win, (int){0}, width);
    
    werase(win);
    box(win, 0, 0);
    
    mvwprintw(win, 0, 2, " Statistics ");
    
    total = state->link_events + state->route_events + state->addr_events +
            state->neigh_events + state->rule_events;
    
    /* First line of stats */
    mvwprintw(win, 1, 2, "Links: ");
    wattron(win, COLOR_PAIR(COLOR_PAIR_LINK));
    wprintw(win, "%-8lu", state->link_events);
    wattroff(win, COLOR_PAIR(COLOR_PAIR_LINK));
    
    wprintw(win, " Routes: ");
    wattron(win, COLOR_PAIR(COLOR_PAIR_ROUTE));
    wprintw(win, "%-8lu", state->route_events);
    wattroff(win, COLOR_PAIR(COLOR_PAIR_ROUTE));
    
    wprintw(win, " Addrs: ");
    wattron(win, COLOR_PAIR(COLOR_PAIR_ADDR));
    wprintw(win, "%-8lu", state->addr_events);
    wattroff(win, COLOR_PAIR(COLOR_PAIR_ADDR));
    
    /* Second line of stats */
    mvwprintw(win, 2, 2, "Neigh: ");
    wattron(win, COLOR_PAIR(COLOR_PAIR_NEIGH));
    wprintw(win, "%-8lu", state->neigh_events);
    wattroff(win, COLOR_PAIR(COLOR_PAIR_NEIGH));
    
    wprintw(win, " Rules:  ");
    wattron(win, COLOR_PAIR(COLOR_PAIR_RULE));
    wprintw(win, "%-8lu", state->rule_events);
    wattroff(win, COLOR_PAIR(COLOR_PAIR_RULE));
    
    wprintw(win, " Total:  ");
    wattron(win, A_BOLD);
    wprintw(win, "%-8lu", total);
    wattroff(win, A_BOLD);
    
    wrefresh(win);
}

/* Draw command panel */
void cli_draw_command_panel(cli_state_t *state)
{
    WINDOW *win;
    int width;
    
    if (!state || !state->panels[PANEL_COMMAND].win)
        return;
    
    win = state->panels[PANEL_COMMAND].win;
    getmaxyx(win, (int){0}, width);
    
    werase(win);
    box(win, 0, 0);
    
    mvwprintw(win, 0, 2, " Commands ");
    
    /* Show search input if active */
    if (state->search.active) {
        mvwprintw(win, 1, 2, "Search: %s", state->search.query);
        if (state->search.total_matches > 0) {
            mvwprintw(win, 1, width - 20, " %d/%d matches ",
                     state->search.current_match + 1, state->search.total_matches);
        }
    } else {
        /* Show key bindings */
        mvwprintw(win, 1, 2, "q:Quit h:Help c:Clear /:Search f:Filter s:Sort e:Export d:Details p:Pause");
    }
    
    wrefresh(win);
}

/* Handle resize */
void cli_handle_resize(cli_state_t *state)
{
    if (!state)
        return;
    
    endwin();
    refresh();
    cli_resize_panels(state);
}

/* Handle input */
void cli_handle_input(cli_state_t *state, int ch)
{
    if (!state)
        return;
    
    /* Handle search mode */
    if (state->search.active) {
        switch (ch) {
        case 27: /* ESC */
        case KEY_F(10):
            cli_cancel_search(state);
            break;
        case '\n':
        case KEY_ENTER:
            cli_search_next(state);
            break;
        case KEY_BACKSPACE:
        case 127:
        case '\b':
            {
                int len = strlen(state->search.query);
                if (len > 0)
                    state->search.query[len - 1] = '\0';
                cli_refresh_panel(state, PANEL_COMMAND);
            }
            break;
        default:
            if (ch >= 32 && ch < 127) {
                int len = strlen(state->search.query);
                if (len < (int)sizeof(state->search.query) - 1) {
                    state->search.query[len] = ch;
                    state->search.query[len + 1] = '\0';
                    cli_refresh_panel(state, PANEL_COMMAND);
                }
            }
            break;
        }
        return;
    }
    
    /* Normal mode input handling */
    switch (ch) {
    case 'q':
    case 'Q':
        state->running = false;
        break;
        
    case 'h':
    case 'H':
        cli_show_help_dialog(state);
        break;
        
    case KEY_F(1):
        cli_show_context_help(state);
        break;
        
    case 'c':
    case 'C':
        cli_clear_events(state);
        break;
        
    case '/':
        cli_start_search(state);
        break;
        
    case 'n':
        cli_search_next(state);
        break;
        
    case 'N':
        cli_search_prev(state);
        break;
        
    case 'f':
    case 'F':
        cli_show_filter_dialog(state);
        break;
        
    case 's':
    case 'S':
        /* Cycle through sort orders */
        state->sort_order = (state->sort_order + 1) % 4;
        cli_sort_events(state);
        cli_refresh_panel(state, PANEL_EVENTS);
        break;
        
    case 'e':
    case 'E':
        cli_show_export_dialog(state);
        break;
        
    case 'd':
    case 'D':
    case '\n':
    case KEY_ENTER:
        /* Toggle details panel focus */
        if (state->focused_panel == PANEL_EVENTS)
            cli_set_focus(state, PANEL_DETAILS);
        else
            cli_set_focus(state, PANEL_EVENTS);
        break;
        
    case 'p':
    case 'P':
        state->paused = !state->paused;
        cli_refresh_panel(state, PANEL_EVENTS);
        break;
        
    case 't':
    case 'T':
        state->show_timestamps = !state->show_timestamps;
        cli_refresh_panel(state, PANEL_EVENTS);
        break;
        
    case KEY_UP:
        if (state->selected_event > 0) {
            state->selected_event--;
            if (state->selected_event < state->event_count - state->event_scroll - 
                (state->panels[PANEL_EVENTS].height - 2)) {
                state->event_scroll++;
            }
        }
        cli_refresh_panel(state, PANEL_EVENTS);
        cli_refresh_panel(state, PANEL_DETAILS);
        break;
        
    case KEY_DOWN:
        if (state->selected_event < state->event_count - 1) {
            state->selected_event++;
            if (state->event_scroll > 0)
                state->event_scroll--;
        }
        cli_refresh_panel(state, PANEL_EVENTS);
        cli_refresh_panel(state, PANEL_DETAILS);
        break;
        
    case KEY_PPAGE: /* Page Up */
        {
            int page_size = state->panels[PANEL_EVENTS].height - 2;
            state->event_scroll += page_size;
            if (state->event_scroll > state->event_count - 1)
                state->event_scroll = state->event_count - 1;
            cli_refresh_panel(state, PANEL_EVENTS);
        }
        break;
        
    case KEY_NPAGE: /* Page Down */
        {
            int page_size = state->panels[PANEL_EVENTS].height - 2;
            state->event_scroll -= page_size;
            if (state->event_scroll < 0)
                state->event_scroll = 0;
            cli_refresh_panel(state, PANEL_EVENTS);
        }
        break;
        
    case KEY_HOME:
        state->event_scroll = state->event_count - 1;
        state->selected_event = 0;
        cli_refresh_panel(state, PANEL_EVENTS);
        cli_refresh_panel(state, PANEL_DETAILS);
        break;
        
    case KEY_END:
        state->event_scroll = 0;
        state->selected_event = state->event_count - 1;
        cli_refresh_panel(state, PANEL_EVENTS);
        cli_refresh_panel(state, PANEL_DETAILS);
        break;
        
    case KEY_RESIZE:
        cli_handle_resize(state);
        break;
    }
}

/* Show context-sensitive help based on current panel */
void cli_show_context_help(cli_state_t *state)
{
    WINDOW *help_win;
    int max_y, max_x;
    
    if (!state)
        return;
    
    getmaxyx(stdscr, max_y, max_x);
    help_win = newwin(max_y - 4, max_x - 4, 2, 2);
    
    if (!help_win)
        return;
    
    box(help_win, 0, 0);
    wattron(help_win, A_BOLD);
    
    int line = 2;
    
    /* Context-sensitive help based on focused panel */
    switch (state->focused_panel) {
    case PANEL_EVENTS:
        mvwprintw(help_win, 0, 2, " Help - Events Panel ");
        wattroff(help_win, A_BOLD);
        
        mvwprintw(help_win, line++, 4, "Events Panel - Network Event List");
        mvwprintw(help_win, line++, 4, "==================================");
        line++;
        
        mvwprintw(help_win, line++, 4, "This panel displays real-time network events captured from");
        mvwprintw(help_win, line++, 4, "the kernel netlink interface.");
        line++;
        
        wattron(help_win, A_BOLD);
        mvwprintw(help_win, line++, 4, "Navigation:");
        wattroff(help_win, A_BOLD);
        mvwprintw(help_win, line++, 6, "UP/DOWN     - Navigate through events");
        mvwprintw(help_win, line++, 6, "PgUp/PgDn   - Scroll page up/down");
        mvwprintw(help_win, line++, 6, "Home        - Jump to first event");
        mvwprintw(help_win, line++, 6, "End         - Jump to last event");
        line++;
        
        wattron(help_win, A_BOLD);
        mvwprintw(help_win, line++, 4, "Actions:");
        wattroff(help_win, A_BOLD);
        mvwprintw(help_win, line++, 6, "Enter/d     - View event details");
        mvwprintw(help_win, line++, 6, "/           - Search events");
        mvwprintw(help_win, line++, 6, "f           - Apply filter");
        mvwprintw(help_win, line++, 6, "s           - Change sort order");
        mvwprintw(help_win, line++, 6, "c           - Clear all events");
        mvwprintw(help_win, line++, 6, "p           - Pause/Resume capture");
        mvwprintw(help_win, line++, 6, "t           - Toggle timestamps");
        mvwprintw(help_win, line++, 6, "e           - Export events");
        break;
        
    case PANEL_DETAILS:
        mvwprintw(help_win, 0, 2, " Help - Details Panel ");
        wattroff(help_win, A_BOLD);
        
        mvwprintw(help_win, line++, 4, "Details Panel - Event Information");
        mvwprintw(help_win, line++, 4, "==================================");
        line++;
        
        mvwprintw(help_win, line++, 4, "This panel shows detailed information about the currently");
        mvwprintw(help_win, line++, 4, "selected event, including all metadata and JSON details.");
        line++;
        
        wattron(help_win, A_BOLD);
        mvwprintw(help_win, line++, 4, "Information Displayed:");
        wattroff(help_win, A_BOLD);
        mvwprintw(help_win, line++, 6, "- Event timestamp");
        mvwprintw(help_win, line++, 6, "- Event type (RTM_NEWLINK, etc.)");
        mvwprintw(help_win, line++, 6, "- Interface name");
        mvwprintw(help_win, line++, 6, "- Event message");
        mvwprintw(help_win, line++, 6, "- Full JSON details");
        line++;
        
        wattron(help_win, A_BOLD);
        mvwprintw(help_win, line++, 4, "Actions:");
        wattroff(help_win, A_BOLD);
        mvwprintw(help_win, line++, 6, "Enter/d     - Return to events panel");
        mvwprintw(help_win, line++, 6, "UP/DOWN     - Navigate to prev/next event");
        break;
        
    case PANEL_STATS:
        mvwprintw(help_win, 0, 2, " Help - Statistics Panel ");
        wattroff(help_win, A_BOLD);
        
        mvwprintw(help_win, line++, 4, "Statistics Panel - Event Counters");
        mvwprintw(help_win, line++, 4, "==================================");
        line++;
        
        mvwprintw(help_win, line++, 4, "This panel displays cumulative statistics for all captured");
        mvwprintw(help_win, line++, 4, "network events, categorized by type.");
        line++;
        
        wattron(help_win, A_BOLD);
        mvwprintw(help_win, line++, 4, "Event Categories:");
        wattroff(help_win, A_BOLD);
        
        wattron(help_win, COLOR_PAIR(COLOR_PAIR_LINK));
        mvwprintw(help_win, line++, 6, "Links  - Interface state changes (up/down)");
        wattroff(help_win, COLOR_PAIR(COLOR_PAIR_LINK));
        
        wattron(help_win, COLOR_PAIR(COLOR_PAIR_ROUTE));
        mvwprintw(help_win, line++, 6, "Routes - Routing table modifications");
        wattroff(help_win, COLOR_PAIR(COLOR_PAIR_ROUTE));
        
        wattron(help_win, COLOR_PAIR(COLOR_PAIR_ADDR));
        mvwprintw(help_win, line++, 6, "Addrs  - IP address assignments");
        wattroff(help_win, COLOR_PAIR(COLOR_PAIR_ADDR));
        
        wattron(help_win, COLOR_PAIR(COLOR_PAIR_NEIGH));
        mvwprintw(help_win, line++, 6, "Neigh  - Neighbor table updates (ARP/NDP)");
        wattroff(help_win, COLOR_PAIR(COLOR_PAIR_NEIGH));
        
        wattron(help_win, COLOR_PAIR(COLOR_PAIR_RULE));
        mvwprintw(help_win, line++, 6, "Rules  - Routing policy rule changes");
        wattroff(help_win, COLOR_PAIR(COLOR_PAIR_RULE));
        break;
        
    default:
        mvwprintw(help_win, 0, 2, " Help - General ");
        wattroff(help_win, A_BOLD);
        cli_show_general_help(help_win, &line);
        break;
    }
    
    line += 2;
    wattron(help_win, A_BOLD);
    mvwprintw(help_win, line++, 4, "Press 'h' again for general help, any other key to close");
    wattroff(help_win, A_BOLD);
    
    wrefresh(help_win);
    nodelay(stdscr, FALSE);
    int ch = getch();
    nodelay(stdscr, TRUE);
    
    /* If 'h' pressed, show general help */
    if (ch == 'h' || ch == 'H') {
        delwin(help_win);
        cli_show_help_dialog(state);
    } else {
        delwin(help_win);
        cli_refresh_all(state);
    }
}

/* Show general help dialog */
void cli_show_general_help(WINDOW *help_win, int *line_ptr)
{
    int line = *line_ptr;
    
    mvwprintw(help_win, line++, 4, "nlmon - Enhanced Network Link Monitor");
    mvwprintw(help_win, line++, 4, "=====================================");
    line++;
    
    wattron(help_win, A_BOLD);
    mvwprintw(help_win, line++, 4, "Navigation:");
    wattroff(help_win, A_BOLD);
    mvwprintw(help_win, line++, 6, "UP/DOWN     - Navigate events");
    mvwprintw(help_win, line++, 6, "PgUp/PgDn   - Page up/down");
    mvwprintw(help_win, line++, 6, "Home/End    - Jump to first/last event");
    mvwprintw(help_win, line++, 6, "Enter/d     - Toggle details panel");
    mvwprintw(help_win, line++, 6, "Tab         - Switch between panels");
    line++;
    
    wattron(help_win, A_BOLD);
    mvwprintw(help_win, line++, 4, "Commands:");
    wattroff(help_win, A_BOLD);
    mvwprintw(help_win, line++, 6, "q           - Quit application");
    mvwprintw(help_win, line++, 6, "h/F1        - Show context-sensitive help");
    mvwprintw(help_win, line++, 6, "c           - Clear event log");
    mvwprintw(help_win, line++, 6, "p/Space     - Pause/Resume monitoring");
    mvwprintw(help_win, line++, 6, "t           - Toggle timestamps");
    mvwprintw(help_win, line++, 6, "r           - Reload configuration");
    line++;
    
    wattron(help_win, A_BOLD);
    mvwprintw(help_win, line++, 4, "Search:");
    wattroff(help_win, A_BOLD);
    mvwprintw(help_win, line++, 6, "/           - Start search");
    mvwprintw(help_win, line++, 6, "n           - Next match");
    mvwprintw(help_win, line++, 6, "N           - Previous match");
    mvwprintw(help_win, line++, 6, "ESC         - Cancel search");
    line++;
    
    wattron(help_win, A_BOLD);
    mvwprintw(help_win, line++, 4, "Filtering & Export:");
    wattroff(help_win, A_BOLD);
    mvwprintw(help_win, line++, 6, "f           - Open filter dialog");
    mvwprintw(help_win, line++, 6, "s           - Cycle sort order");
    mvwprintw(help_win, line++, 6, "e           - Export events");
    
    *line_ptr = line;
}

/* Show help dialog (full help) */
void cli_show_help_dialog(cli_state_t *state)
{
    WINDOW *help_win;
    int max_y, max_x;
    
    if (!state)
        return;
    
    getmaxyx(stdscr, max_y, max_x);
    help_win = newwin(max_y - 4, max_x - 4, 2, 2);
    
    if (!help_win)
        return;
    
    box(help_win, 0, 0);
    wattron(help_win, A_BOLD);
    mvwprintw(help_win, 0, 2, " nlmon Help ");
    wattroff(help_win, A_BOLD);
    
    int line = 2;
    cli_show_general_help(help_win, &line);
    
    line++;
    wattron(help_win, A_BOLD);
    mvwprintw(help_win, line++, 4, "Event Types:");
    wattroff(help_win, A_BOLD);
    
    wattron(help_win, COLOR_PAIR(COLOR_PAIR_LINK));
    mvwprintw(help_win, line++, 6, "LINK   - Interface changes (up/down, flags)");
    wattroff(help_win, COLOR_PAIR(COLOR_PAIR_LINK));
    
    wattron(help_win, COLOR_PAIR(COLOR_PAIR_ROUTE));
    mvwprintw(help_win, line++, 6, "ROUTE  - Routing table changes");
    wattroff(help_win, COLOR_PAIR(COLOR_PAIR_ROUTE));
    
    wattron(help_win, COLOR_PAIR(COLOR_PAIR_ADDR));
    mvwprintw(help_win, line++, 6, "ADDR   - Address assignments/removals");
    wattroff(help_win, COLOR_PAIR(COLOR_PAIR_ADDR));
    
    wattron(help_win, COLOR_PAIR(COLOR_PAIR_NEIGH));
    mvwprintw(help_win, line++, 6, "NEIGH  - Neighbor table updates (ARP/NDP)");
    wattroff(help_win, COLOR_PAIR(COLOR_PAIR_NEIGH));
    
    wattron(help_win, COLOR_PAIR(COLOR_PAIR_RULE));
    mvwprintw(help_win, line++, 6, "RULE   - Routing policy rule changes");
    wattroff(help_win, COLOR_PAIR(COLOR_PAIR_RULE));
    
    line += 2;
    wattron(help_win, A_BOLD);
    mvwprintw(help_win, line++, 4, "Tips:");
    wattroff(help_win, A_BOLD);
    mvwprintw(help_win, line++, 6, "- Use filters to focus on specific events");
    mvwprintw(help_win, line++, 6, "- Press F1 for context-sensitive help");
    mvwprintw(help_win, line++, 6, "- Export events for offline analysis");
    mvwprintw(help_win, line++, 6, "- Check man pages: man nlmon, man nlmon.conf");
    
    line += 2;
    mvwprintw(help_win, line++, 4, "Press any key to close...");
    
    wrefresh(help_win);
    nodelay(stdscr, FALSE);
    getch();
    nodelay(stdscr, TRUE);
    delwin(help_win);
    cli_refresh_all(state);
}

/* Start search */
void cli_start_search(cli_state_t *state)
{
    if (!state)
        return;
    
    state->search.active = true;
    state->search.query[0] = '\0';
    state->search.current_match = 0;
    state->search.total_matches = 0;
    
    cli_refresh_panel(state, PANEL_COMMAND);
}

/* Cancel search */
void cli_cancel_search(cli_state_t *state)
{
    if (!state)
        return;
    
    state->search.active = false;
    state->search.query[0] = '\0';
    
    cli_refresh_panel(state, PANEL_COMMAND);
    cli_refresh_panel(state, PANEL_EVENTS);
}

/* Search next */
void cli_search_next(cli_state_t *state)
{
    if (!state || !state->search.query[0])
        return;
    
    /* Simple substring search */
    cli_event_entry_t *event = state->events;
    int index = 0;
    int found_index = -1;
    
    /* Start from current position + 1 */
    while (event && index <= state->selected_event) {
        event = event->next;
        index++;
    }
    
    /* Search forward */
    while (event) {
        if (strstr(event->message, state->search.query) ||
            strstr(event->event_type, state->search.query) ||
            strstr(event->interface, state->search.query)) {
            found_index = index;
            break;
        }
        event = event->next;
        index++;
    }
    
    /* Wrap around if not found */
    if (found_index < 0) {
        event = state->events;
        index = 0;
        while (event && index <= state->selected_event) {
            if (strstr(event->message, state->search.query) ||
                strstr(event->event_type, state->search.query) ||
                strstr(event->interface, state->search.query)) {
                found_index = index;
                break;
            }
            event = event->next;
            index++;
        }
    }
    
    if (found_index >= 0) {
        state->selected_event = found_index;
        /* Adjust scroll to show selected event */
        int display_lines = state->panels[PANEL_EVENTS].height - 2;
        state->event_scroll = state->event_count - found_index - display_lines / 2;
        if (state->event_scroll < 0)
            state->event_scroll = 0;
        if (state->event_scroll > state->event_count - display_lines)
            state->event_scroll = state->event_count - display_lines;
        
        cli_refresh_panel(state, PANEL_EVENTS);
        cli_refresh_panel(state, PANEL_DETAILS);
    }
}

/* Search previous */
void cli_search_prev(cli_state_t *state)
{
    if (!state || !state->search.query[0])
        return;
    
    /* Search backward from current position */
    cli_event_entry_t *event = state->events;
    int index = 0;
    int found_index = -1;
    
    /* Search up to current position - 1 */
    while (event && index < state->selected_event) {
        if (strstr(event->message, state->search.query) ||
            strstr(event->event_type, state->search.query) ||
            strstr(event->interface, state->search.query)) {
            found_index = index;
        }
        event = event->next;
        index++;
    }
    
    /* Wrap around if not found */
    if (found_index < 0) {
        while (event) {
            if (strstr(event->message, state->search.query) ||
                strstr(event->event_type, state->search.query) ||
                strstr(event->interface, state->search.query)) {
                found_index = index;
            }
            event = event->next;
            index++;
        }
    }
    
    if (found_index >= 0) {
        state->selected_event = found_index;
        /* Adjust scroll to show selected event */
        int display_lines = state->panels[PANEL_EVENTS].height - 2;
        state->event_scroll = state->event_count - found_index - display_lines / 2;
        if (state->event_scroll < 0)
            state->event_scroll = 0;
        if (state->event_scroll > state->event_count - display_lines)
            state->event_scroll = state->event_count - display_lines;
        
        cli_refresh_panel(state, PANEL_EVENTS);
        cli_refresh_panel(state, PANEL_DETAILS);
    }
}

/* Show filter dialog - placeholder */
void cli_show_filter_dialog(cli_state_t *state)
{
    WINDOW *dialog;
    int max_y, max_x;
    
    if (!state)
        return;
    
    getmaxyx(stdscr, max_y, max_x);
    dialog = newwin(10, max_x - 10, (max_y - 10) / 2, 5);
    
    if (!dialog)
        return;
    
    box(dialog, 0, 0);
    mvwprintw(dialog, 0, 2, " Filter ");
    mvwprintw(dialog, 2, 2, "Filter functionality coming soon!");
    mvwprintw(dialog, 4, 2, "This will allow you to:");
    mvwprintw(dialog, 5, 4, "- Filter by event type");
    mvwprintw(dialog, 6, 4, "- Filter by interface");
    mvwprintw(dialog, 7, 4, "- Use regex patterns");
    mvwprintw(dialog, 8, 2, "Press any key to close...");
    
    wrefresh(dialog);
    nodelay(stdscr, FALSE);
    getch();
    nodelay(stdscr, TRUE);
    delwin(dialog);
    cli_refresh_all(state);
}

/* Show export dialog - placeholder */
void cli_show_export_dialog(cli_state_t *state)
{
    WINDOW *dialog;
    int max_y, max_x;
    
    if (!state)
        return;
    
    getmaxyx(stdscr, max_y, max_x);
    dialog = newwin(10, max_x - 10, (max_y - 10) / 2, 5);
    
    if (!dialog)
        return;
    
    box(dialog, 0, 0);
    mvwprintw(dialog, 0, 2, " Export ");
    mvwprintw(dialog, 2, 2, "Export functionality coming soon!");
    mvwprintw(dialog, 4, 2, "This will allow you to export events to:");
    mvwprintw(dialog, 5, 4, "- Text file");
    mvwprintw(dialog, 6, 4, "- JSON file");
    mvwprintw(dialog, 7, 4, "- CSV file");
    mvwprintw(dialog, 8, 2, "Press any key to close...");
    
    wrefresh(dialog);
    nodelay(stdscr, FALSE);
    getch();
    nodelay(stdscr, TRUE);
    delwin(dialog);
    cli_refresh_all(state);
}

/* Sort events - placeholder */
void cli_sort_events(cli_state_t *state)
{
    if (!state)
        return;
    
    /* Sorting would require converting linked list to array,
     * sorting, and converting back. For now, just mark as needing refresh */
    cli_refresh_panel(state, PANEL_EVENTS);
}

/* Apply filter - placeholder */
int cli_apply_filter(cli_state_t *state, const char *expression)
{
    if (!state || !expression)
        return -1;
    
    /* Filter application would integrate with filter_manager */
    return 0;
}

/* Clear filter */
void cli_clear_filter(cli_state_t *state)
{
    if (!state)
        return;
    
    state->filter.active = false;
    state->filter.expression[0] = '\0';
    cli_refresh_panel(state, PANEL_EVENTS);
}

/* Save filter - placeholder */
void cli_save_filter(cli_state_t *state, const char *name)
{
    (void)state;
    (void)name;
}

/* Load filter - placeholder */
void cli_load_filter(cli_state_t *state, const char *name)
{
    (void)state;
    (void)name;
}

/* Export events - placeholder */
int cli_export_events(cli_state_t *state, const char *filename, export_format_t format)
{
    (void)state;
    (void)filename;
    (void)format;
    return 0;
}

/* Set theme */
void cli_set_theme(cli_state_t *state, int theme)
{
    if (!state)
        return;
    
    state->theme = theme;
    cli_init_colors(state);
    cli_refresh_all(state);
}
