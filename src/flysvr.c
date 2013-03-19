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
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int initEpoll() {
	return 0;
}

int setEvent() {
	return 0;
}

int addEvent() {
	return 0;
}

int rmvEvent() {
	return 0;
}

int recvData() {
	return 0;
}

int sendData() {
	return 0;
}

int acceptClient() {
	return 0;
}

int initServerSocket() {
	return 0;
}

int mainLoop() {
	return 0;
}

int main(int argc, char** argv) {
    printf("Hello flysvr!\n");
    return 0;
}
