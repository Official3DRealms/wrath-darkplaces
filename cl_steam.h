#pragma once

// Reki (May 5 2023): For use with steam integration via a launcher (very loosely based on Icculus' Steamshim)

extern cvar_t cv_steamid;

void Steam_Tick(void);
void Steam_Startup(void);

// exposed for use by builtins

// EXT_STEAM_REKI
void Steam_AchievementUnlock(const char *achID);
void Steam_AchievementQuery(const char *achID);
void Steam_StatSet(const char *statID, float statVal);
void Steam_StatIncrement(const char *statID, float statVal);
void Steam_StatQuery(const char *statID);
void Steam_StatWipeAll(void);
void Steam_RegisterAchievement(const char *achID);
void Steam_RegisterStat(const char *statID, int statType);

// EXT_CONTROLLER_REKI
void Controller_Poll(int index);