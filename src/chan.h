/*
 * chan.h --
 *
 *	stuff common to chan.c and mode.c
 *	users.h needs to be loaded too
 */
/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999, 2000, 2001, 2002 Eggheads Development Team
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
/*
 * $Id: chan.h,v 1.31 2002/10/10 05:50:12 wcc Exp $
 */

#ifndef _EGG_CHAN_H
#define _EGG_CHAN_H

typedef struct memstruct {
  char nick[NICKLEN];
  char userhost[UHOSTLEN];
  time_t joined;
  unsigned short flags;
  time_t split;			/* in case they were just netsplit	*/
  time_t last;			/* for measuring idle time		*/
  time_t delay;			/* for delayed autoop			*/
  struct userrec *user;
  struct memstruct *next;
} memberlist;

#define CHANMETA "#&!+"

#define CHANOP      0x0001	/* channel +o				*/
#define CHANVOICE   0x0002	/* channel +v				*/
#define FAKEOP      0x0004	/* op'd by server			*/
#define SENTOP      0x0008	/* a mode +o was already sent out for
				   this user				*/
#define SENTDEOP    0x0010	/* a mode -o was already sent out for
				   this user				*/
#define SENTKICK    0x0020	/* a kick was already sent out for this
				   user					*/
#define SENTVOICE   0x0040	/* a mode +v was already sent out for
				   this user				*/
#define SENTDEVOICE 0x0080	/* a devoice has been sent		*/
#define WASOP       0x0100	/* was an op before a split		*/
#define STOPWHO     0x0200
#define FULL_DELAY  0x0400
#define STOPCHECK   0x0800

#define chan_hasvoice(x) (x->flags & CHANVOICE)
#define chan_hasop(x) (x->flags & CHANOP)
#define chan_fakeop(x) (x->flags & FAKEOP)
#define chan_sentop(x) (x->flags & SENTOP)
#define chan_sentdeop(x) (x->flags & SENTDEOP)
#define chan_sentkick(x) (x->flags & SENTKICK)
#define chan_sentvoice(x) (x->flags & SENTVOICE)
#define chan_sentdevoice(x) (x->flags & SENTDEVOICE)
#define chan_issplit(x) (x->split > 0)
#define chan_wasop(x) (x->flags & WASOP)
#define chan_stopcheck(x) (x->flags & STOPCHECK)

/* Why duplicate this struct for exempts and invites only under another
 * name? <cybah>
 */
typedef struct maskstruct {
  char *mask;
  char *who;
  time_t timer;
  struct maskstruct *next;
} masklist;

/* Used for temporary bans, exempts and invites */
typedef struct maskrec {
  struct maskrec *next;
  char *mask,
       *desc,
       *user;
  time_t expire,
         added,
         lastactive;
  int flags;
} maskrec;
extern maskrec *global_bans, *global_exempts, *global_invites;

#define MASKREC_STICKY 1
#define MASKREC_PERM   2

/* For every channel i join */
struct chan_t {
  memberlist *member;
  masklist *ban;
  masklist *exempt;
  masklist *invite;
  char *topic;
  char *key;
  unsigned short int mode;
  int maxmembers;
  int members;
};

#define CHANINV    0x0001	/* +i					*/
#define CHANPRIV   0x0002	/* +p					*/
#define CHANSEC    0x0004	/* +s					*/
#define CHANMODER  0x0008	/* +m					*/
#define CHANTOPIC  0x0010	/* +t					*/
#define CHANNOMSG  0x0020	/* +n					*/
#define CHANLIMIT  0x0040	/* -l -- used only for protecting modes	*/
#define CHANKEY    0x0080	/* +k					*/
#define CHANANON   0x0100	/* +a -- ircd 2.9			*/
#define CHANQUIET  0x0200	/* +q -- ircd 2.9			*/
#define CHANNOCLR  0x0400	/* +c -- bahamut			*/
#define CHANREGON  0x0800	/* +R -- bahamut			*/
#define CHANMODR   0x1000	/* +M -- bahamut			*/

/* For every channel i'm supposed to be active on */
struct chanset_t {
  struct chanset_t *next;
  struct chan_t channel;	/* current information			*/
  char dname[81];               /* what the users know the channel as,
				   like !eggdev				*/
  char name[81];                /* what the servers know the channel
				   as, like !ABCDEeggdev		*/
  int flood_pub_thr;
  int flood_pub_time;
  int flood_join_thr;
  int flood_join_time;
  int flood_deop_thr;
  int flood_deop_time;
  int flood_kick_thr;
  int flood_kick_time;
  int flood_ctcp_thr;
  int flood_ctcp_time;
  int flood_nick_thr;
  int flood_nick_time;
  int aop_min;
  int aop_max;
  int status;
  int ircnet_status;
  int idle_kick;
  int stopnethack_mode;
  int revenge_mode;
  int ban_time;
  int invite_time;
  int exempt_time;
  maskrec *bans,		/* temporary channel bans		*/
          *exempts,		/* temporary channel exempts		*/
          *invites;		/* temporary channel invites		*/
  /* desired channel modes: */
  int mode_pls_prot;		/* modes to enforce			*/
  int mode_mns_prot;		/* modes to reject			*/
  int limit_prot;		/* desired limit			*/
  char key_prot[121];		/* desired password			*/
  /* queued mode changes: */
  char pls[21];			/* positive mode changes		*/
  char mns[21];			/* negative mode changes		*/
  char *key;			/* new key to set			*/
  char *rmkey;			/* old key to remove			*/
  int limit;			/* new limit to set			*/
  int bytes;			/* total bytes so far			*/
  int compat;			/* to prevent mixing old/new modes	*/
  struct {
    char *op;
    int type;
  } cmode[6];			/* parameter-type mode changes -	*/
  /* detect floods */
  char floodwho[FLOOD_CHAN_MAX][81];
  time_t floodtime[FLOOD_CHAN_MAX];
  int floodnum[FLOOD_CHAN_MAX];
  char deopd[NICKLEN];		/* last person deop'd (must change	*/
};

/* behavior modes for the channel */
#define CHAN_ENFORCEBANS     0x0001	/* kick people who match channel bans */
#define CHAN_DYNAMICBANS     0x0002	/* only activate bans when needed     */
#define CHAN_USERBANS        0x0004	/* don't let non-bots place bans      */
#define CHAN_OPONJOIN        0x0008	/* op +o people as soon as they join  */
#define CHAN_BITCH           0x0010	/* be a tightwad with ops	      */
#define CHAN_GREET           0x0020	/* greet people with their info line  */
#define CHAN_PROTECTOPS      0x0040	/* re-op any +o people who get deop'd */
#define CHAN_LOGSTATUS       0x0080	/* log channel status every 5 mins    */
#define CHAN_REVENGE         0x0100	/* get revenge on bad people	      */
#define CHAN_SECRET          0x0200	/* don't advertise channel on botnet  */
#define CHAN_AUTOVOICE       0x0400	/* dish out voice stuff automatically */
#define CHAN_CYCLE           0x0800	/* cycle the channel if possible      */
#define CHAN_DONTKICKOPS     0x1000	/* never kick +o flag people
					   -arthur2			      */
#define CHAN_INACTIVE        0x2000	/* no irc support for this channel
					   - drummer			      */
#define CHAN_PROTECTFRIENDS  0x4000	/* re-op any +f people who get deop'd */
#define CHAN_SHARED          0x8000	/* channel is being shared	      */
/*			     0x10000 */
#define CHAN_REVENGEBOT      0x20000	/* revenge on actions against the bot */
#define CHAN_NODESYNCH       0x40000
#define CHAN_HONORGLOBALBANS 0x80000     /* enable globalbans for this channel */
/*			     0x100000 */
#define CHAN_ACTIVE          0x1000000	/* like i'm actually on the channel
					   and stuff			      */
#define CHAN_PEND            0x2000000	/* just joined; waiting for end of
					   WHO list			      */
#define CHAN_FLAGGED         0x4000000	/* flagged during rehash for delete   */
#define CHAN_STATIC          0x8000000	/* channels that are NOT dynamic      */
#define CHAN_ASKEDBANS       0x10000000
#define CHAN_ASKEDMODES      0x20000000  /* find out key-info on IRCu          */
#define CHAN_JUPED           0x40000000  /* Is channel juped                   */
#define CHAN_STOP_CYCLE      0x80000000  /* Some efnetservers have defined
    NO_CHANOPS_WHEN_SPLIT               */

#define CHAN_ASKED_EXEMPTS       0x0001
#define CHAN_ASKED_INVITED       0x0002

#define CHAN_DYNAMICEXEMPTS      0x0004
#define CHAN_USEREXEMPTS         0x0008
#define CHAN_DYNAMICINVITES      0x0010
#define CHAN_USERINVITES         0x0020
#define CHAN_HONORGLOBALEXEMPTS  0x0040
#define CHAN_HONORGLOBALINVITES  0x0080

/* prototypes */
memberlist *ismember(struct chanset_t *, char *);
struct chanset_t *findchan(const char *name);
struct chanset_t *findchan_by_dname(const char *name);

/* is this channel +s/+p? */
#define channel_hidden(chan) (chan->channel.mode & (CHANPRIV | CHANSEC))
/* is this channel +t? */
#define channel_optopic(chan) (chan->channel.mode & CHANTOPIC)

#define channel_active(chan)  (chan->status & CHAN_ACTIVE)
#define channel_pending(chan)  (chan->status & CHAN_PEND)
#define channel_bitch(chan) (chan->status & CHAN_BITCH)
#define channel_nodesynch(chan) (chan->status & CHAN_NODESYNCH)
#define channel_autoop(chan) (chan->status & CHAN_OPONJOIN)
#define channel_autovoice(chan) (chan->status & CHAN_AUTOVOICE)
#define channel_greet(chan) (chan->status & CHAN_GREET)
#define channel_logstatus(chan) (chan->status & CHAN_LOGSTATUS)
#define channel_enforcebans(chan) (chan->status & CHAN_ENFORCEBANS)
#define channel_revenge(chan) (chan->status & CHAN_REVENGE)
#define channel_dynamicbans(chan) (chan->status & CHAN_DYNAMICBANS)
#define channel_nouserbans(chan) (!(chan->status & CHAN_USERBANS))
#define channel_protectops(chan) (chan->status & CHAN_PROTECTOPS)
#define channel_protectfriends(chan) (chan->status & CHAN_PROTECTFRIENDS)
#define channel_dontkickops(chan) (chan->status & CHAN_DONTKICKOPS)
#define channel_secret(chan) (chan->status & CHAN_SECRET)
#define channel_shared(chan) (chan->status & CHAN_SHARED)
#define channel_static(chan) (chan->status & CHAN_STATIC)
#define channel_cycle(chan) (chan->status & CHAN_CYCLE)
#define channel_inactive(chan) (chan->status & CHAN_INACTIVE)
#define channel_revengebot(chan) (chan->status & CHAN_REVENGEBOT)
#define channel_dynamicexempts(chan) (chan->ircnet_status & CHAN_DYNAMICEXEMPTS)
#define channel_nouserexempts(chan) (!(chan->ircnet_status & CHAN_USEREXEMPTS))
#define channel_dynamicinvites(chan) (chan->ircnet_status & CHAN_DYNAMICINVITES)
#define channel_nouserinvites(chan) (!(chan->ircnet_status & CHAN_USERINVITES))
#define channel_juped(chan) (chan->status & CHAN_JUPED)
#define channel_stop_cycle(chan) (chan->status & CHAN_STOP_CYCLE)
#define channel_honor_global_bans(chan) (chan->status & CHAN_HONORGLOBALBANS)
#define channel_honor_global_exempts(chan) (chan->ircnet_status & CHAN_HONORGLOBALEXEMPTS)
#define channel_honor_global_invites(chan) (chan->ircnet_status & CHAN_HONORGLOBALINVITES)

struct msgq_head {
  struct msgq *head;
  struct msgq *last;
  int tot;
  int warned;
};

/* Used to queue a lot of things */
struct msgq {
  struct msgq *next;
  int len;
  char *msg;
};

#endif				/* !_EGG_CHAN_H */
