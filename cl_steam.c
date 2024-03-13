
#if __has_include("../steamlauncher/wrath_common.h")

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
typedef HANDLE PipeType;
#define NULLPIPE NULL
typedef unsigned __int8 uint8;
typedef __int32 int32;
typedef unsigned __int64 uint64;
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
typedef uint8_t uint8;
typedef int32_t int32;
typedef uint64_t uint64;
typedef int PipeType;
#define NULLPIPE -1
#endif
typedef uint8 byte;

#include "quakedef.h"
#include "csprogs.h"
#include "cl_steam.h"
#include "../steamlauncher/wrath_common.h"

#ifdef CONFIG_MENU
#include "menu.h"
#endif

qboolean steamConnected;
char SteamID[MAX_PIPESTRING];
void(*func_readarray[SV_MAX])();

#ifdef _WIN32
static int pipeReady(PipeType fd)
{
	DWORD avail = 0;
	return (PeekNamedPipe(fd, NULL, 0, NULL, &avail, NULL) && (avail > 0));
} /* pipeReady */

static int writePipe(PipeType fd, const void *buf, const unsigned int _len)
{
	const DWORD len = (DWORD)_len;
	DWORD bw = 0;
	return ((WriteFile(fd, buf, len, &bw, NULL) != 0) && (bw == len));
} /* writePipe */

static int readPipe(PipeType fd, void *buf, const unsigned int _len)
{
	DWORD avail = 0;
	PeekNamedPipe(fd, NULL, 0, NULL, &avail, NULL);
	if (avail < _len)
		return 0;

	const DWORD len = (DWORD)_len;
	DWORD br = 0;
	return ReadFile(fd, buf, len, &br, NULL) ? (int)br : 0;
} /* readPipe */

static void closePipe(PipeType fd)
{
	CloseHandle(fd);
} /* closePipe */

static char *getEnvVar(const char *key, char *buf, const size_t _buflen)
{
	const DWORD buflen = (DWORD)_buflen;
	const DWORD rc = GetEnvironmentVariableA(key, buf, buflen);
	/* rc doesn't count null char, hence "<". */
	return ((rc > 0) && (rc > buflen)) ? NULL : buf;
} /* getEnvVar */

#else

static int pipeReady(PipeType fd)
{
	int rc;
	struct pollfd pfd = { fd, POLLIN | POLLERR | POLLHUP, 0 };
	while (((rc = poll(&pfd, 1, 0)) == -1) && (errno == EINTR)) { /*spin*/ }
	return (rc == 1);
} /* pipeReady */

static int writePipe(PipeType fd, const void *buf, const unsigned int _len)
{
	const ssize_t len = (ssize_t)_len;
	ssize_t bw;
	while (((bw = write(fd, buf, len)) == -1) && (errno == EINTR)) { /*spin*/ }
	return (bw == len);
} /* writePipe */

static int readPipe(PipeType fd, void *buf, const unsigned int _len)
{
	if (!pipeReady(fd))
		return 0;

	return read(fd, buf, (ssize_t)_len);
	//const ssize_t len = (ssize_t)_len;
	//ssize_t br;
	//while (((br = read(fd, buf, len)) == -1) && (errno == EINTR)) { /*spin*/ }
	//return (int)br == -1 ? (int)br : 0;
} /* readPipe */

static void closePipe(PipeType fd)
{
	close(fd);
} /* closePipe */

static char *getEnvVar(const char *key, char *buf, const size_t buflen)
{
	const char *envr = getenv(key);
	if (!envr || (strlen(envr) >= buflen))
		return NULL;
	strlcpy(buf, envr, buflen);
	return buf;
} /* getEnvVar */

#endif

static PipeType GPipeRead = NULLPIPE;
static PipeType GPipeWrite = NULLPIPE;

static int initPipes(void)
{
	char buf[64];
	unsigned long long val;

	if (!getEnvVar("STEAMSHIM_READHANDLE", buf, sizeof(buf)))
	{
		Con_Printf("STEAM PIPE: could not read STEAMSHIM_READHANDLE\n");
		return 0;
	}
	else if (sscanf(buf, "%llu", &val) != 1)
		return 0;
	else
		GPipeRead = (PipeType)val;

	if (!getEnvVar("STEAMSHIM_WRITEHANDLE", buf, sizeof(buf)))
	{
		Con_Printf("STEAM PIPE: could not read STEAMSHIM_WRITEHANDLE\n");
		return 0;
	}
	else if (sscanf(buf, "%llu", &val) != 1)
		return 0;
	else
		GPipeWrite = (PipeType)val;

	Con_Printf("STEAM PIPE: pipes initialized\n");

	return ((GPipeRead != NULLPIPE) && (GPipeWrite != NULLPIPE));
} /* initPipes */


typedef struct
{
	byte	data[MAX_PIPEBUFFSIZE];
	int		cursize;
} pipebuff_t;

pipebuff_t pipeSendBuffer;


int PIPE_SendData()
{
	unsigned long bytes_written;
	int succ = writePipe(GPipeWrite, pipeSendBuffer.data, pipeSendBuffer.cursize);

	if (succ)
		pipeSendBuffer.cursize = 0;

	return succ;
}


float PIPE_ReadFloat()
{
	float dat;
	int succ = readPipe(GPipeRead, &dat, 4);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


signed long PIPE_ReadLong()
{
	signed long dat;
	int succ = readPipe(GPipeRead, &dat, 4);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


long long PIPE_ReadLongLong()
{
	long long dat;
	int succ = readPipe(GPipeRead, &dat, 8);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


signed short PIPE_ReadShort()
{
	signed short dat;
	int succ = readPipe(GPipeRead, &dat, 2);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


byte PIPE_ReadByte()
{
	byte dat;
	int succ = readPipe(GPipeRead, &dat, 1);
	if (!succ)
	{
		return -1;
	}

	return dat;
}


int PIPE_ReadString(char *buff)
{
	unsigned long amount_written;

	int i;
	for (i = 0; i < MAX_PIPESTRING; i++)
	{
		readPipe(GPipeRead, buff + i, 1);
		if (buff[i] == 0)
			break;
	}
	amount_written = i;

	return amount_written;
}


void PIPE_ReadCharArray(char *into, unsigned long *size)
{
	*size = (unsigned long)PIPE_ReadShort();
	int succ = readPipe(GPipeRead, into, *size);
	if (!succ)
	{
		*size = 0;
	}
}





int PIPE_WriteFloat(float dat_float)
{
	long dat;
	memcpy(&dat, &dat_float, sizeof(long));

	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;
	pipeSendBuffer.data[seek + 2] = (dat >> 16) & 0xFF;
	pipeSendBuffer.data[seek + 3] = (dat >> 24) & 0xFF;

	pipeSendBuffer.cursize += 4;

	return true;
}


int PIPE_WriteLong(signed long dat)
{
	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;
	pipeSendBuffer.data[seek + 2] = (dat >> 16) & 0xFF;
	pipeSendBuffer.data[seek + 3] = (dat >> 24) & 0xFF;

	pipeSendBuffer.cursize += 4;

	return true;
}


int PIPE_WriteLongLong(long long dat)
{
	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;
	pipeSendBuffer.data[seek + 2] = (dat >> 16) & 0xFF;
	pipeSendBuffer.data[seek + 3] = (dat >> 24) & 0xFF;
	pipeSendBuffer.data[seek + 4] = (dat >> 32) & 0xFF;
	pipeSendBuffer.data[seek + 5] = (dat >> 40) & 0xFF;
	pipeSendBuffer.data[seek + 6] = (dat >> 48) & 0xFF;
	pipeSendBuffer.data[seek + 7] = (dat >> 56) & 0xFF;

	pipeSendBuffer.cursize += 8;

	return true;
}


int PIPE_WriteShort(signed short dat)
{
	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat & 0xFF;
	pipeSendBuffer.data[seek + 1] = (dat >> 8) & 0xFF;

	pipeSendBuffer.cursize += 2;

	return true;
}


int PIPE_WriteByte(unsigned char dat)
{
	int seek = pipeSendBuffer.cursize;
	pipeSendBuffer.data[seek] = dat;
	pipeSendBuffer.cursize += 1;

	return true;
}


int PIPE_WriteString(const char *str)
{
	int str_length = strlen(str);
	memcpy(&(pipeSendBuffer.data[pipeSendBuffer.cursize]), str, str_length);

	if (str[str_length - 1] != NULL)
		pipeSendBuffer.data[pipeSendBuffer.cursize + str_length] = NULL; str_length++;

	pipeSendBuffer.cursize += str_length;

	return true;
}


int PIPE_WriteCharArray(char *dat, unsigned long size)
{
	PIPE_WriteShort((signed short)size);

	int seek = pipeSendBuffer.cursize;
	memcpy(&(pipeSendBuffer.data[seek]), dat, size);
	pipeSendBuffer.cursize += size;

	return true;
}

// achievement stuff
#if ACHIEVEMENT_HARDCODE
Achievement_t g_Achievements[] =
{
	ACHIEVEMENT_LIST
};
Stat_t g_Stats[] =
{
	STAT_LIST
};
#endif

// packet reads
void Steam_Print(void)
{
	char msg[MAX_PIPESTRING];
	PIPE_ReadString(msg);
	Con_DPrintf(msg);
}

void Steam_SetName(void)
{
	Con_DPrintf("Steam: Set Name\n");

	char name[MAX_PIPESTRING];
	PIPE_ReadString(name);
	Cvar_Set("steam_name", name);

	Con_DPrintf("New Name: %s\n", name);
}


void Steam_SetSteamID(void)
{
	PIPE_ReadString(SteamID);
	Con_Printf("Set SteamID: %s\n", SteamID);

	//cvar_t cv_steamid;
	//memcpy(&cv_steamid.string, SteamID, sizeof(cv_steamid.string));
	//cv_steamid.name = "steam_id";
	//cv_steamid.flags = 1u << 5;

	const char *id = SteamID;
	Cvar_Set("steam_id", id);

	steamConnected = true;

	//cvarfuncs->SetString(cv_steamid.name, cv_steamid.string);
	//Cvar_Get(cv_steamid.name, cv_steamid.string, cv_steamid.flags, "steam");
	//Cvar_Register(&cv_steamid, "steam");
}

void Steam_AchievementValue(void)
{
	char ach_name[MAX_PIPESTRING];
	qboolean ach_value;

	PIPE_ReadString(ach_name);
	ach_value = (qboolean)PIPE_ReadByte();

	//CL_VM_Steam_AchievementValue(ach_name, ach_value);

#if ACHIEVEMENT_HARDCODE
	for (int iAch = 0; iAch < ACHIEVEMENTS_MAX; ++iAch)
	{
		Achievement_t *ach = &g_Achievements[iAch];
		if (stricmp(ach_name, ach->m_pchAchievementID)) // search for matching achievement
			continue;
		ach->m_bAchieved = ach_value;
		CL_VM_Steam_AchievementValue(ach_name, ach_value);
#ifdef CONFIG_MENU
		MR_VM_Steam_AchievementValue(ach_name, ach_value);
#endif
		break;
	}
#else
	CL_VM_Steam_AchievementValue(ach_name, ach_value);
#ifdef CONFIG_MENU
	MR_VM_Steam_AchievementValue(ach_name, ach_value);
#endif
#endif
}

void Steam_StatValue(void)
{
	char stat_name[MAX_PIPESTRING];
	float stat_value;

	PIPE_ReadString(stat_name);
	stat_value = PIPE_ReadFloat();

#if ACHIEVEMENT_HARDCODE
	for (int iStat = 0; iStat < STATS_MAX; ++iStat)
	{
		Stat_t *stat = &g_Stats[iStat];
		if (stricmp(stat_name, stat->m_pchStatID)) // search for matching stat
			continue;
		stat->m_fValue = stat_value;
		CL_VM_Steam_StatValue(stat_name, stat_value);
#ifdef CONFIG_MENU
		MR_VM_Steam_StatValue(stat_name, stat_value);
#endif
		break;
	}
#else
	CL_VM_Steam_StatValue(stat_name, stat_value);
#ifdef CONFIG_MENU
	MR_VM_Steam_StatValue(stat_name, stat_value);
#endif
#endif
}

void Steam_SetLanguage(void)
{
	char lang_code[MAX_PIPESTRING];
	PIPE_ReadString(lang_code);

	Cvar_Set("steam_language", lang_code);
}

void Steam_ControllerType(void)
{
	int index = PIPE_ReadByte();
	int type = PIPE_ReadByte();
	
	CL_VM_Controller_Type(index, type);
#ifdef CONFIG_MENU
	MR_VM_Controller_Type(index, type);
#endif
}
//

void Steam_Init(void)
{
	func_readarray[SV_PRINT] = Steam_Print;
	func_readarray[SV_SETNAME] = Steam_SetName;
	func_readarray[SV_STEAMID] = Steam_SetSteamID;
	func_readarray[SV_ACHIEVEMENT_VALUE] = Steam_AchievementValue;
	func_readarray[SV_STAT_VALUE] = Steam_StatValue;
	func_readarray[SV_SETLANGUAGE] = Steam_SetLanguage;
	func_readarray[SV_CONTROLLER_TYPE] = Steam_ControllerType;
}

// unlock achievement of a given name, used by CSQC builtin
void Steam_AchievementUnlock(const char *achID)
{
	if (!steamConnected)
		return;

	PIPE_WriteByte(CL_ACHIEVEMENT_SET);
	PIPE_WriteString(achID);
}

void Steam_AchievementQuery(const char *achID)
{
	if (!steamConnected)
		return;

	PIPE_WriteByte(CL_ACHIEVEMENT_GET);
	PIPE_WriteString(achID);
}

// set value of a given stat, used by CSQC builtin
void Steam_StatSet(const char *statID, float statVal)
{
	if (!steamConnected)
		return;

	PIPE_WriteByte(CL_STAT_SET);
	PIPE_WriteString(statID);
	PIPE_WriteFloat(statVal);
}

void Steam_StatIncrement(const char *statID, float statVal)
{
	if (!steamConnected)
		return;

	PIPE_WriteByte(CL_STAT_INCREMENT);
	PIPE_WriteString(statID);
	PIPE_WriteFloat(statVal);
}

void Steam_StatQuery(const char *statID)
{
	if (!steamConnected)
		return;

	PIPE_WriteByte(CL_STAT_GET);
	PIPE_WriteString(statID);
}

void Steam_StatWipeAll(void)
{
	if (!steamConnected)
		return;

	PIPE_WriteByte(CL_STAT_WIPE_ALL);
}

void Steam_OpenOnScreenKeyboard(void)
{
	if (!steamConnected)
		return;

	if (Cmd_Argc() < 5)
	{
		Con_Printf("steam_openkeyboard: not all arguments provided\n");
		return;
	}

	int type, xpos, ypos, xsize, ysize;
	type = atoi(Cmd_Argv(0));
	xpos = atoi(Cmd_Argv(1));
	ypos = atoi(Cmd_Argv(2));
	xsize = atoi(Cmd_Argv(3));
	ysize = atoi(Cmd_Argv(4));

	PIPE_WriteByte(CL_ONSCREENKEYBOARD);
	PIPE_WriteByte(type);
	PIPE_WriteLong(xpos);
	PIPE_WriteLong(ypos);
	PIPE_WriteLong(xsize);
	PIPE_WriteLong(ysize);
}

void Steam_RegisterAchievement(const char *achID) // gamecode wants to register an achievement
{
	if (!steamConnected)
		return;

	PIPE_WriteByte(CL_REGISTER_ACHIEVEMENT);
	PIPE_WriteString(achID);
}

void Steam_RegisterStat(const char *statID, int statType) // gamecode wants to register a stat
{
	if (!steamConnected)
		return;

	PIPE_WriteByte(CL_REGISTER_STAT);
	PIPE_WriteString(statID);
	PIPE_WriteByte(statType);
}


void Controller_Poll(int index) // gamecode wants to know what type of controller we have
{
	controllertype_t controllerType = CONTROLLER_NULL;
	
	controllerType = VID_ControllerType(index);

	if (steamConnected)
	{
		if (controllerType == CONTROLLER_GENERIC || controllerType == CONTROLLER_STEAM) // we need to go out to steam and ask
		{
			PIPE_WriteByte(CL_CONTROLLER_GETTYPE);
			PIPE_WriteByte(index);
			return;
		}
	}
	else
	{
		if (controllerType == CONTROLLER_STEAM)
			controllerType = CONTROLLER_NULL;
	}

	//Con_Printf("%s: controllerType = %i\n", __func__, (int)controllerType);

	CL_VM_Controller_Type(index, (int)controllerType);
#ifdef CONFIG_MENU
	MR_VM_Controller_Type(index, (int)controllerType);
#endif
}

double next_readcheck;
void Steam_Tick(void)
{
	// turns out this shit is expensive...
	// so we need to do it sporadically. hopefully this won't cause chugging!
	if (Sys_DirtyTime() > next_readcheck)
	{
		next_readcheck = Sys_DirtyTime() + 0.1;

		unsigned char index = PIPE_ReadByte();
		while (index != 255)
		{
			if (index < SV_MAX)
			{
				//Con_Printf("STEAM PIPE: reading packet %i\n", (int)index);
				func_readarray[index]();
			}
			else
			{
				Con_Printf("STEAM PIPE: bad packet read\n");
			}

			index = PIPE_ReadByte();
		}
	}


	// this we can just do as needed, shouldn't be too bad.
	if (pipeSendBuffer.cursize)
	{
		PIPE_SendData();
	}
}

cvar_t cv_steamid = { CVAR_USERINFO | CVAR_NOTIFY | CVAR_READONLY, "steam_id", "", "steam id of currently logged in client" };
cvar_t cv_steamlang = { CVAR_USERINFO | CVAR_NOTIFY | CVAR_READONLY, "steam_language", "", "steam language that was last used" };
cvar_t cv_steaminit = { CVAR_SAVE, "steam_firsttime", "", "steam first time setup" };
cvar_t cv_steamname = { CVAR_USERINFO | CVAR_NOTIFY | CVAR_READONLY, "steam_name", "", "steam persona name of currently logged in client" };
void Steam_Startup(void)
{
	Cvar_RegisterVariable(&cv_steamid);
	Cvar_RegisterVariable(&cv_steamlang);
	Cvar_RegisterVariable(&cv_steaminit);
	Cvar_RegisterVariable(&cv_steamname);
	Cvar_Set("steam_id", ""); // start on empty
	Cvar_Set("steam_language", ""); // start on empty
	Cvar_Set("steam_name", ""); // start on empty
	steamConnected = false;

	pipeSendBuffer.cursize = 0;
	if (!initPipes())
		return;

	// Reki (May 9 2023):
	// this is probably shit? I'm not really sure.
	// registering a command only *sometimes* feels wrong
	// but it seems like the right thing to do at the time
	Cmd_AddCommand("steam_openkeyboard", Steam_OpenOnScreenKeyboard, "[steam_openkeyboard type xpos ypos xsize ysize] polls steam to open the on-screen keyboard");
	
	// Reki (March 5 2024): This can be handy, but let's not leave it in for people to get stuffcmd'd when they play a malicious multiplayer server if the community ever does that kinda thing.
	//Cmd_AddCommand("steam_wipeall", Steam_StatWipeAll, "WARNING: this will wipe all your steam stats and achievements, this is meant to be a debug command used for testing.");

	Steam_Init();

	PIPE_WriteByte(CL_HANDSHAKE);
}

#else
#warning "Compiling without steam integration. You've been warned!"

#include "quakedef.h"
#include "csprogs.h"
#include "cl_steam.h"

void Steam_Tick(void) {}
void Steam_Startup(void) {}
void Controller_Poll(int index)
{
	CL_VM_Controller_Type(index, (int)CONTROLLER_XBOX);
#ifdef CONFIG_MENU
	MR_VM_Controller_Type(index, (int)CONTROLLER_XBOX);
#endif
}
void Steam_RegisterStat(const char *statID, int statType) {}
void Steam_RegisterAchievement(const char *achID){}
void Steam_StatQuery(const char *statID) {}
void Steam_StatIncrement(const char *statID, float statVal) {}
void Steam_StatSet(const char *statID, float statVal) {}
void Steam_AchievementQuery(const char *achID) {}
void Steam_AchievementUnlock(const char *achID) {}
#endif