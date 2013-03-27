#ifndef _FS_TYPE_H__
#define _FS_TYPE_H__

#define LOG_V 0
#define LOG_D 1
#define LOG_I 2
#define LOG_W 3
#define LOG_E 4

#define MAX_CLIENT_COUNT 32767
#define FS_BUFFER_BLOCK_SIZE 1024
#define MAX_EVENT_LISTENER_COUNT 256

#include <netinet/ip.h>

struct fs_context;

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
	int idle_clients[MAX_CLIENT_COUNT+1]; /* last for udp */
	struct fs_client clients[MAX_CLIENT_COUNT+1];
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

typedef int (*FS_SEND_TO) (char* buffer, int buffer_len, 
		struct sockaddr_in* toaddr);
typedef int (*FS_WRITE_DATA)(char* buffer, int buffer_len, int cid);
typedef int (*FS_CLOSE_CLIENT)(int cid);
typedef char* (*FS_GET_VERSION)();

struct fs_event_listener {
	void* dp;
	FS_ON_NEW_CLIENT on_new_client;
	FS_ON_RECV_DATA on_recv_data;
	FS_ON_CLOSE_CLIENT on_close_client;
	FS_ON_WRITE_DATA on_write_data;
	FS_ON_RECV_FROM on_recv_from; /*used for udp*/
};

struct fs_context {
	int max_client_count;
	int fs_buffer_block_size;
	struct sockaddr_in localaddr;
	struct fs_client_list* client_list;
	struct fs_client* udp;
	char plugin_dir[255];
	int use_udp; /*is use udp server*/

	int event_listeners_count;
	struct fs_event_listener event_listeners[MAX_EVENT_LISTENER_COUNT];

	FS_LOG log;
	FS_BUFFER_INIT b_init;
	FS_BUFFER_WRITE b_write;
	FS_BUFFER_READ b_read;
	FS_BUFFER_FREE b_free;

	FS_SEND_TO send_to; /*use for udp*/
	FS_WRITE_DATA write_data;
	FS_CLOSE_CLIENT close_client;
	FS_GET_VERSION get_version;
};



#endif
