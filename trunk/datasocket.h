/******************************************************************************
 *                        <<< VDRViewer Plugin >>>
 *-----------------------------------------------------------------------------
 *
 ******************************************************************************/
#ifndef _DATASOCKET_H
#define _DATASOCKET_H

#include <poll.h>


class cDataSocket
{
public:
    cDataSocket(char *address, unsigned short  port);
    virtual ~cDataSocket();
    virtual bool Open() = 0;
    virtual void Close();
    virtual void Shutdown(int how);
    virtual ssize_t Get(char *buf, size_t count, int tio = 0) = 0;
    virtual bool IsOpen() { return m_pHandle.fd != -1; };

protected:
    char	  *m_Address;
    uint16_t 	  m_Port;
    struct pollfd m_pHandle;
    bool 	  m_Terminate;
};


class cDataSocketTCP : public cDataSocket
{
public:
    cDataSocketTCP(char *address, unsigned short port);
    virtual ~cDataSocketTCP();
    virtual bool Open();
    virtual ssize_t cDataSocketTCP::Get(char *buf, size_t count, int tio = 0);

private:
};


class cDataSocketUDP : public cDataSocket
{
public:
    cDataSocketUDP(char *address, unsigned short  port);
    virtual ~cDataSocketUDP();
    virtual bool Open();
    virtual ssize_t cDataSocketUDP::Get(char *buf, size_t count, int tio = 0);

private:
};

#endif // _DATASOCKET_H
