// Discord GameSDK

#include "game.h"
#include "engine.h"
#include "cube.h"

#ifdef DISCORD

discord::Core* core{};

namespace discord
{
	discord::Result whatAmI;

	void discordLogging(LogLevel level, const char* message) {
		conoutf(CON_DEBUG, "Discord debug: %s", message);
	}

	void initDiscord() {
		conoutf(CON_DEBUG, "Attempting to initialize Discord integration...");
		whatAmI = discord::Core::Create(623616609952464936, DiscordCreateFlags_NoRequireDiscord, &core);
		if(discord::connected()) {
			core->SetLogHook(LogLevel::Debug, discordLogging);
			conoutf(CON_DEBUG, "Discord intergation initialized successfully!");
		}
	}

	bool connected() {
		return whatAmI == discord::Result::Ok;
	}

	void updatePresence(int gamestate, const char* modename, physent* d, bool force)
	{
		if((globalgamestate != gamestate || force) && discord::connected()) {
			oldstring partykey;
			discord::Activity activity{};

			switch (gamestate)
			{
			case D_MENU:
				activity.SetState("In the menus");
				break;
			case D_PLAYING:
				activity.SetState("Playing");
				break;
			case D_SPECTATE:
				activity.SetState("Spectating");
				break;
			default:
				activity.SetState("SOMETHING BROKE");
				activity.SetDetails("Tell the #bugs channel about what you did!");
				activity.GetAssets().SetLargeImage("logo-large");
				activity.GetAssets().SetLargeText("https://hnss.ga/bug-report");
				activity.GetAssets().SetSmallImage("turkey-test");
				activity.GetAssets().SetSmallText("https://hnss.ga/bug-report");
				break;
			}
			if(gamestate != D_MENU) {
				const ENetAddress* address = connectedpeer();

				defformatstring(buffer, "Mode: %s", modename);
				activity.SetDetails(buffer);
				if(game::maplimit >= 0) {
					time_t curtime;
					uint32_t endtimedelta = (game::maplimit - lastmillis) / 1000;
					time(&curtime);
					activity.GetTimestamps().SetStart(0);
					activity.GetTimestamps().SetEnd((uint32_t)(curtime + endtimedelta));
				}
				else {
					activity.GetTimestamps().SetStart(0);
					activity.GetTimestamps().SetEnd(0);
				}
				activity.GetAssets().SetLargeImage(game::getclientmap());

				char* x[] = { // all maps with discord rpc keys
					"color",
					"cosmic",
					"duabo",
					"flux",
					"fz_burn",
					"highland",
					"hr",
					"illusion",
					"maze",
					"neo_falls",
					"neo_noir",
					"precipice",
					"retrograde",
					"ruins",
					"secondevermap",
					"zigguraut",
				};
				const char* s = game::getclientmap();
				const char* fin = "logo-large";
				int len = sizeof(x) / sizeof(x[0]);
				int i;

				for (i = 0; i < len; ++i)
				{
					if(!strcmp(x[i], s)) fin = game::getclientmap();
				}

				activity.GetAssets().SetLargeImage(fin);

				activity.GetAssets().SetLargeText(game::getclientmap());
				if(address) {
					if(enet_address_get_host_ip(address, partykey, sizeof(partykey)) >= 0)
					{
						defformatstring(newpartykey, "%s:%u", partykey, address->port);
						defformatstring(partyid, "%s%s", "srv=", newpartykey);
						activity.GetParty().SetId(partyid);
						activity.GetParty().GetSize().SetCurrentSize(game::players.length());
						activity.GetParty().GetSize().SetMaxSize(game::players.length() + 1);
						activity.GetSecrets().SetJoin(newpartykey);
					}
				}
				else {
					activity.SetState("Playing alone");
				}
			}
			else
			{
				activity.GetTimestamps().SetStart(starttime);
				activity.GetTimestamps().SetEnd(0);
				activity.GetAssets().SetLargeImage("logo-large");
			}

			fpsent* pl = (fpsent*)d;

			if(pl) // if there is actually a player involved
			{
				defformatstring(icon, "player-%d", pl->playermodel);
				//conoutf("imagekey: %s, playermodel: %d", icon, playermodel);
				activity.GetAssets().SetSmallImage(icon);
				defformatstring(plname, "%s (%d)", pl->name, pl->frags);
				activity.GetAssets().SetSmallText(plname);
			}
			core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
				
			});
			globalgamestate = gamestate;
		}
	}

	void discordCallbacks() {
		if(discord::connected()) core->RunCallbacks();
	}
}
#endif