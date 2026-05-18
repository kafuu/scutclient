/* File: main.c
 * ------------
 * Windows native SCUT Dr.com 802.1X client entry point.
 */
#include "auth.h"
#include "info.h"
#include "tracelog.h"

static volatile LONG exiting = 0;

static void PrintHelp(const char *argn) {
	printf("Usage: %s --username <username> --password <password> [options...]\n"
		" -l, --list-ifaces              List usable Windows/Npcap interfaces.\n"
		" -i, --iface <name>             Interface name, GUID, friendly name, or Npcap device.\n"
		" -n, --dns <dns>                DNS server address to be sent to UDP server.\n"
		" -H, --hostname <hostname>\n"
		" -s, --udp-server <server>\n"
		" -c, --cli-version <client version>\n"
		" -T, --net-time <time>          The time you are allowed to access internet. e.g. 6:10\n"
		" -h, --hash <hash>              DrAuthSvr.dll hash value.\n"
		" -E, --online-hook <command>    Command to execute after EAP authentication success.\n"
		" -Q, --offline-hook <command>   Command to execute when you are forced offline at night.\n"
		" -D, --debug[=<level>]\n"
		" -o, --logoff\n",
		argn);
}

static void cleanup_winsock(void) {
	WSACleanup();
}

static int init_winsock(void) {
	WSADATA wsa;
	int ret = WSAStartup(MAKEWORD(2, 2), &wsa);
	if (ret != 0) {
		fprintf(stderr, "WSAStartup failed: %d\n", ret);
		return -1;
	}
	atexit(cleanup_winsock);
	return 0;
}

static BOOL WINAPI handle_console_ctrl(DWORD ctrl_type) {
	switch (ctrl_type) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		if (InterlockedExchange(&exiting, 1) == 0) {
			LogWrite(ALL, INF, "Exiting...");
			auth_8021x_Logoff();
		}
		ExitProcess(0);
		return TRUE;
	default:
		return FALSE;
	}
}

static int option_matches(const char *arg, const char *long_name,
		char short_name, const char **inline_value) {
	size_t long_len;

	*inline_value = NULL;
	if (arg[0] == '-' && arg[1] == '-') {
		const char *name = arg + 2;
		long_len = strlen(long_name);
		if (strcmp(name, long_name) == 0) {
			return 1;
		}
		if (strncmp(name, long_name, long_len) == 0 && name[long_len] == '=') {
			*inline_value = name + long_len + 1;
			return 1;
		}
	}
	if (arg[0] == '-' && arg[1] == short_name) {
		if (arg[2] == '\0') {
			return 1;
		}
		if (arg[2] == '=') {
			*inline_value = arg + 3;
			return 1;
		}
		*inline_value = arg + 2;
		return 1;
	}
	return 0;
}

static const char *required_value(int argc, char *argv[], int *index,
		const char *option_name, const char *inline_value) {
	if (inline_value) {
		return inline_value;
	}
	if (*index + 1 >= argc) {
		LogWrite(INIT, ERROR, "%s requires a value.", option_name);
		exit(EXIT_FAILURE);
	}
	(*index)++;
	return argv[*index];
}

static int parse_ipv4(const char *text, struct in_addr *addr) {
	return InetPtonA(AF_INET, text, addr) == 1;
}

static void copy_option(char *dest, size_t dest_len, const char *value) {
	strncpy(dest, value, dest_len - 1);
	dest[dest_len - 1] = '\0';
}

static int parse_debug_level(const char *value) {
	int tmpdbg;
	if (!value || value[0] == '\0') {
		cloglev = DEBUG;
		return 0;
	}
	tmpdbg = atoi(value);
	if ((tmpdbg < NONE) || (tmpdbg > TRACE)) {
		LogWrite(INIT, ERROR, "Invalid debug level!");
		return -1;
	}
	cloglev = (LOGLEVEL) tmpdbg;
	return 0;
}

static int parse_arguments(int argc, char *argv[], int *client,
		uint8_t *allowed_hour, uint8_t *allowed_minute, int *list_ifaces) {
	int i;
	const char *inline_value;
	const char *value;

	for (i = 1; i < argc; i++) {
		if (option_matches(argv[i], "username", 'u', &inline_value)) {
			UserName = (char *) required_value(argc, argv, &i, argv[i], inline_value);
		} else if (option_matches(argv[i], "password", 'p', &inline_value)) {
			Password = (char *) required_value(argc, argv, &i, argv[i], inline_value);
		} else if (option_matches(argv[i], "online-hook", 'E', &inline_value)) {
			OnlineHookCmd = (char *) required_value(argc, argv, &i, argv[i], inline_value);
		} else if (option_matches(argv[i], "offline-hook", 'Q', &inline_value)) {
			OfflineHookCmd = (char *) required_value(argc, argv, &i, argv[i], inline_value);
		} else if (option_matches(argv[i], "iface", 'i', &inline_value)) {
			value = required_value(argc, argv, &i, argv[i], inline_value);
			copy_option(DeviceName, IFNAMSIZ, value);
		} else if (option_matches(argv[i], "dns", 'n', &inline_value)) {
			value = required_value(argc, argv, &i, argv[i], inline_value);
			if (!parse_ipv4(value, &dns_ipaddr)) {
				LogWrite(INIT, ERROR, "DNS invalid!");
				return -1;
			}
		} else if (option_matches(argv[i], "hostname", 'H', &inline_value)) {
			value = required_value(argc, argv, &i, argv[i], inline_value);
			copy_option(HostName, sizeof(HostName), value);
		} else if (option_matches(argv[i], "udp-server", 's', &inline_value)) {
			value = required_value(argc, argv, &i, argv[i], inline_value);
			if (!parse_ipv4(value, &udpserver_ipaddr)) {
				LogWrite(INIT, ERROR, "UDP server IP invalid!");
				return -1;
			}
		} else if (option_matches(argv[i], "net-time", 'T', &inline_value)) {
			int hour, minute;
			value = required_value(argc, argv, &i, argv[i], inline_value);
			if ((sscanf(value, "%d:%d", &hour, &minute) != 2)
					|| (hour < 0) || (hour >= 24) || (minute < 0) || (minute >= 60)) {
				LogWrite(INIT, ERROR, "Time invalid!");
				return -1;
			}
			*allowed_hour = (uint8_t) hour;
			*allowed_minute = (uint8_t) minute;
		} else if (option_matches(argv[i], "cli-version", 'c', &inline_value)) {
			value = required_value(argc, argv, &i, argv[i], inline_value);
			Version_len = hexStrToByte(value, Version, sizeof(Version));
		} else if (option_matches(argv[i], "hash", 'h', &inline_value)) {
			Hash = (char *) required_value(argc, argv, &i, argv[i], inline_value);
		} else if (option_matches(argv[i], "debug", 'D', &inline_value)) {
			value = inline_value;
			if (!value && i + 1 < argc && argv[i + 1][0] != '-') {
				value = argv[++i];
			}
			if (parse_debug_level(value) < 0) {
				return -1;
			}
		} else if (option_matches(argv[i], "logoff", 'o', &inline_value)) {
			*client = LOGOFF;
		} else if (option_matches(argv[i], "list-ifaces", 'l', &inline_value)) {
			*list_ifaces = 1;
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-?") == 0) {
			PrintHelp(argv[0]);
			exit(EXIT_SUCCESS);
		} else {
			PrintHelp(argv[0]);
			return -1;
		}
	}
	return 0;
}

int main(int argc, char *argv[]) {
	int client = 1;
	uint8_t a_hour = 255, a_minute = 255;
	int ret;
	unsigned int retry_time = 1;
	time_t ctime;
	struct tm *cltime;
	int list_ifaces = 0;

	if (init_winsock() != 0) {
		return EXIT_FAILURE;
	}

	LogWrite(ALL, INF, "scutclient Windows build: " __DATE__ " " __TIME__);
	LogWrite(ALL, INF, "Authored by Scutclient Project");
	LogWrite(ALL, INF, "Source code available at https://github.com/scutclient/scutclient");
	LogWrite(ALL, INF, "#######################################");

	if (parse_arguments(argc, argv, &client, &a_hour, &a_minute, &list_ifaces) < 0) {
		return EXIT_FAILURE;
	}

	if (list_ifaces) {
		return auth_ListInterfaces() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (HostName[0] == 0) {
		gethostname(HostName, sizeof(HostName));
		HostName[sizeof(HostName) - 1] = '\0';
	}

	if ((client != LOGOFF) && !((UserName && Password && UserName[0] && Password[0]))) {
		LogWrite(INIT, ERROR, "Please specify username and password!");
		return EXIT_FAILURE;
	}
	if (udpserver_ipaddr.s_addr == 0) {
		parse_ipv4(SERVER_ADDR, &udpserver_ipaddr);
	}
	if (dns_ipaddr.s_addr == 0) {
		parse_ipv4(DNS_ADDR, &dns_ipaddr);
	}

	SetConsoleCtrlHandler(handle_console_ctrl, TRUE);

	while (1) {
		ret = Authentication(client);
		if (ret == 1) {
			retry_time = 1;
			LogWrite(ALL, INF, "Restart authentication.");
		} else if (ret == -ENETUNREACH) {
			LogWrite(ALL, INF, "Retry in %d secs.", retry_time);
			Sleep(retry_time * 1000U);
			if (retry_time <= 256) {
				retry_time *= 2;
			}
		} else if (timeNotAllowed && (a_minute < 60)) {
			timeNotAllowed = 0;
			ctime = time(NULL);
			cltime = localtime(&ctime);
			if (((int) a_hour * 60 + a_minute) > ((int) cltime->tm_hour * 60 + cltime->tm_min)) {
				DWORD wait_seconds = (DWORD) ((((int) a_hour * 60 + a_minute)
						- ((int) cltime->tm_hour * 60 + cltime->tm_min)) * 60 - cltime->tm_sec);
				LogWrite(ALL, INF, "Waiting till %02hhd:%02hhd.", a_hour, a_minute);
				if (OfflineHookCmd) {
					system(OfflineHookCmd);
				}
				Sleep(wait_seconds * 1000U);
			} else {
				break;
			}
		} else {
			break;
		}
	}
	LogWrite(ALL, ERROR, "Exit.");
	return EXIT_SUCCESS;
}
