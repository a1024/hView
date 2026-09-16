#define EXPOSE_CVT
#include"hView.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#define _USE_MATH_DEFINES
#include<math.h>
static const char file[]=__FILE__;

static int prevw=0, prevh=0, remw=0, remh=0, xtiles=0, ytiles=0, ntiles=0;
static uint32_t *textures=0;
int tiles_update(const uint8_t *image, int iw, int ih)
{
	if(iw!=prevw||ih!=prevh)
	{
		xtiles=iw/tilesize;
		remw=iw%tilesize;
		xtiles+=remw!=0;
		ytiles=ih/tilesize;
		remh=ih%tilesize;
		ytiles+=remh!=0;

		if(ntiles>0)
			glDeleteTextures(ntiles, textures);
		ntiles=xtiles*ytiles;
		{
			void *p=realloc(textures, ntiles*sizeof(uint32_t));
			if(!p)
				return 1;
			textures=(uint32_t*)p;
		}
		glGenTextures(ntiles, textures);
		for(int ky=0, idx=0;ky<ytiles;++ky)
		{
			int th=remh!=0&&ky==ytiles-1?remh:tilesize;
			for(int kx=0;kx<xtiles;++kx, ++idx)
			{
				int tw=remw!=0&&kx==xtiles-1?remw:tilesize;
				glBindTexture(GL_TEXTURE_2D, textures[idx]);
				set_texture_params(0, 1);
				glTexImage2D(GL_TEXTURE_2D
					, 0
					, GL_RGBA
					, tw
					, th
					, 0
					, GL_RGBA
					, GL_UNSIGNED_BYTE
					, 0
				);
				GL_CHECK(error);
			}
		}
		prevw=iw;
		prevh=ih;
	}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, iw);
	for(int ky=0, idx=0;ky<ytiles;++ky)
	{
		int th=remh!=0&&ky==ytiles-1?remh:tilesize;
		for(int kx=0;kx<xtiles;++kx, ++idx)
		{
			int tw=remw!=0&&kx==xtiles-1?remw:tilesize;
			glBindTexture(GL_TEXTURE_2D, textures[idx]);
			glTexSubImage2D(GL_TEXTURE_2D
				, 0
				, 0
				, 0
				, tw
				, th
				, GL_RGBA
				, GL_UNSIGNED_BYTE
				, image+4*tilesize*(iw*ky+kx)
			);
			GL_CHECK(error);
		}
	}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	return 0;
}
void tiles_draw(void)
{
	HPoint2D rect[4]={0};
	float scry1=0, scry2=0, scrx1=0, scrx2=0;
	float ndc[16]={0};
	
	for(int ky=0, idx=0;ky<ytiles;++ky)
	{
		int th=remh!=0&&ky==ytiles-1?remh:tilesize;

		scry2+=(float)th;
		for(int kx=0;kx<xtiles;++kx, ++idx)
		{
			int tw=remw!=0&&kx==xtiles-1?remw:tilesize, j;

			scrx2+=(float)tw;
			rect[0].x=scrx1; rect[0].y=scry1;
			rect[1].x=scrx1; rect[1].y=scry2;
			rect[2].x=scrx2; rect[2].y=scry2;
			rect[3].x=scrx2; rect[3].y=scry1;
			image2screen(rect+0);
			image2screen(rect+1);
			image2screen(rect+2);
			image2screen(rect+3);
			j=0;
			ndc[j++]=(float)rect[0].x; ndc[j++]=(float)rect[0].y; ndc[j++]=0; ndc[j++]=0;
			ndc[j++]=(float)rect[1].x; ndc[j++]=(float)rect[1].y; ndc[j++]=0; ndc[j++]=1;
			ndc[j++]=(float)rect[2].x; ndc[j++]=(float)rect[2].y; ndc[j++]=1; ndc[j++]=1;
			ndc[j++]=(float)rect[3].x; ndc[j++]=(float)rect[3].y; ndc[j++]=1; ndc[j++]=0;
			{
				float xscale=2.f/(float)w;
				float yscale=2.f/(float)h;
				for(int k=0;k<4;++k)
				{
					ndc[4*k+0]=ndc[4*k+0]*xscale-1;
					ndc[4*k+1]=1-ndc[4*k+1]*yscale;
				}
			}
			display_texture_ndc(ndc, textures[idx], 1);
			//if(scrx2>0&&scrx1<w&&scry2>0&&scry1<h)//crude clipping
			//{
			//}
			scrx1=scrx2;
		}
		scry1=scry2;
	}
	(void)&screen2image;
}
