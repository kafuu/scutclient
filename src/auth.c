#include "auth.h"
#include "tracelog.h"
#include "info.h"
#include <ctype.h>

struct in_addr local_ipaddr;
uint8_t MAC[6];

#define DRCOM_UDP_HEARTBEAT_DELAY 12
#define DRCOM_UDP_HEARTBEAT_TIMEOUT 2
#define AUTH_8021X_LOGOFF_DELAY_MS 500
#define AUTH_8021X_RECV_DELAY_MS 1000
#define AUTH_8021X_PCAP_TIMEOUT_MS 100
#define AUTH_8021X_RECV_TIMES 3

const static uint8_t BroadcastAddr[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
const static uint8_t MultcastAddr[6] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x03 };
const static uint8_t UnicastAddr[6] = { 0x01, 0xd0, 0xf8, 0x00, 0x00, 0x03 };

static uint8_t send_8021x_data[1024];
static size_t send_8021x_data_len = 0;
static uint8_t send_udp_data[ETH_FRAME_LEN];
static uint8_t recv_udp_data[ETH_FRAME_LEN];
static int send_udp_data_len = 0;
static int resev = 0;
static int times = AUTH_8021X_RECV_TIMES;
static int success_8021x = 0;
static int isNeedHeartBeat = 0;
static uint8_t EthHeader[14] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x8e };
static uint8_t BroadcastHeader[14] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x88, 0x8e };
static uint8_t MultcastHeader[14] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x80, 0xc2, 0x00, 0x00, 0x03, 0x88, 0x8e };
static uint8_t UnicastHeader[14] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x01, 0xd0, 0xf8, 0x00, 0x00, 0x03, 0x88, 0x8e };
static time_t BaseHeartbeatTime = 0;
static uint8_t lastHBDone = 1;

static pcap_t *auth_8021x_handle = NULL;
static SOCKET auth_udp_sock = INVALID_SOCKET;
static struct sockaddr_in serv_addr, local_addr;

typedef enum {
	REQUEST = 1, RESPONSE = 2, SUCCESS = 3, FAILURE = 4, H3CDATA = 10
} EAP_Code;
typedef enum {
	IDENTITY = 1,
	NOTIFICATION = 2,
	MD5 = 4,
	AVAILABLE = 20,
	ALLOCATED_0x07 = 7,
	ALLOCATED_0x08 = 8
} EAP_Type;
typedef uint8_t EAP_ID;

typedef struct {
	char adapter_name[IFNAMSIZ];
	char pcap_name[512];
	char friendly_name[IFNAMSIZ];
	char description[512];
	ULONG if_index;
	struct in_addr ipv4;
	uint8_t mac[6];
} WinAdapterInfo;

static WinAdapterInfo selected_adapter;
static int selected_adapter_valid = 0;

static void close_8021x_handle(void) {
	if (auth_8021x_handle) {
		pcap_close(auth_8021x_handle);
		auth_8021x_handle = NULL;
	}
}

static void close_udp_socket(void) {
	if (auth_udp_sock != INVALID_SOCKET) {
		closesocket(auth_udp_sock);
		auth_udp_sock = INVALID_SOCKET;
	}
}

static void sleep_seconds(unsigned int seconds) {
	Sleep(seconds * 1000U);
}

static void copy_text(char *dest, size_t dest_len, const char *src) {
	if (!dest || dest_len == 0) {
		return;
	}
	if (!src) {
		dest[0] = '\0';
		return;
	}
	strncpy(dest, src, dest_len - 1);
	dest[dest_len - 1] = '\0';
}

static void wide_to_utf8(const WCHAR *src, char *dest, size_t dest_len) {
	int written;
	if (!dest || dest_len == 0) {
		return;
	}
	dest[0] = '\0';
	if (!src) {
		return;
	}
	written = WideCharToMultiByte(CP_UTF8, 0, src, -1, dest, (int) dest_len, NULL, NULL);
	if (written <= 0) {
		dest[0] = '\0';
	} else {
		dest[dest_len - 1] = '\0';
	}
}

static int contains_case_insensitive(const char *haystack, const char *needle) {
	size_t needle_len;
	size_t i, j;

	if (!needle || needle[0] == '\0') {
		return 1;
	}
	if (!haystack) {
		return 0;
	}

	needle_len = strlen(needle);
	for (i = 0; haystack[i]; i++) {
		for (j = 0; j < needle_len; j++) {
			unsigned char hc = (unsigned char) haystack[i + j];
			unsigned char nc = (unsigned char) needle[j];
			if (hc == '\0') {
				return 0;
			}
			if (tolower(hc) != tolower(nc)) {
				break;
			}
		}
		if (j == needle_len) {
			return 1;
		}
	}
	return 0;
}

static const char *ipv4_to_string(struct in_addr addr, char *buf, size_t buf_len) {
	if (!InetNtopA(AF_INET, &addr, buf, (DWORD) buf_len)) {
		copy_text(buf, buf_len, "0.0.0.0");
	}
	return buf;
}

static int first_ipv4(IP_ADAPTER_ADDRESSES *adapter, struct in_addr *addr) {
	IP_ADAPTER_UNICAST_ADDRESS *unicast;
	for (unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
		if (unicast->Address.lpSockaddr
				&& unicast->Address.lpSockaddr->sa_family == AF_INET) {
			*addr = ((struct sockaddr_in *) unicast->Address.lpSockaddr)->sin_addr;
			return 1;
		}
	}
	return 0;
}

static int is_usable_adapter(IP_ADAPTER_ADDRESSES *adapter, struct in_addr *addr) {
	if (adapter->OperStatus != IfOperStatusUp) {
		return 0;
	}
	if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
		return 0;
	}
	if (adapter->PhysicalAddressLength != 6) {
		return 0;
	}
	return first_ipv4(adapter, addr);
}

static IP_ADAPTER_ADDRESSES *load_adapters(void) {
	ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	ULONG out_buf_len = 15 * 1024;
	ULONG ret;
	IP_ADAPTER_ADDRESSES *addresses = NULL;
	int attempt;

	for (attempt = 0; attempt < 3; attempt++) {
		free(addresses);
		addresses = (IP_ADAPTER_ADDRESSES *) malloc(out_buf_len);
		if (!addresses) {
			LogWrite(INIT, ERROR, "Unable to allocate adapter list.");
			return NULL;
		}

		ret = GetAdaptersAddresses(AF_INET, flags, NULL, addresses, &out_buf_len);
		if (ret == NO_ERROR) {
			return addresses;
		}
		if (ret != ERROR_BUFFER_OVERFLOW) {
			LogWrite(INIT, ERROR, "GetAdaptersAddresses failed: %lu", ret);
			free(addresses);
			return NULL;
		}
	}

	LogWrite(INIT, ERROR, "GetAdaptersAddresses buffer sizing failed.");
	free(addresses);
	return NULL;
}

static void fill_adapter_info(IP_ADAPTER_ADDRESSES *adapter,
		struct in_addr ipv4, WinAdapterInfo *info) {
	memset(info, 0, sizeof(*info));
	copy_text(info->adapter_name, sizeof(info->adapter_name), adapter->AdapterName);
	snprintf(info->pcap_name, sizeof(info->pcap_name), "\\Device\\NPF_%s", adapter->AdapterName);
	wide_to_utf8(adapter->FriendlyName, info->friendly_name, sizeof(info->friendly_name));
	wide_to_utf8(adapter->Description, info->description, sizeof(info->description));
	info->if_index = adapter->IfIndex;
	info->ipv4 = ipv4;
	memcpy(info->mac, adapter->PhysicalAddress, 6);
}

static int adapter_matches(const WinAdapterInfo *info, const char *selector) {
	if (!selector || selector[0] == '\0') {
		return 1;
	}
	return contains_case_insensitive(info->adapter_name, selector)
			|| contains_case_insensitive(info->pcap_name, selector)
			|| contains_case_insensitive(info->friendly_name, selector)
			|| contains_case_insensitive(info->description, selector);
}

static int find_adapter(const char *selector, WinAdapterInfo *info) {
	IP_ADAPTER_ADDRESSES *addresses = load_adapters();
	IP_ADAPTER_ADDRESSES *adapter;
	WinAdapterInfo candidate;
	struct in_addr ipv4;

	if (!addresses) {
		return -1;
	}

	for (adapter = addresses; adapter; adapter = adapter->Next) {
		if (!is_usable_adapter(adapter, &ipv4)) {
			continue;
		}
		fill_adapter_info(adapter, ipv4, &candidate);
		if (adapter_matches(&candidate, selector)) {
			*info = candidate;
			free(addresses);
			return 0;
		}
	}

	if (selector && selector[0]) {
		LogWrite(INIT, ERROR, "No active IPv4 adapter matched '%s'. Use --list-ifaces.", selector);
	} else {
		LogWrite(INIT, ERROR, "No active IPv4 adapter found. Use --iface to select one.");
	}
	free(addresses);
	return -1;
}

int auth_ListInterfaces(void) {
	IP_ADAPTER_ADDRESSES *addresses = load_adapters();
	IP_ADAPTER_ADDRESSES *adapter;
	struct in_addr ipv4;
	WinAdapterInfo info;
	char ipbuf[INET_ADDRSTRLEN];
	int count = 0;

	if (!addresses) {
		return -1;
	}

	printf("Usable Windows/Npcap interfaces:\n\n");
	for (adapter = addresses; adapter; adapter = adapter->Next) {
		if (!is_usable_adapter(adapter, &ipv4)) {
			continue;
		}
		fill_adapter_info(adapter, ipv4, &info);
		printf("[%d]\n", ++count);
		printf("  iface:       %s\n", info.pcap_name);
		printf("  friendly:    %s\n", info.friendly_name[0] ? info.friendly_name : "(none)");
		printf("  description: %s\n", info.description[0] ? info.description : "(none)");
		printf("  ip:          %s\n", ipv4_to_string(info.ipv4, ipbuf, sizeof(ipbuf)));
		printf("  mac:         %02x:%02x:%02x:%02x:%02x:%02x\n\n",
				info.mac[0], info.mac[1], info.mac[2],
				info.mac[3], info.mac[4], info.mac[5]);
	}
	free(addresses);

	if (count == 0) {
		printf("No active IPv4 adapters found.\n");
		return -1;
	}
	return 0;
}

int getIfIP(int sock) {
	(void) sock;
	if (!selected_adapter_valid) {
		return -1;
	}
	local_ipaddr = selected_adapter.ipv4;
	return 0;
}

int auth_8021x_Init() {
	char errbuf[PCAP_ERRBUF_SIZE];
	struct bpf_program filter;
	int filter_compiled = 0;

	close_8021x_handle();

	if (find_adapter(DeviceName, &selected_adapter) != 0) {
		return -1;
	}
	selected_adapter_valid = 1;
	copy_text(DeviceName, sizeof(DeviceName), selected_adapter.pcap_name);
	local_ipaddr = selected_adapter.ipv4;
	memcpy(MAC, selected_adapter.mac, sizeof(MAC));

	auth_8021x_handle = pcap_open_live(selected_adapter.pcap_name, ETH_FRAME_LEN,
			1, AUTH_8021X_PCAP_TIMEOUT_MS, errbuf);
	if (!auth_8021x_handle) {
		LogWrite(DOT1X, ERROR, "Unable to open Npcap adapter '%s': %s",
				selected_adapter.pcap_name, errbuf);
		return -1;
	}

	if (pcap_compile(auth_8021x_handle, &filter, "ether proto 0x888e", 1,
			PCAP_NETMASK_UNKNOWN) != 0) {
		LogWrite(DOT1X, ERROR, "Unable to compile EAPOL filter: %s",
				pcap_geterr(auth_8021x_handle));
		goto ERR;
	}
	filter_compiled = 1;

	if (pcap_setfilter(auth_8021x_handle, &filter) != 0) {
		LogWrite(DOT1X, ERROR, "Unable to apply EAPOL filter: %s",
				pcap_geterr(auth_8021x_handle));
		goto ERR;
	}

	pcap_freecode(&filter);
	LogWrite(INIT, INF, "%s link selected.", selected_adapter.pcap_name);
	return 0;

ERR:
	if (filter_compiled) {
		pcap_freecode(&filter);
	}
	close_8021x_handle();
	return -1;
}

int auth_8021x_Sender(uint8_t *send_data, int send_data_len) {
	if (!auth_8021x_handle) {
		return 0;
	}
	if (pcap_sendpacket(auth_8021x_handle, send_data, send_data_len) != 0) {
		LogWrite(DOT1X, ERROR, "auth_8021x_Sender error: %s",
				pcap_geterr(auth_8021x_handle));
		return 0;
	}
	PrintHex(DOT1X, "Packet sent", send_data, send_data_len);
	return 1;
}

int auth_8021x_Receiver(uint8_t *recv_data) {
	struct pcap_pkthdr *header;
	const u_char *pkt_data;
	int ret;

	if (!auth_8021x_handle) {
		return 0;
	}

	ret = pcap_next_ex(auth_8021x_handle, &header, &pkt_data);
	if (ret == 0) {
		return 0;
	}
	if (ret < 0) {
		LogWrite(DOT1X, ERROR, "Npcap receive failed: %s", pcap_geterr(auth_8021x_handle));
		return 0;
	}
	if (header->caplen < 18) {
		return 0;
	}
	if (header->caplen > ETH_FRAME_LEN) {
		LogWrite(DOT1X, ERROR, "Npcap frame too large: %u", header->caplen);
		return 0;
	}
	if (memcmp(pkt_data, MAC, ETH_ALEN) != 0) {
		return 0;
	}
	if (!(pkt_data[12] == 0x88 && pkt_data[13] == 0x8e)) {
		return 0;
	}

	memcpy(recv_data, pkt_data, header->caplen);
	PrintHex(DOT1X, "Packet received", recv_data, header->caplen);
	return 1;
}

static int wait_8021x_packet(uint8_t *recv_data, DWORD timeout_ms) {
	ULONGLONG deadline = GetTickCount64() + timeout_ms;
	do {
		if (auth_8021x_Receiver(recv_data)) {
			return 1;
		}
	} while (GetTickCount64() < deadline);
	return 0;
}

int auth_8021x_Logoff() {
	uint8_t recv_8021x_buf[ETH_FRAME_LEN] = { 0 };
	uint8_t LogoffCnt = 2;
	int ret = 0;

	if (!auth_8021x_handle) {
		return 0;
	}

	LogWrite(DOT1X, INF, "Client: Send Logoff.");
	while (LogoffCnt--) {
		send_8021x_data_len = AppendDrcomLogoffPkt(MultcastHeader, send_8021x_data);
		LogWrite(DOT1X, DEBUG, "Sending logoff packet.");
		auth_8021x_Sender(send_8021x_data, (int) send_8021x_data_len);
		if (wait_8021x_packet(recv_8021x_buf, AUTH_8021X_LOGOFF_DELAY_MS)) {
			if ((EAP_Code) recv_8021x_buf[18] == FAILURE) {
				LogWrite(DOT1X, INF, "Logged off.");
				ret = 1;
			}
		}
	}
	return ret;
}

int auth_UDP_Init() {
	int on = 1;
	u_long nonblock = 1;

	close_udp_socket();

	auth_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (auth_udp_sock == INVALID_SOCKET) {
		LogWrite(DRCOM, ERROR, "Create UDP socket failed: %d", WSAGetLastError());
		return -1;
	}

	if (setsockopt(auth_udp_sock, SOL_SOCKET, SO_REUSEADDR,
			(const char *) &on, sizeof(on)) == SOCKET_ERROR) {
		LogWrite(DRCOM, ERROR, "UDP SO_REUSEADDR failed: %d", WSAGetLastError());
		close_udp_socket();
		return -1;
	}
	if (setsockopt(auth_udp_sock, SOL_SOCKET, SO_BROADCAST,
			(const char *) &on, sizeof(on)) == SOCKET_ERROR) {
		LogWrite(DRCOM, ERROR, "UDP SO_BROADCAST failed: %d", WSAGetLastError());
		close_udp_socket();
		return -1;
	}

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr = udpserver_ipaddr;
	serv_addr.sin_port = htons(SERVER_PORT);

	memset(&local_addr, 0, sizeof(local_addr));
	local_addr.sin_family = AF_INET;
	local_addr.sin_addr = local_ipaddr;
	local_addr.sin_port = htons(SERVER_PORT);

	if (bind(auth_udp_sock, (struct sockaddr *) &(local_addr),
			sizeof(local_addr)) == SOCKET_ERROR) {
		LogWrite(DRCOM, ERROR, "Bind UDP socket to local IP failed: %d", WSAGetLastError());
		close_udp_socket();
		return -1;
	}

	if (ioctlsocket(auth_udp_sock, FIONBIO, &nonblock) == SOCKET_ERROR) {
		LogWrite(DRCOM, ERROR, "Set UDP socket non-blocking failed: %d", WSAGetLastError());
		close_udp_socket();
		return -1;
	}

	return 0;
}

int auth_UDP_Sender(uint8_t *send_data, int send_data_len) {
	int sent;
	if (auth_udp_sock == INVALID_SOCKET) {
		return 0;
	}
	sent = sendto(auth_udp_sock, (const char *) send_data, send_data_len, 0,
			(struct sockaddr *) &serv_addr, sizeof(serv_addr));
	if (sent != send_data_len) {
		LogWrite(DRCOM, ERROR, "auth_UDP_Sender error: %d", WSAGetLastError());
		return 0;
	}
	PrintHex(DRCOM, "Packet sent", send_data, send_data_len);
	return 1;
}

int auth_UDP_Receiver(uint8_t *recv_data) {
	struct sockaddr_in clntaddr;
	int addrlen = sizeof(struct sockaddr_in);
	int recv_len;
	int err;

	if (auth_udp_sock == INVALID_SOCKET) {
		return 0;
	}

	recv_len = recvfrom(auth_udp_sock, (char *) recv_data, ETH_FRAME_LEN, 0,
			(struct sockaddr *) &clntaddr, &addrlen);
	if (recv_len == SOCKET_ERROR) {
		err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK) {
			return 0;
		}
		LogWrite(DRCOM, ERROR, "auth_UDP_Receiver error: %d", err);
		return 0;
	}

	if (recv_len > 0 && memcmp(&clntaddr.sin_addr, &serv_addr.sin_addr, 4) == 0
			&& ((recv_data[0] == 0x07)
					|| ((recv_data[0] == 0x4d) && (recv_data[1] == 0x38)))) {
		PrintHex(DRCOM, "Packet received", recv_data, recv_len);
		return 1;
	}
	return 0;
}

size_t appendStartPkt(uint8_t header[]) {
	return AppendDrcomStartPkt(header, send_8021x_data);
}

size_t appendResponseIdentity(const uint8_t request[]) {
	return AppendDrcomResponseIdentity(request, EthHeader, UserName, send_8021x_data);
}

size_t appendResponseMD5(const uint8_t request[]) {
	return AppendDrcomResponseMD5(request, EthHeader, UserName, Password, send_8021x_data);
}

void initAuthenticationInfo() {
	memcpy(MultcastHeader, MultcastAddr, 6);
	memcpy(MultcastHeader + 6, MAC, 6);
	MultcastHeader[12] = 0x88;
	MultcastHeader[13] = 0x8e;

	memcpy(BroadcastHeader, BroadcastAddr, 6);
	memcpy(BroadcastHeader + 6, MAC, 6);
	BroadcastHeader[12] = 0x88;
	BroadcastHeader[13] = 0x8e;

	memcpy(UnicastHeader, UnicastAddr, 6);
	memcpy(UnicastHeader + 6, MAC, 6);
	UnicastHeader[12] = 0x88;
	UnicastHeader[13] = 0x8e;

	memcpy(EthHeader + 6, MAC, 6);
	EthHeader[12] = 0x88;
	EthHeader[13] = 0x8e;
}

void printIfInfo() {
	char ipbuf[INET_ADDRSTRLEN];
	char dnsbuf[INET_ADDRSTRLEN];
	char udpbuf[INET_ADDRSTRLEN];

	LogWrite(INIT, INF, "Interface: %s", selected_adapter.friendly_name[0]
			? selected_adapter.friendly_name : selected_adapter.pcap_name);
	LogWrite(INIT, INF, "IP: %s", ipv4_to_string(local_ipaddr, ipbuf, sizeof(ipbuf)));
	LogWrite(INIT, INF, "DNS: %s", ipv4_to_string(dns_ipaddr, dnsbuf, sizeof(dnsbuf)));
	LogWrite(INIT, INF, "UDP server: %s", ipv4_to_string(udpserver_ipaddr, udpbuf, sizeof(udpbuf)));
	LogWrite(INIT, INF, "MAC: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
			MAC[0], MAC[1], MAC[2], MAC[3], MAC[4], MAC[5]);
}

int loginToGetServerMAC(uint8_t recv_data[]) {
	send_8021x_data_len = appendStartPkt(MultcastHeader);
	auth_8021x_Sender(send_8021x_data, (int) send_8021x_data_len);
	LogWrite(DOT1X, INF, "%s", "Client: Multcast Start.");
	times = AUTH_8021X_RECV_TIMES;
	while (resev == 0) {
		if (wait_8021x_packet(recv_data, AUTH_8021X_RECV_DELAY_MS)) {
			LogWrite(DOT1X, INF, "Received the first request.");
			resev = 1;
			times = AUTH_8021X_RECV_TIMES;
			memcpy(EthHeader, recv_data + 6, 6);
			if (auth_8021x_Handler(recv_data)) {
				return -EPROTO;
			}
			return 0;
		}

		if (times <= 0) {
			LogWrite(DOT1X, ERROR, "Error! No Response");
			return -ENETUNREACH;
		}
		times--;

		if (send_8021x_data[1] == 0xff) {
			send_8021x_data_len = appendStartPkt(MultcastHeader);
			auth_8021x_Sender(send_8021x_data, (int) send_8021x_data_len);
			LogWrite(DOT1X, INF, "Client: Multcast Start.");
		} else if (send_8021x_data[1] == 0x80) {
			send_8021x_data_len = appendStartPkt(BroadcastHeader);
			auth_8021x_Sender(send_8021x_data, (int) send_8021x_data_len);
			LogWrite(DOT1X, INF, "Client: Broadcast Start.");
		}
	}
	return 0;
}

int Authentication(int client) {
	int ret = 0;
	uint8_t recv_8021x_buf[ETH_FRAME_LEN] = { 0 };

	if (auth_8021x_Init() != 0) {
		LogWrite(DOT1X, ERROR, "Unable to initialize 802.1x capture.");
		exit(EXIT_FAILURE);
	}
	initAuthenticationInfo();
	ret = auth_8021x_Logoff();
	if (client == LOGOFF) {
		close_8021x_handle();
		return 0;
	}

	if (ret == 1) {
		sleep_seconds(2);
	} else if (ret < 0) {
		goto ERR1;
	}

	if ((ret = getIfIP(0)) < 0) {
		goto ERR1;
	}
	printIfInfo();
	if ((ret = auth_UDP_Init()) != 0) {
		LogWrite(DRCOM, ERROR, "Unable to initialize UDP socket.");
		goto ERR1;
	}

	ret = loginToGetServerMAC(recv_8021x_buf);
	if (ret < 0) {
		goto ERR2;
	}

	BaseHeartbeatTime = time(NULL);
	while (resev) {
		if (auth_8021x_Receiver(recv_8021x_buf)) {
			if ((ret = auth_8021x_Handler(recv_8021x_buf)) != 0) {
				resev = 0;
			}
		}

		if (auth_UDP_Receiver(recv_udp_data)) {
			send_udp_data_len = Drcom_UDP_Handler(recv_udp_data);
			if (success_8021x && send_udp_data_len) {
				auth_UDP_Sender(send_udp_data, send_udp_data_len);
			}
		}

		if (success_8021x && isNeedHeartBeat) {
			if ((lastHBDone == 0)
					&& (time(NULL) - BaseHeartbeatTime > DRCOM_UDP_HEARTBEAT_TIMEOUT)) {
				LogWrite(DRCOM, ERROR, "Client: No response to last heartbeat.");
				ret = 1;
				break;
			}
			if (time(NULL) - BaseHeartbeatTime > DRCOM_UDP_HEARTBEAT_DELAY) {
				send_udp_data_len = Drcom_ALIVE_HEARTBEAT_TYPE_Setter(send_udp_data, recv_udp_data);
				LogWrite(DRCOM, INF, "Client: Send alive heartbeat.");
				if (auth_UDP_Sender(send_udp_data, send_udp_data_len) == 0) {
					ret = 1;
					break;
				}
				BaseHeartbeatTime = time(NULL);
				lastHBDone = 0;
			}
		}
	}

	success_8021x = 0;
	resev = 0;
	lastHBDone = 1;
ERR2:
	close_udp_socket();
	auth_8021x_Logoff();
ERR1:
	close_8021x_handle();
	return ret;
}

typedef enum {
	MISC_START_ALIVE = 0x01,
	MISC_RESPONSE_FOR_ALIVE = 0x02,
	MISC_INFO = 0x03,
	MISC_RESPONSE_INFO = 0x04,
	MISC_HEART_BEAT = 0x0b,
	MISC_RESPONSE_HEART_BEAT = 0x06
} DRCOM_Type;
typedef enum {
	MISC_HEART_BEAT_01_TYPE = 0x01,
	MISC_HEART_BEAT_02_TYPE = 0x02,
	MISC_HEART_BEAT_03_TYPE = 0x03,
	MISC_HEART_BEAT_04_TYPE = 0x04,
	MISC_FILE_TYPE = 0x06
} DRCOM_MISC_HEART_BEAT_Type;

int Drcom_UDP_Handler(uint8_t *recv_data) {
	int data_len = 0;
	if (recv_data[0] == 0x07) {
		switch ((DRCOM_Type) recv_data[4]) {
		case MISC_RESPONSE_FOR_ALIVE:
			sleep_seconds(1);
			isNeedHeartBeat = 0;
			BaseHeartbeatTime = time(NULL);
			lastHBDone = 1;
			data_len = Drcom_MISC_INFO_Setter(send_udp_data, recv_data);
			LogWrite(DRCOM, INF, "Server: MISC_RESPONSE_FOR_ALIVE. Send MISC_INFO.");
			break;
		case MISC_RESPONSE_INFO:
			memcpy(tailinfo, recv_data + 16, 16);
			encryptDrcomInfo(tailinfo);
			data_len = Drcom_MISC_HEART_BEAT_01_TYPE_Setter(send_udp_data, recv_data);
			isNeedHeartBeat = 1;
			LogWrite(DRCOM, INF, "Server: MISC_RESPONSE_INFO. Send MISC_HEART_BEAT_01.");
			break;
		case MISC_HEART_BEAT:
			switch ((DRCOM_MISC_HEART_BEAT_Type) recv_data[5]) {
			case MISC_FILE_TYPE:
				data_len = Drcom_MISC_HEART_BEAT_01_TYPE_Setter(send_udp_data, recv_data);
				LogWrite(DRCOM, INF, "Server: MISC_FILE_TYPE. Send MISC_HEART_BEAT_01.");
				break;
			case MISC_HEART_BEAT_02_TYPE:
				data_len = Drcom_MISC_HEART_BEAT_03_TYPE_Setter(send_udp_data, recv_data);
				LogWrite(DRCOM, INF, "Server: MISC_HEART_BEAT_02. Send MISC_HEART_BEAT_03.");
				break;
			case MISC_HEART_BEAT_04_TYPE:
				BaseHeartbeatTime = time(NULL);
				lastHBDone = 1;
				LogWrite(DRCOM, INF, "Server: MISC_HEART_BEAT_04. Waiting next heart beat cycle.");
				break;
			default:
				LogWrite(DRCOM, ERROR, "Server: Unexpected heart beat request (type:0x%02hhx)!",
						recv_data[5]);
				break;
			}
			break;
		case MISC_RESPONSE_HEART_BEAT:
			data_len = Drcom_MISC_HEART_BEAT_01_TYPE_Setter(send_udp_data, recv_data);
			LogWrite(DRCOM, INF, "Server: MISC_RESPONSE_HEART_BEAT. Send MISC_HEART_BEAT_01.");
			break;
		default:
			LogWrite(DRCOM, ERROR, "UDP Server: Unexpected request (type:0x%02hhx)!",
					recv_data[2]);
			break;
		}
	}

	if ((recv_data[0] == 0x4d) && (recv_data[1] == 0x38)) {
		LogWrite(DRCOM, INF, "%s%s", "Server: Server Information: ", recv_data + 4);
	}
	memset(recv_data, 0, ETH_FRAME_LEN);
	return data_len;
}

int auth_8021x_Handler(uint8_t recv_data[]) {
	uint16_t pkg_len = 0;
	const char *errstr;

	memcpy(&pkg_len, recv_data + 20, sizeof(pkg_len));
	pkg_len = htons(pkg_len);

	send_8021x_data_len = 0;
	if ((EAP_Code) recv_data[18] == REQUEST) {
		switch ((EAP_Type) recv_data[22]) {
		case IDENTITY:
			LogWrite(DOT1X, INF, "Server: Request Identity.");
			send_8021x_data_len = appendResponseIdentity(recv_data);
			LogWrite(DOT1X, INF, "Client: Response Identity.");
			break;
		case MD5:
			LogWrite(DOT1X, INF, "Server: Request MD5-Challenge.");
			send_8021x_data_len = appendResponseMD5(recv_data);
			LogWrite(DOT1X, INF, "Client: Response MD5-Challenge.");
			break;
		case NOTIFICATION:
			recv_data[23 + pkg_len - 5] = 0;
			if ((errstr = DrcomEAPErrParse((const char *) (recv_data + 23))) != NULL) {
				LogWrite(DOT1X, ERROR, "Server: Authentication failed: %s", errstr);
				return -1;
			} else {
				LogWrite(DOT1X, INF, "Server: Notification: %s", recv_data + 23);
			}
			break;
		case AVAILABLE:
			LogWrite(DOT1X, ERROR, "Unexpected request type (AVAILABLE). Pls report it.");
			break;
		case ALLOCATED_0x07:
			LogWrite(DOT1X, ERROR, "Unexpected request type (0x07). Pls report it.");
			break;
		case ALLOCATED_0x08:
			LogWrite(DOT1X, ERROR, "Unexpected request type (0x08). Pls report it.");
			break;
		default:
			LogWrite(DOT1X, ERROR, "Unexpected request type (0x%02hhx). Pls report it.",
					(EAP_Type) recv_data[22]);
			LogWrite(DOT1X, ERROR, "Exit.");
			return -1;
		}
	} else if ((EAP_Code) recv_data[18] == FAILURE) {
		uint8_t errtype = recv_data[22];
		success_8021x = 0;
		isNeedHeartBeat = 0;
		LogWrite(DOT1X, ERROR, "Server: Failure.");
		if (times > 0) {
			times--;
			sleep_seconds(AUTH_8021X_RECV_DELAY_MS / 1000);
			return 1;
		} else {
			LogWrite(DOT1X, ERROR, "Reconnection failed. Server: errtype=0x%02hhx", errtype);
			exit(EXIT_FAILURE);
		}
	} else if ((EAP_Code) recv_data[18] == SUCCESS) {
		LogWrite(DOT1X, INF, "Server: Success.");
		times = AUTH_8021X_RECV_TIMES;
		success_8021x = 1;
		send_udp_data_len = Drcom_MISC_START_ALIVE_Setter(send_udp_data, recv_data);
		sleep_seconds(AUTH_8021X_RECV_DELAY_MS / 1000);
		if (OnlineHookCmd) {
			system(OnlineHookCmd);
		}
		isNeedHeartBeat = 1;
		BaseHeartbeatTime = time(NULL);
		lastHBDone = 0;
		auth_UDP_Sender(send_udp_data, send_udp_data_len);
	}

	if (send_8021x_data_len > 0) {
		auth_8021x_Sender(send_8021x_data, (int) send_8021x_data_len);
	}
	return 0;
}
