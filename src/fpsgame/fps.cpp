#include "game.h"

namespace game
{
	char* gametitle = "Carmine Impact"; // game name: are you dumb
	char* gamestage = "Alpha"; // stage: alpha, beta, release, whatever
	char* gameversion = "2.5"; // version: major.minor(.patch)

	ICOMMAND(version, "", (), {
		defformatstring(vers, "%s %s %s", gametitle, gamestage, gameversion);
		int len = strlen(vers);
		char* k = newstring(len);
		filtertext(k, vers, true, false, len);
		stringret(k);
	})

	bool intermission = false;
	int maptime = 0, maprealtime = 0, maplimit = -1;
	int respawnent = -1;
	int lasthit = 0, lastspawnattempt = 0;

	int following = -1, followdir = 0;

	fpsent* player1 = NULL;         // our client
	vector<fpsent*> players;       // other clients
	int savedammo[NUMGUNS];

	bool clientoption(const char* arg) { return false; }

	void taunt()
	{
		if(player1->state != CS_ALIVE || player1->physstate < PHYS_SLOPE) return;
		if(lastmillis - player1->lasttaunt < 1000) return;
		player1->lasttaunt = lastmillis;
		addmsg(N_TAUNT, "rc", player1);
	}
	COMMAND(taunt, "");

	void togglespacepack()
	{
		if(!isconnected()) return;
		if(!spacepackallowed) return;
		//else if(!m_edit) return;
		if(player1->state != CS_ALIVE && player1->state != CS_DEAD && player1->spacepack != true) return;
		if(player1->state == CS_ALIVE) // only allow spacepack toggle when alive
			player1->spacepack = !player1->spacepack;
	}
	COMMAND(togglespacepack, "");

	ICOMMAND(getfollow, "", (),
		{
			fpsent * f = followingplayer();
			intret(f ? f->clientnum : -1);
		});

	bool spectating(physent* d)
	{
		return d->state == CS_SPECTATOR;
	}

	const bool canfollow(const fpsent* const spec, const fpsent* const player)
	{
		return spec && player &&
			player->state != CS_SPECTATOR &&
			((!cmode && spec->state == CS_SPECTATOR) ||
			(cmode && cmode->canfollow(spec, player)));
	}

	void follow(char* arg)
	{
		if(arg[0] ? spectating(player1) : following >= 0)
		{
			following = arg[0] ? parseplayer(arg) : -1;
			if(following == player1->clientnum) following = -1;
			followdir = 0;
			conoutf("following: %s", following >= 0 ? "on" : "off");
		}
	}
	COMMAND(follow, "s");

	void nextfollow(int dir)
	{
		if(clients.empty());
		else if(spectating(player1))
		{
			int cur = following >= 0 ? following : (dir < 0 ? clients.length() - 1 : 0);
			loopv(clients)
			{
				cur = (cur + dir + clients.length()) % clients.length();
				if(clients[cur] && canfollow(player1, clients[cur]))
				{
					if(following < 0) conoutf("follow on");
					following = cur;
					followdir = dir;
					return;
				}
			}
		}
		stopfollowing();
	}
	ICOMMAND(nextfollow, "i", (int* dir), nextfollow(*dir < 0 ? -1 : 1));


	const char* getclientmap() {
		return clientmap;
	}

	void resetgamestate()
	{
		clearprojectiles();
		clearbouncers();
	}

	fpsent* spawnstate(fpsent* d)              // reset player state not persistent accross spawns
	{
		d->respawn();
		d->spawnstate(gamemode);
		return d;
	}

	void respawnself()
	{
		if(ispaused()) return;
		if(m_mp(gamemode))
		{
			int seq = (player1->lifesequence << 16) | ((lastmillis / 1000) & 0xFFFF);
			if(player1->respawned != seq) { addmsg(N_TRYSPAWN, "rc", player1); player1->respawned = seq; }
		}
		else
		{
			spawnplayer(player1);
			showscores(false);
			lasthit = 0;
			if(cmode) cmode->respawned(player1);
		}
	}

	fpsent* pointatplayer()
	{
		loopv(players) if(players[i] != player1 && intersect(players[i], player1->o, worldpos)) return players[i];
		return NULL;
	}

	void stopfollowing()
	{
		if(following < 0) return;
		following = -1;
		followdir = 0;
		conoutf("follow off");
	}

	fpsent* followingplayer()
	{
		if(player1->state != CS_SPECTATOR || following < 0) return NULL;
		fpsent* target = getclient(following);
		if(target && target->state != CS_SPECTATOR) return target;
		return NULL;
	}

	fpsent* hudplayer()
	{
		if(thirdperson) return player1;
		fpsent* target = followingplayer();
		return target ? target : player1;
	}

	void setupcamera()
	{
		fpsent* target = followingplayer();
		if(target)
		{
			player1->yaw = target->yaw;
			player1->pitch = target->state == CS_DEAD ? 0 : target->pitch;
			player1->o = target->o;
			player1->resetinterp();
		}
	}

	bool allowthirdperson()
	{
		return !multiplayer(false) || player1->state == CS_SPECTATOR || player1->state == CS_EDITING || m_edit || m_parkour;
	}

    bool detachcamera()
    {
        fpsent *d = hudplayer();
        return d->state==CS_DEAD;
    }

    bool collidecamera()
    {
        switch(player1->state)
        {
            case CS_EDITING: return false;
            case CS_SPECTATOR: return followingplayer()!=NULL;
        }
        return true;
    }

    VARP(smoothmove, 0, 75, 100);
    VARP(smoothdist, 0, 32, 64);

    void predictplayer(fpsent *d, bool move)
    {
        d->o = d->newpos;
        d->yaw = d->newyaw;
        d->pitch = d->newpitch;
        d->roll = d->newroll;
        if(move)
        {
            moveplayer(d, 1, false);
            d->newpos = d->o;
        }
        float k = 1.0f - float(lastmillis - d->smoothmillis)/smoothmove;
        if(k>0)
        {
            d->o.add(vec(d->deltapos).mul(k));
            d->yaw += d->deltayaw*k;
            if(d->yaw<0) d->yaw += 360;
            else if(d->yaw>=360) d->yaw -= 360;
            d->pitch += d->deltapitch*k;
            d->roll += d->deltaroll*k;
        }
    }

    void otherplayers(int curtime)
    {
        loopv(players)
        {
            fpsent *d = players[i];
            if(d == player1 || d->ai) continue;

            if(d->state==CS_DEAD && d->ragdoll) moveragdoll(d);
            else if(!intermission)
            {
                if(lastmillis - d->lastaction[d->gunselect] >= d->gunwait[d->gunselect]) d->gunwait[d->gunselect] = 0;
            }

            const int lagtime = totalmillis-d->lastupdate;
            if(!lagtime || intermission) continue;
            else if(lagtime>1000 && d->state==CS_ALIVE)
            {
                d->state = CS_LAGGED;
                continue;
            }
            if(d->state==CS_ALIVE || d->state==CS_EDITING)
            {
                if(smoothmove && d->smoothmillis>0) predictplayer(d, true);
                else moveplayer(d, 1, false);
            }
            else if(d->state==CS_DEAD && !d->ragdoll && lastmillis-d->lastpain<2000) moveplayer(d, 1, true);
        }
    }


    void updateworld()        // main game update loop
    {
        if(!maptime) { maptime = lastmillis; maprealtime = totalmillis; return; }
        if(!curtime) { gets2c(); if(player1->clientnum>=0) c2sinfo(); return; }

        physicsframe();
        ai::navigate();
        updateweapons(curtime);
        otherplayers(curtime);
        ai::update();
        moveragdolls();
        gets2c();
		if(connected)
		{
			#ifdef DISCORD
				discord::updatePresence((player1->state == CS_SPECTATOR ? discord::D_SPECTATE : discord::D_PLAYING), gamemodes[gamemode - STARTGAMEMODE].name, player1);
			#endif
			if(player1->state == CS_DEAD)
			{
				if(player1->ragdoll) moveragdoll(player1);
				else if(lastmillis - player1->lastpain < 2000)
				{
					player1->fmove = player1->fstrafe = 0.0f;
					moveplayer(player1, 10, true);
				}
			}
			else if(!intermission)
			{
				if(player1->ragdoll) cleanragdoll(player1);
				moveplayer(player1, 10, true);
				swayhudgun(curtime);
				entities::checkitems(player1);
				if(cmode) cmode->checkitems(player1);
			}
		}
        if(player1->clientnum>=0) c2sinfo();   // do this last, to reduce the effective frame lag
    }

    float proximityscore(float x, float lower, float upper)
    {
        if(x <= lower) return 1.0f;
        if(x >= upper) return 0.0f;
        float a = x - lower, b = x - upper;
        return (b * b) / (a * a + b * b);
    }

    static inline float harmonicmean(float a, float b) { return a + b > 0 ? 2 * a * b / (a + b) : 0.0f; }

    // avoid spawning near other players
    float ratespawn(dynent *d, const extentity &e)
    {
        fpsent *p = (fpsent *)d;
        float maxrange = m_noitems && (!cmode || m_ctf_only) ? 160.0f : 400.0f;
        float minplayerdist = maxrange;
        loopv(players)
        {
            const fpsent *o = players[i];
            if(o == p)
            {
                if(o->state != CS_ALIVE && lastmillis - o->lastpain > 3000) continue;
            }
            else if(o->state != CS_ALIVE || isteam(o->team, p->team)) continue;

            vec dir = vec(o->o).sub(e.o);
            float dist = dir.squaredlen();
            if(dist >= minplayerdist*minplayerdist) continue;
            dist = sqrtf(dist);
            dir.mul(1/dist);

            // scale actual distance if not in line of sight
            if(raycube(e.o, dir, dist) < dist) dist *= 1.5f;
            minplayerdist = min(minplayerdist, dist);
        }
        float rating = 1.0f - proximityscore(minplayerdist, 80.0f, maxrange);
        return cmode ? harmonicmean(rating, cmode->ratespawn(p, e)) : rating;
    }

    void pickgamespawn(fpsent *d)
    {
        int ent = -1;
        int tag = cmode ? cmode->getspawngroup(d) : 0;
        findplayerspawn(d, ent, tag);
    }

    void spawnplayer(fpsent *d)   // place at random spawn
    {
        damageblend(0); // fixes damage screen not going away? -Y
        pickgamespawn(d); //findplayerspawn(d, d==player1 && respawnent  >=0 ? respawnent : -1, (!m_teammode ? 0 : (strcmp(player1->team,"red") ? 2 : 1)));
        spawnstate(d);
        if(d==player1)
        {
            if(editmode) d->state = CS_EDITING;
            else if(d->state != CS_SPECTATOR) d->state = CS_ALIVE;
        }
        else d->state = CS_ALIVE;
    }

    VARP(spawnwait, 0, 0, 1000);

    void respawn()
    {
        if(player1->state==CS_DEAD)
        {
            player1->attacking = false;
			player1->secattacking = false;
            int wait = cmode ? cmode->respawnwait(player1) : 0;
            if(wait>0)
            {
                lastspawnattempt = lastmillis;
                //conoutf(CON_GAMEINFO, "\f2you must wait %d second%s before respawn!", wait, wait!=1 ? "s" : "");
                return;
            }
            if(lastmillis < player1->lastpain + spawnwait) return;
            respawnself();
        }
    }
	COMMAND(respawn, "");

    // inputs
	VARP(attackspawn, 0, 1, 1);

    void doattack(bool on)
    {
        if(!connected || intermission) return;
        if((player1->attacking = on) && attackspawn) respawn();
    }

	void dosecattack(bool on)
	{
		if(!connected || intermission) return;
		if((player1->attacking = on) && attackspawn) respawn();
	}

	VARP(jumpspawn, 0, 1, 1);

    bool canjump()
    {
        if(!connected || intermission) return false;
        if(jumpspawn) respawn();
        return player1->state!=CS_DEAD;
    }

    bool allowmove(physent *d)
    {
        /*
        if(d->type!=ENT_PLAYER) return true;
        return !((fpsent *)d)->lasttaunt || lastmillis-((fpsent *)d)->lasttaunt>=1000;
        */
        return true;
    }

    VARP(hitsound, 0, 0, 1);

    VARP(p_hitmark, 0, 0, 1);

    void damaged(int damage, fpsent *d, fpsent *actor, int gun, bool local)
    {
        if(isteam(d->team,actor->team)) return;

        if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;

        if(local && !m_parkour) damage = d->dodamage(damage);
        else if(actor==player1) return;

        fpsent *h = hudplayer();
        if(h!=player1 && actor==h && d!=actor)
        {
            if(hitsound && lasthit != lastmillis && !m_parkour) playsound(S_HIT);
            else if(m_parkour && p_hitmark) playsound(S_HIT);
            lasthit = lastmillis;
        }
        if(!m_parkour) 
        {
            if (d == h)
            {
                damageblend(damage);
                damagecompass(damage, actor->o);
            }
            damageeffect(damage, d, d != h);

            ai::damaged(d, actor);

            if (d == h) playsound(S_PAIN6);
            else playsound(S_PAIN1 + rnd(5), &d->o);
        }
        
        if (d->health <= 0) { if (local) killed(d, actor, gun); }
    }

    VARP(deathscore, 0, 1, 1);

    void deathstate(fpsent *d, bool restore)
    {
        if(m_lms) d->state = CS_SPECTATOR;
		else d->state = CS_DEAD;
        d->lastpain = lastmillis;
        if(!restore) gibeffect(max(-d->health, 0), d->vel, d);
        if(d==player1)
        {
            if(deathscore) showscores(true);
            disablezoom();
            if(!restore) for(int i = 0; i < int(NUMGUNS); ++i) savedammo[i] = player1->ammo[i];
            d->attacking = false;
			d->secattacking = false;
            if(!restore) d->deaths++;
            //d->pitch = 0;
            d->roll = 0;
            playsound(S_DIE1+rnd(2));
        }
        else
        {
            d->fmove = d->fstrafe = 0.0f;
            d->resetinterp();
            d->smoothmillis = 0;
            playsound(S_DIE1+rnd(2), &d->o);
        }
    }

    VARP(teamcolorfrags, 0, 1, 1);

    VARP(verbosekill, 0, 0, 1);

    void killed(fpsent *d, fpsent *actor, int gun, int special)
    {
        if(d->state==CS_EDITING)
        {
            d->editstate = CS_DEAD;
            if(d==player1) d->deaths++;
            else d->resetinterp();
            return;
        }
        else if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;

        if(cmode) cmode->died(d, actor);

        fpsent *h = followingplayer();
        if(!h) h = player1;
		if(special != -1) {
			int contype = d==h || actor==h ? CON_FRAG_SELF : CON_FRAG_OTHER;
			const char *hs = "\f3 (headshot)";
			const char *dname = "", *aname = "";
			if(m_teammode && teamcolorfrags)
			{
				dname = teamcolorname(d, "you");
				aname = teamcolorname(actor, "you");
			}
			else
			{
				dname = colorname(d, NULL, "", "", "you");
				aname = colorname(actor, NULL, "", "", "you");
			}
			if(verbosekill)
			{
				if(actor->type==ENT_AI)
					conoutf(contype, "\f2%s\f2 got killed by %s with %s%s!", dname, aname, getweaponname(gun), special?hs:"");
				else if(d==actor)
					conoutf(contype, "\f2%s\f2 suicided%s", dname, d==player1 ? "!" : "");
				else
				{
					if(d==player1) conoutf(contype, "\f2%s\f2 got fragged by %s with %s%s!", dname, aname, getweaponname(gun), special?hs:"");
					else conoutf(contype, "\f2%s\f2 fragged %s with %s%s!", aname, dname, getweaponname(gun), special?hs:"");
				}
			}
			else
			{
				if(actor->type==ENT_AI)
					conoutf(contype, "\f2%s\f2 > %s > %s%s", dname, getweaponname(gun), aname, special?hs:"");
				else if(d==actor)
					conoutf(contype, "\f2world > %s", dname);
				else
					conoutf(contype, "\f2%s\f2 > %s > %s%s", aname, getweaponname(gun), dname, special?hs:"");
			}
			if(m_gun) {
				if(d != actor)
				{
					if(actor->frags >= 12) { intermission = true; if(cmode) cmode->gameover(); addmsg(N_FORCEINTERMISSION); }
					actor->ggtier = clamp(int(floor(actor->frags / 2))+1, GUN_SMG, GUN_GL);
					conoutf("gun should be %d", actor->ggtier);
					actor->attacking = false;
					actor->secattacking = false;
					actor->gunselect = actor->ggtier;
				}
                else
                {
                    actor->ggtier = clamp(actor->ggtier - 2, GUN_SMG, GUN_GL);
                    actor->attacking = false;
                    actor->secattacking = false;
                    actor->gunselect = actor->ggtier;
                }
			}
		}
		deathstate(d);
		ai::killed(d, actor);
		#ifdef DISCORD // this updates every time anyone gets a kill, shouldn't be an issue.
			discord::updatePresence((player1->state == CS_SPECTATOR ? discord::D_SPECTATE : discord::D_PLAYING), gamemodes[gamemode - STARTGAMEMODE].name, player1, true);
		#endif
    }

    void timeupdate(int secs)
    {
        if(secs > 0)
        {
            maplimit = lastmillis + secs*1000;
        }
        else
        {
            intermission = true;
            player1->attacking = false;
			player1->secattacking = false;
            if(cmode) cmode->gameover();
            conoutf(CON_GAMEINFO, "\f2intermission:");
            conoutf(CON_GAMEINFO, "\f2game has ended!");
            if(m_ctf) conoutf(CON_GAMEINFO, "\f2player frags: %d, flags: %d, deaths: %d, kdr: %.2f", player1->frags, player1->flags, player1->deaths, (float)player1->frags/player1->deaths);
            else if(m_collect) conoutf(CON_GAMEINFO, "\f2player frags: %d, skulls: %d, deaths: %d, kdr: %.2f", player1->frags, player1->flags, player1->deaths, (float)player1->frags/player1->deaths);
			else if(m_parkour); // do nothing
            else conoutf(CON_GAMEINFO, "\f2player frags: %d, deaths: %d, kdr: %.2f", player1->frags, player1->deaths, (float)player1->frags/player1->deaths);
            int accuracy = (player1->totaldamage*100)/max(player1->totalshots, 1);
            if(!m_parkour) conoutf(CON_GAMEINFO, "\f2player total damage dealt: %d, damage wasted: %d, accuracy(%%): %d", player1->totaldamage, player1->totalshots-player1->totaldamage, accuracy);

            showscores(true);
            disablezoom();

            if(identexists("intermission")) execute("intermission");
        }
    }

    ICOMMAND(getfrags, "", (), intret(player1->frags));
    ICOMMAND(getflags, "", (), intret(player1->flags));
    ICOMMAND(getdeaths, "", (), intret(player1->deaths));
    ICOMMAND(getaccuracy, "", (), intret((player1->totaldamage*100)/max(player1->totalshots, 1)));
    ICOMMAND(gettotaldamage, "", (), intret(player1->totaldamage));
    ICOMMAND(gettotalshots, "", (), intret(player1->totalshots));

    vector<fpsent *> clients;

    fpsent *newclient(int cn)   // ensure valid entity
    {
        if(cn < 0 || cn > max(0xFF, MAXCLIENTS + MAXBOTS))
        {
            neterr("clientnum", false);
            return NULL;
        }

        if(cn == player1->clientnum) return player1;

        while(cn >= clients.length()) clients.add(NULL);
        if(!clients[cn])
        {
            fpsent *d = new fpsent;
            d->clientnum = cn;
            clients[cn] = d;
            players.add(d);
        }
        return clients[cn];
    }

    fpsent *getclient(int cn)   // ensure valid entity
    {
        if(cn == player1->clientnum) return player1;
        return clients.inrange(cn) ? clients[cn] : NULL;
    }

    void clientdisconnected(int cn, bool notify)
    {
        if(!clients.inrange(cn)) return;
        if(following==cn)
        {
            if(followdir) nextfollow(followdir);
            else stopfollowing();
        }
        unignore(cn);
        fpsent *d = clients[cn];
        if(!d) return;
        if(notify && d->name[0]) conoutf("\f4leave:\f7 %s", colorname(d));
        removeweapons(d);
        removetrackedparticles(d);
        removetrackeddynlights(d);
        if(cmode) cmode->removeplayer(d);
        players.removeobj(d);
        DELETEP(clients[cn]);
        cleardynentcache();
    }

    void clearclients(bool notify)
    {
        loopv(clients) if(clients[i]) clientdisconnected(i, notify);
    }

    void initclient()
    {
        player1 = spawnstate(new fpsent);
        filtertext(player1->name, "CardboardPlayer", false, false, MAXNAMELEN);
        players.add(player1);
    }

	static oldstring cname[3];
	static int cidx = 0;
	oldstring fulltag = "";

	VARP(showtags, 0, 1, 1);
    VAR(dbgtags, 0, 0, 1);

	const char* gettags(fpsent* d, char* tag)
	{
        if(offline) { d->tagfetch = true; return ""; }
        if(d->tagfetch) return *d->tags ? d->tags : tag;
		if(!d->name[0] || !strcmp(d->name, "CardboardPlayer")) return "";
		copystring(d->tags, "");
		if(dbgtags) conoutf(CON_INFO, "\fs\f1getting tags for %s...\fr", d->name);
		oldstring apiurl;
		formatstring(apiurl, "%s/game/get/tags?id=1&name=%s", HNAPI, d->name);
		char* thing = web_get(apiurl, false);
		//conoutf(CON_DEBUG, thing);
		cJSON* json = cJSON_Parse(thing); // fix on linux, makefile doesn't work.

		// error handling
		const cJSON* status = cJSON_GetObjectItemCaseSensitive(json, "status");
		const cJSON* message = cJSON_GetObjectItemCaseSensitive(json, "message");
		if(cJSON_IsNumber(status) && cJSON_IsString(message)) {
			if(status->valueint > 0) {
                if(dbgtags) conoutf(CON_INFO, "\fs\f3getting tags for %s failed! (non-zero)\fr", d->name);
                d->tagfetch = true;
				return "";
			}
			else {
				// actual parse
				const cJSON* tag = NULL;
				const cJSON* color = NULL;

				const cJSON* tags = NULL;
				const cJSON* tagitm = NULL;

				oldstring conc = "";

				tags = cJSON_GetObjectItemCaseSensitive(json, "tags");
				cJSON_ArrayForEach(tagitm, tags)
				{
					tag = cJSON_GetObjectItemCaseSensitive(tagitm, "tag");
					if(cJSON_IsString(tag) && (tag->valuestring != NULL))
					{
						color = cJSON_GetObjectItemCaseSensitive(tagitm, "color");
						if(strcmp(tag->valuestring, "")) 
						{
							if(dbgtags) conoutf(CON_DEBUG, "tag is \"%s\"", tag->valuestring);
							if(cJSON_IsString(color) && (color->valuestring != NULL) && (strcmp(color->valuestring, "")))
							{
								if(dbgtags) conoutf(CON_DEBUG, "color is \"%s\"", color->valuestring);
								concformatstring(conc, "\fs\f%s[%s]\fr", color->valuestring, tag->valuestring);
							}
							else {
								concformatstring(conc, "[%s]", tag->valuestring);
							}
						}
						else if(cJSON_IsString(color) && (color->valuestring != NULL))
						{
							if(dbgtags) conoutf(CON_DEBUG, "color is \"%s\"", color->valuestring);
							concformatstring(conc, "\f%s", color->valuestring);
						}
					}
				}
				formatstring(fulltag, "%s", conc);
				copystring(d->tags, fulltag);
				d->tagfetch = true;
				free(json);
				return fulltag;
			}
		}
		else {
			//conoutf(CON_ERROR, "malformed JSON recieved from server");
			free(json);
            conoutf(CON_INFO, "\fs\f3getting tags for %s failed! (malformed JSON)\fr", d->name);
            d->tagfetch = true;
			return "";
		}
		free(json);
        conoutf(CON_INFO, "\fs\f3getting tags for %s failed! (no tag)\fr", d->name);
        d->tagfetch = true;
		return "";
	}

    VARP(showmodeinfo, 0, 1, 1);

	void resetplayers()
	{
		// reset perma-state
		loopv(players)
		{
			fpsent* d = players[i];
			d->frags = d->flags = 0;
			d->deaths = 0;
			d->totaldamage = 0;
			d->totalshots = 0;
            d->health = d->maxhealth = m_insta ? 1 : 1000;
			d->lifesequence = -1;
			d->respawned = d->suicided = -2;
		}
	}

	void restartgame()
	{
		clearprojectiles();
		clearbouncers();
		clearragdolls();

		resetteaminfo();
		resetplayers();

		setclientmode();

		intermission = false;
		maptime = maprealtime = 0;
		maplimit = -1;

		if(cmode) cmode->setup();

		showscores(false);
		disablezoom();
		lasthit = 0;
	}

	void startgame()
	{
		clearprojectiles();
		clearbouncers();
		clearragdolls();

		clearteaminfo();
		resetplayers();

        setclientmode();

        intermission = false;
        maptime = maprealtime = 0;
        maplimit = -1;

        if(cmode)
        {
            cmode->preload();
            cmode->setup();
        }

		gettags(player1);

        //conoutf(CON_GAMEINFO, "\f2game mode is %s", server::modename(gamemode));
		const char* info = m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
		if(showmodeinfo && info) conoutf(CON_GAMEINFO, "\f2%s: \f0%s", server::modename(gamemode), info); // gamemode info, triggered twice on first map load?????? -Y

        if(player1->playermodel != playermodel) switchplayermodel(playermodel);

        showscores(false);
        disablezoom();
        lasthit = 0;

        if(identexists("mapstart")) execute("mapstart");
    }

    void loadingmap(const char *name)
    {
        if(identexists("playsong")) execute("playsong");
    }

	COMMAND(startgame, "");
    void startmap(const char *name)   // called just after a map load
    {
        ai::savewaypoints();
        ai::clearwaypoints(true);

        respawnent = -1; // so we don't respawn at an old spot
        if(!m_mp(gamemode)) spawnplayer(player1);
        else findplayerspawn(player1, -1, (!m_teammode ? 0 : (strcmp(player1->team, "red") ? 2 : 1)));
        entities::resetspawns();
        copystring(clientmap, name ? name : "");

        sendmapinfo();
    }

    const char *getmapinfo()
    {
        return showmodeinfo && m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].name : NULL; // .info
    }

    const char *getscreenshotinfo()
    {
        return server::modename(gamemode, NULL);
    }

    void physicstrigger(physent *d, bool local, int floorlevel, int waterlevel, int material)
    {
        if     (waterlevel>0) { if(material!=MAT_LAVA) playsound(S_SPLASH1, d==player1 ? NULL : &d->o); }
        else if(waterlevel<0) playsound(material==MAT_LAVA ? S_BURN : S_SPLASH2, d==player1 ? NULL : &d->o);
        if     (floorlevel>0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) msgsound(S_JUMP, d); }
        else if(floorlevel<0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) msgsound(S_LAND, d); }
    }

    void dynentcollide(physent *d, physent *o, const vec &dir)
    {
		// nothing here
    }

    void msgsound(int n, physent *d)
    {
        if(!d || d==player1)
        {
            addmsg(N_SOUND, "ci", d, n);
            playsound(n);
        }
        else
        {
            if(d->type==ENT_PLAYER && ((fpsent *)d)->ai)
                addmsg(N_SOUND, "ci", d, n);
            playsound(n, &d->o);
        }
    }

	int numdynents() { return players.length(); }

    dynent *iterdynents(int i)
    {
        if(i<players.length()) return players[i];


        return NULL;
    }

    bool duplicatename(fpsent *d, const char *name = NULL, const char *alt = NULL)
    {
        if(!name) name = d->name;
        if(alt && d != player1 && !strcmp(name, alt)) return true;
        loopv(players) if(d!=players[i] && !strcmp(name, players[i]->name)) return true;
        return false;
    }

	const char* colorname(fpsent* d, const char* name, const char* prefix, const char* suffix, const char* alt, bool tags)
	{
        //if(!name) name = alt && d == player1 ? alt : d->name;
		if(!name) name = d->name;
        bool dup = !name[0] || duplicatename(d, name, alt) || d->aitype != AI_NONE;
		cidx = (cidx + 1) % 3;
		if(d->aitype == AI_NONE && showtags && tags) {
			prefix = gettags(d);
			if(prefix == NULL) prefix = "";
		}
        if(dup || prefix[0] || suffix[0])
        {
            if(dup)
            {
                if(d->aitype == AI_NONE)
                {
                    formatstring(cname[cidx], "%s%s \fs\f8(%d)\fr%s", prefix, name, d->clientnum, suffix);
                }
                else
                {
                    formatstring(cname[cidx], "%s%s \fs\f4(%d) \fs\f5[%d]\fr%s", prefix, name, d->skill, d->clientnum, suffix);
                }
            }
            else formatstring(cname[cidx], "%s%s%s", prefix, name, suffix);
            return cname[cidx];
        }
		else formatstring(cname[cidx], "%s", name);
		return cname[cidx];
        //return name;
    }

    VARP(teamcolortext, 0, 1, 1);

    const char *teamcolorname(fpsent *d, const char *alt, bool tags)
    {
        if(!teamcolortext || !m_teammode || d->state==CS_SPECTATOR) return colorname(d, NULL, "", "", alt, tags);
        return colorname(d, NULL, strcmp(d->team, "red") ? "\fs\f1" : "\fs\f3", "\fr", alt, tags);
    }

    const char *teamcolor(const char *name, bool sameteam, const char *alt)
    {
		conoutf(CON_DEBUG, "wrong teamcolor() used");
		if(!teamcolortext || !m_teammode) return sameteam || !alt ? name : alt;
        cidx = (cidx+1)%3;
        formatstring(cname[cidx], sameteam ? "\fs\f1%s\fr" : "\fs\f3%s\fr", sameteam || !alt ? name : alt);
        return cname[cidx];
    }

    const char *teamcolor(const char *name, const char *team, const char *alt)
    {
        //return teamcolor(name, team && isteam(team, player1->team), alt);
		cidx = (cidx + 1) % 3;
		formatstring(cname[cidx], !strcmp(team, "red") ? "\fs\f1%s\fr" : "\fs\f3%s\fr", name);
		return cname[cidx];
    }

	VARP(teamsounds, 0, 1, 1);

	void teamsound(bool sameteam, int n, const vec* loc)
	{
		playsound(n, loc, NULL, teamsounds ? (m_teammode && sameteam ? SND_USE_ALT : SND_NO_ALT) : 0);
	}

	void teamsound(fpsent* d, int n, const vec* loc)
	{
		teamsound(isteam(d->team, player1->team), n, loc);
	}

    void suicide(physent *d)
    {
        if(d==player1 || (d->type==ENT_PLAYER && ((fpsent *)d)->ai))
        {
            if(d->state!=CS_ALIVE) return;
            fpsent *pl = (fpsent *)d;
            if(!m_mp(gamemode)) killed(pl, pl, 0);
            else
            {
                int seq = (pl->lifesequence<<16)|((lastmillis/1000)&0xFFFF);
                if(pl->suicided!=seq) { addmsg(N_SUICIDE, "rc", pl); pl->suicided = seq; }
            }
        }

    }
    ICOMMAND(suicide, "", (), suicide(player1));
    ICOMMAND(kill, "", (), suicide(player1));

    bool needminimap() { return m_ctf || m_protect || m_hold || m_capture || m_collect || m_race; }

    void drawicon(int icon, float x, float y, float sz)
    {
		defformatstring(iconmap, "packages/hud/items%d.png", (int)(icon/16));
        settexture(iconmap);
        float tsz = 0.25f, tx = tsz*(icon%4), ty = tsz*(icon/4);
        gle::defvertex(2);
        gle::deftexcoord0();
        gle::begin(GL_TRIANGLE_STRIP);
        gle::attribf(x,    y);    gle::attribf(tx,     ty);
        gle::attribf(x+sz, y);    gle::attribf(tx+tsz, ty);
        gle::attribf(x,    y+sz); gle::attribf(tx,     ty+tsz);
        gle::attribf(x+sz, y+sz); gle::attribf(tx+tsz, ty+tsz);
        gle::end();
    }

    void drawhealth(fpsent *d, bool isinsta = false, float tx = 0, float ty = 0, float tw = 1, float th = 1)
    {
        float barh = HICON_SIZE, barw = ((HICON_TEXTY/2)-barh)+(HICON_SIZE*.5);
        float h = (d->state==CS_DEAD ? 0 : ((d->health*barh)/d->maxhealth)), w = HICON_SIZE*.5;
        float x = (HICON_X/2), y = ((HICON_TEXTY/2)-h)+(HICON_SIZE*.5);

		//conoutf("%f/%f=%f", (d->health * barh), (m_insta ? 1 : d->maxhealth), (d->health*barh)/(m_insta ? 1 : d->maxhealth));
		//conoutf("%d (%d)", d->health, d->maxhealth);

        settexture("packages/hud/health_bar_back.png", 3);
        gle::begin(GL_TRIANGLE_STRIP);
        gle::attribf(x,   barw);   gle::attribf(tx,      ty);
        gle::attribf(x+w, barw);   gle::attribf(tx + tw, ty);
        gle::attribf(x,   barw+barh); gle::attribf(tx,      ty + th);
        gle::attribf(x+w, barw+barh); gle::attribf(tx + tw, ty + th);
        gle::end();

        settexture("packages/hud/health_bar.png", 3);
        gle::begin(GL_TRIANGLE_STRIP);
        gle::attribf(x,   y);   gle::attribf(tx,      ty);
        gle::attribf(x+w, y);   gle::attribf(tx + tw, ty);
        gle::attribf(x,   y+h); gle::attribf(tx,      ty + th);
        gle::attribf(x+w, y+h); gle::attribf(tx + tw, ty + th);
        gle::end();
    }

    float abovegameplayhud(int w, int h)
    {
        switch(hudplayer()->state)
        {
            case CS_EDITING:
            case CS_SPECTATOR:
                return 1;
            default:
                return 1500.0f/1800.0f;
        }
    }

    VARP(ammohud, 0, 1, 1);
    VARP(ammohudhidemelee, 0, 0, 1);
    VARP(ammohudspacing, 0, 0, 1);
    VARP(ammohudoffset, 0, 0, 3);
    FVARP(ammohudscale, 1, 1, 2);

	VARP(healthbar, 0, 1, 1);
    VARP(healthcolors, 0, 1, 1);

    VARP(delayammo, 0, 1, 1);

	void drawspacepack(fpsent *d)
	{
		int icon;

		if(d->spacepack) icon = HICON_SPACEPACK;
		else icon = HICON_SPACEPACK_OFF;
		if(d->spacepack && d->spaceclip) icon = HICON_SPACEPACK_CLIP;
        gle::color(bvec::hexcolor(0xFFFFFF), 255);
		drawicon(icon, HICON_X + (healthbar ? 2 : 3) * HICON_STEP, HICON_Y);
	}

    void drawammohud(fpsent *d) // this is messy, clean up later -Y 04/03/2020
    {
        gle::color(bvec::hexcolor(0xFFFFFF), 255);
        int gunact = 0; // ammount of active guns
        for(int i = 0; i < int(7-ammohudhidemelee); ++i) {
            gle::color(bvec::hexcolor(!(lastmillis - d->lastaction[i+ammohudhidemelee] >= d->gunwait[i+ammohudhidemelee])?0x888888:0xFFFFFF), 255);
            if(d->ammo[i+ammohudhidemelee]>0)
            {
                int spa;
                if(!ammohudspacing) { spa = gunact; gunact++; } // only count up when ammo is present
                else spa = i+ammohudhidemelee; // always count up

                

                drawicon(((i==GUN_FIST&&d->jumpstate==2) ? HICON_FIST_OFF : HICON_FIST+i+ammohudhidemelee), HICON_X + ((healthbar ? 3+ammohudoffset : 4+ammohudoffset) * HICON_STEP) + (spa*HICON_SIZE/ammohudscale), HICON_Y, 120/ammohudscale);
            }
        }
    }

    void drawhudicons(fpsent *d)
    {
        pushhudmatrix();
        hudmatrix.scale(2, 2, 1);
        flushhudmatrix();

        if(healthbar) drawhealth(d, m_insta);

        defformatstring(health, "%d", d->state==CS_DEAD ? 0 : d->health);
        bvec healthcolor = bvec::hexcolor(healthcolors && !m_insta ? (d->state==CS_DEAD ? 0x808080 : (d->health<=400 ? 0xFF0000 : (d->health<=600 ? 0xFF8000 : 0xFFFFFF))) : 0xFFFFFF);
        draw_text(health, (HICON_X + (healthbar ? 0 : (20 + HICON_SIZE + HICON_SPACE)))/2, HICON_TEXTY/2, healthcolor.r, healthcolor.g, healthcolor.b);
        if(d->state!=CS_DEAD)
        {
            oldstring ammocount = "INF";
            if(!m_bottomless) formatstring(ammocount, "%d", d->ammo[d->gunselect]);

            bvec ammocolor = bvec::hexcolor((delayammo && !(lastmillis - d->lastaction[d->gunselect] >= d->gunwait[d->gunselect]))?0x888888:0xFFFFFF);
			draw_text(ammocount, (HICON_X + (healthbar ? 1 : 2)*HICON_STEP + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, ammocolor.r, ammocolor.g, ammocolor.b);
        }

        pophudmatrix();

        if(!healthbar) drawicon(HICON_HEALTH, HICON_X, HICON_Y);
        if(d->state!=CS_DEAD)
        {
			if(d->gunselect==GUN_FIST)
			{
				drawicon((d->jumpstate == 2 ? HICON_FIST_OFF : HICON_FIST), HICON_X + (healthbar ? 1 : 2) * HICON_STEP, HICON_Y);
			}
			else drawicon(HICON_FIST + d->gunselect, HICON_X + (healthbar ? 1 : 2) * HICON_STEP, HICON_Y);
            
			if(spacepackallowed) drawspacepack(d);

            if(ammohud) drawammohud(d);
        }
    }

	VARP(gameclock, 0, 1, 1);
	FVARP(gameclockscale, 1e-3f, 0.5f, 1e3f);
	HVARP(gameclockcolour, 0, 0xFFFFFF, 0xFFFFFF);
	HVARP(gameclocklowcolour, 0, 0xFFC040, 0xFFFFFF);
	HVARP(gameclockcritcolour, 0, 0xFF0000, 0xFFFFFF);
	VARP(gameclockalpha, 0, 255, 255);
	VARP(gameclockalign, -1, 0, 1);
	FVARP(gameclockx, 0, 0.50f, 1);
	FVARP(gameclocky, 0, 0.085f, 1);

	void drawgameclock(int w, int h)
	{
        int secs = max(maplimit - lastmillis + 999, 0) / 1000, mins = secs / 60;
		secs %= 60;

		defformatstring(buf, "%d:%02d", mins, secs);
		int tw = 0, th = 0;
		text_bounds(buf, tw, th);

		vec2 offset = vec2(gameclockx, gameclocky).mul(vec2(w, h).div(gameclockscale));
		if(gameclockalign == 1) offset.x -= tw;
		else if(gameclockalign == 0) offset.x -= tw / 2.0f;
		offset.y -= th / 2.0f;

		pushhudmatrix();
		hudmatrix.scale(gameclockscale, gameclockscale, 1);
		flushhudmatrix();

		int color = mins < 1 ? secs < 10 ? gameclockcritcolour : gameclocklowcolour : gameclockcolour;
		if(m_timed) draw_text(buf, int(offset.x), int(offset.y), (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, gameclockalpha);

		pophudmatrix();
	}

	extern int hudscore;
	extern void drawhudscore(int w, int h);

    VARP(showphyscompass, 0, 0, 1);
    HVARP(physcompassvelcolor, 0, 0xFFFFFF, 0xFFFFFF);
    HVARP(physcompassinputcolor, 0, 0xFF0000, 0xFFFFFF);

    void drawphyscompassvector(int &color, float w, float yaw, float magnitude)
    {
        pushhudmatrix();
        hudmatrix.rotate_around_z(yaw);
        flushhudmatrix();
        gle::defvertex(2);
        gle::color(bvec((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF), 0xff);
        gle::begin(GL_TRIANGLE_STRIP);
        const float h = magnitude * 150.0f;
        const float x = -0.5f * w, y = 0.0f;
        gle::attribf(x    , y);
        gle::attribf(x + w, y);
        gle::attribf(x    , y + h);
        gle::attribf(x + w, y + h);
        gle::end();
        pophudmatrix();
    }

    void drawphyscompass(fpsent *d, int w, int h)
    {
        enabletexture(false);
        pushhudmatrix();
        hudmatrix.translate(vec(400.0f, 1400.0f, 0.0f));
        hudmatrix.rotate_around_z(90.0f * RAD);
        flushhudmatrix();

        const float velyaw = atan2f(d->vel.y, d->vel.x) - d->yaw*RAD;
        const float velmag = min(3.0f, d->vel.magnitude2() / d->maxspeed);
        drawphyscompassvector(physcompassvelcolor, 10.0f, velyaw, velmag);

        const float inputyaw = atan2f(d->fmove, d->fstrafe);
        const float inputmag = min(1.0f, vec2(d->fmove, d->fstrafe).magnitude());
        drawphyscompassvector(physcompassinputcolor, 7.5f, inputyaw, inputmag);

        pophudmatrix();
        enabletexture(true);
    }

    void drawhudannounce(int w, int h)
    {
        if(hudannounce_timeout < totalmillis) return;
        int fw, fh;
        float zoom = 1.0f;
        float scale = 500.0f;
        int left = 0;
        int top = 0;
        int duration = hudannounce_timeout - hudannounce_begin;
        int remaining = hudannounce_timeout - totalmillis;

        switch(hudannounce_effect) {
            case E_STATIC_CENTER:
                break;
            case E_STATIC_LEFT:
                left = 500;
                break;
            case E_STATIC_RIGHT:
                left = -500;
                break;
            case E_STATIC_TOP:
                top = 400;
                break;
            case E_STATIC_BOTTOM:
                top = -400;
                break;
            case E_ZOOM_IN:
                zoom = 0.5f + ((float) remaining / duration);
                scale = scale * zoom;
                break;
            case E_ZOOM_OUT:
                zoom = 1.5f - ((float) remaining / duration);
                scale = scale * zoom;
                break;

        }
        // conoutf("drawhudannounce zoom:%1.2f remaining:%d effect:%d text:%s", zoom, hudannounce_timeout - totalmillis, hudannounce_effect, hudannounce_text);
        glPushMatrix();
        glScalef(h/scale, h/scale, 1);
        // glTranslatef(0, 0, +0.9f);
        text_bounds(hudannounce_text, fw, fh);
        draw_text(hudannounce_text, w*scale/h - fw/2 - left, scale - fh/2 - top); // , 255, 255, 255, 0);
        glPopMatrix();

    }

    void gameplayhud(int w, int h)
    {
        pushhudmatrix();
        hudmatrix.scale(h/1800.0f, h/1800.0f, 1);
        flushhudmatrix();

        if(player1->state==CS_SPECTATOR)
        {
            int pw, ph, tw, th, fw, fh;
            text_bounds("  ", pw, ph);
            text_bounds("SPECTATOR", tw, th);
            th = max(th, ph);
            fpsent *f = followingplayer();
            text_bounds(f ? colorname(f) : " ", fw, fh);
            fh = max(fh, ph);
            draw_text("SPECTATOR", w*1800/h - tw - pw, 1650 - th - fh);
            if(f)
            {
				int color = statuscolor(f, 0xFFFFFF);
                draw_text(colorname(f), w*1800/h - fw - pw, 1650 - fh, (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF);
            }
        }

        fpsent *d = hudplayer();
        if(d->state!=CS_EDITING)
        {
            if(d->state!=CS_SPECTATOR) drawhudicons(d);
            drawhudannounce(w, h);
            if(showphyscompass) drawphyscompass(d, w, h);
            if(cmode) cmode->drawhud(d, w, h);
        }

        pophudmatrix();

		if(!m_edit)
		{
			if(gameclock) drawgameclock(w, h);
			if(hudscore) drawhudscore(w, h);
		}
    }

    int clipconsole(int w, int h)
    {
        if(cmode) return cmode->clipconsole(w, h);
        return 0;
    }

    VARP(teamcrosshair, 0, 1, 1);
    VARP(healthcrosshair, 0, 1, 1);
	VARP(delaycrosshair, 0, 1, 1);
    VARP(hitcrosshair, 0, 425, 1000);

    const char *defaultcrosshair(int index)
    {
        switch(index)
        {
            case 2: return "data/hit.png";
            case 1: return "data/teammate.png";
            default: return "data/crosshair.png";
        }
    }

    int selectcrosshair(vec &color)
    {
        fpsent *d = hudplayer();
        if(d->state==CS_SPECTATOR || d->state==CS_DEAD) return -1;

        if(d->state!=CS_ALIVE) return 0;

        int crosshair = 0;
        if(lasthit && lastmillis - lasthit < hitcrosshair && !(m_parkour && !p_hitmark)) crosshair = 2;
        else if(teamcrosshair)
        {
            dynent *o = intersectclosest(d->o, worldpos, d);
            if(o && o->type==ENT_PLAYER && isteam(((fpsent *)o)->team, d->team))
            {
                crosshair = 1;
                if(!strcmp(d->team, "red")) color = vec(1, 0, 0);
				else color = vec(0, 0, 1);
            }
        }

        if(crosshair!=1 && !editmode && !m_insta && healthcrosshair)
        {
            if (d->health <= 200) color = vec(1, 0, 0);
            else if (d->health <= 600) color = vec(1, (d->health - 200) / 400.0f, 0);
            else if (d->health <= 1000) color = vec(1, 1, (d->health - 600) / 400.0f);
            else if (d->health <= 2000) color = vec((2000 - d->health) / 1000.0f, 1, (2000 - d->health) / 1000.0f);
            else color = vec(0, 1, 0);
        }
        if(d->gunwait[d->gunselect] && delaycrosshair) color.mul(0.5f);
        return crosshair;
    }

    void lighteffects(dynent *e, vec &color, vec &dir)
    {
    }

    bool serverinfostartcolumn(g3d_gui *g, int i)
    {
        static const char * const names[] = { "ping ", "players ", "mode ", "map ", "time ", "master ", "host ", "port ", "description " };
        static const float struts[] =       { 7,       7,          12.5f,   14,      7,      8,         14,      7,       24.5f };
        if(size_t(i) >= sizeof(names)/sizeof(names[0])) return false;
        g->pushlist();
        g->text(names[i], 0xFFFF80, !i ? " " : NULL);
        if(struts[i]) g->strut(struts[i]);
        g->mergehits(true);
        return true;
    }

    void serverinfoendcolumn(g3d_gui *g, int i)
    {
        g->mergehits(false);
        g->column(i);
        g->poplist();
    }

    const char *mastermodecolor(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodecolors)/sizeof(mastermodecolors[0])) ? mastermodecolors[n-MM_START] : unknown;
    }

    const char *mastermodeicon(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodeicons)/sizeof(mastermodeicons[0])) ? mastermodeicons[n-MM_START] : unknown;
    }

    bool serverinfoentry(g3d_gui *g, int i, const char *name, int port, const char *sdesc, const char *map, int ping, const vector<int> &attr, int np)
    {
        const char* pingcolor;
        if (attr.length() >= 4) {
            if (ping < 70) {
                pingcolor = "\f0";
            }
            else if (ping < 135) {
                pingcolor = "\f2";
            }
            else if (ping < 200) {
                pingcolor = "\f6";
            }
            else {
                pingcolor = "\f3";
            }
        }
        else {
            pingcolor = "";
        }

        if(ping < 0 || attr.empty() || attr[0]!=PROTOCOL_VERSION)
        {
            switch(i)
            {
                case 0:
                    if(g->button(" ", 0xFFFFDD, "serverunk")&G3D_UP) return true;
                    break;

                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                    if(g->button(" ", 0xFFFFDD)&G3D_UP) return true;
                    break;

                case 6:
                    if(g->buttonf("%s ", 0xFFFFDD, NULL, name)&G3D_UP) return true;
                    break;

                case 7:
                    if(g->buttonf("%d ", 0xFFFFDD, NULL, port)&G3D_UP) return true;
                    break;

                case 8:
                    if(ping < 0)
                    {
                        if(g->button(sdesc, 0xFFFFDD)&G3D_UP) return true;
                    }
                    else if(g->buttonf("[%s version, %d] ", 0xFFFFDD, NULL, attr.empty() ? "unknown" : (attr[0] < PROTOCOL_VERSION ? "older" : "newer"), attr.empty() ? 0 : attr[0])&G3D_UP) return true;
                    break;
            }
            return false;
        }

        switch(i)
        {
            case 0:
            {
                const char *icon = attr.inrange(3) && np >= attr[3] ? "serverfull" : (attr.inrange(4) ? mastermodeicon(attr[4], "serverunk") : "serverunk");
                if(g->buttonf("%s%d ", 0xFFFFDD, icon, pingcolor, ping)&G3D_UP) return true;
                break;
            }

            case 1:
                if(attr.length()>=4)
                {
                    if(g->buttonf(np >= attr[3] ? "\f3%d/%d " : "%d/%d ", 0xFFFFDD, NULL, np, attr[3])&G3D_UP) return true;
                }
                else if(g->buttonf("%d ", 0xFFFFDD, NULL, np)&G3D_UP) return true;
                break;

            case 2:
                if(g->buttonf("%s ", 0xFFFFDD, NULL, attr.length()>=2 ? server::modename(attr[1], "") : "")&G3D_UP) return true;
                break;

            case 3:
                if(g->buttonf("%.25s ", 0xFFFFDD, NULL, map)&G3D_UP) return true;
                break;

            case 4:
                if(attr.length()>=3 && attr[2] > 0)
                {
                    int secs = clamp(attr[2], 0, 59*60+59),
                        mins = secs/60;
                    secs %= 60;
                    if(g->buttonf("%d:%02d ", 0xFFFFDD, NULL, mins, secs)&G3D_UP) return true;
                }
                else if(g->buttonf(" ", 0xFFFFDD)&G3D_UP) return true;
                break;
            case 5:
                if(g->buttonf("%s%s ", 0xFFFFDD, NULL, attr.length()>=5 ? mastermodecolor(attr[4], "") : "", attr.length()>=5 ? server::mastermodename(attr[4], "") : "")&G3D_UP) return true;
                break;

            case 6:
                if(g->buttonf("%s ", 0xFFFFDD, NULL, name)&G3D_UP) return true;
                break;

            case 7:
                if(g->buttonf("%d ", 0xFFFFDD, NULL, port)&G3D_UP) return true;
                break;

            case 8:
                if(g->buttonf("%.25s", 0xFFFFDD, NULL, sdesc)&G3D_UP) return true;
                break;
        }
        return false;
    }

    // any data written into this vector will get saved with the map data. Must take care to do own versioning, and endianess if applicable. Will not get called when loading maps from other games, so provide defaults.
    void writegamedata(vector<char> &extras) {}
    void readgamedata(vector<char> &extras) {}

    const char *savedconfig() { return "config.cfg"; }
    const char *restoreconfig() { return "restore.cfg"; }
    const char *defaultconfig() { return "data/defaults.cfg"; }
    const char *autoexec() { return "autoexec.cfg"; }
    const char *savedservers() { return "servers.cfg"; }

    ICOMMAND(islagged, "i", (int *cn),
    {
        fpsent *d = getclient(*cn);
        if(d) intret(d->state==CS_LAGGED ? 1 : 0);
    });

    ICOMMAND(isdead, "i", (int *cn),
    {
        fpsent *d = getclient(*cn);
        if(d) intret(d->state==CS_DEAD ? 1 : 0);
    });

    void loadconfigs()
    {
        if(identexists("playsong")) execute("playsong");

        execfile("auth.cfg", false);
    }
}

