//h_error.cpp - error reporting functions
//Copyright (C) 2022  Ayman Wagih Mohsen
//
//This program is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include		"generic.h"
#include		"hview.h"
#include		<stdio.h>
static const char file[]=__FILE__;
int				error_count=0;
char			first_error_msg[e_msg_size]={}, latest_error_msg[e_msg_size]={};
void			print_errors(HDC hDC)
{
	if(first_error_msg[0])
	{
		GUIPrint(hDC, w>>2, (h>>1)-32, first_error_msg);
		if(error_count>1)
			GUIPrint(hDC, w>>2, (h>>1)-16, latest_error_msg);
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
	int length=sprintf_s(buf, e_msg_size, "[%d] %s(%d): %s", error_count, file+start, line, g_buf);
	if(firsttime)
	{
		memcpy(latest_error_msg, first_error_msg, length);
#ifndef BENCHMARK
		messageboxa(ghWnd, "Error", latest_error_msg);//redundant, since report_error/emergencyPrint prints both
#endif
	}
	++error_count;
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