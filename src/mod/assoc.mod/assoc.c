/*
 * assoc.c -- part of assoc.mod
 *   the assoc code, moved here mainly from botnet.c for module work
 *
 * $Id: assoc.c,v 1.27 2001/10/21 03:44:30 stdarg Exp $
 */
/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999, 2000, 2001 Eggheads Development Team
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

#define MODULE_NAME "assoc"
#define MAKING_ASSOC
#include "src/mod/module.h"
#include "src/tandem.h"
#include <stdlib.h>
#include "assoc.h"

#define start assoc_LTX_start

#undef global
static Function *global = NULL;

/* Keep track of channel associations */
typedef struct assoc_t_ {
  char name[21];
  unsigned int channel;
  struct assoc_t_ *next;
} assoc_t;

/* Channel name-number associations */
static assoc_t *assoc;

static bind_table_t *BT_dcc, *BT_bot, *BT_link;

static void botnet_send_assoc(int idx, int chan, char *nick,
			      char *buf)
{
  char x[1024];
  int idx2;

  simple_sprintf(x, "assoc %D %s %s", chan, nick, buf);
  for (idx2 = 0; idx2 < dcc_total; idx2++)
    if ((dcc[idx2].type == &DCC_BOT) && (idx2 != idx) &&
	(b_numver(idx2) >= NEAT_BOTNET) &&
	!(bot_flags(dcc[idx2].user) & BOT_ISOLATE))
      botnet_send_zapf(idx2, botnetnick, dcc[idx2].nick, x);
}

static void link_assoc(char *bot, char *via)
{
  char x[1024];

  if (!strcasecmp(via, botnetnick)) {
    int idx = nextbot(bot);
    assoc_t *a;

    if (!(bot_flags(dcc[idx].user) & BOT_ISOLATE)) {
      for (a = assoc; a && a->name[0]; a = a->next) {
          simple_sprintf(x, "assoc %D %s %s", (int) a->channel, botnetnick,
           		a->name);
	  botnet_send_zapf(idx, botnetnick, dcc[idx].nick, x);
	}
    }
  }
}

static void kill_assoc(int chan)
{
  assoc_t *a = assoc, *last = NULL;

  while (a) {
    if (a->channel == chan) {
      if (last != NULL)
	last->next = a->next;
      else
	assoc = a->next;
      free_null(a);
    } else {
      last = a;
      a = a->next;
    }
  }
}

static void kill_all_assoc()
{
  assoc_t *a, *x;

  for (a = assoc; a; a = x) {
    x = a->next;
    free(a);
  }
  assoc = NULL;
}

static void add_assoc(char *name, int chan)
{
  assoc_t *a, *b, *old = NULL;

  for (a = assoc; a; a = a->next) {
    if (name[0] != 0 && !strcasecmp(a->name, name)) {
      kill_assoc(a->channel);
      add_assoc(name, chan);
      return;
    }
    if (a->channel == chan) {
      strncpyz(a->name, name, sizeof a->name);
      return;
    }
  }
  /* Add in numerical order */
  for (a = assoc; a; old = a, a = a->next) {
    if (a->channel > chan) {
      b = (assoc_t *) malloc(sizeof(assoc_t));
      b->next = a;
      b->channel = chan;
      strncpyz(b->name, name, sizeof b->name);
      if (old == NULL)
	assoc = b;
      else
	old->next = b;
      return;
    }
  }
  /* Add at the end */
  b = (assoc_t *) malloc(sizeof(assoc_t));
  b->next = NULL;
  b->channel = chan;
  strncpyz(b->name, name, sizeof b->name);
  if (old == NULL)
    assoc = b;
  else
    old->next = b;
}

static int get_assoc(char *name)
{
  assoc_t *a;

  for (a = assoc; a; a = a->next)
    if (!strcasecmp(a->name, name))
      return a->channel;
  return -1;
}

static char *get_assoc_name(int chan)
{
  assoc_t *a;

  for (a = assoc; a; a = a->next)
    if (a->channel == chan)
      return a->name;
  return NULL;
}

static void dump_assoc(int idx)
{
  assoc_t *a = assoc;

  if (a == NULL) {
    dprintf(idx, _("No channel names\n"));
    return;
  }
  dprintf(idx, _(" Chan  Name\n"));
  for (; a && a->name[0]; a = a->next)
      dprintf(idx, "%c%5d %s\n", (a->channel < 100000) ? ' ' : '*',
	      a->channel % 100000, a->name);
  return;
}

static int cmd_assoc(struct userrec *u, int idx, char *par)
{
  char *num;
  int chan;

  if (!par[0]) {
    putlog(LOG_CMDS, "*", "#%s# assoc", dcc[idx].nick);
    dump_assoc(idx);
  } else if (!u || !(u->flags & USER_BOTMAST)) {
    dprintf(idx, _("What?  You need '.help'\n"));
  } else {
    num = newsplit(&par);
    if (num[0] == '*') {
      chan = 100000 + atoi(num + 1);
      if ((chan < 100000) || (chan > 199999)) {
	   dprintf(idx, _("Channel # out of range: must be *0-*99999\n"));
	return 0;
      }
    } else {
      chan = atoi(num);
      if (chan == 0) {
	    dprintf(idx, _("You cant name the main party line; its just a party line.\n"));
	return 0;
      } else if ((chan < 1) || (chan > 99999)) {
	    dprintf(idx, _("Channel # out of range: must be 1-99999\n"));
	return 0;
      }
    }
    if (!par[0]) {
      /* Remove an association */
      if (get_assoc_name(chan) == NULL) {
	    dprintf(idx, _("Channel %s%d has no name.\n"), (chan < 100000) ? "" : "*",
	            chan % 100000);
	return 0;
      }
      kill_assoc(chan);
      putlog(LOG_CMDS, "*", "#%s# assoc %d", dcc[idx].nick, chan);
      dprintf(idx, _("Okay, removed name for channel %s%d.\n"), (chan < 100000) ? "" : "*",
              chan % 100000);
      chanout_but(-1, chan, _("--- %s removed this channels name.\n"), dcc[idx].nick);
      if (chan < 100000)
	botnet_send_assoc(-1, chan, dcc[idx].nick, "0");
      return 0;
    }
    if (strlen(par) > 20) {
      dprintf(idx, _("Channels name cant be that long (20 chars max).\n"));
      return 0;
    }
    if ((par[0] >= '0') && (par[0] <= '9')) {
      dprintf(idx, _("First character of the channel name cant be a digit.\n"));
      return 0;
    }
    add_assoc(par, chan);
    putlog(LOG_CMDS, "*", "#%s# assoc %d %s", dcc[idx].nick, chan, par);
    dprintf(idx, _("Okay, channel %s%d is %s now.\n"), (chan < 100000) ? "" : "*",
            chan % 100000, par);
    chanout_but(-1, chan, _("--- %s named this channel %s\n"), dcc[idx].nick,
		par);
    if (chan < 100000)
      botnet_send_assoc(-1, chan, dcc[idx].nick, par);
  }
  return 0;
}

static int tcl_killassoc STDVAR {
  int chan;

  BADARGS(2, 2, " chan");
  if (argv[1][0] == '&')
    kill_all_assoc();
  else {
    chan = atoi(argv[1]);
    if ((chan < 1) || (chan > 199999)) {
      Tcl_AppendResult(irp, _("invalid channel #"), NULL);
      return TCL_ERROR;
    }
    kill_assoc(chan);
    botnet_send_assoc(-1, chan, "*script*", "0");
  }
  return TCL_OK;
}

static int tcl_assoc STDVAR {
  int chan;
  char name[21], *p;

  BADARGS(2, 3, " chan ?name?");
  if ((argc == 2) && ((argv[1][0] < '0') || (argv[1][0] > '9'))) {
    chan = get_assoc(argv[1]);
    if (chan == -1)
      Tcl_AppendResult(irp, "", NULL);
    else {
      simple_sprintf(name, "%d", chan);
      Tcl_AppendResult(irp, name, NULL);
    }
    return TCL_OK;
  }
  chan = atoi(argv[1]);
  if ((chan < 1) || (chan > 199999)) {
    Tcl_AppendResult(irp, _("invalid channel #"), NULL);
    return TCL_ERROR;
  }
  if (argc == 3) {
    strncpy(name, argv[2], 20);
    name[20] = 0;
    add_assoc(name, chan);
    botnet_send_assoc(-1, chan, "*script*", name);
  }
  p = get_assoc_name(chan);
  if (p == NULL)
    name[0] = 0;
  else
    strcpy(name, p);
  Tcl_AppendResult(irp, name, NULL);
  return TCL_OK;
}

static void zapf_assoc(char *botnick, char *code, char *par)
{
  int idx = nextbot(botnick);
  char *s, *s1, *nick;
  int linking = 0, chan;

  if ((idx >= 0) && !(bot_flags(dcc[idx].user) & BOT_ISOLATE)) {
    if (!strcasecmp(dcc[idx].nick, botnick))
      linking = b_status(idx) & STAT_LINKING;
    s = newsplit(&par);
    chan = base64_to_int(s);
    if ((chan > 0) || (chan < GLOBAL_CHANS)) {
      nick = newsplit(&par);
      s1 = get_assoc_name(chan);
      if (linking && ((s1 == NULL) || (s1[0] == 0) ||
		      (((int) get_user(find_entry_type("BOTFL"),
				       dcc[idx].user) & BOT_HUB)))) {
	add_assoc(par, chan);
	botnet_send_assoc(idx, chan, nick, par);
	chanout_but(-1, chan, _("--- (%s) named this channel %s.\n"), nick, par);
      } else if (par[0] == '0') {
	kill_assoc(chan);
	chanout_but(-1, chan, _("--- (%s) %s removed this channels name.\n"), botnick, nick);
      } else if (get_assoc(par) != chan) {
	/* New one i didn't know about -- pass it on */
	s1 = get_assoc_name(chan);
	add_assoc(par, chan);
	chanout_but(-1, chan, _("--- (%s) %s named this channel '%s'.\n"), botnick, nick, par);
      }
    }
  }
}

/* A report on the module status.
 */
static void assoc_report(int idx, int details)
{
  assoc_t *a;
  int size = 0, count = 0;;

  if (details) {
    for (a = assoc; a; a = a->next) {
      count++;
      size += sizeof(assoc_t);
    }
    dprintf(idx, _("    %d assocs using %d bytes\n"),
	    count, size);
  }
}

static cmd_t mydcc[] =
{
  {"assoc",	"",	cmd_assoc,		NULL},
  {NULL, 	NULL,	NULL,			NULL}
};

static cmd_t mybot[] =
{
  {"assoc",	"",	(Function) zapf_assoc,	NULL},
  {NULL,        NULL,   NULL,                   NULL}
};

static cmd_t mylink[] =
{
  {"*",		"",	(Function) link_assoc,	"assoc"},
  {NULL,	NULL,	NULL,			NULL}
};

static tcl_cmds mytcl[] =
{
  {"assoc",		tcl_assoc},
  {"killassoc",		tcl_killassoc},
  {NULL,		NULL}
};

static char *assoc_close()
{
  kill_all_assoc();
  if (BT_dcc) rem_builtins2(BT_dcc, mydcc);
  if (BT_bot) rem_builtins2(BT_bot, mybot);
  if (BT_link) rem_builtins2(BT_link, mylink);
  rem_tcl_commands(mytcl);
  module_undepend(MODULE_NAME);
  return NULL;
}

EXPORT_SCOPE char *start();

static Function assoc_table[] =
{
  (Function) start,
  (Function) assoc_close,
  (Function) 0,
  (Function) assoc_report,
};

char *start(Function * global_funcs)
{
  global = global_funcs;

  module_register(MODULE_NAME, assoc_table, 2, 0);
  if (!module_depend(MODULE_NAME, "eggdrop", 107, 0)) {
    module_undepend(MODULE_NAME);
    return _("This module requires eggdrop1.7.0 or later");
  }
  assoc = NULL;
  BT_dcc = find_bind_table2("dcc");
  if (BT_dcc) add_builtins2(BT_dcc, mydcc);
  BT_bot = find_bind_table2("bot");
  if (BT_bot) add_builtins2(BT_bot, mybot);
  BT_link = find_bind_table2("link");
  if (BT_link) add_builtins2(BT_link, mylink);
  add_tcl_commands(mytcl);

  return NULL;
}

