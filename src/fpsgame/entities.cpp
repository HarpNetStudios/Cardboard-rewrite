#include "game.h"

namespace entities
{
    using namespace game;

    int extraentinfosize() { return 0; }       // size in bytes of what the 2 methods below read/write... so it can be skipped by other games

    void writeent(entity &e, char *buf)   // write any additional data to disk (except for ET_ ents)
    {
    }

    void readent(entity &e, char *buf, int ver)     // read from disk, and init
    {
        if(ver <= 30) switch(e.type)
        {
            case FLAG:
            case TELEDEST:
                break;
        }

    }

#ifndef STANDALONE
    vector<extentity *> ents;

    vector<extentity *> &getents() { return ents; }

    bool mayattach(extentity &e) { return false; }
    bool attachent(extentity &e, extentity &a) { return false; }

    const char *itemname(int i)
    {
        int t = ents[i]->type;
        if(t<I_AMMO || t>I_HEALTH) return NULL;
        return itemstats[t-I_AMMO].name;
    }

    int itemicon(int i)
    {
        int t = ents[i]->type;
        if(t<I_AMMO || t>I_HEALTH) return -1;
        return itemstats[t-I_AMMO].icon;
    }

    const char *entmdlname(int type)
    {
        static const char * const entmdlnames[] =
        {
            NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL,
            NULL, NULL,
            NULL,
            NULL, NULL,
            NULL,
            NULL, NULL,
            NULL, NULL,
            NULL
        };
        return entmdlnames[type];
    }

    const char *entmodel(const entity &e)
    {
        if(e.type == TELEPORT)
        {
            if(e.attr2 > 0) return mapmodelname(e.attr2);
            if(e.attr2 < 0) return NULL;
        }
        return e.type < MAXENTTYPES ? entmdlname(e.type) : NULL;
    }

    void preloadentities()
    {
        for(int i = 0; i < int(MAXENTTYPES); ++i)
        {
            switch(i)
            {
                case I_AMMO: case I_HEALTH:
                    break;
                case RACE_START: case RACE_FINISH: case RACE_CHECKPOINT:
                    if (!m_race) continue;
                    break;
            }
            const char *mdl = entmdlname(i);
            if(!mdl) continue;
            preloadmodel(mdl);
        }
        loopv(ents)
        {
            extentity &e = *ents[i];
            switch(e.type)
            {
                case TELEPORT:
                    if(e.attr2 > 0) preloadmodel(mapmodelname(e.attr2));
                case JUMPPAD:
                    if(e.attr4 > 0) preloadmapsound(e.attr4);
                    break;
            }
        }
    }

    void renderentities()
    {
        loopv(ents)
        {
            extentity &e = *ents[i];
            int revs = 10;
            switch(e.type)
            {
                case TELEPORT:
                    if(e.attr2 < 0) continue;
                    break;
                default:
                    if(!e.spawned() || e.type < I_AMMO || e.type > I_HEALTH) continue;
            }
            const char *mdlname = entmodel(e);
            if(mdlname)
            {
                vec p = e.o;
                p.z += 1+sinf(lastmillis/100.0+e.o.x+e.o.y)/20;
                rendermodel(&e.light, mdlname, ANIM_MAPMODEL|ANIM_LOOP, p, lastmillis/(float)revs, 0, MDL_SHADOW | MDL_CULL_VFC | MDL_CULL_DIST | MDL_CULL_OCCLUDED);
            }
        }
    }

    void addammo(int *v, bool local)
    {
        itemstat &is = itemstats[I_AMMO];
		for(int i = 0; i < 6; ++i) v[i+1] += guns[i+1].ammoadd;
		for(int i = 0; i < 6; ++i) if(v[i+1]>is.max) v[i+1] = guns[i+1].ammomax;
        if(local) msgsound(is.sound);
    }

    void repammo(fpsent *d, bool local)
    {
        addammo(d->ammo, local);
    }

    // these two functions are called when the server acknowledges that you really
    // picked up the item (in multiplayer someone may grab it before you).

    void pickupeffects(int n, fpsent *d)
    {
        if(!ents.inrange(n)) return;
        extentity* e = ents[n];
        int type = e->type;
        if(type<I_AMMO || type>I_HEALTH) return;
        e->clearspawned();
        e->clearnopickup();
        if(!d) return;
        itemstat &is = itemstats[type-I_HEALTH];
        fpsent* h = followingplayer();
        if (d != h || isthirdperson())
        {
            //particle_text(d->abovehead(), is.name, PART_TEXT, 2000, 0xFFC864, 4.0f, -8);
            particle_icon(d->abovehead(), is.icon%4, is.icon/4, PART_HUD_ICON_GREY, 2000, 0xFFFFFF, 2.0f, -8);
        }
        playsound(itemstats[type-I_HEALTH].sound, d!=h ? &d->o : NULL, NULL, 0, 0, 0, -1, 0, 1500);
        d->pickup(type);
    }

    // these functions are called when the client touches the item

    void teleporteffects(fpsent *d, int tp, int td, bool local)
    {
        if(ents.inrange(tp) && ents[tp]->type == TELEPORT)
        {
            extentity &e = *ents[tp];
            if(e.attr4 >= 0)
            {
                int snd = S_TELEPORT, flags = 0;
                if(e.attr4 > 0) { snd = e.attr4; flags = SND_MAP; }
                fpsent* h = followingplayer();
                playsound(snd, d == h ? NULL : &e.o, NULL, flags);
                if (d != h && ents.inrange(td) && ents[td]->type == TELEDEST) playsound(snd, &ents[td]->o, NULL, flags);
            }
        }
        if(local && d->clientnum >= 0)
        {
            sendposition(d);
            packetbuf p(32, ENET_PACKET_FLAG_RELIABLE);
            putint(p, N_TELEPORT);
            putint(p, d->clientnum);
            putint(p, tp);
            putint(p, td);
            sendclientpacket(p.finalize(), 0);
            flushclient();
        }
    }

    void jumppadeffects(fpsent *d, int jp, bool local)
    {
        if(ents.inrange(jp) && ents[jp]->type == JUMPPAD)
        {
            extentity &e = *ents[jp];
            if(e.attr4 >= 0)
            {
                int snd = S_JUMPPAD, flags = 0;
                if(e.attr4 > 0) { snd = e.attr4; flags = SND_MAP; }
                playsound(snd, d == followingplayer() ? NULL : &e.o, NULL, flags);
            }
        }
        if(local && d->clientnum >= 0)
        {
			// act like the jump pad lets you touch the ground
			if(!d->jumpstate || (d->jumpstate == 2)) d->jumpstate = 1;
            sendposition(d);
            packetbuf p(16, ENET_PACKET_FLAG_RELIABLE);
            putint(p, N_JUMPPAD);
            putint(p, d->clientnum);
            putint(p, jp);
            sendclientpacket(p.finalize(), 0);
            flushclient();
        }
    }

    void teleport(int n, fpsent *d)
    {
        int e = -1, tag = ents[n]->attr1, beenhere = -1;
        for(;;)
        {
            e = findentity(TELEDEST, e+1);
            if(e==beenhere || e<0) { conoutf(CON_WARN, "no teleport destination for tag %d", tag); return; }
            if(beenhere<0) beenhere = e;
            if(ents[e]->attr2==tag)
            {
                teleporteffects(d, n, e, true);
                d->o = ents[e]->o;
                d->yaw = ents[e]->attr1;
                if(ents[e]->attr3 > 0)
                {
                    vec dir;
                    vecfromyawpitch(d->yaw, 0, 1, 0, dir);
                    float speed = d->vel.magnitude2();
                    d->vel.x = dir.x*speed;
                    d->vel.y = dir.y*speed;
                }
                else d->vel = vec(0, 0, 0);
                entinmap(d);
                updatedynentcache(d);
                ai::inferwaypoints(d, ents[n]->o, ents[e]->o, 16.f);
                break;
            }
        }
    }

    void trypickup(int n, fpsent *d)
    {
        extentity* e = ents[n];
        switch (e->type)
        {
            default:
                if (d->canpickup(e->type))
                {
                    addmsg(N_ITEMPICKUP, "rci", d, n);
                    e->setnopickup(); // even if someone else gets it first
                }
                break;

            case RACE_FINISH:
                if (m_race) {
                    addmsg(N_RACEFINISH, "rc", d);
                }
                d->lastpickup = ents[n]->type;
                d->lastpickupindex = n;
                d->lastpickupmillis = lastmillis;
                break;

            case RACE_START:
                if (m_race) {
                    addmsg(N_RACESTART, "rc", d);
                }
                d->lastpickup = ents[n]->type;
                d->lastpickupindex = n;
                d->lastpickupmillis = lastmillis;
                break;

            case RACE_CHECKPOINT:
                if (m_race) {
                    addmsg(N_RACECHECKPOINT, "rci", d, ents[n]->attr2);
                }
                d->lastpickup = ents[n]->type;
                d->lastpickupindex = n;
                d->lastpickupmillis = lastmillis;
                break;

            case TELEPORT:
            {
                if(d->lastpickup==e->type && lastmillis-d->lastpickupmillis<500) break;
                if(e->attr3 > 0)
                {
                    defformatstring(hookname, "can_teleport_%d", e->attr3);
                    if(identexists(hookname) && !execute(hookname)) break;
                }
                d->lastpickup = e->type;
                d->lastpickupmillis = lastmillis;
                teleport(n, d);
                break;
            }

            case JUMPPAD:
            {
                if(d->lastpickup==e->type && lastmillis-d->lastpickupmillis<300) break;
                d->lastpickup = e->type;
                d->lastpickupmillis = lastmillis;
                jumppadeffects(d, n, true);
                vec v((int)(char)e->attr3*10.0f, (int)(char)e->attr2*10.0f, e->attr1*12.5f);
                if(d->ai) d->ai->becareful = true;
				d->falling = vec(0, 0, 0);
				d->physstate = PHYS_FALL;
                d->timeinair = 1;
                d->vel = v;
                break;
            }
        }
    }

    void checkitems(fpsent *d)
    {
        if(d->state!=CS_ALIVE) return;
        vec o = d->feetpos();
        loopv(ents)
        {
            extentity &e = *ents[i];
            if(e.type==NOTUSED) continue;
            if((!e.spawned() || e.nopickup()) && e.type!=TELEPORT && e.type!=JUMPPAD && (m_race && e.type!=RACE_START && e.type!=RACE_FINISH && e.type!=RACE_CHECKPOINT) ) continue;
            float dist = e.o.dist(o);
            if(dist<(e.type==TELEPORT ? 16 : 12)) trypickup(i, d);
        }
    }

    void putitems(packetbuf &p)            // puts items in network stream and also spawns them locally
    {
        putint(p, N_ITEMLIST);
        loopv(ents) if(ents[i]->type==I_AMMO && (!m_noammo || ents[i]->type!=I_AMMO))
        {
            putint(p, i);
            putint(p, ents[i]->type);
        }
        putint(p, -1);
    }

    void resetspawns() { loopv(ents) { extentity *e = ents[i]; e->clearspawned(); e->clearnopickup(); } }

    void spawnitems(bool force)
    {
        if(m_noitems) return;
        loopv(ents)
        {
            extentity *e = ents[i];
            if(e->type==I_AMMO && (!m_noammo || e->type!=I_AMMO))
            {
                e->setspawned(force || !server::delayspawn(e->type));
                e->clearnopickup();
            }
        }
    }

    void setspawn(int i, bool on) { if(ents.inrange(i)) { extentity *e = ents[i]; e->setspawned(on); e->clearnopickup(); } }

    extentity *newentity() { return new fpsentity(); }
    void deleteentity(extentity *e) { delete (fpsentity *)e; }

    void clearents()
    {
        while(ents.length()) deleteentity(ents.pop());
    }

    enum
    {
        TRIG_COLLIDE    = 1<<0,
        TRIG_TOGGLE     = 1<<1,
        TRIG_ONCE       = 0<<2,
        TRIG_MANY       = 1<<2,
        TRIG_DISAPPEAR  = 1<<3,
        TRIG_AUTO_RESET = 1<<4,
        TRIG_RUMBLE     = 1<<5,
        TRIG_LOCKED     = 1<<6,
        TRIG_ENDSP      = 1<<7
    };

    static const int NUMTRIGGERTYPES = 32;

    static const int triggertypes[NUMTRIGGERTYPES] =
    {
        -1,
        TRIG_ONCE,                    // 1
        TRIG_RUMBLE,                  // 2
        TRIG_TOGGLE,                  // 3
        TRIG_TOGGLE | TRIG_RUMBLE,    // 4
        TRIG_MANY,                    // 5
        TRIG_MANY | TRIG_RUMBLE,      // 6
        TRIG_MANY | TRIG_TOGGLE,      // 7
        TRIG_MANY | TRIG_TOGGLE | TRIG_RUMBLE,    // 8
        TRIG_COLLIDE | TRIG_TOGGLE | TRIG_RUMBLE, // 9
        TRIG_COLLIDE | TRIG_TOGGLE | TRIG_AUTO_RESET | TRIG_RUMBLE, // 10
        TRIG_COLLIDE | TRIG_TOGGLE | TRIG_LOCKED | TRIG_RUMBLE,     // 11
        TRIG_DISAPPEAR,               // 12
        TRIG_DISAPPEAR | TRIG_RUMBLE, // 13
        TRIG_DISAPPEAR | TRIG_COLLIDE | TRIG_LOCKED, // 14
        -1 /* reserved 15 */,
        -1 /* reserved 16 */,
        -1 /* reserved 17 */,
        -1 /* reserved 18 */,
        -1 /* reserved 19 */,
        -1 /* reserved 20 */,
        -1 /* reserved 21 */,
        -1 /* reserved 22 */,
        -1 /* reserved 23 */,
        -1 /* reserved 24 */,
        -1 /* reserved 25 */,
        -1 /* reserved 26 */,
        -1 /* reserved 27 */,
        -1 /* reserved 28 */,
        TRIG_DISAPPEAR | TRIG_RUMBLE | TRIG_ENDSP, // 29
        -1 /* reserved 30 */,
        -1 /* reserved 31 */,
    };

    #define validtrigger(type) (triggertypes[(type) & (NUMTRIGGERTYPES-1)]>=0)
    #define checktriggertype(type, flag) (triggertypes[(type) & (NUMTRIGGERTYPES-1)] & (flag))

    static inline void cleartriggerflags(extentity &e)
    {
        e.flags &= ~(EF_ANIM | EF_NOVIS | EF_NOSHADOW | EF_NOCOLLIDE);
    }

    static inline void setuptriggerflags(fpsentity &e)
    {
        cleartriggerflags(e);
        e.flags |= EF_ANIM;
        if(checktriggertype(e.attr3, TRIG_COLLIDE|TRIG_DISAPPEAR)) e.flags |= EF_NOSHADOW;
        if(!checktriggertype(e.attr3, TRIG_COLLIDE)) e.flags |= EF_NOCOLLIDE;
        switch(e.triggerstate)
        {
            case TRIGGERING:
                if(checktriggertype(e.attr3, TRIG_COLLIDE) && lastmillis-e.lasttrigger >= 500) e.flags |= EF_NOCOLLIDE;
                break;
            case TRIGGERED:
                if(checktriggertype(e.attr3, TRIG_COLLIDE)) e.flags |= EF_NOCOLLIDE;
                break;
            case TRIGGER_DISAPPEARED:
                e.flags |= EF_NOVIS | EF_NOCOLLIDE;
                break;
        }
    }

    void resettriggers()
    {
        loopv(ents)
        {
            fpsentity &e = *(fpsentity *)ents[i];
            if(e.type != ET_MAPMODEL || !validtrigger(e.attr3)) continue;
            e.triggerstate = TRIGGER_RESET;
            e.lasttrigger = 0;
            setuptriggerflags(e);
        }
    }

    void unlocktriggers(int tag, int oldstate = TRIGGER_RESET, int newstate = TRIGGERING)
    {
        loopv(ents)
        {
            fpsentity &e = *(fpsentity *)ents[i];
            if(e.type != ET_MAPMODEL || !validtrigger(e.attr3)) continue;
            if(e.attr4 == tag && e.triggerstate == oldstate && checktriggertype(e.attr3, TRIG_LOCKED))
            {
                if(newstate == TRIGGER_RESETTING && checktriggertype(e.attr3, TRIG_COLLIDE) && overlapsdynent(e.o, 20)) continue;
                e.triggerstate = newstate;
                e.lasttrigger = lastmillis;
                if(checktriggertype(e.attr3, TRIG_RUMBLE)) playsound(S_RUMBLE, &e.o);
            }
        }
    }

    ICOMMAND(trigger, "ii", (int *tag, int *state),
    {
        if(*state) unlocktriggers(*tag);
        else unlocktriggers(*tag, TRIGGERED, TRIGGER_RESETTING);
    });

    VAR(triggerstate, -1, 0, 1);

    void doleveltrigger(int trigger, int state)
    {
        defformatstring(aliasname, "level_trigger_%d", trigger);
        if(identexists(aliasname))
        {
            triggerstate = state;
            execute(aliasname);
        }
    }

    void checktriggers()
    {
        if(player1->state != CS_ALIVE) return;
        vec o = player1->feetpos();
        loopv(ents)
        {
            fpsentity &e = *(fpsentity *)ents[i];
            if(e.type != ET_MAPMODEL || !validtrigger(e.attr3)) continue;
            switch(e.triggerstate)
            {
                case TRIGGERING:
                case TRIGGER_RESETTING:
                    if(lastmillis-e.lasttrigger>=1000)
                    {
                        if(e.attr4)
                        {
                            if(e.triggerstate == TRIGGERING) unlocktriggers(e.attr4);
                            else unlocktriggers(e.attr4, TRIGGERED, TRIGGER_RESETTING);
                        }
                        if(checktriggertype(e.attr3, TRIG_DISAPPEAR)) e.triggerstate = TRIGGER_DISAPPEARED;
                        else if(e.triggerstate==TRIGGERING && checktriggertype(e.attr3, TRIG_TOGGLE)) e.triggerstate = TRIGGERED;
                        else e.triggerstate = TRIGGER_RESET;
                    }
                    setuptriggerflags(e);
                    break;
                case TRIGGER_RESET:
                    if(e.lasttrigger)
                    {
                        if(checktriggertype(e.attr3, TRIG_AUTO_RESET|TRIG_MANY|TRIG_LOCKED) && e.o.dist(o)-player1->radius>=(checktriggertype(e.attr3, TRIG_COLLIDE) ? 20 : 12))
                            e.lasttrigger = 0;
                        break;
                    }
                    else if(e.o.dist(o)-player1->radius>=(checktriggertype(e.attr3, TRIG_COLLIDE) ? 20 : 12)) break;
                    else if(checktriggertype(e.attr3, TRIG_LOCKED))
                    {
                        if(!e.attr4) break;
                        doleveltrigger(e.attr4, -1);
                        e.lasttrigger = lastmillis;
                        break;
                    }
                    e.triggerstate = TRIGGERING;
                    e.lasttrigger = lastmillis;
                    setuptriggerflags(e);
                    if(checktriggertype(e.attr3, TRIG_RUMBLE)) playsound(S_RUMBLE, &e.o);
                    if(e.attr4) doleveltrigger(e.attr4, 1);
                    break;
                case TRIGGERED:
                    if(e.o.dist(o)-player1->radius<(checktriggertype(e.attr3, TRIG_COLLIDE) ? 20 : 12))
                    {
                        if(e.lasttrigger) break;
                    }
                    else if(checktriggertype(e.attr3, TRIG_AUTO_RESET))
                    {
                        if(lastmillis-e.lasttrigger<6000) break;
                    }
                    else if(checktriggertype(e.attr3, TRIG_MANY))
                    {
                        e.lasttrigger = 0;
                        break;
                    }
                    else break;
                    if(checktriggertype(e.attr3, TRIG_COLLIDE) && overlapsdynent(e.o, 20)) break;
                    e.triggerstate = TRIGGER_RESETTING;
                    e.lasttrigger = lastmillis;
                    setuptriggerflags(e);
                    if(checktriggertype(e.attr3, TRIG_RUMBLE)) playsound(S_RUMBLE, &e.o);
                    if(e.attr4) doleveltrigger(e.attr4, 0);
                    break;
            }
        }
    }

    void animatemapmodel(const extentity &e, int &anim, int &basetime)
    {
        const fpsentity &f = (const fpsentity &)e;
        if(validtrigger(f.attr3)) switch(f.triggerstate)
        {
            case TRIGGER_RESET: anim = ANIM_TRIGGER|ANIM_START; break;
            case TRIGGERING: anim = ANIM_TRIGGER; basetime = f.lasttrigger; break;
            case TRIGGERED: anim = ANIM_TRIGGER|ANIM_END; break;
            case TRIGGER_RESETTING: anim = ANIM_TRIGGER|ANIM_REVERSE; basetime = f.lasttrigger; break;
        }
    }

    void fixentity(extentity &e)
    {
        switch(e.type)
        {
            case FLAG:
            case TELEDEST:
                e.attr3 = e.attr2;
            case RACE_START:
            case RACE_FINISH:
            case RACE_CHECKPOINT:
                e.attr2 = e.attr1;
                break;
        }
    }

    void entradius(extentity &e, bool color)
    {
        int maxcheckpoints = 0;

        switch(e.type)
        {
            case TELEPORT:
                loopv(ents) if(ents[i]->type == TELEDEST && e.attr1==ents[i]->attr2)
                {
                    renderentarrow(e, vec(ents[i]->o).sub(e.o).normalize(), e.o.dist(ents[i]->o));
                    break;
                }
                break;

            case JUMPPAD:
                renderentarrow(e, vec((int)(char)e.attr3*10.0f, (int)(char)e.attr2*10.0f, e.attr1*12.5f).normalize(), 4);
                break;

            case RACE_START:
                loopv(ents) {
                    // successor
                    if(ents[i]->type == RACE_CHECKPOINT && ents[i]->attr2 == 1) {
                        renderentarrow(e, vec(ents[i]->o).sub(e.o).normalize(), e.o.dist(ents[i]->o));
                    }
                    // precessor
                    if(ents[i]->type == RACE_FINISH) {
                        renderentarrow(*ents[i], vec(e.o).sub(ents[i]->o).normalize(), ents[i]->o.dist(e.o));
                    }
                }
                break;

            case RACE_CHECKPOINT:
                loopv(ents) {
                    // successor
                    if(ents[i]->type == RACE_CHECKPOINT && (e.attr2+1) == ents[i]->attr2) {
                        renderentarrow(e, vec(ents[i]->o).sub(e.o).normalize(), e.o.dist(ents[i]->o));
                    }
                    // precessor
                    if(ents[i]->type == RACE_CHECKPOINT && (e.attr2-1) == ents[i]->attr2) {
                        renderentarrow(*ents[i], vec(e.o).sub(ents[i]->o).normalize(), ents[i]->o.dist(e.o));
                    }
                }
                break;

            case RACE_FINISH:
                loopv(ents) if(ents[i]->type == RACE_CHECKPOINT && ents[i]->attr2 > maxcheckpoints) {
                    maxcheckpoints = ents[i]->attr2;
                }
                // successor
                loopv(ents) if(ents[i]->type == RACE_START) {
                    renderentarrow(e, vec(ents[i]->o).sub(e.o).normalize(), e.o.dist(ents[i]->o));
                }
                // precessor
                loopv(ents) if(ents[i]->type == RACE_CHECKPOINT && ents[i]->attr2 == maxcheckpoints) {
                    renderentarrow(*ents[i], vec(e.o).sub(ents[i]->o).normalize(), ents[i]->o.dist(e.o));
                }
                break;

            case FLAG:
            case TELEDEST:
			{
				vec dir;
				vecfromyawpitch(e.attr1, 0, 1, 0, dir);
				renderentarrow(e, dir, 4);
				break;
			}
            case MAPMODEL:
                if(validtrigger(e.attr3)) renderentring(e, checktriggertype(e.attr3, TRIG_COLLIDE) ? 20 : 12);
                break;
        }
    }

    bool printent(extentity &e, char *buf, int len)
    {
        return false;
    }

    const char *entnameinfo(entity &e) { return ""; }
    const char *entname(int i)
    {
        static const char * const entnames[] =
        {
            "none?", "light", "mapmodel", "playerstart", "envmap", "particles", "sound", "spotlight",
            "health", "ammo", "start", "finish", "checkpoint", "placeholder4",
            "placeholder5", "placeholder6", "placeholder7", "placeholder8", "placeholder9",
            "teleport", "teledest",
            "placeholder10", "placeholder11", "jumppad",
            "base", "placeholder12",
            "placeholder13", "placeholder14",
            "placeholder15", "placeholder16",
            "flag",
        };
        return i>=0 && size_t(i)<sizeof(entnames)/sizeof(entnames[0]) ? entnames[i] : "";
    }

    void editent(int i, bool local)
    {
        extentity &e = *ents[i];
        if(e.type == ET_MAPMODEL && validtrigger(e.attr3))
        {
            fpsentity &f = (fpsentity &)e;
            f.triggerstate = TRIGGER_RESET;
            f.lasttrigger = 0;
            setuptriggerflags(f);
        }
        else cleartriggerflags(e);
        if(local) addmsg(N_EDITENT, "rii3ii5", i, (int)(e.o.x*DMF), (int)(e.o.y*DMF), (int)(e.o.z*DMF), e.type, e.attr1, e.attr2, e.attr3, e.attr4, e.attr5);
    }

    float dropheight(entity &e)
    {
        if(e.type==BASE || e.type==FLAG) return 0.0f;
        return 4.0f;
    }
#endif
}

