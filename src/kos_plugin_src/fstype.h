/* fstype.h
 * this file is type defines for flyserver. flyserver is a fast, small  
 * framework for tcp&udp server. it use epoll for i/o process.
 *
 * @author: yxb (flairyu)
 * @email: flairyu(at)gmail.com
 * @date:  2013-03-28
 *
 * All right reserved.
 */
#ifndef _FS_TYPE_H__
#define _FS_TYPE_H__
#include <syslog.h>
#include <netinet/ip.h>

/* debug log levels */
#define LOG_D LOG_DEBUG
#define LOG_I LOG_INFO
#define LOG_W LOG_WARNING
#define LOG_E LOG_ERR

/* max client count to process */
#define MAX_CLIENT_COUNT 32767

/* fly server send/recv buffer size */
#define FS_BUFFER_BLOCK_SIZE 1024

/* max count of plugins */ 
#define MAX_EVENT_LISTENER_COUNT 256

struct fs_context;

/* a buffer for read, write. there are operators
 * like: fs_buffer_init, fs_buffer_read, fs_buffer_write
 * fs_buffer_free used for this buffer
 */
struct fs_buffer {
	char* buff; // buffer header
	int   bufflen; //buffer size
	int   datalen; //data size in buffer (from 0)
};

/* 
 * fly server's client info
 */
struct fs_client {
	int fd;       //socket
	int cid;      //client index in fs_context.client_list.clients.
	struct sockaddr_in addr; // client ip address
	void* user_data; //user define data. NULL if nouse.
	struct fs_buffer readbuff, writebuff; 
	// readbuff will be fill in 'read event', 
	// writebuff will be used in 'write event'.
};

struct fs_client_list {
	int idle_top; //fast get a idle client.
	int idle_clients[MAX_CLIENT_COUNT+1]; /* last item for udp */
	struct fs_client clients[MAX_CLIENT_COUNT+1];//clients list.
};

typedef void (*FS_LOG)(int level, char* fmt, ...);
typedef void (*FS_BUFFER_INIT)(struct fs_buffer* buff);
typedef void (*FS_BUFFER_WRITE)(struct fs_buffer* buff, char* data, int datalen);
typedef int  (*FS_BUFFER_READ)(struct fs_buffer* buff, char* data, int datalen);
typedef void (*FS_BUFFER_FREE)(struct fs_buffer* buff);

typedef int (*FS_ON_NEW_CLIENT)(struct fs_context* context, int cid);
typedef int (*FS_ON_RECV_DATA)(struct fs_context* context, int cid);
typedef int (*FS_ON_CLOSE_CLIENT)(struct fs_context* context, int cid);
typedef int (*FS_ON_WRITE_DATA)(struct fs_context* context, int cid);
typedef int (*FS_ON_RECV_FROM)(struct fs_context* context, char* data, int length,
		struct sockaddr_in* fromaddr);
typedef int (*FS_ON_LOAD)(struct fs_context* context);
typedef int (*FS_ON_RELEASE)(struct fs_context* context);

typedef int (*FS_SEND_TO) (char* buffer, int buffer_len, 
		struct sockaddr_in* toaddr);
typedef int (*FS_WRITE_DATA)(char* buffer, int buffer_len, int cid);
typedef int (*FS_CLOSE_CLIENT)(int cid);
typedef char* (*FS_GET_VERSION)();

/* fs_event_listener is for plugin's interface.
 * each plugin must implements these functions.
 */
struct fs_event_listener {
	void* dp;
	/* this event will be invoke while a new client come.
	 * the client info is stores in client_list[cid].
	 */
	FS_ON_NEW_CLIENT on_new_client;
	
	/* this event will be invoke while a client send some 
	 * data to flysvr. the data will store in client_list[cid].readbuff.
	 */
	FS_ON_RECV_DATA on_recv_data;

	/* this event will be invoke while a client is closed.
	 */
	FS_ON_CLOSE_CLIENT on_close_client;

	/* this event will be invoke while some data has send to a client
	 */
	FS_ON_WRITE_DATA on_write_data;

	/* this event will be invoke while received some data
	 * from a udp client.
	 */
	FS_ON_RECV_FROM on_recv_from; /*used for udp*/

	/**
	 * call this onloaded
	 */
	FS_ON_LOAD on_load;

	/**
	 * call this on released
	 */
	FS_ON_RELEASE on_release;
};

/* fs_context is a global context info list.
 * it has all needed by flysvr, and some of them
 * is readonly, DO NOT modify them.
 */
struct fs_context {
	int max_client_count; //read-only.indicate the max size of clients list.
	int fs_buffer_block_size; //read-only
	struct sockaddr_in localaddr;//read-only
	struct fs_client_list* client_list;//read-only
	struct fs_client* udp;//read-only
	char plugin_dir[255]; //read-only
	int use_udp; /*is use udp server*/
	int daemon_mode; /* is in daemon mode */
	
	int event_listeners_count; //read-only
	struct fs_event_listener event_listeners[MAX_EVENT_LISTENER_COUNT];

	FS_LOG log; // use for log debug infos.
	FS_BUFFER_INIT b_init; //use for init fs_buffer.
	FS_BUFFER_WRITE b_write; //use for write some data into a fs_buffer.
	FS_BUFFER_READ b_read; //use for read some data from a fs_buffer
	FS_BUFFER_FREE b_free; //use for release memories of a fs_buffer

	FS_SEND_TO send_to; /*use for udp*/
	FS_WRITE_DATA write_data; //use for send data via a client's fd.
	FS_CLOSE_CLIENT close_client; //use for close a client.
	FS_GET_VERSION get_version; //use for get flysvr's version.
};



#endif
