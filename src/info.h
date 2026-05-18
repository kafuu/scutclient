#ifndef __INFO_H__
#define __INFO_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define IFNAMSIZ 256
#define SCUT_ETH_FRAME_LEN 1514
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <getopt.h>
#define SCUT_ETH_FRAME_LEN ETH_FRAME_LEN
#endif

extern struct in_addr udpserver_ipaddr;
extern struct in_addr dns_ipaddr;
extern char *UserName;
extern char *Password;
extern char *OnlineHookCmd;
extern char *OfflineHookCmd;
extern char DeviceName[IFNAMSIZ];
extern char HostName[32];
extern char *Hash;
extern unsigned char Version[64];
extern int Version_len;

int hexStrToByte(const char* source, unsigned char* dest, int bufLen);
#endif
