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
#include <dlfcn.h>
#include <dirent.h>
#include <sys/resource.h>
#include <syslog.h>

#define MAX_EVENTS 50  /*max process events count*/
#define LISTEN_BACKLOG 50 /*max listen client count one time*/
#define LISTEN_PORT 8477 /* listen port */
#define USE_UDP 1 /*use udp is ture*/

#define min(x,y) ((x)<(y)?(x):(y))
#define max(x,y) ((x)>(y)?(x):(y))

static struct fs_client_list client_list = {0}; /* user list */
static int epollfd = 0; //epoll fd for epoll_wait..
static struct fs_context g_context = {0}; // global context
static char VERSION[] = "1.0.20130330"; // version
static char PLUGIN_DIR[] = "/etc/fsplugins/"; // plugin's directory
static int loglevel = 10; // loglevel

/**
 * write log info into stdout or use syslog save into logfile.
 * if level >= loglevel, the log will be ignored.
 */
void fs_log(int level, char* fmt, ...) {

	va_list args;
	time_t  t;

	if(level>=loglevel) return;

	va_start(args, fmt);
	if (g_context.daemon_mode) {
		vsyslog(level, fmt, args);
	} else {
		t = time(&t);
		printf("[%d]",level);
		vprintf(fmt, args);
		printf("--%s", ctime(&t));
		if(level==LOG_E) {
			perror(fmt);
		}
	}
	va_end(args);

}

/*
 * init a fs_buffer. set fs_buffer's member into 0.
 */
void fs_buffer_init(struct fs_buffer* buff) {
	buff->buff = NULL ; /* (char*) malloc ( FS_BUFFER_BLOCK_SIZE ); */
	buff->bufflen = 0;
	buff->datalen = 0;
}

/*
 * write some data into fs_buffer, the buff's buff will be realloc
 * if the buff.bufflen is less than datalen.
 * after write, the buffer's datalen will be plus data's datalen.
 */
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

/*
 * read some data from buffer into data.
 * return min(buff->datalen, datalen);
 */
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

/*
 * free a fs_buffer
 */
void fs_buffer_free(struct fs_buffer* buff) {
	assert(buff!=NULL);
	if (buff->buff != NULL) free(buff->buff);
	buff->buff = NULL;
	buff->bufflen = 0;
	buff->datalen = 0;
}

/*
 * init a client instance.
 * this client's fd,cid will be set to 0.
 * and read,writebuffer will be call bs_buffer_init on them.
 */
void fs_client_init(struct fs_client* client) {
	assert(client!=NULL);
	client->fd = 0;
	client->cid = 0;
	memset(&client->addr, 0, sizeof(struct sockaddr_in));
	fs_buffer_init(&client->readbuff);
	fs_buffer_init(&client->writebuff);
}

/*
 * free a client
 */
void fs_client_free(struct fs_client* client) {
	if(client->fd!=0){
		fs_log(LOG_I, "close cid:%d fd:%d", client->cid, client->fd);
		close(client->fd);
	}
	client->fd = 0;
	client->cid = 0;
	memset(&client->addr, 0, sizeof(struct sockaddr_in));
	fs_buffer_free(&client->readbuff);
	fs_buffer_free(&client->writebuff);
}

/*
 * set a file descriptor into O_NONBLOCK flag.
 */
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

/*
 * setup a socket that bind, listen, and ready for accept client.
 * if some error happens, it will be exit().
 */
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

/*
 * setup a udp socket for recvfrom() some client use udp.
 */
int setupUdpSocket(int port) {
	int sfd, retval;
	struct sockaddr_in myaddr;

	fs_log(LOG_I, "setup udp socket...");

	/* first ,create a listening socket */
	sfd = socket(AF_INET, SOCK_DGRAM, 0);
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

	fs_log(LOG_I, "udp done![%d]", sfd);
	return sfd;
}

/*
 * get a idle client from a 'idle client stack'.
 */
int get_idle_client(struct fs_client_list* list) {
	int ci = 0;
	if (list->idle_top >= MAX_CLIENT_COUNT) return -1; /* no idle client */
	ci = list->idle_clients[list->idle_top];
	/*pop idle clients top*/
	list->idle_top++;
	return ci;
}

/*
 * push a idle client into 'idle_client_stack'.
 */
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

/*
 * add client with fd, addr, and add a epoll event on epollfd.
 */
int add_client_event(int epollfd, int fd, struct sockaddr* addr) {
	struct epoll_event ev;
	int cid = 0;
	struct fs_client* client;
	int i;

	// use non blocking socket.
	setnonblocking(fd);

	// get a idle client instance.
	cid = get_idle_client(&client_list);
	if (cid > 0) {

		// set up epoll event.
		ev.events =  EPOLLIN | EPOLLOUT | EPOLLET | EPOLLERR | EPOLLRDHUP;
		ev.data.u32 = cid; // save cid.
		client = &client_list.clients[cid];
		client->fd = fd;
		client->cid = cid;
		memcpy(&client->addr, addr, sizeof(struct sockaddr));
	} else {
		fs_log(LOG_W, "no idle client");
		return -1;
	}

	// add epoll event.
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, 
							&ev) == -1) {
		set_idle_client(&client_list, cid);
		fs_log(LOG_E, "epoll_ctl_add");
		return -2;
	} else {
		fs_log(LOG_I, "accept a new client:cid:%d ip:%s port:%d"
							, cid
							, (const char*)inet_ntoa(client->addr.sin_addr)
							, ntohs(client->addr.sin_port));
	} 

	return cid;
}

/*
 * remove client event from epollfd.
 */
int del_client_event(int epollfd, struct epoll_event* ev) {
	struct fs_client* client;
	int i, cid = 0;
	cid = ev->data.u32;
	if(cid<MAX_CLIENT_COUNT) {
		/*call event listener to process close client event*/
		for (i=0; i<g_context.event_listeners_count; i++) {
			if ( g_context.event_listeners[i].on_close_client ) {
				g_context.event_listeners[i].on_close_client(&g_context, cid);
			}
		}
	}

	client = &client_list.clients[cid];
	if (epoll_ctl(epollfd, EPOLL_CTL_DEL, client->fd, ev) == -1) {
		fs_log(LOG_E, "epoll_ctl_del");
	}

	// set this client as idle client. and push it back into idle list.
	if(cid<MAX_CLIENT_COUNT) set_idle_client(&client_list, cid);
	fs_log(LOG_I, "del client cid:%d", cid);
	return 0;
}

/*
 * process read event. it will call read until read EAGAIN or EWOULDBLOCK
 * and restore the data into client's readbuff.
 */
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
			fs_log(LOG_W, "read length is zero.");
			return -2;
		}
	}
	return client->readbuff.datalen;
}

/*
 * process write event. if the client that identified by ev.data.u32 
 * it's writebuff.datalen>0, then call write until EAGAIN .
 */
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
			fs_log(LOG_W, "write length is zero.");
			return -2;
		}
	}
	return write_total;
}

/*
 * if there are read event happens, call all plugins interface to handler this.
 */
int on_read_data(struct fs_client_list* list, int cid) {
	struct fs_client* client;
	int i;
	/*call event listener to process read event*/
	for (i=0; i<g_context.event_listeners_count; i++) {
		if ( g_context.event_listeners[i].on_recv_data ) {
			g_context.event_listeners[i].on_recv_data(&g_context, cid);
		}
	}

	/*clear readbuff*/
	//client = &g_context.client_list->clients[cid];
	//client->readbuff.datalen = 0;
	return 0;
}

/*
 * if there are write event happens, call all plugins to handle them.
 */
int on_write_data(struct fs_client_list* list, int cid) {
	int i;
	/*call event listener to process write event*/
	for (i=0; i<g_context.event_listeners_count; i++) {
		if ( g_context.event_listeners[i].on_write_data ) {
			g_context.event_listeners[i].on_write_data(&g_context, cid);
		}
	}
    return 0;
}

/*
 * mainloop for epoll cicle.
 */
int mainLoop() {
	struct epoll_event ev, events[MAX_EVENTS];
	int listen_sock, conn_sock, nfds;
	int n,i;
	struct sockaddr_in local;
	int addrlen;
	char address_string[32];
	int errid = 0;
	struct fs_client* client;
	char buff[FS_BUFFER_BLOCK_SIZE];
	int recvlen;

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

	/* setup udp */
	if (g_context.use_udp) {
		g_context.udp->fd = setupUdpSocket(LISTEN_PORT);
		g_context.udp->cid = MAX_CLIENT_COUNT;
		ev.events = EPOLLIN;
		ev.data.u32 = MAX_CLIENT_COUNT;
		if (epoll_ctl(epollfd, EPOLL_CTL_ADD, g_context.udp->fd, &ev) == -1) {
			fs_log(LOG_E, "epoll_ctl: udp_sock");
			exit(EXIT_FAILURE);
		}
	}

	/* main cicle */
	for (;;) {
		fs_log(LOG_D, "wait for epoll event..");
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
				errid =	add_client_event(epollfd, conn_sock, 
						(struct sockaddr*)&local);
				if (errid < 0) {
					fs_log(LOG_E, "add_client_event");
					continue;
				}

				/*call event listener to process new client event*/
				for (i=0; i<g_context.event_listeners_count; i++) {
					if ( g_context.event_listeners[i].on_new_client ) {
						g_context.event_listeners[i].on_new_client(&g_context, 
								errid);
					}
				}
			} else if( g_context.use_udp &&
					events[n].data.u32 == MAX_CLIENT_COUNT ) {
				/*process udp events */
				errid = sizeof(struct sockaddr);
				recvlen = recvfrom( g_context.udp->fd, buff, FS_BUFFER_BLOCK_SIZE
						, 0 , (struct sockaddr*)&local,  &errid);
				if(recvlen>0) {
					/*call event listener to process udp recv event*/
					for (i=0; i<g_context.event_listeners_count; i++) {
						if ( g_context.event_listeners[i].on_recv_from ) {
							g_context.event_listeners[i].on_recv_from(
										&g_context, buff, recvlen,
										(struct sockaddr_in*)&local);
						}
					}
				}
			} else {
				if (events[n].events & EPOLLRDHUP) {
					fs_log(LOG_D, "EPOLLRDHUP");
					del_client_event(epollfd, &events[n]);
					continue;
				} 
				
				if (events[n].events & EPOLLHUP) {
					fs_log(LOG_D, "EPOLLHUP");
					del_client_event(epollfd, &events[n]);
					continue;
				} 
				
				if (events[n].events & EPOLLERR) {
					fs_log(LOG_W, "EPOLLERR");
					del_client_event(epollfd, &events[n]);
					continue;
				}

				if (events[n].events & EPOLLIN) {
					errid = process_read_event(epollfd, &events[n]);
					fs_log(LOG_D, "EPOLLIN:%d", errid);
					if (errid>0) {
						on_read_data(&client_list, events[n].data.u32);
					} else if (errid<0) {
						continue;
					}
				}
				
				if (events[n].events & EPOLLOUT) {
					errid = process_write_event(epollfd, &events[n]);
					fs_log(LOG_D, "EPOLLOUT:%d", errid);
					if (errid>0) {
						on_write_data(&client_list, events[n].data.u32);
					} else if (errid<0) {
						continue;
					}
				} 
				
			}
		}

		/* fs_log(LOG_I, "client_top:%d", client_list.idle_top); */
	}

	return 0;
}

/*
 * send some data to 'toaddr' via udp socket.
 */
int send_to(char* buffer, int buffer_len, struct sockaddr_in* toaddr) {
	char buff[FS_BUFFER_BLOCK_SIZE];
	int retval = 0;
	if(g_context.use_udp == 0) return 0;
	if(g_context.udp->fd == 0) return 0;
	retval = sendto(g_context.udp->fd, buffer, buffer_len, 0, 
			(struct sockaddr*) toaddr, sizeof(struct sockaddr_in));
	return retval;
}

/*
 * write data via fd of client(index by cid)
 */
int write_data(char* buffer, int buffer_len, int cid) {
	struct fs_client* client;
	struct epoll_event ev;
	assert(cid>=0 && cid<MAX_CLIENT_COUNT);
	client = &client_list.clients[cid];
	assert(client->fd != 0);
	assert(client->cid == cid);
	fs_buffer_write(&client->writebuff, buffer, buffer_len);
	ev.data.u32 = cid;
	return process_write_event(epollfd, &ev);
}

/*
 * close a client
 */
int close_client(int cid) {
	struct epoll_event ev;
	ev.data.u32 = cid;
	return del_client_event(epollfd, &ev);
}

/*
 * return flysvr's version
 */
char* get_version() {
	return VERSION;
}

/*
 * load a plugin shared object from plugin's directory.
 * every file name with .so will be loaded.
 */
void loadPlugin(char* filepath) {

	struct fs_event_listener *listener;
	void* dp;
	char* errstr;

	if (g_context.event_listeners_count >= MAX_EVENT_LISTENER_COUNT) {
		fs_log(LOG_W, "event listener is too more.");
		return ;
	}

	fs_log(LOG_I, "load:%s", filepath);

	dp = dlopen(filepath, RTLD_LAZY);
	if (dp==NULL) {
		fs_log(LOG_E, dlerror());
		exit(EXIT_FAILURE);
	}

	listener = &g_context.event_listeners[ g_context.event_listeners_count ];

	dlerror();
	listener->on_new_client = dlsym(dp, "on_new_client");
	errstr = dlerror();
	if(errstr) {
		fs_log(LOG_E, errstr);
		exit(EXIT_FAILURE);
	}


	dlerror();
	listener->on_recv_data= dlsym(dp, "on_recv_data");
	errstr = dlerror();
	if(errstr) {
		fs_log(LOG_E, errstr);
		exit(EXIT_FAILURE);
	}
	
	dlerror();
	listener->on_close_client = dlsym(dp, "on_close_client");
	errstr = dlerror();
	if(errstr) {
		fs_log(LOG_E, errstr);
		exit(EXIT_FAILURE);
	}

	dlerror();
	listener->on_write_data = dlsym(dp, "on_write_data");
	errstr = dlerror();
	if(errstr) {
		fs_log(LOG_E, errstr);
		exit(EXIT_FAILURE);
	}

	dlerror();
	listener->on_recv_from = dlsym(dp, "on_recv_from");
	errstr = dlerror();
	if(errstr) {
		fs_log(LOG_E, errstr);
		exit(EXIT_FAILURE);
	}

	dlerror();
	listener->on_load = dlsym(dp, "on_load");
	errstr = dlerror();
	if(errstr) {
		fs_log(LOG_E, errstr);
		exit(EXIT_FAILURE);
	}

	dlerror();
	listener->on_release = dlsym(dp, "on_release");
	errstr = dlerror();
	if(errstr) {
		fs_log(LOG_E, errstr);
		exit(EXIT_FAILURE);
	}

	//dlclose(dp);
	listener->dp = dp;
	g_context.event_listeners_count++;

	listener->on_load(&g_context);
}

/*
 * load all plugins.
 */
void loadAllPlugins() {
	char infile[255];
	struct dirent *ptr = NULL;
	DIR* dir;
	dir = opendir(g_context.plugin_dir);
	if (dir == NULL) {
		fs_log(LOG_E, "opendir");
		exit(EXIT_FAILURE);
	}
	while((ptr = readdir(dir)) != NULL) {
		if (ptr->d_name[0] != '.') {
			memset(infile, 0, 255);
			sprintf(infile, "%s%s", g_context.plugin_dir, ptr->d_name);
			loadPlugin(infile);
		}
	}
	fs_log(LOG_I, "load all plugins done.");
}

/*
 * init all static variables.
 */
void initAll() {
	int i=0;
	
	client_list.idle_top = 1;
	for (i=0; i<MAX_CLIENT_COUNT; i++) {
		client_list.idle_clients[i] = i;
		fs_client_init(&client_list.clients[i]);
	}
	
	g_context.max_client_count = MAX_CLIENT_COUNT;
	g_context.fs_buffer_block_size = FS_BUFFER_BLOCK_SIZE;
	
	// g_context.localaddr = ;
	strcpy(g_context.plugin_dir, PLUGIN_DIR);
	g_context.client_list = &client_list;

	g_context.use_udp = USE_UDP; /*is use udp?*/
	g_context.udp = &client_list.clients[MAX_CLIENT_COUNT]; /*last for udp*/
	fs_client_init(g_context.udp); /* init udp instance */

	g_context.log = fs_log;
	g_context.b_init = fs_buffer_init;
	g_context.b_read = fs_buffer_read;
	g_context.b_write = fs_buffer_write;
	g_context.b_free = fs_buffer_free;

	memset(g_context.event_listeners, 0, sizeof(g_context.event_listeners));
	g_context.event_listeners_count = 0;

	g_context.send_to = send_to;
	g_context.write_data = write_data;
	g_context.close_client = close_client;
	g_context.get_version = get_version;
	
	loadAllPlugins();
}

/*
 * close all client's fd
 * release memory.
 * close log fd.
 */
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

	/* delete udp sock */
	if (g_context.use_udp) {
		ev.data.u32 = MAX_CLIENT_COUNT;
		del_client_event(epollfd, &ev);
	}

	/* close plugin libs */
	for (i=0; i<g_context.event_listeners_count; i++) {
		g_context.event_listeners[i].on_release(&g_context);
		dlclose( g_context.event_listeners[i].dp );
	}
	
	g_context.event_listeners_count = 0;
	closelog();
}

/*
 * process signals
 */
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

/*
 * make programe into daemon
 */
void daemonize(const char* cmd) {
	int i, fd0, fd1, fd2;
	pid_t pid;
	struct rlimit rl;
	struct sigaction sa;

	/*
	 * Clear file creation mask
	 */
	umask(0);

	/*
	 * Get maximum number of file descriptors
	 */
	if ( getrlimit(RLIMIT_NOFILE, &rl)<0 ) {
		perror("getrlimit");
	}

	/*
	 * Become a session leader to lose controlling TTY.
	 */
	if ( (pid=fork())<0 ) {
		perror("fork");
		exit(EXIT_FAILURE);
	} else if (pid > 0) {
		// parent
		exit(0);
	}
	setsid();


	/*
	 * ignore some signals
	 */
	signal(SIGHUP, SIG_IGN); 
	
	/*
	 * Ensure future opens won't allocate controlling TTY.
	 */
	if ( (pid=fork())<0 ) {
		perror("fork2");
		exit(EXIT_FAILURE); 
	} else if (pid>0) {
		//parent
		exit(0);
	}

	/*
	 * change the current working directory to the root so we
	 * won't prevent file system from being unmounted.
	 */
	if ( chdir("/") < 0) {
		perror("chdir to root");
	}

	/*
	 * Close all open file descriptors
	 */
	if (rl.rlim_max == RLIM_INFINITY) rl.rlim_max = 1024;
	for(i=0; i<rl.rlim_max; i++) {
		close(i);
	}

	/*
	 * Attach file descriptors 0, 1, 2 to /dev/null. (why?)
	 */
	fd0 = open("/dev/null", O_RDWR);
	fd1 = dup(0);
	fd2 = dup(0);

	/*
	 * initial the log file
	 */
	openlog(cmd, LOG_CONS, LOG_DAEMON);
	if (fd0!=0 || fd1!=1 || fd2!=2) {
		syslog(LOG_ERR, "unexpected file descritors %d, %d, %d", fd0, fd1, fd2);
		exit(EXIT_FAILURE);
	}
    syslog(LOG_DEBUG, "deamon ok.");
}

/* flysvr by yxb */
int main(int argc, char** argv) {

	printf("fly server %s\n", get_version());
	printf("written by YXB (flairyu), 2013.\n");
	printf("visit http://www.ruanmianbao.com for more info.\n");
	printf("have fun! ^_^\n");

	//signal(SIGHUP, sigrouter);
	//signal(SIGINT, sigrouter);
	//signal(SIGQUIT, sigrouter);
	signal(SIGTERM, sigrouter);

	// enter deamon mode
	if(argc<=2) {
		daemonize("flysvr");
		g_context.daemon_mode = 1;
	} else {
		g_context.daemon_mode = 0;
	}

	if(argc>=2) {
		loglevel = atoi(argv[1]);
	} else {
		loglevel = LOG_D;
	}

	// init server
	initAll();

	// enter main loop
	mainLoop();

	// release all.
	releaseAll();

	return 0;
}
