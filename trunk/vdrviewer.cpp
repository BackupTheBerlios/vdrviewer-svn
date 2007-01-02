//#include <sys/types.h>
#include <sys/ioctl.h>
//#include <linux/if.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>

#include <linux/dvb/audio.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/video.h>
//#include <poll.h>

//#include <sys/time.h>
//#include <sys/stat.h>
//#include <sys/ioctl.h>
#include <fcntl.h>
//#include <unistd.h>
//#include <stdio.h>
//#include <stdlib.h>
#include <signal.h>
#include <errno.h>
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
//#include <dbox/fp.h>
#include <dbox/lcd-ks0713.h>
//#include <linux/fb.h>
//#include "input_fake.h"
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
#include "datasocket.h"

// LCD
typedef unsigned char screen_t[LCD_BUFFER_SIZE];
#define LCD "/dev/dbox/lcd0"
#include "font.h"
#define FILLED 4
int lcd;
cDataSocket *m_pLCDSocket    = NULL;
cDataSocket *m_pStreamSocket = NULL;
bool graph_lcd;
screen_t screen, glcd_screen;


// Controld
#include <controldclient/controldMsg.h>
#include <controldclient/controldclient.h>

//Sectionsd
#include <sectionsdclient/sectionsdclient.h>
CSectionsdClient *g_Sectionsd;

// Zapit
#include <zapit/client/zapitclient.h>
CZapitClient *g_Zapit;


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
bool lcd_info = false;
double datarate=0;
#ifdef HAVE_DREAMBOX_HARDWARE
int ms_fd;
int ms_fd_usb;
#endif
int blev;
char terminate=0;
int gScale,sx,sy,ex,ey;
int usepointer = 0;
bool bResync = false;
int lcd_port;
int ts_port;
int is_ac3=-1;
int auto_aspratio;
int use_lirc;
size_t readsize;
size_t iPlaceToWrite;
bool bDuplicateFrame = false;
ringbuffer_t *ringbuf;
CControldClient	*g_Controld;
void fbvnc_close(void);
Pixel ico_mouse[] = {
ICO_WHITE,ICO_WHITE,ICO_WHITE,ICO_WHITE,
ICO_WHITE,ICO_BLACK,ICO_BLACK,ICO_WHITE,
ICO_WHITE,ICO_BLACK,ICO_BLACK,ICO_WHITE,
ICO_WHITE,ICO_WHITE,ICO_WHITE,ICO_WHITE,
};

//=======================
//== DVB device states ==
//=======================
enum MP_DEVICE_STATE 
{
	DVB_DEVICES_UNDEF   = -1,
	DVB_DEVICES_STOPPED = 0,
	DVB_DEVICES_RUNNING = 1
};

static MP_DEVICE_STATE gDeviceState = DVB_DEVICES_UNDEF; 
bool DevicesOpened = false;

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





#define ADAP      "/dev/dvb/adapter0"
#define ADEC ADAP "/audio0"
#define VDEC ADAP "/video0"
#define DMX  ADAP "/demux0"
#define DVR  ADAP "/dvr0"


#define PF_BUF_SIZE   (348*188)
#define PF_DMX_SIZE   (PF_BUF_SIZE + PF_BUF_SIZE/2)
#define RINGBUFFERSIZE PF_BUF_SIZE*50
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
pthread_t oLCDThread;


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

enum e_playstate {eBufferRefill=0,eBufferRefillAll,ePlayInit,ePlay,ePause};


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
	int   vdec;
	int   adec;

	off_t pos;
	off_t fileSize;

	size_t   readSize;
	char  dvrBuf[PF_BUF_SIZE];
	bool  startDMX;

	bool  isStream;
	bool  canPause;

	bool        itChanged;
	int         it;
	int         lst_cnt;
	MP_LST_ITEM lst[PF_LST_ITEMS];
	
	enum e_playstate playstate;
	
} MP_CTX;


MP_CTX *ctx = NULL;


void put_pixel(screen_t screen,int x, int y,char col)
{
  switch (col&3) {
  case LCD_PIXEL_ON:
    screen[((y/8)*LCD_COLS)+x]|=1<<(y%8);
    break;
  case LCD_PIXEL_OFF:
    screen[((y/8)*LCD_COLS)+x]&=~(1<<(y%8));
    break;
  case LCD_PIXEL_INV:
    screen[((y/8)*LCD_COLS)+x]^=1<<(y%8);
    break;
  }
}

void draw_horiz_line(screen_t screen,int x, int y,int x2, char col)
{
	int count;
	
	for(count=x;count<=x2;count++)
		put_pixel(screen,count,y,col);
}

void draw_vert_line(screen_t screen,int x, int y,int y2, char col)
{
	int count;
	
	for(count=y;count<=y2;count++)
		put_pixel(screen,x,count,col);
}

void draw_rectangle(screen_t screen,int x, int y,int x2, int y2, char col)
{
	
	int x_count,y_count;

	if(col&FILLED)
	{
		for(y_count=y;y_count<=y2;y_count++)
		for(x_count=x;x_count<=x2;x_count++)
		put_pixel(screen,x_count,y_count,col);
	}
	else
	{
		draw_horiz_line(screen,x,y,x2,col);
		draw_horiz_line(screen,x,y2,x2,col);
		draw_vert_line(screen,x,y,y2,col);
		draw_vert_line(screen,x2,y,y2,col);
	}
}

void draw_progressbar(screen_t screen,int x, int y, int height, int len, int max_val, int val, char col)
{
	int screen_val;
	
	if(val>max_val) max_val=val;
	screen_val=x+((len*val)/max_val);
	draw_rectangle(screen,x,y,x+len,y+height,(!((col&3)>0))|4);
	draw_rectangle(screen,x,y,screen_val,y+height,((col&3)>0)|(col&4));
}

void render_string(screen_t screen,int x, int y, char *string)
{
	int x_count,y_count,pos;
	for(pos=0;string[pos]!=0;pos++)
		for(y_count=y;y_count<y+8;y_count++)
			for(x_count=x+(8*pos);x_count<x+(8*(pos+1));x_count++)
				put_pixel(screen,x_count,y_count,(font[(string[pos]*8)+y_count-y]>>(7-(x_count-(x+(8*pos)))))&1);
}

int draw_screen(screen_t screen,int lcd)
{
	return write(lcd,screen,LCD_BUFFER_SIZE);
}

void LCDInfo(char debug_info[] = "", bool refresh = false)
{
	static unsigned int l_rsize=0, l_wsize=0;
	static int l_ac3=0, l_screenmode=0;
	static double l_drate=1;
	static bool l_lcd = false, l_graph_lcd=false;
	double bufffill;
	
	char string[100];		
	
	if (refresh)
	{
		if (l_rsize != readsize)
		{
			draw_progressbar(screen,0,27,1,119,RINGBUFFERSIZE,readsize,LCD_PIXEL_ON | FILLED);
			l_rsize = readsize;
		}
		
		if (l_wsize != iPlaceToWrite)
		{
			draw_progressbar(screen,0,29,1,119,RINGBUFFERSIZE,iPlaceToWrite,LCD_PIXEL_ON | FILLED);
			l_wsize = iPlaceToWrite;
		}
		
		if (l_drate != datarate)
		{
			draw_progressbar(screen,0,21,4,119,10000,(int)datarate*1000,LCD_PIXEL_ON | FILLED);
			sprintf(string, "R: %2.3f Mbit/s", datarate);
			render_string(screen,0,12,string);
			l_drate = datarate;
			bufffill = (readsize * 100) / (RINGBUFFERSIZE);
			dprintf("Transferrate: %2.3f Mbit/s Bufferfüllstand: %2.1f\%\n", datarate, bufffill);
		}
	
		
		if (l_ac3 != is_ac3 || screenmode != l_screenmode)
		{
			sprintf(string, "16:9 %s DD %s", screenmode == 1 ? "on " : "off", is_ac3 == 1 ? "on " : (is_ac3 == 0 ? "off" : "?  ") );
			render_string(screen,0,33,string);
			l_ac3 = is_ac3;
			l_screenmode = screenmode;
		}
		
		if ((m_pLCDSocket != NULL) != l_lcd || graph_lcd != l_graph_lcd)
		{
			sprintf(string, "GraphLCD: %s", (m_pLCDSocket != NULL) ? (graph_lcd ? "yes " : "wait") : "no  ");
			render_string(screen,0,44,string);
			l_lcd = (m_pLCDSocket != NULL);
			l_graph_lcd = graph_lcd;
		}
	}
	
	if (debug_info != "")
	{
		sprintf(string, "Info: %s", debug_info);
		render_string(screen,0,55,string);
	}
		
	if (lcd_info == true || graph_lcd == false)
		draw_screen(screen,lcd);
}

void *updateLCD(void *sArgument)
{
	int lcd_mode;
	screen_t buf;
	
	graph_lcd = false;
	
	lcd_mode=LCD_MODE_BIN;
	if ((ioctl(lcd,LCD_IOCTL_ASC_MODE,&lcd_mode)<0) || (ioctl(lcd,LCD_IOCTL_CLEAR)<0))
	{
		dprintf("lcd ioctl - error setting LCD-mode/clearing LCD: %d\n",errno);
	}

	memset(screen,0,sizeof(screen));	
	render_string(screen,22,1, "VDR-Viewer");
	LCDInfo("Init", true);
	
	m_pLCDSocket = new cDataSocketTCP(hostname, lcd_port);
	m_pLCDSocket->Open();
	LCDInfo("Init", true); // for GraphLCD-Status
	
	while (!terminate && m_pLCDSocket != NULL && m_pLCDSocket->IsOpen())
	{
		int len = m_pLCDSocket->Get((char*)buf, sizeof(buf));
		if (len <= 0)
		{
			perror("LCD: recv");
			sleep(1);
		}
		else 
		{
			graph_lcd = true;
			memset(glcd_screen,0,sizeof(glcd_screen));	

			for (int y=0;y<64;y++)
			{
				for (int x=0;x<120/8;x++)
				{
					for (int k=0;k<8;k++)
					{
						if (*(buf + x + y * (LCD_COLS / 8)) & (1 << k))
							put_pixel(glcd_screen,x*8+k,y,LCD_PIXEL_ON);
						else
							put_pixel(glcd_screen,x*8+k,y,LCD_PIXEL_OFF);
					} 
				}
			}
			
			if (lcd_info == false)
				draw_screen(glcd_screen, lcd);
		}
	}

  	dprintf("LCD: Thread beendet\n");
  	pthread_exit(NULL);
}

static void mp_closeDVBDevices(MP_CTX *ctx)
{
	//-- may prevent black screen after restart ... --
	//------------------------------------------------
	usleep(150000);
	dprintf("Closing DVB-Devices\n");
	
	if(ctx->vdec > 0)
	{
		ioctl (ctx->vdec, VIDEO_STOP);
		close (ctx->vdec);
	}
	if(ctx->adec > 0)
	{
		ioctl (ctx->adec, AUDIO_STOP);
		close (ctx->adec);
	}
	if(ctx->dmxa > 0)
	{
		ioctl (ctx->dmxa, DMX_STOP);
		close(ctx->dmxa);
	}
	if(ctx->dmxv > 0)
	{
		ioctl (ctx->dmxv, DMX_STOP);
		close(ctx->dmxv);
	}
	if(ctx->dvr > 0)
	{
		close(ctx->dvr);
	}

	ctx->dmxa = -1;
	ctx->dmxv = -1;
	ctx->dvr  = -1;
	ctx->adec = -1;
	ctx->vdec = -1;
}

//== open/close DVB devices ==
//============================
static bool mp_openDVBDevices(MP_CTX *ctx)
{
	bool ret = true;
	
    	if (!DevicesOpened)
	{
		ctx->dmxa = -1;
		ctx->dmxv = -1;
		ctx->dvr  = -1;
		ctx->adec = -1;
		ctx->vdec = -1;
		
		dprintf("Open DVB-Devices\n");
		
    		if ((	    ctx->dmxa = open (DMX, O_RDWR | O_NONBLOCK)) < 0
			|| (ctx->dmxv = open (DMX, O_RDWR | O_NONBLOCK)) < 0
			|| (ctx->dvr =  open (DVR, O_WRONLY | O_NONBLOCK)) < 0
			|| (ctx->adec = open (ADEC,O_RDWR | O_NONBLOCK)) < 0
			|| (ctx->vdec = open (VDEC, O_RDWR | O_NONBLOCK)) < 0)
		{
			ret = false;
			dprintf("Open DVB-Devices ERROR\n");
			sleep(1);
			mp_closeDVBDevices(ctx);
		}
		else
		{
			DevicesOpened = true;
		}
	}

	return ret;
}


//== mp_stopDVBDevices ==
//=======================ku
static void mp_stopDVBDevices(MP_CTX *ctx)
{
  if (gDeviceState != DVB_DEVICES_STOPPED && DevicesOpened)
  {
  	gDeviceState = DVB_DEVICES_STOPPED;
  	
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
	ioctl (ctx->dmxa, DMX_SET_PES_FILTER, &(ctx->p));

	ctx->p.pid      = ctx->pidv;
	ctx->p.pes_type = DMX_PES_VIDEO;
	ioctl (ctx->dmxv, DMX_SET_PES_FILTER, &(ctx->p));

	usleep(150000);
  }
}

//== mp_startDVBDevices ==
//========================
static void mp_startDVBDevices(MP_CTX *ctx)
{
	if (gDeviceState != DVB_DEVICES_RUNNING && DevicesOpened)
	{
		//-- start AV devices again and ... --
		ioctl(ctx->adec, AUDIO_PLAY);             // audio
		ioctl(ctx->vdec, VIDEO_PLAY);             // video
		ioctl(ctx->adec, AUDIO_SET_AV_SYNC, 1UL); // needs sync !
			
		//-- ... demuxers also --
		ioctl (ctx->dmxa, DMX_START);	// audio first !
		ioctl (ctx->dmxv, DMX_START);

		gDeviceState = DVB_DEVICES_RUNNING;
	}
}

//== mp_freezeAV ==
//=================
static void mp_freezeAV(MP_CTX *ctx)
{
  if (gDeviceState == DVB_DEVICES_RUNNING && DevicesOpened)
  {
	//-- freeze (pause) AV playback immediate --
	ioctl(ctx->vdec, VIDEO_FREEZE);
	ioctl(ctx->adec, AUDIO_PAUSE);

	//-- workaround: switch off decoder bypass for AC3 audio --
	if(ctx->ac3 == 1)
		ioctl(ctx->adec, AUDIO_SET_BYPASS_MODE, 1UL);
  }
}

//== mp_unfreezeAV ==
//===================
static void mp_unfreezeAV(MP_CTX *ctx)
{
  if (gDeviceState == DVB_DEVICES_RUNNING && DevicesOpened)
  {

	//-- continue AV playback immediate --
	ctx->p.pid      = ctx->pida;
	ctx->p.pes_type = DMX_PES_AUDIO;

	ioctl (ctx->dmxa, DMX_SET_PES_FILTER, &(ctx->p));
	if(ctx->ac3 == 1)
		ioctl(ctx->adec, AUDIO_SET_BYPASS_MODE,0UL);	// bypass on

	ioctl (ctx->dmxa, DMX_START);

	ioctl(ctx->adec, AUDIO_PLAY);				// audio

	ioctl(ctx->vdec, VIDEO_PLAY);				// video
	ioctl(ctx->adec, AUDIO_SET_AV_SYNC, 1UL);		// needs sync !
  }
}

// checks if AR has changed an sets cropping mode accordingly (only video mode auto)
void checkAspectRatio (int vdec, bool init)
{

	static video_size_t size;
	static time_t last_check=0;

	// only necessary for auto mode, check each 2 sec. max
	if((!init && time(NULL) <= last_check+2) || auto_aspratio == 0 || vdec <= 0)
		return;

	if(init)
	{
		if(ioctl(vdec, VIDEO_GET_SIZE, &size) < 0)
			perror("VIDEO_GET_SIZE");
		last_check=0;
		return;
	}
	else
	{
		video_size_t new_size;
		if(ioctl(vdec, VIDEO_GET_SIZE, &new_size) < 0)
			perror("VIDEO_GET_SIZE");
		if(size.aspect_ratio != new_size.aspect_ratio)
		{
			video_displayformat_t vdt;
			
			if(new_size.aspect_ratio == VIDEO_FORMAT_4_3)
			{
				vdt = VIDEO_LETTER_BOX;
				screenmode = 0;
			}
			else
			{
				vdt = VIDEO_CENTER_CUT_OUT;
				screenmode = 1;
			}
			
			dprintf("AR change detected in auto mode, adjusting display format %d\n", screenmode);
			
			if(ioctl(vdec, VIDEO_SET_DISPLAY_FORMAT, vdt))
				perror("VIDEO_SET_DISPLAY_FORMAT");
			memcpy(&size, &new_size, sizeof(size));
			
			ioctl(avs, AVSIOSSCARTPIN8, &fncmodes[screenmode]);
			ioctl(saa, SAAIOSWSS, &saamodes[screenmode]);
						
		}
		last_check=time(NULL);
	}
}

int checkAC3(char* buf, int len, bool force_update)
{
	int ac3 = -1, off;
	static int last_ac3=0;
	static time_t last_check=0;
	
	if (force_update || time(NULL) > last_check+1 || last_ac3 == -1)
	{
		for (int i = 0; i < len && ac3 == -1; i++)
		{
			if (buf[i] == 0x47)
			{
				if (buf[i+1] & 0x40)
				{
					off = 0;
					if ( buf[3+i] & 0x20)//adapt field?
						off = buf[4+i] + 1;
					
				  	switch(buf[i+7+off])
				  	{
						case 0xBD: //PRIVATE_STREAM1:
							ac3=1;
						  	break;
					  	case 0xC0 ... 0xDF:
							ac3=0;
						  	break;
				  	}
				}
				i += 187;
			 }
			 if (ac3 >= 0) break;
		}
		
		last_ac3 = ac3;
		
	  	if (ac3 != ctx->ac3 && ac3 != -1)
	   	{
			ctx->ac3 = ac3;
			
			
			if (DevicesOpened)
			{
				usleep(500000);
				mp_stopDVBDevices(ctx);
				if(ctx->ac3 == 1)
					ioctl(ctx->adec, AUDIO_SET_BYPASS_MODE,0UL);	// bypass on
				else
					ioctl(ctx->adec, AUDIO_SET_BYPASS_MODE,1UL);	// bypass off
			}
						
			dprintf("AC3: %d\n", ctx->ac3);
	
		}
	
		last_check=time(NULL);
	}
	
	return ctx->ac3;
}

// Pacemaker's stuff End

void cleanup_threads()
{
	// Dadurch wird der LCD-Thread geschlossen, da er bei m_pLCDSocket->Get()
	// auf eine Antwort vom Server wartet
	if (m_pLCDSocket != NULL)
	{
		dprintf("closing LCD-Socket\n");
        	m_pLCDSocket->Shutdown(0);
        	m_pLCDSocket->Close();
        	delete m_pLCDSocket;
		m_pLCDSocket = NULL;
	}
	
	if (m_pStreamSocket != NULL)
	{
	    dprintf("closing Stream-Socket\n");
	    m_pStreamSocket->Close();
	    delete m_pStreamSocket;
	    m_pStreamSocket = NULL;
	}
	
	
        printf("Closing Threads!\n");
        pthread_join(oLCDThread, NULL);
        pthread_join(oPlayerThread, NULL); // Player first
        pthread_join(oTSReceiverThread, NULL); 
        // Clear Ringbuffer
	ringbuffer_free(ringbuf);
	dprintf("All Threads closed!\n");
	
	usleep(150000);

	mp_closeDVBDevices(ctx);
	ctx = NULL;
	usleep(500000);

        // Resume zapit
        dprintf("Waking up Zapit\n");
        
	g_Zapit = new CZapitClient;
	g_Sectionsd = new CSectionsdClient;
	g_Zapit->setStandby(false);
	g_Sectionsd->setPauseScanning(false);
	g_Sectionsd->setPauseSorting(false);
	delete g_Sectionsd;
        delete g_Zapit;
       
//        system("/bin/pzapit -lsb");
        usleep(150000);

// Pacemaker's stuff End
}


void cleanup_and_exit(char *msg, int ret)
{
	terminate=1;
	
	delete g_Controld;

	fcntl(rc_fd, F_SETFL, O_NONBLOCK);
	fcntl(lcd, F_SETFL, O_NONBLOCK);
	ioctl(lcd,LCD_IOCTL_CLEAR);
	
        cleanup_threads();
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
}


void *mp_ReceiveStream(void *sArgument)
{
	unsigned int bytes_read=0;
	struct timeval oldtime, curtime;
	int count, iPos, rest=0;
	char  dvrBuf[PF_BUF_SIZE];
	bool findSyncPos = false;
	
	
	dprintf("ReceiverThread gestartet, Server: %s\n", hostname);
	
	m_pStreamSocket = new cDataSocketUDP(hostname, ts_port);
	
	if ( !m_pStreamSocket->Open() )
	{
		dprintf("Receiver: Cannot Connect! Exiting!\n");
		terminate = 1;
	}
	else 
	{
		dprintf("Receicer: Connected\n");
	}

	gettimeofday(&oldtime, NULL);

	while( terminate == 0 )
	{
               	gettimeofday(&curtime, NULL);
		double secs = (curtime.tv_sec * 1000 + (curtime.tv_usec / 1000)) / 1000 - (oldtime.tv_sec * 1000 + (oldtime.tv_usec / 1000)) / 1000;
               	if (secs > 3)
               	{
               		datarate = bytes_read / secs * 8 / 1024 / 1024;
//			dprintf("Info: Rate %2.4f MBits/s\n", datarate);
			oldtime = curtime;
			bytes_read = 0;
		}

		if (secs > 0.3)
		    LCDInfo("", true);
		
		iPlaceToWrite = ringbuffer_write_space (ringbuf);
		
		if (((bDuplicateFrame) && (PF_BUF_SIZE * 2 > iPlaceToWrite)) || (ctx->playstate == eBufferRefill))
		{
		    bDuplicateFrame = false;
		}

		if ( PF_BUF_SIZE > iPlaceToWrite )
		{
			if (count > 50)
			{
				// This shouldn't happen
				dprintf("Receiver: Reset da keine Daten gelesen werden!\n");
				count=0;
				ringbuffer_reset(ringbuf);
				ctx->playstate = eBufferRefill;
				LCDInfo("Recv. Err ");
			}
		
			usleep(100000);
			count++;
			continue;
		}

		count = 0;
		iPos = 0;
		if (m_pStreamSocket != NULL)
		{
		    int rd = (int)m_pStreamSocket->Get(&dvrBuf[rest], 188*7*20, PF_RD_TIMEOUT);
		    if ( rd > 0)
		    {
			int datalen = rd + rest;
			//dprintf("NewVal: %X %X\n", dvrBuf[rest], dvrBuf[rest+1]);
			if (ctx->playstate == eBufferRefill)
			{
			    int iBytes = ringbuffer_read_space (ringbuf);
			    if (iBytes == 0)
				findSyncPos = true;
			}
			
			rest = 0;
			while (iPos < datalen)
			{    
			    if ((iPos + 188 < datalen) && (dvrBuf[iPos]==0x47) && (dvrBuf[iPos + 188]==0x47) && (!findSyncPos))
			    {
				ringbuffer_write(ringbuf, &dvrBuf[iPos], 188);
				iPos += 188;
			    }
			    else if ((iPos + 188 >= datalen) && (dvrBuf[iPos]==0x47) && ((dvrBuf[iPos+1]==0x40) || (dvrBuf[iPos+1]==0x00)) && (!findSyncPos))
			    {
				rest = datalen - iPos;
				if (iPos > 0)
				{
				    memcpy(&dvrBuf[0], &dvrBuf[iPos], rest);
				    //dprintf("Rest: %d\n", rest);
				}
				iPos = datalen;
			    }
			    else
			    {
				int iOldPos = iPos;
				dprintf("iPos: %d %X %X %X\n", iPos, dvrBuf[iPos], dvrBuf[iPos+1], dvrBuf[iPos+188]);
				findSyncPos = true;
				while ((iPos + 188 < datalen) && !((dvrBuf[iPos]==0x47) && (dvrBuf[iPos+1]==0x40) && (dvrBuf[iPos+188]==0x47)))
				{
				    iPos++; 
				}
				
				if ((iPos + 188 < datalen) && (dvrBuf[iPos]==0x47) && (dvrBuf[iPos+1]==0x40) && (dvrBuf[iPos+188]==0x47))
				{
				    findSyncPos = false;
				    dprintf("Datalen: %d iPos: %d\n", datalen, iPos);
				}
				//for (int i=iOldPos; i<iOldPos+200;i++)
				//    dprintf("%d:%X, ", i, dvrBuf[i]);
			    }
			}
			
			if (ctx->playstate == ePause)
			    ctx->playstate = ePlayInit;
			
			bytes_read += rd;
		    }
		    else
		    {
			ctx->playstate = eBufferRefill;
			//ctx->playstate = ePause; // Pause-Modus: für zukünftige Erweiterungen
			ringbuffer_reset(ringbuf);
			dprintf("Receiver: Reset Buffer!!!\n");
			LCDInfo("Reset     ");
		    }
		    
		}
	}

	if (m_pStreamSocket != NULL)
	{
	    m_pStreamSocket->Close();
	    delete m_pStreamSocket;
	    m_pStreamSocket = NULL;
	}
	
	dprintf("Receiver: Thread beendet\n");
	pthread_exit(NULL);
	
}



void *mp_playStream(void *sArgument)
{
	size_t rd;	
	struct pollfd pHandle;
	
	
	//-- install poller --
	pHandle.fd     = ctx->inFd;
	pHandle.events = POLLIN | POLLPRI;
	ctx->pH        = &pHandle;

	ctx->playstate = eBufferRefill;
	
	bool dvbreset = false, freezed = false;
	static time_t play_begun=0;

	dprintf("Starte Player-Loop\n");
	
	while( terminate == 0 )
	{
		if (ctx->playstate == ePause)
		{
			
			if (!freezed)
			{
				mp_freezeAV(ctx);
				freezed = true;
				dprintf("Player: Freezing\n");
			}

			play_begun = time(NULL);
			LCDInfo("Pause    ");
			usleep(200000);
			continue;
		}
		readsize = ringbuffer_read_space(ringbuf);
		if ( ctx->playstate == eBufferRefill || ctx->playstate == eBufferRefillAll) 
		{
			unsigned int buffer;
			usleep(500000);
			
			bDuplicateFrame = false;

			mp_stopDVBDevices(ctx);
			if (ctx->playstate == eBufferRefillAll)
				buffer = (int)(RINGBUFFERSIZE * 0.95);
			else
				buffer = (int)(RINGBUFFERSIZE / 4);

			if (readsize < buffer)
			{
				if (play_begun != time(NULL))
				{
					play_begun = time(NULL);
					if (ctx->playstate == eBufferRefillAll)
					{
						dprintf("Player: Buffering ALL\n");
						LCDInfo("Buf. All  ");
					}	
					else
					{
						dprintf("Player: Buffering readsite:%d buffer:%d to_read:%d\n", readsize, buffer, ctx->readSize);
						LCDInfo("Buffer    ");
					}

				}
				continue;
			}
			
			dprintf("Player: Buffering done\n");
			
			play_begun = time(NULL);
			
			if (dvbreset)
			{
				play_begun -= 5; // damit beim nächsten mal kein dvb-reset ausgelöst wird!
				dvbreset = false;
			}
			
			ctx->playstate = ePlayInit;
			
			continue;
		} 
		else
		{/*
		    if ((readsize < (int)(RINGBUFFERSIZE / 2)) && (!bDuplicateFrame) && (ctx->playstate == ePlay))
		    {
			bDuplicateFrame = true;
			dprintf("start duplicate frames\n");
			mp_freezeAV(ctx);
		    }
		    
		    if ((readsize > (int)(RINGBUFFERSIZE / 4 * 3)) && (bDuplicateFrame) && (ctx->playstate == ePlay))
		    {
			bDuplicateFrame = false;
			dprintf("stop duplicate frames\n");
			mp_unfreezeAV(ctx);
		    }*/
		    /*
		    if (readsize < (int)(RINGBUFFERSIZE / 2) && (ctx->playstate == ePlay))
		    {
			dprintf("duplicate frames\n");
			ioctl (ctx->adec, AUDIO_PAUSE);
			usleep(200000);
			ioctl (ctx->adec, AUDIO_PLAY);
		    }*/
		}

		rd = ringbuffer_read(ringbuf, ctx->dvrBuf, ctx->readSize);
		//dprintf("Puffergroesse: %d, Gelesene Bytes: %d\n",rSize, rd);
		
		// Falls der Datenstrom abgebrochen ist -> Neuinitialisierung versuchen. 
		// Problem: ffnetdev sendet keine Bytes, falls der VDR auf Pause steht. Dann greift der Timeout, 
		// in der funktion mp_tcpRead, was im original movieplayer zum abbruch der Verbindung führt.
		// Dieses Verhalten können wir hier aber nicht brauchen.
		//
		// wenn nicht bereits durch Reciever ein Refill initiiert wurde, dann den
		// Buffer komplett füllen um einen erneuten Buffer-underrun zu vermeiden
		if(ctx->playstate == ePlay && terminate == 0 && (rd != ctx->readSize || readsize < ctx->readSize || bResync) )
		{
			dprintf("Datenstrom abgebrochen oder ReSync -> Neuinit!\n");
			dprintf("play: %lus buffer:%d read:%d to_read:%d \n", time(NULL) - play_begun, readsize, rd, ctx->readSize);

			if (bResync)
			{
				ctx->playstate = eBufferRefill;
				LCDInfo("Resync    ");
			}
			else if ( (time(NULL)-play_begun) > 4) // über 4 Sekunden -> kein DVB-Reset
			{
				ctx->playstate = eBufferRefillAll;
				dvbreset = false;
			}
			else
			{
				// Manchmal kommt es vor, dass beim Start der Wiedergabe die dbox
				// nicht richtig will. Oft werden dann die Daten nicht richtig gelesen
				// und es würde ein Komplettbuffering ausgelöst werden
				dprintf("Player: Evtl hängt nur der Demux. Versuche NeuInit!\n");
				
				// wenn es das 2. mal nicht klappt dann den Buffer wieder füllen
				if (dvbreset == true)
				{
					ctx->playstate = eBufferRefill;
					LCDInfo("DVB Init2 ");
				}
				else
				{
					ctx->playstate = ePlayInit;
					LCDInfo("DVB Init  ");
				}

				usleep(600000);
				mp_stopDVBDevices(ctx);

				dvbreset = true;
			}

			bResync = false;
			
			continue;
		}

		if (!mp_openDVBDevices(ctx))
		{
			LCDInfo("DVB ERR   ");
			dprintf("Player: DVB Error\n");
			sleep(5);
			continue;
		}

		if (ctx->playstate == ePlayInit)
		{
			ctx->playstate = ePlay;
			LCDInfo("Playing   ");
			dprintf("Player: Playing\n");
			is_ac3 = checkAC3(ctx->dvrBuf, rd, true);
		}
		else
			is_ac3 = checkAC3(ctx->dvrBuf, rd, false);

		checkAspectRatio(ctx->vdec, false);

	    	//-- write stream data now --
		int done = 0;
		int wr;
		while (rd > 0)
		{
			wr = write(ctx->dvr, &ctx->dvrBuf[done], rd);
			
			//dprintf("Player: Play to_wr: %d  wr: %d\n", rd, wr);
			
			if (wr < 0)
			{
				perror("Player: Stream write error");
				dprintf("Player: Stream write error\n");
				usleep(1000);
				continue;
			}
			rd   -= wr;
			done += wr;
		}

	    	mp_startDVBDevices(ctx);

		if (freezed)
		{
			mp_unfreezeAV(ctx);
			freezed = false;
		}
	}

	mp_stopDVBDevices(ctx);
	sleep(2);

  	dprintf("Player-Thread beendet\n");

	pthread_exit(NULL);
}

void Resync()
{
    bResync = True;
}

void ToggleLCD()
{
	lcd_info = lcd_info ? false : true;
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


void get_fbinfo() {
	struct fb_fix_screeninfo finf;
	struct fb_var_screeninfo vinf;
#define FFB fb_fd
	if(ioctl(FFB, FBIOGET_FSCREENINFO, &finf))
		cleanup_and_exit("fscreeninfo", EXIT_SYSERROR);

	if(ioctl(FFB, FBIOGET_VSCREENINFO, &vinf))
		cleanup_and_exit("vscreeninfo", EXIT_SYSERROR);
	vinf.bits_per_pixel = 8*sizeof(Pixel);;
	if(ioctl(FFB,FBIOPUT_VSCREENINFO, &vinf) == -1 )
	{
		cleanup_and_exit("Put variable screen settings failed", EXIT_ERROR);
	}

	if(vinf.bits_per_pixel != 8*sizeof(Pixel))
	{
		printf("bpp %d 8*sizeof(Pixel)=%d\n",vinf.bits_per_pixel, 8*sizeof(Pixel));
		cleanup_and_exit("data type 'Pixel' size mismatch", EXIT_ERROR);
	}
	myFormat.bitsPerPixel = vinf.bits_per_pixel;
	myFormat.depth = vinf.bits_per_pixel;
	myFormat.trueColour = 0;
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
	if( (global_framebuffer.p_buf=(Pixel*)mmap((void *)0,finf.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, offset)) == (void *)-1 )
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
#if HAVE_DVB_API_VERSION == 3
	struct input_event ev;
        read(rc_fd, &ev, sizeof(ev));
        fcntl(rc_fd, F_SETFL, fcntl(rc_fd, F_GETFL) &~ O_NONBLOCK);
#else
	int rccode;
        read(rc_fd, &rccode, sizeof(rccode));
        fcntl(rc_fd, F_SETFL, O_NONBLOCK);
#endif
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


void fbvnc_init() {
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
	
	//ShowOsd(False);
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

	//ioctl(avs, AVSIOSSCARTPIN8, &fncmodes[screenmode]);
	//ioctl(saa, SAAIOSWSS, &saamodes[screenmode]);
}

void fbvnc_close() {

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

int cmp_sc(struct sched *A, struct sched *B)
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

void schedule_add(List *s, int ms_delta, int event)
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

void schedule_delete(List *s, int event)
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

int schedule_get_next(List *s, struct timeval *tv)
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

void fd_copy(fd_set *out, fd_set *in, int n)
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

enum fbvnc_event fbvnc_get_event (fbvnc_event_t *ev, List *sched)
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
			dprintf("Input event: time: %d.%d type: %d code: %d value: %d\n",(int)iev.time.tv_sec,(int)iev.time.tv_usec,iev.type,iev.code,iev.value);

			static int keypress_count = 0;
			
			bool handled = cMenu::HandleMenu(rc_fd, iev, ev);

			dprintf("handled: %d evtype: %d\n", handled, ev->evtype);
			if (!handled)
			{
			   	if (ev->evtype == FBVNC_EVENT_NULL)
			    	{
	    				if (iev.code == KEY_VOLUMEUP || iev.code == KEY_VOLUMEDOWN )
					{
						if (iev.value != 0)
						{
							char volume = g_Controld->getVolume(CControld::TYPE_AVS);
							if (iev.code == KEY_VOLUMEUP)
							{
								volume = volume > 95 ? 100 : volume + 5;
								if (use_lirc)
									g_Controld->setVolume(60, CControld::TYPE_LIRC);
							}
							else 
							{
								volume = volume < 5 ? 0 : volume - 5;
								if (use_lirc)
									g_Controld->setVolume(40, CControld::TYPE_LIRC);
							}
							
							if (!use_lirc)
								g_Controld->setVolume(volume, CControld::TYPE_AVS);
						}
						
					}
					else if (iev.code == KEY_MUTE)
					{
						if (iev.value == 1)
						{
							bool mute = g_Controld->getMute(CControld::TYPE_AVS);
							if (use_lirc)
								g_Controld->setMute(!mute, CControld::TYPE_LIRC);
							else
								g_Controld->setMute(!mute, CControld::TYPE_AVS);
							
						}
					}
					else if (iev.value != 2 || keypress_count > 1)
					{
						if (iev.value > 0)
						{
				    		    SendKeyEvent(iev.code, 1);
				    		    dprintf("Sende Code: %d Value: %d\n", iev.code, 1);
						}
					    	
					    	if (iev.value != 2)
	  				    		keypress_count = 0;
					}
					else if (iev.value == 2)
					{
				    		dprintf("Code-Senden unterdrückt! %d\n", keypress_count);
					    	keypress_count++;
					}

					RetEvent(FBVNC_EVENT_NULL);
			    	}
			    	else
			    	{
					RetEvent((fbvnc_event)ev->evtype);
			    	}
			}

        
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
               //if(iev.value==1)
                  RetEvent(FBVNC_EVENT_NULL); // ignore key pressed event
               //retval = FBVNC_EVENT_QUIT;
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
				//screenmode = 1-screenmode;
				//ioctl(avs, AVSIOSSCARTPIN8, &fncmodes[screenmode]);
				//ioctl(saa, SAAIOSWSS, &saamodes[screenmode]);
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

void scaledPointerEvent(int xp, int yp, int button) {
	IMPORT_FRAMEBUFFER_VARS

	SendPointerEvent(xp*v_scale + v_x0, yp*v_scale + v_y0, button);
}

bool img_saved = 0;
List *sched;

#define FDNUM_VNC 0
#define FDNUM_PNM 1

void save_viewport(struct viewport *dst) {
	IMPORT_FRAMEBUFFER_VARS

	dst->v_xsize = v_xsize;
	dst->v_ysize = v_ysize;
	dst->v_buf = v_buf;
	dst->v_scale = v_scale;
}

void activate_viewport(struct viewport *src) {
	global_framebuffer.v_xsize = src->v_xsize;
	global_framebuffer.v_ysize = src->v_ysize;
	global_framebuffer.v_buf = src->v_buf;
	global_framebuffer.v_scale = src->v_scale;
}

static struct viewport v_img;

void open_pnm_fifo(int *fdp)
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

void load_pnm_image(int *fdp)
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

void show_pnm_image() {
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
		printf("0x%X : Freeed\n", (unsigned int)v_img.v_buf);
		free(v_img.v_buf);
	}

	fn_action = 0;
	activate_viewport(&v_save);
	vp_pan(0, 0);

}


extern "C" {
	void plugin_exec(PluginParam *par)
	{
		fbvnc_overlay_t *active_overlay = 0;
		int panning = 0;
		int readfd[2];	/* pnmfd and/or rfbsock */
		static int pnmfd = -1;


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
		char szVNCPort[20];
		char szLCDPort[20];
		char szTSPort[20];
		char szScale[20];
		char szServerScale[20];
		char szPasswd[20];
		char szMouse[20];
		char szAutAspRatio[20];
		char szUseLirc[20];
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
		sprintf(szScale       ,"scale%s",szServerNr);
		sprintf(szServerScale ,"server_scale%s",szServerNr);
		sprintf(szPasswd      ,"passwd%s",szServerNr);
		sprintf(szMouse       ,"mouse%s",szServerNr);
		
		sprintf(szVNCPort     ,"osdport%s",szServerNr);
		sprintf(szTSPort      ,"streamingport%s",szServerNr);
		sprintf(szLCDPort     ,"lcdport%s",szServerNr);
		sprintf(szAutAspRatio ,"autoaspectratio%s",szServerNr);
		sprintf(szUseLirc     ,"lirc%s",szServerNr);
		
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
		int vnc_port=config.getInt32(szVNCPort,20001);
		ts_port=config.getInt32(szTSPort,20002);
		lcd_port=config.getInt32(szLCDPort,20003);
		use_lirc=config.getInt32(szUseLirc,0);
		gScale=config.getInt32(szScale,1);
		auto_aspratio=config.getInt32(szAutAspRatio,0);
		serverScaleFactor = config.getInt32(szServerScale,1);
		rcCycleDuration = config.getInt32("rc_cycle_duration",225)*1000;
		rcTest = config.getInt32("rc_test",0);
		strcpy(passwdString,config.getString(szPasswd,"").substr(0,8).c_str());
		debug = config.getInt32("debug",1);
		//debug = 1;
		screenmode = config.getInt32("screenmode",0);
#endif

		dprintf("Server %s osdport: %d tsport %d lcdport %d\n", hostname, vnc_port, ts_port, lcd_port);
		
		if(gScale > 4 || gScale < 1)
			gScale=1;
		global_framebuffer.v_scale = gScale;
		
		// Shutdown Zapit
		dprintf("Send Standby command to zapit\n");
		
		g_Zapit = new CZapitClient;
		g_Sectionsd = new CSectionsdClient;
		g_Zapit->setStandby(true);
		g_Sectionsd->setPauseScanning(true);
		g_Sectionsd->setPauseSorting(true);
		delete g_Sectionsd;
        	delete g_Zapit;
		sleep(1);
		
//		system("/bin/pzapit -esb");
//		usleep(500000);

		// ctx-Init hier bevor die Threads starten
		MP_CTX mpCtx;
		ctx = &mpCtx;
		
		ctx->pos = 0L;
		ctx->canPause = false;
		
		ctx->readSize = PF_BUF_SIZE / 2;
		
		ctx->pidv     = 99;
		ctx->pida     = 100;
		ctx->ac3      = -1;
		const char *sArgument = "bla";

		ringbuf = ringbuffer_create(RINGBUFFERSIZE);

		if(pthread_create(&oLCDThread, 0, updateLCD, (void *)sArgument) != 0)
		{
			dprintf("Couldn't create LCD thread\n");
		}
		else
		{
			dprintf("Successfully created LCD thread\n");
		}
		
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

		g_Controld      = new CControldClient;
		g_Controld->registerEvent(CControldClient::EVT_MUTECHANGED, 222, CONTROLD_UDS_NAME);
		g_Controld->registerEvent(CControldClient::EVT_VOLUMECHANGED, 222, CONTROLD_UDS_NAME);
  
// End TEST of pacemaker
		
		if(strlen(passwdString) == 0)
		{
			passwdFile = CONFIGDIR "/vncpasswd";
		}
		sched = list_new();

		requestedDepth = 16;
		forceTruecolour = True;
		kbdRate=4;
#if 0
		processArgs(argc, argv);
#endif
		signal(SIGPIPE, SIG_IGN);

		dprintf("ConnectToRFBServer()\n");
		if(!ConnectToRFBServer(hostname, vnc_port))
		{
			MessageBox("Cannot connect", rc_fd);

			restore_screen();
			//list_destroy(sched);
			if( munmap ( (void *)global_framebuffer.p_buf, global_framebuffer.smem_len ) == -1 )
			{
				printf("FBclose: munmap failed");
			}
			cleanup_and_exit("VNC: Cannot connect", 0);
			//cleanup_threads();
			cleanupFT();
			return;
		}

		dprintf("InitialiseRFBConnection()\n");
		if(!InitialiseRFBConnection(rfbsock))
		{
			MessageBox("Cannot initialize", rc_fd);
			dprintf("Cannot initialize\n");
			restore_screen();
			//list_destroy(sched);
			if( munmap ( (void *)global_framebuffer.p_buf, global_framebuffer.smem_len ) == -1 )
			{
				printf("FBclose: munmap failed");
			}
			cleanup_and_exit("VNC: Cannot initialize", 0);
			//cleanup_threads();
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


