#include"hView.h"
#include<Windows.h>
#include<wincodec.h>
#include<wchar.h>
#ifdef _MSC_VER
#pragma comment(lib, "windowscodecs.lib")
#endif
//static const char file[]=__FILE__;

int get_metadata(const wchar_t *fn)
{
	IWICImagingFactory *factory = NULL;
	IWICBitmapDecoder *decoder = NULL;
	IWICBitmapFrameDecode *frame = NULL;
	IWICMetadataQueryReader *reader = NULL;
	int orientation = 1;
	int error=0;
	PROPVARIANT value;

	//if(!g_ole32initialized)
	//{
	//	error=CoInitializeEx(NULL, COINIT_MULTITHREADED);
	//	if(FAILED(error))
	//		return 1;
	//}
	error=CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void**)&factory);

	if(FAILED(error))
		goto cleanup;

	error=factory->lpVtbl->CreateDecoderFromFilename(factory, fn, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
	if(FAILED(error))
		goto cleanup;

	error=decoder->lpVtbl->GetFrame(decoder, 0, &frame);
	if(FAILED(error))
		goto cleanup;

	error=frame->lpVtbl->GetMetadataQueryReader(frame, &reader);
	if(FAILED(error))
		goto cleanup;

	PropVariantInit(&value);

	//EXIF Orientation tag
	error=reader->lpVtbl->GetMetadataByName(reader, L"/app1/ifd/{ushort=274}", &value);

	if(SUCCEEDED(error)&&value.vt==VT_UI2)
		orientation=(int)value.uiVal;

	PropVariantClear(&value);

cleanup:
	if(reader)
		reader->lpVtbl->Release(reader);
	if(frame)
		frame->lpVtbl->Release(frame);
	if(decoder)
		decoder->lpVtbl->Release(decoder);
	if(factory)
		factory->lpVtbl->Release(factory);

	//CoUninitialize();
	return orientation;
}