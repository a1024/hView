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

#include		"generic.h"
#include		"hview.h"
#include		"huff.h"
#include		<stdint.h>
#define			STBI_WINDOWS_UTF8
#define			STB_IMAGE_IMPLEMENTATION
#define			TINY_DNG_LOADER_IMPLEMENTATION
#include		"tiny_dng_loader.h"
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

#include		<vector>
#include		<string>
#include		<fstream>
#include		<assert.h>
#include		<sys/stat.h>
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
	EXT(JPG)\
	EXT(JPEG)\
	EXT(PNG)\
	EXT(BMP)\
	EXT(DIB)\
	EXT(GIF)\
	EXT(TIF)\
	EXT(TIFF)\
	EXT(JP2)\
	EXT(J2K)\
	EXT(WEBP)\
	EXT(AVIF)\
	EXT(HEIC)\
	EXT(BPG)\
	EXT(JXL)\
	EXT(DNG)\
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
#define			WEBP_CHECK(ERROR)		(!(ERROR)||log_error(file, __LINE__, sail_error2str(ERROR)))
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
void			assign_from_RGBA8(const int *src, int iw2, int ih2)
{
	iw=iw2, ih=ih2, image_size=iw*ih;
	void *buf=realloc(image, image_size*4*sizeof(float));
	if(!buf)
	{
		LOG_ERROR("realloc returned null");
		return;
	}
	image=(float*)buf;
	idepth=8;
	float inv255=1.f/255;
	for(int ks=0, kd=0;ks<image_size;++ks, kd+=4)
	{
		auto p=(unsigned char*)(src+ks);
		image[kd  ]=p[0]*inv255;
		image[kd+1]=p[1]*inv255;
		image[kd+2]=p[2]*inv255;
		image[kd+3]=p[3]*inv255;
	}
	imagetype=IM_RGBA;
}
bool			open_mediaw(const wchar_t *filename)//if successful: sets workfolder, updates title
{
	std::wstring wfn=filename, folder, title, extension;
	split_filename(filename, folder, title);
	get_extension(title, extension);

	bitmode=false;
	reset_FFTW_state();
	workfolder=std::move(folder), filetitle=std::move(title);
	SetWindowTextW(ghWnd, (wfn+L" - hView").c_str());
	
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
			auto success=huff::decompress(data.data(), (int)data.size(), RF_F32_BAYER, (void**)&image, iw, ih, idepth, bayer);
			//auto success=decompress_huff(data.data(), data.size(), RF_F32_BAYER, (void**)&image, iw, ih, idepth, bayer);
			if(!success)
				return false;
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
			int iw2=0, ih2=0;
			auto original_image=bpg_to_rgba(g_buf, &iw2, &ih2);
			if(!original_image)
				return false;
			
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
	case EXT_AVIF:
	case EXT_WEBP:
	case EXT_JP2:
	case EXT_J2K:
		{
			stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);

			void *state=nullptr;
			struct sail_image *img=nullptr;
			auto error=sail_start_reading_file(g_buf, nullptr, &state);	WEBP_CHECK(error);
			if(!state)//file may not exist
				return false;
			error=sail_read_next_frame(state, &img);	WEBP_CHECK(error);
			switch(img->pixel_format)
			{
			case SAIL_PIXEL_FORMAT_BPP32_RGBA:
				assign_from_RGBA8((int*)img->pixels, img->width, img->height);
				break;
			default:
				messageboxa(ghWnd, "Error", "Unsupported pixel format from libSAIL: %d", img->pixel_format);
				break;
			}
			error=sail_stop_reading(state);				WEBP_CHECK(error);
			sail_destroy_image(img);

			//struct sail_io *io;
			//auto result=sail_alloc_io_read_file(g_buf, &io);
			//sail_destroy_io(io);
		}
		break;
#endif
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
	default://ordinary image
		{
			stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);
			int iw2=0, ih2=0, nch2=0;
			unsigned char *original_image=stbi_load(g_buf, &iw2, &ih2, &nch2, 4);
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
	render();
	return true;
}
void			open_media()
{
	wchar_t szFile[MAX_PATH]={'\0'};
	tagOFNW ofn={sizeof(ofn), ghWnd, 0, L"All files(*.*)\0*.*\0", 0, 0, 1, szFile, sizeof(szFile), 0, 0, 0, 0, OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST, 0, 0, 0, 0, 0, 0};
	if(GetOpenFileNameW(&ofn))
		open_mediaw(ofn.lpstrFile);
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
				
				JxlPixelFormat format={imagetype==IM_RGBA?4:1, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0};

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
				status=JxlEncoderSetBasicInfo(encoder, &info);	JXLENC_CHECK(status);

				JxlColorEncoding color_encoding={};
				JxlColorEncodingSetToSRGB(&color_encoding, imagetype!=IM_RGBA);
				JxlEncoderSetColorEncoding(encoder, &color_encoding);
				
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
				size_t ccount=image_size;
				char has4=imagetype==IM_RGBA;
				ccount<<=has4<<1;
				auto buffer=new unsigned char[ccount];
				for(int k=0;k<ccount;++k)
				{
					if((k&3)==3)
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
				lodepng::encode(g_buf, (unsigned char*)buffer, iw, ih, has4?LCT_RGBA:LCT_GREY, 8);//most programs (eg: WhatsApp) expect 8bit PNG
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

bool			get_all_image_filenames(std::wstring const &path, std::vector<std::wstring> &filenames)//path ends with slash
{
	std::wstring ext;
	_WIN32_FIND_DATAW data={};
	void *hSearch=FindFirstFileW((path+L'*').c_str(), &data);//skip .
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
	get_all_image_filenames(folder, filenames);

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