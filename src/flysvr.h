/**
 * this is a light-weight server for common C/S mode applications.
 *   Copyright (C) <2013>  <yu xiang bo>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @author: yxb
 * @email: flairyu@gmail.com
 * @copyright: all rights reserved
 */

#ifndef __FLYSVR__H__
#define __FLYSVR__H__

#define LOG_V 0
#define LOG_D 1
#define LOG_I 2
#define LOG_W 3
#define LOG_E 4

#define MAX_CLIENT_COUNT 32767
#define FS_BUFFER_BLOCK_SIZE 1024
#include <netinet/ip.h>

struct fs_buffer {
	char* buff;
	int   bufflen;
	int   datalen;
};

struct fs_client {
	int fd;
	int cid;
	struct sockaddr_in addr;
	void* user_data;
	struct fs_buffer readbuff, writebuff;
};

struct fs_client_list {
	int idle_top;
	int idle_clients[MAX_CLIENT_COUNT];
	struct fs_client clients[MAX_CLIENT_COUNT];
};

void fs_log(int level, char* fmt, ...);
void fs_buffer_init(struct fs_buffer* buff);
void fs_buffer_write(struct fs_buffer* buff, char* data, int datalen);
int  fs_buffer_read(struct fs_buffer* buff, char* data, int datalen);
void fs_buffer_free(struct fs_buffer* buff);
void fs_client_init(struct fs_client* client);
void fs_client_free(struct fs_client* client);

#endif
