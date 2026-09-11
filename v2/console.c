#include<io.h>
#include<stdio.h>
#include<fcntl.h>
#ifdef __cplusplus
#include<iostream>
#endif
#include<Windows.h>
#include<stdarg.h>
enum
{
	G_BUF_SIZE=1024,
};
static char g_buf2[G_BUF_SIZE]={0};
int consoleactive=0;
void console_log(const char *format, ...)
{
	if(format)
	{
		va_list args;
		unsigned long printed=0;

		va_start(args, format);
		printed=vsprintf_s(g_buf2, G_BUF_SIZE-1, format, args);
		va_end(args);
		WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), g_buf2, printed, &printed, 0);
	}
}
void console_logw(const wchar_t *format, ...)
{
	if(format)
	{
		static wchar_t wbuf2[4096]={0};
		va_list args;
		unsigned long printed=0;

		va_start(args, format);
		printed=vswprintf_s(wbuf2, _countof(wbuf2)-1, format, args);
		va_end(args);
		WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), wbuf2, printed, &printed, 0);
	}
}
void console_start()//https://stackoverflow.com/questions/191842/how-do-i-get-console-output-in-c-with-a-windows-program
{
	if(!consoleactive)
	{
		consoleactive=1;
		int hConHandle;
		size_t lStdHandle;
		//CONSOLE_SCREEN_BUFFER_INFO coninfo;
		FILE *fp;

		// allocate a console for this app
		AllocConsole();

		// set the screen buffer to be big enough to let us scroll text
		//GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);
		//coninfo.dwSize.Y=1000;
		//SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);

		// redirect unbuffered STDOUT to the console
		lStdHandle=(size_t)GetStdHandle(STD_OUTPUT_HANDLE);
		hConHandle=_open_osfhandle(lStdHandle, _O_TEXT);
		fp=_fdopen(hConHandle, "w");
		*stdout=*fp;
		setvbuf(stdout, 0, _IONBF, 0);

		// redirect unbuffered STDIN to the console
		lStdHandle=(size_t)GetStdHandle(STD_INPUT_HANDLE);
		hConHandle=_open_osfhandle(lStdHandle, _O_TEXT);
		fp=_fdopen(hConHandle, "r");
		*stdin=*fp;
		setvbuf(stdin, 0, _IONBF, 0);

		// redirect unbuffered STDERR to the console
		lStdHandle=(size_t)GetStdHandle(STD_ERROR_HANDLE);
		hConHandle=_open_osfhandle(lStdHandle, _O_TEXT);
		fp=_fdopen(hConHandle, "w");
		*stderr=*fp;
		setvbuf(stderr, 0, _IONBF, 0);

#ifdef __cplusplus
		// make cout, wcout, cin, wcin, wcerr, cerr, wclog and clog
		// point to console as well
		std::ios::sync_with_stdio();
#endif

		console_log("\n\tWARNING: CLOSING THIS WINDOW WILL CLOSE THE PROGRAM\n\n");
	}
}
void console_end()
{
	if(consoleactive)
	{
		FreeConsole();
		consoleactive=0;
	}
}
void console_buffer_size(short x, short y)
{
	COORD coord={x, y};
	SMALL_RECT Rect={0, 0, (short)(x-1), (short)(y-1)};
	HANDLE Handle=GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleScreenBufferSize(Handle, coord);
	SetConsoleWindowInfo(Handle, TRUE, &Rect);
}

int console_scan(char *buf, int len)
{
	unsigned long ret_len=0;
	int success=ReadConsoleA(GetStdHandle(STD_INPUT_HANDLE), buf, len, &ret_len, 0);
	if(!success)
	{
		int error=GetLastError();
		console_log("I/O ERROR %d\n", error);
	}
	return ret_len;
}
int console_scan_int()
{
	int read=console_scan(g_buf2, G_BUF_SIZE);
	(void)read;
	return atoi(g_buf2);
}
double console_scan_float()
{
	int read=console_scan(g_buf2, G_BUF_SIZE);
	(void)read;
	return atof(g_buf2);
}
void console_pause()
{
	console_log("Enter 0 to continue... ");
	int k=console_scan(g_buf2, G_BUF_SIZE);
	(void)k;
}