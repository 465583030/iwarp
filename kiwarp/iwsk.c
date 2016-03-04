/*
 * functions that operate on iwsk struct
 *
 * $Id: iwsk.c 644 2005-11-21 15:42:20Z pw $
 *
 * Copyright (C) 2005 OSC iWarp Team
 * Distributed under the GNU Public License Version 2 or later (See
 * LICENSE)
 */

#include <linux/spinlock.h>
#include "iwsk.h"

iwsk_state_t iwsk_get_state(iwsk_t *iwsk)
{
	iwsk_state_t st;
	spin_lock(&iwsk->lock);
	st = iwsk->state;
	spin_unlock(&iwsk->lock);
	return st;
}

void iwsk_set_state(iwsk_t *iwsk, iwsk_state_t state)
{
	spin_lock(&iwsk->lock);
	iwsk->state = state;
	spin_unlock(&iwsk->lock);
}

void iwsk_inc_rdmapsk_sink_msn(iwsk_t *iwsk)
{
	spin_lock(&iwsk->lock);
	iwsk->rdmapsk.sink_msn++;
	spin_unlock(&iwsk->lock);
}

void iwsk_inc_ddpsk_recv_msn(iwsk_t *iwsk)
{
	spin_lock(&iwsk->lock);
	iwsk->ddpsk.recv_msn++;
	spin_unlock(&iwsk->lock);
}

msn_t iwsk_get_ddpsk_recv_msn(iwsk_t *iwsk)
{
	msn_t msn;
	spin_lock(&iwsk->lock);
	msn = iwsk->ddpsk.recv_msn;
	spin_unlock(&iwsk->lock);
	return msn;
}

void iwsk_inc_ddpsk_send_msn(iwsk_t *iwsk)
{
	spin_lock(&iwsk->lock);
	iwsk->ddpsk.send_msn++;
	spin_unlock(&iwsk->lock);
}

msn_t iwsk_get_ddpsk_send_msn(iwsk_t *iwsk)
{
	msn_t msn;
	spin_lock(&iwsk->lock);
	msn = iwsk->ddpsk.send_msn;
	spin_unlock(&iwsk->lock);
	return msn;
}

void iwsk_set_mpask_crc(iwsk_t *iwsk, int use_crc)
{
	spin_lock(&iwsk->lock);
	iwsk->mpask.use_crc = use_crc;
	spin_unlock(&iwsk->lock);
}

void iwsk_set_mpask_mrkr(iwsk_t *iwsk, int use_mrkr)
{
	spin_lock(&iwsk->lock);
	iwsk->mpask.use_mrkr = use_mrkr;
	spin_unlock(&iwsk->lock);
}

void iwsk_set_mpask_crc_mrkr(iwsk_t *iwsk, int use_crc, int use_mrkr)
{
	spin_lock(&iwsk->lock);
	iwsk->mpask.use_crc = use_crc;
	iwsk->mpask.use_mrkr = use_mrkr;
	spin_unlock(&iwsk->lock);
}

stream_pos_t iwsk_get_mpask_recv_sp(iwsk_t *iwsk)
{
	stream_pos_t sp;
	spin_lock(&iwsk->lock);
	sp = iwsk->mpask.recv_sp;
	spin_unlock(&iwsk->lock);
	return sp;
}

void iwsk_add_mpask_recv_sp(iwsk_t *iwsk, stream_pos_t delta)
{
	spin_lock(&iwsk->lock);
	iwsk->mpask.recv_sp += delta;
	spin_unlock(&iwsk->lock);
}

stream_pos_t iwsk_get_mpask_send_sp(iwsk_t *iwsk)
{
	stream_pos_t sp;
	spin_lock(&iwsk->lock);
	sp = iwsk->mpask.send_sp;
	spin_unlock(&iwsk->lock);
	return sp;
}

void iwsk_add_mpask_send_sp(iwsk_t *iwsk, stream_pos_t delta)
{
	spin_lock(&iwsk->lock);
	iwsk->mpask.send_sp += delta;
	spin_unlock(&iwsk->lock);
}
