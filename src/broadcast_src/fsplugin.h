#ifndef _FS_PLUGIN_H__
#define _FS_PLUGIN_H__

#include "fstype.h"

int on_new_client(struct fs_context* context, int cid);
int on_recv_data(struct fs_context* context, int cid);
int on_close_client(struct fs_context* context, int cid);
int on_write_data(struct fs_context* context, int cid);
int on_recv_from(struct fs_context* context, char* data, int length,
		struct sockaddr_in* fromaddr);
int on_load(struct fs_context* context);
int on_release(struct fs_context* context);

#endif

