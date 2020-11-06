#ifndef __CUBE_H__
#define __CUBE_H__

#define _FILE_OFFSET_BITS 64

#ifdef WIN32
#define _USE_MATH_DEFINES
#endif
#include <cmath>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstdarg>
#include <climits>
#include <cassert>
#include <ctime>

// include C++ standard libs here

#include <string>
#include <thread>
#include <mutex>

#ifdef WIN32
  #define WIN32_LEAN_AND_MEAN
  #ifdef _WIN32_WINNT
  #undef _WIN32_WINNT
  #endif
  #define _WIN32_WINNT 0x0501
  #include "windows.h"
  #ifndef _WINDOWS
    #define _WINDOWS
  #endif
  #ifndef __GNUC__
    #include <eh.h>
    #include <dbghelp.h>
    #include <intrin.h>
  #endif
  #define ZLIB_DLL
#endif

#ifndef STANDALONE
  #ifdef __APPLE__
    #include "SDL2/SDL.h"
    #define GL_GLEXT_LEGACY
    #define __glext_h_
    #include <OpenGL/gl.h>
    #define main SDL_main
  #else
    #include <SDL.h>
    #include <SDL_opengl.h>
  #endif
#endif

#ifdef DISCORD
#include <discord-game-sdk/discord_game_sdk.h>
#include <discord-game-sdk/discord.h>
#endif

#include <enet/enet.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <zlib.h>

#include "translate.h"

#include "tools.h"
#include "geom.h"
#include "ents.h"
#include "command.h"

#ifndef STANDALONE
#include "glexts.h"
#include "glemu.h"
#endif

#include "iengine.h"
#include "igame.h"
#include "game.h"

#endif

