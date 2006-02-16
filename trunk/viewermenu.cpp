/******************************************************************************
 *                        <<< VDRViewer Plugin >>>
 *-----------------------------------------------------------------------------*/
 

#include <unistd.h>

extern "C" {
#include "vdrviewer.h"
#include "fbgl.h"
}

#include "viewermenu.h"


#define RetEvent(e) do { ev->evtype = (e); return (e); } while(0)


#define ITEM_HOTKEY_LEFT 10
#define ITEM_TEXT_LEFT 30
#define ITEM_TEXT_RIGHT 10 
#define ITEM_TEXT_TOP 2
#define MENU_TOP_MARGIN 50
#define MENU_BOTTOM_MARGIN 50
#define MENU_HEADER_HEIGHT 45
#define MENU_MIN_WIDTH 200 
#define ITEM_HEIGHT 32



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
		  ConvertColor(SKIN3));
    
	RenderString(m_pText, 
		     m_ItemLeft + ITEM_TEXT_LEFT, 
		     m_ItemTop  + ITEM_TEXT_TOP, 
		     m_ItemLeft + m_ItemWidth - ITEM_TEXT_RIGHT, 
		     LEFT, 
		     ConvertColor(WHITE), 
		     ConvertColor(SKIN3));
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
		      ConvertColor(SKIN2));
	}	  
	
	RenderString(m_pText, 
	             m_ItemLeft + ITEM_TEXT_LEFT, 
		     m_ItemTop  + ITEM_TEXT_TOP, 
		     m_ItemLeft + m_ItemWidth - ITEM_TEXT_RIGHT, 
		     LEFT, 
		     ConvertColor(WHITE), 
		     ConvertColor(SKIN2));
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
    if (Key == KEY_OK)
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
	      ConvertColor(SKIN3));
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

//************************************************************************************************
// 	cMenu::GetKey()
//************************************************************************************************
int cMenu::GetKey(int rc_fd)
{
    while (1)
    {
#ifdef HAVE_DREAMBOX_HARDWARE
        unsigned short rckey = 0;
        if (read(rc_fd, &rckey, 2) == 2)
	    return rckey;
#else
        struct input_event iev;
        if (read(rc_fd, &iev, sizeof(struct input_event)) == sizeof(struct input_event) && ((iev.value == 0) || (iev.value == 2)))
	    return iev.code;
#endif
    }
							    
}

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
    
    
    RenderBox(MenuLeft, MenuTop, MenuLeft+m_ItemWidth, MenuTop+MENU_HEADER_HEIGHT, FILL, ConvertColor(SKIN1));
    RenderBox(MenuLeft, MenuTop+MENU_HEADER_HEIGHT, MenuLeft+m_ItemWidth, MenuTop+MenuHeight, FILL, ConvertColor(SKIN2));
    RenderString(m_pName, MenuLeft+ITEM_TEXT_LEFT, MenuTop, m_ItemWidth-ITEM_TEXT_LEFT-ITEM_TEXT_RIGHT, 
		 LEFT, ConvertColor(YELLOW), ConvertColor(SKIN1));
	
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
    case KEY_UP:
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
	
    case KEY_DOWN:
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

    case KEY_HOME:
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
    
    gl_fillbox(0, 0, p_xsize, p_ysize, 0);
    Draw();
    ShowOsd(True);
    
    int rccode;
    while( 1 )
    {
	rccode = cMenu::GetKey(rc_fd);
	if(!ProcessKey(rccode))
	    break;
	    
	if (m_RetValue != eMVNone)
	    break;
    }
    
    SendFramebufferUpdateRequest(0, 0, p_xsize, p_ysize, False);
    
    return m_RetValue;
}


//************************************************************************************************
// 	MsgBox()
//************************************************************************************************
void cMenu::MsgBox(int rc_fd, char* header, char* question)
{

    RenderBox(155, 178, 464, 220, FILL, ConvertColor(SKIN1));
    RenderBox(155, 220, 464, 327, FILL, ConvertColor(SKIN2));
    RenderBox(155, 178, 464, 327, GRID, ConvertColor(SKIN3));
    RenderBox(155, 220, 464, 327, GRID, ConvertColor(SKIN3));

    RenderString(header, 157, 213, 306, CENTER, ConvertColor(YELLOW), ConvertColor(SKIN1));
    RenderString(question, 157, 265, 306, CENTER, ConvertColor(WHITE), ConvertColor(SKIN1));
  
    RenderBox(235, 286, 284, 310, FILL, ConvertColor(SKIN3));
    RenderString("OK", 237, 305, 46, CENTER, ConvertColor(WHITE), ConvertColor(SKIN3));
    RenderBox(335, 286, 384, 310, FILL, ConvertColor(SKIN3));
    RenderString("EXIT", 337, 305, 46, CENTER, ConvertColor(WHITE), ConvertColor(SKIN3));

//    REDRAWBOX(0, 0, 700, 450);
  
    int rccode;
    while( 1 )
    {
	rccode = cMenu::GetKey(rc_fd);
	if(( rccode == KEY_OK ) || ( rccode == KEY_HOME))
	{
	    break;
	}
    }
}




//************************************************************************************************
// 	HandleMenu()
//************************************************************************************************
bool cMenu::HandleMenu(int rc_fd, struct input_event iev, fbvnc_event_t *ev)
{
    if (iev.code == KEY_POWER)
    {
	if (iev.value == 1)
	{
	    ev->evtype = FBVNC_EVENT_NULL;  // ignore key pressed event
	    return true;
	}

	cMenu Menu("VDR-Viewer Menü");
	Menu.Add(new cMenuSelItem("VDR-Viewer schließen", eMVViewerClose));
	Menu.Add(new cMenuSelItem("VDR ausschalten", eMVVdrShutdown));
	Menu.Add(new cMenuSeparatorItem());
	Menu.Add(new cMenuSelItem("Einstellungen", eMVSettings));
	EMenuValue RetVal = Menu.Show(rc_fd);
    
	dprintf("[vncv] MenuRet: %d\n", RetVal);
	switch(RetVal)
	{
	case eMVViewerClose:
	    ev->evtype = FBVNC_EVENT_QUIT;
	    return false;
	    break;
		
	case eMVVdrShutdown:
	    // Send Key to VDR
	    break;
	    
	case eMVSettings:
	    break;
	    
	default:
	    ev->evtype = FBVNC_EVENT_NULL;
	    return True;
	    break;
	}
    }

    ev->evtype = FBVNC_EVENT_NULL;
    return False;
}
