#include		"generic.h"
#include		"hview.h"
#include		"huff.h"
#define			STBI_WINDOWS_UTF8
#include		"stb_image.h"
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
#include		<vector>
#include		<string>
#include		<fstream>
#include		<assert.h>
#include		<sys\stat.h>
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
void			init_from_RGBA8(const int *src, int iw2, int ih2)
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
	if(extension==L"huf")//compressed raw
	{
		std::vector<byte> data;
		read_binary(filename, data);
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
#ifdef HVIEW_INCLUDE_LIBHEIF
	else if(extension==L"heic")
	{
		stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);

		heif_context *ctx=heif_context_alloc();
		heif_error error=heif_context_read_from_file(ctx, g_buf, nullptr);	LIBHEIF_CHECK(error);

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
		init_from_RGBA8((int*)data, iw2, ih2);

		heif_image_release(img);
		heif_image_handle_release(handle);
	}
#endif
#ifdef HVIEW_INCLUDE_SAIL
	else if(extension==L"avif"||extension==L"webp"||extension==L"jp2")
	{
		stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);

		void *state=nullptr;
		struct sail_image *img=nullptr;
		auto error=sail_start_reading_file(g_buf, nullptr, &state);	WEBP_CHECK(error);
		if(!state)
			return false;
		error=sail_read_next_frame(state, &img);					WEBP_CHECK(error);
		switch(img->pixel_format)
		{
		case SAIL_PIXEL_FORMAT_BPP32_RGBA:
			init_from_RGBA8((int*)img->pixels, img->width, img->height);
			break;
		default:
			messageboxa(ghWnd, "Error", "Unsupported pixel format from libSAIL: %d", img->pixel_format);
			break;
		}
		error=sail_stop_reading(state);								WEBP_CHECK(error);
		sail_destroy_image(img);

		//struct sail_io *io;
		//auto result=sail_alloc_io_read_file(g_buf, &io);
		//sail_destroy_io(io);
	}
#endif
	else//ordinary image
	{
		stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);
		int iw2=0, ih2=0, nch2=0;
		unsigned char *original_image=stbi_load(g_buf, &iw2, &ih2, &nch2, 4);
		if(!original_image)
			return false;
		auto src=(int*)original_image;
		
		init_from_RGBA8(src, iw2, ih2);
	/*	iw=iw2, ih=ih2, image_size=iw*ih;
		void *buf=realloc(image, image_size*4*sizeof(float));
		if(!buf)
		{
			LOG_ERROR("realloc returned null");
			return false;
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
		imagetype=IM_RGBA;//*/

		free(original_image);
	}

	//center_image();
	render();
	return true;
}
void			open_media()
{
	wchar_t szFile[MAX_PATH]={'\0'};
	tagOFNW ofn={sizeof(ofn), ghWnd, 0, L"All files(*.*)\0*.*\0", 0, 0, 1, szFile, sizeof(szFile), 0, 0, 0, 0, OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST, 0, 0, 0, 0, 0, 0};
	if(GetOpenFileNameW(&ofn))
	{
		open_mediaw(ofn.lpstrFile);
	}
}

int			enumerate_files(std::wstring const &folder, std::wstring const &currenttitle, std::vector<std::wstring> &filenames)
{
	_WIN32_FIND_DATAW data;
	void *hSearch=FindFirstFileW((workfolder+L'*').c_str(), &data);//skip .
	if(hSearch==INVALID_HANDLE_VALUE)
		return -1;
	int success=FindNextFileW(hSearch, &data);//skip ..
	for(;success=FindNextFileW(hSearch, &data);)
	{
		if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
			filenames.push_back(data.cFileName);
	}
	success=FindClose(hSearch);
	SYS_ASSERT(success);

	for(int k=0;k<(int)filenames.size();++k)
		if(currenttitle==filenames[k])
			return k;
	return -1;
}
void			open_next()
{
	std::vector<std::wstring> filenames;
	int current=enumerate_files(workfolder, filetitle, filenames);
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
	int current=enumerate_files(workfolder, filetitle, filenames);
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