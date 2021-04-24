#include		"generic.h"
#include		"hview.h"
#include		"huff.h"
#define			STBI_WINDOWS_UTF8
#include		"stb_image.h"
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
	int size=temp.size();
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
	int size=temp.size();
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
		end=text.size();
	for(;end>0&&(text[end-1]==' '||text[end-1]=='\t'||text[end-1]=='\r'||text[end-1]=='\n');--end);//skip trailing whitespace
	start+=text[start]==doublequote, end-=text[end-1]==doublequote;
	assert(start<end);
	std::string temp(text.begin()+start, text.begin()+end);
	int size=temp.size();
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
	assign_path(filename, 0, filename.size(), filteredfilename);

	int start=-1;
	for(int k=filteredfilename.size()-1;k>=0;--k)
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
	int k=filename.size()-1;
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
#if _MSC_VER<1800
#define	S_IFMT		00170000//octal
#define	S_IFREG		 0100000
#endif
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

//void			open_mediaa(const char *filename){}
bool			open_mediaw(const wchar_t *filename)//if successful: sets workfolder, updates title
{
	std::wstring wfn=filename, folder, title, extension;
	split_filename(filename, folder, title);
	get_extension(title, extension);
	
	//check extension
	if(extension==L"huf")//compressed raw
	{
		std::vector<byte> data;
		read_binary(filename, data);
		auto success=huff::decompress(data.data(), data.size(), RF_F32_BAYER, (void**)&image, iw, ih, idepth, bayer);
		//auto success=decompress_huff(data.data(), data.size(), RF_F32_BAYER, (void**)&image, iw, ih, idepth, bayer);
		if(!success)
			return false;
		image_size=iw*ih;
		if(bayer[0]==-1)
			imagetype=IM_GRAYSCALE;
		else
			imagetype=IM_BAYER;
	}
	else//ordinary image
	{
		stbi_convert_wchar_to_utf8(g_buf, g_buf_size, filename);
		int iw2=0, ih2=0, nch2=0;
		unsigned char *original_image=stbi_load(g_buf, &iw2, &ih2, &nch2, 4);
		if(!original_image)
			return false;
		auto src=(int*)original_image;
		iw=iw2, ih=ih2, image_size=iw*ih;
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
		imagetype=IM_RGBA;
		free(original_image);
	}

	//on success
	reset_FFTW_state();
	workfolder=std::move(folder), filetitle=std::move(title);
	SetWindowTextW(ghWnd, (wfn+L" - hView").c_str());
	center_image();
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
			current=(current-1+filenames.size())%filenames.size();
		int c0=current;
		for(;!open_mediaw((workfolder+filenames[current]).c_str());)
		{
			current=(current-1+filenames.size())%filenames.size();
			if(current==c0)
				break;
		}
	}
}