#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#include "datasocket.h"

cDataSocket::cDataSocket(char *address, unsigned short  port)
{
    m_Port           = port;
    m_Address        = NULL;
    if (address) 
	m_Address    = strdup(address);
    m_pHandle.fd     = -1;
    m_pHandle.events = POLLIN | POLLPRI;
    m_Terminate      = false;
}


cDataSocket::~cDataSocket()
{
    Close();
}


void cDataSocket::Close()
{
    if(m_pHandle.fd != -1)
    { 
	m_Terminate = true;
	close(m_pHandle.fd);
	m_pHandle.fd = -1;
    }
}


void cDataSocket::Shutdown(int how)
{
    m_Terminate = true;
    shutdown(m_pHandle.fd, how);
}


cDataSocketTCP::cDataSocketTCP(char *address, unsigned short  port)
	         :cDataSocket(address, port)
{
}


cDataSocketTCP::~cDataSocketTCP()
{
}


bool cDataSocketTCP::Open()
{
    struct sockaddr_in ads;
    socklen_t          adsLen;

    //-- tcp server --
    //-----------------------
    bzero((char *)&ads, sizeof(ads));
    ads.sin_family      = AF_INET;
    ads.sin_addr.s_addr = inet_addr(m_Address);
    ads.sin_port        = htons(m_Port);
    adsLen              = sizeof(ads);
    
    m_Terminate = false;

    //-- get socket and try connect --
    //--------------------------------
    if( (m_pHandle.fd = socket(AF_INET, SOCK_STREAM, 0)) != -1)
    {
	if( connect(m_pHandle.fd, (struct sockaddr *)&ads, adsLen) == -1 )
	{
	    close(m_pHandle.fd);
	    m_pHandle.fd = -1;
	}
    }
    
    return (m_pHandle.fd != -1);
}


ssize_t cDataSocketTCP::Get(char *buf, size_t count, int tio)
{
    if (tio > 0)
    {
	ssize_t n, nleft = count;

	while( (nleft>0) && (poll(&m_pHandle, 1, tio) > 0) && (m_pHandle.events & m_pHandle.revents) && !m_Terminate )
	{
	    n = read(m_pHandle.fd, buf, nleft);
	    if (n<0) 
	    {
	        if (errno != EAGAIN) 
		    return -1;
	    }
	    nleft -= n;
	    buf   += n;
        }

	return(count-nleft);
    }
    else
    {
	return recv(m_pHandle.fd, buf, count, 0);
    }
}


ssize_t cDataSocketTCP::Put(char *buf, size_t count, int tio)
{
   if (tio > 0)
   {
      ssize_t n, nleft = count;
      
      while( (nleft>0) && (poll(&m_pHandle, 1, tio) > 0) && (m_pHandle.events & m_pHandle.revents) && !m_Terminate )
      {
         n = write(m_pHandle.fd, buf, nleft);
         if (n<0) 
         {
            if (errno != EAGAIN) 
            return -1;
         }
         nleft -= n;
         buf   += n;
      }
      
      return(count-nleft);
   }
   else
   {
      return send(m_pHandle.fd, buf, count, 0);
   }
}


cDataSocketUDP::cDataSocketUDP(char *address, unsigned short  port)
	         :cDataSocket(address, port)
{
}


cDataSocketUDP::~cDataSocketUDP()
{
}


bool cDataSocketUDP::Open()
{
    struct sockaddr_in 	ads;
    socklen_t          	adsLen;
    int		 	res, one;	

    //-- tcp server --
    //-----------------------
    bzero((char *)&ads, sizeof(ads));
    ads.sin_family      = AF_INET;
    ads.sin_addr.s_addr = htonl( INADDR_ANY );
    ads.sin_port        = htons(m_Port);
    adsLen              = sizeof(ads);
    
    m_Terminate = false;

    //-- get socket and try connect --
    //--------------------------------
    if( (m_pHandle.fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
	printf("Error: socket() faild: %i %s\n", errno,strerror(errno));
	return false;
    }
    
    res=bind(m_pHandle.fd,(sockaddr*)&ads,sizeof ads);
    if (res<0) 
    {
	printf("Error: bind() faild: %i %s\n", errno,strerror(errno));
	return false;
    }
							    
    // Set non-blocking
    res=fcntl(m_pHandle.fd,F_SETFL,O_NONBLOCK);
    if (res<0) 
    {
        printf("Error: fcntl(*,F_SETFL,O_NONBLOCK) faild: %i %s\n", errno, strerror(errno));
        return false;
    }

    // Enable broadcast support for socket
//    res=setsockopt_int(m_pHandle.fd, SOL_SOCKET, SO_BROADCAST, 1);
//    if (res<0) 
//    {
//        printf("Error: setsockopt(*,SOL_SOCKET,SO_BROADCAST,*,*) faild: %i %s\n", errno, strerror(errno));
//    }
																					    
    
    return (m_pHandle.fd != -1);
}


ssize_t cDataSocketUDP::Get(char *buf, size_t count, int tio)
{
    ssize_t n, nleft = count;

    while( (nleft>0) && (poll(&m_pHandle, 1, tio) > 0) && (m_pHandle.events & m_pHandle.revents) && !m_Terminate )
    {
        n = recvfrom(m_pHandle.fd, buf, nleft, 0, NULL, NULL);
        if (n<0) 
        {
            if (errno != EAGAIN) 
    	    return -1;
        }
        nleft -= n;
        buf   += n;
    }

    return(count-nleft);
}


ssize_t cDataSocketUDP::Put(char *buf, size_t count, int tio)
{
   ssize_t n, nleft = count;
   
   while( (nleft>0) && (poll(&m_pHandle, 1, tio) > 0) && (m_pHandle.events & m_pHandle.revents) && !m_Terminate )
   {
      n = sendto(m_pHandle.fd, buf, nleft, 0, NULL, NULL);
      if (n<0) 
      {
         if (errno != EAGAIN) 
         return -1;
      }
      nleft -= n;
      buf   += n;
   }
   
   return(count-nleft);
}