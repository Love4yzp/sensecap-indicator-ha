#ifndef WIFI_MODEL_H
#define WIFI_MODEL_H

#include "view_data.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void wifi_model_state_reset(struct view_data_wifi_st *st)
{
    memset(st, 0, sizeof(*st));
}

static inline void wifi_model_state_start(struct view_data_wifi_st *st)
{
    wifi_model_state_reset(st);
    st->is_connecting = true;
}

static inline void wifi_model_state_got_ip(struct view_data_wifi_st *st)
{
    st->is_connected = true;
    st->is_connecting = false;
    st->has_ip = true;
}

static inline void wifi_model_state_disconnected(
    struct view_data_wifi_st *st, bool retrying)
{
    st->is_connected = false;
    st->is_connecting = retrying;
    st->has_ip = false;
    st->is_network = false;
}

int indicator_wifi_model_init(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MODEL_H */
