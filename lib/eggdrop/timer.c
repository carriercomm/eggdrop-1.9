/* timer.c: timer functions
 *
 * Copyright (C) 2002, 2003, 2004 Eggheads Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef lint
static const char rcsid[] = "$Id: timer.c,v 1.5 2004/06/25 17:44:04 darko Exp $";
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <eggdrop/eggdrop.h>

static egg_timeval_t now;
static char *timestamp_format = NULL;
static char *timestamp = NULL;
static int timestamp_len = 0;

/* Internal use only. */
typedef struct egg_timer_b {
	struct egg_timer_b *next;
	int id;
	char *name;
	Function callback;
	void *client_data;
	egg_timeval_t howlong;
	egg_timeval_t trigger_time;
	int flags;
} egg_timer_t;

/* We keep a sorted list of active timers. */
static egg_timer_t *timer_list_head = NULL;
static int timer_next_id = 1;

int timer_init()
{
	timestamp_len = 32;
	timestamp_format = strdup("[%H:%M] ");
	timestamp = malloc(timestamp_len);
	return(0);
}

int timer_shutdown()
{
	if (timestamp_format) {
		free(timestamp_format);
		timestamp_format = NULL;
	}
	if (timestamp) {
		free(timestamp);
		timestamp = NULL;
	}
	timestamp_len = 0;
	return(0);
}

/* Based on TclpGetTime from Tcl 8.3.3 */
int timer_get_time(egg_timeval_t *curtime)
{
	struct timeval tv;

	(void) gettimeofday(&tv, NULL);
	curtime->sec = tv.tv_sec;
	curtime->usec = tv.tv_usec;
	return(0);
}

int timer_update_now(egg_timeval_t *_now)
{
	timer_get_time(&now);
	if (_now) {
		_now->sec = now.sec;
		_now->usec = now.usec;
	}
	return(now.sec);
}

void timer_get_now(egg_timeval_t *_now)
{
	_now->sec = now.sec;
	_now->usec = now.usec;
}

int timer_get_now_sec(int *sec)
{
	if (sec) *sec = now.sec;
	return(now.sec);
}

/* Find difference between two timers. */
int timer_diff(egg_timeval_t *from_time, egg_timeval_t *to_time, egg_timeval_t *diff)
{
	diff->sec = to_time->sec - from_time->sec;
	if (diff->sec < 0) {
		diff->sec = 0;
		diff->usec = 0;
		return(1);
	}

	diff->usec = to_time->usec - from_time->usec;

	if (diff->usec < 0) {
		if (diff->sec == 0) {
			diff->usec = 0;
			return(1);
		}
		diff->sec -= 1;
		diff->usec += 1000000;
	}

	return(0);
}

static int timer_add_to_list(egg_timer_t *timer)
{
	egg_timer_t *prev, *ptr;

	/* Find out where this should go in the list. */
	prev = NULL;
	for (ptr = timer_list_head; ptr; ptr = ptr->next) {
		if (timer->trigger_time.sec < ptr->trigger_time.sec) break;
		if (timer->trigger_time.sec == ptr->trigger_time.sec && timer->trigger_time.usec < ptr->trigger_time.usec) break;
		prev = ptr;
	}

	/* Insert into timer list. */
	if (prev) {
		timer->next = prev->next;
		prev->next = timer;
	}
	else {
		timer->next = timer_list_head;
		timer_list_head = timer;
	}
	return(0);
}

int timer_create_complex(egg_timeval_t *howlong, const char *name, Function callback, void *client_data, int flags)
{
	egg_timer_t *timer;

	/* Fill out a new timer. */
	timer = malloc(sizeof(*timer));
	timer->id = timer_next_id++;
	if (name) timer->name = strdup(name);
	else timer->name = NULL;
	timer->callback = callback;
	timer->client_data = client_data;
	timer->flags = flags;
	timer->howlong.sec = howlong->sec;
	timer->howlong.usec = howlong->usec;
	timer->trigger_time.sec = now.sec + howlong->sec;
	timer->trigger_time.usec = now.usec + howlong->usec;

	timer_add_to_list(timer);

	return(timer->id);
}

/* Destroy a timer, given an id. */
int timer_destroy(int timer_id)
{
	egg_timer_t *prev, *timer;

	prev = NULL;
	for (timer = timer_list_head; timer; timer = timer->next) {
		if (timer->id == timer_id) break;
		prev = timer;
	}

	if (!timer) return(1); /* Not found! */

	/* Unlink it. */
	if (prev) prev->next = timer->next;
	else timer_list_head = timer->next;

	if (timer->name) free(timer->name);
	free(timer);
	return(0);
}

int timer_destroy_all()
{
	egg_timer_t *timer, *next;

	for (timer = timer_list_head; timer; timer = next) {
		next = timer->next;
		if (timer->name) free(timer->name);
		free(timer);
	}
	timer_list_head = NULL;

	return(0);
}

int timer_get_shortest(egg_timeval_t *howlong)
{
	egg_timer_t *timer = timer_list_head;

	/* No timers? Boo. */
	if (!timer) return(1);

	timer_diff(&now, &timer->trigger_time, howlong);

	return(0);
}

int timer_run()
{
	egg_timer_t *timer;
	Function callback;
	void *client_data;

	while (timer_list_head) {
		timer = timer_list_head;
		if (timer->trigger_time.sec > now.sec || (timer->trigger_time.sec == now.sec && timer->trigger_time.usec > now.usec)) break;

		timer_list_head = timer_list_head->next;

		callback = timer->callback;
		client_data = timer->client_data;

		if (timer->flags & TIMER_REPEAT) {
			/* Update timer. */
			timer->trigger_time.sec += timer->howlong.sec;
			timer->trigger_time.usec += timer->howlong.usec;
			if (timer->trigger_time.usec >= 1000000) {
				timer->trigger_time.usec -= 1000000;
				timer->trigger_time.sec += 1;
			}

			/* Add it back into the list. */
			timer_add_to_list(timer);
		}
		else {
			if (timer->name) free(timer->name);
			free(timer);
		}

		callback(client_data);
	}
	return(0);
}

int timer_list(int **ids)
{
	egg_timer_t *timer;
	int ntimers;

	/* Count timers. */
	ntimers = 0;
	for (timer = timer_list_head; timer; timer = timer->next) ntimers++;

	/* Fill in array. */
	*ids = malloc(sizeof(int) * (ntimers+1));
	ntimers = 0;
	for (timer = timer_list_head; timer; timer = timer->next) {
		(*ids)[ntimers++] = timer->id;
	}
	return(ntimers);
}

int timer_info(int id, char **name, egg_timeval_t *initial_len, egg_timeval_t *trigger_time)
{
	egg_timer_t *timer;

	for (timer = timer_list_head; timer; timer = timer->next) {
		if (timer->id == id) break;
	}
	if (!timer) return(-1);
	if (name) *name = timer->name;
	if (initial_len) memcpy(initial_len, &timer->howlong, sizeof(*initial_len));
	if (trigger_time) memcpy(trigger_time, &timer->trigger_time, sizeof(*trigger_time));
	return(0);
}

int timer_set_timestamp(char *format)
{
	str_redup(&timestamp_format, format);
	return(0);
}

char *timer_get_timestamp(void)
{
        time_t now_secs = (time_t)now.sec;
	int len;

	while (1) {
		len = strftime(timestamp, timestamp_len, timestamp_format, localtime(&now_secs));

		/* Did it work and fit in the buffer? */
		if (len > 0 && len < timestamp_len) break;
		else if (len >= timestamp_len) {
			/* Adjust buffer. */
			timestamp_len = len+1;
			free(timestamp);
			timestamp = malloc(timestamp_len);
		}
		else {
			/* Error with strftime, or empty timestamp. */
			*timestamp = 0;
			break;
		}
	}

	return timestamp;
}