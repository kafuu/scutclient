#ifndef SCUTCLIENT_DRCOM_H
#define SCUTCLIENT_DRCOM_H

#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define htole32(value) ((uint32_t) (value))
#else
#include <endian.h>
#include <netinet/in.h>
#endif

extern uint8_t tailinfo[16];
extern uint8_t timeNotAllowed;

size_t AppendDrcomLogoffPkt(uint8_t *EthHeader, uint8_t *Packet);
size_t AppendDrcomResponseIdentity(const uint8_t *request, uint8_t *EthHeader,
		const char *UserName, uint8_t *Packet);
size_t AppendDrcomResponseMD5(const uint8_t *request, uint8_t *EthHeader,
		const char *UserName, const char *Password, uint8_t *Packet);
size_t AppendDrcomStartPkt(uint8_t *EthHeader, uint8_t *Packet);
const char* DrcomEAPErrParse(const char *str);
int Drcom_ALIVE_HEARTBEAT_TYPE_Setter(uint8_t *send_data, uint8_t *recv_data);
int Drcom_MISC_HEART_BEAT_01_TYPE_Setter(uint8_t *send_data, uint8_t *recv_data);
int Drcom_MISC_HEART_BEAT_03_TYPE_Setter(uint8_t *send_data, uint8_t *recv_data);
int Drcom_MISC_INFO_Setter(uint8_t *send_data, uint8_t *recv_data);
int Drcom_MISC_START_ALIVE_Setter(uint8_t *send_data, uint8_t *recv_data);
void encryptDrcomInfo(unsigned char *info);

#endif
