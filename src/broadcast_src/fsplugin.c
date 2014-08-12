#include "fsplugin.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>

int on_new_client(struct fs_context* context, int cid) {
	context->log(LOG_I, "fsplugin: on_new_client");
	return 1;

}

int on_recv_data(struct fs_context* context, int cid) {
	struct fs_client* client;
	context->log(LOG_I, "fsplugin: on_recv_data[cid:%d]", cid);
	client = &context->client_list->clients[cid];
	context->write_data(client->readbuff.buff, client->readbuff.datalen, cid);
	client->readbuff.datalen = 0;
	return 1;
}

int on_close_client(struct fs_context* context, int cid) {
	context->log(LOG_I, "fsplugin: on_close_client");
	return 1;
}

int on_write_data(struct fs_context* context, int cid) {
	context->log(LOG_I, "fsplugin: on_write_data[cid:%d]", cid);
	return 1;
}

int on_recv_from(struct fs_context* context, char* data, int length,
		struct sockaddr_in* fromaddr) {
	int retval;
	context->log(LOG_I, "fsplugin: on_recv_from - %s:%d - length:%d",
			(const char*)inet_ntoa(fromaddr->sin_addr),
			ntohs( fromaddr->sin_port), length );
			
	retval = context->send_to(data, length, fromaddr);
	context->log(LOG_I, "fsplugin: send_to retval: %d", retval);
	return 1;
}
