#include "fsplugin.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

static int HEAD_KOS = (((int)(0x77))<<24)|(((int)('K')<<16))|(((int)('o'))<<8)|'S';

static struct kos_context kos;

// tool kits
int send_cmd(struct fs_context* context, int cid, int msg, int argc, ...) {
	COMMAND cmd;
	int i=0;
	va_list va;
	
	memset(&cmd, 0, sizeof(COMMAND));
	cmd.msglen = 8;
	cmd.head = HEAD_KOS;
	cmd.cmd = msg;
	va_start(va, argc);
	for(i=0; i<argc; i++) {
		cmd.args[i] = va_arg(va, int);
		cmd.msglen += 4;
	}
	va_end(va);
	context->write_data(cmd.data, cmd.msglen+4, cid);
}

int send_player(struct fs_context* context, int cid, int msg, int arg0, struct kos_player* player) {
	COMMAND cmd;
	int i=0;
	memset(&cmd, 0, sizeof(COMMAND));
	cmd.msglen = 0;
	cmd.head = HEAD_KOS; cmd.msglen+=4;
	cmd.cmd = msg; cmd.msglen+=4;
	cmd.args[0] = arg0; cmd.msglen+=4;
	cmd.args[1] = player->pid; cmd.msglen+=4;
	cmd.args[2] = player->color; cmd.msglen+=4;
	cmd.args[3] = strlen(player->name); cmd.msglen+=4;
	strcpy((char*)&cmd.args[4], player->name); cmd.msglen+=cmd.args[3];
	context->write_data(cmd.data, cmd.msglen+4, cid);
}

int get_idle_room() {
	int retval = kos.idle_room;
	if (kos.idle_room < MAX_ROOM_COUNT) {
		kos.idle_room++;
	}
	return retval;
}

int set_idle_room(int rid) {
	int i;
	for(i=1; i<kos.idle_room; i++) {
		if(kos.idle_rooms[i] == rid) {
			break;
		}
	}
	if(i==kos.idle_room) return 0;
	for(;i<kos.idle_room-1;i++) {
		kos.idle_rooms[i] = kos.idle_rooms[i+1];
	}
	if(kos.idle_room>1) {
		kos.idle_room--;
	}
	kos.idle_rooms[kos.idle_room] = rid;
	memset(&kos.rooms[rid], 0, sizeof(struct kos_room));
}

int get_idle_player() {
	int retval = kos.idle_player;
	if (kos.idle_player < MAX_IDLE_PLAYER) {
		kos.idle_player++;
	}
	return retval;
}

int set_idle_player(int pid) {
	int i;

	for(i=1; i<kos.idle_player; i++) {
		if (kos.idle_players[i] == pid) {
			break;
		}
	}
	
	if (i==kos.idle_player) return 0;

	for(;i<kos.idle_player-1; i++) {
		kos.idle_players[i] = kos.idle_players[i+1];
	}

	if(kos.idle_player>1) {
		kos.idle_player--;
	}
	kos.idle_players[kos.idle_player] = pid;
	memset(&kos.players[pid], 0, sizeof(struct kos_player));
}

int process_login(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid;
	struct kos_player* player;
	int namelen = 0;

	// SIZE(4)|HEAD(4)|CMD(4)|COLOR(4)|NAMELEN(4)|NAME..
	pid = get_idle_player();
	if (pid == MAX_IDLE_PLAYER) {
		// server is full.
		// reply: SIZE(4)|HEAD(4)|CMD(4)|RESULT(4)
		send_cmd(context, cid, LOGIN, 1, 0);
		context->log(LOG_W, "process_login: server is full. cid:%d", cid);
		return 1;
	}

	// success get player.
	player = &kos.players[pid];
	player->pid = pid;
	player->cid = cid;
	player->rid = 0;
	player->gid = 0;
	if(cmd->args[1]>32) cmd->args[1] = 32;
	memcpy(player->name, &cmd->data[20], cmd->args[1]);
	player->life = 0;
	player->posx = 0;
	player->posy = 0;
	player->clock = 0;
	player->delay = 0;
	player->status = 0;
	// reply: SIZE(4)|HEAD(4)|CMD(4)|PID(4)
	send_cmd(context, cid, LOGIN, 1, pid);
	context->log(LOG_D, "process_login: cid:%d pid:%d", cid, pid);
	return 1;
}

int process_logout(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid;
	struct kos_player* player;
	int namelen = 0;
	
	// SIZE(4)|HEAD(4)|CMD(4)|PID(4)
	pid = cmd->args[0]; 
	set_idle_player(pid);
	context->log(LOG_D, "process_logout: cid:%d pid:%d", cid, pid);
	return 1;
}

int process_create(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid, rid, result;
	struct kos_player* player;
	int namelen = 0;

	// SIZE(4)|HEAD(4)|CMD(4)|PID(4)
	pid = cmd->args[0]; 
	if(pid >= MAX_IDLE_PLAYER){
		context->log(LOG_W, "kos:create:pid is out of range. cid:%d pid:%d",
				cid, pid);
		return 0;
	}
	
	player = &kos.players[pid];
	if(player->rid != 0) {
		context->log(LOG_W, "kos:create:player is not idle. cid:%d pid:%d rid:%d",
				cid, pid, player->rid);
		// SIZE(4)|HEAD(4)|CMD(4)|RID(4) [-1, 0, rid]
		send_cmd(context, cid, CREATE, 1, -1);
	}
	
	rid = get_idle_room();
	if (rid == MAX_ROOM_COUNT) {
		result = 0;
		context->log(LOG_W, "process_create: room is full. cid:%d pid:%d rid:%d"
				, cid, pid, rid);
	} else {
		result = rid;
		context->log(LOG_D, "process_create: cid:%d pid:%d rid:%d", cid, pid, rid);
		player->rid = rid;
		kos.rooms[rid].rid = rid;
		kos.rooms[rid].type = 0;
		kos.rooms[rid].player_count = 0;
		kos.rooms[rid].host_pid = pid;
		kos.rooms[rid].clock = 0;
	}
	// write result.
	// SIZE(4)|HEAD(4)|CMD(4)|RID(4) [-1, 0, rid]
	send_cmd(context, cid, CREATE, 1, result);
	return result;
}

int process_join(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid, rid, result;
	struct kos_player* player, *temp;
	struct kos_room* room;
	int i;

	// SIZE(4)|HEAD(4)|CMD(4)|PID(4)|RID(4)
	pid = cmd->args[0]; 
	rid = cmd->args[1];
	if(pid >= MAX_IDLE_PLAYER){
		context->log(LOG_W, "kos:join:pid is out of range. cid:%d pid:%d",
				cid, pid);
		return 0;
	}
	if(rid >= MAX_ROOM_COUNT) {
		context->log(LOG_W, "kos:join:rid is out of range. cid:%d pid:%d rid:%d"
				, cid, pid, rid);
		return 0;
	}
	player = &kos.players[pid];
	room = &kos.rooms[rid];

	if(room->player_count >= MAX_PLAYER_COUNT) {
		context->log(LOG_W, "kos:join:room is full. cid:%d pid:%d rid:%d",
				cid, pid, rid);
		// SIZE(4)|HEAD(4)|CMD(4)|RID(4) [-1, 0, rid]
		send_cmd(context, cid, JOIN, 1, 0);
		return 0;
	}

	// add player to room.
	room->players[room->player_count] = pid;
	room->player_count++;
	player->rid = rid;

	// notify every one to kown a new player is join.
	for(i=0; i<room->player_count; i++) {
		temp = &kos.players[room->players[i]];
		// send new player's info to temp.
		send_player(context, temp->cid, JOIN, rid, player);
		// send temp's info to new player.
		if(temp->pid != player->pid) {
			send_player(context, player->cid, JOIN, rid, temp);
		}
	}
	
	return rid;
}

int process_leave(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid, rid, result;
	struct kos_player* player, *temp;
	struct kos_room* room;
	int i, pindex;
	// SIZE(4)|HEAD(4)|CMD(4)|PID(4)|RID(4)
	pid = cmd->args[0]; 
	if(pid >= MAX_IDLE_PLAYER){
		context->log(LOG_W, "kos:leave:pid is out of range. cid:%d pid:%d",
				cid, pid);
		return 0;
	}
	player = &kos.players[pid];
	rid = player->rid;
	room = &kos.rooms[rid];

	// notify every one to kown a player is leave.
	pindex = room->player_count;
	for(i=0; i<room->player_count; i++) {
		temp = &kos.players[room->players[i]];
		// send leave player's info to temp.
		send_cmd(context, temp->cid, LEAVE, 1, player->pid);
		// send temp's info to leave player.
		if(temp->pid != player->pid) 
			send_cmd(context, player->cid, LEAVE, 1, temp->pid);
		else
			pindex = i;
		
	}
	
	// remove player to room.
	for(i=pindex; i<room->player_count-1; i++) {
		room->players[i] = room->players[i+1];
	}
	room->player_count--;
	room->players[room->player_count] = 0;
	player->rid = 0;
	context->log(LOG_D, "kos:leave: cid:%d pid:%d rid:%d", cid, pid, rid);
	return pid;
}

int process_kick(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid, rid, kid, result;
	struct kos_player* player, *temp;
	struct kos_room* room;
	int i, pindex;

	// SIZE(4)|HEAD(4)|CMD(4)|PID(4)|KickID(4)
	pid = cmd->args[0]; 
	kid = cmd->args[1];
	if(pid >= MAX_IDLE_PLAYER){
		context->log(LOG_W, "kos:kick:pid is out of range. cid:%d pid:%d",
				cid, pid);
		return 0;
	}
	player = &kos.players[pid];
	rid = player->rid;
	room = &kos.rooms[rid];

	if(room->host_pid != pid) {
		context->log(LOG_W, "kos:kick:pid is not the host. cid:%d pid:%d hostid:%d",
				cid, pid, room->host_pid);
		return 0;
	}

	for(i=0; i<room->player_count; i++) {
		if (room->players[i] == kid) break;
	}

	if (i==room->player_count) {
		context->log(LOG_W, "kos:kick:kid is not in this room. kid:%d rid:%d",
				cid, rid);
		return 0;
	}

	cmd->args[0] = kid;
	process_leave(context, client , cid, cmd);
	return pid;
}

int process_message(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int frompid, topid, rid, result, msglen;
	struct kos_player* player, *toplayer;
	struct kos_room* room;
	int i, pindex;

	// SIZE(4)|HEAD(4)|CMD(4)|FROMID(4)|TOID(4)|MSGLEN(4)|MSG(x)
	frompid = cmd->args[0]; 
	topid = cmd->args[1];
	msglen = cmd->args[2];
	
	player = &kos.players[frompid];
	toplayer = &kos.players[topid];
	if (player->cid == 0 || toplayer->cid == 0) {
		context->log(LOG_W,
				"send message to a unvailable client. fromcid:%d tocid:%d", 
				player->cid, toplayer->cid);
		return 0;
	}

	context->write_data(cmd->data, cmd->msglen+4, toplayer->cid);
	return 1;
}

int process_room_msg(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int frompid, topid, rid, result, msglen;
	struct kos_player* player, *toplayer;
	struct kos_room* room;
	int i, pindex;

	// SIZE(4)|HEAD(4)|CMD(4)|FROMID(4)|ROOMID(4)|MSGLEN(4)|MSG(x)
	frompid = cmd->args[0]; 
	rid = cmd->args[1];
	msglen = cmd->args[2];

	player = &kos.players[frompid];
	room = &kos.rooms[player->rid];
	for(i=0; i<room->player_count; i++) {
		toplayer = &kos.players[room->players[i]];
		if(toplayer->cid) {
			context->write_data(cmd->data, cmd->msglen+4, toplayer->cid);	
		}
	}

	return 1;
}

int process_broad_msg(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int frompid, topid, rid, result, msglen;
	struct kos_player* player, *toplayer;
	struct kos_room* room;
	int i, pindex;

	// SIZE(4)|HEAD(4)|CMD(4)|FROMID(4)|MSGLEN(4)|MSG(x)
	frompid = cmd->args[0]; 
	msglen = cmd->args[1];

	player = &kos.players[frompid];
	for(i=1; i<kos.idle_player; i++) {
		toplayer = &kos.players[kos.idle_players[i]];
		if(toplayer->cid) {
			context->write_data(cmd->data, cmd->msglen+4, toplayer->cid);	
		}
	}

	return 1;
}

int process_game_cmd(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid, action, rid, topclock, clock;
	struct kos_player* player, *toplayer;
	struct kos_room* room;
	int i, pindex;

	// SIZE(4)|HEAD(4)|CMD(4)|PID(4)|ACTION(4)|CLOCK(4)|OTHER(x)
	pid = cmd->args[0]; 
	action = cmd->args[1];
	clock = cmd->args[2];

	player = &kos.players[pid];
	rid = player->rid;
	room = &kos.rooms[rid];
    topclock = 0;
	for(i=0; i<room->player_count; i++) {
		toplayer = &kos.players[room->players[i]];
		if(topclock< (toplayer->clock+toplayer->delay)) {
			topclock = (toplayer->clock+toplayer->delay);
		}
	}
	
	// fix clock
	cmd->args[2] = topclock;

	for(i=0; i<room->player_count; i++) {
		toplayer = &kos.players[room->players[i]];
		if(toplayer->cid) {
			context->write_data(cmd->data, cmd->msglen+4, toplayer->cid);	
		}
	}

	return 1;
}

int process_game_start(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid, action, rid, topclock, clock, allready;
	struct kos_player* player, *toplayer;
	struct kos_room* room;
	int i, pindex;

	// SIZE(4)|HEAD(4)|CMD(4)|PID(4)|ACTION(4)|CLOCK(4)|OTHER(x)
	pid = cmd->args[0]; 
	action = cmd->args[1]; // 1 host start. 2 game start.
	clock = cmd->args[2];

	player = &kos.players[pid];
	rid = player->rid;
	room = &kos.rooms[rid];

	// 1 host start, 2 client start.
	player->status = action;

    topclock = 0;
	allready = 1;
	for(i=0; i<room->player_count; i++) {
		toplayer = &kos.players[room->players[i]];
		if(topclock< (toplayer->clock+toplayer->delay)) {
			topclock = (toplayer->clock+toplayer->delay);
		}
		if(toplayer->status==0) {
			allready = 0;
		}
	}
	
	// fix clock
	cmd->args[2] = topclock;

	for(i=0; i<room->player_count; i++) {
		toplayer = &kos.players[room->players[i]];
		if(toplayer->cid && ((action ==1)||(allready == 1))) {
			context->write_data(cmd->data, cmd->msglen+4, toplayer->cid);	
		}
	}

	return 1;
}

int process_game_stop(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid, action, rid, topclock, clock, allready;
	struct kos_player* player, *toplayer;
	struct kos_room* room;
	int i, pindex;

	// SIZE(4)|HEAD(4)|CMD(4)|PID(4)|ACTION(4)|CLOCK(4)|OTHER(x)
	pid = cmd->args[0]; 
	action = cmd->args[1]; // 0 stop.
	clock = cmd->args[2];

	player = &kos.players[pid];
	rid = player->rid;
	room = &kos.rooms[rid];

	// 0 stop. 1 host start, 2 client start.
	player->status = action;

    topclock = 0;
	for(i=0; i<room->player_count; i++) {
		toplayer = &kos.players[room->players[i]];
		if(topclock< (toplayer->clock+toplayer->delay)) {
			topclock = (toplayer->clock+toplayer->delay);
		}
	}
	
	// fix clock
	cmd->args[2] = topclock;

	for(i=0; i<room->player_count; i++) {
		toplayer = &kos.players[room->players[i]];
		if(toplayer->cid) {
			context->write_data(cmd->data, cmd->msglen+4, toplayer->cid);	
		}
	}

	return 1;
}

int process_clock(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid, action, rid, topclock, clock, allready;
	struct kos_player* player, *toplayer;
	struct kos_room* room;
	int i, pindex;

	// SIZE(4)|HEAD(4)|CMD(4)|PID(4)|ACTION(4)|CLOCK(4)|OTHER(x)
	pid = cmd->args[0]; 
	clock = cmd->args[1];

	player = &kos.players[pid];
	rid = player->rid;
	room = &kos.rooms[rid];
	player->clock = clock;

	return 1;
}

int process_delay(struct fs_context* context, 
		struct fs_client* client, 
		int cid, 
		COMMAND* cmd) {
	int pid, action, rid, topclock, delay, allready;
	struct kos_player* player, *toplayer;
	struct kos_room* room;
	int i, pindex;

	// SIZE(4)|HEAD(4)|CMD(4)|PID(4)|ACTION(4)|CLOCK(4)|OTHER(x)
	pid = cmd->args[0]; 
	delay = cmd->args[1];

	player = &kos.players[pid];
	rid = player->rid;
	room = &kos.rooms[rid];
	player->delay = delay;

	context->write_data(cmd->data, cmd->msglen+4, cid);

	return 1;
}


int on_new_client(struct fs_context* context, int cid) {
	return 1;
}

int on_recv_data(struct fs_context* context, int cid) {
	struct fs_client* client;
	int msg = 0, msglen = 0, retval = 0;
	COMMAND cmd;

	client = &context->client_list->clients[cid];
	if (client->readbuff.datalen < 4) {
		return 0;
	}

	// get message total len.
	msglen = *(int*)&client->readbuff.buff[0];
	if (client->readbuff.datalen < 4+msglen) {
		return 0;
	}

	// read data into COMMAND.
	if (client->readbuff.datalen > sizeof(COMMAND)){
		context->b_free(&client->readbuff);
		return 1;
	}

	memset(&cmd, 0, sizeof(COMMAND));
	context->b_read(&client->readbuff,(char*)&cmd, client->readbuff.datalen);

	//check if this message is for kos
	if (cmd.head != HEAD_KOS) {
		return 0;
	}

	//check msg
	msg = cmd.cmd;
	switch( msg ) {
		case LOGIN:
			process_login(context, client, cid, &cmd);
			break;
		case LOGOUT:
			process_logout(context, client, cid, &cmd);
			break;
		case CREATE:
			retval = process_create(context, client, cid, &cmd);
			if(retval>0 && retval<MAX_ROOM_COUNT) {
				cmd.args[1] = retval;
				process_join(context, client, cid, &cmd);
			}
			break;
		case JOIN:
			process_join(context, client, cid, &cmd);
			break;
		case LEAVE:
			process_leave(context, client, cid, &cmd);
			break;
		case KICK:
			process_kick(context, client, cid, &cmd);
			break;
		case MESSAGE:
			process_message(context, client, cid, &cmd);
			break;
		case ROOM_MSG:
			process_room_msg(context, client, cid, &cmd);
			break;
		case BROAD_MSG:
			process_broad_msg(context, client, cid, &cmd);
			break;
		case GAME_CMD:
			process_game_cmd(context, client, cid, &cmd);
			break;
		case GAME_START:
			process_start(context, client, cid, &cmd);
			break;
		case GAME_STOP:
			process_stop(context, client, cid, &cmd);
			break;
		case DELAY_TEST:
			process_delay(context, client, cid, &cmd);
			break;
		case CLOCK:
			process_clock(context, client, cid, &cmd);
			break;
	};

	return 1;
}

int on_close_client(struct fs_context* context, int cid) {
	context->log(LOG_I, "kos: cid:%d leave.", cid);
	return 1;
}

int on_write_data(struct fs_context* context, int cid) {
	return 0;
}

int on_recv_from(struct fs_context* context, char* data, int length,
		struct sockaddr_in* fromaddr) {
	return 0;
}

int on_load(struct fs_context* context) {
	int i=0;
	memset(&kos, 0, sizeof(struct kos_context));
	for (i=0; i<MAX_ROOM_COUNT; i++) {
		kos.idle_rooms[i] = i;
	}
	kos.idle_room = 1;

	for (i=0; i<MAX_IDLE_PLAYER; i++) {
		kos.idle_players[i] = i;
	}
	kos.idle_player = 1;
	return 1;
}

int on_release(struct fs_context* context) {
	return 1;
}
