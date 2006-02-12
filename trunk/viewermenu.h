/******************************************************************************
 *                        <<< VDRViewer Plugin >>>
 *-----------------------------------------------------------------------------
 * 
 ******************************************************************************/
#ifndef _VIEWERMENU_H
#define _VIEWERMENU_H

#include <list>

extern "C" {
#include "overlay.h"
}


class cMenuItem 
{
protected:
    const char *m_pText;
    int  m_ItemNr;
    bool m_Selected;
    int  m_ItemLeft;
    int  m_ItemTop;
    int  m_ItemWidth;
    int  m_ItemHeight;
    bool m_Selectable;

public:
    cMenuItem(const char *pName);
    virtual ~cMenuItem();
    virtual bool ProcessKey(int Key);
    void SetItemNr(int Nr);
    void SetSelected(bool Selected);
    int  GetItemWidth();
    int  GetItemHeight();
    bool IsSelectable() { return m_Selectable; };
    virtual void Draw(int ItemLeft = -1, int ItemTop = -1, int ItemWidth = -1);
};
	    
class cMenuIntItem : public cMenuItem {
protected:
    int *m_Value;
    int m_Min, m_Max;

public:
    cMenuIntItem(const char *Name, int *Value, int Min = 0, int Max = INT_MAX);
    virtual ~cMenuIntItem();
    virtual bool ProcessKey(int Key);
};

class cMenuBoolItem : public cMenuItem {
protected:
    const char *falseString, *trueString;
    virtual void Set(void);

public:
    cMenuBoolItem(const char *Name, int *Value, const char *FalseString = NULL, const char *TrueString = NULL);
    virtual bool ProcessKey(int Key) { return false; };
};


class cMenuSeparatorItem : public cMenuItem {
protected:

public:
    cMenuSeparatorItem();
    virtual ~cMenuSeparatorItem();
    virtual void Draw(int ItemLeft = -1, int ItemTop = -1, int ItemWidth = -1);
};
			

typedef std::list<cMenuItem*> TItemList;

class cMenu
{
private:
    const char *m_pName;
    int m_ItemWidth;
    TItemList m_ItemList;
    TItemList::iterator m_VisibleItemBegin;
    TItemList::iterator m_VisibleItemEnd;
    TItemList::iterator m_SelectedItem;
    static int GetKey(int rc_fd);

public:
    cMenu(const char *pName);
    virtual ~cMenu();

    void Add(cMenuItem *pMenuItem);
    virtual bool ProcessKey(int Key);
    void Draw();
    void Show(int rc_fd);

    static void MsgBox(int rc_fd, char* header, char* question);
    static bool HandleMenu(int rc_fd, struct input_event iev, fbvnc_event_t *ev);
    
};



#endif // _VIEWERMENU_H
