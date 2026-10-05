/***
*
*	Copyright (c) 1999, Valve LLC. All rights reserved.
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
// MOTD.cpp
//
// for displaying a server-sent message of the day
//

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "kbutton.h"
#include "triangleapi.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "draw_util.h"
#include "build.h"
#include "strl.h"
#include "utlvector.h"

#if XASH_WIN32 == 1 || XASH_PSVITA == 1
#define strcasestr strstr
#endif

// Forward declarations for the HTML/text helpers defined later in this file.
static int MeasureHudStringWidth( const char *str );
static int MeasureHudStringWidthPrefix( const char *str, int count );

int CHudMOTD :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_MOTD, MOTD );

	cl_hide_motd = CVAR_CREATE("cl_hide_motd", "0", FCVAR_ARCHIVE); // hide motd

	m_iDefaultR = 255;
	m_iDefaultG = 180;
	m_iDefaultB = 0;

	Reset();

	return 1;
}

int CHudMOTD :: VidInit( void )
{
	// Load sprites here
	return 1;
}

void CHudMOTD :: Reset( void )
{
	m_iFlags &= ~HUD_DRAW;  // start out inactive
	m_szMOTD.Clear();
	m_ColorSpans.RemoveAll();
	m_iLines = 0;
	m_bShow = false;
	ignoreThisMotd = false;
}

#define LINE_HEIGHT  13
#define ROW_GAP  13
#define ROW_RANGE_MIN 30
#define ROW_RANGE_MAX ( ScreenHeight - 100 )
int CHudMOTD :: Draw( float fTime )
{
	gHUD.m_iNoConsolePrint &= ~( 1 << 1 );
	if( !m_bShow )
		return 1;

	if( cl_hide_motd->value )
	{
		Reset();
		return 1;
	}

	gHUD.m_iNoConsolePrint |= 1 << 1;
	// find the top of where the MOTD should be drawn,  so the whole thing is centered in the screen
	int ypos = (ScreenHeight - LINE_HEIGHT * m_iLines)/2; // shift it up slightly
	char *ch = m_szMOTD.Access();
	int xpos = (ScreenWidth - gHUD.GetCharWidth( 'M' ) * m_iMaxLength) / 2;
	if( xpos < 30 ) xpos = 30;
	int xmax = xpos + gHUD.GetCharWidth( 'M' ) * m_iMaxLength;
	int height = LINE_HEIGHT * m_iLines;
	int ypos_r=ypos;
	if( height > ROW_RANGE_MAX )
	{
		ypos = ROW_RANGE_MIN + 7 + scroll;
		if( ypos  > ROW_RANGE_MIN + 4 )
			scroll-= (ypos - ( ROW_RANGE_MIN + 4))/3.0;
		if( ypos + height < ROW_RANGE_MAX )
			scroll+= (ROW_RANGE_MAX - (ypos + height))/ 3.0;
		ypos_r = ROW_RANGE_MIN;
		height = ROW_RANGE_MAX;
	}
	if( xmax > ScreenWidth - 30 ) xmax = ScreenWidth - 30;
	char *next_line;
	DrawUtils::DrawRectangle(xpos-5, ypos_r - 5, xmax - xpos+10, height + 10);
	int lineIndex = 0;
	while ( *ch )
	{
		int line_length = 0;  // count the length of the current line
		for ( next_line = ch; *next_line != '\n' && *next_line != 0; next_line++ )
			line_length += gHUD.GetCharWidth( (unsigned char)*next_line );
		char *top = next_line;
		if ( *top == '\n' )
			*top = 0;
		else
			top = NULL;

		// find where to start drawing the line
		if( (ypos > ROW_RANGE_MIN) && (ypos + LINE_HEIGHT <= ypos_r + height) )
		{
			// Collect the color spans belonging to this line. Spans are stored
			// flat and ascending by line then character.
			int len = (int)strlen( ch );
			int firstSpan = -1;
			int spanCount = 0;
			for( int s = 0; s < m_ColorSpans.Count(); s++ )
			{
				if( m_ColorSpans[s].lineIndex == lineIndex )
				{
					if( firstSpan < 0 ) firstSpan = s;
					spanCount++;
				}
				else if( firstSpan >= 0 )
				{
					break; // past this line's spans
				}
			}

			// Center each line individually inside the MOTD box, matching how
			// browsers lay out typical (centered) MOTD pages.
			int lineWidth = MeasureHudStringWidth( ch );
			int lineX = xpos;
			if( lineWidth < ( xmax - xpos ) )
				lineX = xpos + ( xmax - xpos - lineWidth ) / 2;

			// Render each color run in its own color, falling back to the
			// default MOTD color for lines with no spans.
			if( spanCount > 0 )
			{
				for( int s = 0; s < spanCount; s++ )
				{
					const MOTDColorSpan &span = m_ColorSpans[firstSpan + s];
					int start = span.charIndex;
					int end = ( s + 1 < spanCount ) ? m_ColorSpans[firstSpan + s + 1].charIndex : len;
					if( start < 0 ) start = 0;
					if( start >= len ) break;
					if( end > len ) end = len;

					// Offset this run by the width of the text preceding it on the
					// line so the individually drawn runs stay correctly aligned.
					int runX = lineX + MeasureHudStringWidthPrefix( ch, start );

					// Temporarily terminate the run so DrawHudString renders only it.
					char saved = ch[end];
					ch[end] = 0;
					DrawUtils::DrawHudString( runX, ypos, xmax, ch + start,
						span.r, span.g, span.b );
					ch[end] = saved;
				}
			}
			else
			{
				DrawUtils::DrawHudString( lineX, ypos, xmax, ch, m_iDefaultR, m_iDefaultG, m_iDefaultB );
			}
		}

		ypos += LINE_HEIGHT;
		lineIndex++;

		if ( top )  // restore 
			*top = '\n';
		ch = next_line;
		if ( *ch == '\n' )
			ch++;

		if ( ypos > (ScreenHeight - 20) )
			break;  // don't let it draw too low
	}
	
	return 1;
}

int CHudMOTD :: MsgFunc_MOTD( const char *pszName, int iSize, void *pbuf )
{
	if( cl_hide_motd->value )
		return 1;

	if ( m_iFlags & HUD_DRAW )
	{
		Reset(); // clear the current MOTD in prep for this one
	}

	if( ignoreThisMotd )
		return 1;

	BufferReader reader( pszName, pbuf, iSize );

	int is_finished = reader.ReadByte();

	// Append the raw chunk first; we will strip HTML when the message is finished.
	m_szMOTD.Append( reader.ReadString() );

	if ( is_finished )
	{
		// Strip HTML tags / decode entities, producing a plain-text version and
		// per-line color spans for <font color="..."> wrappers.
		CUtlString plain;
		StripHtmlMOTD( m_szMOTD.String(), plain );

		m_szMOTD.Clear();
		m_szMOTD.Append( plain.String() );

		// If nothing but markup remained, there is nothing meaningful to show.
		bool hasVisibleText = false;
		for( const char *v = m_szMOTD.String(); v && *v; v++ )
		{
			if( *v != '\n' && *v != '\r' && *v != ' ' && *v != '\t' )
			{
				hasVisibleText = true;
				break;
			}
		}

		if( !hasVisibleText )
			return 1;

		int length = 0;
		
		m_iLines = 0;
		m_iMaxLength = 0;
		m_iFlags |= HUD_DRAW;


		for ( const char *sz = m_szMOTD.String(); *sz != 0; sz++ )  // count the number of lines in the MOTD
		{
			if ( *sz == '\n' )
			{
				m_iLines++;
				if( length > m_iMaxLength )
				{
					m_iMaxLength = length;
					length = 0;
				}
			}
			length++;
		}
		
		m_iLines++;
		if( length > m_iMaxLength )
		{
			m_iMaxLength = length;
			length = 0;
		}
		m_bShow = true;
	}

	return 1;
}

// ---------------------------------------------------------------------------
// HTML MOTD support
//
// Server MOTDs are frequently HTML documents. We do not attempt to lay out a
// real document; instead we convert the markup into plain text the existing
// text renderer can draw, while preserving <font color="..."> colors so the
// result still looks like the original page.
// ---------------------------------------------------------------------------

// Decode a single HTML entity. 'p' points at the character following '&'.
// Returns the number of input characters consumed (including the '&' and ';'),
// or 0 if this is not a recognized entity. On success 'out' receives the
// NUL-terminated decoded UTF-8 byte sequence and 'outLen' its length (max 4).
static int DecodeHtmlEntity( const char *p, char out[5], int &outLen )
{
	outLen = 0;

	// &amp; &lt; &gt; &quot; &apos; &nbsp;
	struct { const char *name; const char *value; } named[] = {
		{ "amp;",  "&" },
		{ "lt;",   "<" },
		{ "gt;",   ">" },
		{ "quot;", "\"" },
		{ "apos;", "'" },
		{ "nbsp;", " " },
	};

	for( size_t i = 0; i < sizeof( named ) / sizeof( named[0] ); i++ )
	{
		size_t len = strlen( named[i].name );
		if( !strncmp( p, named[i].name, len ) )
		{
			strlcpy( out, named[i].value, 5 );
			outLen = (int)strlen( out );
			return (int)len + 1; // +1 for the '&'
		}
	}

	// Numeric references: &#123; or &#x1F;
	if( p[0] == '#' )
	{
		unsigned int code = 0;
		int consumed = 1; // '#'

		if( p[1] == 'x' || p[1] == 'X' )
		{
			consumed = 2;
			while( isxdigit( (unsigned char)p[consumed] ) )
			{
				char c = p[consumed];
				code = code * 16 + ( ( c >= 'a' ) ? c - 'a' + 10 : ( c >= 'A' ) ? c - 'A' + 10 : c - '0' );
				consumed++;
			}
		}
		else
		{
			while( isdigit( (unsigned char)p[consumed] ) )
			{
				code = code * 10 + ( p[consumed] - '0' );
				consumed++;
			}
		}

		if( consumed > ( ( p[1] == 'x' || p[1] == 'X' ) ? 2 : 1 ) && p[consumed] == ';' )
		{
			// Encode the code point as UTF-8 (up to 4 bytes).
			if( code < 0x80 )
			{
				out[0] = (char)code;
				outLen = 1;
			}
			else if( code < 0x800 )
			{
				out[0] = (char)( 0xC0 | ( code >> 6 ) );
				out[1] = (char)( 0x80 | ( code & 0x3F ) );
				outLen = 2;
			}
			else if( code < 0x10000 )
			{
				out[0] = (char)( 0xE0 | ( code >> 12 ) );
				out[1] = (char)( 0x80 | ( ( code >> 6 ) & 0x3F ) );
				out[2] = (char)( 0x80 | ( code & 0x3F ) );
				outLen = 3;
			}
			else
			{
				out[0] = (char)( 0xF0 | ( code >> 18 ) );
				out[1] = (char)( 0x80 | ( ( code >> 12 ) & 0x3F ) );
				out[2] = (char)( 0x80 | ( ( code >> 6 ) & 0x3F ) );
				out[3] = (char)( 0x80 | ( code & 0x3F ) );
				outLen = 4;
			}
			out[outLen] = 0;
			return consumed + 1; // +1 for the '&'
		}
	}

	return 0;
}

// Parse a color value from a <font color="..."> attribute. Accepts #RRGGBB,
// #RGB and a handful of common named colors. Returns true on success.
static bool ParseHtmlColor( const char *value, int &r, int &g, int &b )
{
	while( *value == ' ' || *value == '\t' ) value++;

	if( *value == '#' )
	{
		value++;
		unsigned int hex[6];
		int n = 0;
		while( n < 6 && isxdigit( (unsigned char)value[n] ) )
		{
			char c = value[n];
			hex[n] = ( c >= 'a' ) ? c - 'a' + 10 : ( c >= 'A' ) ? c - 'A' + 10 : c - '0';
			n++;
		}

		if( n == 6 )
		{
			r = hex[0] * 16 + hex[1];
			g = hex[2] * 16 + hex[3];
			b = hex[4] * 16 + hex[5];
			return true;
		}
		else if( n == 3 )
		{
			r = hex[0] * 17;
			g = hex[1] * 17;
			b = hex[2] * 17;
			return true;
		}
		return false;
	}

	struct { const char *name; int r, g, b; } named[] = {
		{ "white",   255, 255, 255 },
		{ "black",     0,   0,   0 },
		{ "red",     255,   0,   0 },
		{ "green",     0, 128,   0 },
		{ "blue",      0,   0, 255 },
		{ "yellow",  255, 255,   0 },
		{ "orange",  255, 165,   0 },
		{ "purple",  128,   0, 128 },
		{ "gray",    128, 128, 128 },
		{ "grey",    128, 128, 128 },
		{ "lime",      0, 255,   0 },
		{ "aqua",      0, 255, 255 },
		{ "cyan",      0, 255, 255 },
		{ "magenta", 255,   0, 255 },
		{ "fuchsia", 255,   0, 255 },
		{ "silver",  192, 192, 192 },
		{ "maroon",  128,   0,   0 },
		{ "navy",      0,   0, 128 },
		{ "teal",      0, 128, 128 },
		{ "olive",   128, 128,   0 },
		{ "pink",    255, 192, 203 },
	};

	for( size_t i = 0; i < sizeof( named ) / sizeof( named[0] ); i++ )
	{
		if( !strncasecmp( value, named[i].name, strlen( named[i].name ) ) )
		{
			r = named[i].r;
			g = named[i].g;
			b = named[i].b;
			return true;
		}
	}

	return false;
}

// Extract the value of an attribute (e.g. "color") from a tag's attribute text.
// Returns true and copies the value into 'out' if found.
static bool GetHtmlAttribute( const char *attrs, const char *name, char *out, int outSize )
{
	int nameLen = (int)strlen( name );
	const char *p = attrs;

	while( *p )
	{
		while( *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ) p++;
		if( !*p ) break;

		// Match attribute name.
		if( !strncasecmp( p, name, nameLen ) )
		{
			const char *q = p + nameLen;
			while( *q == ' ' || *q == '\t' ) q++;
			if( *q == '=' )
			{
				q++;
				while( *q == ' ' || *q == '\t' ) q++;
				char quote = 0;
				if( *q == '"' || *q == '\'' ) quote = *q++;
				int n = 0;
				while( *q && ( quote ? *q != quote : ( *q != ' ' && *q != '\t' ) ) )
				{
					if( n < outSize - 1 )
						out[n++] = *q;
					q++;
				}
				out[n] = 0;
				return true;
			}
		}

		// Skip to the next whitespace-delimited attribute.
		while( *p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' ) p++;
	}

	return false;
}

// Measure the pixel width that DrawUtils::DrawHudString would use for the
// first 'count' characters of 'str'. This mirrors DrawHudString's handling of
// the backslash escapes and ^-color codes that it consumes without drawing.
static int MeasureHudStringWidthPrefix( const char *str, int count )
{
	int width = 0;
	int used = 0;

	for( const char *it = str; *it != 0 && *it != '\n' && used < count; it++, used++ )
	{
		if( *it == '\\' && *( it + 1 ) != '\n' && *( it + 1 ) != 0 )
		{
			// Escape sequence: consumes the next character, drawn as nothing.
			it++;
			used++;
			continue;
		}

		// ^<digit> color code: DrawHudString consumes it without drawing it.
		if( *it == '^' && *( it + 1 ) >= '0' && *( it + 1 ) <= '9' )
		{
			++it;
			used++;
			continue;
		}

		width += gHUD.GetCharWidth( (unsigned char)*it );
	}

	return width;
}

// Measure the full pixel width of a HUD string (up to a newline or NUL).
static int MeasureHudStringWidth( const char *str )
{
	return MeasureHudStringWidthPrefix( str, 0x7fffffff );
}

// Convert an HTML MOTD into plain text, recording per-line color spans into
// m_ColorSpans so Draw() can render each <font color="..."> run correctly.
void CHudMOTD::StripHtmlMOTD( const char *src, CUtlString &out )
{
	// Default (no span) color: the classic MOTD amber.
	const int defR = m_iDefaultR, defG = m_iDefaultG, defB = m_iDefaultB;

	int curR = defR, curG = defG, curB = defB;

	// Current line being built, its line index, and the index of its first span
	// in m_ColorSpans.
	CUtlString line;
	int lineIndex = 0;
	int lineSpanStart = 0;
	int lineLen = 0;

	// Fixed-size color stack for nested <font> tags.
	struct StackColor { int r, g, b; };
	StackColor colorStack[16];
	int colorDepth = 0;

	m_ColorSpans.RemoveAll();

	// Finish the current line: emit its text plus a separator and advance to the
	// next line. Spans were already appended to m_ColorSpans as they were met.
#define MOTD_FLUSH_LINE() \
		do { \
			out.Append( line.String() ); \
			out.Append( "\n" ); \
			line.Clear(); \
			lineLen = 0; \
			lineIndex++; \
			lineSpanStart = m_ColorSpans.Count(); \
		} while( 0 )

	// Emit a single decoded character, recording a color span on change.
#define MOTD_PUT_CHAR( ch ) \
		do { \
			if( m_ColorSpans.Count() == lineSpanStart || \
				m_ColorSpans[m_ColorSpans.Count() - 1].r != curR || \
				m_ColorSpans[m_ColorSpans.Count() - 1].g != curG || \
				m_ColorSpans[m_ColorSpans.Count() - 1].b != curB ) \
			{ \
				MOTDColorSpan span; \
				span.lineIndex = lineIndex; \
				span.charIndex = lineLen; \
				span.r = curR; span.g = curG; span.b = curB; \
				m_ColorSpans.AddToTail( span ); \
			} \
			line.AppendChar( ch ); \
			lineLen++; \
		} while( 0 )

	const char *p = src;

	while( *p )
	{
		if( *p == '<' )
		{
			// Skip HTML comments entirely.
			if( !strncmp( p, "<!--", 4 ) )
			{
				const char *end = strstr( p + 4, "-->" );
				p = end ? end + 3 : p + strlen( p );
				continue;
			}

			// script / style / head / title: drop their contents entirely. Note
			// that <head> holds metadata such as <title>, which is what produces
			// the stray "New Page 1" line in WYSIWYG-generated MOTDs.
			const char *closeName = NULL;
			if( !strncasecmp( p, "<script", 7 ) ) closeName = "</script";
			else if( !strncasecmp( p, "<style", 6 ) ) closeName = "</style";
			else if( !strncasecmp( p, "<head", 5 ) ) closeName = "</head";
			else if( !strncasecmp( p, "<title", 6 ) ) closeName = "</title";

			if( closeName )
			{
				const char *end = strcasestr( p, closeName );
				if( end )
				{
					const char *gt = strchr( end, '>' );
					p = gt ? gt + 1 : end + strlen( end );
				}
				else
				{
					p += strlen( p );
				}
				continue;
			}

			// Find the end of the tag.
			const char *gt = strchr( p, '>' );
			if( !gt )
			{
				// Unterminated '<': treat it as a literal character, not markup.
				MOTD_PUT_CHAR( '<' );
				p++;
				continue;
			}

			// Copy the tag body (without < and >) for parsing.
			char tag[128];
			int tagLen = (int)( gt - p - 1 );
			if( tagLen < 0 ) tagLen = 0;
			if( tagLen > (int)sizeof( tag ) - 1 ) tagLen = (int)sizeof( tag ) - 1;
			memcpy( tag, p + 1, tagLen );
			tag[tagLen] = 0;

			// Trim leading whitespace.
			char *t = tag;
			while( *t == ' ' || *t == '\t' || *t == '\n' || *t == '\r' ) t++;

			bool closing = ( *t == '/' );
			if( closing ) t++;
			while( *t == ' ' || *t == '\t' ) t++;

			// Extract the tag name.
			char tagName[32];
			int n = 0;
			while( t[n] && t[n] != ' ' && t[n] != '\t' && t[n] != '\n' && t[n] != '\r' && t[n] != '/' && n < (int)sizeof( tagName ) - 1 )
			{
				tagName[n] = t[n];
				n++;
			}
			tagName[n] = 0;

			// Tags that force a line break. Without this, block-level content
			// (divs, table rows, list items, headings) is concatenated into one
			// long line, which reads as left-aligned text.
			static const char *breakTags[] = {
				"br", "p", "div", "tr", "li", "ul", "ol", "table", "tbody",
				"thead", "tfoot", "h1", "h2", "h3", "h4", "h5", "h6",
				"blockquote", "pre", "hr", "section", "article", "header",
				"footer", "nav", "form", "fieldset", "dl", "dt", "dd", NULL
			};

			bool isBreakTag = false;
			for( int i = 0; breakTags[i]; i++ )
			{
				if( !strncasecmp( tagName, breakTags[i], strlen( breakTags[i] ) ) )
				{
					isBreakTag = true;
					break;
				}
			}

			if( isBreakTag )
			{
				// Only emit a break if there is pending text (or on <br>, which
				// always implies an explicit break).
				if( !strncasecmp( tagName, "br", 2 ) || lineLen > 0 )
					MOTD_FLUSH_LINE();
			}
			else if( !strncasecmp( tagName, "img", 3 ) )
			{
				// We cannot render remote images, but echoing the source URL keeps
				// image-only MOTDs visible instead of collapsing to an empty box.
				char value[512];
				if( !closing && GetHtmlAttribute( t, "src", value, sizeof( value ) ) )
				{
					if( lineLen > 0 )
						MOTD_FLUSH_LINE();
					for( const char *u = value; *u; u++ )
						MOTD_PUT_CHAR( *u );
				}
			}
			else if( !strncasecmp( tagName, "font", 4 ) )
			{
				if( closing )
				{
					if( colorDepth > 0 )
					{
						colorDepth--;
						curR = colorStack[colorDepth].r;
						curG = colorStack[colorDepth].g;
						curB = colorStack[colorDepth].b;
					}
					else
					{
						curR = defR; curG = defG; curB = defB;
					}
				}
				else
				{
					if( colorDepth < 16 )
					{
						colorStack[colorDepth].r = curR;
						colorStack[colorDepth].g = curG;
						colorStack[colorDepth].b = curB;
						colorDepth++;
					}

					char value[64];
					if( GetHtmlAttribute( t, "color", value, sizeof( value ) ) )
					{
						int r, g, b;
						if( ParseHtmlColor( value, r, g, b ) )
						{
							curR = r; curG = g; curB = b;
						}
					}
				}
			}
			// All other tags are dropped.

			p = gt + 1;
			continue;
		}

		if( *p == '&' )
		{
			char decoded[5] = { 0 };
			int decodedLen = 0;
			int consumed = DecodeHtmlEntity( p + 1, decoded, decodedLen );
			if( consumed > 0 )
			{
				for( int i = 0; i < decodedLen; i++ )
					MOTD_PUT_CHAR( decoded[i] );
				p += consumed;
				continue;
			}
			// Not an entity: fall through and emit literally.
		}

		if( *p == '\r' )
		{
			p++;
			continue;
		}

		if( *p == '\n' )
		{
			MOTD_FLUSH_LINE();
			p++;
			continue;
		}

		MOTD_PUT_CHAR( *p );
		p++;
	}

	// Flush the final line. It does not get a trailing newline; the previous
	// flushed line already contributed the separator.
	out.Append( line.String() );

	#undef MOTD_FLUSH_LINE
	#undef MOTD_PUT_CHAR
}
