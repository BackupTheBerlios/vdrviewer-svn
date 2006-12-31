/******************************************************************************
 *                        <<< VDRViewer Plugin >>>
 *-----------------------------------------------------------------------------*/
 

#include <unistd.h>
#include <configfile.h>

extern "C" {
#include "vdrviewer.h"
#include "fbgl.h"
#include "overlay.h"
}

#include "viewermenu.h"


#define RetEvent(e) do { ev->evtype = (e); return (e); } while(0)


#define VDRVIEWER_SETTINGS_FILE CONFIGDIR "/vdr.conf"

#define ITEM_HOTKEY_LEFT 10
#define ITEM_TEXT_LEFT 30
#define ITEM_TEXT_RIGHT 10 
#define ITEM_TEXT_TOP 2
#define MENU_TOP_MARGIN 50
#define MENU_BOTTOM_MARGIN 50
#define MENU_HEADER_HEIGHT 45
#define MENU_MIN_WIDTH 200 
#define ITEM_HEIGHT 32

#define REPEAT_TIMER 3


#define RC_0                    '0'
#define RC_1                    '1'
#define RC_2                    '2'
#define RC_3                    '3'
#define RC_4                    '4'
#define RC_5                    '5'
#define RC_6                    '6'
#define RC_7                    '7'
#define RC_8                    '8'
#define RC_9                    '9'

#define RC_RIGHT        0x0191
#define RC_LEFT         0x0192
#define RC_UP                   0x0193
#define RC_DOWN         0x0194
#define RC_PLUS         0x0195
#define RC_MINUS        0x0196

#define RC_OK                           0x0D
#define RC_STANDBY      0x1C
#define RC_ESC                  RC_HOME

#define RC_HOME                 0x01B1
#define RC_MUTE                 0x01B2
#define RC_HELP                 0x01B3
#define RC_DBOX                 0x01B4

#define RC_GREEN        0x01A1
#define RC_YELLOW       0x01A2
#define RC_RED          0x01A3
#define RC_BLUE         0x01A4

#define RC_PAUSE        RC_HELP
#define RC_ALTGR        0x12
#define RC_BS                   0x08
#define RC_POS1         RC_HOME
#define RC_END          0x13
#define RC_INS          0x10
#define RC_ENTF         0x11
#define RC_STRG         0x00
#define RC_LSHIFT       0x0E
#define RC_RSHIFT       0x0E
#define RC_ALT          0x0F
#define RC_NUM          RC_DBOX
#define RC_ROLLEN       0x00
#define RC_F5                   0x01C5
#define RC_F6                   0x01C6
#define RC_F7                   0x01C7
#define RC_F8                   0x01C8
#define RC_F9                   0x01C9
#define RC_F10          0x01CA
#define RC_RET          0x0D
#define RC_RET1         0x01CC
#define RC_CAPSLOCK     0x01CD
#define RC_ON                   0x01CE

#define RC_F1           RC_RED
#define RC_F2           RC_GREEN
#define RC_F3           RC_YELLOW
#define RC_F4           RC_BLUE
#define RC_PAGEUP       RC_PLUS
#define RC_PAGEDOWN     RC_MINUS


int rctable[] =
{
    0x00, RC_ESC, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'ß', '´', RC_BS, 0x09,
    'q',  'w', 'e', 'r', 't', 'z', 'u', 'i', 'o', 'p', 'ü', '+', RC_RET, RC_STRG, 'a', 's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', 'ö', 'ä', '^', RC_LSHIFT, '#', 'y', 'x', 'c', 'v',
    'b',  'n', 'm', ',', '.', '-', RC_RSHIFT, 0x00, RC_ALT, 0x20, RC_CAPSLOCK,RC_F1,RC_F2,RC_F3,RC_F4,RC_F5,
    RC_F6,RC_F7,RC_F8,RC_F9,RC_F10,RC_NUM,RC_ROLLEN,0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, RC_STANDBY, 0x00, 0x00, 0x00, 0x00, '<', RC_OK, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, RC_ALTGR, 0x00, RC_POS1, RC_UP, RC_PAGEUP, RC_LEFT, RC_RIGHT, RC_END, RC_DOWN,RC_PAGEDOWN,RC_INS,RC_ENTF,
    0x00, RC_MUTE, RC_MINUS, RC_PLUS, RC_STANDBY, 0x00, 0x00, RC_PAUSE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

int rcshifttable[] =
{
    0x00, RC_ESC, '!', '"', '§', '$', '%', '&', '/', '(', ')', '=', '?', '`', 0x08, 0x09,
    'Q',  'W', 'E', 'R', 'T', 'Z', 'U', 'I', 'O', 'P', 'Ü', '*', RC_RET1, RC_STRG, 'A', 'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', 'Ö', 'Ä', '°', RC_LSHIFT, 0x27, 'Y', 'X', 'C', 'V',
    'B',  'N', 'M', ';', ':', '_', RC_RSHIFT, 0x00, RC_ALT, 0x20, RC_CAPSLOCK,RC_F1,RC_F2,RC_F3,RC_F4,RC_F5,
    RC_F6,RC_F7,RC_F8,RC_F9,RC_F10,RC_NUM,RC_ROLLEN,0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, RC_STANDBY, 0x00, 0x00, 0x00, 0x00, '>'
};
		  			

int rcaltgrtable[] =
{
    0x00, RC_ESC, 0x00, '²', '³', 0x00, 0x00, 0x00, '{', '[', ']', '}', '\\', 0x00, 0x00, 0x00,
    '@',  0x00, '.', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '~', RC_RET1, RC_STRG, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, RC_LSHIFT, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,  0x00, 'µ', 0x00, 0x00, 0x00, RC_RSHIFT, 0x00, RC_ALT, 0x20, RC_CAPSLOCK,RC_F1,RC_F2,RC_F3,RC_F4,RC_F5,
    RC_F6,RC_F7,RC_F8,RC_F9,RC_F10,RC_NUM,RC_ROLLEN,0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x00, RC_STANDBY, 0x00, 0x00, 0x00, 0x00, '|'
};


//                     TRANSP   WHITE    GREEN    YELLOW   RED      BLUE     ORANGE   SKIN1    SKIN2   SKIN3
unsigned short rd[] = {0x00<<8, 0xF2<<8, 0x00<<8, 0xFF<<8, 0xFF<<8, 0x00<<8, 0xE8<<8, 0x00<<8, 0x00<<8, 0x38<<8};
unsigned short gn[] = {0x00<<8, 0xF2<<8, 0xFF<<8, 0xFF<<8, 0x00<<8, 0x00<<8, 0xA9<<8, 0x18<<8, 0x20<<8, 0x88<<8};
unsigned short bl[] = {0x00<<8, 0xF2<<8, 0x00<<8, 0x00<<8, 0x00<<8, 0xFF<<8, 0x00<<8, 0x38<<8, 0x58<<8, 0xF8<<8};
unsigned short tr[] = {0xFF00,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000};
struct fb_cmap colormap = {0, 10, rd, gn, bl, tr};


//************************************************************************************************
// 	cMenuItem::cMenuItem()
//************************************************************************************************
cMenuItem::cMenuItem(const char *pText)
{
    m_pText = pText;
    m_ItemNr  = -1;
    m_Selected = 0;
    m_ItemWidth  = ITEM_TEXT_LEFT + GetStringLen(m_pText) + ITEM_TEXT_RIGHT;
    m_ItemHeight = ITEM_HEIGHT;
    m_Selectable = true;
    m_RetValue   = eMVNone;
}

//************************************************************************************************
// 	cMenuItem::~cMenuItem()
//************************************************************************************************
cMenuItem::~cMenuItem()
{
}

//************************************************************************************************
// 	cMenuItem::SetItemNr()
//************************************************************************************************
void cMenuItem::SetItemNr(int Nr)
{
    m_ItemNr = Nr;
}


//************************************************************************************************
// 	cMenuItem::SetSelected()
//************************************************************************************************
void cMenuItem::SetSelected(bool Selected)
{
    if (Selected != m_Selected)
    {
	m_Selected = Selected;
	Draw();
    }
}


//************************************************************************************************
// 	cMenuItem::GetItemWidth()
//************************************************************************************************
int cMenuItem::GetItemWidth()
{
    return m_ItemWidth;
}


//************************************************************************************************
// 	cMenuItem::GetItemHeight()
//************************************************************************************************
int cMenuItem::GetItemHeight()
{
    return m_ItemHeight;
}


//************************************************************************************************
// 	cMenuItem::Draw()
//************************************************************************************************
void cMenuItem::Draw(int ItemLeft , int ItemTop, int ItemWidth)
{
    if (ItemLeft > -1)
	m_ItemLeft = ItemLeft;
    if (ItemTop > -1)
	m_ItemTop = ItemTop;
    if (ItemWidth > -1)
	m_ItemWidth = ItemWidth;
	
    if (m_Selected)
    {
	RenderBox(m_ItemLeft, 
		  m_ItemTop, 
		  m_ItemLeft + m_ItemWidth, 
		  m_ItemTop  + ITEM_HEIGHT, 
		  FILL, 
		  SKIN3);
    
	RenderString(m_pText, 
		     m_ItemLeft + ITEM_TEXT_LEFT, 
		     m_ItemTop  + ITEM_TEXT_TOP, 
		     m_ItemLeft + m_ItemWidth - ITEM_TEXT_RIGHT, 
		     LEFT, 
		     WHITE, 
		     SKIN3);
    }
    else
    {
	if (ItemLeft == -1)
	{
	    RenderBox(m_ItemLeft, 
		      m_ItemTop, 
		      m_ItemLeft + m_ItemWidth, 
		      m_ItemTop  + ITEM_HEIGHT, 
		      FILL, 
		      SKIN2);
	}	  
	
	RenderString(m_pText, 
	             m_ItemLeft + ITEM_TEXT_LEFT, 
		     m_ItemTop  + ITEM_TEXT_TOP, 
		     m_ItemLeft + m_ItemWidth - ITEM_TEXT_RIGHT, 
		     LEFT, 
		     WHITE, 
		     SKIN2);
    }
}

//************************************************************************************************
// 	cMenuItem::ProcessKey()
//************************************************************************************************
bool cMenuItem::ProcessKey(int Key)
{
    return false; 
}


//************************************************************************************************
// 	cMenuIntItem()
//************************************************************************************************
cMenuIntItem::cMenuIntItem(const char *Name, int *Value, int Min, int Max): cMenuItem(Name)
{
    m_Value = Value;
    m_Min   = Min;
    m_Max   = Max;
}


//************************************************************************************************
// 	~cMenuIntItem()
//************************************************************************************************
cMenuIntItem::~cMenuIntItem()
{    
}

//************************************************************************************************
// 	cMenuIntItem::ProcessKey()
//************************************************************************************************
bool cMenuIntItem::ProcessKey(int Key)
{
    return false; 
}


//************************************************************************************************
// 	cMenuSelItem::cMenuSelItem()
//************************************************************************************************
cMenuSelItem::cMenuSelItem(const char *Name, EMenuValue MenuValue): cMenuItem(Name)
{
    m_MenuValue = MenuValue;
}


//************************************************************************************************
// 	cMenuSelItem::~cMenuSelItem()
//************************************************************************************************
cMenuSelItem::~cMenuSelItem()
{
}


//************************************************************************************************
// 	cMenuSelItem::Draw()
//************************************************************************************************
void cMenuSelItem::Draw(int ItemLeft , int ItemTop, int ItemWidth)
{
    cMenuItem::Draw(ItemLeft , ItemTop, ItemWidth);
}

//************************************************************************************************
// 	cMenuSelItem::ProcessKey()
//************************************************************************************************
bool cMenuSelItem::ProcessKey(int Key)
{
    if (Key == RC_OK)
    {
	m_RetValue = m_MenuValue;
	return true;
    }
    
    return false; 
}


//************************************************************************************************
// 	cMenuSeparatorItem::cMenuSeparatorItem()
//************************************************************************************************
cMenuSeparatorItem::cMenuSeparatorItem(): cMenuItem("")
{
    m_ItemHeight = ITEM_HEIGHT / 2;
    m_Selectable = false;
}


//************************************************************************************************
// 	cMenuSeparatorItem::~cMenuSeparatorItem()
//************************************************************************************************
cMenuSeparatorItem::~cMenuSeparatorItem()
{
}


//************************************************************************************************
// 	cMenuSeparatorItem::Draw()
//************************************************************************************************
void cMenuSeparatorItem::Draw(int ItemLeft , int ItemTop, int ItemWidth)
{
    if (ItemLeft > -1)
	m_ItemLeft = ItemLeft;
    if (ItemTop > -1)
	m_ItemTop = ItemTop;
    if (ItemWidth > -1)
	m_ItemWidth = ItemWidth;
	
    RenderBox(m_ItemLeft + 3, 
	      m_ItemTop + m_ItemHeight / 2 - 1, 
	      m_ItemLeft + m_ItemWidth - 3, 
	      m_ItemTop + m_ItemHeight / 2 + 1, 
	      FILL, 
	      SKIN3);
}


//************************************************************************************************
// 	cMenu()
//************************************************************************************************
cMenu::cMenu(const char *pName)
{
    m_pName = pName;
    m_ItemWidth = ITEM_TEXT_LEFT + GetStringLen(m_pName) + ITEM_TEXT_RIGHT;
    if (m_ItemWidth < MENU_MIN_WIDTH)
	m_ItemWidth = MENU_MIN_WIDTH;
	
    m_RetValue = eMVNone;
}

//************************************************************************************************
// 	~cMenu()
//************************************************************************************************
cMenu::~cMenu()
{
    TItemList::iterator it;
    while (m_ItemList.size() > 0)
    {
	it = m_ItemList.begin();
	delete (*it);
	m_ItemList.erase(it);
    }
}


/******************************************************************************
 * GetRCCode
 ******************************************************************************/

#ifndef HAVE_DREAMBOX_HARDWARE

int cMenu::GetRCCode(int rc_fd, int &rccode)
{
    static int count = 0;
    //get code
    struct input_event ev;
    static __u16 rc_last_key = KEY_RESERVED;
    static __u16 rc_last_code = KEY_RESERVED;
    if(read(rc_fd, &ev, sizeof(ev)) == sizeof(ev))
    {
	if(ev.value)
	{
	    if(ev.code == rc_last_key)
	    {
		if (count < REPEAT_TIMER)
		{
	    	    count++;
		    rccode = -1;
		    return 1;
		}
	    }
	    else
		count = 0;
	    rc_last_key = ev.code;
	    switch(ev.code)
    	    {
	    case KEY_UP:		rccode = RC_UP;			break;
	    case KEY_DOWN:		rccode = RC_DOWN;		break;
	    case KEY_LEFT:		rccode = RC_LEFT;		break;
	    case KEY_RIGHT:		rccode = RC_RIGHT;		break;
	    case KEY_OK:		rccode = RC_OK;			break;
	    case KEY_RED:		rccode = RC_RED;		break;
	    case KEY_GREEN:		rccode = RC_GREEN;		break;
	    case KEY_YELLOW:		rccode = RC_YELLOW;		break;
	    case KEY_BLUE:		rccode = RC_BLUE;		break;
	    case KEY_HELP:		rccode = RC_HELP;		break;
	    case KEY_SETUP:		rccode = RC_DBOX;		break;
	    case KEY_HOME:		rccode = RC_HOME;		break;
	    case KEY_POWER:		rccode = RC_STANDBY;		break;
	    default:
		if( ev.code > 0x7F )
		{
		    rccode = 0;
		    if( ev.code == 0x110 )
		    {
	    		rccode = RC_ON;
		    }
		}
		else
		{
		    rccode = rctable[ev.code & 0x7F];
		}
		if( rc_last_code == RC_LSHIFT )
		{
		    if( ev.code <= 0x56 )  //(sizeof(rcshifttable)/sizeof(int)-1)
		    {
	    	        rccode = rcshifttable[ev.code];
		    }
		}
		else if( rc_last_code == RC_ALTGR )
		{
		    if( ev.code <= 0x56 )  //(sizeof(rcaltgrtable)/sizeof(int)-1)
		    {
			rccode = rcaltgrtable[ev.code];
		    }
		}
		else if( rc_last_code == RC_ALT )
		{
		    if((ev.code >=2) && ( ev.code <= 11 ))
		    {
			rccode = (ev.code-1) | 0x0200;
		    }
		}
//	    	if( !rccode )
		{
//			rccode = -1;
		}
    	    }
	    rc_last_code = rccode;
	    return 1;
	}
	else
	{
	    rccode = -1;
	    rc_last_key = KEY_RESERVED;
	    rc_last_code = KEY_RESERVED;
	}
    }
    
    rccode = -1;
    usleep(1000000/100);
    return 0;
}

#else

int GetRCCode(int rc_fd, int &rccode)
{
    static int count = 0;
    //get code
    static unsigned short LastKey = -1;
    rccode = -1;
    int bytesavail = 0;
    int bytesread = read(rc_fd, &rccode, 2);
    unsigned short tmprc;
    kbcode = 0;
    
    if (bytesread == 2)
    {
	if (read(rc, &tmprc, 2) == 2)
	{
	    if (rccode == tmprc && count >= 0)
		count++;
	}
    }

    if (bytesread == 2)
    {
	if (rccode == LastKey)
	{
	    if (count < REPEAT_TIMER)
	    {
    		if (count >= 0)
		    count++;
		rccode = -1;
		return 1;
	    }
	}
	else
	    count = 0;
	LastKey = rccode;
	if ((rccode & 0xFF00) == 0x5C00)
	{
	    kbcode = 0;
	    switch(rccode)
	    {
	    case KEY_UP:		rccode = RC_UP;			break;
	    case KEY_DOWN:		rccode = RC_DOWN;		break;
    	    case KEY_LEFT:		rccode = RC_LEFT;		break;
	    case KEY_RIGHT:		rccode = RC_RIGHT;		break;
	    case KEY_OK:		rccode = RC_OK;			break;
	    case KEY_0:			rccode = RC_0;			break;
	    case KEY_1:			rccode = RC_1;			break;
	    case KEY_2:			rccode = RC_2;			break;
	    case KEY_3:			rccode = RC_3;			break;
	    case KEY_4:			rccode = RC_4;			break;
	    case KEY_5:			rccode = RC_5;			break;
	    case KEY_6:			rccode = RC_6;			break;
	    case KEY_7:			rccode = RC_7;			break;
	    case KEY_8:			rccode = RC_8;			break;
	    case KEY_9:			rccode = RC_9;			break;
	    case KEY_RED:		rccode = RC_RED;		break;
	    case KEY_GREEN:		rccode = RC_GREEN;		break;
	    case KEY_YELLOW:	rccode = RC_YELLOW;		break;
	    case KEY_BLUE:		rccode = RC_BLUE;		break;
	    case KEY_VOLUMEUP:	rccode = RC_PLUS;		break;
	    case KEY_VOLUMEDOWN:rccode = RC_MINUS;		break;
	    case KEY_MUTE:		rccode = RC_MUTE;		break;
	    case KEY_HELP:		rccode = RC_HELP;		break;
	    case KEY_SETUP:		rccode = RC_DBOX;		break;
	    case KEY_HOME:		rccode = RC_HOME;		break;
	    case KEY_POWER:		rccode = RC_STANDBY;	break;
	    }
	    return 1;
	}
	else
	{
	    rccode &= 0x003F;
	}
    	return 0;
    }
    
    rccode = -1;
    usleep(1000000/100);
    return 0;
}
#endif

//************************************************************************************************
// 	Add()
//************************************************************************************************
void cMenu::Add(cMenuItem *pMenuItem)
{
    int ItemWidth = pMenuItem->GetItemWidth();
    if (ItemWidth > m_ItemWidth)
	m_ItemWidth = ItemWidth;
	
    pMenuItem->SetItemNr(m_ItemList.size());
    m_ItemList.push_back(pMenuItem);
    if (m_ItemList.size() == 1)
    {
	m_VisibleItemBegin = m_ItemList.begin();
	m_SelectedItem     = m_ItemList.begin();
    }
}

//************************************************************************************************
// 	Draw()
//************************************************************************************************
void cMenu::Draw()
{
    IMPORT_FRAMEBUFFER_VARS;
    
    int MaxMenuHeight = p_ysize - MENU_TOP_MARGIN - MENU_BOTTOM_MARGIN;
    int MenuHeight = MENU_HEADER_HEIGHT;
    m_VisibleItemEnd = m_VisibleItemBegin;
    for (unsigned int i = 0; (MenuHeight + (*m_VisibleItemEnd)->GetItemHeight() <= MaxMenuHeight) && (i < m_ItemList.size()); i++)
    {
	MenuHeight += ITEM_HEIGHT;
	m_VisibleItemEnd++;
    }
    
    if ((m_VisibleItemBegin != m_ItemList.begin()) || (m_VisibleItemEnd != m_ItemList.end()))
	MenuHeight = MENU_HEADER_HEIGHT + m_ItemList.size() * ITEM_HEIGHT;
    
    int MenuTop  = (p_ysize - MenuHeight)  / 2; 
    int MenuLeft = (p_xsize  - m_ItemWidth) / 2;
    
    
    RenderBox(MenuLeft, MenuTop, MenuLeft+m_ItemWidth, MenuTop+MENU_HEADER_HEIGHT, FILL, SKIN1);
    RenderBox(MenuLeft, MenuTop+MENU_HEADER_HEIGHT, MenuLeft+m_ItemWidth, MenuTop+MenuHeight, FILL, SKIN2);
    RenderString(m_pName, MenuLeft+ITEM_TEXT_LEFT, MenuTop, m_ItemWidth-ITEM_TEXT_LEFT-ITEM_TEXT_RIGHT, 
		 LEFT, YELLOW, SKIN1);
	
    TItemList::iterator it;	 
    int Top = MenuTop + MENU_HEADER_HEIGHT;
    for (it = m_VisibleItemBegin; it != m_VisibleItemEnd; it++)
    {
	(*it)->Draw(MenuLeft, Top, m_ItemWidth);
	Top += (*it)->GetItemHeight();
	
	if (it == m_SelectedItem)
	    (*it)->SetSelected(true);
    }
}


//************************************************************************************************
// 	cMenu::ProcessKey()
//************************************************************************************************
bool cMenu::ProcessKey(int Key)
{ 
    bool handled = (*m_SelectedItem)->ProcessKey(Key);
    if (handled)
    {
	m_RetValue = (*m_SelectedItem)->GetRetValue();
	return true;
    }
	
    switch (Key)
    {
    case RC_UP:
	if (m_SelectedItem != m_ItemList.begin())
	{
	    TItemList::iterator old = m_SelectedItem;
	    (*m_SelectedItem)->SetSelected(false);
	    m_SelectedItem--;
	    while (!(*m_SelectedItem)->IsSelectable())
	    {
		if (m_SelectedItem == m_ItemList.begin())
		    m_SelectedItem = old;
		else
		    m_SelectedItem--;
	    }
	    (*m_SelectedItem)->SetSelected(true);
	}
	return true;
	
    case RC_DOWN:
	if (m_SelectedItem != m_ItemList.end())
	{
	    TItemList::iterator old = m_SelectedItem;
	    (*m_SelectedItem)->SetSelected(false);
	    m_SelectedItem++;
	    while ((!(*m_SelectedItem)->IsSelectable()) || (m_SelectedItem == m_ItemList.end()))
	    {
		if (m_SelectedItem == m_ItemList.end())
		    m_SelectedItem = old;
		else
		    m_SelectedItem++;
	    }
	    (*m_SelectedItem)->SetSelected(true);
	}
	return true;

    case RC_HOME:
	return false;
	
    default:
	return true;
    }
}

//************************************************************************************************
// 	cMenu::Show()
//************************************************************************************************
EMenuValue cMenu::Show(int rc_fd)
{
    IMPORT_FRAMEBUFFER_VARS;
    
    STORE_PALETTE(&colormap);
    
    gl_fillbox(0, 0, p_xsize, p_ysize, TRANSP);
    Draw();
    //ShowOsd(True);
    int rccode;
    while( 1 )
    {
	cMenu::GetRCCode(rc_fd, rccode);
	if(!ProcessKey(rccode))
	    break;
	    
	if (m_RetValue != eMVNone)
	    break;
    }
    
    gl_fillbox(0, 0, p_xsize, p_ysize, TRANSP);
    STORE_PALETTE(NULL);
    SendFramebufferUpdateRequest(0, 0, p_xsize, p_ysize, False);
    
    return m_RetValue;
}


//************************************************************************************************
// 	MsgBox()
//************************************************************************************************
void cMenu::MsgBox(int rc_fd, char* header, char* question)
{

    RenderBox(155, 178, 464, 220, FILL, SKIN1);
    RenderBox(155, 220, 464, 327, FILL, SKIN2);
    RenderBox(155, 178, 464, 327, GRID, SKIN3);
    RenderBox(155, 220, 464, 327, GRID, SKIN3);

    RenderString(header, 157, 213, 306, CENTER, YELLOW, SKIN1);
    RenderString(question, 157, 265, 306, CENTER, WHITE, SKIN1);
  
    RenderBox(235, 286, 284, 310, FILL, SKIN3);
    RenderString("OK", 237, 305, 46, CENTER, WHITE, SKIN3);
    RenderBox(335, 286, 384, 310, FILL, SKIN3);
    RenderString("EXIT", 337, 305, 46, CENTER, WHITE, SKIN3);

//    REDRAWBOX(0, 0, 700, 450);
  
    ShowOsd(True);
    
    int rccode;
    while( 1 )
    {
	cMenu::GetRCCode(rc_fd, rccode);
	if(( rccode == RC_OK ) || ( rccode == RC_HOME))
	{
	    break;
	}
    }
    
    ShowOsd(False);
}

//************************************************************************************************
// 	cMenu::PowerButtonMenu()
//************************************************************************************************
bool cMenu::PowerButtonMenu(int rc_fd, fbvnc_event_t *ev)
{
    while (1)
    {
	cMenu Menu("VDR-Viewer Menü");
	Menu.Add(new cMenuSelItem("VDR-Viewer schließen", eMVViewerClose));
	Menu.Add(new cMenuSelItem("VDR ausschalten", eMVVdrShutdown));
	Menu.Add(new cMenuSeparatorItem());
	Menu.Add(new cMenuSelItem("Stream synchronisieren", eMVResync));
	Menu.Add(new cMenuSelItem("LCD umschalten", eMVToggleLCD));
	//Menu.Add(new cMenuSeparatorItem());
	//Menu.Add(new cMenuSelItem("Einstellungen", eMVSettings));
	EMenuValue RetVal = Menu.Show(rc_fd);
    
	dprintf("[vncv] MenuRet: %d\n", RetVal);
	switch(RetVal)
	{
	case eMVViewerClose: // Close VDR-Viewer
    	    ev->evtype = FBVNC_EVENT_QUIT;
    	    return false;
    	    break;
		
	case eMVVdrShutdown: // Send Key to VDR
	    ev->evtype = FBVNC_EVENT_NULL;
	    return false;
	    break;
	    
	case eMVResync: // Resync Stream
	    Resync();
	    ev->evtype = FBVNC_EVENT_NULL;
	    return true;
    	    break;

	case eMVToggleLCD: // Toggle LCD
	    ToggleLCD();
	    ev->evtype = FBVNC_EVENT_NULL;
	    return true;
    	    break;

	case eMVSettings: // Show Settings Menu
	    SettingsMenu(rc_fd);
    	    break;
	    
	default:
    	    ev->evtype = FBVNC_EVENT_NULL;
    	    return true;
    	    break;
	}
    }
    
    ev->evtype = FBVNC_EVENT_NULL;
    return false;
}


//************************************************************************************************
// 	cMenu::SettingsMenu
//************************************************************************************************
void cMenu::SettingsMenu(int rc_fd)
{
    CConfigFile configfile('\t');
    configfile.loadConfig(VDRVIEWER_SETTINGS_FILE);
    std::string serverip = configfile.getString("server", "10.10.10.10");
    int    osdport 	 = configfile.getInt32("osdport", 20001);
    int    streamingport = configfile.getInt32("streamingport", 20002);
    

    cMenu Menu("VDR-Viewer Einstellungen");
    //Menu.Add(new cMenuStringItem("Server-Adresse", serverip));
    Menu.Add(new cMenuIntItem("OSD-Port", &osdport, 100, 99999));
    Menu.Add(new cMenuIntItem("Streaming-Port", &streamingport, 100, 99999));
    Menu.Add(new cMenuSeparatorItem());
    EMenuValue RetVal = Menu.Show(rc_fd);
    
    
    configfile.getString("server", serverip);
    configfile.setInt32("osdport", osdport);
    configfile.setInt32("streamingport", streamingport);
    configfile.saveConfig(VDRVIEWER_SETTINGS_FILE);
}


//************************************************************************************************
// 	HandleMenu()
//************************************************************************************************
bool cMenu::HandleMenu(int rc_fd, struct input_event iev, fbvnc_event_t *ev)
{
	static time_t last_open=0;
	
	if (iev.code == KEY_POWER)
	{
		if (iev.value == 1)
		{
			ev->evtype = FBVNC_EVENT_NULL;  // ignore key pressed event
			return true;
		}
		
		bool ret = PowerButtonMenu(rc_fd, ev);
		
		if (ret)
			last_open = time(NULL);
				
		return ret;
	}

	ev->evtype = FBVNC_EVENT_NULL;

	
	// Billiger Workaround gegen das zusätzliche Auswerten der OK/Home-Keys
	// durch VNC beim verlassen des menüs
/*	if (time(NULL) - last_open <= 1 && (iev.code == KEY_HOME || iev.code == KEY_OK) )
	{
		dprintf("Menü: OK/Home-Key unterdrückt\n");
		return true;
	}*/
	

	
	return false;
}
