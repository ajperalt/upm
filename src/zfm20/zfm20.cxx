/*
 * Author: Jon Trulson <jtrulson@ics.com>
 * Copyright (c) 2015 Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <iostream>

#include "zfm20.h"

using namespace upm;
using namespace std;

static const int defaultDelay = 100;     // max wait time for read

ZFM20::ZFM20(int uart)
{
  m_ttyFd = -1;

  if ( !(m_uart = mraa_uart_init(uart)) )
    {
      cerr << __FUNCTION__ << ": mraa_uart_init() failed" << endl;
      return;
    }

  // This requires a recent MRAA (1/2015)
  const char *devPath = mraa_uart_get_dev_path(m_uart);

  if (!devPath)
    {
      cerr << __FUNCTION__ << ": mraa_uart_get_dev_path() failed" << endl;
      return;
    }

  // now open the tty
  if ( (m_ttyFd = open(devPath, O_RDWR)) == -1)
    {
      cerr << __FUNCTION__ << ": open of " << devPath << " failed: " 
           << strerror(errno) << endl;
      return;
    }

  // Set the default password and address
  setPassword(ZFM20_DEFAULT_PASSWORD);
  setAddress(ZFM20_DEFAULT_ADDRESS);

  initClock();
}

ZFM20::~ZFM20()
{
  if (m_ttyFd != -1)
    close(m_ttyFd);

  mraa_deinit();
}

bool ZFM20::dataAvailable(unsigned int millis)
{
  if (m_ttyFd == -1)
    return false;

  struct timeval timeout;

  // no waiting
  timeout.tv_sec = 0;
  timeout.tv_usec = millis * 1000;

  int nfds;  
  fd_set readfds;

  FD_ZERO(&readfds);

  FD_SET(m_ttyFd, &readfds);
  
  if (select(m_ttyFd + 1, &readfds, NULL, NULL, &timeout) > 0)
    return true;                // data is ready
  else
    return false;
}

int ZFM20::readData(char *buffer, size_t len)
{
  if (m_ttyFd == -1)
    return(-1);

  if (!dataAvailable(defaultDelay))
    return 0;               // timed out

  int rv = read(m_ttyFd, buffer, len);

  if (rv < 0)
    cerr << __FUNCTION__ << ": read failed: " << strerror(errno) << endl;

  return rv;
}

int ZFM20::writeData(char *buffer, size_t len)
{
  if (m_ttyFd == -1)
    return(-1);

  // first, flush any pending but unread input
  tcflush(m_ttyFd, TCIFLUSH);

  int rv = write(m_ttyFd, buffer, len);

  if (rv < 0)
    {
      cerr << __FUNCTION__ << ": write failed: " << strerror(errno) << endl;
      return rv;
    }

  tcdrain(m_ttyFd);

  return rv;
}

bool ZFM20::setupTty(speed_t baud)
{
  if (m_ttyFd == -1)
    return(false);
  
  struct termios termio;

  // get current modes
  tcgetattr(m_ttyFd, &termio);

  // setup for a 'raw' mode.  81N, no echo or special character
  // handling, such as flow control.
  cfmakeraw(&termio);

  // set our baud rates
  cfsetispeed(&termio, baud);
  cfsetospeed(&termio, baud);

  // make it so
  if (tcsetattr(m_ttyFd, TCSAFLUSH, &termio) < 0)
    {
      cerr << __FUNCTION__ << ": tcsetattr failed: " << strerror(errno) << endl;
      return false;
    }

  return true;
}

int ZFM20::writeCmdPacket(unsigned char *pkt, int len)
{
  unsigned char rPkt[ZFM20_MAX_PKT_LEN];

  rPkt[0] = ZFM20_START1;             // header bytes
  rPkt[1] = ZFM20_START2;

  rPkt[2] = (m_address >> 24) & 0xff; // address
  rPkt[3] = (m_address >> 16) & 0xff;
  rPkt[4] = (m_address >> 8) & 0xff;
  rPkt[5] = m_address & 0xff;

  rPkt[6] = PKT_COMMAND;

  rPkt[7] = ((len + 2) >> 8) & 0xff;  // length (+ len bytes)
  rPkt[8] = (len + 2) & 0xff;

  // compute the starting checksum
  uint16_t cksum = rPkt[7] + rPkt[8] + PKT_COMMAND;

  int j = 9;
  for (int i=0; i<len; i++)
    {
      rPkt[j] = pkt[i];
      cksum += rPkt[j];
      j++;
    }

  rPkt[j++] = (cksum >> 8) & 0xff;    // store the cksum
  rPkt[j++] = cksum & 0xff;

  return writeData((char *)rPkt, j);
}

void ZFM20::initClock()
{
  gettimeofday(&m_startTime, NULL);
}

uint32_t ZFM20::getMillis()
{
  struct timeval elapsed, now;
  uint32_t elapse;

  // get current time
  gettimeofday(&now, NULL);

  // compute the delta since m_startTime
  if( (elapsed.tv_usec = now.tv_usec - m_startTime.tv_usec) < 0 ) 
    {
      elapsed.tv_usec += 1000000;
      elapsed.tv_sec = now.tv_sec - m_startTime.tv_sec - 1;
    } 
  else 
    {
      elapsed.tv_sec = now.tv_sec - m_startTime.tv_sec;
    }

  elapse = (uint32_t)((elapsed.tv_sec * 1000) + (elapsed.tv_usec / 1000));

  // never return 0
  if (elapse == 0)
    elapse = 1;

  return elapse;
}

bool ZFM20::verifyPacket(unsigned char *pkt)
{
  // verify packet header
  if (pkt[0] != ZFM20_START1 || pkt[1] != ZFM20_START2)
    {
      cerr << __FUNCTION__ << ": Invalid packet header." << endl;
      return false;
    }

  // check the ack byte
  if (pkt[6] != PKT_ACK)
    {
      cerr << __FUNCTION__ << ": Invalid ACK code: " 
           << int(pkt[6]) << ", expected: " << int(PKT_ACK) << endl;
      return false;
    }
  
  return true;
}

bool ZFM20::getResponse(unsigned char *pkt, int len)
{
  char buf[ZFM20_MAX_PKT_LEN];

  initClock();

  int idx = 0;
  int timer = 0;
  int rv;
  int plen = 0;

  while (idx < len)
    {
      // wait for some data
      if (!dataAvailable(100))
        {
          timer += getMillis();
          if (timer > ZFM20_TIMEOUT)
            {
              cerr << __FUNCTION__ << ": Timed out waiting for packet" << endl;
              return false;
            }

          continue;
        }

      if ((rv = readData(buf, ZFM20_MAX_PKT_LEN)) <= 0)
        {
          cerr << __FUNCTION__ << ": Read failed" << endl;
          return false;
        }

      // copy it into the user supplied buffer
      for (int i=0; i<rv; i++)
        {
          pkt[idx++] = buf[i];
          if (idx >= len)
            break;
        }
    }

  // now verify it.
  return verifyPacket(pkt);
}

bool ZFM20::verifyPassword()
{
  const int pktLen = 5;
  uint8_t pkt[pktLen] = {CMD_VERIFY_PASSWORD, 
                         (m_password >> 24) & 0xff,
                         (m_password >> 16) & 0xff,
                         (m_password >> 8) & 0xff,
                         m_password & 0xff };

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return false;
    }

  // now read a response
  const int rPktLen = 12;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return false;
    }

  return true;
}

int ZFM20::getNumTemplates()
{
  const int pktLen = 1;
  uint8_t pkt[pktLen] = {CMD_GET_TMPL_COUNT};

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return 0;
    }

  // now read a response
  const int rPktLen = 14;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return 0;
    }

  // check confirmation code
  if (rPkt[9] != 0x00)
    {
      cerr << __FUNCTION__ << ": Invalid confirmation code:" << int(rPkt[9])
           << endl;
      return 0;
    }      

  return ((rPkt[10] << 8) | rPkt[11]);
}

bool ZFM20::setNewPassword(uint32_t pwd)
{
  const int pktLen = 5;
  uint8_t pkt[pktLen] = {CMD_SET_PASSWORD, 
                         (pwd >> 24) & 0xff,
                         (pwd >> 16) & 0xff,
                         (pwd >> 8) & 0xff,
                         pwd & 0xff };

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return false;
    }

  // now read a response
  const int rPktLen = 12;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return false;
    }

  // check confirmation code
  if (rPkt[9] != 0x00)
    {
      cerr << __FUNCTION__ << ": Invalid confirmation code:" << int(rPkt[9])
           << endl;
      return false;
    }      

  m_password = pwd;

  return true;
}

bool ZFM20::setNewAddress(uint32_t addr)
{
  const int pktLen = 5;
  uint8_t pkt[pktLen] = {CMD_SET_ADDRESS, 
                         (addr >> 24) & 0xff,
                         (addr >> 16) & 0xff,
                         (addr >> 8) & 0xff,
                         addr & 0xff };

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return false;
    }

  // now read a response
  const int rPktLen = 12;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return false;
    }

  // check confirmation code
  if (rPkt[9] != 0x00)
    {
      cerr << __FUNCTION__ << ": Invalid confirmation code:" << int(rPkt[9])
           << endl;
      return false;
    }      

  m_address = addr;

  return true;
}

uint8_t ZFM20::generateImage()
{
  const int pktLen = 1;
  uint8_t pkt[pktLen] = {CMD_GEN_IMAGE};

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  // now read a response
  const int rPktLen = 12;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  return rPkt[9];
}

uint8_t ZFM20::image2Tz(int slot)
{
  if (slot != 1 && slot != 2)
    {
      cerr << __FUNCTION__ << ": slot must be 1 or 2" << endl;
      return ERR_INTERNAL_ERR;
    }

  const int pktLen = 2;
  uint8_t pkt[pktLen] = {CMD_IMG2TZ,
                         (slot & 0xff)};

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  // now read a response
  const int rPktLen = 12;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  return rPkt[9];
}

uint8_t ZFM20::createModel()
{
  const int pktLen = 1;
  uint8_t pkt[pktLen] = {CMD_REGMODEL};

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  // now read a response
  const int rPktLen = 12;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  return rPkt[9];
}

uint8_t ZFM20::storeModel(int slot, uint16_t id)
{
  if (slot != 1 && slot != 2)
    {
      cerr << __FUNCTION__ << ": slot must be 1 or 2" << endl;
      return ERR_INTERNAL_ERR;
    }

  const int pktLen = 4;
  uint8_t pkt[pktLen] = {CMD_STORE,
                         (slot & 0xff),
                         (id >> 8) & 0xff,
                         id & 0xff};

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  // now read a response
  const int rPktLen = 12;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  return rPkt[9];
}

uint8_t ZFM20::deleteModel(uint16_t id)
{
  const int pktLen = 5;
  uint8_t pkt[pktLen] = {CMD_DELETE_TMPL,
                         (id >> 8) & 0xff,
                         id & 0xff,
                         0x00,
                         0x01};

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  // now read a response
  const int rPktLen = 12;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  return rPkt[9];
}

uint8_t ZFM20::deleteDB()
{
  const int pktLen = 1;
  uint8_t pkt[pktLen] = {CMD_EMPTYDB};

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  // now read a response
  const int rPktLen = 12;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  return rPkt[9];
}

uint8_t ZFM20::search(int slot, uint16_t *id, uint16_t *score)
{
  *id = 0;
  *score = 0;

  if (slot != 1 && slot != 2)
    {
      cerr << __FUNCTION__ << ": slot must be 1 or 2" << endl;
      return ERR_INTERNAL_ERR;
    }

  // search from page 0x0000 to page 0x00a3
  const int pktLen = 6;
  uint8_t pkt[pktLen] = {CMD_SEARCH,
                         (slot & 0xff),
                         0x00,
                         0x00,
                         0x00,
                         0xa3};

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  // now read a response
  const int rPktLen = 16;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  // if it was found, extract the location and the score
  if (rPkt[9] == ERR_OK)
    {
      *id = (rPkt[10] << 8) & 0xff | rPkt[11] & 0xff;
      *score = (rPkt[12] << 8) & 0xff | rPkt[13] & 0xff;
    }

  return rPkt[9];
}

uint8_t ZFM20::match(uint16_t *score)
{
  *score = 0;

  const int pktLen = 1;
  uint8_t pkt[pktLen] = {CMD_MATCH};

  if (!writeCmdPacket(pkt, pktLen))
    {
      cerr << __FUNCTION__ << ": writePacket() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  // now read a response
  const int rPktLen = 14;
  unsigned char rPkt[rPktLen];

  if (!getResponse(rPkt, rPktLen))
    {
      cerr << __FUNCTION__ << ": getResponse() failed" << endl;
      return ERR_INTERNAL_ERR;
    }

  *score = (rPkt[10] << 8) & 0xff | rPkt[11] & 0xff;

  return rPkt[9];
}


