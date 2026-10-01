/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// death notice
//
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"

#include <string.h>
#include <stdio.h>
#include "triangleapi.h"	// triangleapi_s, kRenderTransTexture
#include "draw_util.h"
#include "strl.h"

float color[3];

// Extended death message flags, sent by ReGameDLL after the weapon name.
enum DeathMessageFlags {
	PLAYERDEATH_POSITION	= 0x001,	// float[3], where the victim was killed
	PLAYERDEATH_ASSISTANT	= 0x002,	// byte, index of the player who assisted
	PLAYERDEATH_KILLRARITY	= 0x004	// long, bitsum of KillRarity
};

// Classification of how rare a kill was. Same values as the KillRarity enum ReGameDLL uses
// to fill the message; the sprite frame for each flag is its bit position.
enum KillRarity {
	KILLRARITY_HEADSHOT			= 0x001, // Headshot
	KILLRARITY_KILLER_BLIND		= 0x002, // Killer was blind
	KILLRARITY_NOSCOPE			= 0x004, // No-scope sniper rifle kill
	KILLRARITY_PENETRATED		= 0x008, // Penetrated kill (through walls)
	KILLRARITY_THRUSMOKE		= 0x010, // Smoke grenade penetration kill (bullets went through smoke)
	KILLRARITY_ASSISTEDFLASH	= 0x020, // Assister helped with a flash
	KILLRARITY_DOMINATION_BEGAN	= 0x040, // Killer player began dominating the victim (NOTE: this flag is set once)
	KILLRARITY_DOMINATION		= 0x080, // Continues domination by the killer
	KILLRARITY_REVENGE			= 0x100, // Revenge by the killer
	KILLRARITY_INAIR			= 0x200  // Killer was in the air (skill to deal with high inaccuracy)
};

struct DeathNoticeItem {
	char szKiller[MAX_PLAYER_NAME_LENGTH*2];
	char szVictim[MAX_PLAYER_NAME_LENGTH*2];
	int iId;	// the index number of the associated sprite
	bool bSuicide;
	bool bTeamKill;
	bool bNonPlayerKill;
	bool bKillerHighlight;	// the local player made this kill, so the box gets the red outline
	bool bVictimHighlight;	// the local player was the victim, so the box gets the red fill
	float flDisplayTime;
	float *KillerColor;
	float *VictimColor;
	int iKillRarity;		// bitsum of KillRarity, 0 when the server sent none
	int iAssistantId;		// index of the player who assisted, 0 when nobody did
	float flSpawnTime;  // when the notice was added, used to fade it in
};

#define MAX_DEATHNOTICES	4
static int DEATHNOTICE_DISPLAY_TIME = 6;

#define DEATHNOTICE_TOP		32
#define DEATHNOTICE_RIGHT	16

// The notice box is drawn around the whole row, so these are the row metrics rather
// than the sprite metrics used by the original Valve layout.
#define DEATHNOTICE_BOX_PAD_TOP		4
#define DEATHNOTICE_BOX_PAD_BOTTOM	4
#define DEATHNOTICE_BOX_PAD_X		8
#define DEATHNOTICE_BOX_OUTLINE		3
#define DEATHNOTICE_BOX_GAP			4
#define DEATHNOTICE_NAME_GAP			5
#define DEATHNOTICE_SPRITE_PAD_X		3

// Death notice fade in / out, in seconds
#define DEATHNOTICE_FADE_IN		0.35f
#define DEATHNOTICE_FADE_OUT		0.75f

// The row is faded by pre-multiplying the alpha into the colours.
#define DEATHNOTICE_FADE_SCALE( a )		( ( a ) / 255.0f )

// Full strength when the time comes after the notice's lifetime has nearly run out.
#define DEATHNOTICE_FADE_OUT_GRACE		( DEATHNOTICE_DISPLAY_TIME - DEATHNOTICE_FADE_OUT )

// Kill rarity icons, one TGA per flag.
#define DEATHNOTICE_RARITY_PATH		"gfx/killfeed/"
#define DEATHNOTICE_RARITY_SIZE			24
#define DEATHNOTICE_RARITY_PAD_X		3

static const char *const kRarityIconFiles[RARITYICON_COUNT] =
{
	"headshot.tga",
	"blind.tga",
	"noscope.tga",
	"wallbang.tga",
	"smoke.tga",
	"flash_assist.tga",
	"dominate.tga",
	"revenge.tga",
	"inair.tga"
};

DeathNoticeItem rgDeathNoticeList[ MAX_DEATHNOTICES + 1 ];

cvar_t *cl_killsound;
cvar_t *cl_killsound_path;
cvar_t *hud_deathnotice_boxes;
cvar_t *hud_deathnotice_fade;

// Clips negative coordinates, which the engine does not handle.
static void DeathNoticeFillRect( int x, int y, int w, int h, int r, int g, int b, int a )
{
	if ( w <= 0 || h <= 0 || a <= 0 )
		return;

	if ( x < 0 ) { w += x; x = 0; }
	if ( y < 0 ) { h += y; y = 0; }
	if ( w <= 0 || h <= 0 )
		return;

	FillRGBABlend( x, y, w, h, r, g, b, a );
}

static void DeathNoticeDrawOutlineRect( int x, int y, int w, int h,
	int r, int g, int b, int a, int thickness )
{
	thickness = min( thickness, min( w, h ) / 2 );
	if ( thickness <= 0 )
		return;

	DeathNoticeFillRect( x, y, w, thickness, r, g, b, a );
	DeathNoticeFillRect( x, y + h - thickness, w, thickness, r, g, b, a );
	DeathNoticeFillRect( x, y + thickness, thickness, h - thickness * 2, r, g, b, a );
	DeathNoticeFillRect( x + w - thickness, y + thickness, thickness, h - thickness * 2, r, g, b, a );
}

static int DeathNoticeRarityWidth( void )
{
	return DEATHNOTICE_RARITY_SIZE + DEATHNOTICE_RARITY_PAD_X * 2;
}

static int DeathNoticeRarityHeight( void )
{
	return DEATHNOTICE_RARITY_SIZE;
}

// The "+" that joins the assistant onto the killer.
static const char *const kKillAssistSeparator = "+";

static int DeathNoticeDrawRarity( int hTexture, int x, int y, int a )
{
	const int draw_x = x + DEATHNOTICE_RARITY_PAD_X;

	gRenderAPI.GL_SelectTexture( 0 );
	gRenderAPI.GL_Bind( 0, hTexture );
	gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
	gEngfuncs.pTriAPI->Color4f( 1.0f, 1.0f, 1.0f, a / 255.0f );

	// Draw2DQuad takes scaled coordinates; the HUD's other 2D quads all do this.
	DrawUtils::Draw2DQuad( draw_x * gHUD.m_flScale, y * gHUD.m_flScale,
		( draw_x + DEATHNOTICE_RARITY_SIZE ) * gHUD.m_flScale,
		( y + DEATHNOTICE_RARITY_SIZE ) * gHUD.m_flScale );

	return draw_x + DEATHNOTICE_RARITY_SIZE + DEATHNOTICE_RARITY_PAD_X;
}

// Returns the alpha the notice row should be drawn with: transparent when it just
// appeared, solid while it is on screen, then fading back out as it expires.
static int DeathNoticeFadeAlpha( const DeathNoticeItem *pItem, float flTime )
{
	if ( !hud_deathnotice_fade->value )
		return 255;

	float flAlpha = 255.0f;

	// flDisplayTime runs out DEATHNOTICE_FADE_OUT seconds early, so the fade window is
	// already under way when the client has that long left to live.
	float flLife = pItem->flDisplayTime - flTime;

	if ( flLife < DEATHNOTICE_FADE_OUT )
		flAlpha *= max( 0.0f, flLife / DEATHNOTICE_FADE_OUT );

	if ( flTime - pItem->flSpawnTime < DEATHNOTICE_FADE_IN )
		flAlpha *= max( 0.0f, min( 1.0f, ( flTime - pItem->flSpawnTime ) / DEATHNOTICE_FADE_IN ) );

	return (int)max( 0.0f, min( 255.0f, flAlpha ) );
}

int CHudDeathNotice :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_DeathNotice, DeathMsg );

	hud_deathnotice_time = CVAR_CREATE( "hud_deathnotice_time", "6", FCVAR_ARCHIVE );
	cl_killsound = CVAR_CREATE( "cl_killsound", "0", FCVAR_ARCHIVE );
	cl_killsound_path = CVAR_CREATE( "cl_killsound_path", "buttons/bell1.wav", FCVAR_ARCHIVE );
	hud_deathnotice_boxes = CVAR_CREATE( "hud_deathnotice_boxes", "1", FCVAR_ARCHIVE );
	hud_deathnotice_fade = CVAR_CREATE( "hud_deathnotice_fade", "1", FCVAR_ARCHIVE );
	m_iFlags = 0;

	return 1;
}


void CHudDeathNotice :: InitHUDData( void )
{
	memset( rgDeathNoticeList, 0, sizeof(rgDeathNoticeList) );
}


int CHudDeathNotice :: VidInit( void )
{
	m_HUD_d_skull = gHUD.GetSpriteIndex( "d_skull" );

	char szPath[256];

	for( int i = 0; i < RARITYICON_COUNT; i++ )
	{
		snprintf( szPath, sizeof( szPath ), "%s%s", DEATHNOTICE_RARITY_PATH, kRarityIconFiles[i] );
		m_hRarityIcons[i] = gRenderAPI.GL_LoadTexture( szPath, NULL, 0, TF_NEAREST | TF_NOMIPMAP | TF_CLAMP );
	}

	return 1;
}

void CHudDeathNotice :: Shutdown( void )
{
	for( int i = 0; i < RARITYICON_COUNT; i++ )
	{
		gRenderAPI.GL_FreeTexture( m_hRarityIcons[i] );
		m_hRarityIcons[i] = 0;
	}
}

int CHudDeathNotice :: Draw( float flTime )
{
	int x, y, i;

	for( i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( rgDeathNoticeList[i].iId == 0 )
			break;  // we've gone through them all

		if ( rgDeathNoticeList[i].flDisplayTime < flTime )
		{ // display time has expired
			// remove the current item from the list
			memmove( &rgDeathNoticeList[i], &rgDeathNoticeList[i+1], sizeof(DeathNoticeItem) * (MAX_DEATHNOTICES - i) );
			i--;  // continue on the next item;  stop the counter getting incremented
			continue;
		}

		// Keep the notice alive for the full display time, fade-out window included.
		rgDeathNoticeList[i].flDisplayTime = min( rgDeathNoticeList[i].flDisplayTime, flTime + DEATHNOTICE_FADE_OUT_GRACE );

		// How faded the whole row is right now. Drives the box, the names and the icons.
		const int iRowAlpha = DeathNoticeFadeAlpha( &rgDeathNoticeList[i], flTime );
		const float flRowScale = DEATHNOTICE_FADE_SCALE( iRowAlpha );

		// Hide when scoreboard drawing. It will break triapi
		//if ( gViewPort && gViewPort->AllowedToPrintText() )
		//if ( !gHUD.m_iNoConsolePrint )
		{
			// Draw the death notice

			int id = (rgDeathNoticeList[i].iId == -1) ? m_HUD_d_skull : rgDeathNoticeList[i].iId;
			int iRarity = rgDeathNoticeList[i].iKillRarity;

			// Width of every rarity icon in the row, for the box size.
			int rarity_w = 0;
			if( iRarity )
			{
				for( int iFlag = KILLRARITY_HEADSHOT; iFlag <= KILLRARITY_INAIR; iFlag <<= 1 )
				{
					if( iRarity & iFlag )
						rarity_w += DeathNoticeRarityWidth();
				}
			}

			const int weapon_w = gHUD.GetSpriteRect( id ).Width();

			bool bAssistant = rgDeathNoticeList[i].iAssistantId >= 1 && rgDeathNoticeList[i].iAssistantId <= MAX_PLAYERS;
			const char *assistant_name = bAssistant ? g_PlayerInfoList[ rgDeathNoticeList[i].iAssistantId ].name : NULL;
			const float *assistant_color = bAssistant ? GetClientColor( rgDeathNoticeList[i].iAssistantId ) : NULL;

			const int killer_w = rgDeathNoticeList[i].bSuicide ? 0 : DrawUtils::ConsoleStringLen( rgDeathNoticeList[i].szKiller ) + DEATHNOTICE_NAME_GAP * 2;
			const int victim_w = rgDeathNoticeList[i].bNonPlayerKill ? 0 : DrawUtils::ConsoleStringLen( rgDeathNoticeList[i].szVictim ) + DEATHNOTICE_NAME_GAP * 2;
			const int assistant_w = assistant_name ? DrawUtils::ConsoleStringLen( kKillAssistSeparator ) + DrawUtils::ConsoleStringLen( assistant_name ) + DEATHNOTICE_NAME_GAP * 2 : 0;

			const int row_w = rarity_w + weapon_w + killer_w + assistant_w + victim_w;
			const int box_w = row_w + DEATHNOTICE_BOX_PAD_X * 2;

			// The rarity icon is taller than a line of text, so the row grows to hold it.
			const int text_h = gHUD.m_iFontHeight + DEATHNOTICE_BOX_PAD_TOP + DEATHNOTICE_BOX_PAD_BOTTOM;
			const int rarity_h = rarity_w ? DeathNoticeRarityHeight() : 0;
			const int box_h = max( text_h, rarity_h );
			const int box_x = ScreenWidth - DEATHNOTICE_RIGHT - box_w;

			if( !g_iUser1 )
			{
				y = YRES(DEATHNOTICE_TOP) + (box_h + DEATHNOTICE_BOX_OUTLINE * 2 + DEATHNOTICE_BOX_GAP) * i;
			}
			else
			{
				y = ScreenHeight / 5 + (box_h + DEATHNOTICE_BOX_OUTLINE * 2 + DEATHNOTICE_BOX_GAP) * i;
			}

			x = box_x + DEATHNOTICE_BOX_PAD_X;

			const int name_y = y + ( box_h - gHUD.m_iFontHeight ) / 2;
			const int rarity_y = y + ( box_h - rarity_h ) / 2;

			if( hud_deathnotice_boxes->value )
			{
				if( rgDeathNoticeList[i].bVictimHighlight )
				{
					DeathNoticeFillRect( box_x, y, box_w, box_h, (int)( 150 * flRowScale ), 0, (int)( 20 * flRowScale ), iRowAlpha );
				}
				else
				{
					DeathNoticeFillRect( box_x, y, box_w, box_h, 0, 0, 0, iRowAlpha * 100 / 255 );

					if( rgDeathNoticeList[i].bKillerHighlight )
						DeathNoticeDrawOutlineRect( box_x, y, box_w, box_h, (int)( 230 * flRowScale ), (int)( 20 * flRowScale ), 0, iRowAlpha, DEATHNOTICE_BOX_OUTLINE );
					else
						DeathNoticeDrawOutlineRect( box_x, y, box_w, box_h, 0, 0, 0, iRowAlpha, DEATHNOTICE_BOX_OUTLINE );
				}
			}

			const int iIconFade = (int)( 255 * flRowScale );

			// Icons are laid out by what they describe, matching NextClient.
			if( iRarity & KILLRARITY_DOMINATION )
				x = DeathNoticeDrawRarity( m_hRarityIcons[RARITYICON_DOMINATE], x, rarity_y, iRowAlpha );
			else if( iRarity & KILLRARITY_REVENGE )
				x = DeathNoticeDrawRarity( m_hRarityIcons[RARITYICON_REVENGE], x, rarity_y, iRowAlpha );

			if( iRarity & KILLRARITY_KILLER_BLIND )
				x = DeathNoticeDrawRarity( m_hRarityIcons[RARITYICON_BLIND], x, rarity_y, iRowAlpha );

			if ( !rgDeathNoticeList[i].bSuicide )
			{
				if ( rgDeathNoticeList[i].KillerColor )
					DrawUtils::SetConsoleTextColor( rgDeathNoticeList[i].KillerColor[0] * flRowScale, rgDeathNoticeList[i].KillerColor[1] * flRowScale, rgDeathNoticeList[i].KillerColor[2] * flRowScale );
				x = DrawUtils::DrawConsoleString( x + DEATHNOTICE_NAME_GAP, name_y, rgDeathNoticeList[i].szKiller ) + DEATHNOTICE_NAME_GAP;
			}

			if( assistant_name )
			{
				DrawUtils::SetConsoleTextColor( flRowScale, flRowScale, flRowScale );
				x = DrawUtils::DrawConsoleString( x + DEATHNOTICE_NAME_GAP, name_y, kKillAssistSeparator ) + DEATHNOTICE_NAME_GAP;

				if( iRarity & KILLRARITY_ASSISTEDFLASH )
					x = DeathNoticeDrawRarity( m_hRarityIcons[RARITYICON_FLASH_ASSIST], x, rarity_y, iRowAlpha );

				if( assistant_color )
					DrawUtils::SetConsoleTextColor( assistant_color[0] * flRowScale, assistant_color[1] * flRowScale, assistant_color[2] * flRowScale );

				x = DrawUtils::DrawConsoleString( x + DEATHNOTICE_NAME_GAP, name_y, assistant_name ) + DEATHNOTICE_NAME_GAP;
			}

			if( iRarity & KILLRARITY_INAIR )
				x = DeathNoticeDrawRarity( m_hRarityIcons[RARITYICON_INAIR], x, rarity_y - rarity_h / 2, iRowAlpha );

			const int weapon_y = y + ( box_h - gHUD.GetSpriteRect( id ).Height() ) / 2;
			SPR_Set( gHUD.GetSprite(id), iIconFade, iIconFade, iIconFade );
			SPR_DrawAdditive( 0, x, weapon_y, &gHUD.GetSpriteRect(id) );

			x += weapon_w;

			if( iRarity & KILLRARITY_NOSCOPE )
				x = DeathNoticeDrawRarity( m_hRarityIcons[RARITYICON_NOSCOPE], x, rarity_y, iRowAlpha );

			if( iRarity & KILLRARITY_THRUSMOKE )
				x = DeathNoticeDrawRarity( m_hRarityIcons[RARITYICON_SMOKE], x, rarity_y, iRowAlpha );

			if( iRarity & KILLRARITY_PENETRATED )
				x = DeathNoticeDrawRarity( m_hRarityIcons[RARITYICON_WALLBANG], x, rarity_y, iRowAlpha );

			if( iRarity & KILLRARITY_HEADSHOT )
				x = DeathNoticeDrawRarity( m_hRarityIcons[RARITYICON_HEADSHOT], x, rarity_y, iRowAlpha );

			if (!rgDeathNoticeList[i].bNonPlayerKill)
			{
				if ( rgDeathNoticeList[i].VictimColor )
					DrawUtils::SetConsoleTextColor( rgDeathNoticeList[i].VictimColor[0] * flRowScale, rgDeathNoticeList[i].VictimColor[1] * flRowScale, rgDeathNoticeList[i].VictimColor[2] * flRowScale );
				DrawUtils::DrawConsoleString( x + DEATHNOTICE_NAME_GAP, name_y, rgDeathNoticeList[i].szVictim );
			}
		}
	}

	if( i == 0 )
		m_iFlags &= ~HUD_DRAW; // disable hud item

	return 1;
}

// This message handler may be better off elsewhere
int CHudDeathNotice :: MsgFunc_DeathMsg( const char *pszName, int iSize, void *pbuf )
{
	m_iFlags |= HUD_DRAW;

	BufferReader reader( pszName, pbuf, iSize );

	int killer = reader.ReadByte();
	int victim = reader.ReadByte();
	int headshot = reader.ReadByte();

	char killedwith[32];
	strlcpy( killedwith, "d_", sizeof( killedwith ) );
	strlcat( killedwith, reader.ReadString(), sizeof( killedwith ) );

	// ReGameDLL appends the extended death message fields after the weapon name. A vanilla
	// server stops here, so everything below is read only while the buffer still has data.
	int kill_rarity = headshot ? KILLRARITY_HEADSHOT : 0;
	int assistant = 0;

	if( reader.Valid() )
	{
		const int extra_flags = reader.ReadLong();

		if( extra_flags & PLAYERDEATH_POSITION )
		{
			reader.ReadCoord();
			reader.ReadCoord();
			reader.ReadCoord();
		}

		if( extra_flags & PLAYERDEATH_ASSISTANT )
			assistant = reader.ReadByte();

		if( extra_flags & PLAYERDEATH_KILLRARITY )
			kill_rarity = reader.ReadLong();
	}

	//if (gViewPort)
	//	gViewPort->DeathMsg( killer, victim );
	gHUD.m_Scoreboard.DeathMsg( killer, victim );

	gHUD.m_Spectator.DeathMessage(victim);
	int i;
	for ( i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( rgDeathNoticeList[i].iId == 0 )
			break;
	}
	if ( i == MAX_DEATHNOTICES )
	{ // move the rest of the list forward to make room for this item
		memmove( rgDeathNoticeList, rgDeathNoticeList+1, sizeof(DeathNoticeItem) * MAX_DEATHNOTICES );
		i = MAX_DEATHNOTICES - 1;
	}

	//if (gViewPort)
		//gViewPort->GetAllPlayersInfo();
	gHUD.m_Scoreboard.GetAllPlayersInfo();

	// Get the Killer's name
	const char *killer_name = NULL;
	bool killer_this_player = false;
	if ( killer >= 1 && killer <= MAX_PLAYERS )
	{
		killer_name = g_PlayerInfoList[killer].name;
		killer_this_player = g_PlayerInfoList[killer].thisplayer;
	}

	rgDeathNoticeList[i].bKillerHighlight = killer_this_player;

	if ( !killer_name )
	{
		killer_name = "";
		rgDeathNoticeList[i].szKiller[0] = 0;
	}
	else
	{
		rgDeathNoticeList[i].KillerColor = GetClientColor( killer );
		strlcpy( rgDeathNoticeList[i].szKiller, killer_name, sizeof( rgDeathNoticeList[i].szKiller ) );
	}

	// Get the Victim's name
	const char *victim_name = NULL;
	bool victim_this_player = false;

	if ( victim >= 1 && victim <= MAX_PLAYERS )
	{
		victim_name = g_PlayerInfoList[ victim ].name;
		victim_this_player = g_PlayerInfoList[ victim ].thisplayer;
	}

	rgDeathNoticeList[i].bVictimHighlight = victim_this_player;

	if ( !victim_name )
	{
		victim_name = "";
		rgDeathNoticeList[i].szVictim[0] = 0;
	}
	else
	{
		rgDeathNoticeList[i].VictimColor = GetClientColor( victim );
		strlcpy( rgDeathNoticeList[i].szVictim, victim_name, sizeof( rgDeathNoticeList[i].szVictim ) );
	}

	// Is it a non-player object kill?
	// If victim is 255, the killer killed a specific, non-player object (like a sentrygun)
	if( victim == 255 )
	{
		rgDeathNoticeList[i].bNonPlayerKill = true;

		// Store the object's name in the Victim slot (skip the d_ bit)
		strlcpy( rgDeathNoticeList[i].szVictim, killedwith+2, sizeof( rgDeathNoticeList[i].szVictim ) );
	}
	else
	{
		if ( killer == victim || killer == 0 )
			rgDeathNoticeList[i].bSuicide = true;

		if ( !strncmp( killedwith, "d_teammate", sizeof(killedwith)  ) )
			rgDeathNoticeList[i].bTeamKill = true;
	}

	// Being dominated again is only worth showing to the player who is being dominated when
	// the domination starts; the repeats are noise. Same rule as NextClient.
	if( ( kill_rarity & KILLRARITY_DOMINATION ) && victim_this_player && !( kill_rarity & KILLRARITY_DOMINATION_BEGAN ) )
		kill_rarity &= ~KILLRARITY_DOMINATION;

	rgDeathNoticeList[i].iKillRarity = kill_rarity;

	rgDeathNoticeList[i].iAssistantId = assistant;

	// Find the sprite in the list
	int spr = gHUD.GetSpriteIndex( killedwith );

	rgDeathNoticeList[i].iId = spr;

	rgDeathNoticeList[i].flSpawnTime = gHUD.m_flTime;
	rgDeathNoticeList[i].flDisplayTime = gHUD.m_flTime + hud_deathnotice_time->value - DEATHNOTICE_FADE_OUT;

	// Play kill sound
	if ((killer_this_player || g_iUser2 == killer) &&
		!rgDeathNoticeList[i].bNonPlayerKill &&
		!rgDeathNoticeList[i].bSuicide &&
		cl_killsound->value > 0.0f)
	{
		PlaySound(cl_killsound_path->string, cl_killsound->value);
	}

	if (rgDeathNoticeList[i].bNonPlayerKill)
	{
		ConsolePrint( rgDeathNoticeList[i].szKiller );
		ConsolePrint( " killed a " );
		ConsolePrint( rgDeathNoticeList[i].szVictim );
		ConsolePrint( "\n" );
	}
	else
	{
		// record the death notice in the console
		if ( rgDeathNoticeList[i].bSuicide )
		{
			ConsolePrint( rgDeathNoticeList[i].szVictim );

			if ( !strncmp( killedwith, "d_world", sizeof(killedwith)  ) )
			{
				ConsolePrint( " died" );
			}
			else
			{
				ConsolePrint( " killed self" );
			}
		}
		else if ( rgDeathNoticeList[i].bTeamKill )
		{
			ConsolePrint( rgDeathNoticeList[i].szKiller );
			ConsolePrint( " killed his teammate " );
			ConsolePrint( rgDeathNoticeList[i].szVictim );
		}
		else
		{
			if( headshot )
				ConsolePrint( "*** ");
			ConsolePrint( rgDeathNoticeList[i].szKiller );
			ConsolePrint( " killed " );
			ConsolePrint( rgDeathNoticeList[i].szVictim );
		}

		if ( *killedwith && (*killedwith > 13 ) && strncmp( killedwith, "d_world", sizeof(killedwith) ) && !rgDeathNoticeList[i].bTeamKill )
		{
			if ( headshot )
				ConsolePrint(" with a headshot from ");
			else
				ConsolePrint(" with ");

			ConsolePrint( killedwith+2 ); // skip over the "d_" part
		}

		if( headshot ) ConsolePrint( " ***");
		ConsolePrint( "\n" );
	}

	return 1;
}

