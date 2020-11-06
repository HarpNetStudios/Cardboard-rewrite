// main.cpp: initialisation & main loop

#include <engine.h>
#include <game.h>

oldstring gametoken = "OFFLINE";

extern void cleargamma();

void cleanup()
{
	rawinput::release();
	joystick::release();
    cleanupserver();
    SDL_ShowCursor(SDL_TRUE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    if(screen) SDL_SetWindowGrab(screen, SDL_FALSE);
    cleargamma();
    freeocta(worldroot);
    extern void clear_command(); clear_command();
    extern void clear_console(); clear_console();
    extern void clear_mdls();    clear_mdls();
    extern void clear_sound();   clear_sound();
    closelogfile();
    #ifdef __APPLE__
        if(screen) SDL_SetWindowFullscreen(screen, 0);
    #endif
    SDL_Quit();
}

extern void writeinitcfg();

void quit()                     // normal exit
{
    writeinitcfg();
    writeservercfg();
    abortconnect();
    disconnect();
    localdisconnect();
    writecfg();
    cleanup();
    exit(EXIT_SUCCESS);
}

void fatal(const char *s, ...)    // failure exit
{
    static int errors = 0;
    errors++;

    if(errors <= 2) // print up to one extra recursive error
    {
        defvformatstring(msg,s,s);
        logoutf("%s", msg);

        if(errors <= 1) // avoid recursion
        {
            if(SDL_WasInit(SDL_INIT_VIDEO))
            {
                SDL_ShowCursor(SDL_TRUE);
                SDL_SetRelativeMouseMode(SDL_FALSE);
                if(screen) SDL_SetWindowGrab(screen, SDL_FALSE);
                cleargamma();
                #ifdef __APPLE__
                    if(screen) SDL_SetWindowFullscreen(screen, 0);
                #endif
            }
            SDL_Quit();
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cardboard Engine fatal error", msg, NULL);
        }
    }
    exit(EXIT_FAILURE);
}

int curtime = 0, lastmillis = 1, elapsedtime = 0, totalmillis = 1, starttime = time(0);

dynent *player = NULL;

int initing = NOT_INITING;

bool initwarning(const char *desc, int level, int type)
{
    if(initing < level)
    {
        addchange(desc, type);
        return true;
    }
    return false;
}

VAR(desktopw, 1, 0, 0);
VAR(desktoph, 1, 0, 0);
int screenw = 0, screenh = 0;
SDL_Window *screen = NULL;
SDL_GLContext glcontext = NULL;

#define SCR_MINW 320
#define SCR_MINH 200
#define SCR_MAXW 10000
#define SCR_MAXH 10000
#define SCR_DEFAULTW 1024
#define SCR_DEFAULTH 768
VARF(scr_w, SCR_MINW, -1, SCR_MAXW, initwarning("screen resolution"));
VARF(scr_h, SCR_MINH, -1, SCR_MAXH, initwarning("screen resolution"));
VARF(depthbits, 0, 0, 32, initwarning("depth-buffer precision"));
VARF(fsaa, -1, -1, 16, initwarning("anti-aliasing"));

void writeinitcfg()
{
    stream *f = openutf8file("init.cfg", "w");
    if(!f) return;
    f->printf("// automatically written on exit, DO NOT MODIFY\n// modify settings in game\n");
    extern int fullscreen;
    f->printf("fullscreen %d\n", fullscreen);
    f->printf("scr_w %d\n", scr_w);
    f->printf("scr_h %d\n", scr_h);
    f->printf("depthbits %d\n", depthbits);
    f->printf("fsaa %d\n", fsaa);
	extern int usesound, soundchans, soundfreq, soundbufferlen;
	extern char* audiodriver;
	f->printf("usesound %d\n", usesound);
    f->printf("soundchans %d\n", soundchans);
    f->printf("soundfreq %d\n", soundfreq);
    f->printf("soundbufferlen %d\n", soundbufferlen);
	if(audiodriver[0]) f->printf("audiodriver %s\n", escapestring(audiodriver));
    delete f;
}

COMMAND(quit, "");

static void getbackgroundres(int &w, int &h)
{
    float wk = 1, hk = 1;
    if(w < 1024) wk = 1024.0f/w;
    if(h < 768) hk = 768.0f/h;
    wk = hk = max(wk, hk);
    w = int(ceil(w*wk));
    h = int(ceil(h*hk));
}

oldstring backgroundcaption = "";
Texture *backgroundmapshot = NULL;
oldstring backgroundmapname = "";
char *backgroundmapinfo = NULL;

void setbackgroundinfo(const char *caption = NULL, Texture *mapshot = NULL, const char *mapname = NULL, const char *mapinfo = NULL)
{
    renderedframe = false;
    copystring(backgroundcaption, caption ? caption : "");
    backgroundmapshot = mapshot;
    copystring(backgroundmapname, mapname ? mapname : "");
    if(mapinfo != backgroundmapinfo)
    {
        DELETEA(backgroundmapinfo);
        if(mapinfo) backgroundmapinfo = newstring(mapinfo);
    }
}

void restorebackground(bool force = false)
{
    if(renderedframe)
    {
        if(!force) return;
        setbackgroundinfo();
    }
    renderbackground(backgroundcaption[0] ? backgroundcaption : NULL, backgroundmapshot, backgroundmapname[0] ? backgroundmapname : NULL, backgroundmapinfo, true);
}

void bgquad(float x, float y, float w, float h, float tx = 0, float ty = 0, float tw = 1, float th = 1)
{
    gle::begin(GL_TRIANGLE_STRIP);
    gle::attribf(x,   y);   gle::attribf(tx,      ty);
    gle::attribf(x+w, y);   gle::attribf(tx + tw, ty);
    gle::attribf(x,   y+h); gle::attribf(tx,      ty + th);
    gle::attribf(x+w, y+h); gle::attribf(tx + tw, ty + th);
    gle::end();
}

void renderbackground(const char *caption, Texture *mapshot, const char *mapname, const char *mapinfo, bool restore, bool force, bool splash)
{
    if(!inbetweenframes && !force) return;

	if(!restore || force || !splash) stopsounds(); // stop sounds while loading

    int w = screenw, h = screenh;
    if(forceaspect) w = int(ceil(h*forceaspect));
    getbackgroundres(w, h);
    gettextres(w, h);

    static int lastupdate = -1, lastw = -1, lasth = -1;
	static float backgroundu = 0, backgroundv = 0;
    if((renderedframe && !mainmenu && lastupdate != lastmillis) || lastw != w || lasth != h)
    {
        lastupdate = lastmillis;
        lastw = w;
        lasth = h;

        backgroundu = rndscale(1);
        backgroundv = rndscale(1);
    }
    else if(lastupdate != lastmillis) lastupdate = lastmillis;

    for(int i = 0; i < int(restore ? 1 : 3); ++i)
    {
        hudmatrix.ortho(0, w, h, 0, -1, 1);
        resethudmatrix();

        hudshader->set();
        gle::colorf(1, 1, 1);

        gle::defvertex(2);
        gle::deftexcoord0();

		
		if(!(mapshot || mapname)) {
			/*background and logo*/
			float bu = w * 0.67f / 256.0f + backgroundu, bv = h * 0.67f / 256.0f + backgroundv;
			if(splash) settexture("data/splash.png", 0);
			else settexture("data/background.png", 0);

			if(splash) bgquad(0, 0, w, h);
			else bgquad(0, 0, w, h, 0, 0, bu, bv);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glEnable(GL_BLEND);
			float lh = 0.5f * min(w, h), lw = lh * 2,
				lx = 0.5f * (w - lw), ly = 0.5f * (h * 0.5f - lh);
			if(!splash) 
			{
				settexture((maxtexsize ? min(maxtexsize, hwtexsize) : hwtexsize) >= 1024 && (screenw > 1280 || screenh > 800) ? "data/logo_1024.png" : "data/logo.png", 3);
				bgquad(lx, ly, lw, lh);
			}
		
			/*cardboard badge*/
			float badgeh = 0.12f*min(w, h), badgew = badgeh, badgex = 20, badgey = (h - badgeh - 20);
			if(!splash)
			{
				settexture("data/cardboard.png", 3);
				bgquad(badgex, badgey, badgew, badgeh);
			}

			/*cube 2 badge*/
			badgex = (w - badgew - 20);
			if(!splash && mainmenu)
			{
				settexture("data/cube.png", 3);
				bgquad(badgex, badgey, badgew, badgeh);
			}
		}
		else {
			// blank black box, used for map load
			gle::colorf(0,0,0);
			bgquad(0, 0, w, h);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glEnable(GL_BLEND);
		}

        if(caption)
        {
            int tw = text_width(caption);
            float tsz = 0.04f*min(w, h)/FONTH,
                  tx = 0.5f*(w - tw*tsz), ty = h - 0.075f*1.5f*min(w, h) - 1.25f*FONTH*tsz;
            pushhudmatrix();
            hudmatrix.translate(tx, ty, 0);
            hudmatrix.scale(tsz, tsz, 1);
            flushhudmatrix();
            draw_text(caption, 0, 0);
            pophudmatrix();
        }
        if(mapshot || mapname)
        {
            int infowidth = 12*FONTH;
			float sz = 0.35f * min(w, h);
            if(mapinfo)
            {
                int mw, mh;
                text_bounds(mapinfo, mw, mh, infowidth);
				//int tw = text_width(mapinfo);
				float tsz = sz / (8 * FONTH),
					tx = (w / 2) - (mw / 4),
					ty = 0.125f * sz - FONTH * tsz;
				pushhudmatrix();
				//hudmatrix.translate(x+sz+FONTH*msz, y, 0);
				//hudmatrix.translate((0.5f * w) - (infowidth / 2), 0, 0);
				hudmatrix.translate(tx, ty, 0);
				hudmatrix.scale(tsz, tsz, 1);
				flushhudmatrix();
				draw_text(mapinfo, 0, 0, 0xFF, 0xFF, 0xFF);
				pophudmatrix();
            }
            if(mapshot && mapshot!=notexture)
            {
                glBindTexture(GL_TEXTURE_2D, mapshot->id);
				bgquad(0, h/10, w, h-(h/10));
            }
            if(mapname)
            {
                int tw = text_width(mapname);
				float tsz = sz / (7 * FONTH),
					//tx = (0.5f * w) - (tw/2),
					tx = (w/2) - (tw/2),
					ty = 0.275f * sz - FONTH * tsz;
                pushhudmatrix();
				hudmatrix.translate(tx, ty, 0);
                hudmatrix.scale(tsz, tsz, 1);
                flushhudmatrix();
                draw_text(mapname, 0, 0, 0xFF, 0x24, 0x00);
                pophudmatrix();
            }
        }
        glDisable(GL_BLEND);
        if(!restore) swapbuffers(false);
    }

    if(!restore) setbackgroundinfo(caption, mapshot, mapname, mapinfo);

	if(splash) playsound(S_KILLBONUS);  // sound for splash screen
}

VAR(progressbackground, 0, 0, 1);

float loadprogress = 0;

void renderprogress(float bar, const char *text, GLuint tex, bool background)   // also used during loading
{
    if(!inbetweenframes || drawtex) return;

    extern int menufps, maxfps;
    int fps = menufps ? (maxfps ? min(maxfps, menufps) : menufps) : maxfps;
    if(fps)
    {
        static int lastprogress = 0;
        int ticks = SDL_GetTicks(), diff = ticks - lastprogress;
        if(bar > 0 && diff >= 0 && diff < (1000 + fps-1)/fps) return;
        lastprogress = ticks;
    }

    clientkeepalive();      // make sure our connection doesn't time out while loading maps etc.

    #ifdef __APPLE__
    interceptkey(SDLK_UNKNOWN); // keep the event queue awake to avoid 'beachball' cursor
    #endif

    extern int mesa_swap_bug, curvsync;
    bool forcebackground = progressbackground || (mesa_swap_bug && (curvsync || totalmillis==1));
    if(background || forcebackground) restorebackground(forcebackground);

    int w = screenw, h = screenh;
    if(forceaspect) w = int(ceil(h*forceaspect));
    getbackgroundres(w, h);
    gettextres(w, h);

    hudmatrix.ortho(0, w, h, 0, -1, 1);
    resethudmatrix();

    hudshader->set();
    gle::colorf(1, 1, 1);

    gle::defvertex(2);
    gle::deftexcoord0();

    /*float fh = 0.075f*min(w, h), fw = fh*10,
          fx = renderedframe ? w - fw - fh/4 : 0.5f*(w - fw),
          fy = renderedframe ? fh/4 : h - fh*1.5f,
          fu1 = 0/512.0f, fu2 = 511/512.0f,
          fv1 = 0/64.0f, fv2 = 52/64.0f;*/
	float fh = h/30, fw = w,
		fx = 0,
		fy = (h / 30) * 29.8f,
		fu1 = 0 / 512.0f, fu2 = 511 / 512.0f,
		fv1 = 0 / 64.0f, fv2 = 52 / 64.0f;
    settexture("data/loading_frame.png", 3);
    bgquad(fx, fy, fw, fh, fu1, fv1, fu2-fu1, fv2-fv1);
	//bgquad(0,(h/20)*19, w, h / 20, fu1, fv1, fu2 - fu1, fv2 - fv1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float bw = fw/**(511 - 2*17)/511.0f*/, bh = fh/**20/52.0f*/,
          bx = fx/* + fw*17/511.0f*/, by = fy/* + fh*16/52.0f*/,
          bv1 = 0/32.0f, bv2 = 20/32.0f,
          su1 = 0/32.0f, su2 = 7/32.0f, sw = fw*7/511.0f,
          eu1 = 23/32.0f, eu2 = 30/32.0f, ew = fw*7/511.0f,
          mw = bw - sw - ew,
          ex = bx+sw + max(mw*bar, fw*7/511.0f);
    if(bar > 0)
    {
        settexture("data/loading_bar.png", 3);
        gle::begin(GL_QUADS);
        gle::attribf(bx,    by);    gle::attribf(su1, bv1);
        gle::attribf(bx+sw, by);    gle::attribf(su2, bv1);
        gle::attribf(bx+sw, by+bh); gle::attribf(su2, bv2);
        gle::attribf(bx,    by+bh); gle::attribf(su1, bv2);

        gle::attribf(bx+sw, by);    gle::attribf(su2, bv1);
        gle::attribf(ex,    by);    gle::attribf(eu1, bv1);
        gle::attribf(ex,    by+bh); gle::attribf(eu1, bv2);
        gle::attribf(bx+sw, by+bh); gle::attribf(su2, bv2);

        gle::attribf(ex,    by);    gle::attribf(eu1, bv1);
        gle::attribf(ex+ew, by);    gle::attribf(eu2, bv1);
        gle::attribf(ex+ew, by+bh); gle::attribf(eu2, bv2);
        gle::attribf(ex,    by+bh); gle::attribf(eu1, bv2);
        gle::end();
    }

    if(text)
    {
        int tw = text_width(text);
        float tsz = bh*0.8f/FONTH;
        if(tw*tsz > mw) tsz = mw/tw;
        pushhudmatrix();
        hudmatrix.translate(bx+sw, by + (bh - FONTH*tsz)/2, 0);
        hudmatrix.scale(tsz, tsz, 1);
        flushhudmatrix();
        draw_text(text, 0, 0);
        pophudmatrix();
    }

    glDisable(GL_BLEND);

    if(tex)
    {
        glBindTexture(GL_TEXTURE_2D, tex);
        float sz = 0.35f*min(w, h), x = 0.5f*(w-sz), y = 0.5f*min(w, h) - sz/15;
        bgquad(x, y, sz, sz);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        settexture("data/mapshot_frame.png", 3);
        bgquad(x, y, sz, sz);
        glDisable(GL_BLEND);
    }

    swapbuffers(false);
}

int keyrepeatmask = 0, textinputmask = 0;
Uint32 textinputtime = 0;
VAR(textinputfilter, 0, 5, 1000);

void keyrepeat(bool on, int mask)
{
    if(on) keyrepeatmask |= mask;
    else keyrepeatmask &= ~mask;
}

void textinput(bool on, int mask)
{
    if(on)
    {
        if(!textinputmask)
        {
            SDL_StartTextInput();
            textinputtime = SDL_GetTicks();
        }
        textinputmask |= mask;
    }
    else if(textinputmask)
    {
        textinputmask &= ~mask;
        if(!textinputmask) SDL_StopTextInput();
    }
}

VARNP(relativemouse, userelativemouse, 0, 1, 1);

bool shouldgrab = false, grabinput = false, minimized = false, canrelativemouse = true, relativemouse = false;

void inputgrab(bool on)
{
    if(on)
    {
        SDL_ShowCursor(SDL_FALSE);
        if(canrelativemouse && userelativemouse)
        {
            if(SDL_SetRelativeMouseMode(SDL_TRUE) >= 0)
            {
                SDL_SetWindowGrab(screen, SDL_TRUE);
                relativemouse = true;
            }
            else
            {
                SDL_SetWindowGrab(screen, SDL_FALSE);
                canrelativemouse = false;
                relativemouse = false;
            }
        }
    }
    else
    {
        SDL_ShowCursor(SDL_TRUE);
        if(relativemouse)
        {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_SetWindowGrab(screen, SDL_FALSE);
            relativemouse = false;
        }
    }
    shouldgrab = false;
}

bool initwindowpos = false;

void setfullscreen(int enable)
{
    if(!screen) return;
    //initwarning(enable ? "fullscreen" : "windowed");
    extern int fullscreen, fullscreendesktop;
    if(fullscreendesktop && enable) {
        enable = fullscreen = 2;
        fullscreendesktop = 0;
    }
    SDL_SetWindowFullscreen(screen, enable ? ((enable == 2) ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN) : 0);
    if(!enable)
    {
        SDL_SetWindowSize(screen, scr_w, scr_h);
        if(initwindowpos)
        {
            int winx = SDL_WINDOWPOS_CENTERED, winy = SDL_WINDOWPOS_CENTERED;
            SDL_SetWindowPosition(screen, winx, winy);
            initwindowpos = false;
        }
    }
}

VARF(fullscreen, 0, 0, 2, setfullscreen(fullscreen));

void resetfullscreen()
{
    setfullscreen(false);
    setfullscreen(true);
}

VAR(fullscreendesktop, 0, 0, 1);

void screenres(int w, int h)
{
    scr_w = clamp(w, SCR_MINW, SCR_MAXW);
    scr_h = clamp(h, SCR_MINH, SCR_MAXH);
    if(screen)
    {
        if(fullscreen == 2)
        {
            scr_w = min(scr_w, desktopw);
            scr_h = min(scr_h, desktoph);
        }
        if(SDL_GetWindowFlags(screen) & SDL_WINDOW_FULLSCREEN)
        {
            if(fullscreen == 2) gl_resize();
            else resetfullscreen();
        }
        else SDL_SetWindowSize(screen, scr_w, scr_h);
    }
    else
    {
        initwarning("screen resolution");
    }
}

ICOMMAND(screenres, "ii", (int *w, int *h), screenres(*w, *h));

static void setgamma(int val)
{
    if(screen && SDL_SetWindowBrightness(screen, val/100.0f) < 0) conoutf(CON_ERROR, "Could not set gamma: %s", SDL_GetError());
}

static int curgamma = 100;
VARFNP(gamma, reqgamma, 30, 100, 300,
{
    if(initing || reqgamma == curgamma) return;
    curgamma = reqgamma;
    setgamma(curgamma);
});

void restoregamma()
{
	if(initing || reqgamma == 100) return;
	curgamma = reqgamma;
    setgamma(curgamma);
}

void cleargamma()
{
    if(curgamma != 100 && screen) SDL_SetWindowBrightness(screen, 1.0f);
}

int curvsync = -1;
void restorevsync()
{
    if(initing || !glcontext) return;
    extern int vsync, vsynctear;
    if(!SDL_GL_SetSwapInterval(vsync ? (vsynctear ? -1 : 1) : 0))
        curvsync = vsync;
}

VARFP(vsync, 0, 0, 1, restorevsync());
VARFP(vsynctear, 0, 0, 1, { if(vsync) restorevsync(); });

static void SetSDLIcon(SDL_Window* window)
{
	static const struct {
		uint  	 width;
		uint  	 height;
		uint  	 bytes_per_pixel; /* 2:RGB16, 3:RGB, 4:RGBA */
		Uint8 	 pixel_data[32 * 32 * 4 + 1];
	} cardboard_icon = {
      32, 32, 4,
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\020\040\004\000\177u\020\000\257U\014\000\277+\005\000_\000\000\000\040\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\060v\021\000\357\357\"\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\221\025\000\337\000\000\000\060\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\040\207\023\000\357\377$\000\377\377$\000\377\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\207\023\000\357\000\000\000\040\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\063\006\000\240\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377F\012\000\257\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\040\240\026\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377\220\024\000\377\000\000\000\020\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000p\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377\357\"\000\377\000\000\000`\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\040\000\000\000\020\000\000"
      "\000\000L\013\000\240\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\071\007\000\220\000\000\000\000\000"
      "\000\000\040\000\000\000\040\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000`U\014\000\277\220\024\000\377\237\026\000\377"
      "\237\026\000\377\210\023\000\357/\005\000\357\240\026\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\357\"\000\377\220\024\000"
      "\377D\012\000\357\232\025\000\357\237\026\000\377\237\026\000\377\220\024\000\377U\014\000"
      "\277\000\000\000_\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020@\011"
      "\000\277\337\037\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377"
      "\203\021\000\377\247\023\000\377g\015\000\377\277\033\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\277\033\000\377s\016\000\377\263\024\000\377\204\022\000\377\377$"
      "\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\337\037\000\377/\006\000\257"
      "\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\063\006\000\240\357\"\000\377\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377|\021\000\377\277\026"
      "\000\377\277\026\000\377\203\017\000\377\213\023\000\377\377$\000\377\377$\000\377\213\023"
      "\000\377\217\020\000\377\277\026\000\377\277\026\000\377\214\023\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\357\"\000\377\071\007\000\217"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\040\240\026\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\277\033\000\377\247\023\000"
      "\377\277\026\000\377\277\026\000\377\217\020\000\377\277\033\000\377\237\026\000\377\233"
      "\022\000\377\277\026\000\377\277\026\000\377\233\022\000\377\277\033\000\377\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\220\024"
      "\000\377\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000@\317\035\000\377\377$\000\377\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\357\"\000\377o\015\000"
      "\377\277\026\000\377\277\026\000\377\277\026\000\377g\015\000\377g\014\000\377\277\026\000"
      "\377\277\026\000\377\277\026\000\377g\015\000\377\377$\000\377\377$\000\377\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\317\035\000\377\000\000"
      "\000@\000\000\000\000\000\000\000\000\000\000\000\060\317\035\000\377\377$\000\377\377$\000\377\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\233\026\000\377\247"
      "\023\000\377\277\026\000\377\217\020\000\377T#\000\377p.\000\377\203\017\000\377\277\026\000"
      "\377\247\023\000\377\240\026\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\377$\000\377\277\033\000\377\000\000\000@\000\000\000\000"
      "\000\000\000\000\000\000\000\020\210\023\000\357\377$\000\377\377$\000\377\377$\000\377\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377w\020\000\377`\013\000\377"
      "\277{\000\377\377\244\000\377\377\244\000\377\237f\000\377w\016\000\377\207\023\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000"
      "\377\377$\000\377\377$\000\377m\017\000\337\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\040\004"
      "\000\200\357\"\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377"
      "\377$\000\377\277\033\000\377o\016\000\377o\015\000\377X,\000\377\377\244\000\377\377\244"
      "\000\377\377\244\000\377\377\244\000\377S#\000\377o\015\000\377o\016\000\377\240\026\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\337\037"
      "\000\377@\010\000\200\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000;\011\000\317\357\""
      "\000\377\377$\000\377\377$\000\377\377$\000\377\337\037\000\377o\016\000\377\217\020\000\377"
      "\277\026\000\377\277\026\000\377{/\000\377\377\244\000\377\377\244\000\377\377\244\000"
      "\377\377\244\000\377p.\000\377\277\026\000\377\277\026\000\377\247\023\000\377w\020\000\377"
      "\337\037\000\377\377$\000\377\377$\000\377\377$\000\377\357\"\000\377;\011\000\317\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020/\006\000\257\337\037\000\377"
      "\377$\000\377\337\037\000\377g\015\000\377\277\026\000\377\277\026\000\377\277\026\000\377"
      "\277\026\000\377w\015\000\377\317\205\000\377\377\244\000\377\377\244\000\377\277{\000"
      "\377w\015\000\377\277\026\000\377\277\026\000\377\277\026\000\377\277\026\000\377h\015\000"
      "\377\337\037\000\377\377$\000\377\317\035\000\377/\006\000\257\000\000\000\020\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000`n\017\000\337D\011\000"
      "\377\277\026\000\377\277\026\000\377\277\026\000\377\277\026\000\377\263\024\000\377o\015"
      "\000\377\060\005\000\377\177\070\000\377{\060\000\377<\006\000\377\177\017\000\377\247\023\000\377"
      "\277\026\000\377\277\026\000\377\277\026\000\377\277\026\000\377S\013\000\377u\020\000\317"
      "\000\000\000_\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000@\010\000\177\257\031\000\377\214\023\000\377\204\021\000\377\204"
      "\021\000\377\207\023\000\377\237\026\000\377\337\037\000\377w\016\000\377\277\026\000\377\277"
      "\026\000\377w\016\000\377\337\037\000\377\237\026\000\377\207\023\000\377\217\022\000\377x"
      "\020\000\377\233\026\000\377\257\031\000\377$\005\000p\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\200\022\000"
      "\337\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\337"
      "\037\000\377w\016\000\377\277\026\000\377\277\026\000\377w\016\000\377\357\"\000\377\377$"
      "\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\210\023\000\357"
      "\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000P\337\037\000\377\377$\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377{\017\000\377\277\026\000\377\277\026\000"
      "\377o\015\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\337\037\000\377\000\000\000@\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\034\004\000\220\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377"
      "p\016\000\377\277\026\000\377\277\026\000\377\200\020\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\034\004\000\220"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000f\016\000\240\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\257\031\000\377\247\023\000\377\247\023\000\377"
      "\240\026\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377M\013\000\237\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000L\013\000\240\377$\000\377\377$"
      "\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377"
      "\377$\000\377p\016\000\377\\\013\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000"
      "\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\071\007\000\220\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000p\357\"\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377"
      "$\000\377\377$\000\377\377$\000\377\377$\000\377w\021\000\357\210\023\000\357\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000"
      "\377\337\037\000\377\000\000\000_\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\210\023\000\357\377$\000\377\377$\000"
      "\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\210\023\000\357"
      "\000\000\000\060\000\000\000@\210\023\000\357\377$\000\377\377$\000\377\377$\000\377\377$\000\377"
      "\377$\000\377\377$\000\377\377$\000\377\222\025\000\337\000\000\000\020\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000P\220\024\000\377\377$\000\377\377$\000\377\377$\000\377\377$\000\377\317\035\000\377"
      "U\014\000\277\000\000\000\040\000\000\000\000\000\000\000\000\000\000\000\040n\017\000\337\337\037\000\377\377$"
      "\000\377\377$\000\377\377$\000\377\377$\000\377\210\023\000\357\000\000\000\060\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\040\063\006\000\240b\016\000\317n\017\000\337M\013\000\237\000"
      "\000\000@\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000`]\015\000\257v\020"
      "\000\317v\020\000\317\034\004\000\220\000\000\000\040\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
    };

// these masks are needed to tell SDL_CreateRGBSurface(From)
// to assume the data it gets is byte-wise RGB(A) data
	Uint32 rmask, gmask, bmask, amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	int shift = (my_icon.bytes_per_pixel == 3) ? 8 : 0;
	rmask = 0xff000000 >> shift;
	gmask = 0x00ff0000 >> shift;
	bmask = 0x0000ff00 >> shift;
	amask = 0x000000ff >> shift;
#else // little endian, like x86
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
	amask = (cardboard_icon.bytes_per_pixel == 3) ? 0 : 0xff000000;
#endif
	SDL_Surface* icon = SDL_CreateRGBSurfaceFrom((void*)cardboard_icon.pixel_data,
		cardboard_icon.width, cardboard_icon.height, cardboard_icon.bytes_per_pixel * 8,
		cardboard_icon.bytes_per_pixel * cardboard_icon.width, rmask, gmask, bmask, amask);

	SDL_SetWindowIcon(window, icon);

	SDL_FreeSurface(icon);
}

void setupscreen()
{
    if(glcontext)
    {
        SDL_GL_DeleteContext(glcontext);
        glcontext = NULL;
    }
    if(screen)
    {
        SDL_DestroyWindow(screen);
        screen = NULL;
    }
    curvsync = -1;

    SDL_Rect desktop;
    if(SDL_GetDisplayBounds(0, &desktop) < 0) fatal("failed querying desktop bounds: %s", SDL_GetError());
    desktopw = desktop.w;
    desktoph = desktop.h;

    if(scr_h < 0) scr_h = fullscreen ? desktoph : SCR_DEFAULTH;
    if(scr_w < 0) scr_w = (scr_h*desktopw)/desktoph;
    scr_w = clamp(scr_w, SCR_MINW, SCR_MAXW);
    scr_h = clamp(scr_h, SCR_MINH, SCR_MAXH);
    if(fullscreen == 2)
    {
        scr_w = min(scr_w, desktopw);
        scr_h = min(scr_h, desktoph);
    }

    int winx = SDL_WINDOWPOS_UNDEFINED, winy = SDL_WINDOWPOS_UNDEFINED, winw = scr_w, winh = scr_h, flags = SDL_WINDOW_RESIZABLE;
    if(fullscreen)
    {
        if(fullscreen == 2)
        {
            winw = desktopw;
            winh = desktoph;
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
        else flags |= SDL_WINDOW_FULLSCREEN;
        initwindowpos = true;
    }

    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    static const int configs[] =
    {
        0x3, /* try everything */
        0x2, 0x1, /* try disabling one at a time */
        0 /* try disabling everything */
    };
    int config = 0;
    if(!depthbits) SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    if(!fsaa)
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    }
    for(int i = 0; i < int(sizeof(configs)/sizeof(configs[0])); ++i)
    {
        config = configs[i];
        if(!depthbits && config&1) continue;
        if(fsaa<=0 && config&2) continue;
        if(depthbits) SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, config&1 ? depthbits : 24);
        if(fsaa>0)
        {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, config&2 ? 1 : 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config&2 ? fsaa : 0);
        }
		defformatstring(vers, "%s %s %s", game::gametitle, game::gamestage, game::gameversion);
        screen = SDL_CreateWindow(vers, winx, winy, winw, winh, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS | flags);
		SetSDLIcon(screen);
        if(!screen) continue;

    #ifdef __APPLE__
        static const int glversions[] = { 32, 20 };
    #else
        static const int glversions[] = { 33, 32, 31, 30, 20 };
    #endif
        for(int j = 0; j < int(sizeof(glversions)/sizeof(glversions[0])); ++j)
        {
            glcompat = glversions[j] <= 30 ? 1 : 0;
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, glversions[j] / 10);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, glversions[j] % 10);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, glversions[j] >= 32 ? SDL_GL_CONTEXT_PROFILE_CORE : 0);
            glcontext = SDL_GL_CreateContext(screen);
            if(glcontext) break;
        }
        if(glcontext) break;
    }
    if(!screen) fatal("failed to create OpenGL window: %s", SDL_GetError());
    else if(!glcontext) fatal("failed to create OpenGL context: %s", SDL_GetError());
    else
    {
        if(depthbits && (config&1)==0) conoutf(CON_WARN, "%d bit z-buffer not supported - disabling", depthbits);
        if(fsaa>0 && (config&2)==0) conoutf(CON_WARN, "%dx anti-aliasing not supported - disabling", fsaa);
    }

    SDL_SetWindowMinimumSize(screen, SCR_MINW, SCR_MINH);
    SDL_SetWindowMaximumSize(screen, SCR_MAXW, SCR_MAXH);

    SDL_GetWindowSize(screen, &screenw, &screenh);
}

void resetgl()
{
    clearchanges(CHANGE_GFX);

    renderbackground("resetting OpenGL");

    extern void cleanupva();
    extern void cleanupparticles();
    extern void cleanupdecals();
    extern void cleanupblobs();
    extern void cleanupsky();
    extern void cleanupmodels();
    extern void cleanupprefabs();
    extern void cleanuplightmaps();
    extern void cleanupblendmap();
    extern void cleanshadowmap();
    extern void cleanreflections();
    extern void cleanupglare();
    extern void cleanupdepthfx();
    cleanupva();
    cleanupparticles();
    cleanupdecals();
    cleanupblobs();
    cleanupsky();
    cleanupmodels();
    cleanupprefabs();
    cleanuptextures();
    cleanuplightmaps();
    cleanupblendmap();
    cleanshadowmap();
    cleanreflections();
    cleanupglare();
    cleanupdepthfx();
    cleanupshaders();
    cleanupgl();

    setupscreen();
    inputgrab(grabinput);
    gl_init();

    inbetweenframes = false;
    if(!reloadtexture(*notexture) ||
       !reloadtexture("data/logo.png") ||
       !reloadtexture("data/logo_1024.png") ||
       !reloadtexture("data/background.png") ||
       !reloadtexture("data/mapshot_frame.png") ||
       !reloadtexture("data/loading_frame.png") ||
       !reloadtexture("data/loading_bar.png") ||
       !reloadtexture("data/cardboard.png") ||
       !reloadtexture("data/cube.png"))
        fatal("failed to reload core texture");
    reloadfonts();
    inbetweenframes = true;
    renderbackground("initializing...");
    restoregamma();
    restorevsync();
    reloadshaders();
    reloadtextures();
    initlights();
	#ifdef DISCORD
		discord::updatePresence(discord::D_MENU);
	#endif
    allchanged(true);
}

COMMAND(resetgl, "");

vector<SDL_Event> events;

void pushevent(const SDL_Event &e)
{
    events.add(e);
}

static bool filterevent(const SDL_Event &event)
{
    switch(event.type)
    {
        case SDL_MOUSEMOTION:
            if(grabinput && !relativemouse && !(SDL_GetWindowFlags(screen) & SDL_WINDOW_FULLSCREEN))
            {
                if(event.motion.x == screenw / 2 && event.motion.y == screenh / 2)
                    return false;  // ignore any motion events generated by SDL_WarpMouse
                #ifdef __APPLE__
                if(event.motion.y == 0)
                    return false;  // let mac users drag windows via the title bar
                #endif
            }
            break;
    }
    return true;
}

static inline bool pollevent(SDL_Event &event)
{
    while(SDL_PollEvent(&event))
    {
        if(filterevent(event)) return true;
    }
    return false;
}

bool interceptkey(int sym)
{
    static int lastintercept = SDLK_UNKNOWN;
    int len = lastintercept == sym ? events.length() : 0;
    SDL_Event event;
    while(pollevent(event))
    {
        switch(event.type)
        {
            case SDL_MOUSEMOTION: break;
            default: pushevent(event); break;
        }
    }
    lastintercept = sym;
    if(sym != SDLK_UNKNOWN) for(int i = len; i < events.length(); i++)
    {
		if(events[i].type == SDL_KEYDOWN && events[i].key.keysym.sym == sym) { events.remove(i); return true; }
    }
    return false;
}

static void ignoremousemotion()
{
    SDL_Event e;
    SDL_PumpEvents();
    while(SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION));
}

static void resetmousemotion()
{
    if(grabinput && !relativemouse && !(SDL_GetWindowFlags(screen) & SDL_WINDOW_FULLSCREEN))
    {
        SDL_WarpMouseInWindow(screen, screenw / 2, screenh / 2);
    }
}

static void checkmousemotion(int &dx, int &dy)
{
    loopv(events)
    {
        SDL_Event &event = events[i];
        if(event.type != SDL_MOUSEMOTION)
        {
            if(i > 0) events.remove(0, i);
            return;
        }
        dx += event.motion.xrel;
        dy += event.motion.yrel;
    }
    events.setsize(0);
    SDL_Event event;
    while(pollevent(event))
    {
        if(event.type != SDL_MOUSEMOTION)
        {
            events.add(event);
            return;
        }
        dx += event.motion.xrel;
        dy += event.motion.yrel;
    }
}

void checkinput()
{
    SDL_Event event;
    //int lasttype = 0, lastbut = 0;
	bool mousemoved = false;
	if(rawinput::enabled) rawinput::flush();
    while(events.length() || pollevent(event))
    {
        if(events.length()) event = events.remove(0);

        switch(event.type)
        {
            case SDL_QUIT:
                quit();
                return;

            case SDL_TEXTINPUT:
                if(textinputmask && int(event.text.timestamp-textinputtime) >= textinputfilter)
                {
                    uchar buf[SDL_TEXTINPUTEVENT_TEXT_SIZE+1];
                    size_t len = decodeutf8(buf, sizeof(buf)-1, (const uchar *)event.text.text, strlen(event.text.text));
                    if(len > 0) { buf[len] = '\0'; processtextinput((const char *)buf, len); }
                }
                break;

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if(keyrepeatmask || !event.key.repeat)
                   processkey(event.key.keysym.sym, event.key.state==SDL_PRESSED, event.key.keysym.mod | SDL_GetModState());
                break;

            case SDL_WINDOWEVENT:
                switch(event.window.event)
                {
                    case SDL_WINDOWEVENT_CLOSE:
                        quit();
                        break;

                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                        shouldgrab = true;
                        break;
                    case SDL_WINDOWEVENT_ENTER:
                        inputgrab(grabinput = true);
                        break;

                    case SDL_WINDOWEVENT_LEAVE:
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        inputgrab(grabinput = false);
                        break;

                    case SDL_WINDOWEVENT_MINIMIZED:
                        minimized = true;
                        break;

                    case SDL_WINDOWEVENT_MAXIMIZED:
                    case SDL_WINDOWEVENT_RESTORED:
                        minimized = false;
                        break;

                    case SDL_WINDOWEVENT_RESIZED:
                        break;

                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                    {
                        SDL_GetWindowSize(screen, &screenw, &screenh);
                        if(fullscreen != 2 || !(SDL_GetWindowFlags(screen) & SDL_WINDOW_FULLSCREEN))
                        {
                            scr_w = clamp(screenw, SCR_MINW, SCR_MAXW);
                            scr_h = clamp(screenh, SCR_MINH, SCR_MAXH);
                        }
                        gl_resize();
                        break;
                    }
                }
                break;

            case SDL_MOUSEMOTION:
				if(rawinput::debugrawmouse)
				{
					conoutf("%d sdl mouse motion (%d, %d)",
						lastmillis, event.motion.xrel, event.motion.yrel);
				}
				if(grabinput && !rawinput::enabled)
                {
                    int dx = event.motion.xrel, dy = event.motion.yrel;
                    checkmousemotion(dx, dy);
                    if(!g3d_movecursor(dx, dy)) mousemove(dx, dy);
                    mousemoved = true;
                }
                else if(shouldgrab) inputgrab(grabinput = true);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
				if(rawinput::enabled) break;
                //if(lasttype==event.type && lastbut==event.button.button) break; // why?? get event twice without it
                switch(event.button.button)
                {
                    case SDL_BUTTON_LEFT: processkey(-1, event.button.state==SDL_PRESSED); break;
                    case SDL_BUTTON_MIDDLE: processkey(-2, event.button.state==SDL_PRESSED); break;
                    case SDL_BUTTON_RIGHT: processkey(-3, event.button.state==SDL_PRESSED); break;
                    case SDL_BUTTON_X1: processkey(-6, event.button.state==SDL_PRESSED); break;
                    case SDL_BUTTON_X2: processkey(-7, event.button.state==SDL_PRESSED); break;
                }
                //lasttype = event.type;
                //lastbut = event.button.button;
                break;

            case SDL_MOUSEWHEEL:
                if(event.wheel.y > 0) { processkey(-4, true); processkey(-4, false); }
                else if(event.wheel.y < 0) { processkey(-5, true); processkey(-5, false); }
                break;

			case SDL_JOYAXISMOTION:
			case SDL_JOYBALLMOTION:
			case SDL_JOYHATMOTION:
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
				joystick::handleevent(event);
        }
    }
    if(mousemoved) resetmousemotion();
}

void swapbuffers(bool overlay)
{
    gle::disable();
    SDL_GL_SwapWindow(screen);
}

VAR(menufps, 0, 60, 1000);
VARP(maxfps, 0, 200, 1000);

void limitfps(int &millis, int curmillis)
{
    int limit = (mainmenu || minimized) && menufps ? (maxfps ? min(maxfps, menufps) : menufps) : maxfps;
    if(!limit) return;
    static int fpserror = 0;
    int delay = 1000/limit - (millis-curmillis);
    if(delay < 0) fpserror = 0;
    else
    {
        fpserror += 1000%limit;
        if(fpserror >= limit)
        {
            ++delay;
            fpserror -= limit;
        }
        if(delay > 0)
        {
            SDL_Delay(delay);
            millis += delay;
        }
    }
}

#if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
void stackdumper(unsigned int type, EXCEPTION_POINTERS *ep)
{
    if(!ep) fatal("unknown type");
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *context = ep->ContextRecord;
    char out[512];
    formatstring(out, "Cardboard Engine Win32 Exception: 0x%x [0x%x]\n\n", er->ExceptionCode, er->ExceptionCode==EXCEPTION_ACCESS_VIOLATION ? er->ExceptionInformation[1] : -1);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
#ifdef _AMD64_
	STACKFRAME64 sf = {{context->Rip, 0, AddrModeFlat}, {}, {context->Rbp, 0, AddrModeFlat}, {context->Rsp, 0, AddrModeFlat}, 0};
    while(::StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
	{
		union { IMAGEHLP_SYMBOL64 sym; char symext[sizeof(IMAGEHLP_SYMBOL64) + sizeof(oldstring)]; };
		sym.SizeOfStruct = sizeof(sym);
		sym.MaxNameLength = sizeof(symext) - sizeof(sym);
		IMAGEHLP_LINE64 line;
		line.SizeOfStruct = sizeof(line);
        DWORD64 symoff;
		DWORD lineoff;
        if(SymGetSymFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &symoff, &sym) && SymGetLineFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &lineoff, &line))
#else
    STACKFRAME sf = {{context->Eip, 0, AddrModeFlat}, {}, {context->Ebp, 0, AddrModeFlat}, {context->Esp, 0, AddrModeFlat}, 0};
    while(::StackWalk(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
	{
		union { IMAGEHLP_SYMBOL sym; char symext[sizeof(IMAGEHLP_SYMBOL) + sizeof(oldstring)]; };
		sym.SizeOfStruct = sizeof(sym);
		sym.MaxNameLength = sizeof(symext) - sizeof(sym);
		IMAGEHLP_LINE line;
		line.SizeOfStruct = sizeof(line);
        DWORD symoff, lineoff;
        if(SymGetSymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &symoff, &sym) && SymGetLineFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &lineoff, &line))
#endif
        {
            char *del = strrchr(line.FileName, '\\');
            concformatstring(out, "%s - %s [%d]\n", sym.Name, del ? del + 1 : line.FileName, line.LineNumber);
        }
    }
    fatal(out);
}
#endif

#define MAXFPSHISTORY 60

int fpspos = 0, fpshistory[MAXFPSHISTORY];

void resetfpshistory()
{
    for(int i = 0; i < int(MAXFPSHISTORY); ++i) fpshistory[i] = 1;
    fpspos = 0;
}

void updatefpshistory(int millis)
{
    fpshistory[fpspos++] = max(1, min(1000, millis));
    if(fpspos>=MAXFPSHISTORY) fpspos = 0;
}

void getfps(int &fps, int &bestdiff, int &worstdiff)
{
    int total = fpshistory[MAXFPSHISTORY-1], best = total, worst = total;
    for(int i = 0; i < int(MAXFPSHISTORY-1); ++i)
    {
        int millis = fpshistory[i];
        total += millis;
        if(millis < best) best = millis;
        if(millis > worst) worst = millis;
    }

    fps = (1000*MAXFPSHISTORY)/total;
    bestdiff = 1000/best-fps;
    worstdiff = fps-1000/worst;
}

void getfps_(int *raw)
{
    int fps, bestdiff, worstdiff;
    if(*raw) fps = 1000/fpshistory[(fpspos+MAXFPSHISTORY-1)%MAXFPSHISTORY];
    else getfps(fps, bestdiff, worstdiff);
    intret(fps);
}

COMMANDN(getfps, getfps_, "i");

bool inbetweenframes = false, renderedframe = true;

static bool findarg(int argc, char **argv, const char *str)
{
    for(int i = 1; i<argc; i++) if(strstr(argv[i], str)==argv[i]) return true;
    return false;
}

static int clockrealbase = 0, clockvirtbase = 0;
static void clockreset() { clockrealbase = SDL_GetTicks(); clockvirtbase = totalmillis; }
VARFP(clockerror, 990000, 1000000, 1010000, clockreset());
VARFP(clockfix, 0, 0, 1, clockreset());

int getclockmillis()
{
    int millis = SDL_GetTicks() - clockrealbase;
    if(clockfix) millis = int(millis*(double(clockerror)/1000000));
    millis += clockvirtbase;
    return max(millis, totalmillis);
}

VAR(numcpus, 1, 1, 16);

void testcurl_(char* targetUrl) {
	char* thing = web_get(targetUrl, true);
	conoutf(CON_ECHO, "%s", thing);
}

COMMANDN(testcurl, testcurl_, "s");

void testcurlpost_(char* targetUrl, char* postFields) {
    char* thing = web_post(targetUrl, postFields, true);
    conoutf(CON_ECHO, "%s", thing);
}

COMMANDN(testcurlpost, testcurlpost_, "ss");

VARFP(offline, 0, 0, 1, { getuserinfo_(false); });

void getuserinfo_(bool debug, bool first) {
	if(offline) return; // don't waste time trying to check everything if we are offline.
	if(!strcmp(gametoken, "OFFLINE")) // check if playing without logging into launcher
	{
		if(!first) conoutf(CON_ERROR, "\f3[HNID] Please restart the game and log in with your HNID to play online!");
		offline = 1;
		return;
	}
	oldstring apiurl;
	formatstring(apiurl, "%s/game/get/userinfo?id=1", HNAPI);
	char* thing = web_auth(apiurl, gametoken, debug);
	if(!thing[0]) {
		conoutf(CON_ERROR, "\f3[HNID] No data recieved from server, switching to offline mode");
		offline = 1;
		return; 
	}
	cJSON *json = cJSON_Parse(thing);

	// error handling
	const cJSON* status = cJSON_GetObjectItemCaseSensitive(json, "status");
	const cJSON* message = cJSON_GetObjectItemCaseSensitive(json, "message");
	if(cJSON_IsNumber(status) && cJSON_IsString(message)) {
		if(status->valueint > 0) {
			conoutf(CON_ERROR, "\f3[HNID] Web error! status: %d, \"%s\"", status->valueint, message->valuestring);
			if(!strcmp(message->valuestring, "no token found") || !strcmp(message->valuestring, "malformed token")) {
				offline = 1;
				return;
			}
		}
		else {
			// actual parse
			const cJSON* name = NULL;
			name = cJSON_GetObjectItemCaseSensitive(json, "username");
            const cJSON* pubt = NULL;
            pubt = cJSON_GetObjectItemCaseSensitive(json, "public");
			if(cJSON_IsString(name) && (name->valuestring != NULL))
			{
                game::switchname(name->valuestring);
                if(cJSON_IsString(pubt) && (pubt->valuestring != NULL))
                {
                    game::setpubtoken(pubt->valuestring);
                }
				if(!first) conoutf(CON_INFO, "\f1[HNID] Successfully logged in as %s!", name->valuestring);
			}
		}
	}
	else {
		conoutf(CON_ERROR, "\f3[HNID] Malformed JSON recieved from server (server blocked?)");
        logoutf("[MALFORMED JSON]: %s", thing);
		offline = 1;
	}
}

COMMANDN(getuserinfo, getuserinfo_, "i");

ICOMMAND(getgametoken, "", (), result(gametoken));

int globalgamestate = -1;

#define MAXTOKENLEN 64

void setgametoken(const char* token) {
	filtertext(gametoken, token, false, false, MAXTOKENLEN);
	if(player!=NULL) getuserinfo_(false);
}

int main(int argc, char **argv)
{
    #ifdef WIN32
    //atexit((void (__cdecl *)(void))_CrtDumpMemoryLeaks);
    #ifndef _DEBUG
    #ifndef __GNUC__
    __try {
    #endif
    #endif
    #endif

    setlogfile(NULL);

    int dedicated = 0;
    char *load = NULL, *initscript = NULL;

    initing = INIT_RESET;
    // set home dir first
    for(int i = 1; i<argc; i++) if(argv[i][0]=='-' && argv[i][1] == 'q') { sethomedir(&argv[i][2]); break; }
    // set log after home dir, but before anything else
    for(int i = 1; i<argc; i++) if(argv[i][0]=='-' && argv[i][1] == 'g')
    {
        const char *file = argv[i][2] ? &argv[i][2] : "log.txt";
        setlogfile(file);
        logoutf("Setting log file: %s", file);
        break;
    }

	// get gametoken, you son of a bitch
	for (int i = 1; i < argc; i++) if(argv[i][0] == '-' && argv[i][1] == 'c') { setgametoken(&argv[i][2]); break; }
	if(!gametoken[0]) fatal("no gametoken given");
	if(!strcmp(gametoken, "OFFLINE")) offline = 1;

    execfile("init.cfg", false);
    for(int i = 1; i<argc; i++)
    {
        if(argv[i][0]=='-') switch(argv[i][1])
        {
			// reordered alpha to make it easier to see what's being used already. -Y 03/14/19
			case 'a': fsaa = atoi(&argv[i][2]); break;
			case 'b': /* compat, ignore */ break;
			case 'c': if(gametoken[0]) logoutf("Using game token: [REDACTED]"); break;
			case 'd': dedicated = atoi(&argv[i][2]); if(dedicated <= 0) dedicated = 2; break;
			case 'f': /* compat, ignore */ break;
			case 'g': break;
			case 'h': scr_h = clamp(atoi(&argv[i][2]), SCR_MINH, SCR_MAXH); if(!findarg(argc, argv, "-w")) scr_w = -1; break;
			case 'k':
			{
				const char *dir = addpackagedir(&argv[i][2]);
				if(dir) logoutf("Adding package directory: %s", dir);
				break;
			}
			case 'l':
			{
				char pkgdir[] = "packages/";
				load = strstr(path(&argv[i][2]), path(pkgdir));
				if(load) load += sizeof(pkgdir) - 1;
				else load = &argv[i][2];
				break;
			}
            case 'q': if(homedir[0]) logoutf("Using home directory: %s", homedir); break;
			case 'r': /* compat, ignore */ break;
			case 's': /* compat, ignore */ break;
			case 't': fullscreen = atoi(&argv[i][2]); break;
			case 'v': /* compat, ignore */ break;
            case 'w': scr_w = clamp(atoi(&argv[i][2]), SCR_MINW, SCR_MAXW); if(!findarg(argc, argv, "-h")) scr_h = -1; break;
			case 'x': initscript = &argv[i][2]; break;
            case 'z': depthbits = atoi(&argv[i][2]); break;
            default: if(!serveroption(argv[i])) gameargs.add(argv[i]); break;
        }
        else gameargs.add(argv[i]);
    }
    initing = NOT_INITING;

    numcpus = clamp(SDL_GetCPUCount(), 1, 64); // i wonder if this will break anything -Y 09/13/19

    if(dedicated <= 1)
    {
        logoutf("init: sdl");

        if(SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO|SDL_INIT_AUDIO)<0) fatal("Unable to initialize SDL: %s", SDL_GetError());
    }

    logoutf("init: net");
    if(enet_initialize()<0) fatal("Unable to initialise network module");
    atexit(enet_deinitialize);
    enet_time_set(0);

    logoutf("init: game");
    game::parseoptions(gameargs);
    initserver(dedicated>0, dedicated>1);  // never returns if dedicated
    ASSERT(dedicated <= 1);
    game::initclient();

    logoutf("init: video");
    SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "0");
    #if !defined(WIN32) && !defined(__APPLE__)
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    #endif
    setupscreen();
    SDL_ShowCursor(SDL_FALSE);
    SDL_StopTextInput(); // workaround for spurious text-input events getting sent on first text input toggle?

    logoutf("init: gl");
    gl_checkextensions();
    gl_init();
    notexture = textureload("packages/textures/notexture.png");
    if(!notexture) fatal("could not find core textures (are you using the launcher?)");

    logoutf("init: console");
	if(!execfile("data/stdlib.cfg", false)) fatal("cannot find data files (are you using the launcher?)"); // this is the first config file we load.
    if(!execfile("data/lang.cfg", false)) fatal("cannot find lang config"); // after this point in execution, translations are safe to use.
    if(!execfile("data/font.cfg", false)) fatal("cannot find font definitions");
    if(!setfont("default")) fatal("no default font specified");

	logoutf("init: sound");
	execfile("data/sounds.cfg"); // load sounds early
	initsound();

    inbetweenframes = true;
	renderbackground(NULL, NULL, NULL, NULL, false, false, true);
    //renderbackground("initializing...");

    logoutf("init: world");
    camera1 = player = game::iterdynents(0);
    emptymap(0, true, NULL, false);

    logoutf("init: cfg");
    initing = INIT_LOAD;
    execfile("data/keymap.cfg");
    execfile("data/stdedit.cfg");
    execfile("data/menus.cfg"); 
    execfile("data/heightmap.cfg");
    execfile("data/blendbrush.cfg");
    defformatstring(gamecfgname, "data/game_%s.cfg", game::gameident());
    execfile(gamecfgname);
    if(game::savedservers()) execfile(game::savedservers(), false);

    identflags |= IDF_PERSIST;

    if(!execfile(game::savedconfig(), false))
    {
        execfile(game::defaultconfig());
        writecfg(game::restoreconfig());
    }
    execfile(game::autoexec(), false);

    identflags &= ~IDF_PERSIST;

    initing = INIT_GAME;
    game::loadconfigs();

    initing = NOT_INITING;

	logoutf("init: auth");
	renderprogress(0, "connecting to auth server...");
	getuserinfo_(false, true);

    logoutf("init: render");
    restoregamma();
    restorevsync();
    loadshaders();
    initparticles();
    initdecals();

    identflags |= IDF_PERSIST;
	#ifdef DISCORD
		logoutf("init: discord");
		discord::initDiscord();
		discord::updatePresence(discord::D_MENU);
	#endif

    logoutf("init: mainloop");

    if(execfile("once.cfg", false)) remove(findfile("once.cfg", "rb"));
	if(execfile("update.cfg", false)) remove(findfile("update.cfg", "rb"));

    if(load)
    {
        logoutf("init: localconnect");
        //localconnect();
        game::changemap(load);
    }

    if(initscript) execute(initscript);

    initmumble();
    resetfpshistory();

    inputgrab(grabinput = true);
    ignoremousemotion();

    for(;;)
    {
        static int frames = 0;
        int millis = getclockmillis();
        limitfps(millis, totalmillis);
        elapsedtime = millis - totalmillis;
        static int timeerr = 0;
        int scaledtime = game::scaletime(elapsedtime) + timeerr;
        curtime = scaledtime/100;
        timeerr = scaledtime%100;
        if(!multiplayer(false) && curtime>200) curtime = 200;
        if(game::ispaused()) curtime = 0;
		lastmillis += curtime;
        totalmillis = millis;
        updatetime();

		//SDL_SetWindowTitle(screen, SDL_GetWindowTitle(screen));

        checkinput();
        menuprocess();
        tryedit();

        if(lastmillis) game::updateworld();

        checksleep(lastmillis);

        serverslice(false, 0);

        if(frames) updatefpshistory(elapsedtime);
        frames++;

        // miscellaneous general game effects
        recomputecamera();
        updateparticles();
        updatesounds();

        if(minimized) continue;

        inbetweenframes = false;
		if(mainmenu) {
			gl_drawmainmenu(); 
			#ifdef DISCORD
				discord::updatePresence(discord::D_MENU);
			#endif
		}
        else gl_drawframe();
		//gl_drawframe();
        swapbuffers();
        renderedframe = inbetweenframes = true;
        #ifdef DISCORD
		    discord::discordCallbacks();
        #endif
    }

    ASSERT(0);
    return EXIT_FAILURE;

    #if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
    } __except(stackdumper(0, GetExceptionInformation()), EXCEPTION_CONTINUE_SEARCH) { return 0; }
    #endif
}
