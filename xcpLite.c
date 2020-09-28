

/*****************************************************************************
| Project Name:   XCP Protocol Layer
|    File Name:   xcpLite.c
|    V1.0 23.9.2020
|
|  Description:   Implementation of the XCP Protocol Layer 
|                 XCP V1.0 slave device driver
|                 Lite Version (see feature list below)
|                 Don't change this file !
|
|
|     Limitations of the Lite version
|
|     - Only suitable for XCP on Ethernet on 32 bit Microcontrollers !!
|     - Platform must support unaligned word and dword memory access
|     - 8 bit and 16 bit CPUs are not supported
|     - Daq and Event numbers are BYTE
|     - No transmit queue
|     - Fixed DAQ+ODT header
|     - Fixed 32 bit time stamp
|     - Only dynamic DAQ list allocation supported
|     - MAX_DTO is limited to max. 255
|     - Resume is not supported
|     - Overload indication by event is not supported
|     - DAQ does not support address extensions and prescaler
|     - DAQ-list and event channel prioritization is not supported
|     - Event channels contain one DAQ-list
|     - ODT optimization not supported
|     - Interleaved communication mode is not supported
|     - Seed & key is not supported
|     - Flash programming is not supported
|     - Calibration pages are not supported
|     - Checksum is not supported
|     - Event messages (SERV_TEXT) are not supported
|     - User commands are not supported
|
|       More features are available in the basic version available free of charge from
|       Vector Informatik GmbH
|
|Limitations of the XCP basic version:
|
|     - Stimulation (Bypassing) is not available
|         XCP_ENABLE_STIM
|     - Bit stimulation is not available
|         XCP_ENABLE_STIM_BIT
|     - SHORT_DOWNLOAD is not available
|         XCP_ENABLE_SHORT_DOWNLOAD
|     - MODIFY_BITS is not available
|         XCP_ENABLE_MODIFY_BITS
|     - FLASH and EEPROM Programming is not available
|         XCP_ENABLE_PROGRAM, XCP_ENABLE_BOOTLOADER_DOWNLOAD, XCP_ENABLE_READ_EEPROM, XCP_ENABLE_WRITE_EEPROM
|     - Block mode for UPLOAD, DOWNLOAD and PROGRAM is not available
|         XCP_ENABLE_BLOCK_UPLOAD, XCP_ENABLE_BLOCK_DOWNLOAD
|     - Resume mode is not available
|         XCP_ENABLE_DAQ_RESUME
|     - Memory write and read protection is not supported
|         XCP_ENABLE_WRITE_PROTECTION
|         XCP_ENABLE_READ_PROTECTION
|     - Checksum calculation with AUTOSAR CRC module is not supported
|         XCP_ENABLE_AUTOSAR_CRC_MODULE
|     
|       All these feature are available in the full version available from
|       Vector Informatik GmbH
|***************************************************************************/


/***************************************************************************/
/* Include files                                                           */
/***************************************************************************/

#include "xcpLite.h"


/****************************************************************************/
/* Macros                                                                   */
/****************************************************************************/

#define error(e) { err=(e); goto negative_response; }
#define check_error(e) { err=(e); if (err!=0) { goto negative_response; } }
#define error1(e,b1) { err=(e); CRM_BYTE(2)=(b1); xcp.CrmLen=3; goto negative_response1; }
#define error2(e,b1,b2) { err=(e); CRM_BYTE(2)=(b1); CRM_BYTE(3)=(b2); xcp.CrmLen=4; goto negative_response1; }


#define XCP_WRITE_BYTE_2_ADDR(addr, data)           *(addr) = (data) 
#define XCP_READ_BYTE_FROM_ADDR(addr)               *(addr) 
  

/****************************************************************************/
/* Local data                                                               */
/****************************************************************************/

RAM tXcpData xcp; 

static tXcpDto dto;

#if defined ( XCP_ENABLE_TESTMODE )
vuint8 gDebugLevel;
#endif



/***************************************************************************/
/* Prototypes for local functions                                          */
/***************************************************************************/

static vuint8 XcpWriteMta( vuint8 size, const BYTEPTR data );
static vuint8 XcpReadMta( vuint8 size, BYTEPTR data );
static void XcpFreeDaq( void );
static vuint8 XcpAllocMemory( void );
static vuint8 XcpAllocDaq( vuint8 daqCount );
static vuint8 XcpAllocOdt( vuint8 daq, vuint8 odtCount );
static vuint8 XcpAllocOdtEntry( vuint8 daq, vuint8 odt, vuint8 odtEntryCount );
static void XcpStartDaq( vuint8 daq );
static void XcpStartAllSelectedDaq( void );
static void XcpStopDaq( vuint8 daq );
static void XcpStopAllSelectedDaq( void );
static void XcpStopAllDaq( void );





/****************************************************************************/
/* Transmit                                                                 */
/****************************************************************************/


/*****************************************************************************
| NAME:             XcpSendCrm
| CALLED BY:        XcpBackground, XcpCommand, XcpSendCallBack, application
| PRECONDITIONS:    XCP is initialized and in connected state and 
|                   a command packet (CMD) has been received.
| INPUT PARAMETERS: none
| RETURN VALUES:    none 
| DESCRIPTION:      Transmission of a command response packet (RES), 
|                    or error packet (ERR) if no other packet is pending.
******************************************************************************/
void XcpSendCrm( void )
{
  ApplXcpSend(xcp.CrmLen,&xcp.Crm.b[0]);
  ApplXcpSendFlush();
  
}


/*****************************************************************************
| NAME:             XcpSendDto
| CALLED BY:        XcpSendDtoFromQueue, XcpEvent, XcpSendCallBack
| PRECONDITIONS:    none
| INPUT PARAMETERS: dto : pointer to XCP packet type definition
| RETURN VALUES:    none 
| DESCRIPTION:      Send a DTO.
******************************************************************************/

void XcpSendDto( const tXcpDto *dto )
{
  ApplXcpSend( dto->l, &dto->b[0] );
}




/****************************************************************************/
/* Handle Mta (Memory-Transfer-Address) */
/****************************************************************************/

#define XcpSetMta(p,e) (xcp.Mta = (p)) 


/*****************************************************************************
| NAME:             XcpWriteMta
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: size : number of data bytes.
|                   data : address of data.
| RETURN VALUES:    XCP_CMD_OK, XCP_CMD_DENIED
| DESCRIPTION:      Write n bytes.
|                   Copying of size bytes from data to xcp.Mta
******************************************************************************/
static vuint8 XcpWriteMta( vuint8 size, const BYTEPTR data )
{

  /* Standard RAM memory write access */
  while ( size > (vuint8)0u ) 
  {
    XCP_WRITE_BYTE_2_ADDR( xcp.Mta, *data );
    xcp.Mta++; 
    data++; 
    size--;
  }
  return (vuint8)XCP_CMD_OK;
  
}

/*****************************************************************************
| NAME:             XcpReadMta
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: size :
|                   data : address of data
| RETURN VALUES:    XCP_CMD_OK
| DESCRIPTION:      Read n bytes.
|                   Copying of size bytes from data to xcp.Mta
******************************************************************************/
static vuint8 XcpReadMta( vuint8 size, BYTEPTR data )
{

  /* Standard RAM memory read access */
  while (size > 0)
  {
    *(data) = XCP_READ_BYTE_FROM_ADDR( xcp.Mta );
    data++; 
    xcp.Mta++; 
    size--;
  }
  return (vuint8)XCP_CMD_OK;

  
}


/****************************************************************************/
/* Data Aquisition Setup                                                    */
/****************************************************************************/


/*****************************************************************************
| NAME:             XcpFreeDaq
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    none
| DESCRIPTION:      Free all dynamic DAQ lists
******************************************************************************/
static void XcpFreeDaq( void )
{
  xcp.SessionStatus &= (SessionStatusType)((~SS_DAQ) & 0xFFu);

  xcp.Daq.DaqCount = 0;
  xcp.Daq.OdtCount = 0;
  xcp.Daq.OdtEntryCount = 0;

  xcp.pOdt = (tXcpOdt*)0;
  xcp.pOdtEntryAddr = 0;
  xcp.pOdtEntrySize = 0;

  memset((BYTEPTR)&xcp.Daq.u.b[0], 0, (vuint16)kXcpDaqMemSize);  
  
}

/*****************************************************************************
| NAME:             XcpAllocMemory
| CALLED BY:        XcpAllocDaq, XcpAllocOdt, XcpAllocOdtEntry, XcpInit
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    0, CRC_MEMORY_OVERFLOW
| DESCRIPTION:      Allocate Memory for daq,odt,odtEntries and Queue
|                   according to DaqCount, OdtCount and OdtEntryCount
******************************************************************************/
static vuint8 XcpAllocMemory( void )
{
  vuint16 s;
  
  /* Check memory overflow */
  s = (vuint16)( ( xcp.Daq.DaqCount      *   (vuint8)sizeof(tXcpDaqList)                           ) + 
                 ( xcp.Daq.OdtCount      *  (vuint16)sizeof(tXcpOdt)                               ) + 
                 ( xcp.Daq.OdtEntryCount * ( (vuint8)sizeof(DAQBYTEPTR) + (vuint8)sizeof(vuint8) ) )
               );
  
  if (s>=(vuint16)kXcpDaqMemSize) return (vuint8)CRC_MEMORY_OVERFLOW;
  
  xcp.pOdt = (tXcpOdt*)&xcp.Daq.u.DaqList[xcp.Daq.DaqCount];
  xcp.pOdtEntryAddr = (DAQBYTEPTR*)&xcp.pOdt[xcp.Daq.OdtCount];
  xcp.pOdtEntrySize = (vuint8*)&xcp.pOdtEntryAddr[xcp.Daq.OdtEntryCount]; 
  

  #if defined ( XCP_ENABLE_TESTMODE )
    if ( gDebugLevel != 0) ApplXcpPrint("[XcpAllocMemory] %u/%u Bytes used\n",s,kXcpDaqMemSize );
  #endif

  return (vuint8)0u;
  
}



/*****************************************************************************
| NAME:             XcpAllocDaq
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: daqCount : 
| RETURN VALUES:    return value of XcpAllocMemory, CRC_SEQUENCE
| DESCRIPTION:      Allocate DAQ list
******************************************************************************/
static vuint8 XcpAllocDaq( vuint8 daqCount )
{
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
  if ( (xcp.Daq.OdtCount!=0) || (xcp.Daq.OdtEntryCount!=0) )
  {
    return (vuint8)CRC_SEQUENCE;
  }
  if( daqCount == 0 )
  {
    return (vuint8)CRC_OUT_OF_RANGE;
  }
  #endif

  xcp.Daq.DaqCount = daqCount;

  return XcpAllocMemory();
  
}

/*****************************************************************************
| NAME:             XcpAllocOdt
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: daq      : 
|                   odtCount :
| RETURN VALUES:    return value of XcpAllocMemory,
|                   CRC_SEQUENCE, CRC_MEMORY_OVERFLOW
| DESCRIPTION:      Allocate all ODTs in a DAQ list
******************************************************************************/
static vuint8 XcpAllocOdt( vuint8 daq, vuint8 odtCount )
{
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
  if ( (xcp.Daq.DaqCount==0) || (xcp.Daq.OdtEntryCount!=0) )
  {
    return (vuint8)CRC_SEQUENCE;
  }
  if( odtCount == 0 )
  {
    return (vuint8)CRC_OUT_OF_RANGE;
  }
  #endif

  

  xcp.Daq.u.DaqList[daq].firstOdt = xcp.Daq.OdtCount;
  xcp.Daq.OdtCount = (vuint16)(xcp.Daq.OdtCount+odtCount);
  xcp.Daq.u.DaqList[daq].lastOdt = (vuint16)(xcp.Daq.OdtCount-1);

  return XcpAllocMemory();
  
}

/*****************************************************************************
| NAME:             XcpAllocOdtEntry
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: daq :
|                   odt :
|                   odtEntryCount :
| RETURN VALUES:    return value of XcpAllocMemory
| DESCRIPTION:      Allocate all ODT entries
|                   Parameter odt is relative odt number
******************************************************************************/
static vuint8 XcpAllocOdtEntry( vuint8 daq, vuint8 odt, vuint8 odtEntryCount )
{
  vuint16 xcpFirstOdt;

  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
  if ( (xcp.Daq.DaqCount==0) || (xcp.Daq.OdtCount==0) )
  {
    return (vuint8)CRC_SEQUENCE;
  }
  if (odtEntryCount==0)
  {
    return (vuint8)CRC_OUT_OF_RANGE;
  }
  #endif

  /* Absolute ODT entry count count must fit in a WORD */
  if (xcp.Daq.OdtEntryCount > (0xFFFFu - odtEntryCount))
  {
    return (vuint8)CRC_MEMORY_OVERFLOW;
  }
  xcpFirstOdt = xcp.Daq.u.DaqList[daq].firstOdt;
  xcp.pOdt[xcpFirstOdt+odt].firstOdtEntry = xcp.Daq.OdtEntryCount;
  xcp.Daq.OdtEntryCount = (vuint16)(xcp.Daq.OdtEntryCount+odtEntryCount);
  xcp.pOdt[xcpFirstOdt+odt].lastOdtEntry = (vuint16)(xcp.Daq.OdtEntryCount-1);

  return XcpAllocMemory();
  
}

/*****************************************************************************
| NAME:             XcpStartDaq
| CALLED BY:        XcpCommand, XcpStartAllSelectedDaq
| PRECONDITIONS:    none
| INPUT PARAMETERS: daq :
| RETURN VALUES:    none
| DESCRIPTION:      Start DAQ
******************************************************************************/
static void XcpStartDaq( vuint8 daq )
{
  /* Initialize the DAQ list */
  DaqListFlags(daq) |= (vuint8)DAQ_FLAG_RUNNING;
  xcp.SessionStatus |= (SessionStatusType)SS_DAQ;
}

/*****************************************************************************
| NAME:             XcpStartAllSelectedDaq
| CALLED BY:        XcpCommand, XcpInit
| PRECONDITIONS:    none
| INPUT PARAMETERS: none
| RETURN VALUES:    none
| DESCRIPTION:      Start all selected DAQs
******************************************************************************/
static void XcpStartAllSelectedDaq(void)
{
  vuint8 daq;

  /* Start all selected DAQs */
  for (daq=0;daq<xcp.Daq.DaqCount;daq++)
  {
    if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_SELECTED) != 0 )
    {
      XcpStartDaq(daq);
      DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_SELECTED & 0x00FFu);
    }
  }
}

/*****************************************************************************
| NAME:             XcpStopDaq
| CALLED BY:        XcpCommand, XcpStopAllSelectedDaq
| PRECONDITIONS:    none
| INPUT PARAMETERS: daq : 
| RETURN VALUES:    none
| DESCRIPTION:      Stop DAQ
******************************************************************************/
static void XcpStopDaq( vuint8 daq )
{
  vuint8 i;

  DaqListFlags(daq) &= (vuint8)(DAQ_FLAG_DIRECTION|DAQ_FLAG_TIMESTAMP|DAQ_FLAG_NO_PID);

  /* Check if all DAQ lists are stopped */
  for (i=0;i<xcp.Daq.DaqCount;i++)
  {
    if ( (DaqListFlags(i) & (vuint8)DAQ_FLAG_RUNNING) != 0 )
    {
      return;
    }
  }

  xcp.SessionStatus &= (SessionStatusType)(~SS_DAQ & 0x00FFu);

  
  
}

/*****************************************************************************
| NAME:             XcpStopAllSelectedDaq
| CALLED BY:        XcpCommand
| PRECONDITIONS:    none
| INPUT PARAMETERS: none 
| RETURN VALUES:    none
| DESCRIPTION:      Stop all selected DAQs
******************************************************************************/
static void XcpStopAllSelectedDaq(void)
{
  vuint8 daq;

  for (daq=0;daq<xcp.Daq.DaqCount;daq++)
  {
    if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_SELECTED) != 0 )
    {
      XcpStopDaq(daq);
      DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_SELECTED & 0x00FFu);
    }
  }
}

/*****************************************************************************
| NAME:             XcpStopAllDaq
| CALLED BY:        XcpCommand, XcpDisconnect
| PRECONDITIONS:    none
| INPUT PARAMETERS: none 
| RETURN VALUES:    none
| DESCRIPTION:      Stop all DAQs
******************************************************************************/
static void XcpStopAllDaq( void )
{
  vuint8 daq;

  for (daq=0; daq<xcp.Daq.DaqCount; daq++)
  {
    DaqListFlags(daq) &= (vuint8)(DAQ_FLAG_DIRECTION|DAQ_FLAG_TIMESTAMP|DAQ_FLAG_NO_PID);
  }

  xcp.SessionStatus &= (SessionStatusType)(~SS_DAQ & 0x00FFu);

  
}


/****************************************************************************/
/* Data Aquisition Processor                                                */
/****************************************************************************/


/*****************************************************************************
| NAME:             XcpEvent,XcpEventExt
| CALLED BY:        application
| PRECONDITIONS:    The XCP is initialized and in connected state.
| INPUT PARAMETERS: event : event channel number to process
| 
| DESCRIPTION:      Handling of data acquisition channel.
******************************************************************************/


void XcpEvent(vuint8 event) {

  XcpEventExt(event, 0);
}

void XcpEventExt(vuint8 event, BYTEPTR offset)
{
  tXcpDto *dtop;
  BYTEPTR d;
  vuint16 e,el;
  vuint8  n;
  vuint8  daq;
  vuint16 odt;
  vuint8  i;

  if ( (xcp.SessionStatus & (SessionStatusType)SS_DAQ) == 0 ) return;

  for (daq=0; daq<xcp.Daq.DaqCount; daq++) {
      if ((DaqListFlags(daq) & (vuint8)DAQ_FLAG_RUNNING) == 0) continue;
      if ( DaqListEventChannel(daq) != event ) continue; 
       
      for (odt=DaqListFirstOdt(daq);odt<=DaqListLastOdt(daq);odt++)  {

        ApplXcpInterruptDisable(); /* The following code is not reentrant */

        dtop = &dto;
  
        /* ODT,DAQ */
        dtop->b[0] = (vuint8)(odt-DaqListFirstOdt(daq)); /* Relative odt number */
        dtop->b[1] = daq;
        i = 2;

        /* Use BIT7 of PID or ODT to indicate overruns */  
        if ( (DaqListFlags(daq) & (vuint8)DAQ_FLAG_OVERRUN) != 0 ) {
          dtop->b[0] |= (vuint8)0x80;
          DaqListFlags(daq) &= (vuint8)(~DAQ_FLAG_OVERRUN & 0xFFu);
        }
  
        /* Timestamp */
        if (odt==DaqListFirstOdt(daq))  {
            *(XcpDaqTimestampType*)&dtop->b[2] = ApplXcpGetTimestamp();
            i = 2 + kXcpDaqTimestampSize;
        }

        /* Copy data */
        /* This is the inner loop, optimize here */
        e = DaqListOdtFirstEntry(odt);
        if (OdtEntrySize(e)==0) goto next_odt; 
        el = DaqListOdtLastEntry(odt);
        d = (vuint8*)&dtop->b[i];
        while (e<=el) { // inner DAQ loop
          n = OdtEntrySize(e);
          if (n == 0) break;
          memcpy((DAQBYTEPTR)d, offset+(vuint32)OdtEntryAddr(e), n);
          d += n; 
          e++;
        }
        dtop->l = (vuint8)(d-(&dtop->b[0]) );
        XCP_ASSERT(dtop->l<=kXcpMaxDTO);

        XcpSendDto(&dto);
  
        next_odt:

        ApplXcpInterruptEnable();

      } /* odt */
  } /* daq */
  
}


/****************************************************************************/
/* Command Processor                                                        */
/****************************************************************************/

/*****************************************************************************
| NAME:             XcpDisconnect
| CALLED BY:        XcpCommand
| PRECONDITIONS:    XCP is initialized and in connected state.
| INPUT PARAMETERS: none 
| RETURN VALUES:    none
| DESCRIPTION:      If the XCP slave is connected to a XCP master a call of this
|                   function discontinues the connection (transition to disconnected state).
|                   If the XCP slave is not connected this function performs no action.
******************************************************************************/
void XcpDisconnect( void )
{
  xcp.SessionStatus &= (SessionStatusType)(~SS_CONNECTED & 0xFFu);
  XcpStopAllDaq();
}


/*****************************************************************************
| NAME:             XcpCommand
| CALLED BY:        XcpSendCallBack, XCP Transport Layer
| PRECONDITIONS:    none
| INPUT PARAMETERS: pCmd : data of received CTO message.
| RETURN VALUES:    none
| DESCRIPTION:      
******************************************************************************/

#if defined ( XCP_ENABLE_TESTMODE )
    static void XcpPrintCmd(const tXcpCto* pCmd);
    #define debugPrintCmd() if (gDebugLevel!=0) XcpPrintCmd(pCmd);
    static void XcpPrintRes(const tXcpCto* pCmd);
    #define debugPrintRes() if (gDebugLevel!=0) XcpPrintRes(pCmd);
#else
    #define debugPrintCmd()
#endif

void XcpCommand( const vuint32* pCommand )
{
  const tXcpCto* pCmd = (const tXcpCto*) pCommand; 
  vuint8 err;

  /* CONNECT */
  if (CRO_CMD==CC_CONNECT)
  {

    /* Prepare the default response */
    CRM_CMD = 0xFF; /* No Error */
    xcp.CrmLen = 1; /* Length = 1 */

#if defined ( XCP_ENABLE_TESTMODE )
    if ( gDebugLevel != 0) {
      ApplXcpPrint("\n-> CONNECT mode=%u\n",CRO_CONNECT_MODE);
    }
#endif
        
    /* Reset DAQ */
    XcpFreeDaq();
  
    /* Reset Session Status */
    xcp.SessionStatus = (SessionStatusType)SS_CONNECTED;

    xcp.CrmLen = CRM_CONNECT_LEN;

    /* Versions of the XCP Protocol Layer and Transport Layer Specifications. */
    CRM_CONNECT_TRANSPORT_VERSION = (vuint8)( (vuint16)XCP_TRANSPORT_LAYER_VERSION >> 8 );
    CRM_CONNECT_PROTOCOL_VERSION =  (vuint8)( (vuint16)XCP_VERSION >> 8 );

    CRM_CONNECT_MAX_CTO_SIZE = kXcpMaxCTO;
    CRM_CONNECT_MAX_DTO_SIZE_WRITE(kXcpMaxDTO); 
    CRM_CONNECT_RESOURCE = 0x00;                  /* Reset resource mask */
    CRM_CONNECT_RESOURCE |= (vuint8)RM_DAQ;       /* Data Acquisition */
    CRM_CONNECT_COMM_BASIC = 0;
    CRM_CONNECT_COMM_BASIC |= (vuint8)CMB_OPTIONAL;
#if defined ( XCP_CPUTYPE_BIGENDIAN )
    CRM_CONNECT_COMM_BASIC |= (vuint8)PI_MOTOROLA;
#endif

#if defined ( XCP_ENABLE_TESTMODE )
    ApplXcpPrint("<- 0xFF version=%02Xh/%02Xh, maxcro=%02Xh, maxdto=%02Xh, resource=%02X, mode=%02X\n",
        CRM_CONNECT_PROTOCOL_VERSION,
        CRM_CONNECT_TRANSPORT_VERSION,
        CRM_CONNECT_MAX_CTO_SIZE,
        CRM_CONNECT_MAX_DTO_SIZE,
        CRM_CONNECT_RESOURCE,
        CRM_CONNECT_COMM_BASIC);
#endif

    goto positive_response; 

  }

  /* Handle other commands only if connected */
  else /* CC_CONNECT */
  {
    if ( (xcp.SessionStatus & (SessionStatusType)SS_CONNECTED) != 0 )
    {
      /* Ignore commands if the previous command sequence has not been completed */

      #if defined ( XCP_ENABLE_GET_SESSION_STATUS_API )
        xcp.SessionStatus |= (SessionStatusType)SS_POLLING;
      #endif

      /* Prepare the default response */
      CRM_CMD = 0xFF; /* No Error */
      xcp.CrmLen = 1; /* Length = 1 */

      debugPrintCmd();

      switch (CRO_CMD)
      {

        case CC_SYNC:
          {
            /* Always return a negative response with the error code ERR_CMD_SYNCH. */
            xcp.CrmLen = CRM_SYNCH_LEN;
            CRM_CMD    = PID_ERR;
            CRM_ERR    = CRC_CMD_SYNCH;
           }
          break;

        case CC_GET_COMM_MODE_INFO:
          {
            xcp.CrmLen = CRM_GET_COMM_MODE_INFO_LEN;
            /* Transmit the version of the XCP Protocol Layer implementation.    */
            /* The higher nibble is the main version, the lower the sub version. */
            /* The lower nibble overflows, if the sub version is greater than 15.*/
            CRM_GET_COMM_MODE_INFO_DRIVER_VERSION = (vuint8)( ((XCP_VERSION & 0x0F00) >> 4) | (XCP_VERSION & 0x000F) );
            CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL = 0;
            CRM_GET_COMM_MODE_INFO_QUEUE_SIZE = 0;
            CRM_GET_COMM_MODE_INFO_MAX_BS = 0;
            CRM_GET_COMM_MODE_INFO_MIN_ST = 0;
          }
          break;

          case CC_DISCONNECT:
            {
              xcp.CrmLen = CRM_DISCONNECT_LEN;
              XcpDisconnect();
            }
            break;
                       
          case CC_GET_ID:
            {
              xcp.CrmLen = CRM_GET_ID_LEN;
              CRM_GET_ID_MODE = 0;
              CRM_GET_ID_LENGTH_WRITE(0x00); 
              if ( CRO_GET_ID_TYPE == IDT_ASAM_NAME )   /* Type = ASAM MC2 */
              {
                CRM_GET_ID_LENGTH_WRITE((vuint32)kXcpStationIdLength); 
                XcpSetMta( ApplXcpGetPointer(0xFF, (vuint32)(&kXcpStationId[0])), 0xFF); 
              }    
            }
            break;                    

          case CC_GET_STATUS: 
            {
              xcp.CrmLen = CRM_GET_STATUS_LEN;
              CRM_GET_STATUS_STATUS = (vuint8)xcp.SessionStatus;
              CRM_GET_STATUS_PROTECTION = 0;
              CRM_GET_STATUS_CONFIG_ID_WRITE(0x00); /* Session configuration ID not available. */
            }
            break;

          case CC_SET_MTA:
            {
              XcpSetMta(ApplXcpGetPointer(CRO_SET_MTA_EXT,CRO_SET_MTA_ADDR),CRO_SET_MTA_EXT);
            }
            break;

          case CC_DOWNLOAD: 
            {
              vuint8 size;
              size = CRO_DOWNLOAD_SIZE;
              if (size>CRO_DOWNLOAD_MAX_SIZE)
              {
                error(CRC_OUT_OF_RANGE)
              }

              err = XcpWriteMta(size,CRO_DOWNLOAD_DATA);
              if (err==(vuint8)XCP_CMD_PENDING) 
              {
                goto no_response;
              }
              if (err==(vuint8)XCP_CMD_DENIED)
              {
                error(CRC_WRITE_PROTECTED) 
              }
              if (err==(vuint8)XCP_CMD_SYNTAX)
              {
                error(CRC_CMD_SYNTAX) 
              }                
            }
            break;        
          
          case CC_DOWNLOAD_MAX:
            {
              err = XcpWriteMta(CRO_DOWNLOAD_MAX_MAX_SIZE,CRO_DOWNLOAD_MAX_DATA);
              if (err==(vuint8)XCP_CMD_PENDING)
              {
                return;
              }
              if (err==(vuint8)XCP_CMD_DENIED)
              {
                error(CRC_WRITE_PROTECTED) 
              }
              if (err==(vuint8)XCP_CMD_SYNTAX)
              {
                error(CRC_CMD_SYNTAX) 
              }
            }
            break;

          case CC_UPLOAD:
            {
              vuint8 size = CRO_UPLOAD_SIZE;
              if (size > (vuint8)CRM_UPLOAD_MAX_SIZE) error(CRC_OUT_OF_RANGE);
              err = XcpReadMta(size,CRM_UPLOAD_DATA);
              xcp.CrmLen = (vuint8)(CRM_UPLOAD_LEN+size);
              if (err==(vuint8)XCP_CMD_PENDING) goto no_response; 
              if (err == (vuint8)XCP_CMD_DENIED) error(CRC_ACCESS_DENIED);
            }
            break;

          case CC_SHORT_UPLOAD:
            {

#if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (CRO_SHORT_UPLOAD_SIZE > (vuint8)CRM_SHORT_UPLOAD_MAX_SIZE) error(CRC_OUT_OF_RANGE);
#endif
              XcpSetMta(ApplXcpGetPointer(CRO_SHORT_UPLOAD_EXT,CRO_SHORT_UPLOAD_ADDR),CRO_SHORT_UPLOAD_EXT);
              err = XcpReadMta(CRO_SHORT_UPLOAD_SIZE,CRM_SHORT_UPLOAD_DATA);
              xcp.CrmLen = (vuint8)(CRM_SHORT_UPLOAD_LEN+CRO_SHORT_UPLOAD_SIZE);
              if (err==(vuint8)XCP_CMD_PENDING) goto no_response; 
              if (err == (vuint8)XCP_CMD_DENIED) error(CRC_ACCESS_DENIED);
            }
            break;

            case CC_GET_DAQ_PROCESSOR_INFO: 
            {
    
              xcp.CrmLen = CRM_GET_DAQ_PROCESSOR_INFO_LEN;
              CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ = 0;          
              CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ_WRITE(xcp.Daq.DaqCount); /* dynamic or static */ 
              CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT_WRITE(0x00); /* Unknown */    
              CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE = (vuint8)DAQ_HDR_ODT_DAQB; /* DTO identification field type: Relative ODT number, absolute list number (BYTE) */
              CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES = (vuint8)( DAQ_PROPERTY_CONFIG_TYPE | DAQ_PROPERTY_TIMESTAMP | DAQ_OVERLOAD_INDICATION_PID );
            }
            break;

            case CC_GET_DAQ_RESOLUTION_INFO: 
              {
                xcp.CrmLen = CRM_GET_DAQ_RESOLUTION_INFO_LEN;
                CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_DAQ = 1;
                CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_STIM = 1;
                CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_DAQ  = (vuint8)XCP_MAX_ODT_ENTRY_SIZE;
                CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_STIM = (vuint8)XCP_MAX_ODT_ENTRY_SIZE;
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE = kXcpDaqTimestampUnit | (vuint8)kXcpDaqTimestampSize | DAQ_TIMESTAMP_FIXED;
                CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS_WRITE(kXcpDaqTimestampTicksPerUnit);  /* BCD coded */ 
              }
              break;
  
            case CC_FREE_DAQ: 
            {
                XcpFreeDaq();
            }
            break;

          case CC_ALLOC_DAQ:
            {
                vuint8 count = (vuint8)CRO_ALLOC_DAQ_COUNT;
                check_error(XcpAllocDaq(count));
            }
            break;

          case CC_ALLOC_ODT:
            {
              vuint8 daq = (vuint8)CRO_ALLOC_ODT_DAQ;
              vuint8 count = CRO_ALLOC_ODT_COUNT;
   #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (daq >= xcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
  #endif
              check_error( XcpAllocOdt(daq, count) ) 
            }
            break;

          case CC_ALLOC_ODT_ENTRY:
            {
              vuint8 daq = (vuint8)CRO_ALLOC_ODT_ENTRY_DAQ;
              vuint8 odt = CRO_ALLOC_ODT_ENTRY_ODT;
              vuint8 count = CRO_ALLOC_ODT_ENTRY_COUNT;
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if ((daq >= xcp.Daq.DaqCount) || (odt >= (vuint8)DaqListOdtCount(daq))) error(CRC_OUT_OF_RANGE);
  #endif
              check_error( XcpAllocOdtEntry(daq, odt, count) ) 
            }
            break;

          case CC_GET_DAQ_LIST_MODE:
            {
              vuint8 daq = (vuint8)CRO_GET_DAQ_LIST_MODE_DAQ;
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (daq >= xcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
  #endif
              xcp.CrmLen = CRM_GET_DAQ_LIST_MODE_LEN;
              CRM_GET_DAQ_LIST_MODE_MODE = DaqListFlags(daq);
              CRM_GET_DAQ_LIST_MODE_PRESCALER = 1;
              CRM_GET_DAQ_LIST_MODE_EVENTCHANNEL_WRITE(DaqListEventChannel(daq));
              CRM_GET_DAQ_LIST_MODE_PRIORITY = 0;  /* DAQ-list prioritization is not supported. */
            }
            break;

          case CC_SET_DAQ_LIST_MODE:
            {
              vuint8 daq = (vuint8)CRO_SET_DAQ_LIST_MODE_DAQ;
              vuint8 event = (vuint8)(CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL&0xFFu);
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (daq >= xcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE);
              if (CRO_SET_DAQ_LIST_MODE_PRIORITY != 0) error(CRC_OUT_OF_RANGE);  /* Priorization is not supported */
  #endif
              DaqListEventChannel(daq) = event;
              DaqListFlags(daq) = CRO_SET_DAQ_LIST_MODE_MODE;
              break;
            }

          case CC_SET_DAQ_PTR:
            {
              vuint8 daq = (vuint8) (CRO_SET_DAQ_PTR_DAQ&0xFFu);
              vuint8 odt = CRO_SET_DAQ_PTR_ODT;
              vuint8 idx = CRO_SET_DAQ_PTR_IDX;
              vuint16 odt0 = (vuint16)(DaqListFirstOdt(daq)+odt); /* Absolute odt number */
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if ((daq >= xcp.Daq.DaqCount) || (odt >= (vuint8)DaqListOdtCount(daq)) || (idx >= (vuint8)DaqListOdtEntryCount(odt0))) error(CRC_OUT_OF_RANGE); 
  #endif
              xcp.CrmLen = CRM_SET_DAQ_PTR_LEN;
              xcp.DaqListPtr = (vuint16)(DaqListOdtFirstEntry(odt0)+idx);
            }
            break;

          case CC_WRITE_DAQ: /* Write DAQ entry */
          {
              DAQBYTEPTR addr;
#if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (((vuint8)CRO_WRITE_DAQ_SIZE == (vuint8)0u) || (CRO_WRITE_DAQ_SIZE > (vuint8)XCP_MAX_ODT_ENTRY_SIZE)) error(CRC_OUT_OF_RANGE);
              if ((0u == xcp.Daq.DaqCount) || (0u == xcp.Daq.OdtCount) || (0u == xcp.Daq.OdtEntryCount)) error(CRC_DAQ_CONDIF);
#endif
              addr = (DAQBYTEPTR)ApplXcpGetPointer(CRO_WRITE_DAQ_EXT, CRO_WRITE_DAQ_ADDR);
              OdtEntrySize(xcp.DaqListPtr) = CRO_WRITE_DAQ_SIZE;
              OdtEntryAddr(xcp.DaqListPtr) = addr;
              xcp.DaqListPtr++; /* Autoincrement */
          }
          break;

          case CC_WRITE_DAQ_MULTIPLE: /* Write multiple DAQ entries */
          {
              DAQBYTEPTR addr;
             for (int i = 0; i < CRO_WRITE_DAQ_MULTIPLE_NODAQ; i++) {
#if defined ( XCP_ENABLE_PARAMETER_CHECK )
                  if (((vuint8)CRO_WRITE_DAQ_MULTIPLE_SIZE(i) == (vuint8)0u) || (CRO_WRITE_DAQ_MULTIPLE_SIZE(i) > (vuint8)XCP_MAX_ODT_ENTRY_SIZE)) error(CRC_OUT_OF_RANGE);
                  if (CRO_WRITE_DAQ_MULTIPLE_BITOFFSET(i)!=0xFF) error(CRC_OUT_OF_RANGE);
                  if ((0u == xcp.Daq.DaqCount) || (0u == xcp.Daq.OdtCount) || (0u == xcp.Daq.OdtEntryCount)) error(CRC_DAQ_CONDIF);
#endif
                  addr = (DAQBYTEPTR)ApplXcpGetPointer(CRO_WRITE_DAQ_MULTIPLE_EXT(i), CRO_WRITE_DAQ_MULTIPLE_ADDR(i));
                  OdtEntrySize(xcp.DaqListPtr) = CRO_WRITE_DAQ_MULTIPLE_SIZE(i);
                  OdtEntryAddr(xcp.DaqListPtr) = addr;
                  xcp.DaqListPtr++; /* Autoincrement */
              }
          }
          break;

          case CC_START_STOP_DAQ_LIST:
            {
              vuint8 daq = (vuint8)(CRO_START_STOP_DAQ&0xFFu);
  #if defined ( XCP_ENABLE_PARAMETER_CHECK )
              if (daq >= xcp.Daq.DaqCount) error(CRC_OUT_OF_RANGE); 
  #endif

              if ( (CRO_START_STOP_MODE==1 ) || (CRO_START_STOP_MODE==2) )
              {
                DaqListFlags(daq) |= (vuint8)DAQ_FLAG_SELECTED;
                if ( CRO_START_STOP_MODE == (vuint8)1u ) XcpStartDaq(daq);
                xcp.CrmLen = CRM_START_STOP_LEN;
                CRM_START_STOP_FIRST_PID = 0; // Absolute DAQ, Relative ODT - DaqListFirstPid(daq);
              } 
              else
              {
                XcpStopDaq(daq);
              }

            }
            break;

          case CC_START_STOP_SYNCH:
            {
              if( (0 == xcp.Daq.DaqCount) || (0 == xcp.Daq.OdtCount) || (0 == xcp.Daq.OdtEntryCount) ) error(CRC_DAQ_CONDIF) 
              if (CRO_START_STOP_MODE==2) /* stop selected */
              {
                XcpStopAllSelectedDaq();
              } 
              else
              {
                if (CRO_START_STOP_MODE==1) /* start selected */
                {
                  XcpStartAllSelectedDaq();
                }
                else /* CRO_START_STOP_MODE==0 : stop all */
                {
                  
                  XcpStopAllDaq();
                }
              }
  
            }
            break;

  
          case CC_GET_DAQ_CLOCK:
            {
              xcp.CrmLen = CRM_GET_DAQ_CLOCK_LEN;
              CRM_GET_DAQ_CLOCK_TIME_WRITE((vuint32)ApplXcpGetTimestamp()); 
            }
            break;
               
          default: /* unknown */
            {

  #if defined ( XCP_ENABLE_TESTMODE )
              if ( gDebugLevel != 0) {
                ApplXcpPrint("-> UNKNOWN COMMAND %02X\n", CRO_CMD);
              }
  #endif
              error(CRC_CMD_UNKNOWN) 
            }

      } /* switch */

      goto positive_response; 
    }

    /* Not connected */
    else
    {
      goto no_response; 
    }
  } /* CC_CONNECT */


negative_response:
  xcp.CrmLen = 2;
  CRM_CMD = (vuint8)PID_ERR;
  CRM_ERR = (vuint8)err;
 
positive_response:
#if defined ( XCP_ENABLE_TESTMODE )
  debugPrintRes();
#endif
  XcpSendCrm();

no_response:
   return;
  
}



/*****************************************************************************
| NAME:             XcpInit
| CALLED BY:        application
| PRECONDITIONS:    the data link layer has to be initialized.
| INPUT PARAMETERS: none
| RETURN VALUES:    none
| DESCRIPTION:      Initialization of the XCP Protocol Layer
|                   Application specific initialization
|                    ( e.g. Vector XCP on CAN Transport Layer )
******************************************************************************/
void XcpInit( void )
{
  /* Initialize all XCP variables to zero */
  memset((BYTEPTR)&xcp,0,(vuint16)sizeof(xcp)); 
   
  /* Initialize the session status */
  xcp.SessionStatus = (SessionStatusType)0u;

}


/****************************************************************************/
/* Test                                                                     */
/****************************************************************************/

#if defined ( XCP_ENABLE_TESTMODE )

static void XcpPrintCmd(const tXcpCto * pCmd) {

    switch (CRO_CMD) {

    case CC_SYNC:
            ApplXcpPrint("-> SYNC\n");
            break;
            
    case CC_GET_COMM_MODE_INFO:
            ApplXcpPrint("-> GET_COMM_MODE_INFO\n");
            break;

    case CC_DISCONNECT:
            ApplXcpPrint("-> DISCONNECT\n");
            break;

    case CC_GET_ID:
            ApplXcpPrint("-> GET_ID type=%u\n", CRO_GET_ID_TYPE);
            break;

    case CC_GET_STATUS:
            ApplXcpPrint("-> GET_STATUS\n");
            break;

    case CC_SET_MTA:
            ApplXcpPrint("-> SET_MTA addr=%08Xh, addrext=%02Xh\n", CRO_SET_MTA_ADDR, CRO_SET_MTA_EXT);
            break;

     case CC_DOWNLOAD:
            if ((CRO_CMD != CC_DOWNLOAD_NEXT)) {
                vuint16 i;
                ApplXcpPrint("-> DOWNLOAD size=%u, data=", CRO_DOWNLOAD_SIZE);
                for (i = 0; (i < CRO_DOWNLOAD_SIZE) && (i < CRO_DOWNLOAD_MAX_SIZE); i++)
                {
                    ApplXcpPrint("%02X ", CRO_DOWNLOAD_DATA[i]);
                }
                ApplXcpPrint("\n");
            }
            break;

     case CC_DOWNLOAD_MAX:
            ApplXcpPrint("DOWNLOAD_MAX data=");
            for (int i = 0; i < CRO_DOWNLOAD_MAX_MAX_SIZE; i++) {
                ApplXcpPrint("%02X ", CRO_DOWNLOAD_MAX_DATA[i]);
            }
            ApplXcpPrint("\n");
            break;

     case CC_UPLOAD:
            ApplXcpPrint("-> UPLOAD size=%u\n", CRO_UPLOAD_SIZE);
            break;

     case CC_SHORT_UPLOAD:
            ApplXcpPrint("-> SHORT_UPLOAD addr=%08Xh, addrext=%02Xh, size=%u\n", CRO_SHORT_UPLOAD_ADDR, CRO_SHORT_UPLOAD_EXT, CRO_SHORT_UPLOAD_SIZE);
            break;

     case CC_GET_DAQ_PROCESSOR_INFO:
            ApplXcpPrint("-> GET_DAQ_PROCESSOR_INFO\n");
            break;

     case CC_GET_DAQ_RESOLUTION_INFO:
            ApplXcpPrint("-> GET_DAQ_RESOLUTION_INFO\n");
            break;

     case CC_FREE_DAQ:
            ApplXcpPrint("-> FREE_DAQ\n");
            break;

     case CC_ALLOC_DAQ:
            ApplXcpPrint("-> ALLOC_DAQ count=%u\n", CRO_ALLOC_DAQ_COUNT);
            break;

     case CC_ALLOC_ODT:
            ApplXcpPrint("-> ALLOC_ODT daq=%u, count=%u\n", CRO_ALLOC_ODT_DAQ, CRO_ALLOC_ODT_COUNT);
            break;

     case CC_ALLOC_ODT_ENTRY:
            ApplXcpPrint("-> ALLOC_ODT_ENTRY daq=%u, odt=%u, count=%u\n", CRO_ALLOC_ODT_ENTRY_DAQ, CRO_ALLOC_ODT_ENTRY_ODT, CRO_ALLOC_ODT_ENTRY_COUNT);
            break;

     case CC_GET_DAQ_LIST_MODE:
            ApplXcpPrint("-> GET_DAQ_LIST_MODE daq=%u\n",CRO_GET_DAQ_LIST_MODE_DAQ );
            break;

     case CC_SET_DAQ_LIST_MODE:
            ApplXcpPrint("-> SET_DAQ_LIST_MODE daq=%u, mode=%02Xh, eventchannel=%u\n",CRO_SET_DAQ_LIST_MODE_DAQ, CRO_SET_DAQ_LIST_MODE_MODE, CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL);
            break;
           
     case CC_SET_DAQ_PTR:
            ApplXcpPrint("-> SET_DAQ_PTR daq=%u,odt=%u,idx=%u\n", CRO_SET_DAQ_PTR_DAQ, CRO_SET_DAQ_PTR_ODT, CRO_SET_DAQ_PTR_IDX);
            break;

     case CC_WRITE_DAQ: /* Write DAQ entry */
            ApplXcpPrint("-> WRITE_DAQ size=%u,addr=%08Xh,%02Xh\n", CRO_WRITE_DAQ_SIZE, CRO_WRITE_DAQ_ADDR, CRO_WRITE_DAQ_EXT);
            break;

     case CC_WRITE_DAQ_MULTIPLE: /* Write multiple DAQ entries */
         ApplXcpPrint("-> WRITE_MULTIPLE_DAQ count=%u\n", CRO_WRITE_DAQ_MULTIPLE_NODAQ);
         for (int i = 0; i < CRO_WRITE_DAQ_MULTIPLE_NODAQ; i++) {
                ApplXcpPrint("   %u: size=%u,addr=%08Xh,%02Xh\n", i, CRO_WRITE_DAQ_MULTIPLE_SIZE(i), CRO_WRITE_DAQ_MULTIPLE_ADDR(i), CRO_WRITE_DAQ_MULTIPLE_EXT(i));
            }
            break;

     case CC_START_STOP_DAQ_LIST:
            ApplXcpPrint("-> START_STOP mode=%02Xh, daq=%u\n", CRO_START_STOP_MODE, CRO_START_STOP_DAQ);
            break;

     case CC_START_STOP_SYNCH:
            ApplXcpPrint("-> CC_START_STOP_SYNCH mode=%02Xh\n", CRO_START_STOP_MODE);
            break;

     case CC_GET_DAQ_CLOCK:
            ApplXcpPrint("-> GET_DAQ_CLOCK\n");
            break;

     default: /* unknown */
            ApplXcpPrint("-> UNKNOWN COMMAND %02X\n", CRO_CMD);
         
    } /* switch */
}

static void XcpPrintRes(const tXcpCto* pCmd) {

    if (CRM_CMD == PID_ERR) {
        char* e;
        switch (CRM_ERR) {
                case  CRC_CMD_SYNCH: e = "CRC_CMD_SYNCH"; break;
                case  CRC_CMD_BUSY: e = "CRC_CMD_BUSY"; break;
                case  CRC_DAQ_ACTIVE: e = "CRC_DAQ_ACTIVE"; break;
                case  CRC_PRM_ACTIVE: e = "CRC_PRM_ACTIVE"; break;
                case  CRC_CMD_UNKNOWN: e = "CRC_CMD_UNKNOWN"; break;
                case  CRC_CMD_SYNTAX: e = "CRC_CMD_SYNTAX"; break;
                case  CRC_OUT_OF_RANGE: e = "CRC_OUT_OF_RANGE"; break;
                case  CRC_WRITE_PROTECTED: e = "CRC_WRITE_PROTECTED"; break;
                case  CRC_ACCESS_DENIED: e = "CRC_ACCESS_DENIED"; break;
                case  CRC_ACCESS_LOCKED: e = "CRC_ACCESS_LOCKED"; break;
                case  CRC_PAGE_NOT_VALID: e = "CRC_PAGE_NOT_VALID"; break;
                case  CRC_PAGE_MODE_NOT_VALID: e = "CRC_PAGE_MODE_NOT_VALID"; break;
                case  CRC_SEGMENT_NOT_VALID: e = "CRC_SEGMENT_NOT_VALID"; break;
                case  CRC_SEQUENCE: e = "CRC_SEQUENCE"; break;
                case  CRC_DAQ_CONDIF: e = "CRC_DAQ_CONDIF"; break;
                case  CRC_MEMORY_OVERFLOW: e = "CRC_MEMORY_OVERFLOW"; break;
                case  CRC_GENERIC: e = "CRC_GENERIC"; break;
                case  CRC_VERIFY: e = "CRC_VERIFY"; break;
                default: e = "Unknown errorcode";
        }
        ApplXcpPrint("<- 0xFE error %02Xh - %s\n", CRM_ERR, e );
    }
    else {
        switch (CRO_CMD) {

        case CC_GET_STATUS:
            ApplXcpPrint("<- 0xFF sessionstatus=%02Xh, protectionstatus=%02X\n", CRM_GET_STATUS_STATUS, CRM_GET_STATUS_PROTECTION);
            break;

        case CC_GET_ID:
            ApplXcpPrint("<- 0xFF mode=%u,len=%u\n", CRM_GET_ID_MODE, CRM_GET_ID_LENGTH);
            break;

        case CC_UPLOAD:
            ApplXcpPrint("<- 0xFF data=");
            for (int i = 0; i < CRO_UPLOAD_SIZE; i++) {
                ApplXcpPrint("%02X ", CRM_UPLOAD_DATA[i]);
            }
            ApplXcpPrint("\n");
            break;

        case CC_SHORT_UPLOAD:
            ApplXcpPrint("<- 0xFF data=");
            for (int i = 0; i < (vuint16)CRO_SHORT_UPLOAD_SIZE; i++) {
                ApplXcpPrint("%02X ", CRM_SHORT_UPLOAD_DATA[i]);
            }
            ApplXcpPrint("\n");
            break;

        case CC_GET_DAQ_RESOLUTION_INFO:
            ApplXcpPrint("<- 0xFF , mode=%02Xh, , ticks=%02Xh\n", CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE, CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS);
            break;

        default:
            ApplXcpPrint("<- 0xFF\n");
            break;


        } /* switch */
    }
}


void XcpPrintDaqList( vuint8 daq )
{
  int i;
  vuint16 e;
    
  if (daq>=xcp.Daq.DaqCount) return;

  ApplXcpPrint("DAQ %u:\n",daq);
  ApplXcpPrint(" eventchannel=%04Xh,",DaqListEventChannel(daq));
  ApplXcpPrint(" firstOdt=%u,",DaqListFirstOdt(daq));
  ApplXcpPrint(" lastOdt=%u,",DaqListLastOdt(daq));
  ApplXcpPrint(" flags=%02Xh\n",DaqListFlags(daq));
  ApplXcpPrint(" firstPid=%02Xh\n",DaqListFirstPid(daq)); 
  for (i=DaqListFirstOdt(daq);i<=DaqListLastOdt(daq);i++)
  {
    ApplXcpPrint("  ODT %u (%u):\n",i-DaqListFirstOdt(daq),i);
    ApplXcpPrint("   pid=%u:\n",i);
    ApplXcpPrint("   firstOdtEntry=%u,lastOdtEntry=%u:\n",DaqListOdtFirstEntry(i),DaqListOdtLastEntry(i));
    for (e=DaqListOdtFirstEntry(i);e<=DaqListOdtLastEntry(i);e++)
    {
      ApplXcpPrint("   [%08Xh,%u]\n",OdtEntryAddr(e),OdtEntrySize(e));
    }
  } /* j */
} 

  
#endif /* XCP_ENABLE_TESTMODE */


















