/*
THE COMPUTER CODE CONTAINED HEREIN IS THE SOLE PROPERTY OF PARALLAX
SOFTWARE CORPORATION ("PARALLAX").  PARALLAX, IN DISTRIBUTING THE CODE TO
END-USERS, AND SUBJECT TO ALL OF THE TERMS AND CONDITIONS HEREIN, GRANTS A
ROYALTY-FREE, PERPETUAL LICENSE TO SUCH END-USERS FOR USE BY SUCH END-USERS
IN USING, DISPLAYING,  AND CREATING DERIVATIVE WORKS THEREOF, SO LONG AS
SUCH USE, DISPLAY OR CREATION IS FOR NON-COMMERCIAL, ROYALTY OR REVENUE
FREE PURPOSES.  IN NO EVENT SHALL THE END-USER USE THE COMPUTER CODE
CONTAINED HEREIN FOR REVENUE-BEARING PURPOSES.  THE END-USER UNDERSTANDS
AND AGREES TO THE TERMS HEREIN AND ACCEPTS THE SAME BY USE OF THIS FILE.  
COPYRIGHT 1993-1998 PARALLAX SOFTWARE CORPORATION.  ALL RIGHTS RESERVED.
*/
/*
 * $Source: f:/miner/source/main/rcs/menu.c $
 * $Revision: 2.5 $
 * $Author: john $
 * $Date: 1995/10/07 13:19:09 $
 *
 * Inferno main menu.
 *
 * 
 *
 */

#pragma unused(rcsid)
static char rcsid[] = "$Id: menu.c 2.5 1995/10/07 13:19:09 john Exp $";

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>

#include "menu.h"
#include "inferno.h"
#include "game.h"
#include "key.h"
#include "error.h"
#include "mono.h"
#include "palette.h"
#include "newdemo.h"
#include "timer.h"
#include "gameseq.h"
#include "text.h"
#include "gamefont.h"
#include "newmenu.h"
#include "network.h"
#include "scores.h"
#include "modem.h"
#include "playsave.h"
#include "kconfig.h"
#include "credits.h"
#include "texmap.h"
#include "polyobj.h"
#include "state.h"
#include "songs.h"
#include "config.h"
#include "motion.h"

#ifdef EDITOR
#include "editor\editor.h"
#endif

#define EZERO 0

//char *menu_difficulty_text[] = { "Trainee", "Rookie", "Fighter", "Hotshot", "Insane" };
//char *menu_detail_text[] = { "Lowest", "Low", "Medium", "High", "Highest", "", "Custom..." };

#define MENU_NEW_GAME            0
#define MENU_GAME      				1 
#define MENU_EDITOR					2
#define MENU_VIEW_SCORES			3
#define MENU_QUIT                4
#define MENU_LOAD_GAME				5
#define MENU_SAVE_GAME				6
#define MENU_DEMO_PLAY				8
#define MENU_LOAD_LEVEL				9
#define MENU_START_NETGAME			10
#define MENU_JOIN_NETGAME			11
#define MENU_CONFIG					13
#define MENU_REJOIN_NETGAME		14
#define MENU_DIFFICULTY				15
#define MENU_START_SERIAL			18
#define MENU_HELP						19
#define MENU_NEW_PLAYER				20
#define MENU_MULTIPLAYER			21
#define MENU_STOP_MODEM				22
#define MENU_SHOW_CREDITS			23
#define MENU_ORDER_INFO				24
#define MENU_PLAY_SONG				25

//ADD_ITEM("Start netgame...", MENU_START_NETGAME, -1 );
//ADD_ITEM("Send net message...", MENU_SEND_NET_MESSAGE, -1 );

#define ADD_ITEM(t,value,key)  do { m[num_options].type=NM_TYPE_MENU; m[num_options].text=t; menu_choice[num_options]=value;num_options++; } while (0)

extern int last_joy_time;		//last time the joystick was used
#ifndef NDEBUG
extern int Speedtest_on;
#else
#define Speedtest_on 0
#endif

ubyte do_auto_demo = 1;			// Flag used to enable auto demo starting in main menu.
int Player_default_difficulty; // Last difficulty level chosen by the player
int Auto_leveling_on = 0;
int Menu_draw_copyright = 0;

// Options menu "Invert Y" checkbox. The stick handling in DescentView.java's
// onGenericMotionEvent() has always compared axis values the way an "invert Y enabled"
// scheme would, so this defaults ON (1) to match existing behavior unchanged -- turning
// it off is what actually flips anything. See gamepad_remap.c's isInvertYEnabled() JNI
// getter for how DescentView.java reads this.
int Config_invert_y = 1;

void autodemo_menu_check(int nitems, newmenu_item * items, int *last_key, int citem )
{
	int curtime;

	nitems = nitems;
	items=items;
	citem = citem;

	//draw copyright message
	if ( Menu_draw_copyright )		{
		Menu_draw_copyright = 0;
		gr_set_current_canvas(NULL);
		gr_set_curfont(GAME_FONT);
		gr_set_fontcolor(BM_XRGB(6,6,6),-1);
		gr_scale_printf(0x8000,grd_curcanv->cv_bitmap.bm_h-GAME_FONT->ft_h*f2fl(Scale_factor)-2*f2fl(Scale_factor),Scale_factor,Scale_factor,TXT_COPYRIGHT);
	}
	
	// Don't allow them to hit ESC in the main menu.
	if (*last_key==KEY_ESC) *last_key = 0;

	if ( do_auto_demo )	{
		curtime = timer_get_approx_seconds();
		//if ( ((keyd_time_when_last_pressed+i2f(20)) < curtime) && ((last_joy_time+i2f(20)) < curtime) && (!speedtest_on)  ) {
		if ( ((keyd_time_when_last_pressed+i2f(45)) < curtime) && (!Speedtest_on)  ) {
			keyd_time_when_last_pressed = curtime;			// Reset timer so that disk won't thrash if no demos.
			newdemo_start_playback(NULL);		// Randomly pick a file
			if (Newdemo_state == ND_STATE_PLAYBACK)	{
				Function_mode = FMODE_GAME;
				*last_key = -2;							  	
			}
		}
	}
}

//static int First_time = 1;
static int main_menu_choice = 0;

//	-----------------------------------------------------------------------------
//	Create the main menu.
void create_main_menu(newmenu_item *m, int *menu_choice, int *callers_num_options)
{
	int	num_options;

	#ifndef DEMO_ONLY
	num_options = 0;

//	//	Move down to allow for space to display "Destination Saturn"
//	if (Saturn) {
//		int	i;
//
//		for (i=0; i<4; i++)
//			ADD_ITEM("", 0, -1);
//
//		if (First_time) {
//			main_menu_choice = 4;
//			First_time = 0;
//		}
//	}

	ADD_ITEM(TXT_NEW_GAME,MENU_NEW_GAME,KEY_N);

#ifdef SHAREWARE
	if (get_game_list(NULL)>0)
#endif

  	ADD_ITEM(TXT_LOAD_GAME,MENU_LOAD_GAME,KEY_L);

	ADD_ITEM(TXT_MULTIPLAYER_,MENU_MULTIPLAYER,-1);

	ADD_ITEM(TXT_OPTIONS_, MENU_CONFIG, -1 );
	ADD_ITEM(TXT_CHANGE_PILOTS,MENU_NEW_PLAYER,unused);
	ADD_ITEM(TXT_VIEW_DEMO,MENU_DEMO_PLAY,0);
	ADD_ITEM(TXT_VIEW_SCORES,MENU_VIEW_SCORES,KEY_V);
	#ifdef SHAREWARE
	ADD_ITEM(TXT_ORDERING_INFO,MENU_ORDER_INFO,-1);
	#endif
	ADD_ITEM(TXT_CREDITS,MENU_SHOW_CREDITS,-1);
	#endif
	ADD_ITEM(TXT_QUIT,MENU_QUIT,KEY_Q);

	#ifndef RELEASE
	if (!(Game_mode & GM_MULTI ))	{
		//m[num_options].type=NM_TYPE_TEXT;
		//m[num_options++].text=" Debug options:";

		ADD_ITEM("  Load level...",MENU_LOAD_LEVEL ,KEY_N);
		#ifdef EDITOR
		ADD_ITEM("  Editor", MENU_EDITOR, KEY_E);
		#endif
	}

	ADD_ITEM( "  Play song", MENU_PLAY_SONG, -1 );
	#endif

	*callers_num_options = num_options;
}

void do_option(int select);

void do_multi_player_menu()
{
	int menu_choice[3];
	newmenu_item m[3];
	int choice = 0, num_options = 0;
	int old_game_mode;

	do {
		old_game_mode = Game_mode;
		num_options = 0;

		ADD_ITEM(TXT_START_NET_GAME, MENU_START_NETGAME, -1);
		ADD_ITEM(TXT_JOIN_NET_GAME, MENU_JOIN_NETGAME, -1);
		ADD_ITEM(TXT_MODEM_GAME, MENU_START_SERIAL, -1);

		choice = newmenu_do1(NULL, TXT_MULTIPLAYER, num_options, m, NULL, choice);

		if (choice > -1)
			do_option(menu_choice[choice]);

		if (old_game_mode != Game_mode)
			break;		// leave menu

	} while (choice > -1);

}

extern void show_order_form(void);	// John didn't want this in inferno.h so I just externed it.

int do_difficulty_menu()
{
	int s;
	newmenu_item m[5];
	
	m[0].type=NM_TYPE_MENU; m[0].text=MENU_DIFFICULTY_TEXT(0);
	m[1].type=NM_TYPE_MENU; m[1].text=MENU_DIFFICULTY_TEXT(1);
	m[2].type=NM_TYPE_MENU; m[2].text=MENU_DIFFICULTY_TEXT(2);
	m[3].type=NM_TYPE_MENU; m[3].text=MENU_DIFFICULTY_TEXT(3);
	m[4].type=NM_TYPE_MENU; m[4].text=MENU_DIFFICULTY_TEXT(4);
	
	s = newmenu_do1( NULL, TXT_DIFFICULTY_LEVEL, NDL, m, NULL, Difficulty_level);
	
	if (s > -1 )	{
		if (s != Difficulty_level)
		{
			Player_default_difficulty = s;
			write_player_file();
		}
		Difficulty_level = s;
		mprintf((0, "%s %s %i\n", TXT_DIFFICULTY_LEVEL, TXT_SET_TO, Difficulty_level));
		return 1;
	}
	return 0;
}

void do_new_game_menu()
{
	int n_missions,new_level_num,player_highest_level;
	
#ifndef SHAREWARE
	n_missions = build_mission_list(0);
	
	if (n_missions > 1) {
		int new_mission_num,i, default_mission;
		char * m[MAX_MISSIONS];
		
		default_mission = 0;
		for (i=0;i<n_missions;i++) {
			m[i] = Mission_list[i].mission_name;
			if ( !strcasecmp( m[i], config_last_mission ) )
				default_mission = i;
		}
		
		new_mission_num = newmenu_listbox1( "New Game\n\nSelect mission", n_missions, m, 1, default_mission, NULL );
		
		if (new_mission_num == -1)
			return;		//abort!
		
		strcpy(config_last_mission, m[new_mission_num]  );
		
		if (!load_mission(new_mission_num)) {
			nm_messagebox( NULL, 1, TXT_OK, "Error in Mission file");
			return;
		}
	}
#endif
	
	new_level_num = 1;
	
	player_highest_level = get_highest_level();
	
	if (player_highest_level > Last_level)
		player_highest_level = Last_level;
	
	if (player_highest_level > 1) {
		newmenu_item m[2];
		char info_text[80];
		char num_text[10];
		int choice;
		
	try_again:
		sprintf(info_text,"%s %d",TXT_START_ANY_LEVEL, player_highest_level);
		
		m[0].type=NM_TYPE_TEXT; m[0].text = info_text;
		m[1].type=NM_TYPE_INPUT; m[1].text_len = 10; m[1].text = num_text;
		
		strcpy(num_text,"1");
		
		choice = newmenu_do( NULL, TXT_SELECT_START_LEV, 2, &m, NULL );
		
		if (choice==-1 || m[1].text[0]==0)
			return;
		
		new_level_num = atoi(m[1].text);
		
		if (!(new_level_num>0 && new_level_num<=player_highest_level)) {
			m[0].text = TXT_ENTER_TO_CONT;
			nm_messagebox( NULL, 1, TXT_OK, TXT_INVALID_LEVEL);
			goto try_again;
		}
	}
	
	Difficulty_level = Player_default_difficulty;
	
	if (!do_difficulty_menu())
		return;
	
	gr_palette_fade_out( gr_palette, 32, 0 );
	
#ifdef PSX_BUILD_TOOLS
	{
		int i;
		for (i=Last_secret_level; i<=Last_level; i++ )	{
			if ( i!=0 )
				StartNewGame(i);
		}
	}
#endif
	
	StartNewGame(new_level_num);
	
}

//returns flag, true means quit menu
void do_option(int select)
{
	switch (select) {
	case MENU_NEW_GAME:
		do_new_game_menu();
		break;
	case MENU_GAME:
		break;
	case MENU_DEMO_PLAY:
	{
		char demo_file[16];
		if (newmenu_get_filename(TXT_SELECT_DEMO, "*.dem", demo_file, 1))	{
			newdemo_start_playback(demo_file);
		}
	}
	break;
	case MENU_LOAD_GAME:
#ifdef SHAREWARE
		do_load_game_menu();
#else
		state_restore_all(0);
#endif
		break;
#ifdef EDITOR
	case MENU_EDITOR:
		Function_mode = FMODE_EDITOR;
		init_cockpit();
		break;
#endif
	case MENU_VIEW_SCORES:
		gr_palette_fade_out(gr_palette, 32, 0);
		scores_view(-1);
		break;
#ifdef SHAREWARE
	case MENU_ORDER_INFO:
		show_order_form();
		break;
#endif
	case MENU_QUIT:
#ifdef EDITOR
		if (!SafetyCheck()) break;
#endif
		gr_palette_fade_out(gr_palette, 32, 0);
		Function_mode = FMODE_EXIT;
		break;
	case MENU_NEW_PLAYER:
		RegisterPlayer();		//1 == allow escape out of menu
		break;

	case MENU_HELP:
		do_show_help();
		break;

#ifndef RELEASE

	case MENU_PLAY_SONG:	{
		int i;
		char * m[MAX_SONGS];

		for (i = 0; i<MAX_SONGS; i++) {
			m[i] = Songs[i].filename;
		}
		i = newmenu_listbox("Select Song", MAX_SONGS, m, 1, NULL);

		if (i > -1)	{
			songs_play_song(i, 0);
		}
	}
							break;
	case MENU_LOAD_LEVEL: {
		newmenu_item m;
		char text[10] = "";
		int new_level_num;

		m.type = NM_TYPE_INPUT; m.text_len = 10; m.text = text;

		newmenu_do(NULL, "Enter level to load", 1, &m, NULL);

		new_level_num = atoi(m.text);

		if (new_level_num != 0 && new_level_num >= Last_secret_level && new_level_num <= Last_level)	{
			gr_palette_fade_out(gr_palette, 32, 0);
			StartNewGame(new_level_num);
		}

		break;
	}
#endif


	case MENU_START_NETGAME:
#ifdef NETWORK
		//temp!
		load_mission(0);
		network_start_game();
#endif
		break;
	case MENU_JOIN_NETGAME:
		//temp!
#ifdef NETWORK
		load_mission(0);
		network_join_game();
#endif
		break;
	case MENU_START_SERIAL:
#ifdef NETWORK
		com_main_menu();
#endif
		break;
	case MENU_MULTIPLAYER:
		do_multi_player_menu();
		break;
	case MENU_CONFIG:
		do_options_menu();
		break;
	case MENU_SHOW_CREDITS:
		gr_palette_fade_out(gr_palette, 32, 0);
		credits_show();
		break;
	default:
		Error("Unknown option %d in do_option", select);
		break;
	}

}

//returns number of item chosen
int DoMenu() 
{
	int menu_choice[25];
	newmenu_item m[25];
	int num_options = 0;

	if ( Players[Player_num].callsign[0]==0 )	{
		RegisterPlayer();
		return 0;
	}
	
	if ((Game_mode & GM_SERIAL) || (Game_mode & GM_MODEM)) {
		do_option(MENU_START_SERIAL);
		return 0;
	}

	create_main_menu(m, menu_choice, &num_options);

	do {
		keyd_time_when_last_pressed = timer_get_fixed_seconds();		// .. 20 seconds from now!
		if (main_menu_choice < 0 )	main_menu_choice = 0;		
		Menu_draw_copyright = 1;
		main_menu_choice = newmenu_do2( "", NULL, num_options, m, autodemo_menu_check, main_menu_choice, Menu_pcx_name);
		if ( main_menu_choice > -1 ) do_option(menu_choice[main_menu_choice]);
		create_main_menu(m, menu_choice, &num_options);	//	may have to change, eg, maybe selected pilot and no save games.
	} while( Function_mode==FMODE_MENU );

//	if (main_menu_choice != -2)
//		do_auto_demo = 0;		// No more auto demos
	if ( Function_mode==FMODE_GAME )	
		gr_palette_fade_out( gr_palette, 32, 0 );

	return main_menu_choice;
}

int	Max_debris_objects, Max_objects_onscreen_detailed;
int	Max_linear_depth_objects;

byte	Object_complexity=2, Object_detail=2;
byte	Wall_detail=2, Wall_render_depth=2, Debris_amount=2, SoundChannels = 2;

// Index into the Android render-scale presets (1.0x/1.25x/1.5x/1.75x/2.0x) -- see
// do_detail_level_menu_custom() below. Persisted via config.c like the other detail settings.
int Render_scale_index = 0;

// "Force 4:3" checkbox (do_detail_level_menu_custom() below) -- 0=Normal (stretch to fill
// whatever aspect ratio the device screen is, today's only behavior), 1=render into a
// centered 4:3 region of the screen (with black bars on the sides on a wider device),
// closer to the original DOS game's intended look. Like render scale, this reshapes the
// render surface itself, which can only safely happen once at app startup -- see
// setAspectRatio43() (Descent/src/main/cpp/motion.c) and DescentActivity.java.
int Aspect_ratio_4_3 = 0;

// Set by do_detail_level_menu_custom_menuset() below when Render Scale or Force 4:3 actually
// changes during a Video Options visit, and read (then cleared) by do_detail_level_menu_custom()
// once that submenu is exited -- the trigger for restartAppForSettingsChange() below. Checked
// only once per visit, on exit, rather than on every slider/checkbox tick, so dragging the
// Render Scale slider around doesn't restart the app mid-drag.
static int Video_settings_dirty = 0;

// The in-game FPS counter (main/game.c) used to only exist in non-RELEASE builds. It's now
// a normal persisted option -- see the "Show FPS Counter" checkbox below -- toggleable from
// both the title-screen and in-game pause menu versions of this submenu, unlike render scale.
extern int framerate_on;

byte	Render_depths[NUM_DETAIL_LEVELS-1] =								{ 6,  9, 12, 15, 20};
byte	Max_perspective_depths[NUM_DETAIL_LEVELS-1] =					{ 1,  2,  3,  5,  8};
byte	Max_linear_depths[NUM_DETAIL_LEVELS-1] =							{ 3,  5,  7, 10, 17};
byte	Max_linear_depths_objects[NUM_DETAIL_LEVELS-1] =				{ 1,  2,  3,  5, 12};
byte	Max_debris_objects_list[NUM_DETAIL_LEVELS-1] =					{ 2,  4,  7, 10, 15};
byte	Max_objects_onscreen_detailed_list[NUM_DETAIL_LEVELS-1] =	{ 2,  4,  7, 10, 15};
byte	Smts_list[NUM_DETAIL_LEVELS-1] =										{ 2,  4,  8, 16, 50};	//	threshold for models to go to lower detail model, gets multiplied by obj->size
byte	Max_sound_channels[NUM_DETAIL_LEVELS-1] =							{ 2,  4,  8, 12, 16};

//	-----------------------------------------------------------------------------
//	Set detail level based stuff.
//	Note: Highest detail level (detail_level == NUM_DETAIL_LEVELS-1) is custom detail level.
void set_detail_level_parameters(int detail_level)
{
	Assert((detail_level >= 0) && (detail_level < NUM_DETAIL_LEVELS));

	if (detail_level < NUM_DETAIL_LEVELS-1) {
		Render_depth = Render_depths[detail_level];
		Max_perspective_depth = Max_perspective_depths[detail_level];
		Max_linear_depth = Max_linear_depths[detail_level];
		Max_linear_depth_objects = Max_linear_depths_objects[detail_level];

		Max_debris_objects = Max_debris_objects_list[detail_level];
		Max_objects_onscreen_detailed = Max_objects_onscreen_detailed_list[detail_level];

		Simple_model_threshhold_scale = Smts_list[detail_level];

		digi_set_max_channels( Max_sound_channels[ detail_level ] );

		//	Set custom menu defaults.
		Object_complexity = detail_level;
		Wall_render_depth = detail_level;
		Object_detail = detail_level;
		Wall_detail = detail_level;
		Debris_amount = detail_level;
		SoundChannels = detail_level;

	}
}

//	-----------------------------------------------------------------------------
void do_detail_level_menu_custom_menuset(int nitems, newmenu_item * items, int *last_key, int citem)
{
	int in_title_menu = (Function_mode == FMODE_MENU);
	// Render Scale and Force 4:3 are both title-screen-only (see do_detail_level_menu_custom()
	// below), so together they shift every later item's index by 2 when present, 0 when not.
	int title_extra = in_title_menu ? 2 : 0;

	nitems = nitems;
	*last_key = *last_key;
	citem = citem;

	Object_complexity = items[0].value;
	Object_detail = items[1].value;
	Wall_detail = items[2].value;
	Wall_render_depth = items[3].value;
	Debris_amount = items[4].value;
	// Sound Channels now lives in the Options menu (see joydef_menuset()), not here.

	// Render scale and Force 4:3 are only offered from the title-screen menu
	// (Function_mode==FMODE_MENU) -- see do_detail_level_menu_custom() -- so items[5] and
	// items[6] only exist in that case.
	if (in_title_menu) {
		if (Render_scale_index != items[5].value) {
			Render_scale_index = items[5].value;
			setRenderScaleIndex(Render_scale_index);
			Video_settings_dirty = 1;
		}
		if (Aspect_ratio_4_3 != items[6].value) {
			Aspect_ratio_4_3 = items[6].value;
			setAspectRatio43(Aspect_ratio_4_3);
			Video_settings_dirty = 1;
		}
	}

	// FPS counter toggle -- offered from both menu locations, so it always exists, but its
	// index shifts by title_extra when the title-only rows above are also present.
	framerate_on = items[5 + title_extra].value;
}

void set_custom_detail_vars(void)
{
	Render_depth = Render_depths[Wall_render_depth];

	Max_perspective_depth = Max_perspective_depths[Wall_detail];
	Max_linear_depth = Max_linear_depths[Wall_detail];

	Max_debris_objects = Max_debris_objects_list[Debris_amount];

	Max_objects_onscreen_detailed = Max_objects_onscreen_detailed_list[Object_complexity];
	Simple_model_threshhold_scale = Smts_list[Object_complexity];
	Max_linear_depth_objects = Max_linear_depths_objects[Object_detail];
}

extern void game_flush_inputs(void);
extern void showRenderBuffer(void);	// Descent/src/main/cpp/render.c -- presents the frame
										// (eglSwapBuffers); without this the text drawn below
										// would never actually become visible on screen.
extern void delay(unsigned long time);	// main/kconfig.c -- ~100Hz usleep throttle.

// Shown right before the app closes for a settings-triggered restart (see
// do_detail_level_menu_custom() below). This used to wait for a tap or the gamepad confirm
// button, but this device's always-on analog-stick menu navigation continuously feeds synthetic
// key/confirm-adjacent input from any stick drift, which made a reliable "wait for a genuine
// press" dismiss check more trouble than it was worth. A flat timer sidesteps that whole class
// of problem: the message is simply on screen for a couple of seconds, then the app closes on
// its own, no input handling involved. No canvas/background save-restore is needed here (unlike
// every other custom-drawn screen in this codebase) because the process closes moments after
// this returns; nothing will ever need to be redrawn.
static void show_video_restart_message(void)
{
	// TITLE_FONT (Gamefonts[GFONT_BIG_1]) is a #define private to main/newmenu.c, not exported
	// via any header -- GFONT_BIG_1 itself (from lib/gamefont.h, already included here) is the
	// part that's actually shared, so index Gamefonts with it directly instead.
	grs_font *title_font = Gamefonts[GFONT_BIG_1];
	fix close_at;

	gr_set_current_canvas(NULL);
	nm_draw_background(0, 0, grd_curcanv->cv_bitmap.bm_w - 1, grd_curcanv->cv_bitmap.bm_h - 1);

	gr_set_curfont(title_font);
	gr_set_fontcolor(BM_XRGB(31, 31, 31), -1);
	// "Settings changed." rather than "Video settings changed." -- the longer wording ran past
	// the edge of the screen once the render surface itself is already in Force 4:3 (narrower)
	// mode, which is exactly the case this message is likeliest to show up in.
	gr_scale_printf(0x8000, grd_curcanv->cv_bitmap.bm_h / 2 - title_font->ft_h * f2fl(Scale_factor),
		Scale_factor, Scale_factor, "Settings changed.");
	gr_scale_printf(0x8000, grd_curcanv->cv_bitmap.bm_h / 2 + title_font->ft_h * f2fl(Scale_factor),
		Scale_factor, Scale_factor, "Restart required.");

#ifdef OGLES
	showRenderBuffer();
#endif

	game_flush_inputs();
	close_at = timer_get_approx_seconds() + i2f(2);
	while (timer_get_approx_seconds() < close_at) {
		// Just hold the frame on screen -- deliberately not reading any input here. delay()
		// throttles this to ~100Hz instead of spinning the CPU flat-out for two seconds.
		delay(10);
	}
}

//	-----------------------------------------------------------------------------
void do_detail_level_menu_custom(void)
{
	int	s = 0;
	// Render scale and Force 4:3 both change the shape/size of the render surface itself,
	// which this old engine only sets up once at startup -- it isn't safe to resize or
	// reshape mid-game. So both are only offered here when reached from the title screen
	// (before a game is running), not from the in-game pause menu (Function_mode==FMODE_GAME
	// at that point).
	int in_title_menu = (Function_mode == FMODE_MENU);
	// How many extra title-screen-only rows (Render Scale, Force 4:3) are inserted above --
	// every row after them shifts by this many slots. 0 when not in the title menu.
	int title_extra = in_title_menu ? 2 : 0;
	newmenu_item m[9];

	// Cleared on every entry so a stale flag from some earlier visit can't cause a spurious
	// restart -- only a real change made *during this visit* (see
	// do_detail_level_menu_custom_menuset() above) should trigger one.
	Video_settings_dirty = 0;

	do {
		m[0].type = NM_TYPE_SLIDER;
		m[0].text = TXT_OBJ_COMPLEXITY;
		m[0].value = Object_complexity;
		m[0].min_value = 0;
		m[0].max_value = NDL - 1;

		m[1].type = NM_TYPE_SLIDER;
		m[1].text = TXT_OBJ_DETAIL;
		m[1].value = Object_detail;
		m[1].min_value = 0;
		m[1].max_value = NDL - 1;

		m[2].type = NM_TYPE_SLIDER;
		m[2].text = TXT_WALL_DETAIL;
		m[2].value = Wall_detail;
		m[2].min_value = 0;
		m[2].max_value = NDL - 1;

		m[3].type = NM_TYPE_SLIDER;
		m[3].text = TXT_WALL_RENDER_DEPTH;
		m[3].value = Wall_render_depth;
		m[3].min_value = 0;
		m[3].max_value = NDL - 1;

		m[4].type = NM_TYPE_SLIDER;
		m[4].text = TXT_DEBRIS_AMOUNT;
		m[4].value = Debris_amount;
		m[4].min_value = 0;
		m[4].max_value = NDL - 1;

		// Sound Channels moved to the Options menu -- see do_options_menu().

		if (in_title_menu) {
			m[5].type = NM_TYPE_SLIDER;
			m[5].text = "Render Scale";
			m[5].value = Render_scale_index;
			m[5].min_value = 0;
			m[5].max_value = 4;

			// Checked = render into a centered 4:3 region of the screen (black bars on
			// the sides on a wider device) instead of stretching to fill it. Worded as
			// "Force" rather than "4:3 Aspect Ratio" so a player already on a 4:3 device
			// isn't misled into thinking they need to check it -- their screen already
			// displays in 4:3 either way; this is only for stretched (e.g. 16:9) devices
			// that want the more original-looking pillarboxed presentation instead.
			m[6].type = NM_TYPE_CHECK;
			m[6].text = "Force 4:3";
			m[6].value = Aspect_ratio_4_3;
		}

		m[5 + title_extra].type = NM_TYPE_CHECK;
		m[5 + title_extra].text = "Show FPS Counter";
		m[5 + title_extra].value = framerate_on;

		// Used to be followed by a stray "lo   hi" NM_TYPE_TEXT row (TXT_LO_HI) -- a leftover
		// slider-endpoint legend from the original DOS UI that never made sense as a standalone
		// row in this custom submenu. See the "stray text at the bottom of Video Options" report.

		s = newmenu_do1(NULL, "Video Options", 6 + title_extra, m, do_detail_level_menu_custom_menuset, s);
	} while (s > -1);

	set_custom_detail_vars();

	// Render Scale and/or Force 4:3 actually changed during this visit -- both need a fresh
	// process to take effect (see the comment on Aspect_ratio_4_3 above). Getting Android to
	// reliably relaunch itself in the foreground turned out not to be solvable cleanly -- every
	// approach tried either raced this game's own singleTask launch mode or ran straight into
	// Android's background-activity-start restrictions, landing the relaunch minimized in
	// Recents instead of in front of the player. So instead: save everything, tell the player
	// plainly what's happening, and close -- see restartAppForSettingsChange() below (JNI call
	// into DescentActivity.java) and its "-quickresume" resume path, which still makes the
	// player's *next* manual launch skip the intro logos and pilot picker and land back on the
	// main menu, so reopening it still feels quick even though it isn't fully automatic.
	if (in_title_menu && Video_settings_dirty) {
		// Persist Render_scale_index/Aspect_ratio_4_3 to the native config file *now* --
		// normally that only happens when the whole game session ends (see WriteConfigFile()
		// in main/inferno.c) or a pilot is (re)registered, neither of which happens on this
		// path, since restartAppForSettingsChange() below closes the app directly. Without
		// this, the value driving the Android-side render surface (already durably saved via
		// setRenderScaleIndex()/setAspectRatio43()'s SharedPreferences commit()) would go out
		// of sync with what this menu reads back into Render_scale_index/Aspect_ratio_4_3 the
		// next time it's opened -- which is exactly what caused Force 4:3 to apply correctly to
		// the render surface while its checkbox still showed unchecked afterward.
		WriteConfigFile();

		show_video_restart_message();

		restartAppForSettingsChange();
	}
}

void do_load_game_menu()
{
	newmenu_item m[N_SAVE_SLOTS];
	char *saved_text[N_SAVE_SLOTS];
	int i,choice;

	get_game_list(saved_text);

	for (i=0;i<N_SAVE_SLOTS;i++) {

		if (saved_text[i][0]) {
			m[i].type = NM_TYPE_MENU;
			m[i].text = saved_text[i];
		}
		else {
			m[i].type = NM_TYPE_TEXT;
			m[i].text = TXT_EMPTY;
		}
	}

	choice = newmenu_do( NULL, TXT_LOAD_GAME, N_SAVE_SLOTS, m, NULL );

	if (choice != -1) {
		int ret;

		if ((ret=load_player_game(choice)) == EZERO)
			ResumeSavedGame(Players[Player_num].level);
		else {
			newmenu_item m1[3];

			m1[0].type = NM_TYPE_TEXT;  m1[0].text = strerror(ret);
			m1[1].type = NM_TYPE_TEXT;  m1[1].text = "";
			m1[2].type = NM_TYPE_TEXT;  m1[2].text = TXT_ENTER_TO_CONT;

			newmenu_do( NULL, TXT_ERR_LOADING_GAME, 3, m1, NULL );

		}
	}
}

void do_save_game_menu()
{
	newmenu_item m[N_SAVE_SLOTS];
	char *saved_text_ptrs[N_SAVE_SLOTS];
	char menu_text[N_SAVE_SLOTS][GAME_NAME_LEN+1];		//+1 for terminating zero
	int i,choice;

	get_game_list(saved_text_ptrs);

	for (i=0;i<N_SAVE_SLOTS;i++) {

		strcpy(menu_text[i],saved_text_ptrs[i]);

		m[i].type = NM_TYPE_INPUT_MENU;
		m[i].text_len = GAME_NAME_LEN;
		m[i].text = menu_text[i];

		if (!menu_text[i][0])
			strcpy(menu_text[i],TXT_EMPTY);

	}

	choice = newmenu_do( NULL, TXT_SAVE_GAME_SLOTS, N_SAVE_SLOTS, m, NULL );

	if (choice != -1) {
		int ret;
		char *src, *dst;
		int is_blank = 1;

		// Strip out any control characters (a stray Enter/line-break byte can end up in
		// here from how the on-screen keyboard's confirm key gets fed into this field --
		// see draw_item()'s NM_TYPE_INPUT_MENU case). A control character doesn't render as
		// anything visible, so a name that's really just an Enter keypress *looks* blank on
		// screen but isn't an empty string, and would slip past a check that only looks for
		// '\0' or spaces.
		for (src = dst = m[choice].text; *src; src++) {
			if (!iscntrl((unsigned char) *src))
				*dst++ = *src;
		}
		*dst = '\0';

		// A blank (or whitespace-only, once the above strip runs) name is confusing to tell
		// apart from the others in the load list later -- rather than blocking the save and
		// re-prompting (which didn't reliably catch this in testing), just fall back to a
		// plain default name and let the save proceed normally.
		for (src = m[choice].text; *src; src++) {
			if (*src != ' ') {
				is_blank = 0;
				break;
			}
		}
		if (is_blank) {
			strcpy(m[choice].text, "default");
		}

		if ((ret=save_player_game(choice,m[choice].text)) != EZERO)
			nm_messagebox( NULL,1, TXT_CONTINUE,"%s\n%s\n\n", TXT_SAVE_ERROR, strerror(ret));
	}

}

extern void GameLoop(int, int );

// Set by do_options_menu() before each menu build so joydef_menuset() (called while the
// menu is live) knows where the Brightness slider currently landed -- its index shifts
// depending on whether the "Use Gyroscope" entry is present.
static int Options_menu_have_gyroscope = 0;

void joydef_menuset(int nitems, newmenu_item * items, int *last_key, int citem )
{
	int brightness_item = 11 + Options_menu_have_gyroscope;

	nitems=nitems;
	*last_key = *last_key;

	if ( citem == brightness_item )	{
		gr_palette_set_gamma(items[brightness_item].value);
	}

	if ( Config_digi_volume != items[0].value )	{
		Config_digi_volume = items[0].value;
		digi_set_digi_volume( (Config_digi_volume*32768)/8 );
		digi_play_sample_once( SOUND_DROP_BOMB, F1_0 );
	}

	if (Config_midi_volume != items[1].value )	{
		Config_midi_volume = items[1].value;
		digi_set_midi_volume( (Config_midi_volume*128)/8 );
	}
}

//this change was made in DESCENT.TEX, but since we're not including that
//file in the v1.1 update, we're making the change in the code here also
#ifdef SHAREWARE
#undef	TXT_JOYS_SENSITIVITY
#define	TXT_JOYS_SENSITIVITY "Joystick/Mouse\nSensitivity"
#endif

extern void do_remap_gamepad_menu(void);

// Options menu "Touch Scaling" slider -- Descent/src/main/cpp/controls.c. Persisted the
// same way, one more trailing byte (main/playsave.c).
extern ubyte Config_touch_control_scale;
extern void touch_control_scale_changed(void);

// Pause-menu Cheats submenu (do_cheats_menu() below), called directly from
// main/game.c's do_game_menu(), one level up from Options. Every effect here is the
// exact same code the classic typed cheat codes (GABBAGABBAHEY, then
// RACERX/GUILE/TWILIGHT/MITZI/SCOURGE/etc.) already trigger, factored out into these
// standalone functions in main/game.c so this menu is just a second front end onto the
// same, single implementation of what each cheat actually does -- see the "add a Cheats
// submenu, leverage the existing cheat codes" request.
extern void cheat_toggle_invulnerability(void);
extern void cheat_toggle_cloak(void);
extern void cheat_fill_shields(void);
extern void cheat_grant_all_keys(void);
extern void cheat_full_arsenal(void);
extern void cheat_extra_life(void);
extern void cheat_toggle_ghost_mode(void);
extern void cheat_toggle_turbo_mode(void);
extern void cheat_toggle_robot_firing(void);
extern void cheat_warp_to_level(int new_level_num);
extern int Physics_cheat_flag;
extern int Game_turbo_mode;
extern int Robot_firing_enabled;

void do_cheats_menu(void)
{
	newmenu_item m[11];
	// Captured fresh at the top of every redraw so a flag that changes for some other
	// reason while this menu is open (invulnerability running out on its own timer, say)
	// still shows correctly -- and compared against what the player leaves each checkbox
	// as once newmenu_do1() returns, so a toggle below only fires for a checkbox the
	// player actually touched, not every row on every redraw.
	int was_invuln, was_cloaked, was_ghost, was_turbo, was_robots_fire;
	int i = 0;

	do {
		was_invuln = (Players[Player_num].flags & PLAYER_FLAGS_INVULNERABLE) != 0;
		was_cloaked = (Players[Player_num].flags & PLAYER_FLAGS_CLOAKED) != 0;
		was_ghost = (Physics_cheat_flag == 0xBADA55);
		was_turbo = (Game_turbo_mode != 0);
		was_robots_fire = (Robot_firing_enabled != 0);

		m[0].type = NM_TYPE_CHECK; m[0].text = "Invulnerability"; m[0].value = was_invuln;
		m[1].type = NM_TYPE_CHECK; m[1].text = "Cloak"; m[1].value = was_cloaked;
		m[2].type = NM_TYPE_CHECK; m[2].text = "Ghost Mode (no clip)"; m[2].value = was_ghost;
		m[3].type = NM_TYPE_CHECK; m[3].text = "Turbo Mode"; m[3].value = was_turbo;
		m[4].type = NM_TYPE_CHECK; m[4].text = "Robots Can Fire"; m[4].value = was_robots_fire;
		m[5].type = NM_TYPE_TEXT; m[5].text = "";
		m[6].type = NM_TYPE_MENU; m[6].text = "Full Shields";
		m[7].type = NM_TYPE_MENU; m[7].text = "All Keys";
		m[8].type = NM_TYPE_MENU; m[8].text = "Full Arsenal";
		m[9].type = NM_TYPE_MENU; m[9].text = "Extra Life";
		m[10].type = NM_TYPE_MENU; m[10].text = "Warp to Level...";

		i = newmenu_do1(NULL, "Cheats", 11, m, NULL, i);

		if (m[0].value != was_invuln) cheat_toggle_invulnerability();
		if (m[1].value != was_cloaked) cheat_toggle_cloak();
		if (m[2].value != was_ghost) cheat_toggle_ghost_mode();
		if (m[3].value != was_turbo) cheat_toggle_turbo_mode();
		if (m[4].value != was_robots_fire) cheat_toggle_robot_firing();

		if (i == 6) cheat_fill_shields();
		if (i == 7) cheat_grant_all_keys();
		if (i == 8) cheat_full_arsenal();
		if (i == 9) cheat_extra_life();
		if (i == 10) {
			// Same "type a level number" prompt the typed "farmerjoe" cheat uses (see
			// game.c) -- this menu just owns the prompt, cheat_warp_to_level() owns the
			// validate-and-go-there logic, same as every other cheat here.
			newmenu_item wm;
			char text[10] = "";
			int item;
			wm.type = NM_TYPE_INPUT; wm.text_len = 10; wm.text = text;
			item = newmenu_do(NULL, TXT_WARP_TO_LEVEL, 1, &wm, NULL);
			if (item != -1) {
				cheat_warp_to_level(atoi(text));
				// StartNewLevel() (inside cheat_warp_to_level()) tears down and rebuilds
				// the whole level/render/briefing state. The typed "farmerjoe" cheat only
				// ever calls it from the top-level game input loop, never while a menu is
				// still up on top of it. Looping back here to redraw the Cheats menu over
				// that in-flight rebuild is exactly what was causing the missing briefing
				// background, scrambled colors, and the app minimizing after warping --
				// so bail out of this menu immediately instead of looping again.
				return;
			}
		}
	} while (i > -1);
}

void do_options_menu()
{
	newmenu_item m[14];
	int i = 0;
	int have_gyroscope = haveGyroscope();
	Options_menu_have_gyroscope = have_gyroscope;

	do {
		m[0].type = NM_TYPE_SLIDER; m[0].text=TXT_FX_VOLUME; m[0].value=Config_digi_volume;m[0].min_value=0; m[0].max_value=8;
		m[1].type = NM_TYPE_SLIDER; m[1].text=TXT_MUSIC_VOLUME; m[1].value=Config_midi_volume;m[1].min_value=0; m[1].max_value=8;
		m[2].type = NM_TYPE_CHECK; m[2].text=TXT_REVERSE_STEREO; m[2].value=Config_channels_reversed;
		m[3].type = NM_TYPE_TEXT; m[3].text="";
		m[4].type = NM_TYPE_MENU; m[4].text="Remap Gamepad";
		m[5].type = NM_TYPE_SLIDER; m[5].text="Look Sensitivity"; m[5].value=Config_joystick_sensitivity; m[5].min_value =0; m[5].max_value = 8;
		m[6].type = NM_TYPE_SLIDER; m[6].text="Touch Scaling"; m[6].value=Config_touch_control_scale; m[6].min_value=0; m[6].max_value=8;
		m[7].type = NM_TYPE_CHECK; m[7].text="Invert Y"; m[7].value=Config_invert_y;
		m[8].type = NM_TYPE_TEXT; m[8].text="";
		m[9].type = NM_TYPE_CHECK; m[9].text="Ship auto-leveling"; m[9].value=Auto_leveling_on;
		if (have_gyroscope) {
			m[10].type = NM_TYPE_CHECK;
			m[10].text = "Use Gyroscope";
			m[10].value = Config_use_gyroscope;
		}
		m[10 + have_gyroscope].type = NM_TYPE_TEXT; m[10 + have_gyroscope].text="";
		m[11 + have_gyroscope].type = NM_TYPE_SLIDER; m[11 + have_gyroscope].text=TXT_BRIGHTNESS; m[11 + have_gyroscope].value=gr_palette_get_gamma();m[11 + have_gyroscope].min_value=0; m[11 + have_gyroscope].max_value=8;
		m[12 + have_gyroscope].type = NM_TYPE_MENU; m[12 + have_gyroscope].text="Video Options";

		i = newmenu_do1( NULL, TXT_OPTIONS, 13 + have_gyroscope, m, joydef_menuset, i );

		if (i == 4) {
			do_remap_gamepad_menu();
		}

		if (i == 12 + have_gyroscope) {
			do_detail_level_menu_custom();
		}

		Config_channels_reversed = m[2].value;
		Config_joystick_sensitivity = m[5].value;
		if (Config_touch_control_scale != m[6].value) {
			Config_touch_control_scale = (ubyte) m[6].value;
			touch_control_scale_changed();
		}
		Config_invert_y = m[7].value;
		Auto_leveling_on = m[9].value;
		if (have_gyroscope) {
			if (Config_use_gyroscope != m[10].value) {
				if (m[10].value) {
					startMotion();
				} else {
					stopMotion();
				}
			}
			Config_use_gyroscope = m[10].value;
		}
	} while( i>-1 );

	if ( Config_midi_volume < 1 )	{
		digi_play_midi_song( NULL, NULL, NULL, 0 );
	}

	write_player_file();
}
