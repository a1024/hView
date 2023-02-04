//h_file.cpp - all file operations
//Copyright (C) 2022  Ayman Wagih Mohsen, unless source link provided
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

#define _CRT_SECURE_NO_WARNINGS
#include		"generic.h"
#include		"hview.h"
#include		"huff.h"
#include		<stdint.h>
#include		<vector>
#include		<string>
#include		<fstream>
#include		<sstream>
#include		<assert.h>
#include		<sys/stat.h>

#define STRICT_TYPED_ITEMIDS
#include		<shlobj.h>
#include		<objbase.h>      // For COM headers
#include		<shobjidl.h>     // for IFileDialogEvents and IFileDialogControlEvents
#include		<shlwapi.h>
#include		<knownfolders.h> // for KnownFolder APIs/datatypes/function headers
#include		<propvarutil.h>  // for PROPVAR-related functions
#include		<propkey.h>      // for the Property key APIs/datatypes
#include		<propidl.h>      // for the Property System APIs
#include		<strsafe.h>      // for StringCchPrintfW
#include		<shtypes.h>      // for COMDLG_FILTERSPEC
#include		<new>

#define			STBI_WINDOWS_UTF8
#define			STB_IMAGE_IMPLEMENTATION
#include		"stb_image.h"
//#define		TINY_DNG_LOADER_IMPLEMENTATION//was replaced by libraw
//#include		"tiny_dng_loader.h"
#include		"lodepng.h"
#ifdef HVIEW_INCLUDE_SAIL
#include		<sail/sail.h>
#pragma			comment(lib, "sail.lib")
#pragma			comment(lib, "sail-c++.lib")
#pragma			comment(lib, "sail-codecs.lib")
#pragma			comment(lib, "sail-common.lib")
#pragma			comment(lib, "sail-manip.lib")
#endif
#ifdef HVIEW_INCLUDE_LIBHEIF
#include		<libheif/heif.h>
#pragma			comment(lib, "libheif-1.lib")
#pragma			comment(lib, "liblibde265.lib")
#endif
#ifdef HVIEW_INCLUDE_LIBJXL
#include		<jxl/decode.h>
#include		<jxl/encode.h>
#include		<jxl/thread_parallel_runner.h>
#pragma			comment(lib, "libjxl.lib")
#pragma			comment(lib, "libjxl_threads.lib")
#endif
#ifdef HVIEW_INCLUDE_LIBTIFF
#include		<libtiff/tiffio.h>
#pragma			comment(lib, "libtiff-5.lib")
#endif
#ifdef HVIEW_INCLUDE_LIBBPG
extern "C"
{
#include		<libbpgdec.h>
}
#include		"libfree.h"
#pragma			comment(lib, "bpgdec.lib")
#pragma			comment(lib, "libfree.lib")
//extern "C"
//{
//#include		<libbpg.h>
//}
//#pragma		comment(lib, "bpg.lib")
#endif
#ifdef HVIEW_INCLUDE_LIBRAW
#include		<libraw/libraw.h>
#pragma			comment(lib, "libraw.lib")
#endif
#ifdef HVIEW_INCLUDE_LIBCFITSIO
#include		<fitsio.h>
#pragma			comment(lib, "libcfitsio.lib")
#endif
#define			QOI_IMPLEMENTATION
#include		"qoi.h"
#define			SANS_IMPLEMENTATION
#include		"sans.h"
const char		file[]=__FILE__;

const wchar_t	doublequote=L'\"';
void			assign_path(std::wstring const &text, int start, int end, std::wstring &pathret)//pathret can be text
{
	for(;end>0&&(text[end-1]==' '||text[end-1]=='\t'||text[end-1]=='\r'||text[end-1]=='\n');--end);//skip trailing whitespace
	start+=text[start]==doublequote, end-=text[end-1]==doublequote;
	assert(start<end);
	std::wstring temp(text.begin()+start, text.begin()+end);
	int size=(int)temp.size();
	for(int k=0;k<size;++k)
	{
		if(temp[k]==L'\\')
			temp[k]=L'/';
	}
	if(temp[size-1]=='/')
		temp.pop_back();
	pathret=std::move(temp);
}
void			assign_path(std::string const &text, int start, int end, std::wstring &pathret)//pathret can be text
{
	for(;end>0&&(text[end-1]==' '||text[end-1]=='\t'||text[end-1]=='\r'||text[end-1]=='\n');--end);//skip trailing whitespace
	start+=text[start]==doublequote, end-=text[end-1]==doublequote;
	assert(start<end);
	std::wstring temp(text.begin()+start, text.begin()+end);
	int size=(int)temp.size();
	for(int k=0;k<size;++k)
	{
		if(temp[k]==L'\\')
			temp[k]=L'/';
	}
	if(temp[size-1]=='/')
		temp.pop_back();
	pathret=std::move(temp);
}
void			assign_path(std::string const &text, int start, int end, std::string &pathret)//pathret can be text
{
	assert(text.size());
	if(end>=(int)text.size())
		end=(int)text.size();
	for(;end>0&&(text[end-1]==' '||text[end-1]=='\t'||text[end-1]=='\r'||text[end-1]=='\n');--end);//skip trailing whitespace
	start+=text[start]==doublequote, end-=text[end-1]==doublequote;
	assert(start<end);
	std::string temp(text.begin()+start, text.begin()+end);
	int size=(int)temp.size();
	for(int k=0;k<size;++k)
	{
		if(temp[k]==L'\\')
			temp[k]=L'/';
	}
	if(temp[size-1]=='/')
		temp.pop_back();
	pathret=std::move(temp);
}
void			split_filename(std::wstring const &filename, std::wstring &folder, std::wstring &title)//folder ends with slash
{
	std::wstring filteredfilename;
	assign_path(filename, 0, (int)filename.size(), filteredfilename);

	int start=-1;
	for(int k=(int)filteredfilename.size()-1;k>=0;--k)
	{
		if(filteredfilename[k]==L'/'||filteredfilename[k]==L'\\')
		{
			start=k+1;
			break;
		}
	}
	if(start==-1)
		start=0;//unreachable
	folder.assign(filteredfilename.begin(), filteredfilename.begin()+start);
	//if(folder.size()&&*folder.rbegin()=='/')
	//	folder.pop_back();
	title.assign(filteredfilename.begin()+start, filteredfilename.end());
}
void			get_extension(std::wstring const &filename, std::wstring &extension)
{
	int k=(int)filename.size()-1;
	bool has_ext=true;
	for(;k>=0&&filename[k]!=L'.';--k)
	{
		if(filename[k]==L'/')
		{
			has_ext=false;
			break;
		}
	}
	if(has_ext&&k>=0)
		extension.assign(filename.begin()+k+1, filename.end());
}

int				wcscmp_ci(const wchar_t *s1, const wchar_t *s2)//case-insensitive only for ASCII
{
	for(;*s1&&*s2&&tolower(*s1)==tolower(*s2);++s1, ++s2);
	int result=*s1-*s2;
	return (result>0)-(result<0);
}

#define			EXT_LIST	\
	EXT(JPG) EXT(JPEG)\
	EXT(PNG)\
	EXT(BMP) EXT(DIB)\
	EXT(GIF)\
	EXT(TIF) EXT(TIFF)\
	EXT(JP2) EXT(J2K)\
	EXT(WEBP)\
	EXT(AVIF)\
	EXT(HEIC)\
	EXT(BPG)\
	EXT(JXL)\
	EXT(CRW) EXT(CR2) EXT(NEF) EXT(REF) EXT(DNG) EXT(MOS) EXT(KDC) EXT(DCR)\
	EXT(FITS)\
	EXT(QOI)\
	EXT(HUF)
enum			Extension
{
#define EXT(LABEL)	EXT_##LABEL,
	EXT_LIST
#undef	EXT
	EXT_COUNT,
};
const wchar_t	*supported_ext[]=
{
#define EXT(LABEL)	L###LABEL,
	EXT_LIST
#undef	EXT
};
inline int		enumerate_extension(std::wstring const &ext)
{
	for(int k=0;k<EXT_COUNT;++k)
		if(!wcscmp_ci(ext.c_str(), supported_ext[k]))
			return k;
	return EXT_COUNT;
}

#if !defined __linux__
//#if _MSC_VER<1800
//#define	S_IFMT		00170000//octal
//#define	S_IFREG		 0100000
//#endif
#define	S_ISREG(m)	(((m)&S_IFMT)==S_IFREG)
#endif
int				file_is_readablea(const char *filename)//0: not readable, 1: regular file, 2: folder
{
	struct stat info;
	int error=stat(filename, &info);
	if(!error)
		return 1+!S_ISREG(info.st_mode);
	return 0;
}
int				file_is_readablew(const wchar_t *filename)//0: not readable, 1: regular file, 2: folder
{
	struct _stat32 info;
	int error=_wstat32(filename, &info);
	if(!error)
		return 1+!S_ISREG(info.st_mode);
	return 0;
}
long			file_sizew(const wchar_t *filename)
{
	struct _stat32 info;
	int error=_wstat32(filename, &info);
	if(!error)
		return info.st_size;
	return -1;
}
void			read_binary(const wchar_t *filename, std::vector<byte> &binary_data)
{
	std::ifstream input(filename, std::ios::binary);
	binary_data.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	input.close();
}

#ifdef HVIEW_INCLUDE_SAIL
const char*		sail_error2str(int e)
{
	const char *a="<Unknown error>";
	switch(e)
	{
#define	CASE(LABEL)	case LABEL:a=#LABEL;break;
		CASE(SAIL_OK)
		CASE(SAIL_ERROR_MEMORY_ALLOCATION)
		CASE(SAIL_ERROR_OPEN_FILE)
		CASE(SAIL_ERROR_READ_FILE)
		CASE(SAIL_ERROR_SEEK_FILE)
		CASE(SAIL_ERROR_CLOSE_FILE)
		CASE(SAIL_ERROR_LIST_DIR)
		CASE(SAIL_ERROR_PARSE_FILE)
		CASE(SAIL_ERROR_INVALID_ARGUMENT)
		CASE(SAIL_ERROR_READ_IO)
		CASE(SAIL_ERROR_WRITE_IO)
		CASE(SAIL_ERROR_FLUSH_IO)
		CASE(SAIL_ERROR_SEEK_IO)
		CASE(SAIL_ERROR_TELL_IO)
		CASE(SAIL_ERROR_CLOSE_IO)
		CASE(SAIL_ERROR_EOF)
		CASE(SAIL_ERROR_NOT_IMPLEMENTED)
		CASE(SAIL_ERROR_UNSUPPORTED_SEEK_WHENCE)
		CASE(SAIL_ERROR_EMPTY_STRING)
		CASE(SAIL_ERROR_NULL_PTR)
		CASE(SAIL_ERROR_INVALID_IO)
		CASE(SAIL_ERROR_INCORRECT_IMAGE_DIMENSIONS)
		CASE(SAIL_ERROR_UNSUPPORTED_PIXEL_FORMAT)
		CASE(SAIL_ERROR_INVALID_PIXEL_FORMAT)
		CASE(SAIL_ERROR_UNSUPPORTED_COMPRESSION)
		CASE(SAIL_ERROR_UNSUPPORTED_META_DATA)
		CASE(SAIL_ERROR_UNDERLYING_CODEC)
		CASE(SAIL_ERROR_NO_MORE_FRAMES)
		CASE(SAIL_ERROR_INTERLACING_UNSUPPORTED)
		CASE(SAIL_ERROR_INCORRECT_BYTES_PER_LINE)
		CASE(SAIL_ERROR_UNSUPPORTED_IMAGE_PROPERTY)
		CASE(SAIL_ERROR_UNSUPPORTED_BIT_DEPTH)
		CASE(SAIL_ERROR_MISSING_PALETTE)
		CASE(SAIL_ERROR_UNSUPPORTED_FORMAT)
		CASE(SAIL_ERROR_BROKEN_IMAGE)
		CASE(SAIL_ERROR_CODEC_LOAD)
		CASE(SAIL_ERROR_CODEC_NOT_FOUND)
		CASE(SAIL_ERROR_UNSUPPORTED_CODEC_LAYOUT)
		CASE(SAIL_ERROR_CODEC_SYMBOL_RESOLVE)
		CASE(SAIL_ERROR_INCOMPLETE_CODEC_INFO)
		CASE(SAIL_ERROR_UNSUPPORTED_CODEC_FEATURE)
		CASE(SAIL_ERROR_UNSUPPORTED_CODEC_PRIORITY)
		CASE(SAIL_ERROR_ENV_UPDATE)
		CASE(SAIL_ERROR_CONTEXT_UNINITIALIZED)
		CASE(SAIL_ERROR_GET_DLL_PATH)
		CASE(SAIL_ERROR_CONFLICTING_OPERATION)
#undef	CASE
	}
	return a;
}
#define			SAIL_CHECK(ERROR)		(!(ERROR)||log_error(file, __LINE__, sail_error2str(ERROR)))
#endif
#ifdef HVIEW_INCLUDE_LIBHEIF
#define			LIBHEIF_CHECK(ERROR)	(!(ERROR).code||log_error(file, __LINE__, (ERROR).message))
#endif
#ifdef HVIEW_INCLUDE_LIBJXL
const char*		libjxldec_err2str(int e)
{
	const char *a="<Unknown error>";
	switch(e)
	{
#define	CASE(LABEL)	case LABEL:a=#LABEL;break;
		CASE(JXL_DEC_SUCCESS)
		CASE(JXL_DEC_ERROR)
		CASE(JXL_DEC_NEED_MORE_INPUT)
		CASE(JXL_DEC_NEED_PREVIEW_OUT_BUFFER)
		CASE(JXL_DEC_NEED_DC_OUT_BUFFER)
		CASE(JXL_DEC_NEED_IMAGE_OUT_BUFFER)
		CASE(JXL_DEC_JPEG_NEED_MORE_OUTPUT)
		CASE(JXL_DEC_BOX_NEED_MORE_OUTPUT)
		CASE(JXL_DEC_BASIC_INFO)
		CASE(JXL_DEC_EXTENSIONS)
		CASE(JXL_DEC_COLOR_ENCODING)
		CASE(JXL_DEC_PREVIEW_IMAGE)
		CASE(JXL_DEC_FRAME)
		CASE(JXL_DEC_DC_IMAGE)
		CASE(JXL_DEC_FULL_IMAGE)
		CASE(JXL_DEC_JPEG_RECONSTRUCTION)
		CASE(JXL_DEC_BOX)
		CASE(JXL_DEC_FRAME_PROGRESSION)
#undef	CASE
	}
	return a;
}
const char*		libjxlenc_err2str(int e)
{
	const char *a="<Unknown error>";
	switch(e)
	{
#define	CASE(LABEL)	case LABEL:a=#LABEL;break;
		CASE(JXL_ENC_SUCCESS)
		CASE(JXL_ENC_ERROR)
		CASE(JXL_ENC_NEED_MORE_OUTPUT)
		CASE(JXL_ENC_NOT_SUPPORTED)
#undef	CASE
	}
	return a;
}
#define			JXLDEC_CHECK(ERROR)		(!(ERROR)||log_error(file, __LINE__, "JPEG XL library: %s", libjxldec_err2str(ERROR)))
#define			JXLENC_CHECK(ERROR)		(!(ERROR)||log_error(file, __LINE__, "JPEG XL library: %s", libjxlenc_err2str(ERROR)))
#endif
#ifdef HVIEW_INCLUDE_LIBTIFF
#define			LIBTIFF_CHECK(RESULT)	((RESULT)==1||log_error(file, __LINE__, "TIFF library: %d", RESULT))
#endif
#ifdef HVIEW_INCLUDE_LIBRAW
#if 0
const char*		libraw_err2str(int e)
{
	const char *a="<Unknown error>";
	switch(e)
	{
#define	CASE(LABEL)	case LABEL:a=#LABEL;break;
		CASE(LIBRAW_SUCCESS)
		CASE(LIBRAW_UNSPECIFIED_ERROR)
		CASE(LIBRAW_FILE_UNSUPPORTED)
		CASE(LIBRAW_REQUEST_FOR_NONEXISTENT_IMAGE)
		CASE(LIBRAW_OUT_OF_ORDER_CALL)
		CASE(LIBRAW_NO_THUMBNAIL)
		CASE(LIBRAW_UNSUPPORTED_THUMBNAIL)
		CASE(LIBRAW_INPUT_CLOSED)
		CASE(LIBRAW_NOT_IMPLEMENTED)
		CASE(LIBRAW_UNSUFFICIENT_MEMORY)
		CASE(LIBRAW_DATA_ERROR)
		CASE(LIBRAW_IO_ERROR)
		CASE(LIBRAW_CANCELLED_BY_CALLBACK)
		CASE(LIBRAW_BAD_CROP)
		CASE(LIBRAW_TOO_BIG)
		CASE(LIBRAW_MEMPOOL_OVERFLOW)
#undef	CASE
	}
	return a;
}
#endif
#define			LIBRAW_CHECK(ERROR)		((ERROR)==LIBRAW_SUCCESS||log_error(file, __LINE__, "Libraw error %d: %s", ERROR, libraw_strerror(ERROR)))
#endif
#ifdef HVIEW_INCLUDE_LIBCFITSIO//https://stackoverflow.com/questions/5419356/redirect-stdout-stderr-to-a-string
int				fits_check(int status)
{
	if(status)
	{
		console_start_good();
		fits_report_error(stderr, status);
		console_pause();
		console_end();
		return 1;
	}
	return 0;
}
#if 0
#ifdef _MSC_VER
#include <io.h>
#define popen _popen 
#define pclose _pclose
#define stat _stat 
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define close _close
#define pipe _pipe
#define read _read
#define eof _eof
#else
#include <unistd.h>
#endif
#include <fcntl.h>
#include <stdio.h>
#include <mutex>

class StdCapture
{
public:
    static void Init()
    {
        // make stdout & stderr streams unbuffered
        // so that we don't need to flush the streams
        // before capture and after capture 
        // (fflush can cause a deadlock if the stream is currently being 
        std::lock_guard<std::mutex> lock(m_mutex);
        setvbuf(stdout,NULL,_IONBF,0);
        setvbuf(stderr,NULL,_IONBF,0);
    }

    static void BeginCapture()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_capturing)
            return;

        secure_pipe(m_pipe);
        m_oldStdOut = secure_dup(STD_OUT_FD);
        m_oldStdErr = secure_dup(STD_ERR_FD);
        secure_dup2(m_pipe[WRITE],STD_OUT_FD);
        secure_dup2(m_pipe[WRITE],STD_ERR_FD);
        m_capturing = true;
#ifndef _MSC_VER
        secure_close(m_pipe[WRITE]);
#endif
    }
    static bool IsCapturing()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_capturing;
    }
    static bool EndCapture()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_capturing)
            return;

        m_captured.clear();
        secure_dup2(m_oldStdOut, STD_OUT_FD);
        secure_dup2(m_oldStdErr, STD_ERR_FD);

        const int bufSize = 1025;
        char buf[bufSize];
        int bytesRead = 0;
        bool fd_blocked(false);
        do
        {
            bytesRead = 0;
            fd_blocked = false;
#ifdef _MSC_VER
            if (!eof(m_pipe[READ]))
                bytesRead = read(m_pipe[READ], buf, bufSize-1);
#else
            bytesRead = read(m_pipe[READ], buf, bufSize-1);
#endif
            if (bytesRead > 0)
            {
                buf[bytesRead] = 0;
                m_captured += buf;
            }
            else if (bytesRead < 0)
            {
                fd_blocked = (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
                if (fd_blocked)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        while(fd_blocked || bytesRead == (bufSize-1));

        secure_close(m_oldStdOut);
        secure_close(m_oldStdErr);
        secure_close(m_pipe[READ]);
#ifdef _MSC_VER
        secure_close(m_pipe[WRITE]);
#endif
        m_capturing = false;
    }
    static std::string GetCapture()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_captured;
    }
private:
    enum PIPES { READ, WRITE };

    int StdCapture::secure_dup(int src)
    {
        int ret = -1;
        bool fd_blocked = false;
        do
        {
             ret = dup(src);
             fd_blocked = (errno == EINTR ||  errno == EBUSY);
             if (fd_blocked)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        while (ret < 0);
        return ret;
    }
    void StdCapture::secure_pipe(int * pipes)
    {
        int ret = -1;
        bool fd_blocked = false;
        do
        {
#ifdef _MSC_VER
            ret = pipe(pipes, 65536, O_BINARY);
#else
            ret = pipe(pipes) == -1;
#endif
            fd_blocked = (errno == EINTR ||  errno == EBUSY);
            if (fd_blocked)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        while (ret < 0);
    }
    void StdCapture::secure_dup2(int src, int dest)
    {
        int ret = -1;
        bool fd_blocked = false;
        do
        {
             ret = dup2(src,dest);
             fd_blocked = (errno == EINTR ||  errno == EBUSY);
             if (fd_blocked)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        while (ret < 0);
    }

    void StdCapture::secure_close(int & fd)
    {
        int ret = -1;
        bool fd_blocked = false;
        do
        {
             ret = close(fd);
             fd_blocked = (errno == EINTR);
             if (fd_blocked)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        while (ret < 0);

        fd = -1;
    }

    static int m_pipe[2];
    static int m_oldStdOut;
    static int m_oldStdErr;
    static bool m_capturing;
    static std::mutex m_mutex;
    static std::string m_captured;
};

// actually define vars.
int StdCapture::m_pipe[2];
int StdCapture::m_oldStdOut;
int StdCapture::m_oldStdErr;
bool StdCapture::m_capturing;
std::mutex StdCapture::m_mutex;
std::string StdCapture::m_captured;
#endif
#endif
void			assign_from_gray8(const void *src, int iw2, int ih2)
{
	iw=iw2, ih=ih2, image_size=iw*ih;
	auto buf=realloc(image, image_size*sizeof(float));
	if(!buf)
	{
		LOG_ERROR("realloc returned null");
		return;
	}
	image=(float*)buf;
	float inv255=1.f/255;
	for(int k=0;k<image_size;++k)
	{
		auto p=(unsigned char*)src+k;
		image[k]=p[0]*inv255;
	}
	idepth=8;
	imagetype=IM_GRAYSCALE;
}
void			assign_from_gray16(const void *src, int iw2, int ih2)
{
	iw=iw2, ih=ih2, image_size=iw*ih;
	auto buf=realloc(image, image_size*sizeof(float));
	if(!buf)
	{
		LOG_ERROR("realloc returned null");
		return;
	}
	image=(float*)buf;
	float inv=1.f/65535;
	for(int ks=0, kd=0;kd<image_size;ks+=2, ++kd)
	{
		auto p=(unsigned char*)src+ks;
		image[kd]=(p[0]<<8|p[1])*inv;
	}
	idepth=16;
	imagetype=IM_GRAYSCALE;
}
void			assign_from_RGB8(const void *src, int iw2, int ih2)
{
	iw=iw2, ih=ih2, image_size=iw*ih;
	auto buf=realloc(image, image_size*4*sizeof(float));
	if(!buf)
	{
		LOG_ERROR("realloc returned null");
		return;
	}
	image=(float*)buf;
	float inv255=1.f/255;
	long long srcsize=image_size*3;
	for(int ks=0, kd=0;ks<srcsize;ks+=3, kd+=4)
	{
		auto p=(unsigned char*)src+ks;
		image[kd  ]=p[0]*inv255;
		image[kd+1]=p[1]*inv255;
		image[kd+2]=p[2]*inv255;
		image[kd+3]=1;
	}
	idepth=8;
	imagetype=IM_RGBA;
}
void			assign_from_RGBA8(const int *src, int iw2, int ih2)
{
	iw=iw2, ih=ih2, image_size=iw*ih;
	auto buf=realloc(image, image_size*4*sizeof(float));
	if(!buf)
	{
		LOG_ERROR("realloc returned null");
		return;
	}
	image=(float*)buf;
	float inv255=1.f/255;
	for(int ks=0, kd=0;ks<image_size;++ks, kd+=4)
	{
		auto p=(unsigned char*)(src+ks);
		image[kd  ]=p[0]*inv255;
		image[kd+1]=p[1]*inv255;
		image[kd+2]=p[2]*inv255;
		image[kd+3]=p[3]*inv255;
	}
	idepth=8;
	imagetype=IM_RGBA;
}
void			assign_from_RGB16(const void *src, int iw2, int ih2)
{
	iw=iw2, ih=ih2, image_size=iw*ih;
	auto buf=realloc(image, image_size*4*sizeof(float));
	if(!buf)
	{
		LOG_ERROR("realloc returned null");
		return;
	}
	image=(float*)buf;
	float gain=1.f/65535;
	long long srcsize=image_size*6;
	for(int ks=0, kd=0;ks<srcsize;ks+=6, kd+=4)
	{
		auto p=(unsigned char*)src+ks;
		image[kd  ]=(p[0]<<8|p[1])*gain;
		image[kd+1]=(p[2]<<8|p[3])*gain;
		image[kd+2]=(p[4]<<8|p[5])*gain;
		image[kd+3]=1;
	}
	idepth=16;
	imagetype=IM_RGBA;
}
void			assign_from_RGBA16(const void *src, int iw2, int ih2)
{
	iw=iw2, ih=ih2, image_size=iw*ih;
	auto buf=realloc(image, image_size*4*sizeof(float));
	if(!buf)
	{
		LOG_ERROR("realloc returned null");
		return;
	}
	image=(float*)buf;
	float gain=1.f/65535;
	long long srcsize=image_size<<3;
	for(int ks=0, kd=0;ks<srcsize;ks+=8, kd+=4)
	{
		auto p=(unsigned char*)src+ks;
		image[kd  ]=(p[0]<<8|p[1])*gain;
		image[kd+1]=(p[2]<<8|p[3])*gain;
		image[kd+2]=(p[4]<<8|p[5])*gain;
		image[kd+3]=(p[6]<<8|p[7])*gain;
	}
	idepth=16;
	imagetype=IM_RGBA;
}
bool			open_mediaw(const wchar_t *filename)//if successful: sets workfolder, updates title
{
	std::wstring wfn=filename, folder, title, extension;
	split_filename(filename, folder, title);
	get_extension(title, extension);
	
	//check extension
	int ext_idx=enumerate_extension(extension);
	switch(ext_idx)
	{
	case EXT_HUF://compressed raw
		{
			std::vector<byte> data;
			read_binary(filename, data);
			if(!data.size())//file may not exist
				return false;
#ifdef BENCHMARK
			long long t1=__rdtsc();
#endif
			auto success=huff::decompress(data.data(), (int)data.size(), RF_F32_BAYER, (void**)&image, iw, ih, idepth, bayer);
			//auto success=decompress_huff(data.data(), data.size(), RF_F32_BAYER, (void**)&image, iw, ih, idepth, bayer);
			if(!success)
				return false;
#ifdef BENCHMARK
			long long t2=__rdtsc();
			LOG_ERROR("HUF: %lld cycles", t2-t1);
#endif
			image_size=iw*ih;
			if(bayer[0]==-1)
				imagetype=IM_GRAYSCALE;
			else
				imagetype=IM_BAYER;
		}
		break;
#ifdef HVIEW_INCLUDE_LIBJXL
	case EXT_JXL:
		{
			std::vector<byte> data;
			read_binary(filename, data);
			if(!data.size())//file may not exist
				return false;

			//int version=JxlDecoderVersion();//7000

			JxlDecoder *decoder=JxlDecoderCreate(nullptr);		GEN_ASSERT(decoder);//https://github.com/alistair7/imlib2-jxl/blob/main/imlib2-jxl.c
			if(!decoder)
				return false;
			JxlDecoderStatus result=JXL_DEC_SUCCESS;

			void *runner=JxlThreadParallelRunnerCreate(nullptr, JxlThreadParallelRunnerDefaultNumWorkerThreads());	GEN_ASSERT(runner);
			result=JxlDecoderSetParallelRunner(decoder, JxlThreadParallelRunner, runner);		JXLDEC_CHECK(result);
			result=JxlDecoderSubscribeEvents(decoder, JXL_DEC_BASIC_INFO|JXL_DEC_FULL_IMAGE);	JXLDEC_CHECK(result);
			
#ifdef BENCHMARK
			long long t1=__rdtsc();
#endif
			result=JxlDecoderSetInput(decoder, data.data(), data.size());	JXLDEC_CHECK(result);
			JxlBasicInfo info={};
			JxlPixelFormat format={4, JXL_TYPE_UINT8, JXL_LITTLE_ENDIAN, 0};
#if 0
			JxlColorEncoding color_encoding={};
			unsigned char *icc_blob=nullptr;
			size_t icc_size=0;
#endif
			size_t imsize=0;
			int *buffer=nullptr;
			while((result=JxlDecoderProcessInput(decoder))!=JXL_DEC_FULL_IMAGE)
			{
				switch(result)
				{
				case JXL_DEC_BASIC_INFO:
					result=JxlDecoderGetBasicInfo(decoder, &info);			JXLDEC_CHECK(result);
					continue;
				case JXL_DEC_COLOR_ENCODING:
#if 0
					result=JxlDecoderGetColorAsEncodedProfile(decoder, &format, JXL_COLOR_PROFILE_TARGET_DATA, &color_encoding);
					result=JxlDecoderGetICCProfileSize(decoder, &format, JXL_COLOR_PROFILE_TARGET_DATA, &icc_size);
					icc_blob=new unsigned char[icc_size];
					result=JxlDecoderGetColorAsICCProfile(decoder, &format, JXL_COLOR_PROFILE_TARGET_DATA, icc_blob, icc_size);
#endif
					continue;
				case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
					{
						if(buffer)
						{
							LOG_ERROR("Out of buffer again, multiple frames are not supported yet");
							continue;
						}
						result=JxlDecoderImageOutBufferSize(decoder, &format, &imsize);			JXLDEC_CHECK(result);
						GEN_ASSERT(imsize==info.xsize*info.ysize*format.num_channels);
						buffer=new int[info.xsize*info.ysize];
						result=JxlDecoderSetImageOutBuffer(decoder, &format, buffer, imsize);	JXLDEC_CHECK(result);
					}
					continue;
				case JXL_DEC_NEED_MORE_INPUT:
					LOG_ERROR("File is truncated");
					break;
				case JXL_DEC_ERROR:
					{
						JxlSignature sig=JxlSignatureCheck((uint8_t*)data.data(), data.size());
						const char *a="<UNKNOWN ERROR>";
						switch(sig)
						{
#define					CASE(LABEL)		case LABEL:a=#LABEL;break;
						CASE(JXL_SIG_NOT_ENOUGH_BYTES)
						CASE(JXL_SIG_INVALID)
						CASE(JXL_SIG_CODESTREAM)
						CASE(JXL_SIG_CONTAINER)
#undef					CASE
						}
						LOG_ERROR("JPEG XL library: %s", a);
					}
					break;
				default:
					LOG_ERROR("JPEG XL library returned %s: %s", result, libjxldec_err2str(result));
					break;
				}
			}
#ifdef BENCHMARK
			long long t2=__rdtsc();
			LOG_ERROR("JXL: %lld cycles", t2-t1);
#endif
			assign_from_RGBA8((int*)buffer, info.xsize, info.ysize);
			if(buffer)
				delete[] buffer;
#if 0
			if(icc_blob)
				delete[] icc_blob;
#endif
			//auto remaining=JxlDecoderReleaseInput(decoder);
			JxlDecoderDestroy(decoder);
		}
		break;
#endif
#ifdef HVIEW_INCLUDE_LIBHEIF
	case EXT_HEIC:
		{
			stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);

			heif_context *ctx=heif_context_alloc();
#ifdef BENCHMARK
			long long t1=__rdtsc();
#endif
			heif_error error=heif_context_read_from_file(ctx, g_buf, nullptr);	LIBHEIF_CHECK(error);//TODO: file may not exist

			heif_image_handle *handle=nullptr;
			error=heif_context_get_primary_image_handle(ctx, &handle);			LIBHEIF_CHECK(error);//get a handle to the primary image

			heif_context_free(ctx);

			int iw2=heif_image_handle_get_width(handle), ih2=heif_image_handle_get_height(handle);

			heif_image *img=nullptr;
			error=heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);	LIBHEIF_CHECK(error);
			//error=heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);//decode the image and convert colorspace to RGB, saved as 24bit interleaved
			if(!img)
				return false;

			int stride=4;
			const uint8_t *data=heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);	LIBHEIF_CHECK(error);
#ifdef BENCHMARK
			long long t2=__rdtsc();
			LOG_ERROR("HEIC: %lld cycles", t2-t1);
#endif
			assign_from_RGBA8((int*)data, iw2, ih2);

			heif_image_release(img);
			heif_image_handle_release(handle);
		}
		break;
#endif
#ifdef HVIEW_INCLUDE_LIBBPG
	case EXT_BPG:
		{
#if 1
			stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);
#ifdef BENCHMARK
			long long t1=__rdtsc();
#endif
			int iw2=0, ih2=0;
			auto original_image=bpg_to_rgba(g_buf, &iw2, &ih2);
			if(!original_image)
				return false;
#ifdef BENCHMARK
			long long t2=__rdtsc();
			LOG_ERROR("BPG: %lld cycles", t2-t1);
#endif
			
			assign_from_RGBA8((int*)original_image, iw2, ih2);
			gcc_free_memory(original_image);
#endif
#if 0
#define		LIBBPG_ASSERT(ERROR)	(!(ERROR)||log_error(file, __LINE__, "BPG library: %d", ERROR))
			std::vector<byte> data;
			read_binary(filename, data);
			if(!data.size())//file may not exist
				return false;

			BPGDecoderContext *decoder=bpg_decoder_open();
			int error=bpg_decoder_decode(decoder, data.data(), (int)data.size());	LIBBPG_ASSERT(error);
			BPGImageInfo info={};
			error=bpg_decoder_get_info(decoder, &info);		LIBBPG_ASSERT(error);

			iw=info.width, ih=info.height, image_size=iw*ih;
			idepth=info.bit_depth;
			image=(float*)realloc(image, image_size*sizeof(float)<<2);
			int count=iw<<2;
			if(info.bit_depth>8)
			{
				bpg_decoder_start(decoder, BPG_OUTPUT_FORMAT_RGBA64);
				auto buffer=new unsigned short[count];
				float gain=1.f/((1<<idepth)-1);
				for(int ky=0;ky<ih;++ky)
				{
					error=bpg_decoder_get_line(decoder, buffer);	LIBBPG_ASSERT(error);
					for(int kx=0;kx<count;++kx)
						image[count*ky+kx]=gain*buffer[kx];
				}
				delete[] buffer;
			}
			else
			{
				bpg_decoder_start(decoder, BPG_OUTPUT_FORMAT_RGBA32);
				auto buffer=new unsigned char[count];
				float gain=1.f/((1<<idepth)-1);
				for(int ky=0;ky<ih;++ky)
				{
					error=bpg_decoder_get_line(decoder, buffer);	LIBBPG_ASSERT(error);
					for(int kx=0;kx<count;++kx)
						image[count*ky+kx]=gain*buffer[kx];
				}
				delete[] buffer;
			}
#endif
		}
		break;
#endif
#ifdef HVIEW_INCLUDE_LIBTIFF
	case EXT_TIF:
	case EXT_TIFF:
		{
#ifdef BENCHMARK
			long long t1=__rdtsc();
#endif
			TIFF *tiff=TIFFOpenW(filename, "r");
			if(!tiff)//file may not exist
				return false;
			int iw2=0, ih2=0, idepth2=0;
			int result=TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &iw2);		LIBTIFF_CHECK(result);
			result=TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &ih2);			LIBTIFF_CHECK(result);
			result=TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &idepth2);		LIBTIFF_CHECK(result);
			//int compression=0;
			//result=TIFFGetField(tiff, TIFFTAG_COMPRESSION, &compression);	LIBTIFF_CHECK(result);
			tmsize_t scanlength=TIFFScanlineSize(tiff);

			size_t bufsize=ih2*scanlength;
			auto buffer=new unsigned char[bufsize];
			for(int k=0;k<ih2;++k)
			{
				result=TIFFReadScanline(tiff, buffer+scanlength*k, k);
				if(result<0)
					LOG_ERROR("TIFF library: error reading row %d", k);
			}
#ifdef BENCHMARK
			long long t2=__rdtsc();
			LOG_ERROR("TIFF: %lld cycles", t2-t1);
#endif
			TIFFClose(tiff);
			iw=iw2, ih=ih2, image_size=iw2*ih2, idepth=idepth2;
			imagetype=IM_RGBA;
			image=(float*)realloc(image, image_size*sizeof(float)<<2);
			switch(idepth2)
			{
			case 8:
				{
					float gain=1.f/255;
					for(int ks=0, kd=0;kd<image_size;ks+=3, ++kd)
					{
						int idx=kd<<2;
						image[idx  ]=gain*buffer[ks  ];
						image[idx+1]=gain*buffer[ks+1];
						image[idx+2]=gain*buffer[ks+2];
						image[idx+3]=1;
					}
				}
				break;
			case 16:
				{
					auto b2=(unsigned short*)buffer;
					float gain=1.f/0xFFFF;
					for(int ks=0, kd=0;kd<image_size;ks+=3, ++kd)
					{
						int idx=kd<<2;
						image[idx  ]=gain*b2[ks  ];
						image[idx+1]=gain*b2[ks+1];
						image[idx+2]=gain*b2[ks+2];
						image[idx+3]=1;
					}
				}
				break;
			case 32:
				{
					auto b2=(float*)buffer;
					for(int ks=0, kd=0;kd<image_size;ks+=3, ++kd)
					{
						int idx=kd<<2;
						image[idx  ]=b2[ks  ];
						image[idx+1]=b2[ks+1];
						image[idx+2]=b2[ks+2];
						image[idx+3]=1;
					}
				}
				break;
			}

		/*	size_t bufsize=iw2*(ih2-1)*3+(scanlength>>2);
			auto buffer=new float[bufsize];
			for(int k=0;k<ih2;++k)
			{
				result=TIFFReadScanline(tiff, buffer+3*iw2*k, k);
				if(result<0)
					LOG_ERROR("TIFF library: error reading row %d", k);
			}
			TIFFClose(tiff);

			iw=iw2, ih=ih2, image_size=iw2*ih2, idepth=idepth2;
			imagetype=IM_RGBA;
			image=(float*)realloc(image, image_size*sizeof(float)<<2);
			//memcpy(image, buffer, bufsize*sizeof(float));
			for(int ks=0, kd=0;kd<image_size;ks+=3, ++kd)
			{
				image[(kd<<2)  ]=buffer[ks  ];
				image[(kd<<2)+1]=buffer[ks+1];
				image[(kd<<2)+2]=buffer[ks+2];
				image[(kd<<2)+3]=1;
			}
			delete[] buffer;//*/
		}
		break;
#endif
#ifdef HVIEW_INCLUDE_SAIL
	case EXT_PNG://20220924 16bit PNG support
	case EXT_JP2:case EXT_J2K:
	case EXT_WEBP:
	case EXT_AVIF:
		{
			stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);

			void *state=nullptr;
			struct sail_image *img=nullptr;
#ifdef BENCHMARK
			long long t1=__rdtsc();
#endif
			auto error=sail_start_reading_file(g_buf, nullptr, &state);	SAIL_CHECK(error);
			if(!state)//file may not exist
				return false;
			error=sail_read_next_frame(state, &img);	SAIL_CHECK(error);
#ifdef BENCHMARK
			long long t2=__rdtsc();
			LOG_ERROR("libSAIL: %lld cycles", t2-t1);
#endif
			switch(img->pixel_format)
			{
			case SAIL_PIXEL_FORMAT_BPP8_GRAYSCALE:
				assign_from_gray8(img->pixels, img->width, img->height);
				break;
			case SAIL_PIXEL_FORMAT_BPP16_GRAYSCALE:
				assign_from_gray16(img->pixels, img->width, img->height);
				break;
			case SAIL_PIXEL_FORMAT_BPP24_RGB:
				assign_from_RGB8((int*)img->pixels, img->width, img->height);
				break;
			case SAIL_PIXEL_FORMAT_BPP32_RGBA:
				assign_from_RGBA8((int*)img->pixels, img->width, img->height);
				break;
			case SAIL_PIXEL_FORMAT_BPP48_RGB:
				assign_from_RGB16(img->pixels, img->width, img->height);
				break;
			case SAIL_PIXEL_FORMAT_BPP64_RGBA:
				assign_from_RGBA16(img->pixels, img->width, img->height);
				break;
			default:
				error=sail_stop_reading(state);				SAIL_CHECK(error);
				sail_destroy_image(img);
				goto try_stb;
				//messageboxa(ghWnd, "Error", "Unsupported pixel format from libSAIL: %d", img->pixel_format);
				break;
			}
			error=sail_stop_reading(state);				SAIL_CHECK(error);
			sail_destroy_image(img);

			//struct sail_io *io;
			//auto result=sail_alloc_io_read_file(g_buf, &io);
			//sail_destroy_io(io);
		}
		break;
#endif
#ifdef HVIEW_INCLUDE_LIBRAW
	case EXT_CRW:
	case EXT_CR2:
	case EXT_NEF:
	case EXT_REF:
	case EXT_DNG:
	case EXT_MOS:
	case EXT_KDC:
	case EXT_DCR:
		{
			//stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);

			auto decoder=libraw_init(0);
			if(!decoder)
			{
				LOG_ERROR("Failed to initialize libraw decoder");
				return false;
			}
			int error=libraw_open_wfile(decoder, filename);	LIBRAW_CHECK(error);
			//int error=libraw_open_file(decoder, g_buf);	LIBRAW_CHECK(error);

			error=libraw_unpack(decoder);	LIBRAW_CHECK(error);

			int iw2=decoder->sizes.raw_width;
			int ih2=decoder->sizes.raw_height;
			int count=iw2*ih2;
			auto buf=realloc(image, count*4*sizeof(float));
			if(!buf)
			{
				LOG_ERROR("realloc returned null");
				return false;
			}
			idepth=ceil_log2(decoder->color.maximum);
			iw=iw2, ih=ih2, image_size=count;
			image=(float*)buf;
			char color_sh[]={16, 8, 0, 8};//RGBG
			bayer[0]=color_sh[libraw_COLOR(decoder, 0, 0)];
			bayer[1]=color_sh[libraw_COLOR(decoder, 0, 1)];
			bayer[2]=color_sh[libraw_COLOR(decoder, 1, 0)];
			bayer[3]=color_sh[libraw_COLOR(decoder, 1, 1)];
			auto src=decoder->rawdata.raw_image;
			float gain=1.f/((1<<idepth)-1);
			switch(decoder->sizes.flip)
			{
			case 0:
				for(int k=0;k<image_size;++k)
					image[k]=gain*src[k];
				break;
			case 3://upside-down
				std::swap(bayer[0], bayer[2]);
				std::swap(bayer[1], bayer[3]);
				for(int ky=0;ky<ih;++ky)
				{
					for(int kx=0;kx<iw;++kx)
						image[iw*ky+kx]=gain*src[iw*(ih-1-ky)+kx];
				}
				break;
			case 5://90 degrees CCW
				{
					char temp=bayer[0];
					bayer[0]=bayer[1];
					bayer[1]=bayer[3];
					bayer[3]=bayer[2];
					bayer[2]=temp;
					std::swap(iw, ih);
					for(int ky=0;ky<ih;++ky)
					{
						for(int kx=0;kx<iw;++kx)
							image[iw*ky+kx]=gain*src[ih*kx+ih-1-ky];
					}
				}
				break;
			case 6://90 degrees CW
				{
					char temp=bayer[0];
					bayer[0]=bayer[2];
					bayer[2]=bayer[3];
					bayer[3]=bayer[1];
					bayer[1]=temp;
					std::swap(iw, ih);
					for(int ky=0;ky<ih;++ky)
					{
						for(int kx=0;kx<iw;++kx)
							image[iw*ky+kx]=gain*src[ih*(iw-1-kx)+ky];
					}
				}
				break;
			}
			//for(int ky=0, ks=0;ky<ih;ky+=2)
			//{
			//	auto row=image+iw*ky;
			//	for(int kx=0;kx<iw;kx+=2, ks+=4)
			//	{
			//		row[kx]=gain*src[ks];
			//		row[kx+1]=gain*src[ks+1];
			//		row[kx+iw]=gain*src[ks+2];
			//		row[kx+iw+1]=gain*src[ks+3];
			//	}
			//}
			libraw_free_image(decoder);
			libraw_close(decoder);
			imagetype=IM_BAYER;
		}
		break;
#endif
#ifdef HVIEW_INCLUDE_TINYDNGLOADER
	case EXT_DNG:
		{
			stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);
			std::string warn, err;
			std::vector<tinydng::DNGImage> images;
			std::vector<tinydng::FieldInfo> custom_field_lists;
			tinydng::LoadDNG(g_buf, custom_field_lists, &images, &warn, &err);
			if(err.size())
				LOG_ERROR("DNG loader error: %s", err.c_str());
			if(warn.size())
				LOG_ERROR("DNG loader warning: %s", warn.c_str());
			if(!images.size())
				return false;
			auto &dng=images[0];
			switch(dng.bits_per_sample)
			{
			case 16:
				{
					auto buffer=(unsigned short*)dng.data.data();
					int size=dng.width*dng.height;
					image=(float*)realloc(image, size*sizeof(float));
					float gain=1.f/0xFFFF;
					for(int k=0;k<size;++k)
						image[k]=gain*buffer[k];
				}
				break;
			default:
				LOG_ERROR("Error: bit depth = %d", dng.bits_per_sample);
				return false;
			}
			bayer[0]=(2-dng.cfa_pattern[0][0])<<3, bayer[1]=(2-dng.cfa_pattern[0][1])<<3;
			bayer[2]=(2-dng.cfa_pattern[1][0])<<3, bayer[3]=(2-dng.cfa_pattern[1][1])<<3;
			//memcpy(bayer, dng.cfa_plane_color, 4);
			iw=dng.width, ih=dng.height, image_size=iw*ih;
			idepth=dng.bits_per_sample_original;
			imagetype=IM_BAYER;
		}
		break;
#endif
#ifdef HVIEW_INCLUDE_LIBCFITSIO
	case EXT_FITS:
		{
			fitsfile *f;
			int status, imtype, naxis;
			long dimensions[2], firstpixel[2]={1, 1};
			float null=0;
			//int nkeys;
			std::stringstream buffer;
			
#define		FITS_CHECK()	if(fits_check(status))goto fits_cleanup
//#define	FITS_CHECK()	if(status)fits_report_error(stderr, status)
			status=0;
			stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);
#ifdef BENCHMARK
			long long t1=__rdtsc();
#endif
			fits_open_file(&f, g_buf, READONLY, &status);		FITS_CHECK();

			//console_start();
			//fits_get_hdrspace(f, &nkeys, NULL, &status);		FITS_CHECK();
			//for(int k=1;k<=nkeys;++k)
			//{
			//	fits_read_record(f, k, g_buf, &status);			FITS_CHECK();
			//	printf("%s\n", g_buf);
			//}
			//printf("END\n");

			//fits_get_img_type(f, &imtype, &status);	FITS_CHECK();
			//fits_get_img_dim(f, &naxis, &status);		FITS_CHECK();
			fits_get_img_param(f, 2, &imtype, &naxis, dimensions, &status);
			switch(imtype)
			{
			case BYTE_IMG:		idepth=8;break;
			case SHORT_IMG:		idepth=16;break;
			case LONG_IMG:		idepth=32;break;
			case LONGLONG_IMG:	idepth=64;break;
			case FLOAT_IMG:		idepth=16;break;
			case DOUBLE_IMG:	idepth=32;break;
			}

			long long count=dimensions[0]*dimensions[1];//FIXME support 1D images
			auto buf=realloc(image, count*4*sizeof(float));
			if(!buf)
			{
				LOG_ERROR("realloc returned null");
				return false;
			}
			image=(float*)buf;
			imagetype=IM_GRAYSCALE;
			iw=dimensions[0], ih=dimensions[1], image_size=count;
			fits_read_pix(f, TFLOAT, firstpixel, count, &null, image, 0, &status);	FITS_CHECK();
#ifdef BENCHMARK
			long long t2=__rdtsc();
			LOG_ERROR("FITS: %lld cycles", t2-t1);
#endif
			fits_close_file(f, &status);	FITS_CHECK();
			goto fits_skip;
		fits_cleanup:
			fits_close_file(f, &status);
		fits_skip:
			float gain=(float)(1/(pow(2, idepth)-1));
			for(ptrdiff_t k=0;k<count;++k)
				image[k]*=gain;
		}
		break;
#endif
	case EXT_QOI:
		{
			stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);
			qoi_desc desc={0};
#ifdef BENCHMARK
			long long t1=__rdtsc();
#endif
			void *src=qoi_read(g_buf, &desc);
			if(!src)
				return false;
#ifdef BENCHMARK
			long long t2=__rdtsc();
			LOG_ERROR("QOI: %lld cycles", t2-t1);
#endif
			
			switch(desc.channels)
			{
			case 1:
				assign_from_gray8(src, desc.width, desc.height);
				break;
			case 3:
				assign_from_RGB8(src, desc.width, desc.height);
				break;
			case 4:
				assign_from_RGBA8((int*)src, desc.width, desc.height);
				break;
			default:
				LOG_ERROR("Invalid number of channels %d", desc.channels);
				break;
			}

			free(src);
		}
		break;
	default://ordinary image
		{
		try_stb:
			stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);
			int iw2=0, ih2=0, nch2=0;
#ifdef BENCHMARK
			long long t1=__rdtsc();
#endif
			unsigned char *original_image=stbi_load(g_buf, &iw2, &ih2, &nch2, 4);
#ifdef BENCHMARK
			long long t2=__rdtsc();
			LOG_ERROR("stbi_load(): %lld cycles", t2-t1);
#endif
			if(!original_image)//file may not exist
				return false;
			auto src=(int*)original_image;
		
			assign_from_RGBA8(src, iw2, ih2);

			free(original_image);
		}
	}
	if(histOn)
	{
		toggle_histogram();
		toggle_histogram();
	}

	if(imagecentered&&iw&&ih)
		center_image();
	bitmode=false;
	reset_FFTW_state();
	workfolder=std::move(folder), filetitle=std::move(title);
	SetWindowTextW(ghWnd, (wfn+L" - hView").c_str());
	render();
	return true;
}
int				open_media()
{
#if 0
	if(image&&imagetype==IM_GRAYSCALE)
	{
		static int inv=0;

		//int hist[256]={0};
		//int histhi[16]={0}, histlo[16][16]={0};
		//unsigned char vmax[256]={0};
		size_t imsize=(size_t)iw*ih;
		unsigned char *buffer=new unsigned char[imsize];

		for(int k=0;k<imsize;++k)//quantize image
		{
			float val=255*image[k];
			if(val<0)
				val=0;
			if(val>255)
				val=255;
			buffer[k]=(unsigned char)val;
		}

		if(inv)
			integrate_buffer(buffer, iw, ih);
			//DWT2inv_16x16c(data);
		else
			differentiate_buffer(buffer, iw, ih);
			//DWT2fwd_16x16c(data);

		float gain=1.f/255;
		for(int k=0;k<imsize;++k)//dequantize image
			image[k]=buffer[k]*gain;
#if 0
		__m128i data[16], d2[16];
		unsigned char *p=(unsigned char*)data;
		for(int ky=0;ky<ih;ky+=16)
		{
			for(int kx=0;kx<iw;kx+=16)
			{
				for(int ky2=0;ky2<16;++ky2)//load block
				{
					for(int kx2=0;kx2<16;++kx2)
					{
						float val=255*image[iw*(ky+ky2)+kx+kx2];
						if(val<0)
							val=0;
						if(val>255)
							val=255;
						p[ky2<<4|kx2]=(unsigned char)val;
					}
				}

				if(inv)
					integrate_buffer(p, 16, 16);
					//DWT2inv_16x16c(data);
				else
					differentiate_buffer(p, 16, 16);
					//DWT2fwd_16x16c(data);
#if 0
				//for(int k=0;k<256;++k)//
				//	((unsigned char*)data)[k]=k;

				memcpy(d2, data, 256);
				DWT2fwd_16x16c(data);
				//for(int k=0;k<256;++k)
				//{
				//	unsigned char v=((unsigned char*)data)[k];
				//	++hist[v];
				//	if(vmax[k]<v)
				//		vmax[k]=v;
				//}
				DWT2inv_16x16c(data);

				int broken=0;
				for(int k=0;k<256;++k)
				{
					unsigned char src=((unsigned char*)d2)[k], dst=((unsigned char*)data)[k];
					if(src!=dst)
					{
						LOG_ERROR("[%d %d][%d %d] %d != %d", kx, ky, k&15, k>>4, src, dst);
						broken=1;
						break;
					}
				}
#endif

				float gain=1.f/255;
				for(int ky2=0;ky2<16;++ky2)//store block
				{
					for(int kx2=0;kx2<16;++kx2)
						image[iw*(ky+ky2)+kx+kx2]=p[ky2<<4|kx2]*gain;
				}
#if 0
				if(broken)
				{
					render();
					return 0;
				}
#endif
			}
		}
#endif
		inv=!inv;
		//console_start_good();
		//for(int k=0;k<256;++k)
		//	printf("%3d %4d\n", k, hist[k]);
		//console_pause();
		//LOG_ERROR("Done");
		//for(int k=0;k<imsize;++k)
		//	++hist[buffer[k]];
		//for(int k=0;k<imsize;++k)
		//{
		//	unsigned char v=buffer[k];
		//	++histhi[v>>4];
		//	++histlo[v>>4][v&15];
		//}
		delete[] buffer;

		render();
		return 0;
	}
#endif
#if 0
	{
		qoi_desc desc={32, 2, 1, QOI_LINEAR};
		unsigned char udata[]=
		{
			193, 95, 114, 86, 128, 193, 175, 63, 8, 21, 64, 81, 35, 65, 0, 0,
			193, 95, 114, 86, 128, 193, 175, 63, 8, 21, 64, 81, 35, 65, 0, 0,
			193, 95, 114, 86, 128, 193, 175, 63, 8, 21, 64, 81, 35, 65, 0, 0,
			193, 95, 114, 86, 128, 193, 175, 63, 8, 21, 64, 81, 35, 65, 0, 0,

			//0, 1, 2, 3,
			//4, 5, 6, 7,
			//8, 9, 10, 11,
			//12, 13, 14, 15,

			0, 0, 0, 0,
		};
		size_t len=0;
		//differentiate_buffer(udata, 4, 4);
		void *cdata=qoi_encode(udata, &desc, (int*)&len);
		unsigned char *im=(unsigned char*)qoi_decode(cdata, len, &desc);
		//integrate_buffer(udata, 4, 4);
		//integrate_buffer(im, 4, 4);
		int size=desc.width*desc.height;
		for(int k=0;k<size;++k)
		{
			if(im[k]!=udata[k])
				LOG_ERROR("Test error at %d", k);
		}
	}
#endif
	wchar_t szFile[MAX_PATH]={'\0'};
	tagOFNW ofn={sizeof(ofn), ghWnd, 0, L"All files(*.*)\0*.*\0", 0, 0, 1, szFile, sizeof(szFile), 0, 0, 0, 0, OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST, 0, 0, 0, 0, 0, 0};
	if(GetOpenFileNameW(&ofn))
		return open_mediaw(ofn.lpstrFile);
	return 0;
}
bool			save_media_as()
{
	const wchar_t filetitle[]=L"Untitled.PNG";
	memcpy(g_wbuf, filetitle, sizeof filetitle);
	tagOFNW ofn=
	{
		sizeof(tagOFNW), ghWnd, 0,
		L"PNG (*.PNG)\0*.PNG\0"//lpstrFilter
#ifdef HVIEW_INCLUDE_LIBJXL
		L"JPEG XL (*.JXL)\0*.JXL\0"
#endif
		L"Quite OK Image format (8-bit only) (*.QOI)\0*.QOI\0"
//#ifdef HVIEW_INCLUDE_LIBHEIF
//		L"HEIF (*.HEIC)\0*.HEIC\0"
//#endif
		,
		0, 0,//custom filter
		1,//filter index: 0 is custom filter, 1 is first, ...

		g_wbuf, g_buf_size,//file (output)

		0, 0,//file title
		0, 0, OFN_ENABLESIZING|OFN_EXPLORER|OFN_NOTESTFILECREATE|OFN_PATHMUSTEXIST|OFN_EXTENSIONDIFFERENT|OFN_OVERWRITEPROMPT,//flags

		0,//nFileOffset
		8,//nFileExtension
		L"PNG",//default extension

		0, 0, 0
	};
	if(GetSaveFileNameW(&ofn))
	{
		std::wstring filename=ofn.lpstrFile, extension;
		get_extension(filename, extension);
		int ext_idx=enumerate_extension(extension);
		switch(ext_idx)
		{
#ifdef HVIEW_INCLUDE_LIBJXL
		case EXT_JXL:
			{
				auto encoder=JxlEncoderCreate(nullptr);		GEN_ASSERT(encoder);
				void *runner=JxlThreadParallelRunnerCreate(nullptr, JxlThreadParallelRunnerDefaultNumWorkerThreads());	GEN_ASSERT(runner);
				JxlEncoderStatus status=JxlEncoderSetParallelRunner(encoder, JxlThreadParallelRunner, runner);			JXLENC_CHECK(status);
				
				JxlPixelFormat format=
				{
					imagetype==IM_RGBA?4u:1u,
					JXL_TYPE_FLOAT,
					JXL_NATIVE_ENDIAN,
					0
				};

				JxlBasicInfo info={};
				JxlEncoderInitBasicInfo(&info);
				info.xsize=iw, info.ysize=ih;

				info.bits_per_sample=32, info.exponent_bits_per_sample=8, info.uses_original_profile=JXL_FALSE;
				//if(idepth>16)
				//	info.bits_per_sample=16;
				//else
				//	info.bits_per_sample=idepth;
				//info.exponent_bits_per_sample=0;

				//if(idepth==32)
				//	info.bits_per_sample=32, info.exponent_bits_per_sample=8;
				//else
				//	info.bits_per_sample=idepth, info.exponent_bits_per_sample=0;
#ifdef BENCHMARK
				long long t1=__rdtsc();
#endif
				status=JxlEncoderSetBasicInfo(encoder, &info);	JXLENC_CHECK(status);

				JxlColorEncoding color_encoding={};
				JxlColorEncodingSetToSRGB(&color_encoding, imagetype!=IM_RGBA);
				status=JxlEncoderSetColorEncoding(encoder, &color_encoding);	JXLENC_CHECK(status);
				
				size_t bytesize=image_size*sizeof(float);
				bytesize<<=(imagetype==IM_RGBA)<<1;
				auto settings=JxlEncoderFrameSettingsCreate(encoder, nullptr);
				status=JxlEncoderAddImageFrame(settings, &format, image, bytesize);		JXLENC_CHECK(status);
				JxlEncoderCloseInput(encoder);

				std::vector<unsigned char> compressed(64);
				auto next_out=compressed.data();
				size_t avail_out=compressed.size();
				for(JxlEncoderStatus result=JXL_ENC_NEED_MORE_OUTPUT;;)
				{
					result=JxlEncoderProcessOutput(encoder, &next_out, &avail_out);
					if(result!=JXL_ENC_NEED_MORE_OUTPUT)
						break;
					size_t offset=next_out-compressed.data();
					compressed.resize(compressed.size()<<1);
					next_out=compressed.data()+offset;
					avail_out=compressed.size()-offset;
				}
				compressed.resize(next_out-compressed.data());

				JxlEncoderDestroy(encoder);
#ifdef BENCHMARK
				long long t2=__rdtsc();
				LOG_ERROR("QOI: %lld cycles", t2-t1);
#endif

				FILE *out;
				_wfopen_s(&out, filename.c_str(), L"wb");
				if(!out)
				{
					LOG_ERROR("Could not save as %s", filename.c_str());
					return false;
				}
				fwrite(compressed.data(), sizeof(*compressed.data()), compressed.size(), out);
				fclose(out);
			}
			break;
#endif
		case EXT_PNG:
			{
				stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename.c_str());
				if(imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
					debayer();
				if(idepth>8)
					idepth=16;
				else
					idepth=8;
				char has4=imagetype==IM_RGBA;
				size_t ccount=image_size<<(has4<<1);
				auto buffer=new unsigned char[ccount<<(int)(idepth==16)];
				if(idepth==16)
				{
					for(int k=0;k<ccount;++k)
					{
						unsigned short v2;
						if((k&3)==3)//alpha
							v2=(unsigned short)(image[k]*65535);
						else
						{
							auto val=(contrast_gain*(image[k]-contrast_offset)+contrast_offset)*65535+0.5;
							if(val<0)
								val=0;
							if(val>65535)
								val=65535;
							v2=(unsigned short)val;
						}
						((unsigned short*)buffer)[k]=v2>>8|v2<<8;
					}
				}
				else
				{
					for(int k=0;k<ccount;++k)
					{
						if((k&3)==3)//alpha
							buffer[k]=(unsigned char)(image[k]*255);
						else
						{
							auto val=(contrast_gain*(image[k]-contrast_offset)+contrast_offset)*255+0.5;
							if(val<0)
								val=0;
							if(val>255)
								val=255;
							buffer[k]=(unsigned char)val;
						}
					}
				}
#ifdef BENCHMARK
				long long t1=__rdtsc();
#endif
				int error=lodepng::encode(g_buf, (unsigned char*)buffer, iw, ih, has4?LCT_RGBA:LCT_GREY, idepth);
#ifdef BENCHMARK
				long long t2=__rdtsc();
				LOG_ERROR("LODEPNG: %lld cycles", t2-t1);
#endif
				if(error)
					LOG_ERROR("LodePNG: %s", lodepng_error_text(error));
				delete[] buffer;
			}
			break;
		case EXT_QOI:
			{
				stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename.c_str());
				if(imagetype==IM_BAYER||imagetype==IM_BAYER_SEPARATE)
					debayer();
				char has4=imagetype==IM_RGBA;
				size_t ccount=image_size<<(has4<<1);
				auto buffer=new unsigned char[ccount];
				for(int k=0;k<ccount;++k)
				{
					float val;
					if((k&3)!=3)//color component
						val=(float)((contrast_gain*(image[k]-contrast_offset)+contrast_offset)*255+0.5);
					else//alpha component
						val=image[k]*255;
					if(val<0)
						val=0;
					if(val>255)
						val=255;
					buffer[k]=(unsigned char)val;
				}
				qoi_desc desc={(unsigned)iw, (unsigned)ih, has4?4u:1u, QOI_LINEAR};
#ifdef BENCHMARK
				long long t1=__rdtsc();
#endif
				int size=qoi_write(g_buf, buffer, &desc);
#ifdef BENCHMARK
				long long t2=__rdtsc();
				LOG_ERROR("QOI: %lld cycles", t2-t1);
#endif
				if(!size)
					LOG_ERROR("Failed to save %s", g_buf);
				delete[] buffer;
			}
			break;
/*#ifdef HVIEW_INCLUDE_LIBHEIF
		case EXT_HEIC:
			{

				heif_context *ctx=heif_context_alloc();

				stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename.c_str());
				heif_context_write_to_file(ctx, g_buf);
			}
			break;
#endif//*/
		default:
			LOG_ERROR("Can't save as .%s", extension.c_str());
			break;
		}
		return true;
	}
	return false;
}

bool			dialog_get_folder(const wchar_t *user_instr, std::wstring &path)
{
	HRESULT hr=OleInitialize(nullptr);
	if(hr!=S_OK)
	{
		OleUninitialize();
		return false;
	}
	IFileOpenDialog *pFileOpenDialog=nullptr;
	IShellItem *pShellItem=nullptr;
	LPWSTR fullpath=nullptr;
	hr=CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileOpenDialog));
	bool success=false;
	if(SUCCEEDED(hr))
	{
		hr=pFileOpenDialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
		hr=pFileOpenDialog->Show(nullptr);
		success=SUCCEEDED(hr);
		if(success)
		{
			hr=pFileOpenDialog->GetResult(&pShellItem);
			pShellItem->GetDisplayName(SIGDN_FILESYSPATH, &fullpath);
			path=fullpath;
			path=filter_path(path.c_str(), path.size());
			CoTaskMemFree(fullpath);
		}
		pFileOpenDialog->Release();
	}
	OleUninitialize();
	return success;

	//tree structure dialog
#if 0
	wchar_t folderpath[MAX_PATH]={};//https://stackoverflow.com/questions/12034943/win32-select-directory-dialog-from-c-c
	BROWSEINFOW binfo=
	{
		ghWnd,
		nullptr,
		folderpath,
		user_instr,
		BIF_USENEWUI|BIF_RETURNONLYFSDIRS,
		nullptr, 0,
		0,
	};
	auto result=SHBrowseForFolderW(&binfo);
	if(result)
	{
		SHGetPathFromIDListW(result, folderpath);
		path=folderpath;
		assign_path(path, 0, (int)path.size(), path);

		IMalloc *imalloc=nullptr;
        if(SUCCEEDED(SHGetMalloc(&imalloc)))
        {
            imalloc->Free(result);
            imalloc->Release();
        }
		return true;
	}
	return false;
#endif

	//wchar_t szFile[MAX_PATH]={'\0'};
	//tagOFNW ofn=
	//{
	//	sizeof(ofn), ghWnd, 0, nullptr, 0, 0, 1, szFile, sizeof(szFile), 0, 0, 0, 0,
	//	0,
	//	0, 0, 0, 0, 0, 0
	//};
	//if(GetOpenFileNameW(&ofn))
	//	path=ofn.lpstrFile;
}
void			convert_w2utf8(const wchar_t *src, std::string &dst)
{
	stbi_convert_wchar_to_utf8(g_buf, g_buf_size, src);
	dst=g_buf;
}

bool			get_all_image_filenames(const wchar_t *path, size_t len, std::vector<std::wstring> &filenames)//path ends with slash
{
	std::wstring ext;
	_WIN32_FIND_DATAW data={};
	if(!len)
		len=wcslen(path);
	std::wstring src=filter_path(path, len);

	void *hSearch=FindFirstFileW((src+L'*').c_str(), &data);//skip .
	if(hSearch==INVALID_HANDLE_VALUE)
		return false;
	int success=FindNextFileW(hSearch, &data);//skip ..
	for(;success=FindNextFileW(hSearch, &data);)
	{
		get_extension(data.cFileName, ext);
		if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&enumerate_extension(ext)<EXT_COUNT)
			filenames.push_back(data.cFileName);
	}
	success=FindClose(hSearch);
	SYS_ASSERT(success);
	return true;
}
int				enumerate_files(std::wstring const &folder, std::vector<std::wstring> &filenames, std::wstring const &currenttitle)
{
	get_all_image_filenames(folder.c_str(), folder.size(), filenames);

	for(int k=0;k<(int)filenames.size();++k)
		if(currenttitle==filenames[k])
			return k;
	return -1;
}
void			open_next()
{
	std::vector<std::wstring> filenames;
	int current=enumerate_files(workfolder, filenames, filetitle);
	if(filenames.size())
	{
		if(current==-1)
			current=0;
		else
			current=(current+1)%filenames.size();
		int c0=current;
		for(;!open_mediaw((workfolder+filenames[current]).c_str());)
		{
			current=(current+1)%filenames.size();
			if(current==c0)
				break;
		}
	}
}
void			open_prev()
{
	std::vector<std::wstring> filenames;
	int current=enumerate_files(workfolder, filenames, filetitle);
	if(filenames.size())
	{
		if(current==-1)
			current=0;
		else
			current=(current-1+(int)filenames.size())%(int)filenames.size();
		int c0=current;
		for(;!open_mediaw((workfolder+filenames[current]).c_str());)
		{
			current=(current-1+(int)filenames.size())%(int)filenames.size();
			if(current==c0)
				break;
		}
	}
}