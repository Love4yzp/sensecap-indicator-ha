#ifndef VIEW_EVENT_H
#define VIEW_EVENT_H

#include "view_data_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIEW_EVENT_DELIVERY_ENQUEUED = 0,
    VIEW_EVENT_DELIVERY_COALESCED,
    VIEW_EVENT_DELIVERY_REJECTED,
    VIEW_EVENT_DELIVERY_DROPPED,
    VIEW_EVENT_DELIVERY_INVALID,
} view_event_delivery_t;

struct view_event_stats {
    uint32_t command_enqueued;
    uint32_t command_retries;
    uint32_t command_rejected;
    uint32_t command_dispatched;
    uint32_t state_enqueued;
    uint32_t state_coalesced;
    uint32_t state_dropped;
    uint32_t state_dispatched;
    uint32_t synchronization_busy;
    uint32_t invalid_publications;
};

/*
 * Payload data is immutable and valid only for the duration of the handler.
 * Handlers run serially on the module-owned delivery task.
 */
typedef void (*view_event_handler_t)(
    void *context, enum view_event_id id, const void *payload);

bool view_event_init(void);
bool view_event_subscribe(
    enum view_event_id id, view_event_handler_t handler, void *context);

/*
 * Critical commands use a module-owned bounded enqueue policy. A rejected
 * command is logged, counted, and returned to the caller; it is never silently
 * discarded.
 */
view_event_delivery_t view_event_post_command(
    enum view_event_id id, const void *payload, size_t payload_size);

/*
 * Replaceable state never waits for queue capacity. A newer value with the
 * same semantic key replaces a queued value; if no queue token can be
 * accepted, the state is dropped, logged, and counted.
 */
view_event_delivery_t view_event_publish_state(
    enum view_event_id id, const void *payload, size_t payload_size);

void view_event_stats_get(struct view_event_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_EVENT_H */
