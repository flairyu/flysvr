#include "fsplugin.h"
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
