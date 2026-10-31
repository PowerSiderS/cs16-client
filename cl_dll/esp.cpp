// hud/esp.cpp
// Player marker feature: draws team-colored boxes, names and distance
// for all players, projected through world geometry (radar-style, 3D).
#include "hud.h"
#include "cl_util.h"
#include "triangleapi.h"
#include <math.h>
#include "com_weapons.h"

cvar_t *esp_enable_cvar;
cvar_t *esp_name_cvar;
cvar_t *esp_dist_cvar;
cvar_t *esp_mates_cvar;

#define ESP_BOX_THICKNESS 2

static void ESP_DrawBox( int x, int y, int w, int h, int r, int g, int b, int a )
{
	// top
	FillRGBA( x, y, w, ESP_BOX_THICKNESS, r, g, b, a );
	// bottom
	FillRGBA( x, y + h - ESP_BOX_THICKNESS, w, ESP_BOX_THICKNESS, r, g, b, a );
	// left
	FillRGBA( x, y, ESP_BOX_THICKNESS, h, r, g, b, a );
	// right
	FillRGBA( x + w - ESP_BOX_THICKNESS, y, ESP_BOX_THICKNESS, h, r, g, b, a );
}

int CHudEsp::Init( void )
{
	esp_enable_cvar = CVAR_CREATE( "esp", "1", FCVAR_ARCHIVE );
	esp_name_cvar  = CVAR_CREATE( "esp_name", "1", FCVAR_ARCHIVE );
	esp_dist_cvar  = CVAR_CREATE( "esp_dist", "1", FCVAR_ARCHIVE );
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
	if ( !esp_enable_cvar || !esp_enable_cvar->value )
		return 1;

	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	if ( !local || CL_IsDead() )
		return 1;

	int myTeam = g_iTeamNumber;

	for ( int i = 1; i <= gEngfuncs.GetMaxClients(); i++ )
	{
		// skip self
		if ( local->index == i )
			continue;

		// skip disconnected / dead
		if ( g_PlayerExtraInfo[i].dead )
			continue;
		if ( !g_PlayerInfoList[i].name || !g_PlayerInfoList[i].name[0] )
			continue;

		cl_entity_t *ent = gEngfuncs.GetEntityByIndex( i );
		if ( !ent || !ent->player )
			continue;
		if ( ent->curstate.solid == SOLID_NOT )
			continue;

		int team = g_PlayerExtraInfo[i].teamnumber;

		// teammates optional
		if ( team == myTeam && !esp_mates_cvar->value )
			continue;

		// world positions: feet & head
		vec3_t vFeet = ent->origin;
		vec3_t vHead = ent->origin;
		vHead[2] += ( ent->curstate.usehull ) ? 32.0f : 72.0f;

		vec3_t sFeet, sHead;

		// project both; WorldToScreen returns nonzero if point is behind camera
		if ( gEngfuncs.pTriAPI->WorldToScreen( vFeet, sFeet ) )
			continue;
		if ( gEngfuncs.pTriAPI->WorldToScreen( vHead, sHead ) )
			continue;

		// skip offscreen / degenerate
		if ( sFeet[0] < -1.5f || sFeet[0] > 1.5f || sHead[0] < -1.5f || sHead[0] > 1.5f )
			continue;
		if ( sFeet[1] < -1.5f || sFeet[1] > 1.5f || sHead[1] < -1.5f || sHead[1] > 1.5f )
			continue;

		// to pixels
		int xFeet = (int)( ( 1.0f + sFeet[0] ) * ScreenWidth  * 0.5f );
		int yFeet = (int)( ( 1.0f - sFeet[1] ) * ScreenHeight * 0.5f );
		int xHead = (int)( ( 1.0f + sHead[0] ) * ScreenWidth  * 0.5f );
		int yHead = (int)( ( 1.0f - sHead[1] ) * ScreenHeight * 0.5f );

		int boxH = yFeet - yHead;
		if ( boxH < 4 )
			continue;

		int boxW = (int)( boxH * 0.5f );
		if ( boxW < 2 )
			boxW = 2;

		// color: enemy red, teammate blue (independent of my team value quirks)
		int r, g, b;
		if ( team == myTeam ) { r = 60;  g = 120; b = 255; }
		else                 { r = 255; g = 40;  b = 40;  }

		int x0 = xHead - boxW / 2;
		int y0 = yHead;

		ESP_DrawBox( x0, y0, boxW, boxH, r, g, b, 200 );

		// name
		if ( esp_name_cvar->value )
		{
			gEngfuncs.pfnDrawSetTextColor( (float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f );
			gEngfuncs.pfnDrawConsoleString( x0, y0 - gHUD.m_iFontHeight - 2, g_PlayerInfoList[i].name );
		}

		// distance in meters (1 unit = 1 inch => * 0.0254)
		if ( esp_dist_cvar->value )
		{
			float dx = ent->origin[0] - local->origin[0];
			float dy = ent->origin[1] - local->origin[1];
			float dz = ent->origin[2] - local->origin[2];
			int meters = (int)( sqrt( dx*dx + dy*dy + dz*dz ) * 0.0254f );

			char sz[16];
			snprintf( sz, sizeof( sz ), "%dm", meters );

			gEngfuncs.pfnDrawSetTextColor( 0.8f, 0.8f, 0.8f );
			gEngfuncs.pfnDrawConsoleString( x0, yFeet + 2, sz );
		}
	}

	return 1;
}
