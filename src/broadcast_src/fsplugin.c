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


static struct sockaddr_in addrs[10] = {0};
static int addrs_count = 0;
#define MIN(x,y) (((x)<(y))?(x):(y))

int on_recv_from(struct fs_context* context, char* data, int length,
		struct sockaddr_in* fromaddr) {
	int retval,i,found;
	context->log(LOG_I, "fsplugin: on_recv_from - %s:%d - length:%d",
			(const char*)inet_ntoa(fromaddr->sin_addr),
			ntohs( fromaddr->sin_port), length );
	
	found = 0;
	for (i=0; i<MIN(addrs_count,10); i++) {
		if (fromaddr->sin_addr.s_addr==addrs[i].sin_addr.s_addr &&
				fromaddr->sin_port==addrs[i].sin_port) {
			found = 1;
		} else {
			retval = context->send_to(data, length, fromaddr);
			context->log(LOG_I, "fsplugin: send_to retval: %d", retval);
		}
	}

	if (found == 0) {
		addrs[addrs_count%10] = *fromaddr;
		addrs_count++;
	}

	return 1;
}
