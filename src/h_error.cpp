#include		"generic.h"
#include		"hview.h"
#include		<stdio.h>
static const char file[]=__FILE__;
char			first_error_msg[e_msg_size]={}, latest_error_msg[e_msg_size]={};
void			print_errors(HDC hDC)
{
	if(first_error_msg[0])
	{
		GUIPrint(hDC, w>>1, (h>>1)-32, first_error_msg);
		GUIPrint(hDC, w>>1, (h>>1)-16, latest_error_msg);
	}
}
bool 			log_error(const char *file, int line, const char *format, ...)
{
	bool firsttime=first_error_msg[0]=='\0';
	char *buf=first_error_msg[0]?latest_error_msg:first_error_msg;
	va_list args;
	va_start(args, format);
	vsprintf_s(g_buf, e_msg_size, format, args);
	va_end(args);
	int size=(int)strlen(file), start=size-1;
	for(;start>=0&&file[start]!='/'&&file[start]!='\\';--start);
	start+=start==-1||file[start]=='/'||file[start]=='\\';
//	int length=snprintf(buf, e_msg_size, "%s (%d)%s", g_buf, line, file+start);
//	int length=snprintf(buf, e_msg_size, "%s\n%s(%d)", g_buf, file, line);
//	int length=snprintf(buf, e_msg_size, "%s(%d):\n\t%s", file, line, g_buf);
	int length=sprintf_s(buf, e_msg_size, "%s(%d): %s", file+start, line, g_buf);
	if(firsttime)
	{
		memcpy(latest_error_msg, first_error_msg, length);
		messageboxa(ghWnd, "Error", latest_error_msg);//redundant, since report_error/emergencyPrint prints both
	}
	return firsttime;
}
int				sys_check(const char *file, int line)
{
	int error=GetLastError();
	if(error)
	{
		char *messageBuffer=nullptr;
		size_t size=FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
		log_error(file, line, "System %d: %s", error, messageBuffer);
		LocalFree(messageBuffer);
	}
	return 0;
}