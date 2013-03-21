#ifndef _FS_PLUGIN_H__
#define _FS_PLUGIN_H__

#include "fstype.h"

int on_new_client(struct fs_context* context, int cid);
int on_recv_data(struct fs_context* context, int cid);
int on_close_client(struct fs_context* context, int cid);
int on_write_data(struct fs_context* context, int cid);

#endif

