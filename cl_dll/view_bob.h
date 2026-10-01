// View bobbing styles.
#pragma once

// cl_viewbob values.
enum ViewBobStyle
{
	BOBSTYLE_CLASSIC = 0,
	BOBSTYLE_CLASSIC_SWAY = 1,
	BOBSTYLE_MODERN = 2
};

// Units/s per second the modern bob lets the speed it follows change by.
#define BOB_MODERN_SPEED_ACCEL	620.0f

struct bob_params_t
{
	int		style;			// ViewBobStyle; any other value behaves as BOBSTYLE_CLASSIC
	float	bob;			// cl_bob
	float	bob_cycle;		// cl_bobcycle, seconds per cycle; <= 0 disables the bob
	float	bob_up;			// cl_bobup, fraction of the cycle spent rising
	float	amt_vert;		// cl_bobamt_vert
	float	amt_lat;		// cl_bobamt_lat
	float	lower_amt;		// cl_bob_lower_amt
	bool	camera_bob;		// cl_bob_camera, classic styles also move the view origin
};

struct bob_classic_state_t
{
	double	bob_time;
};

struct bob_modern_state_t
{
	float	bob_time;
	float	last_bob_time;
	float	last_speed;
};

struct bob_modern_offsets_t
{
	float	vert;
	float	hor;
};

// Where a bob step puts the view model: offsets in world units and angles in degrees.
struct bob_offsets_t
{
	float	forward;
	float	up;
	float	side;
	float	pitch;
	float	yaw;
	float	roll;
};

// speed is the horizontal speed in units/s. Returns the offset in world units, clamped to [-7, 4].
float V_StepClassicBob( bob_classic_state_t &state, const bob_params_t &params, float frametime, float speed );

// time is absolute seconds. vert is clamped to [-8, 4], hor to [-7, 4], both in world units.
bob_modern_offsets_t V_StepModernBob( bob_modern_state_t &state, const bob_params_t &params, float time, float speed, bool onground );

// style is the classic style the bob was stepped for; BOBSTYLE_CLASSIC_SWAY turns the model as well.
bob_offsets_t V_PlaceClassicBob( float bob, int style );
bob_offsets_t V_PlaceModernBob( const bob_modern_offsets_t &offsets );
