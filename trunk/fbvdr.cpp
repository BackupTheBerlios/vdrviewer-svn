#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/dvb/audio.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/video.h>
#include <poll.h>



#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif
#ifndef HAVE_DREAMBOX_HARDWARE
#include <configfile.h>
#endif
#include <plugin.h>
#include <sys/mman.h>
#include <dbox/avs_core.h>
#include <dbox/saa7126_core.h>
#include <dbox/fp.h>
#include <linux/fb.h>
#include "input_fake.h"
#ifndef HAVE_DREAMBOX_HARDWARE
#include <dbox/fb.h>
#endif

#include <X11/keysym.h>

#define STEP_PAN 30

extern "C" {
#include "vdrviewer.h"
#include "fbgl.h"
#include "overlay.h"
#include "keyboard.h"
#include "list.h"
#include "ringbuffer.h"
}

#include "viewermenu.h"




//extern "C" {
//#include <../neutrino/src/driver/ringbuffer.h>

//#include <../neutrino/src/driver/fb_window.h>
//#include <../neutrino/src/gui/widget/hintbox.h>
//}           	

#define AVS "/dev/dbox/avs0"
#define SAA "/dev/dbox/saa0"
const int fncmodes[] = { AVS_FNCOUT_EXT43,AVS_FNCOUT_EXT169 };
const int saamodes[] = { SAA_WSS_43F,SAA_WSS_169F };
int screenmode, fnc_old, saa_old, avs, saa;
fbvnc_framebuffer_t global_framebuffer;
int fb_fd;
int rc_fd;
#ifdef HAVE_DREAMBOX_HARDWARE
int ms_fd;
int ms_fd_usb;
#endif
int blev;
char terminate;
int gScale,sx,sy,ex,ey;
int usepointer = 0;
void fbvnc_close(void);
Pixel ico_mouse[] = {
ICO_WHITE,ICO_WHITE,ICO_WHITE,ICO_WHITE,
ICO_WHITE,ICO_BLACK,ICO_BLACK,ICO_WHITE,
ICO_WHITE,ICO_BLACK,ICO_BLACK,ICO_WHITE,
ICO_WHITE,ICO_WHITE,ICO_WHITE,ICO_WHITE,
};


// Pacemaker's stuff Start
/* How to call it ....
#ifndef HAVE_DREAMBOX_HARDWARE
setMoreTransparency( false );
#endif
*/

#ifndef HAVE_DREAMBOX_HARDWARE
static int iLastTransparency = -1;
static struct timeval tvLastTrancparencChange;
static int iDeltaTime	= 5;
void setMoreTransparency( bool bDo = true );
void setMoreTransparency( bool bDo )
{
	int iOrigTrans;
	if ( iLastTransparency < 0 )
	{
		if (ioctl(global_framebuffer.framebuf_fds,AVIA_GT_GV_GET_BLEV, &iLastTransparency) == -1)
		{
			printf("Error get blev\n");
			iLastTransparency = -1;
		}

		dprintf("Original blev = %d\n", iLastTransparency);
	}
	iOrigTrans = iLastTransparency;


	if ( !bDo )
	{
		iLastTransparency = 0;
	}
	else
	{
		struct timeval tv;
		gettimeofday(&tv, 0);

		if ( tv.tv_sec < (tvLastTrancparencChange.tv_sec + iDeltaTime) )
			return;
	}

	iLastTransparency = iLastTransparency +1;
	if (iLastTransparency > 8 )
		iLastTransparency = 8;

	if ( iOrigTrans == iLastTransparency )
	{
		gettimeofday(&tvLastTrancparencChange, NULL);
		return;
	}

	unsigned int c = (iLastTransparency << 8) | iLastTransparency;

	if (ioctl(global_framebuffer.framebuf_fds,AVIA_GT_GV_SET_BLEV, c) == -1)
	{
		printf("Error set blev\n");
	}
	else
	{
		gettimeofday(&tvLastTrancparencChange, NULL);
	}
}
#endif



//char* sServerIP = "192.168.0.20";
unsigned short int nPort = 20002;

#define ADAP      "/dev/dvb/adapter0"
#define ADEC ADAP "/audio0"
#define VDEC ADAP "/video0"
#define DMX  ADAP "/demux0"
#define DVR  ADAP "/dvr0"


#define PF_BUF_SIZE   (348*188)
#define PF_DMX_SIZE   (PF_BUF_SIZE + PF_BUF_SIZE/2)
#define RINGBUFFERSIZE PF_BUF_SIZE*10
// original timeout: #define PF_RD_TIMEOUT 3000
#define PF_RD_TIMEOUT 500
#define PF_EMPTY      0
#define PF_READY      1
#define PF_LST_ITEMS  30
#define PF_SKPOS_OFFS MINUTEOFFSET
#define PF_JMP_DIV    4
#define PF_JMP_START  0
#define PF_JMP_MID    5555
#define PF_JMP_END    9999

pthread_t oTSReceiverThread;
pthread_t oPlayerThread;
static int nStopPlaying;
static off_t g_fileposition;




//-- live stream item --
//----------------------
typedef struct
{
	std::string        pname;
	std::string        ip;
	int                port;
	int                vpid;
	int                apid;
	long long          zapid;
} MP_LST_ITEM;

//-- main player context --
//-------------------------
typedef struct
{
	struct dmx_pes_filter_params p;
	short                        ac3;
	unsigned short               pida;
	unsigned short               pidv;

	struct pollfd *pH;

	int   inFd;
	int   dvr;
	int   dmxv;
	int   dmxa;
	int   dmxp;
	int   vdec;
	int   adec;

	off_t pos;
	off_t fileSize;

	int   readSize;
	char  dvrBuf[PF_BUF_SIZE];
	bool  refillBuffer;
	bool  startDMX;

	bool  isStream;
	bool  canPause;

	bool        itChanged;
	int         it;
	int         lst_cnt;
	MP_LST_ITEM lst[PF_LST_ITEMS];

} MP_CTX;


MP_CTX *ctx = NULL;


static int mp_tcpOpen(const char *serverIp, unsigned short serverPort)
{
	struct sockaddr_in ads;
	socklen_t          adsLen;
	int                fd;

	//-- tcp server --
	//-----------------------
	bzero((char *)&ads, sizeof(ads));
	ads.sin_family      = AF_INET;
	ads.sin_addr.s_addr = inet_addr(serverIp);
	ads.sin_port        = htons(serverPort);
	adsLen              = sizeof(ads);

	//-- get socket and try connect --
	//--------------------------------
	if( (fd = socket(AF_INET, SOCK_STREAM, 0)) != -1)
	{
		if( connect(fd, (struct sockaddr *)&ads, adsLen) == -1 )
		{
			close(fd);
			fd = -1;
		}
	}

	return fd;
}


static void mp_tcpClose(int fd)
{
	if(fd != -1) close(fd);
}


// checks if AR has changed an sets cropping mode accordingly (only video mode auto)
void checkAspectRatio (int vdec, bool init)
{

	static video_size_t size;
	static time_t last_check=0;

	// only necessary for auto mode, check each 5 sec. max
	//if(g_settings.video_Format != 0 || (!init && time(NULL) <= last_check+5))
	//	return;

	if(init)
	{
		if(ioctl(vdec, VIDEO_GET_SIZE, &size) < 0)
			perror("[movieplayer.cpp] VIDEO_GET_SIZE");
		last_check=0;
		return;
	}
	else
	{
		video_size_t new_size;
		if(ioctl(vdec, VIDEO_GET_SIZE, &new_size) < 0)
			perror("[movieplayer.cpp] VIDEO_GET_SIZE");
		if(size.aspect_ratio != new_size.aspect_ratio)
		{
			printf("[movieplayer.cpp] AR change detected in auto mode, adjusting display format\n");
			video_displayformat_t vdt;
			if(new_size.aspect_ratio == VIDEO_FORMAT_4_3)
				vdt = VIDEO_LETTER_BOX;
			else
				vdt = VIDEO_CENTER_CUT_OUT;
			if(ioctl(vdec, VIDEO_SET_DISPLAY_FORMAT, vdt))
				perror("[movieplayer.cpp] VIDEO_SET_DISPLAY_FORMAT");
			memcpy(&size, &new_size, sizeof(size));
		}
		last_check=time(NULL);
	}
}


//== open/close DVB devices ==
//============================
static bool mp_openDVBDevices(MP_CTX *ctx)
{
	//-- may prevent black screen after restart ... --
	//------------------------------------------------
	usleep(150000);

	ctx->dmxa = -1;
	ctx->dmxv = -1;
	ctx->dmxp = -1;
	ctx->dvr  = -1;
	ctx->adec = -1;
	ctx->vdec = -1;

	if((ctx->dmxa = open(DMX, O_RDWR)) < 0
		|| (ctx->dmxv = open(DMX, O_RDWR)) < 0
		|| (ctx->dvr = open(DVR, O_WRONLY)) < 0
		|| (ctx->adec = open(ADEC, O_RDWR)) < 0 || (ctx->vdec = open(VDEC, O_RDWR)) < 0)
	{
    if( ctx->dmxa < 0 )
    {
      dprintf("Device ctx->dmxa konnte nicht geoeffnet werden !!!!!\n");
    }
    if( ctx->dmxv < 0 )
    {
      dprintf("Device ctx->dmxv konnte nicht geoeffnet werden !!!!!\n");
    }
    if( ctx->dvr < 0 )
    {
      dprintf("Device ctx->dvr konnte nicht geoeffnet werden !!!!!\n");
    }
    if( ctx->adec < 0 )
    {
      dprintf("Device ctx->adec konnte nicht geoeffnet werden !!!!!\n");
    }
    if( ctx->vdec < 0 )
    {
      dprintf("Device ctx->vdec konnte nicht geoeffnet werden !!!!!\n");
    }
    
	}
	return true;
  
  
}static void mp_closeDVBDevices(MP_CTX *ctx)
{
	//-- may prevent black screen after restart ... --
	//------------------------------------------------
	usleep(150000);
	

	if(ctx->vdec != -1)
	{
		ioctl (ctx->vdec, VIDEO_STOP);
		close (ctx->vdec);
	}
	if(ctx->adec != -1)
	{
		ioctl (ctx->adec, AUDIO_STOP);
		close (ctx->adec);
	}

	if(ctx->dmxa != -1)
	{
		ioctl (ctx->dmxa, DMX_STOP);
		close(ctx->dmxa);
	}
	if(ctx->dmxv != -1)
	{
		ioctl (ctx->dmxv, DMX_STOP);
		close(ctx->dmxv);
	}
	if(ctx->dvr!= -1)	close(ctx->dvr);

	ctx->dmxp = -1;
	ctx->dmxa = -1;
	ctx->dmxv = -1;
	ctx->dvr  = -1;
	ctx->adec = -1;
	ctx->vdec = -1;
}




static ssize_t mp_tcpRead(struct pollfd *pH, char *buf, size_t count, int tio)
{
	ssize_t n, nleft = count;

	while( (nleft>0) && (poll(pH, 1, tio) > 0) && (pH->events & pH->revents) && nStopPlaying == 0 )
	{
		n = read(pH->fd, buf, nleft);
		nleft -= n;
		buf   += n;
	}

	return(count-nleft);
}


//== mp_startDMX ==
//=================
static void mp_startDMX(MP_CTX *ctx)
{
	if(ctx->startDMX)
	{
		ioctl (ctx->dmxa, DMX_START);	// audio first !
		ioctl (ctx->dmxv, DMX_START);
		ctx->startDMX = false;
	}
}


//== mp_softReset ==
//==================
static void mp_softReset(MP_CTX *ctx, bool refill = true)
{
	//-- stop DMX devices --
	ioctl(ctx->dmxv, DMX_STOP);
	ioctl(ctx->dmxa, DMX_STOP);

	//-- stop AV devices --
	ioctl(ctx->vdec, VIDEO_STOP);
	ioctl(ctx->adec, AUDIO_STOP);

	//-- setup DMX devices (again) --
	ctx->p.input    = DMX_IN_DVR;
	ctx->p.output   = DMX_OUT_DECODER;
	ctx->p.flags    = 0;

	ctx->p.pid      = ctx->pida;
	ctx->p.pes_type = DMX_PES_AUDIO;

	ioctl (ctx->dmxa, DMX_SET_BUFFER_SIZE, PF_DMX_SIZE/4);
	ioctl (ctx->dmxa, DMX_SET_PES_FILTER, &(ctx->p));

	ctx->p.pid      = ctx->pidv;
	ctx->p.pes_type = DMX_PES_VIDEO;
	ioctl (ctx->dmxv, DMX_SET_BUFFER_SIZE, PF_DMX_SIZE);
	ioctl (ctx->dmxv, DMX_SET_PES_FILTER, &(ctx->p));

	//-- switch audio decoder bypass depending on audio type --
	if(ctx->ac3 == 1)
		ioctl(ctx->adec, AUDIO_SET_BYPASS_MODE,0UL);	// bypass on
	else
		ioctl(ctx->adec, AUDIO_SET_BYPASS_MODE,1UL);	// bypass off

	//-- start AV devices again --
	ioctl(ctx->adec, AUDIO_PLAY);					// audio
	ioctl(ctx->vdec, VIDEO_PLAY);					// video
	ioctl(ctx->adec, AUDIO_SET_AV_SYNC, 1UL);	// needs sync !

	//-- refill input buffer --
	if (refill) ctx->refillBuffer = true;

	//-- but never start demuxers here, because this will --
	//-- be done outside (e.g. short before writing.)     --
	ctx->startDMX = true;
}



//== mp_unfreezeAV ==
//===================
static void mp_unfreezeAV(MP_CTX *ctx)
{
	//-- continue AV playback immediate --
	ctx->p.pid      = ctx->pida;
	ctx->p.pes_type = DMX_PES_AUDIO;

	ioctl (ctx->dmxa, DMX_SET_PES_FILTER, &(ctx->p));
	if(ctx->ac3 == 1)
		ioctl(ctx->adec, AUDIO_SET_BYPASS_MODE,0UL);	// bypass on

	ioctl (ctx->dmxa, DMX_START);

	ioctl(ctx->adec, AUDIO_PLAY);					// audio

	ioctl(ctx->vdec, VIDEO_PLAY);					// video
	ioctl(ctx->adec, AUDIO_SET_AV_SYNC, 1UL);	// needs sync !
}


//== mp_resyncA ==
//===================
static void mp_resyncDMX(MP_CTX *ctx)
{
	ioctl (ctx->dmxa, DMX_STOP);
	ioctl (ctx->dmxa, DMX_START);
}

// Pacemaker's stuff End

void
cleanup_stream_threads(){
// Pacemaker's stuff Start
        // Stop playing TransportStream
        nStopPlaying = 1;

        pthread_join (oTSReceiverThread, NULL);
        pthread_join (oPlayerThread, NULL);
        usleep(150000);

        // Resume zapit       
        system("/bin/pzapit -lsb");
        //int nTest = system("/bin/pzapit -p");
        //int nTest = system("/bin/zapit");
        usleep(150000);
        

// Pacemaker's stuff End
}


void
cleanup_and_exit(char *msg, int ret) {
        
        cleanup_stream_threads();

	dprintf("Error:%s\n",msg);
	if(ret==EXIT_SYSERROR)
	{
		if(msg) perror(msg);
		else perror("error");
	}
	else
	{
		if(msg) fprintf(stderr, "%s\n", msg);
	}

	fbvnc_close();
	list_destroy(global_framebuffer.overlays);
	list_destroy(sched);
	terminate=1;
}



ringbuffer_t *ringbuf;

void *mp_ReceiveStream(void *sArgument)
{
  size_t rd, rSize;
  rSize = PF_BUF_SIZE; // We let 10 Bytes left

  dprintf("ReceiverThread gestartet\n");
  dprintf("Server: %s", hostname);

  int   inFd;
  if( (inFd = mp_tcpOpen(hostname, nPort)) == -1 ) 
  { 
    dprintf("Server-Socket konnte nicht geoeffnet werden !!!!!!!\n");
    pthread_exit(NULL);
  }
  else dprintf("Server-Socket erfolgreich goeffnet.\n");

  struct pollfd pHandle;
  pHandle.fd     = inFd;
  pHandle.events = POLLIN | POLLPRI;
  char  dvrBuf[PF_BUF_SIZE];

  //ringbuffer_data_t vec[1];

  while( nStopPlaying == 0 )
  { 
    size_t iPlaceToWrite = ringbuffer_write_space (ringbuf);
    if ( rSize > iPlaceToWrite )
    {
  	usleep(100000);
	continue;
    }
    
    rd = mp_tcpRead(&pHandle, dvrBuf, rSize, PF_RD_TIMEOUT);    
    if ( rd > 0 )
    {// ToDo Put it into Ringbuffer
	ringbuffer_write(ringbuf, dvrBuf, rd);
    }
    else
    {
	ringbuffer_reset(ringbuf);
	mp_softReset(ctx, true);
	dprintf("Reset Buffer!!!\n");
    }
  }
  
  mp_tcpClose(inFd);
  sleep(1);
  // Clear Ringbuffer
  ringbuffer_free(ringbuf);
  pthread_exit(NULL);
  dprintf("ReceiverThread beendet\n");
}

void *mp_playStream(void *sArgument)
{
  int rd, rSize;
  MP_CTX mpCtx;
  ctx = &mpCtx;
  
  struct pollfd pHandle;
  
  if(mp_openDVBDevices(ctx)) 
  {
    dprintf("DVB-Devices erfolgreich geoeffnet\n");
  }
  else dprintf("Konnte einige DVB-Devices nicht oeffnen !!!!!!!!!!!!!!\n");

  //CHintBox *bufferingBox;
  //bufferingBox = new CHintBox(LOCALE_MESSAGEBOX_INFO, g_Locale->getText(LOCALE_MOVIEPLAYER_BUFFERING));	// UTF-8

  ctx->pos = 0L;
  ctx->canPause = false;

	ctx->readSize = PF_BUF_SIZE/2;
/*	
	dprintf("Server: %s", hostname);

  if( (ctx->inFd = mp_tcpOpen(hostname, nPort)) == -1 ) 
  { 
    dprintf("Server-Socket konnte nicht geoeffnet werden !!!!!!!\n");
    mp_closeDVBDevices(ctx);
    pthread_exit(NULL);
  }
  else dprintf("Server-Socket erfolgreich goeffnet.\n");
*/  

  ctx->pidv     = 99;
  ctx->pida     = 100;
  ctx->ac3      = 0;


  mp_softReset(ctx);

  //-- aspect ratio init --
	//checkAspectRatio(ctx->vdec, true);

  //-- install poller --
	pHandle.fd     = ctx->inFd;
	pHandle.events = POLLIN | POLLPRI;
	ctx->pH        = &pHandle;

  dprintf("Starte Player-Loop\n");

//  int debugzaehler = 0;
  while( nStopPlaying == 0 )
  {
    //-- after device reset read double amount of stream data --
	rSize = (ctx->startDMX)? ctx->readSize*2 : ctx->readSize;

	int readsize = ringbuffer_read_space (ringbuf);
//	if (debugzaehler == 0 ) dprintf("Buffersize %d %d\n", readsize, c);
//	if (++debugzaehler > 5 ) debugzaehler = 0;  
	if ( ctx->refillBuffer )
	{
	    if (ctx->startDMX)
	    {
		int removed = ringbuffer_remove_bad_frame(ringbuf);
		dprintf("removed %d bad frames --\n", removed);
	    }

	    dprintf("refill Buffer --\n");
	    
	    if ( readsize >= RINGBUFFERSIZE-PF_BUF_SIZE )
	    {
		//mp_resyncDMX(ctx);
		//
	        //bufferingBox->hide();
	        ctx->refillBuffer = false;
		ioctl (ctx->dmxa, DMX_START);
            }
	    else  // ToDo wait until RingBuffer is full enought
            {
		//bufferingBox->paint();
		sleep(1);
		continue;
	    }
	}

//JNJN    rd = mp_tcpRead(ctx->pH, ctx->dvrBuf, rSize, PF_RD_TIMEOUT);
    //read(ctx->pH->fd, ctx->dvrBuf, rSize);

	if(readsize > rSize)
	{
		readsize = rSize;
	}

	rd = ringbuffer_read(ringbuf, ctx->dvrBuf, (readsize/188)*188 );

    ctx->pos += rd;
		g_fileposition = ctx->pos;
    //dprintf("Puffergroesse: %d, Gelesene Bytes: %d\n",rSize, rd);

    // Falls der Datenstrom abgebrochen ist -> Neuinitialisierung versuchen. 
    // Problem: ffnetdev sendet keine Bytes, falls der VDR auf Pause steht. Dann greift der Timeout, 
    // in der funktion mp_tcpRead, was im original movieplayer zum abbruch der Verbindung führt.
    // Dieses Verhalten können wir hier aber nicht brauchen.
    if(rd != rSize && nStopPlaying == 0 )
    {
      dprintf("Datenstrom abgebrochen. Versuche Neuinitialisierung\n");
//      ringbuffer_reset(ringbuf);
//      mp_softReset(ctx);
      ioctl (ctx->dmxa, DMX_STOP);
      ctx->refillBuffer = true;
      continue;
    }

    //checkAspectRatio(ctx->vdec, false);
    
    //-- after device reset, DMX devices --
		//-- has to be started here ...      --
		mp_startDMX(ctx);	// starts only if stopped !

    //-- write stream data now --
	int done = 0;
	int wr;
	while (rd > 0) {
	    wr = write(ctx->dvr, &ctx->dvrBuf[done], rd);
	    if (wr < 0) {
    		//if (errno != EAGAIN) {
		//    dprintf("PlayStream: write error\n");
		//    nStopPlaying = 1;
		//    continue;
		//}
		//else {
		    usleep(1000);
		    continue;
		//}
	    }
	    rd   -= wr;
	    done += wr;
	}
  }

//JNJN  mp_tcpClose(ctx->inFd);
  
  mp_closeDVBDevices(ctx);

  //delete bufferingBox;
  
  sleep(1);
  
//  pthread_exit(NULL);
  ctx = NULL;

  dprintf("Player-Thread beendet\n");
}




extern "C"
{
	void *
	xmalloc(size_t s) {
		void *r;
		r=malloc(s);
		if(!r) cleanup_and_exit("out of memory", 1);
		return r;
	}
}


void
get_fbinfo() {
	struct fb_fix_screeninfo finf;
	struct fb_var_screeninfo vinf;
#define FFB fb_fd
	if(ioctl(FFB, FBIOGET_FSCREENINFO, &finf))
		cleanup_and_exit("fscreeninfo", EXIT_SYSERROR);

	if(ioctl(FFB, FBIOGET_VSCREENINFO, &vinf))
		cleanup_and_exit("vscreeninfo", EXIT_SYSERROR);
	vinf.bits_per_pixel = 16;
	if(ioctl(FFB,FBIOPUT_VSCREENINFO, &vinf) == -1 )
	{
		cleanup_and_exit("Put variable screen settings failed", EXIT_ERROR);
	}

	if(vinf.bits_per_pixel != 8*sizeof(Pixel))
	{
		printf("bpp %d 8*sizeof(Pixel)=%d\n",vinf.bits_per_pixel, 8*sizeof(Pixel));
		cleanup_and_exit("data type 'Pixel' size mismatch", EXIT_ERROR);
	}
	myFormat.bitsPerPixel = 8*sizeof(Pixel);
	myFormat.depth = vinf.bits_per_pixel;
	myFormat.trueColour = 1;
	myFormat.redShift = vinf.red.offset;
	myFormat.redMax = (1<<vinf.red.length)-1;
	myFormat.greenShift = vinf.green.offset;
	myFormat.greenMax = (1<<vinf.green.length)-1;
	myFormat.blueShift = vinf.blue.offset;
	myFormat.blueMax = (1<<vinf.blue.length)-1;
	dprintf("RGB %d/%d %d/%d %d/%d\n", myFormat.redMax, myFormat.redShift, myFormat.greenMax, myFormat.greenShift, myFormat.blueMax, myFormat.blueShift);
	global_framebuffer.p_xsize = vinf.xres;
	global_framebuffer.p_ysize = vinf.yres;
	global_framebuffer.pv_xsize = ex-sx;
	global_framebuffer.pv_ysize = ey-sy;
	global_framebuffer.p_xoff = sx;
	global_framebuffer.p_yoff = sy;

	/* Map fb into memory */
	off_t offset = 0;
	global_framebuffer.smem_len = finf.smem_len;
	if( (global_framebuffer.p_buf=(unsigned short *)mmap((void *)0,finf.smem_len,
																		  PROT_READ | PROT_WRITE,
																		  MAP_SHARED, fb_fd, offset)) == (void *)-1 )
	{
		cleanup_and_exit("fb mmap failed", EXIT_ERROR);
	}
	memset(global_framebuffer.p_buf,0 , vinf.xres * vinf.yres * (vinf.bits_per_pixel/8));
#if 0
	global_framebuffer.p_buf = f->sbuf;
	global_framebuffer.kb_fd = f->tty;
#else
#ifdef HAVE_DREAMBOX_HARDWARE
	global_framebuffer.kb_fd = open("/dev/vc/0", O_RDONLY);
	fcntl(global_framebuffer.kb_fd, F_SETFL, O_NONBLOCK);
	ms_fd = open("/dev/input/mouse0", O_RDONLY);
	ms_fd_usb = open("/dev/input/mouse1", O_RDONLY);
	fcntl(ms_fd, F_SETFL, O_NONBLOCK);
	fcntl(ms_fd_usb, F_SETFL, O_NONBLOCK);
#else
	global_framebuffer.kb_fd = -1;
#endif
#endif
}

void ShowOsd(Bool show) {
    int nTransparency;
    
    if (show)
	nTransparency = 3;
    else
	nTransparency = 8;
    //int x = 2;
    //int y = 2;

    //dprintf("blev = %d", (x << 8) | y);

    if (nTransparency> 8) nTransparency = 8;
    unsigned int c = (nTransparency << 8) | nTransparency;

    //if (ioctl(global_framebuffer.framebuf_fds,AVIA_GT_GV_SET_BLEV, (x << 8) | y) == -1)
    if (ioctl(global_framebuffer.framebuf_fds,AVIA_GT_GV_SET_BLEV, c) == -1)
    {
	printf("Error set blev\n");
    }

    // Original:
    //unsigned int c=0;
    //if (ioctl(global_framebuffer.framebuf_fds,AVIA_GT_GV_SET_BLEV, c) == -1)
    //{
    //	printf("Error set blev\n");
    //}
}


void
fbvnc_init() {
	int v_xsize = si.framebufferWidth;
	int v_ysize = si.framebufferHeight;

	global_framebuffer.ts_fd = -1;
	global_framebuffer.p_framebuf = 0;
	global_framebuffer.num_read_fds = 0;
	global_framebuffer.num_write_fds = 0;

	global_framebuffer.overlays = list_new();
	global_framebuffer.hide_overlays = 0;

	global_framebuffer.v_xsize = v_xsize;
	global_framebuffer.v_ysize = v_ysize;
	global_framebuffer.v_x0 = 0;
	global_framebuffer.v_y0 = 0;
	global_framebuffer.v_scale = gScale;
	global_framebuffer.v_bpp = sizeof(Pixel);
	global_framebuffer.v_buf = (Pixel*) xmalloc(v_xsize * v_ysize * sizeof(Pixel));

	init_keyboard();

	struct fb_var_screeninfo vinf;
	struct fb_fix_screeninfo finf;
	global_framebuffer.framebuf_fds = fb_fd;

	if(ioctl(global_framebuffer.framebuf_fds,FBIOGET_VSCREENINFO, &vinf) == -1 )
	{
		cleanup_and_exit("Get variable screen settings failed", EXIT_ERROR);
	}
	if(ioctl(global_framebuffer.framebuf_fds,FBIOGET_FSCREENINFO, &finf) == -1 )
	{
		cleanup_and_exit("Get fixed screen settings failed", EXIT_ERROR);
	}
#ifndef HAVE_DREAMBOX_HARDWARE
	if (ioctl(global_framebuffer.framebuf_fds,AVIA_GT_GV_GET_BLEV, &blev) == -1)
	{
		printf("Error get blev\n");
	}
	
	dprintf("Original blev = %d\n", blev);
	
	ShowOsd(False);
#endif

	//open avs
	if((avs = ::open(AVS, O_RDWR)) == -1)
	{
		cleanup_and_exit("open avs failed", EXIT_ERROR);
	}
	ioctl(avs, AVSIOGSCARTPIN8, &fnc_old);
	//open saa
	if((saa = ::open(SAA, O_RDWR)) == -1)
	{
		cleanup_and_exit("open saa failed", EXIT_ERROR);
	}
	ioctl(saa, SAAIOGWSS, &saa_old);

	ioctl(avs, AVSIOSSCARTPIN8, &fncmodes[screenmode]);
	ioctl(saa, SAAIOSWSS, &saamodes[screenmode]);
}

void
fbvnc_close() {

#ifdef HAVE_DREAMBOX_HARDWARE
	if (global_framebuffer.kb_fd != -1)
	{
//	    char dummy;
//	    while (read(rc_fd, &dummy, 1)> 0);
	    close(global_framebuffer.kb_fd);
	}
	if (ms_fd != -1) close(ms_fd);
	if (ms_fd_usb != -1) close(ms_fd_usb);
#endif
	/* Unmap framebuffer from memory */

	if( munmap ( (void *)global_framebuffer.p_buf, global_framebuffer.smem_len ) == -1 )
	{
		printf("FBclose: munmap failed");
	}
	struct fb_var_screeninfo vinf;
	if(ioctl(global_framebuffer.framebuf_fds,FBIOGET_VSCREENINFO, &vinf) == -1 )
	{
		printf("Get variable screen settings failed\n");
	}
	vinf.bits_per_pixel = 8;
	if(ioctl(global_framebuffer.framebuf_fds,FBIOPUT_VSCREENINFO, &vinf) == -1 )
	{
		printf("Put variable screen settings failed\n");
	}
	else
		printf("Set 8\n");
#ifndef HAVE_DREAMBOX_HARDWARE
	if (ioctl(global_framebuffer.framebuf_fds,AVIA_GT_GV_SET_BLEV, blev) == -1)
	{
		printf("Error set blev\n");
	}
#endif
	if(global_framebuffer.v_buf)
	{
		free(global_framebuffer.v_buf);
	}
	DisconnectFromRFBServer();

	//restore videoformat
	ioctl(avs, AVSIOSSCARTPIN8, &fnc_old);
	ioctl(saa, SAAIOSWSS, &saa_old);
	::close(avs);
	::close(saa);
	dprintf("Exiting\n");

}

void restore_screen() {
	struct fb_fix_screeninfo finf;
	struct fb_var_screeninfo vinf;
#define FFB fb_fd
	if(ioctl(FFB, FBIOGET_FSCREENINFO, &finf))
		cleanup_and_exit("fscreeninfo", EXIT_SYSERROR);

	if(ioctl(FFB, FBIOGET_VSCREENINFO, &vinf))
		cleanup_and_exit("vscreeninfo", EXIT_SYSERROR);
	vinf.bits_per_pixel = 8;
	if(ioctl(FFB,FBIOPUT_VSCREENINFO, &vinf) == -1 )
	{
		cleanup_and_exit("Put variable screen settings failed", EXIT_ERROR);
	}
}

struct sched
{
	int tv_sec;
	int tv_usec;
	int event;
};

int
cmp_sc(struct sched *A, struct sched *B)
{
	if(A->tv_sec < B->tv_sec) return -1;
	if(A->tv_sec > B->tv_sec) return  1;
	if(A->tv_usec < B->tv_usec) return -1;
	if(A->tv_usec > B->tv_usec) return  1;
	return 0;
}

#if 0
int
cmp_sc_p(void *a, void *b)
{
	struct sched *A = ((List*)a)->val;
	struct sched *B = ((List*)b)->val;

	return cmp_sc(A, B);
}
#endif

void
schedule_add(List *s, int ms_delta, int event)
{
	struct timeval tv;
	struct sched sc;
	List *p;

	gettimeofday(&tv, 0);
	sc.tv_sec = tv.tv_sec;
	sc.tv_usec = tv.tv_usec;
	sc.event = event;

	sc.tv_usec += (ms_delta%1000)*1000;
	while(sc.tv_usec > 1000000)
	{
		sc.tv_usec -= 1000000;
		++ sc.tv_sec;
	}
	sc.tv_sec += ms_delta / 1000;

	/* insert into sorted list */
	for(p=s; p->next; p=p->next)
	{
		if(cmp_sc(&sc, (struct sched*)p->next->val) < 0)
		{
			list_insert_copy(p, &sc, sizeof sc);
			break;
		}
	}
	if(!p->next)
	{
		list_append_copy(s, &sc, sizeof sc);
	}
}

void
schedule_delete(List *s, int event)
{
	List *p;

	for(p=s->next; p; p=p->next)
	{
		if(p->val)
		{
			struct sched *n = (struct sched*)p->val;
			if(n->event == event) break;
		}
	}

	if(p)	list_remove(s, p);
}

int
schedule_get_next(List *s, struct timeval *tv)
{
	List *p = s->next;
	struct sched *nv;

	if(!p) return 0;

	nv = (struct sched *)p->val;
	gettimeofday(tv, 0);
	tv->tv_sec = nv->tv_sec - tv->tv_sec;
	tv->tv_usec = nv->tv_usec - tv->tv_usec;

	while(tv->tv_usec < 0)
	{
		tv->tv_usec += 1000000;
		-- tv->tv_sec;
	}

	while(tv->tv_usec > 1000000)
	{
		tv->tv_usec -= 1000000;
		++ tv->tv_sec;
	}

	if(tv->tv_sec < 0)
	{
		tv->tv_sec = 0;
		tv->tv_usec = 0;
	}
	return nv->event;
}

void
fd_copy(fd_set *out, fd_set *in, int n)
{
	int i;
	FD_ZERO(out);

	for(i=0; i<n; i++)
	{
		if(FD_ISSET(i, in)) FD_SET(i, out);
	}
}

#define FD_SET_u(fd, set) do {			\
		FD_SET((fd), (set));		\
		if ((fd) > max) max=(fd);	\
	} while(0)

#define RetEvent(e) do { ev->evtype = (e); return (e); } while(0)

enum fbvnc_event 
fbvnc_get_event (fbvnc_event_t *ev, List *sched)
{
	fd_set rfds, wfds;
	int i,ret;
	enum fbvnc_event retval;
	int max;
	struct timeval timeout;
	static fbvnc_event_t nextev = {0,0,0,0,0,0,0};
	static int next_mb = -1, countevt=0;
	IMPORT_FRAMEBUFFER_VARS

    if (usepointer)
    {
		// draw additional mousepointer
		int mx = mouse_x + (mouse_x > 2 ? -2 : 0)
		                 + (mouse_x < pv_xsize -2 ? 0 : -2);
		int my = mouse_y + (mouse_y > 2 ? -2 : 0)
		                 + (mouse_y < pv_ysize -2 ? 0 : -2);
		draw_pixmap(ico_mouse, mx, my, 4, 4);
	}
	if(nextev.evtype!=0)
	{
		ev->dx = nextev.dx;
		ev->dy = nextev.dy;
		ev->x = nextev.x;
		ev->y = nextev.y;
		ev->evtype = nextev.evtype;
		ev->fd = nextev.fd;
		ev->key = nextev.key;
		if(next_mb!=-1)
			mouse_button=next_mb;
		nextev.evtype=0;
		nextev.dx=0;
		nextev.dy=0;
		nextev.x=-0;
		nextev.y=0;
		nextev.fd=0;
		nextev.key=0;
		next_mb=-1;
		RetEvent((fbvnc_event)ev->evtype);
	}
	if(sched)
	{
		int s_event;

		s_event = schedule_get_next(sched, &timeout);

		if(s_event)
		{
			ev->evtype = s_event;
		}
		else
		{
			ev->evtype = FBVNC_EVENT_TIMEOUT;
		}
	}
	else
	{
		ev->evtype = FBVNC_EVENT_TIMEOUT;
	}
	if(ev->evtype == FBVNC_EVENT_TIMEOUT)
	{
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
	}

	max = 0;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	if(kb_fd!=-1)
		FD_SET_u(kb_fd, &rfds);
		
#ifdef HAVE_DREAMBOX_HARDWARE
	if(ms_fd!=-1)
		FD_SET_u(ms_fd, &rfds);
	if(ms_fd_usb!=-1)
		FD_SET_u(ms_fd_usb, &rfds);
#endif
	FD_SET_u(rc_fd, &rfds);
	for(i=0; i<num_read_fds; i++)
	{
		if(read_fd[i] < 0) continue;
		FD_SET_u(read_fd[i], &rfds);
	}
	for(i=0; i<num_write_fds; i++)
	{
		if(write_fd[i] < 0) continue;
		FD_SET_u(write_fd[i], &wfds);
	}
	ret = select(max+1, &rfds, &wfds, 0, &timeout);

	if(!ret)
	{
		if(ev->evtype != FBVNC_EVENT_TIMEOUT)
		{
			schedule_delete(sched, ev->evtype);
		}
		return((fbvnc_event)ev->evtype);
	}

	ev->x = global_framebuffer.mouse_x;
	ev->y = global_framebuffer.mouse_y;
	ev->dx = 0;
	ev->dy = 0;

	for(i=0; i<num_read_fds; i++)
	{
		int fd = read_fd[i];
		if(fd<0)	continue;
		if(FD_ISSET(fd, &rfds))
		{
			ev->fd = fd;
			RetEvent(FBVNC_EVENT_DATA_READABLE);
		}
	}
#ifdef HAVE_DREAMBOX_HARDWARE
	static unsigned short oldkey = 0;
	if(kb_fd!=-1)
	{
		int bytesavail=0;
		ioctl(kb_fd, FIONREAD, &bytesavail);
		if (bytesavail>0)
		{
		    ev->key = 0;
			char tch[100];
			unsigned short modkey = 0;
			
			if (bytesavail > 99) bytesavail = 99;
			read(kb_fd,tch,bytesavail);
			tch[bytesavail] = 0x00;
			if (bytesavail >= 2)
			{
			    if (tch[0] == 0x1b)
			    {
				if (bytesavail >= 3 && tch[1] == 0x5b)
				{
					for (int i = 0; i < MAXKEYS; i++)
					{
						if ( tch[2] == fbvnc_keymap_dreambox[i*4] &&
							(tch[3] == fbvnc_keymap_dreambox[i*4+1] || fbvnc_keymap_dreambox[i*4+1] == 0 ) &&
							(tch[4] == fbvnc_keymap_dreambox[i*4+2] || fbvnc_keymap_dreambox[i*4+2] == 0 )
							)
						{
							ev->key = fbvnc_keymap_dreambox[i*4+3] ;
							break;
						}
					}
					
				}
				if (ev->key == 0)
				{
				    modkey = XK_Alt_L;
				    ev->key = tch[1];
				}
			    }

			}
		    else
		    {
				ev->key = tch[0] & 0x7f;
				if (tch[0] == 0x7f) ev->key = XK_BackSpace;
				if (tch[0] == 0x1b) ev->key = XK_Escape;
				if (tch[0] < 0x20) { modkey = XK_Control_L; ev->key= tch[0] + XK_a -1;}

		    }
			char dummy;
			while (read(rc_fd, &dummy, 1) > 0);
			if (ev->key) 
			{
    			    if (modkey) oldkey = 0xFFFF; else oldkey = ev->key;ig
			    if (modkey) SendKeyEvent(modkey,1);
			    SendKeyEvent(ev->key,1);
			    SendKeyEvent(ev->key,0);
			    if (modkey) SendKeyEvent(modkey,0);
			}
			    RetEvent(FBVNC_EVENT_NULL);
		}
	}
#else
	if(kb_fd!=-1)
	{
		if(FD_ISSET(kb_fd, &rfds))
		{
			unsigned char k;
			int r;
			r=read(kb_fd, &k, sizeof k);
			if(r!=sizeof k) cleanup_and_exit("read kb", EXIT_SYSERROR);

			/* debug keyboard */
			dprintf("key=%d (0x%02x)\n", k, k);
			ev->key = k&0x7f;
			RetEvent((k&0x80) ? FBVNC_EVENT_BTN_UP : FBVNC_EVENT_BTN_DOWN);
		}
	}
#endif
		struct input_event iev;
		int count = 0;
		static int rc_dx=0, rc_dy=0, step=5, keyboard_active = 0;
		static char rc_pan=0;

		memset(&iev, 0, sizeof(struct input_event));
#ifdef HAVE_DREAMBOX_HARDWARE
dprintf("reading ms_fd\n");
	if(ms_fd != -1|| ms_fd_usb != -1)
	{
		static unsigned short lastmousekey = 0xffff;
		static int slowdown = 0;
		if ( (ms_fd != -1 && FD_ISSET(ms_fd, &rfds)) || (ms_fd_usb != -1 && FD_ISSET(ms_fd_usb, &rfds)))
		{
		    unsigned char msbuf[3];
		    count = 0;
		    if (ms_fd != -1) count = read(ms_fd, &msbuf, 3);
		    if (ms_fd_usb != -1 && count < 3 ) count = read(ms_fd_usb, &msbuf, 3);
		    if (count==3)
		    {
			if ((msbuf[0] & 0xC0) == 0)
			{
			    if (msbuf[0] & 0x01) // left button
			    {
				iev.code = KEY_OK;
			    }
			    else if (msbuf[0] & 0x02) // right button
			    {
				iev.code = KEY_YELLOW;
			    }
			    else
			    {
				if (msbuf[1] > 0x08 && msbuf[1] < 0xf0)
				{
				    iev.code = (msbuf[0] &0x10 ?
					KEY_LEFT : KEY_RIGHT);
				}
				if (msbuf[2] > 0x08 && msbuf[2] < 0xf0)
				{
				    iev.code = (msbuf[0] &0x20 ?
				         (iev.code == KEY_LEFT ?
					   KEY_TOPLEFT :
				           (iev.code == KEY_RIGHT ?
					     KEY_TOPRIGHT :
					     KEY_UP)
					 ) :
				         (iev.code == KEY_LEFT ?
					   KEY_BOTTOMLEFT :
				           (iev.code == KEY_RIGHT ?
					     KEY_BOTTOMRIGHT :
					     KEY_DOWN)
					)
				     );
				}
			    }
			    if (iev.code)
			    {
    				count = sizeof(struct input_event);

    				iev.type = EV_KEY;
				
				if (lastmousekey == iev.code)
				{
				    iev.value = 2;
				    slowdown++;
				    if (slowdown == 2) slowdown = 0;
				    countevt = countevt- (slowdown== 0 ? 1 : 0);
				}
				else
				{
				    slowdown = 0;
				    iev.value = 0;
				    lastmousekey = iev.code;
				}
			    }
			    else
			    {
				RetEvent(FBVNC_EVENT_NULL);
			    }
			}
		    }
		}
	}
		if(iev.code == 0  && FD_ISSET(rc_fd, &rfds))
		{
		    static unsigned short lastkey = 0xffff;
		    unsigned short rckey = 0;

		    iev.code = 0;
		    count = read(rc_fd, &rckey, 2);
		    if(count == 2)
		    {
			switch (rckey)
			{
				case DREAM_KEY_UP:		if (oldkey != XK_Up   ) iev.code = KEY_UP;		break;
				case DREAM_KEY_DOWN:		if (oldkey != XK_Down ) iev.code = KEY_DOWN;		break;
				case DREAM_KEY_LEFT:		if (oldkey != XK_Left ) iev.code = KEY_LEFT;		break;
				case DREAM_KEY_RIGHT:		if (oldkey != XK_Right) iev.code = KEY_RIGHT;		break;
				case DREAM_KEY_OK:		iev.code = KEY_OK;		break;
				case DREAM_KEY_0:		iev.code = KEY_0;		break;
				case DREAM_KEY_1:		iev.code = KEY_1;		break;
				case DREAM_KEY_2:		iev.code = KEY_2;		break;
				case DREAM_KEY_3:		iev.code = KEY_3;		break;
				case DREAM_KEY_4:		iev.code = KEY_4;		break;
				case DREAM_KEY_5:		iev.code = KEY_5;		break;
				case DREAM_KEY_6:		iev.code = KEY_6;		break;
				case DREAM_KEY_7:		iev.code = KEY_7;		break;
				case DREAM_KEY_8:		iev.code = KEY_8;		break;
				case DREAM_KEY_9:		iev.code = KEY_9;		break;
				case DREAM_KEY_RED:		iev.code = KEY_RED;		break;
				case DREAM_KEY_GREEN:		iev.code = KEY_GREEN;		break;
				case DREAM_KEY_YELLOW:		iev.code = KEY_YELLOW;		break;
				case DREAM_KEY_BLUE:		iev.code = KEY_BLUE;		break;
				case DREAM_KEY_VOLUMEUP:	iev.code = KEY_VOLUMEUP;	break;
				case DREAM_KEY_VOLUMEDOWN:	iev.code = KEY_VOLUMEDOWN;	break;
				case DREAM_KEY_MUTE:		iev.code = KEY_MUTE;		break;
				case DREAM_KEY_HELP:		iev.code = KEY_HELP;		break;
				case DREAM_KEY_SETUP:		iev.code = KEY_SETUP;		break;
				case DREAM_KEY_HOME:		iev.code = KEY_HOME;		break;
				case DREAM_KEY_POWER:		iev.code = KEY_POWER;		break;
	    		}
			if (iev.code)
			{
    			    count = sizeof(struct input_event);
    			    iev.type = EV_KEY;
			    if (lastkey == iev.code)
			    {
				iev.value = 2;
				if (!(rckey == DREAM_KEY_UP ||
				    rckey == DREAM_KEY_DOWN ||
				    rckey == DREAM_KEY_LEFT ||
				    rckey == DREAM_KEY_RIGHT))
				    lastkey=0xffff;
			    }
			    else
			    {
				iev.value = 0;
				lastkey = iev.code;
			    }
			}
			else
			    RetEvent(FBVNC_EVENT_NULL);
		    }
		}
	if(iev.code)
	{
#else
	if(FD_ISSET(rc_fd, &rfds))
	{
		count = read(rc_fd, &iev, sizeof(struct input_event));
#endif
		if(count == sizeof(struct input_event))
		{

			dprintf("Input event: \ntime: %d.%d\ntype: %d\ncode: %d\nvalue: %d\n",
				(int)iev.time.tv_sec,(int)iev.time.tv_usec,iev.type,iev.code,iev.value);


                        // pacemaker's and Zwer2k's code start
			
			bool handled = cMenu::HandleMenu(rc_fd, iev, ev);
			dprintf("dprintf: handled: %d evtype: %d\n", handled, ev->evtype);
			if (!handled)
			{
			    if (ev->evtype == FBVNC_EVENT_NULL)
			    {
				dprintf("dprintf: Sende Code: %d Value: %d\n", iev.code, iev.value);
				printf("printf: Sende Code: %d Value: %d\n", iev.code, iev.value);
				SendKeyEvent(iev.code, iev.value);
				RetEvent(FBVNC_EVENT_NULL);
			    }
			    else
			    {
				RetEvent((fbvnc_event)ev->evtype);
			    }
			}

                        // pacemaker's and Zwer2k's code end

         
         if(iev.type == EV_KEY)
         {
            retval=FBVNC_EVENT_NULL;

            // count events for speedup
            if(iev.value == 2) // REPEAT
               countevt++;
            else
               countevt=0;			

            if(iev.code == KEY_TOPLEFT || iev.code == KEY_TOPRIGHT || iev.code == KEY_UP ||
               iev.code == KEY_BOTTOMLEFT || iev.code == KEY_BOTTOMRIGHT || iev.code == KEY_DOWN ||
               iev.code == KEY_LEFT || iev.code == KEY_RIGHT)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
#ifndef HAVE_DREAMBOX_HARDWARE
               // ignore curser events older than 350 ms
               struct timeval now;
               gettimeofday(&now,NULL);
               if((now.tv_sec > iev.time.tv_sec+1) ||
                  ((now.tv_sec == iev.time.tv_sec+1) && ((now.tv_usec+1000000) - iev.time.tv_usec) > 350000) ||
                  ((now.tv_sec == iev.time.tv_sec) && ((now.tv_usec) - iev.time.tv_usec) > 350000))
               {
                  dprintf("Ignoring old cursor event\n");
                  RetEvent(FBVNC_EVENT_NULL);
               }
#endif
			   if (keyboard_active)
			   {
				   nextev.evtype = FBVNC_EVENT_TS_UP;
                   nextev.x = global_framebuffer.mouse_x;
                   nextev.y = global_framebuffer.mouse_y;
                   nextev.key = iev.code;
                   ev->key = iev.code;
				   RetEvent(FBVNC_EVENT_TS_DOWN);
			   }
               if(rc_pan)
                  step=STEP_PAN;
               else
               {
                  if(countevt>20) step=80;
                  else if(countevt>15) step=40;
                  else if(countevt>10) step=20;
                  else if(countevt >5) step=10;
                  else step=5;
               }
               retval=FBVNC_EVENT_TS_MOVE;
               if(iev.code == KEY_TOPLEFT || iev.code == KEY_TOPRIGHT || iev.code == KEY_UP)
               {
                  if(global_framebuffer.mouse_y == 0 && global_framebuffer.v_y0==0)
                  {
                     RetEvent(FBVNC_EVENT_NULL);
                  }
                  if(!rc_pan)
                  {
                     global_framebuffer.mouse_y -= step;
                     if(global_framebuffer.mouse_y <0)
                     {
                        if(step < STEP_PAN)
                           step=STEP_PAN;
                        if(global_framebuffer.v_y0<step)
                           step=global_framebuffer.v_y0;
                        global_framebuffer.mouse_y = 0;
                        rc_dy = step;
                        ev->key = hbtn.pan;
                        retval=(fbvnc_event) (FBVNC_EVENT_BTN_DOWN | FBVNC_EVENT_TS_MOVE);

                        nextev.key = hbtn.pan;
                        nextev.x = global_framebuffer.mouse_x;
                        nextev.y = global_framebuffer.mouse_y;
                        nextev.evtype = (fbvnc_event) (FBVNC_EVENT_BTN_UP | FBVNC_EVENT_TS_MOVE);
                     }
                  }
                  else
                     rc_dy	= step;
               }
               if(iev.code == KEY_BOTTOMLEFT || iev.code == KEY_BOTTOMRIGHT || iev.code == KEY_DOWN)
               {
                  if(global_framebuffer.mouse_y >= global_framebuffer.pv_ysize &&
                     global_framebuffer.v_y0 + global_framebuffer.pv_ysize >= global_framebuffer.v_ysize)
                  {
                     RetEvent(FBVNC_EVENT_NULL);
                  }
                  if(!rc_pan)
                  {
                     global_framebuffer.mouse_y += step;
                     if(global_framebuffer.mouse_y >= global_framebuffer.pv_ysize)
                     {
                        if(step < STEP_PAN)
                           step=STEP_PAN;
                        if((global_framebuffer.v_ysize - global_framebuffer.v_y0 - global_framebuffer.pv_ysize) < step)
                           step=(global_framebuffer.v_ysize - global_framebuffer.v_y0 - global_framebuffer.pv_ysize);
                        global_framebuffer.mouse_y = global_framebuffer.pv_ysize - 1;
                        rc_dy = -step;
                        ev->key = hbtn.pan;
                        retval=(fbvnc_event) (FBVNC_EVENT_BTN_DOWN | FBVNC_EVENT_TS_MOVE);

                        nextev.key = hbtn.pan;
                        nextev.x = global_framebuffer.mouse_x;
                        nextev.y = global_framebuffer.mouse_y;
                        nextev.evtype = (fbvnc_event) (FBVNC_EVENT_BTN_UP | FBVNC_EVENT_TS_MOVE);
                     }
                  }
                  else
                     rc_dy	=-step;
               }
               if(iev.code == KEY_TOPLEFT || iev.code == KEY_BOTTOMLEFT || iev.code == KEY_LEFT)
               {
                  if(global_framebuffer.mouse_x == 0 && global_framebuffer.v_x0==0)
                  {
                     RetEvent(FBVNC_EVENT_NULL);
                  }
                  if(!rc_pan)
                  {
                     global_framebuffer.mouse_x -= step;
                     if(global_framebuffer.mouse_x <0)
                     {
                        if(step < STEP_PAN)
                           step=STEP_PAN;
                        if(global_framebuffer.v_x0<step)
                           step=global_framebuffer.v_x0;
                        global_framebuffer.mouse_x = 0;
                        rc_dx = step;
                        ev->key = hbtn.pan;
                        retval=(fbvnc_event) (FBVNC_EVENT_BTN_DOWN | FBVNC_EVENT_TS_MOVE);

                        nextev.key = hbtn.pan;
                        nextev.x = global_framebuffer.mouse_x;
                        nextev.y = global_framebuffer.mouse_y;
                        nextev.evtype = (fbvnc_event) (FBVNC_EVENT_BTN_UP | FBVNC_EVENT_TS_MOVE);
                     }
                  }
                  else
                     rc_dx	=step;
               }
               if(iev.code == KEY_TOPRIGHT || iev.code == KEY_BOTTOMRIGHT || iev.code == KEY_RIGHT)
               {
                  if(global_framebuffer.mouse_x >= global_framebuffer.pv_xsize &&
                     global_framebuffer.v_x0 + global_framebuffer.pv_xsize >= global_framebuffer.v_xsize)
                  {
                     RetEvent(FBVNC_EVENT_NULL);
                  }
                  if(!rc_pan)
                  {
                     global_framebuffer.mouse_x += step;
                     if(global_framebuffer.mouse_x >= global_framebuffer.pv_xsize)
                     {
                        if(step < STEP_PAN)
                           step=STEP_PAN;
                        if((global_framebuffer.v_xsize - global_framebuffer.v_x0 - global_framebuffer.pv_xsize) < step)
                           step=(global_framebuffer.v_xsize - global_framebuffer.v_x0 - global_framebuffer.pv_xsize);
                        global_framebuffer.mouse_x = global_framebuffer.pv_xsize - 1;
                        rc_dx = -step;
                        ev->key = hbtn.pan;
                        retval=(fbvnc_event) (FBVNC_EVENT_BTN_DOWN | FBVNC_EVENT_TS_MOVE);

                        nextev.key = hbtn.pan;
                        nextev.x = global_framebuffer.mouse_x;
                        nextev.y = global_framebuffer.mouse_y;
                        nextev.evtype = (fbvnc_event) (FBVNC_EVENT_BTN_UP | FBVNC_EVENT_TS_MOVE);
                     }
                  }
                  else
                     rc_dx	=-step;
               }
            }
            else if(countevt>0)
            {
               // for othe rkeys reject because of prelling
               RetEvent(FBVNC_EVENT_NULL);
            }
            // codes
            else if(iev.code == KEY_HOME)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               retval = FBVNC_EVENT_QUIT;
            }

            else if(iev.code == KEY_HELP)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               if(mouse_button > 0)
               {
                  mouse_button=0;
                  retval=FBVNC_EVENT_TS_UP;
               }
               else
               {
                  mouse_button=1;
                  retval=FBVNC_EVENT_TS_DOWN;
               }
            }
            else if(iev.code == KEY_MUTE)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               ev->key = hbtn.pan;
               if(rc_pan)
                  retval=FBVNC_EVENT_BTN_UP;
               else
                  retval=FBVNC_EVENT_BTN_DOWN;
               rc_pan=!rc_pan;
            }
            else if(iev.code == KEY_VOLUMEDOWN)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               global_framebuffer.mouse_x = global_framebuffer.mouse_y = 0;
               retval = (fbvnc_event) (FBVNC_EVENT_ZOOM_OUT | FBVNC_EVENT_TS_MOVE);
            }
            else if(iev.code == KEY_VOLUMEUP)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               global_framebuffer.mouse_x = global_framebuffer.mouse_y = 0;
               retval = (fbvnc_event) (FBVNC_EVENT_ZOOM_IN | FBVNC_EVENT_TS_MOVE);
            }
            else if(iev.code == KEY_OK)
            {
               if(iev.value==1 && !keyboard_active)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               nextev.x =global_framebuffer.mouse_x;
               nextev.y =global_framebuffer.mouse_y;  
               nextev.key = 0;
               nextev.evtype = FBVNC_EVENT_TS_UP;
               next_mb=0;
               mouse_button=0;
               if (keyboard_active)
               {
	               nextev.key = KEY_OK;
               	   ev->key = KEY_OK;
			   }
			   else
               mouse_button=1;
               retval=FBVNC_EVENT_TS_DOWN;
            }
            else if(iev.code == KEY_RED)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               retval=FBVNC_EVENT_DCLICK;				
            }
            else if(iev.code == KEY_GREEN)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               nextev.x =global_framebuffer.mouse_x;
               nextev.y =global_framebuffer.mouse_y;  
               nextev.evtype = FBVNC_EVENT_TS_UP;
               next_mb=0;
               mouse_button=2;
               retval=FBVNC_EVENT_TS_DOWN;
            }
            else if(iev.code == KEY_YELLOW)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               nextev.x =global_framebuffer.mouse_x;
               nextev.y =global_framebuffer.mouse_y;  
               nextev.evtype = FBVNC_EVENT_TS_UP;
               next_mb=0;
               mouse_button=4;
               retval=FBVNC_EVENT_TS_DOWN;
            }
            else if(iev.code == KEY_BLUE)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               toggle_keyboard();
               keyboard_active = 1- keyboard_active;
            }
            else if(iev.code == KEY_SETUP)
            {
               if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
				screenmode = 1-screenmode;
				ioctl(avs, AVSIOSSCARTPIN8, &fncmodes[screenmode]);
				ioctl(saa, SAAIOSWSS, &saamodes[screenmode]);
            }
            else if(iev.code == BTN_LEFT)
            {
               if(iev.value==1) // key pressed event
					{
                  mouse_button |= 1;
                  retval=FBVNC_EVENT_TS_DOWN;
					}
					else
					{
						mouse_button &= ~1;
                  retval=FBVNC_EVENT_TS_UP;
               }
            }
            else if(iev.code == BTN_RIGHT)
            {
               if(iev.value==1) // key pressed event
					{
                  mouse_button |= 4;
					}
					else
					{
						mouse_button &= ~4;
               }
					retval=FBVNC_EVENT_TS_MOVE;
            }
            else if(iev.code == BTN_MIDDLE)
            {
               if(iev.value==1) // key pressed event
					{
                  mouse_button |= 2;
					}
					else
					{
						mouse_button &= ~2;
               }
					retval=FBVNC_EVENT_TS_MOVE;
            }

            else
            {
	      
               ev->key = iev.code;
               if(iev.value == 1) // key pressed event
                  retval = FBVNC_EVENT_BTN_DOWN;
               else // key released event
                  retval=FBVNC_EVENT_BTN_UP;
	       //SendKeyEvent(iev.code, 1);
	       //RetEvent(FBVNC_EVENT_NULL);
            }

            // action
            ev->x =global_framebuffer.mouse_x;
            ev->y =global_framebuffer.mouse_y;
            ev->dx=rc_dx;
            ev->dy=rc_dy;
            rc_dx = rc_dy = 0;

            RetEvent(retval);
         }
         else if(iev.type == EV_REL)
         {
            retval=FBVNC_EVENT_TS_MOVE;
            if(iev.code == REL_Y)
            {
               if (iev.value < 0) //Up
               {
						if(global_framebuffer.mouse_y == 0 && global_framebuffer.v_y0==0)
                  {
                     RetEvent(FBVNC_EVENT_NULL);
                  }
                  if(!rc_pan)
                  {
							global_framebuffer.mouse_y += iev.value;
							if(global_framebuffer.mouse_y <0)
                     {
                        if(global_framebuffer.v_y0< (int)(-iev.value))
                           rc_dy=global_framebuffer.v_y0;
                        else
                           rc_dy=(-iev.value);
                        global_framebuffer.mouse_y = 0;
                        ev->key = hbtn.pan;
                        retval=(fbvnc_event) (FBVNC_EVENT_BTN_DOWN | FBVNC_EVENT_TS_MOVE);

                        nextev.key = hbtn.pan;
                        nextev.x = global_framebuffer.mouse_x;
                        nextev.y = global_framebuffer.mouse_y;
                        nextev.evtype = (fbvnc_event) (FBVNC_EVENT_BTN_UP | FBVNC_EVENT_TS_MOVE);
                     }
                  }
                  else
                     rc_dy	= (-iev.value);
               }
               else //Down
               {
                  if(global_framebuffer.mouse_y >= global_framebuffer.pv_ysize &&
                     global_framebuffer.v_y0 + global_framebuffer.pv_ysize >= global_framebuffer.v_ysize)
                  {
                     RetEvent(FBVNC_EVENT_NULL);
                  }
                  if(!rc_pan)
                  {
                     global_framebuffer.mouse_y += iev.value;
                     if(global_framebuffer.mouse_y >= global_framebuffer.pv_ysize)
                     {
                        if((global_framebuffer.v_ysize - global_framebuffer.v_y0 - global_framebuffer.pv_ysize) < (int) iev.value)
                           rc_dy=-(global_framebuffer.v_ysize - global_framebuffer.v_y0 - global_framebuffer.pv_ysize);
                        else
                           rc_dy=-(iev.value);
                        global_framebuffer.mouse_y = global_framebuffer.pv_ysize - 1;
                        ev->key = hbtn.pan;
                        retval=(fbvnc_event) (FBVNC_EVENT_BTN_DOWN | FBVNC_EVENT_TS_MOVE);

                        nextev.key = hbtn.pan;
                        nextev.x = global_framebuffer.mouse_x;
                        nextev.y = global_framebuffer.mouse_y;
                        nextev.evtype = (fbvnc_event) (FBVNC_EVENT_BTN_UP | FBVNC_EVENT_TS_MOVE);
                     }
                  }
                  else
                     rc_dy	= iev.value;
               }
            }
            else if(iev.code == REL_X)
            {
               if (iev.value < 0) //Left
					{
						if(global_framebuffer.mouse_x == 0 && global_framebuffer.v_x0==0)
						{
							RetEvent(FBVNC_EVENT_NULL);
						}
						if(!rc_pan)
						{
							global_framebuffer.mouse_x += iev.value;
							if(global_framebuffer.mouse_x <0)
							{
								if(global_framebuffer.v_x0< (-(int)iev.value))
									rc_dx=global_framebuffer.v_x0;
								else
									rc_dx = (-iev.value);
								global_framebuffer.mouse_x = 0;
								ev->key = hbtn.pan;
								retval=(fbvnc_event) (FBVNC_EVENT_BTN_DOWN | FBVNC_EVENT_TS_MOVE);

								nextev.key = hbtn.pan;
								nextev.x = global_framebuffer.mouse_x;
								nextev.y = global_framebuffer.mouse_y;
								nextev.evtype = (fbvnc_event) (FBVNC_EVENT_BTN_UP | FBVNC_EVENT_TS_MOVE);
							}
						}
						else
							rc_dx	=step;
					}
					else /* Right */
					{
						if(global_framebuffer.mouse_y >= global_framebuffer.pv_ysize &&
							global_framebuffer.v_y0 + global_framebuffer.pv_ysize >= global_framebuffer.v_ysize)
						{
							RetEvent(FBVNC_EVENT_NULL);
						}
						if(!rc_pan)
						{
							global_framebuffer.mouse_x += iev.value;
							if(global_framebuffer.mouse_x >= global_framebuffer.pv_xsize)
							{
								if((global_framebuffer.v_xsize - global_framebuffer.v_x0 - global_framebuffer.pv_xsize) < (int)iev.value)
									rc_dx=-(global_framebuffer.v_xsize - global_framebuffer.v_x0 - global_framebuffer.pv_xsize);
								else
									rc_dx=-(iev.value);
								global_framebuffer.mouse_x = global_framebuffer.pv_xsize - 1;
								ev->key = hbtn.pan;
								retval=(fbvnc_event) (FBVNC_EVENT_BTN_DOWN | FBVNC_EVENT_TS_MOVE);
								
								nextev.key = hbtn.pan;
								nextev.x = global_framebuffer.mouse_x;
								nextev.y = global_framebuffer.mouse_y;
								nextev.evtype = (fbvnc_event) (FBVNC_EVENT_BTN_UP | FBVNC_EVENT_TS_MOVE);
							}
						}
						else
							rc_dy	= iev.value;
					}
				}
            else
               retval=FBVNC_EVENT_NULL;

            ev->x =global_framebuffer.mouse_x;
            ev->y =global_framebuffer.mouse_y;
            ev->dx = rc_dx;
            ev->dy = rc_dy;
            rc_dx = rc_dy = 0;
            RetEvent(retval);
         }
      }
      else
         RetEvent(FBVNC_EVENT_NULL);
	}
	for(i=0; i<num_write_fds; i++)
	{
		int fd = write_fd[i];
		if(fd<0)	continue;
		if(FD_ISSET(fd, &wfds))
		{
			ev->fd = fd;
			RetEvent(FBVNC_EVENT_DATA_WRITABLE);
		}
	}
	cleanup_and_exit("unhandled event", EXIT_ERROR);
	return(fbvnc_event)0;
}

void 
scaledPointerEvent(int xp, int yp, int button) {
	IMPORT_FRAMEBUFFER_VARS

	SendPointerEvent(xp*v_scale + v_x0, yp*v_scale + v_y0, button);
}

bool img_saved = 0;
List *sched;

#define FDNUM_VNC 0
#define FDNUM_PNM 1

void
save_viewport(struct viewport *dst) {
	IMPORT_FRAMEBUFFER_VARS

	dst->v_xsize = v_xsize;
	dst->v_ysize = v_ysize;
	dst->v_buf = v_buf;
	dst->v_scale = v_scale;
}

void
activate_viewport(struct viewport *src) {
	global_framebuffer.v_xsize = src->v_xsize;
	global_framebuffer.v_ysize = src->v_ysize;
	global_framebuffer.v_buf = src->v_buf;
	global_framebuffer.v_scale = src->v_scale;
}

static struct viewport v_img;

void
open_pnm_fifo(int *fdp)
{
	dprintf("open_pnm_fifo()\n");
	int pnmfd;
	pnmfd = open(pnmFifo, O_RDONLY | O_NDELAY);
	if(pnmfd < 0)
	{
		perror("can't open pnmfifo");
	}
	*fdp = pnmfd;
}

void
load_pnm_image(int *fdp)
{
	dprintf("load_pnm_image()\n");
	static FILE *f = 0;
	int fd;

	if(!f)
	{
		fd = *fdp;
		fcntl(fd, F_SETFL, 0); /* clear non-blocking mode */
		f = fdopen(fd, "rb");
		if(!f) return;

		v_img.v_scale = 1;
		v_img.v_x0 = 0;
		v_img.v_y0 = 0;
		v_img.v_bpp = 16;
	}

	if(img_read(&v_img, f))
	{
		img_saved = 1;
	}
	else
	{
		fclose(f);
		f=0;
		open_pnm_fifo(fdp);
	}
}

void
show_pnm_image() {
	dprintf("show_pnm_image()\n");
	struct viewport v_save;
	static int kx0=0, ky0=0;

	img_saved = 0;
	save_viewport(&v_save);
	activate_viewport(&v_img);
	vp_pan_virt(kx0, ky0);

	while(1)
	{
		fbvnc_event_t ev;

		fbvnc_get_event(&ev, 0);
		
		#ifndef HAVE_DREAMBOX_HARDWARE
		if ( ev.evtype != FBVNC_EVENT_NULL )
		{
			setMoreTransparency( false );
		}
		else
		{
			setMoreTransparency();
		}
		#endif


		if(ev.evtype & FBVNC_EVENT_TS_MOVE)
		{
			vp_pan(-ev.dx, -ev.dy);
		}
		if(ev.evtype & FBVNC_EVENT_BTN_DOWN)
		{
			int key;
			key = key_map(ev.key);

			if(key==XK_q || key==XK_Q || key==XK_Escape)
			{
				break;
			}
			else if(key==XK_k)
			{
				kx0 = global_framebuffer.v_x0;
				ky0 = global_framebuffer.v_y0;
			}
			else if(key==XK_space || key==XK_Return)
			{
				save_viewport(&v_img);
				break; /* next image */
			}
			else if(key==XK_z)
			{
				save_viewport(&v_img);
				img_saved=1;
				break;
			}
			else
			{
				fn_action = 1;
				key_special_action(key);
				fn_action = 0;
			}
		}
	}
	if(!img_saved)
	{
		printf("0x%X : Freeed\n",v_img.v_buf);
		free(v_img.v_buf);
	}

	fn_action = 0;
	activate_viewport(&v_save);
	vp_pan(0, 0);

}

extern "C" {
	void plugin_exec(PluginParam *par) {
		fbvnc_overlay_t *active_overlay = 0;
		int panning = 0;
		int readfd[2];	/* pnmfd and/or rfbsock */
		static int pnmfd = -1;
		int lcd;


		fb_fd = lcd = rc_fd = sx = ex = sy = ey = -1;
		for(; par; par = par->next)
		{
			if(!strcmp(par->id, P_ID_FBUFFER))
			{
				fb_fd = atoi(par->val);
			}
			else if(!strcmp(par->id, P_ID_LCD))
			{
				lcd = atoi(par->val);
			}
			else if(!strcmp(par->id, P_ID_RCINPUT))
			{
				rc_fd = atoi(par->val);
			}
			else if(!strcmp(par->id, P_ID_OFF_X))
			{
				//sx = atoi(par->val);
                                sx = 0;
			}
			else if(!strcmp(par->id, P_ID_END_X))
			{
				ex = atoi(par->val);
			}
			else if(!strcmp(par->id, P_ID_OFF_Y))
			{
				//sy = atoi(par->val);
                                sy = 0;
			}
			else if(!strcmp(par->id, P_ID_END_Y))
			{
				ey = atoi(par->val);
			}
		}
		if(fb_fd == -1 || lcd == -1 || rc_fd == -1 || sx == -1 || ex == -1 || sy == -1 || ey == -1)
		{
			printf("FBVNC <Invalid Param(s)>\n");
			return;
		}

		screenmode = 0;
		get_fbinfo();

		char szServerNr[20] = "";
		char szServer[20];
		char szPort[20];
		char szScale[20];
		char szServerScale[20];
		char szPasswd[20];
		char szMouse[20];
#ifdef HAVE_DREAMBOX_HARDWARE
		unsigned short dummy;
		while (read(rc_fd, &dummy, 2) > 0);
#endif
		if (selectServer(szServerNr, rc_fd) == 0)
		{
			cleanupFT();
			if( munmap ( (void *)global_framebuffer.p_buf, global_framebuffer.smem_len ) == -1 )
			{
				printf("FBclose: munmap failed");
			}
			restore_screen();
			return;
		}
		sprintf(szServer      ,"server%s",szServerNr);
		sprintf(szPort        ,"port%s",szServerNr);
		sprintf(szScale       ,"scale%s",szServerNr);
		sprintf(szServerScale ,"server_scale%s",szServerNr);
		sprintf(szPasswd      ,"passwd%s",szServerNr);
		sprintf(szMouse       ,"mouse%s",szServerNr);
		
		
#ifdef HAVE_DREAMBOX_HARDWARE

		strcpy(hostname,"vnc");
		gScale=1;
		serverScaleFactor = 1;
		rcCycleDuration = 225*1000;
		rcTest = 0;
		*passwdString = 0x00;
		debug = 0;
		char line[256], *p;
		FILE* fp = fopen( CONFIGDIR "/vdr.conf", "r" );
		if ( !fp )
		{
			printf("vdrviewer: could not open " CONFIGDIR "/vdr.conf !!!\n");
		}
		else
		{
			while( fgets( line, 128, fp ) )
			{
				if ( *line == '#' )	continue;
				if ( *line == ';' )	continue;
				p=strchr(line,'\n');
				if ( p ) *p=0;
				p=strchr(line,'=');
				if ( !p ) continue;
				*p=0;
				p++;
				if      ( !strcmp(line,szServer           ) ) strcpy(hostname, p);
				else if ( !strcmp(line,szPort             ) ) port = atoi(p);
				else if ( !strcmp(line,szScale            ) ) gScale = atoi(p);
				else if ( !strcmp(line,szServerScale      ) ) serverScaleFactor = atoi(p);
				else if ( !strcmp(line,"rc_cycle_duration") ) rcCycleDuration  = atoi(p)*1000;
				else if ( !strcmp(line,"rc_test"          ) ) rcTest = atoi(p);
				else if ( !strcmp(line,szPasswd           ) ) strcpy(passwdString, p);
				else if ( !strcmp(line,szMouse            ) ) usepointer = atoi(p);
				else if ( !strcmp(line,"debug"            ) ) debug = atoi(p);
				else if ( !strcmp(line,"screenmode"       ) ) screenmode = atoi(p);
			}
			fclose(fp);
		}
#else
		CConfigFile config('\t');
		config.loadConfig(CONFIGDIR "/vdr.conf"); // ToDo JNJN
		strncpy(hostname, config.getString(szServer,"vnc").c_str(), 254);
		port=config.getInt32(szPort,5900);
		gScale=config.getInt32(szScale,1);
		serverScaleFactor = config.getInt32(szServerScale,1);
		rcCycleDuration = config.getInt32("rc_cycle_duration",225)*1000;
		rcTest = config.getInt32("rc_test",0);
		strcpy(passwdString,config.getString(szPasswd,"").substr(0,8).c_str());
		//debug = config.getInt32("debug",0);
		debug = 1;
		screenmode = config.getInt32("screenmode",0);
#endif
		if(gScale > 4 || gScale < 1)
			gScale=1;
		global_framebuffer.v_scale = gScale;
		
    // Start TEST of pacemaker

    system("/bin/pzapit -esb");
    //int nTest = system("/bin/pzapit -kill");
    //int nTest = system("/bin/pzapit -p");
    //	g_Zapit->setStandby(true);
    //dprintf("Forcing zapit to go to standby: %d\n", nTest);
    usleep(350000);



    nStopPlaying = 0;
    const char *sArgument = "bla";

    
    ringbuf = ringbuffer_create(RINGBUFFERSIZE);

    if(pthread_create(&oTSReceiverThread, 0, mp_ReceiveStream, (void *)sArgument) != 0)
    {
      dprintf("Couldn't create oTSReceiverThread\n");
    }
    else
    {
      dprintf("Successfully created oTSReceiverThread\n");
    }	

    if(pthread_create(&oPlayerThread, 0, mp_playStream, (void *)sArgument) != 0)
    {
      dprintf("Couldn't create player thread\n");
    }
    else
    {
      dprintf("Successfully created player thread\n");
    }	
// End TEST of pacemaker
		
		
		if(strlen(passwdString) == 0)
		{
			passwdFile = CONFIGDIR "/vncpasswd";
		}
		sched = list_new();
		terminate=0;
		requestedDepth = 16;
		forceTruecolour = True;
		kbdRate=4;
#if 0
		processArgs(argc, argv);
#endif
		signal(SIGPIPE, SIG_IGN);

		if(!ConnectToRFBServer(hostname, port))
		{
			MessageBox("Cannot connect", rc_fd);
			dprintf("Cannot connect\n");
			restore_screen();
			list_destroy(sched);
			if( munmap ( (void *)global_framebuffer.p_buf, global_framebuffer.smem_len ) == -1 )
			{
				printf("FBclose: munmap failed");
			}
			cleanup_stream_threads();
			cleanupFT();
			return;
		}

		dprintf("InitialiseRFBConnection()\n");
		if(!InitialiseRFBConnection(rfbsock))
		{
			MessageBox("Cannot initialize", rc_fd);
			dprintf("Cannot initialize\n");
			restore_screen();
			list_destroy(sched);
			if( munmap ( (void *)global_framebuffer.p_buf, global_framebuffer.smem_len ) == -1 )
			{
				printf("FBclose: munmap failed");
			}
			cleanup_stream_threads();
			cleanupFT();
			return;
		}

		fbvnc_init();
		dprintf("Overlays init\n");
		overlays_init();
		dprintf("toggle key\n");
		toggle_keyboard();

		
		dprintf("SetFormat\n");
		if(!SetFormatAndEncodings())
		{
		    	MessageBox("Cannot set encodings", rc_fd);
			cleanup_and_exit("encodings", EXIT_ERROR);
			return;
		}
		
		dprintf("SetScaleFactor()\n");
 		if(serverScaleFactor > 1)
		{
			if(!SetScaleFactor())
			{
				cleanup_and_exit("server side scale", EXIT_ERROR);
				return;
			}
			// workaround for ultravnc, ultravnc sends FramebufferChange Msg after update only
			if(!SendFramebufferUpdateRequest(0, 0, 1, 1, False))
			{
				cleanup_and_exit("update request", EXIT_ERROR);
				return;
			}
		}
		else
		{
			if(!SendFramebufferUpdateRequest(0, 0, si.framebufferWidth, si.framebufferHeight, False))
			{
				cleanup_and_exit("update request", EXIT_ERROR);
				return;
			}
		}

		global_framebuffer.mouse_x = global_framebuffer.pv_xsize / 2;
		global_framebuffer.mouse_y = global_framebuffer.pv_ysize / 2;
		scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, 0);
		
		global_framebuffer.num_read_fds = 1;
		global_framebuffer.num_write_fds = 0;
		global_framebuffer.read_fd = readfd;
		readfd[FDNUM_VNC] = rfbsock;
		readfd[FDNUM_PNM]=-1;
		
		if(pnmFifo)
		{
			open_pnm_fifo(&pnmfd);
		}
		
		schedule_add(sched, 1000, FBVNC_EVENT_TICK_SECOND);
	

		sendUpdateRequest=false;
		while(!terminate)
		{
			fbvnc_event_t ev;

			if(sendUpdateRequest)
			{
				int msWait;
				struct timeval tv;
				gettimeofday(&tv, 0);
				msWait = (updateRequestPeriodms +
							 ((updateRequestTime.tv_sec - tv.tv_sec) *1000) +
							 ((updateRequestTime.tv_usec-tv.tv_usec) /1000));

				if(msWait > 3000)
				{
					/* bug workaround - max wait time to guard
					 * against time jump */
					msWait = 1000;
				}

				if(msWait <= 0)
				{
					if(!SendIncrementalFramebufferUpdateRequest())
					{
						cleanup_and_exit("inc update", EXIT_ERROR);
						continue;
					}
				}
				else
				{
					schedule_add(sched, msWait, FBVNC_EVENT_SEND_UPDATE_REQUEST);
				}

			}

			if(img_saved)
			{
				readfd[FDNUM_PNM] = -1;
			}
			else if(pnmFifo)
			{
				readfd[FDNUM_PNM] = pnmfd;
			}
			fbvnc_get_event(&ev, sched);

			if(ev.evtype == FBVNC_EVENT_NULL)
			{
				//nothing yet
			}
			else
			{
				if(ev.evtype & FBVNC_EVENT_QUIT)
				{
					cleanup_and_exit("QUIT", EXIT_OK);
					continue;
				}
				if(ev.evtype & FBVNC_EVENT_TICK_SECOND)
				{
					check_overlays(&ev);
					schedule_delete(sched, FBVNC_EVENT_TICK_SECOND);
					schedule_add(sched, 1000, FBVNC_EVENT_TICK_SECOND);
				}
				else
					dprintf("Event %X\n",ev.evtype);

				if(ev.evtype & FBVNC_EVENT_SEND_UPDATE_REQUEST)
				{
					if(!SendIncrementalFramebufferUpdateRequest())
					{
						cleanup_and_exit("inc update", EXIT_ERROR);
						continue;
					}	
				}
				if(ev.evtype & FBVNC_EVENT_DATA_READABLE)
				{
					if(ev.fd >= 0)
					{
						if(ev.fd == readfd[FDNUM_VNC])
						{
							if(!HandleRFBServerMessage())
							{
								cleanup_and_exit("rfb server closed connection", EXIT_ERROR);
								continue;
							}
						}
						else if(pnmFifo && ev.fd == pnmfd)
						{
							load_pnm_image(&pnmfd);
							if(img_saved) show_pnm_image();
						}
						else
						{
							fprintf(stderr, "Got data on filehandle %d?!",
									  ev.fd);
							cleanup_and_exit("bad data", EXIT_ERROR);
							continue;
						}
					}
				}
				if(ev.evtype & FBVNC_EVENT_BTN_DOWN)
				{
					if(btn_state[ev.key])
					{
						static bool warned=0;
						if(!warned)
						{
							fprintf(stderr, "got hardware keyrepeat?!\n");
							warned=1;
						}
						rep_key = 0;
					}
					dprintf("Removing Keyrep1\n");
					schedule_delete(sched, FBVNC_EVENT_KEYREPEAT);
					btn_state[ev.key] = 1;

					if(active_overlay)
					{
						overlay_event(&ev, active_overlay, 0);
					}
					else if(ev.key==hbtn.mouse1)
					{
						mouse_button = 1;
 						scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, mouse_button);
					}
					else if(ev.key==hbtn.mouse2)
					{
						mouse_button = 2;
 						scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, mouse_button);
					}
					else if(ev.key==hbtn.mouse3)
					{
						mouse_button = 4;
 						scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, mouse_button);
					}
					else if(ev.key==hbtn.pan)
					{
						vp_hide_overlays();
						panning = 1;
					}
					else
					{
						key_press(ev.key);
					}
				}
				if(ev.evtype & FBVNC_EVENT_BTN_UP)
				{
					btn_state[ev.key] = 0;
					dprintf("Removing Keyrep1\n");
					schedule_delete(sched, FBVNC_EVENT_KEYREPEAT);
					rep_key = 0;
					if(active_overlay)
					{
						overlay_event(&ev, active_overlay, 0);
					}
					else if(ev.key==hbtn.mouse1 ||
							  ev.key==hbtn.mouse2 ||
							  ev.key==hbtn.mouse3 )
					{
						mouse_button = 0;
						scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, 0);
					}
					else if(ev.key==hbtn.pan)
					{
						if(panning == 1)
						{
							/* no mouse move events, toggle overlay */
							if(global_framebuffer.hide_overlays > 1)
							{
								/* was hidden, restore it */
								vp_restore_overlays();
								vp_restore_overlays();
							}
							else
							{
								/* keep overlay hidden */
							}
							++ pan_toggle_count;
							if(pan_toggle_count > 7)
							{
								pan_toggle_count = 0;
								vp_pan(0, 0);
							}
						}
						else
						{
							vp_restore_overlays();
						}
						scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, mouse_button);
						panning = 0;
					}
					else if(ev.key==hbtn.action)
					{
						/* nothing to do ?! */
					}
					else
					{
						key_release(ev.key);
					}
				}
				if(ev.evtype & FBVNC_EVENT_TS_DOWN)
				{
					pan_toggle_count = 0;
					active_overlay = check_overlays(&ev);
					if(!active_overlay)
					{
						if(panning)
						{
							panning = 2;
						}
						else
						{
							global_framebuffer.mouse_x = ev.x;
							global_framebuffer.mouse_y = ev.y;
							scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, mouse_button);
						}
					}
				}
				if(ev.evtype & FBVNC_EVENT_TS_MOVE)
				{
					if(active_overlay)
					{
						overlay_event(&ev, active_overlay, 0);
					}
					else if(panning)
					{
						vp_pan(-ev.dx, -ev.dy);
					}
					else
					{
						global_framebuffer.mouse_x = ev.x;
						global_framebuffer.mouse_y = ev.y;
						scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, mouse_button);
					}
				}
				if(ev.evtype & FBVNC_EVENT_TS_UP)
				{
					if(active_overlay)
					{
						overlay_event(&ev, active_overlay, 0);
						active_overlay = 0;
					}
					else if(!panning)
					{
						global_framebuffer.mouse_x = ev.x;
						global_framebuffer.mouse_y = ev.y;
						scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, mouse_button);
					}
				}
				if(ev.evtype & FBVNC_EVENT_KEYREPEAT)
				{
					dprintf("Keyrepeat\n");
					if(kbdRate && rep_key)
					{
						schedule_add(sched, 1000 / kbdRate, FBVNC_EVENT_KEYREPEAT);
						SendKeyEvent(rep_key, 1);
					}
				}
				if(ev.evtype & FBVNC_EVENT_ZOOM_IN)
				{
					if(global_framebuffer.v_scale > 1)
					{
						global_framebuffer.v_scale--;
						vp_pan(0, 0);
					}
				}
				if(ev.evtype & FBVNC_EVENT_ZOOM_OUT)
				{
					if(global_framebuffer.v_scale < 4)
					{
						global_framebuffer.v_scale++;
						vp_pan(0, 0);
					}
				}
				if(ev.evtype & FBVNC_EVENT_DCLICK)
				{
					global_framebuffer.mouse_x = ev.x;
					global_framebuffer.mouse_y = ev.y;
					scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, 1);
					scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, 0);
					scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, 1);
					scaledPointerEvent(global_framebuffer.mouse_x, global_framebuffer.mouse_y, 0);
				}

			}
		}
		return;
	}
}