#include "fsplugin.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>

#define DATA_LIMIT_LEN 1048576 //1024*1024 Bytes, limit to 1MB data.
int on_load(struct fs_context* context) {
	return 0;
}

int on_release(struct fs_context* context) {
	return 0;
}

int on_new_client(struct fs_context* context, int cid) {
	return 0;
}

int on_recv_data(struct fs_context* context, int cid) {
	struct fs_client* client;
	client = &context->client_list->clients[cid];
	if(client->readbuff.datalen > DATA_LIMIT_LEN) {
		context->log(LOG_W, "cleanup: cid:%d fid:%d datalen:%d addr:%s:%d", client->cid, client->fd,
				client->readbuff.datalen, (const char*)inet_ntoa(client->addr.sin_addr),
				ntohs(client->addr.sin_port));
		context->b_free(&client->readbuff);
		return 1;
	}
	return 0;
}

int on_close_client(struct fs_context* context, int cid) {
	return 0;
}

int on_write_data(struct fs_context* context, int cid) {
	return 0;
}

int on_recv_from(struct fs_context* context, char* data, int length,
		struct sockaddr_in* fromaddr) {
	return 0;
}
