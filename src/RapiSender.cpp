#include <Arduino.h>
#include "RapiSender.h"

//#define DBG

#ifdef DBG
#include <SoftwareSerial.h>
extern SoftwareSerial alt;
#define dbgprint(s) alt.print(s)
#define dbgprintln(s) alt.println(s)
#else
#define dbgprint(s)
#define dbgprintln(s)
#endif // DBG

// convert 2-digit hex string to uint8_t
uint8_t
htou8(const char *s) {
  uint8_t u = 0;
  for (int i = 0; i < 2; i++) {
    char c = s[i];
    if (c != '\0') {
      if (i == 1)
        u <<= 4;
      if ((c >= '0') && (c <= '9')) {
        u += c - '0';
      } else if ((c >= 'A') && (c <= 'F')) {
        u += c - 'A' + 10;
      }
      //      else if ((c >= 'a') && (c <= 'f')) {
      //        u += c - 'a' + 10;
      //      }
      else {
        // invalid character received
        return 0;
      }
    }
  }
  return u;
}


RapiSender::RapiSender(Stream * stream) {
  _stream = stream;
  *_respBuf = 0;
  _flags = 0;

  _sequenceId = RAPI_INVALID_SEQUENCE_ID;
}

// return = 0 = OK
//        = 1 = command will cause buffer overflow
void
RapiSender::_sendCmd(const char *cmdstr) {
  _stream->print(cmdstr);
  dbgprint(cmdstr);

  const char *s = cmdstr;
  uint8_t chk = 0;
  while (*s) {
    chk ^= *(s++);
  }
  if (_sequenceIdEnabled()) {
    if (++_sequenceId == RAPI_INVALID_SEQUENCE_ID)
      ++_sequenceId;
    sprintf(_respBuf, " %c%02X", ESRAPI_SOS, (unsigned) _sequenceId);
    s = _respBuf;
    while (*s) {
      chk ^= *(s++);
    }
    _stream->print(_respBuf);
    dbgprint(_respBuf);
  }

  sprintf(_respBuf, "^%02X%c", (unsigned) chk, ESRAPI_EOC);
  _stream->print(_respBuf);
  dbgprintln(_respBuf);
  _stream->flush();

  *_respBuf = 0;
}


// return = 0 = OK
//        = 1 = bad checksum
//        = 2 = bad sequence id
int
RapiSender::_tokenize() {
  uint8_t chk = 0;
  char *s = _respBuf;
  dbgprint("resp: ");
  dbgprintln(_respBuf);

  while (*s != '^') {
    chk ^= *(s++);
  }
  if (*s == '^') {
    uint8_t rchk = htou8(s + 1);
    if (rchk != chk) {
      _tokenCnt = 0;
#ifdef DBG
      sprintf(_respBuf, "bad chk %x %x %s", rchk, chk, s);
      dbgprintln(_respBuf);
#endif
      return 1;
    }
    *s = '\0';
    s = _respBuf;
  }

  _tokenCnt = 0;
  while (*s) {
    _tokens[_tokenCnt++] = s++;
    if (_tokenCnt == RAPI_MAX_TOKENS)
      break;

    while (*s && (*s != ' ')) {
      s++;
    }
    if (*s == ' ') {
      *(s++) = '\0';
    }

    if (*s == ESRAPI_SOS) {
      *(s++) = '\0';
      uint8_t seqid = htou8(s + 1);
      if (seqid != _sequenceId) {
#ifdef DBG
        sprintf(_respBuf, "bad seqid %x %x %s", seqid, _sequenceId, s);
        dbgprintln(_respBuf);
#endif
        _tokenCnt = 0;
        return 2;
      }
      break;                    // sequence id is last - break out
    }
  }

  return 0;
}

/*
 * return values:
 * -1= timeout
 * 0= success
 * 1=$NK
 * 2=invalid RAPI response
 * 3=cmdstr too long
*/
int
RapiSender::sendCmd(const char *cmdstr, unsigned long timeout) {
  _sendCmd(cmdstr);

  unsigned long mss = millis();
start:
  _tokenCnt = 0;
  *_respBuf = 0;
  int bufpos = 0;
  do {
    int bytesavail = _stream->available();
    if (bytesavail) {
      for (int i = 0; i < bytesavail; i++) {
        char c = _stream->read();
        if (!bufpos && (c != ESRAPI_SOC)) {
          // wait for start character
          continue;
        } else if (bufpos && (c == ESRAPI_EOC)) {
          _respBuf[bufpos] = '\0';
          // Save the original response
          strncpy(_respBufOrig, _respBuf, RAPI_BUFLEN);
          if (!_tokenize())
            break;
          else
            goto start;
        } else {
          _respBuf[bufpos++] = c;
          if (bufpos >= (RAPI_BUFLEN - 1))
            return -2;
        }
      }
    }
  } while (!_tokenCnt && ((millis() - mss) < timeout));

#ifdef DBG
  dbgprint("\n\rTOKENCNT: ");
  dbgprintln(_tokenCnt);
  for (int i = 0; i < _tokenCnt; i++) {
    dbgprintln(_tokens[i]);
  }
  dbgprintln("");
#endif

  if (_tokenCnt) {
    if (!strcmp(_respBuf + 1, "OK")) {
      return 0;
    } else if (!strcmp(_respBuf + 1, "NK")) {
      return 1;
    }
    /*notyet
       else if (!strcmp(_respBuf,"$WF")) { // async WIFI
       ResetEEPROM();
       goto start;
       }
       else if (!strcmp(respBuf,"$ST")) { // async EVSE state transition
       // placeholder.. no action et
       goto start;
       }
     */
    else { // not OK or NK
      return 2;
    }
  } else { // !_tokenCnt
    return -1;
  }
}

void
RapiSender::enableSequenceId(uint8_t tf) {
  if (tf) {
    _sequenceId = (uint8_t) millis();   // seed with random number
    _flags |= RSF_SEQUENCE_ID_ENABLED;
  } else {
    _sequenceId = RAPI_INVALID_SEQUENCE_ID;
    _flags &= ~RSF_SEQUENCE_ID_ENABLED;
  }
}