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

#include "flysvr.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>

#define MAX_EVENTS 10  /*max process events count*/
#define LISTEN_BACKLOG 50 /*max listen client count one time*/
#define LISTEN_PORT 8477 /* listen port */
#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((x)>(y)?(x):(y))

static struct fs_client_list client_list = {0}; /* user list */
static int epollfd = 0;

void fs_log(int level, char* fmt, ...) {

	char level_ch[5] = {'v', 'd', 'i', 'w', 'e' };
	va_list args;
	time_t  t;
	assert(level <= LOG_E);
	va_start(args, fmt);
	t = time(&t);
	printf("[%c]",level_ch[level]);
	vprintf(fmt, args);
	printf("--%s", ctime(&t));
	va_end(args);

	if(level==LOG_E) {
		perror(fmt);
	}
}

void fs_buffer_init(struct fs_buffer* buff) {
	buff->buff = NULL ; /* (char*) malloc ( FS_BUFFER_BLOCK_SIZE ); */
	buff->bufflen = 0;
	buff->datalen = 0;
}

void fs_buffer_write(struct fs_buffer* buff, char* data, int datalen) {
	assert(data!=NULL);
	assert(buff!=NULL);
	assert(datalen>=0);

    if (buff->datalen + datalen > buff->bufflen) {
		buff->buff = (char*) realloc (buff->buff, buff->bufflen+datalen);
		buff->bufflen += datalen;
	}
	
	memcpy(buff->buff+buff->datalen, data, datalen);
	buff->datalen += datalen;
}

int  fs_buffer_read(struct fs_buffer* buff, char* data, int datalen) {
	assert(data!=NULL);
	assert(buff!=NULL);
	assert(datalen>=0);

    int readlen = min(buff->datalen, datalen);
	if (readlen >0) {
		memcpy(data, buff->buff, readlen);
		memcpy(buff->buff, buff->buff+readlen, buff->datalen-readlen);
		buff->datalen -= readlen;
	}
	return readlen;
}

void fs_buffer_free(struct fs_buffer* buff) {
	assert(buff!=NULL);
	if (buff->buff != NULL) free(buff->buff);
	buff->buff = NULL;
	buff->bufflen = 0;
	buff->datalen = 0;
}

void fs_client_init(struct fs_client* client) {
	assert(client!=NULL);
	client->fd = 0;
	client->cid = 0;
	memset(&client->addr, 0, sizeof(struct sockaddr_in));
	fs_buffer_init(&client->readbuff);
	fs_buffer_init(&client->writebuff);
}

void fs_client_free(struct fs_client* client) {
	if(client->fd!=0){
		fs_log(LOG_I, "close:%d", client->fd);
		close(client->fd);
	}
	client->fd = 0;
	client->cid = 0;
	memset(&client->addr, 0, sizeof(struct sockaddr_in));
	fs_buffer_free(&client->readbuff);
	fs_buffer_free(&client->writebuff);
}

void do_use_fd(struct fs_client* client) {
	fs_log(LOG_I, "use fd:%d", client->fd);
}

void setnonblocking(int fd) {
	int flag = 0;
	flag = fcntl(fd, F_GETFL, 0);
	if (flag == -1) {
		fs_log(LOG_E, "fcntl F_GETFL");
		return;
	}
	if( fcntl(fd, F_SETFL, flag|O_NONBLOCK) == -1 ) {
		fs_log(LOG_E, "fcntl F_SETFL: O_NONBLOCK");
		return;
	}
}

int setupListenSocket(int port) {
	int sfd, retval;
	struct sockaddr_in myaddr;

	fs_log(LOG_I, "setup listen socket...");

	/* first ,create a listening socket */
	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd == -1) {
		fs_log(LOG_E, "socket");
		exit(EXIT_FAILURE);
	}

	memset(&myaddr, 0, sizeof(struct sockaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(port);
	myaddr.sin_addr.s_addr = INADDR_ANY;

	retval = bind( sfd, (struct sockaddr*)&myaddr, sizeof(struct sockaddr));
	if (retval == -1) {
		fs_log(LOG_E, "bind");
		exit(EXIT_FAILURE);
	}

	retval = listen( sfd, LISTEN_BACKLOG);
	if (retval == -1) {
		fs_log(LOG_E, "listen");
		exit(EXIT_FAILURE);
	}
	
	fs_log(LOG_I, "done![%d]", sfd);
	return sfd;
}

int get_idle_client(struct fs_client_list* list) {
	int ci = 0;
	if (list->idle_top >= MAX_CLIENT_COUNT) return -1; /* no idle client */
	ci = list->idle_clients[list->idle_top];
	/*pop idle clients top*/
	list->idle_top++;
	return ci;
}

int set_idle_client(struct fs_client_list* list, int ci) {
	assert(ci<MAX_CLIENT_COUNT);
	assert(list!=NULL);
	struct fs_client* client = &list->clients[ci];
	fs_client_free(client);
	/* push ci into idle clients stack */
	if (list->idle_top>1) list->idle_top--;
	list->idle_clients[ list->idle_top ] = ci;
	return 0;
}

int add_client_event(int epollfd, int fd, struct sockaddr* addr) {
	struct epoll_event ev;
	int cid = 0;
	struct fs_client* client;

	setnonblocking(fd);
	cid = get_idle_client(&client_list);
	if (cid > 0) {
		ev.events =  EPOLLIN | EPOLLOUT | EPOLLET | EPOLLERR | EPOLLRDHUP;
		ev.data.u32 = cid; 
		client = &client_list.clients[cid];
		client->fd = fd;
		client->cid = cid;
		memcpy(&client->addr, addr, sizeof(struct sockaddr));
	} else {
		fs_log(LOG_W, "no idle client");
		return -1;
	}

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, 
							&ev) == -1) {
		set_idle_client(&client_list, cid);
		fs_log(LOG_E, "epoll_ctl_add");
		return -2;
	} else {
		do_use_fd(client);
		fs_log(LOG_I, "accept a new client:%s:%d", 
							(const char*)inet_ntoa(client->addr.sin_addr)
							, ntohs(client->addr.sin_port));
	} 

	return 0;
}

int del_client_event(int epollfd, struct epoll_event* ev) {
	struct fs_client* client;
	int cid = 0;
	cid = ev->data.u32;
	client = &client_list.clients[cid];
	if (epoll_ctl(epollfd, EPOLL_CTL_DEL, client->fd, ev) == -1) {
		fs_log(LOG_E, "epoll_ctl_del");
	}
	set_idle_client(&client_list, cid);
	fs_log(LOG_I, "del client.");
	return 0;
}

int process_read_event(int epollfd, struct epoll_event* ev) {
	struct fs_client* client;
	char rbuff[FS_BUFFER_BLOCK_SIZE];
	int readlen = 0;
	int cid = 0;
	cid = ev->data.u32;
	client = &client_list.clients[cid];
    for(;;) {
		readlen = read(client->fd, rbuff, FS_BUFFER_BLOCK_SIZE);
		if (readlen>0) {
			fs_buffer_write(&client->readbuff, rbuff, readlen); 
		} else if (readlen<0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
				break;
			} else {
				del_client_event(epollfd, ev);
				return -1;
			}
		} else if (readlen == 0) {
			del_client_event(epollfd, ev);
			fs_log(LOG_I, "read length is zero.");
		}
	}
	return client->readbuff.datalen;
}

int process_write_event(int epollfd, struct epoll_event* ev) {
	struct fs_client* client;
	char wbuff[FS_BUFFER_BLOCK_SIZE];
	int writelen = 0, write_total = 0;
	int cid = 0;
	cid = ev->data.u32;
	client = &client_list.clients[cid];
	for(;client->writebuff.datalen>0;) {
		writelen = min(client->writebuff.datalen, FS_BUFFER_BLOCK_SIZE);
		writelen = write(client->fd, client->writebuff.buff, writelen);
		if (writelen>0) {
			fs_buffer_read(&client->writebuff, wbuff, writelen);
			write_total += writelen;
		} else if (writelen<0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
				break;
			} else {
				del_client_event(epollfd, ev);
				return -1;
			}
		} else if (writelen == 0) {
			del_client_event(epollfd, ev);
			fs_log(LOG_I, "write length is zero.");
		}
	}
	return write_total;
}

int on_read_data(struct fs_client_list* list, int cid) {
	struct fs_client* client;
	char   buff[FS_BUFFER_BLOCK_SIZE];
	int    len;
	client = &list->clients[cid];
	for(; client->readbuff.datalen>0; ) {
		len = fs_buffer_read(&client->readbuff, buff, FS_BUFFER_BLOCK_SIZE);
		if (len>0) {
			fs_buffer_write(&client->writebuff, buff, len);
		} else {
			break;
		}
	}
	return 0;
}

int on_write_data(struct fs_client_list* list, int cid) {
    return 0;
}

int mainLoop() {
	struct epoll_event ev, events[MAX_EVENTS];
	int listen_sock, conn_sock, nfds;
	int n;
	struct sockaddr_in local;
	int addrlen;
	char address_string[32];
	int errid = 0;
	struct fs_client* client;

	/* Set up listening socket, 'listen_sock' */
	listen_sock = setupListenSocket(LISTEN_PORT);


	epollfd = epoll_create(MAX_EVENTS);
	if (epollfd == -1) {
		fs_log(LOG_E, "epoll_create");
		exit(EXIT_FAILURE);
	}

	fs_log(LOG_I, "epoll_create success!");

	/* client list 0 is listen sock */
	client_list.clients[0].fd = listen_sock;
	ev.events = EPOLLIN;
	ev.data.u32 = 0; /* get_idle_client(&client_list); */

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
		fs_log(LOG_E, "epoll_ctl: listen_sock");
		exit(EXIT_FAILURE);
	}

	for (;;) {
		fs_log(LOG_I, "wait for epoll event..");
		nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
		if (nfds == -1) {
			fs_log(LOG_E, "epoll_pwait");
			exit(EXIT_FAILURE);
		}

		for (n=0; n < nfds; n++) {
			if (events[n].data.u32 == 0) {
				conn_sock = accept(listen_sock, 
						(struct sockaddr*) &local, &addrlen);
				if (conn_sock == -1) {
					fs_log(LOG_E, "accept");
					continue;
				}
				errid =	add_client_event(epollfd, conn_sock, (struct sockaddr*)&local);
				if (errid < 0) {
					fs_log(LOG_E, "add_client_event");
					continue;
				}
			} 
			else {
				if (events[n].events & EPOLLIN) {
					errid = process_read_event(epollfd, &events[n]);
					fs_log(LOG_I, "EPOLLIN:%d", errid);
					if (errid>0) {
						on_read_data(&client_list, events[n].data.u32);
					}
				}
				
				if (events[n].events & EPOLLOUT) {
					errid = process_write_event(epollfd, &events[n]);
					fs_log(LOG_I, "EPOLLOUT:%d", errid);
					if (errid>0) {
						on_write_data(&client_list, events[n].data.u32);
					}
				} 
				
				if (events[n].events & EPOLLRDHUP) {
					fs_log(LOG_I, "EPOLLRDHUP");
					del_client_event(epollfd, &events[n]);
				} 
				
				if (events[n].events & EPOLLHUP) {
					fs_log(LOG_I, "EPOLLHUP");
					del_client_event(epollfd, &events[n]);
				} 
				
				if (events[n].events & EPOLLERR) {
					fs_log(LOG_W, "EPOLLERR");
					del_client_event(epollfd, &events[n]);
				}
			}
		}

		/* fs_log(LOG_I, "client_top:%d", client_list.idle_top); */
	}

	ev.events = EPOLLIN;
	ev.data.u32 = 0;
	del_client_event(epollfd, &ev);

	return 0;
}

void initAll() {
	int i=0;
	client_list.idle_top = 1;
	for (i=0; i<MAX_CLIENT_COUNT; i++) {
		client_list.idle_clients[i] = i;
		fs_client_init(&client_list.clients[i]);
	}
}

void releaseAll() {
	struct epoll_event ev;
	int i=0;
	client_list.idle_top = 1;
	for (i=1; i<MAX_CLIENT_COUNT; i++) {
		ev.data.u32 = i;
		if (client_list.clients[i].fd!=0) { 
			del_client_event(epollfd, &ev);
		}
	}

	/* delete listen sock */
	ev.data.u32 = 0;
	del_client_event(epollfd, &ev);
}

void sigrouter(int sig) {
	switch(sig) {
		case 1:
			fs_log(LOG_I, "SIGHUP");
			break;
		case 2:
			fs_log(LOG_I, "SIGINT");
			break;
		case 3:
			fs_log(LOG_I, "SIGQUIT");
			break;
	}
	releaseAll();
	exit(0);
	return;
}

int main(int argc, char** argv) {
	int num = 105;
	signal(SIGHUP, sigrouter);
	signal(SIGINT, sigrouter);
	signal(SIGQUIT, sigrouter);

	initAll();
	mainLoop();
	releaseAll();
	
	return 0;
}
