#include "hud.h"
#include "cl_util.h"
#include <math.h>
#include "view_bob.h"

// Coefficients the offsets reach the view model through.
#define BOB_CLASSIC_FORWARD_SCALE	0.4f
#define BOB_CLASSIC_SWAY_PITCH		0.3f
#define BOB_CLASSIC_SWAY_YAW		0.5f
#define BOB_CLASSIC_SWAY_ROLL		1.0f
#define BOB_MODERN_FORWARD_SCALE	0.4f
#define BOB_MODERN_UP_SCALE			0.1f
#define BOB_MODERN_SIDE_SCALE		0.2f

#define BOB_PI						3.14159265358979323846f

static float BobPhase( double time, float cycle_len )
{
	float cycle = (float)( time - (int)( time / cycle_len ) * cycle_len ) / cycle_len;

	if ( !( cycle >= 0.0f && cycle < 1.0f ) )
	{
		cycle = 0.0f;
	}

	return cycle;
}

static float ClampBobFinite( float bob, float lo, float hi )
{
	if ( !( bob == bob ) || bob > 1e30f || bob < -1e30f )
	{
		return 0.0f;
	}

	return min( max( bob, lo ), hi );
}

static float RaisedCycle( float cycle, float bob_up )
{
	if ( cycle < bob_up )
	{
		return BOB_PI * cycle / bob_up;
	}

	return BOB_PI + BOB_PI * ( cycle - bob_up ) / ( 1.0f - bob_up );
}

float V_StepClassicBob( bob_classic_state_t &state, const bob_params_t &params, float frametime, float speed )
{
	state.bob_time += frametime;

	if ( params.bob_cycle <= 0.0f )
	{
		return 0.0f;
	}

	float cycle = RaisedCycle( BobPhase( state.bob_time, params.bob_cycle ), params.bob_up );

	float bob = speed * params.bob;
	bob = bob * 0.3f + bob * 0.7f * sinf( cycle );

	return ClampBobFinite( bob, -7.0f, 4.0f );
}

bob_modern_offsets_t V_StepModernBob( bob_modern_state_t &state, const bob_params_t &params, float time, float speed, bool onground )
{
	float max_speed_delta = max( 0.0f, ( time - state.last_bob_time ) * BOB_MODERN_SPEED_ACCEL );

	speed = min( max( speed, state.last_speed - max_speed_delta ), state.last_speed + max_speed_delta );
	speed = min( max( speed, -320.0f ), 320.0f );

	state.last_speed = speed;

	float lower_amt = params.lower_amt * ( speed * 0.001f );

	float bob_offset = speed / 320.0f;
	bob_offset = min( max( bob_offset, 0.0f ), 1.0f );

	state.bob_time += ( time - state.last_bob_time ) * bob_offset;
	state.last_bob_time = time;

	// Scaled by 1.25 so CS 1.6's default cl_bobcycle (0.8) looks right. Not in patch 10040.
	float bob_cycle = ( ( ( 1000.0f - 150.0f ) / 3.5f ) * 0.001f ) * params.bob_cycle * 1.25f;

	if ( bob_cycle <= 0.0f )
	{
		bob_modern_offsets_t none = { 0.0f, 0.0f };
		return none;
	}

	float cycle = RaisedCycle( BobPhase( state.bob_time, bob_cycle ), params.bob_up );

	float bob_scale = onground ? 0.00625f : 0.00125f;

	float vert = speed * ( bob_scale * params.amt_vert );
	vert = vert * 0.3f + vert * 0.7f * sinf( cycle );
	vert = ClampBobFinite( vert - lower_amt, -8.0f, 4.0f );

	// Kept verbatim from the original: the truncation gives half cycles, so this is
	// deliberately not BobPhase( bob_time, bob_cycle * 2 ).
	cycle = state.bob_time - (int)( state.bob_time / bob_cycle * 2 ) * bob_cycle * 2;
	cycle /= bob_cycle * 2;
	cycle = RaisedCycle( cycle, params.bob_up );

	float hor = speed * ( bob_scale * params.amt_lat );
	hor = hor * 0.3f + hor * 0.7f * sinf( cycle );
	hor = ClampBobFinite( hor, -7.0f, 4.0f );

	bob_modern_offsets_t result = { vert, hor };
	return result;
}

bob_offsets_t V_PlaceClassicBob( float bob, int style )
{
	bob_offsets_t offsets;

	offsets.forward = bob * BOB_CLASSIC_FORWARD_SCALE;
	offsets.up = 0.0f;
	offsets.side = 0.0f;
	offsets.pitch = 0.0f;
	offsets.yaw = 0.0f;
	offsets.roll = 0.0f;

	if ( style == BOBSTYLE_CLASSIC_SWAY )
	{
		offsets.pitch = -bob * BOB_CLASSIC_SWAY_PITCH;
		offsets.yaw = -bob * BOB_CLASSIC_SWAY_YAW;
		offsets.roll = -bob * BOB_CLASSIC_SWAY_ROLL;
	}

	return offsets;
}

bob_offsets_t V_PlaceModernBob( const bob_modern_offsets_t &offsets )
{
	bob_offsets_t placed;

	placed.forward = offsets.vert * BOB_MODERN_FORWARD_SCALE;
	placed.up = offsets.vert * BOB_MODERN_UP_SCALE;
	placed.side = offsets.hor * BOB_MODERN_SIDE_SCALE;
	placed.pitch = 0.0f;
	placed.yaw = 0.0f;
	placed.roll = 0.0f;

	return placed;
}
