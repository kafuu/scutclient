#include "tracelog.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// 默认日志最大100KB大小
#define MAXFILELEN 102400
char logtime[20];
char filepath[MAXFILEPATH] = "scutclient.log";
FILE *logfile;
LOGLEVEL cloglev = INF;

const static char *LogLevelText[] =
		{ "NONE", "ERROR", "INF", "DEBUG", "TRACE" };
const static char *LogTypeText[] =
		{ "ALL", "INIT", "8021X", "DRCOM" };

static unsigned long get_file_size(const char *path) {
	FILE *fp = fopen(path, "rb");
	long filesize;
	if (!fp) {
		return 0;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return 0;
	}
	filesize = ftell(fp);
	fclose(fp);
	return filesize < 0 ? 0 : (unsigned long) filesize;
}

static void init_log_path(void) {
	static int initialized = 0;
	if (initialized) {
		return;
	}
	initialized = 1;
#ifdef _WIN32
	{
		char tmp[MAXFILEPATH];
		DWORD len = GetTempPathA(sizeof(tmp), tmp);
		if (len > 0 && len < sizeof(tmp)) {
			snprintf(filepath, sizeof(filepath), "%sscutclient.log", tmp);
		}
	}
#endif
}

/*
 *获取时间
 * */
static void settime() {
	time_t timer = time(NULL);
	strftime(logtime, 20, "%Y-%m-%d %H:%M:%S", localtime(&timer));
}

static int initlog(LOGTYPE logtype, LOGLEVEL loglevel) {
	init_log_path();
	//获取日志时间
	settime();

	// 判定是否大于指定的大小，进行重命名为备份文件
	if (get_file_size(filepath) > MAXFILELEN) {
		char backup[MAXFILEPATH];
		snprintf(backup, sizeof(backup), "%s.backup.log", filepath);
		remove(backup);
		rename(filepath, backup);
	}

	if ((logfile = fopen(filepath, "a+")) == NULL) {
		perror("Unable to open log file");
		return -1;
	}
	//写入日志级别，日志时间
	fprintf(logfile, "[%s][%-5s][%-3s]:[", logtime, LogTypeText[logtype], LogLevelText[loglevel]);
	printf("[%s][%-5s][%-3s]:[", logtime, LogTypeText[logtype], LogLevelText[loglevel]);
	return 0;
}

/*
 *日志写入
 * */
int LogWrite(LOGTYPE logtype, LOGLEVEL loglevel, const char *format, ...) {
	va_list args;

	if (loglevel > cloglev)
		return 0;
	//初始化日志
	if (initlog(logtype, loglevel) != 0)
		return -1;
	//打印日志信息
	va_start(args, format);
	vfprintf(logfile, format, args);
	va_end(args);
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	fprintf(logfile, "]\n");
	printf("]\n");
	//文件刷出
	fflush(logfile);
	//日志关闭
	fclose(logfile);
	return 0;
}
