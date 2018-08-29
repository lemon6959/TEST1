#ifndef __SUPERVISOR_TEST_H__
#define __SUPERVISOR_TEST_H__

#include "ethernet.h"

typedef enum
{
    SPVT_CMD_REQ_RPT_ID = 1,
    SPVT_CMDR_REQ_RPT_ID = 2,
    SPVT_CMD_WARN = 3,
    SPVT_CMD_CLEAR_WARN = 4
}spvtCmd_e;


#ifndef RPT_ID_T
#define RPT_ID_T
typedef struct rptID_s
{
    uint8_t     m_ucRoomID;
    uint8_t     m_ucTestBenchID;
}rptID_t;
#endif

typedef struct spvtMsg_noBody_s
{
    uint32_t        m_ulMsgID;
}spvtMsg_noBody_t;

typedef struct spvtMsg_rptID_s
{
    uint32_t        m_ulMsgID;
    
    rptID_t         m_xRptID;
}spvtMsg_rptID_t;

typedef struct spvtMsg_warn_s
{
    uint32_t        m_ulMsgID;
    
    rptID_t         m_xRptID;
    uint8_t         m_ucWarnCode;
    
    uint8_t         m_ucBufLen;
    uint8_t         m_aucBuf[];
}spvtMsg_warn_t;

extern const TCPConnCbk_t m_xTCPConnCbk_Spvt;

extern void vTask_SupervisorTest( void *pvParameters );

extern TCPOper_e xSPVT_TCPCbk_EstbConn(struct uip_conn *pxCurConn, void** ppvAppInst);
extern TCPOper_e xSPVT_TCPCbk_RcvMsg(uint8_t* pucMsgBuf, unsigned int uiMsgLen, void* pvAppInst);
extern TCPOper_e xSPVT_TCPCbk_LostConn(TCPCbkErrCode_e ucErrCode, void* pvAppInst);
extern TCPOper_e xSPVT_TCPCbk_Poll(void* pvAppInst);


#endif  // __SUPERVISOR_TEST_H__
