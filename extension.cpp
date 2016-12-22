#include "extension.h"
#include <igameevents.h>
#include <icvar.h>

#include "CDetour/detours.h"

#define GAMEDATA_FILE "l4d2_bugfixes"

CDetour *Detour_WitchAttack__Create = NULL;
CDetour *Detour_CTerrorGameRules__CalculateSurvivalMultiplier = NULL;
tCDirector__AreTeamsFlipped CDirector__AreTeamsFlipped;

IGameConfig *g_pGameConf = NULL;
IGameEventManager2 *gameevents = NULL;
IServerGameEnts *gameents = NULL;
void **g_pDirector = NULL;
ICvar *icvar = NULL;
ConVar *gcv_mp_gamemode = NULL;

int g_SurvivorCountsOffset = -1;
int g_WitchACharasterOffset = -1;
int g_iSurvivorCount = 0;

BugFixes g_BugFixes;		/**< Global singleton for extension's main interface */
SMEXT_LINK(&g_BugFixes);

class CRoundStartListener : public IGameEventListener2
{
	int GetEventDebugID(void) { return EVENT_DEBUG_ID_INIT; }
	void FireGameEvent(IGameEvent* pEvent){
		g_iSurvivorCount = 0;
	}
};
CRoundStartListener RoundStartListener;

class CVenchicleLeaving : public IGameEventListener2
{
	int GetEventDebugID(void) { return EVENT_DEBUG_ID_INIT; }
	void FireGameEvent(IGameEvent* pEvent)
	{
		if (g_iSurvivorCount == 0)
		{
			g_iSurvivorCount = pEvent->GetInt("survivorcount");
		}
		g_pSM->LogMessage(myself,"VenchicleLeaving %d",g_iSurvivorCount);
	}
};
CVenchicleLeaving VenchicleLeaving;

DETOUR_DECL_MEMBER1(CTerrorGameRules__CalculateSurvivalMultiplier, int ,char,survcount)
{
	int result = DETOUR_MEMBER_CALL(CTerrorGameRules__CalculateSurvivalMultiplier)(survcount);
	if (g_iSurvivorCount)
	{
		int flipped = 0;
		if (!strcmp(gcv_mp_gamemode->GetString(),"versus") || !strcmp(gcv_mp_gamemode->GetString(),"scavenge") || !strcmp(gcv_mp_gamemode->GetString(),"teamversus") || !strcmp(gcv_mp_gamemode->GetString(),"teamscavenge"))
		{
			flipped = CDirector__AreTeamsFlipped(*g_pDirector);
		}
		char* SurvCounts=((char*)this)+g_SurvivorCountsOffset;

		if (SurvCounts)
		{
			int *SurvCount = (int*)(SurvCounts + 4 * flipped);
			if (SurvCount)
			{
				g_pSM->LogMessage(myself,"Survivor count %d",*SurvCount);
				if (*SurvCount < g_iSurvivorCount)
				{
					*SurvCount = g_iSurvivorCount;
					g_pSM->LogMessage(myself,"Survivor count FIXED TO %d",*SurvCount);
				}
			}
		}
		g_iSurvivorCount = 0;
	}
	return result;
}

DETOUR_DECL_MEMBER1(WitchAttack__WitchAttack, void* ,CBaseEntity*,pEntity)
{
	int client=gamehelpers->IndexOfEdict(gameents->BaseEntityToEdict((CBaseEntity*)pEntity));
	IGamePlayer *player = playerhelpers->GetGamePlayer(client);
	if (player)
	{
		g_pSM->LogMessage(myself,"WitchAttack created for client %s.",player->GetName());	
	}
	else
	{
		g_pSM->LogMessage(myself,"WitchAttack created for %d entity.",client);
	}

	void*result=DETOUR_MEMBER_CALL(WitchAttack__WitchAttack)(pEntity);

	DWORD*CharId=((DWORD *)this + g_WitchACharasterOffset);
	g_pSM->LogMessage(myself,"WitchAttack char=%d.",*CharId);
	*CharId=8;

	return result;
}

bool BugFixes::SDK_OnMetamodLoad( ISmmAPI *ismm, char *error, size_t maxlength, bool late )
{
	size_t maxlen=maxlength;
	GET_V_IFACE_CURRENT(GetEngineFactory, gameevents, IGameEventManager2, INTERFACEVERSION_GAMEEVENTSMANAGER2);
	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);
	g_pCVar = icvar;

	return true;
}

bool BugFixes::SDK_OnLoad( char *error, size_t maxlength, bool late )
{
	char conf_error[255];
	if (!gameconfs->LoadGameConfigFile(GAMEDATA_FILE, &g_pGameConf, conf_error, sizeof(conf_error))){
		if (!strlen(conf_error)){
			snprintf(error, maxlength, "Could not read BugFixes.txt: %s", conf_error);
		}
		return false;
	}

	g_pGameConf->GetMemSig("CDirector::AreTeamsFlipped",(void **)&CDirector__AreTeamsFlipped);
	g_pGameConf->GetOffset("SurvivorCounters",&g_SurvivorCountsOffset);
	g_pGameConf->GetOffset("WitchAttackCharaster",&g_WitchACharasterOffset);

	if (!SetupHooks()) return false;

	//Register events
	gameevents->AddListener(&RoundStartListener,"round_start",true);
	gameevents->AddListener(&VenchicleLeaving, "finale_vehicle_leaving", true);

	//Register ConVars
	gcv_mp_gamemode=icvar->FindVar("mp_gamemode");

	//Get L4D2 Globals
	char *addr = NULL;
	int offset = 0;

	#ifdef PLATFORM_WINDOWS
		/* g_pDirector */
		if (!g_pGameConf->GetMemSig("DirectorMusicBanks::OnRoundStart", (void **)&addr) || !addr)
			return false;
		if (!g_pGameConf->GetOffset("TheDirector", &offset) || !offset)
			return false;
		g_pDirector = *reinterpret_cast<void ***>(addr + offset);
	#elif defined PLATFORM_LINUX
		/* g_pDirector */
		if (!g_pGameConf->GetMemSig("TheDirector", (void **)&addr) || !addr)
			return false;
		g_pDirector = reinterpret_cast<void **>(addr);
	#endif

	return true;
}

void BugFixes::SDK_OnUnload()
{
	//remove events
	gameevents->RemoveListener(&RoundStartListener);
	gameevents->RemoveListener(&VenchicleLeaving);

	//remove hooks
	RemoveHooks();

	gameconfs->CloseGameConfigFile(g_pGameConf);
}

bool BugFixes::SetupHooks()
{
	CDetourManager::Init(g_pSM->GetScriptingEngine(), g_pGameConf);	

	//witch fix hooks
	Detour_WitchAttack__Create=DETOUR_CREATE_MEMBER(WitchAttack__WitchAttack, "WitchAttack::WitchAttack");
	if (Detour_WitchAttack__Create) {
		Detour_WitchAttack__Create->EnableDetour();
	} else {
		g_pSM->LogError(myself,"Cannot find signature of WitchAttack::WitchAttack");
		RemoveHooks();
		return false;
	}

	//score fix hooks
	Detour_CTerrorGameRules__CalculateSurvivalMultiplier=DETOUR_CREATE_MEMBER(CTerrorGameRules__CalculateSurvivalMultiplier, "CTerrorGameRules::CalculateSurvivalMultiplier");
	if (Detour_CTerrorGameRules__CalculateSurvivalMultiplier) {
		Detour_CTerrorGameRules__CalculateSurvivalMultiplier->EnableDetour();
	} else {
		g_pSM->LogError(myself,"Cannot find signature of CTerrorGameRules::CalculateSurvivalMultiplier");
		RemoveHooks();
		return false;
	}

	return true;
}

void BugFixes::RemoveHooks()
{
	if (Detour_WitchAttack__Create){
		Detour_WitchAttack__Create->Destroy();
		Detour_WitchAttack__Create=NULL;
	}
	if (Detour_CTerrorGameRules__CalculateSurvivalMultiplier){
		Detour_CTerrorGameRules__CalculateSurvivalMultiplier->Destroy();
		Detour_CTerrorGameRules__CalculateSurvivalMultiplier = NULL;
	}
}

