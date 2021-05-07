#ifndef GENERIC_H
#define GENERIC_H
#include		<Windows.h>
#include		<string>

//error handling
const int		e_msg_size=2048;
extern char		first_error_msg[], latest_error_msg[];
void			print_errors(HDC hDC);
bool 			log_error(const char *file, int line, const char *format, ...);
#define			LOG_ERROR(format, ...)	log_error(file, __LINE__, format, __VA_ARGS__)
int				sys_check(const char *file, int line);
#define			SYS_CHECK()		sys_check(file, __LINE__)
#define			SYS_ASSERT(SUCCESS)		((void)((SUCCESS)!=0||sys_check(file, __LINE__)))
#define			GEN_ASSERT(SUCCESS)		((void)((SUCCESS)!=0||log_error(file, __LINE__, #SUCCESS)))

//math
int				floor_log2(unsigned long long n);

extern bool		consoleactive;
void			console_start();
void			console_end();
void			console_pause();
void			console_buffer_size(int x, int y);
void			console_start(int cx, int cy);
void			console_start_good();

//gneeric buffer
const int		g_buf_size=2048;
extern char		g_buf[g_buf_size];
extern wchar_t	g_wbuf[g_buf_size];

extern HWND		ghWnd;

//win32
void			copy_to_clipboard(const char *a, int size);
inline void		copy_to_clipboard(std::string const &str){copy_to_clipboard(str.c_str(), str.size());}
int				GUINPrint(HDC hDC, int x, int y, int w, int h, const char *a, ...);
long			GUITPrint(HDC hDC, int x, int y, const char *a, ...);//return value: 0xHHHHWWWW		width=(short&)ret, height=((short*)&ret)[1]
void			GUIPrint(HDC hDC, int x, int y, const char *a, ...);
void			messageboxw(HWND hWnd, const wchar_t *title, const wchar_t *format, ...);
void			messageboxa(HWND hWnd, const char *title, const char *format, ...);
const char*		wm2str(int message);

//[PROFILER SETTINGS]

//comment or uncomment
//	#define PROFILER_CYCLES

//select one or nothing
	#define	PROFILER_SCREEN
//	#define PROFILER_TITLE
//	#define PROFILER_CMD

//comment or uncomment
//	#define PROFILER_CLIPBOARD

//select one
	#define TIMING_USE_QueryPerformanceCounter
//	#define	TIMING_USE_rdtsc
//	#define TIMING_USE_GetProcessTimes	//~16ms resolution
//	#define TIMING_USE_GetTickCount		//~16ms resolution
//	#define TIMING_USE_timeGetTime		//~16ms resolution

//END OF [PROFILER SETTINGS]


//time-measuring functions
#ifdef __GNUC__
#define	__rdtsc	__builtin_ia32_rdtsc
#endif
inline double		time_sec()
{
#ifdef TIMING_USE_QueryPerformanceCounter
	static long long t=0;
	static LARGE_INTEGER li={};
	QueryPerformanceFrequency(&li);
	t=li.QuadPart;
	QueryPerformanceCounter(&li);
	return (double)li.QuadPart/t;
#elif defined TIMING_USE_rdtsc
	static LARGE_INTEGER li={};
	QueryPerformanceFrequency(&li);
	return (double)__rdtsc()*0.001/li.QuadPart;//pre-multiplied by 1000
#elif defined TIMING_USE_GetProcessTimes
	FILETIME create, exit, kernel, user;
	int success=GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
	if(success)
//#ifdef PROFILER_CYCLES
	{
		const auto hns2sec=100e-9;
		return hns2ms*(unsigned long long&)user;
	//	return hns2ms*(unsigned long long&)kernel;
	}
//#else
//	{
//		SYSTEMTIME t;
//		success=FileTimeToSystemTime(&user, &t);
//		if(success)
//			return t.wHour*3600000.+t.wMinute*60000.+t.wSecond*1000.+t.wMilliseconds;
//		//	return t.wHour*3600.+t.wMinute*60.+t.wSecond+t.wMilliseconds*0.001;
//	}
//#endif
	SYS_CHECK();
	return -1;
#elif defined TIMING_USE_GetTickCount
	return (double)GetTickCount()*0.001;//the number of milliseconds that have elapsed since the system was started
#elif defined TIMING_USE_timeGetTime
	return (double)timeGetTime()*0.001;//system time, in milliseconds
#endif
}
inline double		time_ms()
{
#ifdef TIMING_USE_QueryPerformanceCounter
	static long long t=0;
	static LARGE_INTEGER li={};
	QueryPerformanceFrequency(&li);
	t=li.QuadPart;
	QueryPerformanceCounter(&li);
	return (double)li.QuadPart*1000./t;
#elif defined TIMING_USE_rdtsc
	static LARGE_INTEGER li={};
	QueryPerformanceFrequency(&li);
	return (double)__rdtsc()/li.QuadPart;//pre-multiplied by 1000
#elif defined TIMING_USE_GetProcessTimes
	FILETIME create, exit, kernel, user;
	int success=GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
	if(success)
//#ifdef PROFILER_CYCLES
	{
		const auto hns2ms=100e-9*1000.;
		return hns2ms*(unsigned long long&)user;
	//	return hns2ms*(unsigned long long&)kernel;
	}
//#else
//	{
//		SYSTEMTIME t;
//		success=FileTimeToSystemTime(&user, &t);
//		if(success)
//			return t.wHour*3600000.+t.wMinute*60000.+t.wSecond*1000.+t.wMilliseconds;
//		//	return t.wHour*3600.+t.wMinute*60.+t.wSecond+t.wMilliseconds*0.001;
//	}
//#endif
	SYS_CHECK();
	return -1;
#elif defined TIMING_USE_GetTickCount
	return (double)GetTickCount();//the number of milliseconds that have elapsed since the system was started
#elif defined TIMING_USE_timeGetTime
	return (double)timeGetTime();//system time, in milliseconds
#endif
}
inline double		elapsed_ms(double &calltime)//since last call
{
	double t0=calltime;
	calltime=time_ms();
	return calltime-t0;

	//static double t1=0;
	//double t2=time_ms(), diff=t2-t1;
	//t1=t2;
	//return diff;
}
inline double		elapsed_cycles(long long &calltime)//since last call
{
	long long t0=calltime;
	calltime=__rdtsc();
	return double(calltime-t0);

	//static long long t1=0;
	//long long t2=__rdtsc();
	//double diff=double(t2-t1);
	//t1=t2;
	//return diff;
}

extern int			prof_on;
void				prof_toggle();

//void				prof_start();
void				prof_add(const char *label, int divisor=1);
void				prof_sum(const char *label, int count);//add the sum of last 'count' steps
void				prof_loop_start(const char **labels, int n);//describe the loop body parts in 'labels'
void				prof_add_loop(int idx);//call on each part of loop body
#ifdef PROFILER_SCREEN
#define		PROF_PRINT_ARGS	HDC hDC, int xlabels, int xnumbers
#else
#define		PROF_PRINT_ARGS
#endif
void				prof_print(PROF_PRINT_ARGS);
void				prof_print_in_title();
#endif