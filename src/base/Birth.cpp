/*============================================================================*/
/* Birth.cpp - seconds&microseconds since Posix Epoch January 1, 1970         */
/*============================================================================*/

#include "BASE.h"
#include "eIV.h"

/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
Birth::Birth(void)
: Object(CN_BIRTH),
  m_wIvl(IV_SECOND)
{
  _precondition_(_MAGIC_,CN_BIRTH,_FOOTER_);
  
  m_wTsp.m_iWrd = 0;
  m_wIvl.vSetZero();
  
  _checkInitialised_(CN_BIRTH);
  
  _postcondition_(_MAGIC_,CN_BIRTH,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
Birth::~Birth(void) noexcept(false)
{
  _precondition_(_MAGIC_,CN_BIRTH,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* microseconds since the epoch                                               */
/*----------------------------------------------------------------------------*/
u8 Birth::uNowMicros(void)
{
  struct timespec xNow;
  clock_gettime(CLOCK_REALTIME, &xNow);
  return (u8)xNow.tv_sec * C_1M + (u8)xNow.tv_nsec / 1000;
}

/*----------------------------------------------------------------------------*/
/* clear the birth time                                                       */
/*----------------------------------------------------------------------------*/
void Birth::vClear(void)
{
  _precondition_(_MAGIC_,CN_BIRTH,_FOOTER_);
  
  m_wTsp.m_iWrd = 0;
  m_wIvl = 0;
}

/*----------------------------------------------------------------------------*/
/* get seconds elapsed since birth                                            */
/*----------------------------------------------------------------------------*/
i8 Birth::iElapsedSsec(void)
{
  _precondition_(_MAGIC_,CN_BIRTH,_FOOTER_);
  
  // (it converted the birth timestamp, not the current time: always 0)
  Ivl wCurrentIvl = IvlLib::wToIvl(uNowMicros());

  u8 uMicros = IvlLib::uSubtract(wCurrentIvl, m_wIvl);
  
  return uMicros/C_1M;
}

/*----------------------------------------------------------------------------*/
/* set this Birth as at now                                                   */
/*----------------------------------------------------------------------------*/
void Birth::vNow(void)
{
  _precondition_(_MAGIC_,CN_BIRTH,_FOOTER_);
  
  // Every DataBuffer is born, many per statement: converting the clock to a
  // local timestamp and back (localtime, timelocal) took a tenth of the SQL
  // compile time. The interval is the microseconds since the epoch, which is
  // what that round trip gave. The timestamp is not kept (nothing reads it).
  m_wIvl = IvlLib::wToIvl(uNowMicros());
  m_wTsp.m_iWrd = 0;
}

/*----------------------------------------------------------------------------*/
/*-- Footer ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
__aFooterFunction(Birth)

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
