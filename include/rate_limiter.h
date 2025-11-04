/* rate_limiter.h - Token bucket rate limiter for event processing
 *
 * Implements token bucket algorithm for rate limiting with per-event-type
 * support and statistics tracking.
 */

#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Rate limiter structure (opaque) */
struct rate_limiter;

/**
 * rate_limiter_create() - Create a new rate limiter
 * @rate: Maximum events per second (tokens per second)
 * @burst: Maximum burst size (bucket capacity)
 *
 * Returns: Pointer to rate limiter or NULL on error
 */
struct rate_limiter *rate_limiter_create(double rate, size_t burst);

/**
 * rate_limiter_destroy() - Destroy rate limiter
 * @rl: Rate limiter to destroy
 */
void rate_limiter_destroy(struct rate_limiter *rl);

/**
 * rate_limiter_allow() - Check if event is allowed
 * @rl: Rate limiter
 *
 * Returns: true if event is allowed, false if rate limited
 */
bool rate_limiter_allow(struct rate_limiter *rl);

/**
 * rate_limiter_allow_n() - Check if N events are allowed
 * @rl: Rate limiter
 * @n: Number of events
 *
 * Returns: true if all N events are allowed, false otherwise
 */
bool rate_limiter_allow_n(struct rate_limiter *rl, size_t n);

/**
 * rate_limiter_reset() - Reset rate limiter state
 * @rl: Rate limiter
 */
void rate_limiter_reset(struct rate_limiter *rl);

/**
 * rate_limiter_set_rate() - Update rate limit
 * @rl: Rate limiter
 * @rate: New rate (events per second)
 */
void rate_limiter_set_rate(struct rate_limiter *rl, double rate);

/**
 * rate_limiter_set_burst() - Update burst size
 * @rl: Rate limiter
 * @burst: New burst size
 */
void rate_limiter_set_burst(struct rate_limiter *rl, size_t burst);

/**
 * rate_limiter_get_tokens() - Get current token count
 * @rl: Rate limiter
 *
 * Returns: Current number of tokens available
 */
double rate_limiter_get_tokens(struct rate_limiter *rl);

/**
 * rate_limiter_stats() - Get rate limiter statistics
 * @rl: Rate limiter
 * @allowed: Output for total allowed events
 * @denied: Output for total denied events
 * @current_rate: Output for current event rate (events/sec)
 */
void rate_limiter_stats(struct rate_limiter *rl,
                        unsigned long *allowed,
                        unsigned long *denied,
                        double *current_rate);

/* Per-event-type rate limiter */
struct rate_limiter_map;

/**
 * rate_limiter_map_create() - Create per-event-type rate limiter
 * @default_rate: Default rate for unspecified event types
 * @default_burst: Default burst for unspecified event types
 *
 * Returns: Pointer to rate limiter map or NULL on error
 */
struct rate_limiter_map *rate_limiter_map_create(double default_rate, 
                                                  size_t default_burst);

/**
 * rate_limiter_map_destroy() - Destroy rate limiter map
 * @map: Rate limiter map to destroy
 */
void rate_limiter_map_destroy(struct rate_limiter_map *map);

/**
 * rate_limiter_map_set() - Set rate limit for specific event type
 * @map: Rate limiter map
 * @event_type: Event type identifier
 * @rate: Rate limit (events per second)
 * @burst: Burst size
 *
 * Returns: true on success, false on error
 */
bool rate_limiter_map_set(struct rate_limiter_map *map, uint32_t event_type,
                          double rate, size_t burst);

/**
 * rate_limiter_map_allow() - Check if event type is allowed
 * @map: Rate limiter map
 * @event_type: Event type identifier
 *
 * Returns: true if event is allowed, false if rate limited
 */
bool rate_limiter_map_allow(struct rate_limiter_map *map, uint32_t event_type);

/**
 * rate_limiter_map_reset() - Reset all rate limiters
 * @map: Rate limiter map
 */
void rate_limiter_map_reset(struct rate_limiter_map *map);

/**
 * rate_limiter_map_stats() - Get statistics for event type
 * @map: Rate limiter map
 * @event_type: Event type identifier
 * @allowed: Output for total allowed events
 * @denied: Output for total denied events
 * @current_rate: Output for current event rate
 */
void rate_limiter_map_stats(struct rate_limiter_map *map, uint32_t event_type,
                            unsigned long *allowed,
                            unsigned long *denied,
                            double *current_rate);

#endif /* RATE_LIMITER_H */
