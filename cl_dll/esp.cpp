#include "hud.h"
#include "cl_util.h"

cvar_t *esp_enable_cvar;
cvar_t *esp_mates_cvar;

bool ESP_GetPlayerHighlightColor( int playerIndex, int &r, int &g, int &b )
{
	if ( !esp_enable_cvar || !esp_enable_cvar->value ||
		 playerIndex < 1 || playerIndex > gEngfuncs.GetMaxClients() )
		return false;

	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	if ( !local || local->index == playerIndex || g_PlayerExtraInfo[playerIndex].dead ||
		 !g_PlayerInfoList[playerIndex].name || !g_PlayerInfoList[playerIndex].name[0] )
		return false;

	const int team = g_PlayerExtraInfo[playerIndex].teamnumber;
	if ( team != TEAM_CT && team != TEAM_TERRORIST )
		return false;

	if ( team == g_iTeamNumber && esp_mates_cvar && !esp_mates_cvar->value )
		return false;

	if ( team == TEAM_CT )
	{
		r = 45; g = 105; b = 255;
	}
	else
	{
		r = 255; g = 35; b = 35;
	}

	return true;
}

int CHudEsp::Init( void )
{
	esp_enable_cvar = CVAR_CREATE( "esp", "1", FCVAR_ARCHIVE );
	esp_mates_cvar = CVAR_CREATE( "esp_teammates", "1", FCVAR_ARCHIVE );
	m_iFlags |= HUD_ACTIVE;
	gHUD.AddHudElem( this );
	return 0;
}

int CHudEsp::VidInit( void )
{
	return 1;
}

int CHudEsp::Draw( float flTime )
{
	return 1;
}
