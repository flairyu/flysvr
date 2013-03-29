#ifndef _FS_PLUGIN_H__
#define _FS_PLUGIN_H__

#include "fstype.h"

int on_new_client(struct fs_context* context, int cid);
int on_recv_data(struct fs_context* context, int cid);
int on_close_client(struct fs_context* context, int cid);
int on_write_data(struct fs_context* context, int cid);
int on_recv_from(struct fs_context* context, char* data, int length,
		struct sockaddr_in* fromaddr);
int on_load(struct fs_context* context);
int on_release(struct fs_context* context);

/*struct for game contents*/

#define MAX_PLAYER_COUNT 8
#define MAX_ROOM_COUNT 500 
#define MAX_IDLE_PLAYER 6000

struct kos_player {
	int pid; //player id.
	int cid; //client id.
	int rid; //room id.
	int gid; //group id. used for VS mode.
	char name[32]; //user name.
	int color;
	int life;
	int posx, posy; // position
	int clock; // player's timer clock.
	int delay; // player's delay
	int status; // players's state.
};

struct kos_room {
	int rid; // room id
	int type; // room type. 0 normal, 1 compare.
	int players[MAX_PLAYER_COUNT]; // 
	int player_count;
	int host_pid; // client who create this room
	int clock; // the fastest player's clock
};

struct kos_context {
	struct kos_room rooms[MAX_ROOM_COUNT]; // 0 is the ungame room.
	int idle_rooms[MAX_ROOM_COUNT];
	int idle_room;
	struct kos_player players[MAX_IDLE_PLAYER];
	int idle_players[MAX_IDLE_PLAYER];
	int idle_player;
};

/*
 * base cmd format:   7f k o s | CMD | ARG1 | ARG2 ...
 * CMD, ARG* are all 4 Bytes.
 */
enum kos_msg {
	LOGIN = 1, // ARG1:name_lenth, ARG2:varable name. max lenth is 32 Bytes.
	LOGOUT = 2, // ARGS:NONE.
	CREATE = 3, //create room. ARGS:NONE. return created room id.
	JOIN = 4,   //join room. ARG1:room id.
	LIST = 5,   //list all the rooms.
	LEAVE = 6,  //leave room
	KICK = 7,	//kick player
	MESSAGE = 8, //chatmessage
	ROOM_MSG = 9, //room message
	BROAD_MSG = 10, //message to everyone
	GAME_CMD = 11, //game controlling commands. like 'fire','move'
	GAME_START = 12, //start game
	GAME_STOP = 13, //stop game (with result)
	DELAY_TEST = 14, //delay test
	CLOCK = 15, //sync clock.
};

/*
 * general command struct.
 */
typedef union command {
	struct {
		int msglen; // total length without 'msglen'.
		int head;   // 0x77-K-o-S
		int cmd;
		int args[125];
	};
	char data[512];
} COMMAND;

int send_cmd(struct fs_context* context, int cid, int cmd, int argc, ...) ;
/*
 * game loop: login->create|join->      <-|
 * start->game_cmd,delaytest,message->stop+->leave->logout.
 */
#endif

