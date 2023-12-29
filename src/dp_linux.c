#include "wnp.h"

/**
 * Maybe implement MPRIS here one day, if i ever need it.
 * This file is just here for consistency with the windows
 * version so that the builds don't differ that much.
 */

void wnp_dp_start()
{
}

void wnp_dp_stop()
{
}

void wnp_dp_free_dp_data(void* dp_data)
{
}

void wnp_dp_try_set_state(struct wnp_player* player, int event_id, enum wnp_state state)
{
}

void wnp_dp_try_skip_previous(struct wnp_player* player, int event_id)
{
}

void wnp_dp_try_skip_next(struct wnp_player* player, int event_id)
{
}

void wnp_dp_try_set_position(struct wnp_player* player, int event_id, int seconds)
{
}

void wnp_dp_try_set_volume(struct wnp_player* player, int event_id, int volume)
{
}

void wnp_dp_try_set_rating(struct wnp_player* player, int event_id, int rating)
{
}

void wnp_dp_try_set_repeat(struct wnp_player* player, int event_id, enum wnp_repeat repeat)
{
}

void wnp_dp_try_set_shuffle(struct wnp_player* player, int event_id, bool shuffle)
{
}