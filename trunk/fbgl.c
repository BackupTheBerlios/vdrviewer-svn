/*
 *  Copyright (C) 1997, 1998 Olivetti & Oracle Research Laboratory
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

/* 
 * svga.c - the svgalib interface
 * Basically hacked from x.c by {ganesh,sitaram}@cse.iitb.ernet.in
 */

#include <vdrviewer.h>

#include <fcntl.h>
#include <unistd.h>

#include "fbgl.h"

#define INVALID_PIXEL 0xffffffff
#define COLORMAP_SIZE 256


struct fb_cmap cmap;
__u16 red[256], green[256], blue[256], transp[256];


/*
 * CopyDataToScreen.
 */

void CopyDataToScreen(CARD8 *buf, int x, int y, int width, int height)
{
	gl_putbox(x, y, width, height, buf);
}


void gl_initpalettecolor(int numcolors) 
{
    cmap.start  = 0;
    cmap.red    = red;
    cmap.green  = green;
    cmap.blue   = blue;
    cmap.transp = transp;
    cmap.len    = numcolors;
}

void gl_setpalettecolor(int i, int r, int g, int b, int tr) 
{
    cmap.red[i]    = r;
    cmap.green[i]  = g;
    cmap.blue[i]   = b;
    cmap.transp[i] = tr;
    dprintf("SetPaletteColor r=%x g=%x b=%x t=%x\n", r, g, b, tr);
}

void gl_storepalette(struct fb_cmap *map) 
{
    if (map == NULL)
	map = &cmap;

    if (ioctl(global_framebuffer.framebuf_fds, FBIOPUTCMAP, map) == -1)
	dprintf("VDRViewer <FBIOPUTCMAP failed>\n");
}

void gl_copybox(int xsrc, int ysrc, int w, int h, int x, int y) {
	int j;
	IMPORT_FRAMEBUFFER_VARS

	if (y < ysrc) {
		Pixel *src, *dst;
		src = v_buf + ysrc * v_xsize + xsrc;
		dst = v_buf + y * v_xsize + x;

		for (j=0; j<h; j++) {
			memcpy(dst, src, w*sizeof(Pixel));
			src += v_xsize;
			dst += v_xsize;
		}
	} else if (y > ysrc) {
		Pixel *src, *dst;
		src = v_buf + (ysrc+h-1) * v_xsize + xsrc;
		dst = v_buf + (y+h-1) * v_xsize + x;
		
		for (j=h; j; j--) {
			memcpy(dst, src, w*sizeof(Pixel));
			src -= v_xsize;
			dst -= v_xsize;
		}
	} else /* y == ysrc */ {
		Pixel *src, *dst;
		src = v_buf + ysrc * v_xsize + xsrc;
		dst = v_buf + y * v_xsize + x;

		for (j=0; j<h; j++) {
			memmove(dst, src, w*sizeof(Pixel));
			src += v_xsize;
			dst += v_xsize;
		}
	}
	redraw_virt(x, y, w, h);
}

void gl_fillbox(int x, int y, int w, int h, int col) {
	int i,j;
	IMPORT_FRAMEBUFFER_VARS
	for (j=0; j<h; j++) {
		Pixel *buf = v_buf + (y+j) * v_xsize + x;
		
		for (i=0; i<w; i++) {
			*(buf++) = col;
		}
	}
	redraw_virt(x, y, w, h);
//	printf("Fillbox %d\n",col);
}

void gl_fillboxnoredraw(int x, int y, int w, int h, int col) {
	int i,j;
	IMPORT_FRAMEBUFFER_VARS
	for (j=0; j<h; j++) {
		Pixel *buf = v_buf + (y+j) * v_xsize + x;
		
		for (i=0; i<w; i++) {
			*(buf++) = col;
		}
	}
//	printf("Fillboxnoredraw %d\n",col);
}

void gl_putbox(int x, int y, int w, int h, CARD8 *buf) {
	int j;
	Pixel *src, *dst;
	IMPORT_FRAMEBUFFER_VARS

	src = (Pixel*)buf;
	dst = v_buf + y * v_xsize + x;
	
	for (j=0; j<h; j++) {
		memcpy(dst, src, w * sizeof(Pixel));
		src += w;
		dst += v_xsize;
	}
	redraw_virt(x, y, w, h);
//	printf("Putbox \n");
}

void gl_redrawbox(int x, int y, int w, int h) {
	redraw_virt(x, y, w, h);
//	printf("Redwaw \n");
}