/*
 * mode.c --
 *	queuing and flushing mode changes made by the bot
 *	channel mode changes and the bot's reaction to them
 *	setting and getting the current wanted channel modes
 */
/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999, 2000, 2001, 2002, 2003 Eggheads Development Team
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

/* FIXME: #include mess
#ifndef lint
static const char rcsid[] = "$Id: mode.c,v 1.17 2003/01/02 21:33:15 wcc Exp $";
#endif
*/

/* Reversing this mode? */
static int reversing = 0;

#define PLUS    0x01
#define MINUS   0x02
#define CHOP    0x04
#define BAN     0x08
#define VOICE   0x10
#define EXEMPT  0x20
#define INVITE  0x40

static struct flag_record user   = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};
static struct flag_record victim = {FR_GLOBAL | FR_CHAN, 0, 0, 0, 0, 0};

/* FIXME: adapt this function to strlcat() so we can get rid of egg_strcatn() */
static void flush_mode(struct chanset_t *chan, int pri)
{
  char		*p, out[512], post[512];
  size_t	postsize = sizeof(post);
  int		i, plus = 2;		/* 0 = '-', 1 = '+', 2 = none */

  p = out;
  post[0] = 0, postsize--;

  if (chan->mns[0]) {
    *p++ = '-', plus = 0;
    for (i = 0; i < strlen(chan->mns); i++)
      *p++ = chan->mns[i];
    chan->mns[0] = 0;
  }

  if (chan->pls[0]) {
    *p++ = '+', plus = 1;
    for (i = 0; i < strlen(chan->pls); i++)
      *p++ = chan->pls[i];
    chan->pls[0] = 0;
  }

  chan->bytes = 0;
  chan->compat = 0;

  /* +k or +l ? */
  if (chan->key) {
    if (plus != 1) {
      *p++ = '+', plus = 1;
    }
    *p++ = 'k';

    postsize -= egg_strcatn(post, chan->key, sizeof(post));
    postsize -= egg_strcatn(post, " ", sizeof(post));

    free(chan->key), chan->key = NULL;
  }

  /* max +l is signed 2^32 on ircnet at least... so makesure we've got at least
   * a 13 char buffer for '-2147483647 \0'. We'll be overwriting the existing
   * terminating null in 'post', so makesure postsize >= 12.
   */
  if (chan->limit != 0 && postsize >= 12) {
    if (plus != 1) {
      *p++ = '+', plus = 1;
    }
    *p++ = 'l';

    /* 'sizeof(post) - 1' is used because we want to overwrite the old null */
    postsize -= sprintf(&post[(sizeof(post) - 1) - postsize], "%d ", chan->limit);

    chan->limit = 0;
  }

  /* -k ? */
  if (chan->rmkey) {
    if (plus) {
      *p++ = '-', plus = 0;
    }
    *p++ = 'k';

    postsize -= egg_strcatn(post, chan->rmkey, sizeof(post));
    postsize -= egg_strcatn(post, " ", sizeof(post));

    free(chan->rmkey), chan->rmkey = NULL;
  }

  /* Do -{b,e,I} before +{b,e,I} to avoid the server ignoring overlaps */
  for (i = 0; i < modesperline; i++) {
    if ((chan->cmode[i].type & MINUS) &&
        postsize > strlen(chan->cmode[i].op)) {
      if (plus) {
        *p++ = '-', plus = 0;
      }

      *p++ = ((chan->cmode[i].type & BAN) ? 'b' :
              ((chan->cmode[i].type & CHOP) ? 'o' :
               ((chan->cmode[i].type & EXEMPT) ? 'e' :
                ((chan->cmode[i].type & INVITE) ? 'I' : 'v'))));

      postsize -= egg_strcatn(post, chan->cmode[i].op, sizeof(post));
      postsize -= egg_strcatn(post, " ", sizeof(post));

      free(chan->cmode[i].op), chan->cmode[i].op = NULL;
      chan->cmode[i].type = 0;
    }
  }

  /* now do all the + modes... */
  for (i = 0; i < modesperline; i++) {
    if ((chan->cmode[i].type & PLUS) &&
        postsize > strlen(chan->cmode[i].op)) {
      if (plus != 1) {
        *p++ = '+', plus = 1;
      }

      *p++ = ((chan->cmode[i].type & BAN) ? 'b' :
              ((chan->cmode[i].type & CHOP) ? 'o' :
               ((chan->cmode[i].type & EXEMPT) ? 'e' :
                ((chan->cmode[i].type & INVITE) ? 'I' : 'v'))));

      postsize -= egg_strcatn(post, chan->cmode[i].op, sizeof(post));
      postsize -= egg_strcatn(post, " ", sizeof(post));

      free(chan->cmode[i].op), chan->cmode[i].op = NULL;
      chan->cmode[i].type = 0;
    }
  }

  /* remember to terminate the buffer ('out')... */
  *p = 0;

  if (post[0]) {
    /* remove the trailing space... */
    size_t index = (sizeof(post) - 1) - postsize;
    if (index > 0 && post[index - 1] == ' ')
      post[index - 1] = 0;

    egg_strcatn(out, " ", sizeof(out));
    egg_strcatn(out, post, sizeof(out));
  }
  if (out[0]) {
    if (pri == QUICK)
      dprintf(DP_MODE, "MODE %s %s\n", chan->name, out);
    else
      dprintf(DP_SERVER, "MODE %s %s\n", chan->name, out);
  }
}

/* Queue a channel mode change
 */
static void real_add_mode(struct chanset_t *chan,
			  char plus, char mode, char *op)
{
  int i, type, modes, l;
  masklist *m;
  memberlist *mx;
  char s[21];

  if (!me_op(chan))
    return;			/* No point in queueing the mode */

  if (mode == 'o' || mode == 'v') {
    mx = ismember(chan, op);
    if (!mx)
      return;

    if (plus == '-' && mode == 'o') {
      if (chan_sentdeop(mx) || !chan_hasop(mx))
	return;
      mx->flags |= SENTDEOP;
    }
    if (plus == '+' && mode == 'o') {
      if (chan_sentop(mx) || chan_hasop(mx))
	return;
      mx->flags |= SENTOP;
    }
    if (plus == '-' && mode == 'v') {
      if (chan_sentdevoice(mx) || !chan_hasvoice(mx))
	return;
      mx->flags |= SENTDEVOICE;
    }
    if (plus == '+' && mode == 'v') {
      if (chan_sentvoice(mx) || chan_hasvoice(mx))
	return;
      mx->flags |= SENTVOICE;
    }
  }

  if (chan->compat == 0) {
    if (mode == 'e' || mode == 'I')
      chan->compat = 2;
    else
      chan->compat = 1;
  } else if (mode == 'e' || mode == 'I') {
    if (prevent_mixing && chan->compat == 1)
      flush_mode(chan, NORMAL);
  } else if (prevent_mixing && chan->compat == 2)
    flush_mode(chan, NORMAL);

  if (mode == 'o' || mode == 'b' || mode == 'v' || mode == 'e' || mode == 'I') {
    type = (plus == '+' ? PLUS : MINUS) |
	   (mode == 'o' ? CHOP :
	    (mode == 'b' ? BAN :
	     (mode == 'v' ? VOICE :
	      (mode == 'e' ? EXEMPT : INVITE))));

    /*
     * FIXME: Some networks remove overlapped bans, IrcNet does not
     *        (poptix/drummer)
     *
     * Note:  On ircnet ischanXXX() should be used, otherwise isXXXed().
     */
    /* If removing a non-existant mask... */
    if ((plus == '-' &&
         ((mode == 'b' && !ischanban(chan, op)) ||
          (mode == 'e' && !ischanexempt(chan, op)) ||
          (mode == 'I' && !ischaninvite(chan, op)))) ||
        /* or adding an existant mask... */
        (plus == '+' &&
         ((mode == 'b' && ischanban(chan, op)) ||
          (mode == 'e' && ischanexempt(chan, op)) ||
          (mode == 'I' && ischaninvite(chan, op)))))
      return;	/* ...nuke it */

    /* If there are already max_bans bans, max_exempts exemptions,
     * max_invites invitations or max_modes +b/+e/+I modes on the
     * channel, don't try to add one more.
     */
    if (plus == '+' && (mode == 'b' || mode == 'e' || mode == 'I')) {
      int	bans = 0, exempts = 0, invites = 0;

      for (m = chan->channel.ban; m && m->mask[0]; m = m->next)
	bans++;
      if (mode == 'b')
	if (bans >= max_bans)
	  return;

      for (m = chan->channel.exempt; m && m->mask[0]; m = m->next)
	exempts++;
      if (mode == 'e')
	if (exempts >= max_exempts)
	  return;

      for (m = chan->channel.invite; m && m->mask[0]; m = m->next)
	invites++;
      if (mode == 'I')
	if (invites >= max_invites)
	  return;

      if (bans + exempts + invites >= max_modes)
	return;
    }

    /* op-type mode change */
    for (i = 0; i < modesperline; i++)
      if (chan->cmode[i].type == type && chan->cmode[i].op != NULL &&
	  !irccmp(chan->cmode[i].op, op))
	return;			/* Already in there :- duplicate */
    l = strlen(op) + 1;
    if (chan->bytes + l > mode_buf_len)
      flush_mode(chan, NORMAL);
    for (i = 0; i < modesperline; i++)
      if (chan->cmode[i].type == 0) {
	chan->cmode[i].type = type;
	chan->cmode[i].op = calloc(1, l);
	chan->bytes += l;	/* Add 1 for safety */
	strcpy(chan->cmode[i].op, op);
	break;
      }
  }

  /* +k ? store key */
  else if (plus == '+' && mode == 'k') {
    if (chan->key)
      free(chan->key);
    chan->key = calloc(1, strlen(op) + 1);
    if (chan->key)
      strcpy(chan->key, op);
  }
  /* -k ? store removed key */
  else if (plus == '-' && mode == 'k') {
    if (chan->rmkey)
      free(chan->rmkey);
    chan->rmkey = calloc(1, strlen(op) + 1);
    if (chan->rmkey)
      strcpy(chan->rmkey, op);
  }
  /* +l ? store limit */
  else if (plus == '+' && mode == 'l')
    chan->limit = atoi(op);
  else {
    /* Typical mode changes */
    if (plus == '+')
      strcpy(s, chan->pls);
    else
      strcpy(s, chan->mns);
    if (!strchr(s, mode)) {
      if (plus == '+') {
	chan->pls[strlen(chan->pls) + 1] = 0;
	chan->pls[strlen(chan->pls)] = mode;
      } else {
	chan->mns[strlen(chan->mns) + 1] = 0;
	chan->mns[strlen(chan->mns)] = mode;
      }
    }
  }
  modes = modesperline;			/* Check for full buffer. */
  for (i = 0; i < modesperline; i++)
    if (chan->cmode[i].type)
      modes--;
  if (include_lk && chan->limit)
    modes--;
  if (include_lk && chan->rmkey)
    modes--;
  if (include_lk && chan->key)
    modes--;
  if (modes < 1)
    flush_mode(chan, NORMAL);		/* Full buffer! Flush modes. */
}


/*
 *    Mode parsing functions
 */

static void got_key(struct chanset_t *chan, char *nick, char *from,
		    char *key)
{
  if ((!nick[0]) && (bounce_modes))
    reversing = 1;
  if (((reversing) && !(chan->key_prot[0])) ||
      ((chan->mode_mns_prot & CHANKEY) &&
       !(glob_master(user) || glob_bot(user) || chan_master(user)))) {
    if (strlen(key) != 0) {
      add_mode(chan, '-', 'k', key);
    } else {
      add_mode(chan, '-', 'k', "");
    }
  }
}

static void got_op(struct chanset_t *chan, char *nick, char *from,
		   char *who, struct userrec *opu, struct flag_record *opper)
{
  memberlist *m;
  char s[UHOSTLEN];
  struct userrec *u;
  int check_chan = 0;
  int snm = chan->stopnethack_mode;

  m = ismember(chan, who);
  if (!m) {
    if (channel_pending(chan))
      return;
    putlog(LOG_MISC, chan->dname, _("* Mode change on %s for nonexistant %s!"), chan->dname, who);
    dprintf(DP_MODE, "WHO %s\n", who);
    return;
  }
  /* Did *I* just get opped? */
  if (!me_op(chan) && match_my_nick(who))
    check_chan = 1;

  if (!m->user) {
    simple_sprintf(s, "%s!%s", m->nick, m->userhost);
    u = get_user_by_host(s);
  } else
    u = m->user;

  get_user_flagrec(u, &victim, chan->dname);
  /* Flags need to be set correctly right from the beginning now, so that
   * add_mode() doesn't get irritated.
   */
  m->flags |= CHANOP;
  check_tcl_mode(nick, from, opu, chan->dname, "+o", who);
  /* Added new meaning of WASOP:
   * in mode binds it means: was he op before get (de)opped
   * (stupid IrcNet allows opped users to be opped again and
   *  opless users to be deopped)
   * script now can use [wasop nick chan] proc to check
   * if user was op or wasnt  (drummer)
   */
  m->flags &= ~SENTOP;

  if (channel_pending(chan))
    return;

  /* I'm opped, and the opper isn't me */
  if (me_op(chan) && !match_my_nick(who) &&
    /* and it isn't a server op */
	   nick[0]) {
    /* Channis is +bitch, and the opper isn't a global master or a bot */
    if (channel_bitch(chan) && !(glob_master(*opper) || glob_bot(*opper)) &&
	/* ... and the *opper isn't a channel master */
	!chan_master(*opper) &&
	/* ... and the oppee isn't global op/master/bot */
	!(glob_op(victim) || glob_bot(victim)) &&
	/* ... and the oppee isn't a channel op/master */
	!chan_op(victim))
      add_mode(chan, '-', 'o', who);
      /* Opped is channel +d or global +d */
    else if ((chan_deop(victim) ||
		(glob_deop(victim) && !chan_op(victim))) &&
	       !glob_master(*opper) && !chan_master(*opper))
      add_mode(chan, '-', 'o', who);
    else if (reversing)
      add_mode(chan, '-', 'o', who);
  } else if (reversing && !match_my_nick(who))
    add_mode(chan, '-', 'o', who);
  if (!nick[0] && me_op(chan) && !match_my_nick(who)) {
    if (chan_deop(victim) || (glob_deop(victim) && !chan_op(victim))) {
      m->flags |= FAKEOP;
      add_mode(chan, '-', 'o', who);
    } else if (snm > 0 && snm < 7 && !((channel_autoop(chan) || glob_autoop(victim) ||
	       chan_autoop(victim)) && (chan_op(victim) || (glob_op(victim) &&
	       !chan_deop(victim)))) && !glob_exempt(victim) && !chan_exempt(victim)) {
      if (snm == 5) snm = channel_bitch(chan) ? 1 : 3;
      if (snm == 6) snm = channel_bitch(chan) ? 4 : 2;
      if (chan_wasoptest(victim) || glob_wasoptest(victim) ||
      snm == 2) {
        if (!chan_wasop(m)) {
          m->flags |= FAKEOP;
          add_mode(chan, '-', 'o', who);
        }
      } else if (!(chan_op(victim) ||
		 (glob_op(victim) && !chan_deop(victim)))) {
		  if (snm == 1 || snm == 4 || (snm == 3 && !chan_wasop(m))) {
		    add_mode(chan, '-', 'o', who);
		    m->flags |= FAKEOP;
        }
      } else if (snm == 4 && !chan_wasop(m)) {
        add_mode(chan, '-', 'o', who);
        m->flags |= FAKEOP;
      }
    }
  }
  m->flags |= WASOP;
  if (check_chan)
    recheck_channel(chan, 1);
}

static void got_deop(struct chanset_t *chan, char *nick, char *from,
		     char *who, struct userrec *opu)
{
  memberlist *m;
  char s[UHOSTLEN], s1[UHOSTLEN];
  struct userrec *u;
  int had_op;

  m = ismember(chan, who);
  if (!m) {
    if (channel_pending(chan))
      return;
    putlog(LOG_MISC, chan->dname, _("* Mode change on %s for nonexistant %s!"), chan->dname, who);
    dprintf(DP_MODE, "WHO %s\n", who);
    return;
  }

  simple_sprintf(s, "%s!%s", m->nick, m->userhost);
  simple_sprintf(s1, "%s!%s", nick, from);
  u = get_user_by_host(s);
  get_user_flagrec(u, &victim, chan->dname);

  had_op = chan_hasop(m);
  /* Flags need to be set correctly right from the beginning now, so that
   * add_mode() doesn't get irritated.
   */
  m->flags &= ~(CHANOP | SENTDEOP | FAKEOP);
  check_tcl_mode(nick, from, opu, chan->dname, "-o", who);
  /* Check comments in got_op()  (drummer) */
  m->flags &= ~WASOP;

  if (channel_pending(chan))
    return;

  /* Deop'd someone on my oplist? */
  if (me_op(chan)) {
    int ok = 1;

    /* if they aren't d|d then check if they are something we should protect */
    if (!glob_deop(victim) && !chan_deop(victim)) {
      if (channel_protectops(chan) && (glob_master(victim) || chan_master(victim) ||
	  glob_op(victim) || chan_op(victim)))
	ok = 0;
      else if (channel_protectfriends(chan) && (glob_friend(victim) ||
	       chan_friend(victim)))
	ok = 0;
    }

    if ((reversing || !ok) && had_op && !match_my_nick(nick) &&
        rfc_casecmp(who, nick) && !match_my_nick(who) &&
	/* Is the deopper NOT a master or bot? */
        !glob_master(user) && !chan_master(user) && !glob_bot(user) &&
        ((chan_op(victim) || (glob_op(victim) && !chan_deop(victim))) ||
        !channel_bitch(chan)))
      /* Then we'll bless the victim */
      add_mode(chan, '+', 'o', who);
  }

  if (!nick[0])
    putlog(LOG_MODES, chan->dname, "TS resync (%s): %s deopped by %s",
	   chan->dname, who, from);
  /* Check for mass deop */
  if (nick[0])
    detect_chan_flood(nick, from, s1, chan, FLOOD_DEOP, who);
  /* Having op hides your +v status -- so now that someone's lost ops,
   * check to see if they have +v
   */
  if (!(m->flags & (CHANVOICE | STOPWHO))) {
    dprintf(DP_HELP, "WHO %s\n", m->nick);
    m->flags |= STOPWHO;
  }
  /* Was the bot deopped? */
  if (match_my_nick(who)) {
    /* Cancel any pending kicks and modes */
    memberlist *m2;

    for (m2 = chan->channel.member; m2 && m2->nick[0]; m2 = m2->next)
	m2->flags &= ~(SENTKICK | SENTDEOP | SENTOP | SENTVOICE | SENTDEVOICE);

    check_tcl_need(chan->dname, "op");

    if (!nick[0])
      putlog(LOG_MODES, chan->dname, "TS resync deopped me on %s :(",
	     chan->dname);
  }
  if (nick[0])
    maybe_revenge(chan, s1, s, REVENGE_DEOP);
}


static void got_ban(struct chanset_t *chan, char *nick, char *from, char *who)
{
  char me[UHOSTLEN], s[UHOSTLEN], s1[UHOSTLEN];
  memberlist *m;
  struct userrec *u;

  snprintf(me, sizeof me, "%s!%s", botname, botuserhost);
  snprintf(s, sizeof s, "%s!%s", nick, from);
  newban(chan, who, s);

  if (channel_pending(chan) || !me_op(chan))
    return;

  if (wild_match(who, me) && !isexempted(chan, me)) {
    add_mode(chan, '-', 'b', who);
    return;
  }

  if (!match_my_nick(nick)) {
    if (channel_nouserbans(chan) && nick[0] && !glob_bot(user) &&
        !glob_master(user) && !chan_master(user)) {
      add_mode(chan, '-', 'b', who);
      return;
    }
    for (m = chan->channel.member; m && m->nick[0]; m = m->next) {
      egg_snprintf(s1, sizeof s1, "%s!%s", m->nick, m->userhost);
      if (wild_match(who, s1)) {
        u = get_user_by_host(s1);
        if (u) {
          get_user_flagrec(u, &victim, chan->dname);
          if ((glob_friend(victim) || (glob_op(victim) && !chan_deop(victim)) ||
              chan_friend(victim) || chan_op(victim)) && !glob_master(user) &&
              !glob_bot(user) && !chan_master(user) && !isexempted(chan, s1)) {
            add_mode(chan, '-', 'b', who);
	    return;
          }
        }
      }
    }
  }
  refresh_exempt(chan, who);
  if (nick[0] && channel_enforcebans(chan)) 
    register maskrec *b;
    int cycle;
    char resn[512]="";

    for (cycle = 0; cycle < 2; cycle++) {
      for (b = cycle ? chan->bans : global_bans; b; b = b->next) {
        if (wild_match(b->mask, who)) {
          if (b->desc && b->desc[0] != '@')
            snprintf(resn, sizeof resn, "%s%s", IRC_PREBANNED, b->desc);
          else
	    resn[0] = 0;
        }
      }
    }
    kick_all(chan, who, resn[0] ? resn : _("Banned"), match_my_nick(nick) ? 0 : 1);
  }
  if (!nick[0] && (bounce_bans || bounce_modes) &&
      (!u_equals_mask(global_bans, who) || !u_equals_mask(chan->bans, who)))
    add_mode(chan, '-', 'b', who);
}

static void got_unban(struct chanset_t *chan, char *nick, char *from,
		      char *who, struct userrec *u)
{
  masklist *b, *old;

  old = NULL;
  for (b = chan->channel.ban; b->mask[0] && irccmp(b->mask, who);
       old = b, b = b->next)
    ;
  if (b->mask[0]) {
    if (old)
      old->next = b->next;
    else
      chan->channel.ban = b->next;
    free(b->mask);
    free(b->who);
    free(b);
  }

  if (channel_pending(chan))
    return;

  if (u_sticky_mask(chan->bans, who) || u_sticky_mask(global_bans, who)) {
    /* That's a sticky ban! No point in being
     * sticky unless we enforce it!!
     */
    add_mode(chan, '+', 'b', who);
  }
  if ((u_equals_mask(global_bans, who) || u_equals_mask(chan->bans, who)) &&
      me_op(chan) && !channel_dynamicbans(chan)) {
    /* That's a permban! */
    if ((!glob_bot(user) || !(bot_flags(u) & BOT_SHARE)) &&
        ((!glob_op(user) || chan_deop(user)) && !chan_op(user)))
      add_mode(chan, '+', 'b', who);
  }
}

static void got_exempt(struct chanset_t *chan, char *nick, char *from,
		       char *who)
{
  char s[UHOSTLEN];

  simple_sprintf(s, "%s!%s", nick, from);
  newexempt(chan, who, s);

  if (channel_pending(chan))
    return;

  if (!match_my_nick(nick)) {	/* It's not my exemption */
    if (channel_nouserexempts(chan) && nick[0] && !glob_bot(user) &&
	!glob_master(user) && !chan_master(user)) {
      /* No exempts made by users */
      add_mode(chan, '-', 'e', who);
      return;
    }
    if (!nick[0] && bounce_modes)
      reversing = 1;
  }
  if (reversing || (bounce_exempts && !nick[0] &&
		   (!u_equals_mask(global_exempts, who) ||
		    !u_equals_mask(chan->exempts, who))))
    add_mode(chan, '-', 'e', who);
}

static void got_unexempt(struct chanset_t *chan, char *nick, char *from,
			 char *who, struct userrec *u)
{
  masklist *e = chan->channel.exempt, *old = NULL;
  masklist *b ;
  int match = 0;

  while (e && e->mask[0] && irccmp(e->mask, who)) {
    old = e;
    e = e->next;
  }
  if (e && e->mask[0]) {
    if (old)
      old->next = e->next;
    else
      chan->channel.exempt = e->next;
    free(e->mask);
    free(e->who);
    free(e);
  }

  if (channel_pending(chan))
    return;

  if (u_sticky_mask(chan->exempts, who) || u_sticky_mask(global_exempts, who)) {
    /* That's a sticky exempt! No point in being sticky unless we enforce it!!
     */
    add_mode(chan, '+', 'e', who);
  }
  /* If exempt was removed by master then leave it else check for bans */
  if (!nick[0] && glob_bot(user) && !glob_master(user) && !chan_master(user)) {
    b = chan->channel.ban;
    while (b->mask[0] && !match) {
      if (wild_match(b->mask, who) ||
          wild_match(who, b->mask)) {
        add_mode(chan, '+', 'e', who);
        match = 1;
      } else
        b = b->next;
    }
  }
  if ((u_equals_mask(global_exempts, who) || u_equals_mask(chan->exempts, who)) &&
      me_op(chan) && !channel_dynamicexempts(chan)) {
    /* That's a permexempt! */
    if (glob_bot(user) && (bot_flags(u) & BOT_SHARE)) {
      /* Sharebot -- do nothing */
    } else
      add_mode(chan, '+', 'e', who);
  }
}

static void got_invite(struct chanset_t *chan, char *nick, char *from,
		       char *who)
{
  char s[UHOSTLEN];

  simple_sprintf(s, "%s!%s", nick, from);
  newinvite(chan, who, s);

  if (channel_pending(chan))
    return;

  if (!match_my_nick(nick)) {	/* It's not my invitation */
    if (channel_nouserinvites(chan) && nick[0] && !glob_bot(user) &&
	!glob_master(user) && !chan_master(user)) {
      /* No exempts made by users */
      add_mode(chan, '-', 'I', who);
      return;
    }
    if ((!nick[0]) && (bounce_modes))
      reversing = 1;
  }
  if (reversing || (bounce_invites && (!nick[0])  &&
		    (!u_equals_mask(global_invites, who) ||
		     !u_equals_mask(chan->invites, who))))
    add_mode(chan, '-', 'I', who);
}

static void got_uninvite(struct chanset_t *chan, char *nick, char *from,
			 char *who, struct userrec *u)
{
  masklist *inv = chan->channel.invite, *old = NULL;

  while (inv->mask[0] && irccmp(inv->mask, who)) {
    old = inv;
    inv = inv->next;
  }
  if (inv->mask[0]) {
    if (old)
      old->next = inv->next;
    else
      chan->channel.invite = inv->next;
    free(inv->mask);
    free(inv->who);
    free(inv);
  }

  if (channel_pending(chan))
    return;

  if (u_sticky_mask(chan->invites, who) || u_sticky_mask(global_invites, who)) {
    /* That's a sticky invite! No point in being sticky unless we enforce it!!
     */
    add_mode(chan, '+', 'I', who);
  }
  if (!nick[0] && glob_bot(user) && !glob_master(user) && !chan_master(user)
      &&  (chan->channel.mode & CHANINV))
    add_mode(chan, '+', 'I', who);
  if ((u_equals_mask(global_invites, who) ||
       u_equals_mask(chan->invites, who)) && me_op(chan) &&
      !channel_dynamicinvites(chan)) {
    /* That's a perminvite! */
    if (glob_bot(user) && (bot_flags(u) & BOT_SHARE)) {
      /* Sharebot -- do nothing */
    } else
      add_mode(chan, '+', 'I', who);
  }
}

static int gotmode(char *from, char *ignore, char *origmsg)
{
  char buf[UHOSTLEN], *nick, *uhost, *ch, *op, *chg, *msg;
  char s[256], buf2[512];
  char ms2[3];
  int z;
  struct userrec *u;
  memberlist *m;
  struct chanset_t *chan;

  strlcpy(buf2, origmsg, sizeof buf2);
  msg = buf2;
  /* Usermode changes? */
  if (msg[0] && (strchr(CHANMETA, msg[0]) != NULL)) {
    ch = newsplit(&msg);
    chg = newsplit(&msg);
    reversing = 0;
    chan = findchan(ch);
    if (!chan) {
      putlog(LOG_MISC, "*", _("Oops.   Someone made me join %s... leaving..."), ch);
      dprintf(DP_SERVER, "PART %s\n", ch);
    } else if (channel_active(chan) || channel_pending(chan)) {
      z = strlen(msg);
      if (msg[--z] == ' ')	/* I hate cosmetic bugs :P -poptix */
	msg[z] = 0;
      putlog(LOG_MODES, chan->dname, "%s: mode change '%s %s' by %s",
	     ch, chg, msg, from);
      u = get_user_by_host(from);
      get_user_flagrec(u, &user, ch);
      strlcpy(buf, from, sizeof buf);
      nick = strtok(buf, "!");
      uhost = strtok(NULL, "!");
      m = ismember(chan, nick);
      if (m)
	m->last = now;
      if (channel_active(chan) && m && me_op(chan)  &&
	  !(glob_friend(user) || chan_friend(user) ||
	    (channel_dontkickops(chan) &&
	     (chan_op(user) || (glob_op(user) && !chan_deop(user)))))) {
	if (chan_fakeop(m)) {
	  putlog(LOG_MODES, ch, _("Mode change by fake op on %s!  Reversing..."), ch);
	  dprintf(DP_MODE, "KICK %s %s :%s\n", ch, nick,
		  _("Abusing ill-gained server ops"));
	  m->flags |= SENTKICK;
	  reversing = 1;
	} else if (!chan_hasop(m) && !channel_nodesynch(chan)) {
	  putlog(LOG_MODES, ch, _("Mode change by non-chanop on %s!  Reversing..."), ch);
	  dprintf(DP_MODE, "KICK %s %s :%s\n", ch, nick,
		  _("Abusing desync"));
	  m->flags |= SENTKICK;
	  reversing = 1;
	}
      }
      ms2[0] = '+';
      ms2[2] = 0;
      while ((ms2[1] = *chg)) {
	int todo = 0;

	switch (*chg) {
	case '+':
	  ms2[0] = '+';
	  break;
	case '-':
	  ms2[0] = '-';
	  break;
	case 'i':
	  todo = CHANINV;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 'p':
	  todo = CHANPRIV;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 's':
	  todo = CHANSEC;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 'm':
	  todo = CHANMODER;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 'c':
	  todo = CHANNOCLR;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 'R':
	  todo = CHANREGON;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 'M':
	  todo = CHANMODREG;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 't':
	  todo = CHANTOPIC;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 'n':
	  todo = CHANNOMSG;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 'a':
	  todo = CHANANON;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 'q':
	  todo = CHANQUIET;
	  if (!nick && bounce_modes)
	    reversing = 1;
	  break;
	case 'l':
	  if (!nick && bounce_modes)
	    reversing = 1;
	  if (ms2[0] == '-') {
	    check_tcl_mode(nick, uhost, u, chan->dname, ms2, "");
	    if (channel_active(chan)) {
	      if ((reversing) && (chan->channel.maxmembers != 0)) {
		simple_sprintf(s, "%d", chan->channel.maxmembers);
		add_mode(chan, '+', 'l', s);
	      } else if ((chan->limit_prot != 0) && !glob_master(user) &&
			 !chan_master(user)) {
		simple_sprintf(s, "%d", chan->limit_prot);
		add_mode(chan, '+', 'l', s);
	      }
	    }
	    chan->channel.maxmembers = 0;
	  } else {
	    op = newsplit(&msg);
	    fixcolon(op);
	    if (op == '\0')
	      break;
	    chan->channel.maxmembers = atoi(op);
	    check_tcl_mode(nick, uhost, u, chan->dname, ms2,
			   int_to_base10(chan->channel.maxmembers));
	    if (channel_pending(chan))
	      break;
	    if (((reversing) &&
		 !(chan->mode_pls_prot & CHANLIMIT)) ||
		((chan->mode_mns_prot & CHANLIMIT) &&
		 !glob_master(user) && !chan_master(user))) 
	      add_mode(chan, '-', 'l', "");
	    if ((chan->limit_prot != chan->channel.maxmembers) &&
		(chan->mode_pls_prot & CHANLIMIT) &&
		(chan->limit_prot != 0) &&	/* arthur2 */
		!glob_master(user) && !chan_master(user)) {
	      simple_sprintf(s, "%d", chan->limit_prot);
	      add_mode(chan, '+', 'l', s);
	    }
	  }
	  break;
	case 'k':
 	  if (ms2[0] == '+')
	    chan->channel.mode |= CHANKEY;
	  else
            chan->channel.mode &= ~CHANKEY;
          op = newsplit(&msg);
	  fixcolon(op);
	  if (op == '\0') {
	    break;
	  }
	  check_tcl_mode(nick, uhost, u, chan->dname, ms2, op);
	  if (ms2[0] == '+') {
	    set_key(chan, op);
	    if (channel_active(chan))
	      got_key(chan, nick, uhost, op);
	  } else {
	    if (channel_active(chan)) {
	      if ((reversing) && (chan->channel.key[0]))
		add_mode(chan, '+', 'k', chan->channel.key);
	      else if ((chan->key_prot[0]) && !glob_master(user)
		       && !chan_master(user))
		add_mode(chan, '+', 'k', chan->key_prot);
	    }
	    set_key(chan, NULL);
	  }
	  break;
	case 'o':
	  op = newsplit(&msg);
	  fixcolon(op);
	  if (ms2[0] == '+')
	    got_op(chan, nick, uhost, op, u, &user);
	  else
	    got_deop(chan, nick, uhost, op, u);
	  break;
	case 'v':
	  op = newsplit(&msg);
	  fixcolon(op);
	  m = ismember(chan, op);
	  if (!m) {
	    if (channel_pending(chan))
	      break;
	    putlog(LOG_MISC, chan->dname,
		   _("* Mode change on %s for nonexistant %s!"), chan->dname, op);
	    dprintf(DP_MODE, "WHO %s\n", op);
	  } else {
	    simple_sprintf(s, "%s!%s", m->nick, m->userhost);
	    get_user_flagrec(m->user ? m->user : get_user_by_host(s),
			     &victim, chan->dname);
	    if (ms2[0] == '+') {
	      m->flags &= ~SENTVOICE;
	      m->flags |= CHANVOICE;
	      check_tcl_mode(nick, uhost, u, chan->dname, ms2, op);
	      if (channel_active(chan) &&
		  !glob_master(user) && !chan_master(user)) {
		if (channel_autovoice(chan) &&
		    (chan_quiet(victim) ||
		     (glob_quiet(victim) && !chan_voice(victim)))) {
		  add_mode(chan, '-', 'v', op);
		} else if (reversing)
		  add_mode(chan, '-', 'v', op);
	      }
	    } else {
	      m->flags &= ~SENTDEVOICE;
	      m->flags &= ~CHANVOICE;
	      check_tcl_mode(nick, uhost, u, chan->dname, ms2, op);
	      if (channel_active(chan) &&
		  !glob_master(user) && !chan_master(user)) {
		if ((channel_autovoice(chan) && !chan_quiet(victim) &&
		    (chan_voice(victim) || glob_voice(victim))) ||
		    (!chan_quiet(victim) &&
		    (glob_gvoice(victim) || chan_gvoice(victim)))) {
		  add_mode(chan, '+', 'v', op);
		} else if (reversing)
		  add_mode(chan, '+', 'v', op);
	      }
	    }
	  }
	  break;
	case 'b':
	  op = newsplit(&msg);
	  fixcolon(op);
	  check_tcl_mode(nick, uhost, u, chan->dname, ms2, op);
	  if (ms2[0] == '+')
	    got_ban(chan, nick, uhost, op);
	  else
	    got_unban(chan, nick, uhost, op, u);
	  break;
	case 'e':
	  op = newsplit(&msg);
	  fixcolon(op);
	  check_tcl_mode(nick, uhost, u, chan->dname, ms2, op);
	  if (ms2[0] == '+')
	    got_exempt(chan, nick, uhost, op);
	  else
	    got_unexempt(chan, nick, uhost, op, u);
	  break;
	case 'I':
	  op = newsplit(&msg);
	  fixcolon(op);
	  check_tcl_mode(nick, uhost, u, chan->dname, ms2, op);
	  if (ms2[0] == '+')
	    got_invite(chan, nick, uhost, op);
	  else
	    got_uninvite(chan, nick, uhost, op, u);
	  break;
	}
	if (todo) {
	  check_tcl_mode(nick, uhost, u, chan->dname, ms2, "");
	  if (ms2[0] == '+')
	    chan->channel.mode |= todo;
	  else
	    chan->channel.mode &= ~todo;
	  if (channel_active(chan)) {
	    if ((((ms2[0] == '+') && (chan->mode_mns_prot & todo)) ||
		 ((ms2[0] == '-') && (chan->mode_pls_prot & todo))) &&
		!glob_master(user) && !chan_master(user))
	      add_mode(chan, ms2[0] == '+' ? '-' : '+', *chg, "");
	    else if (reversing &&
		     ((ms2[0] == '+') || (chan->mode_pls_prot & todo)) &&
		     ((ms2[0] == '-') || (chan->mode_mns_prot & todo)))
	      add_mode(chan, ms2[0] == '+' ? '-' : '+', *chg, "");
	  }
	}
	chg++;
      }
      if (!me_op(chan) && !nick)
        chan->status |= CHAN_ASKEDMODES;
    }
  }
  return 0;
}
