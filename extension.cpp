#include "extension.h"
#include <igameevents.h>
#include <icvar.h>

#include "CDetour/detours.h"

#define GAMEDATA_FILE "l4d2_bugfixes"

bool ChargerVSSurvivorCollisions[L4D_MAX_PLAYERS+1][L4D_MAX_PLAYERS+1];

CDetour *Detour_WitchAttack__Create = NULL;
CDetour *Detour_HandleCustomCollision = NULL;
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

char* Patch_HandleCustomCollision_addr;
BYTE Patch_HandleCustomCollision_org[6];
BYTE Patch_HandleCustomCollision_new[]="\x90\x90\x90\x90\x90\x90";

class CRoundStartListener : public IGameEventListener2
{
	int GetEventDebugID(void) { return EVENT_DEBUG_ID_INIT; }
	void FireGameEvent(IGameEvent* pEvent){
		g_iSurvivorCount = 0;
		memset(&ChargerVSSurvivorCollisions[0][0],0,sizeof(ChargerVSSurvivorCollisions));
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

class CPummelEndListener : public IGameEventListener2
{
	int GetEventDebugID(void) { return EVENT_DEBUG_ID_INIT; }
	void FireGameEvent(IGameEvent* pEvent)
	{
		int client = playerhelpers->GetClientOfUserId(pEvent->GetInt("userid"));
		if (client>0 && client<=L4D_MAX_PLAYERS){
			memset(&ChargerVSSurvivorCollisions[client][0],0,sizeof(ChargerVSSurvivorCollisions[client]));
		}
	}
};
CPummelEndListener PummelEndListener;

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

DETOUR_DECL_MEMBER5(CCharge__HandleCustomCollision, int ,CBaseEntity *,pEntity, Vector  const&, v1, Vector  const&, v2, CGameTrace *, gametrace, void *,movedata)
{
	int client=gamehelpers->IndexOfEdict(gameents->BaseEntityToEdict(META_IFACEPTR(CBaseEntity)));
	int target=gamehelpers->IndexOfEdict(gameents->BaseEntityToEdict((CBaseEntity*)pEntity));
	if (client>0 && target>0 && client<=L4D_MAX_PLAYERS && target<=L4D_MAX_PLAYERS)
	{
		if (!ChargerVSSurvivorCollisions[client][target])
		{
			ChargerVSSurvivorCollisions[client][target]=true;
			int result;
			BugFixes::ChargerImpactPatch(true);
			result=DETOUR_MEMBER_CALL(CCharge__HandleCustomCollision)(pEntity,v1,v2,gametrace,movedata);
			BugFixes::ChargerImpactPatch(false);

			return result;
		}
	}
	return DETOUR_MEMBER_CALL(CCharge__HandleCustomCollision)(pEntity,v1,v2,gametrace,movedata);
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
	g_pGameConf->GetMemSig("CCharge::HandleCustomCollision_code",(void **)&Patch_HandleCustomCollision_addr);

	if (Patch_HandleCustomCollision_addr == NULL) {
		snprintf(error, maxlength, "Cannot find CCharge::HandleCustomCollision code signature.");
		g_pSM->LogError(myself,error);		
		return false;
	}

	SetMemPatchable(Patch_HandleCustomCollision_addr,6); 
	memcpy(&Patch_HandleCustomCollision_org,Patch_HandleCustomCollision_addr,6);

	if (!SetupHooks()) return false;

	//Register events
	gameevents->AddListener(&RoundStartListener,"round_start",true);
	gameevents->AddListener(&VenchicleLeaving, "finale_vehicle_leaving", true);
	gameevents->AddListener(&PummelEndListener,"charger_pummel_end",true);

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
	gameevents->RemoveListener(&PummelEndListener);

	//remove hooks
	RemoveHooks();

	gameconfs->CloseGameConfigFile(g_pGameConf);
}

void BugFixes::ChargerImpactPatch(bool enable)
{
	if (Patch_HandleCustomCollision_addr)
	{
		if (enable)
			memcpy(Patch_HandleCustomCollision_addr,&Patch_HandleCustomCollision_new,6);
		else
			memcpy(Patch_HandleCustomCollision_addr,&Patch_HandleCustomCollision_org,6);
	}
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

	//charger fix hooks
	Detour_HandleCustomCollision=DETOUR_CREATE_MEMBER(CCharge__HandleCustomCollision, "CCharge::HandleCustomCollision");	
	if (Detour_HandleCustomCollision) {
		Detour_HandleCustomCollision->EnableDetour();
	} else {
		g_pSM->LogError(myself,"Cannot find signature of CCharge::HandleCustomCollision");
		RemoveHooks();
		return false;
	}

	return true;
}

void BugFixes::RemoveHooks()
{
	ChargerImpactPatch(false);

	if (Detour_WitchAttack__Create){
		Detour_WitchAttack__Create->Destroy();
		Detour_WitchAttack__Create=NULL;
	}
	if (Detour_CTerrorGameRules__CalculateSurvivalMultiplier){
		Detour_CTerrorGameRules__CalculateSurvivalMultiplier->Destroy();
		Detour_CTerrorGameRules__CalculateSurvivalMultiplier = NULL;
	}
	if (Detour_HandleCustomCollision){
		Detour_HandleCustomCollision->Destroy();
		Detour_HandleCustomCollision=NULL;
	}
}

