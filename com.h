#ifndef DEFINE_COM_HEADER
#define DEFINE_COM_HEADER

#include "HALMsg.h"

/*!
 *  Connection to the arduino. Manage requests and responses,
 */
typedef struct HALConnection HALConnection;

typedef enum HALErr {
    OK        = 0, //!< No error
    TIMEOUT   = 1, //!< Command timeouted
    SEQERR    = 2, //!< No more seq no avialableat the moment
    LOCKERR   = 3, //!< Cannot acquire lock on connection
    CHKERR    = 4, //!< Checksum error

    READERR   = 5, //!< Cannot read
    WRITEERR  = 6, //!< Cannot write
    OUTOFSYNC = 7, //!< Encountered unexpected SYNC byte; must resync

    UNKNERR   = 8  //!< Unknown error
} HALErr;

typedef enum HALLogLvl {
    SILENT  = 0, //!< No logging at all
    ERROR   = 1, //!< Errors only
    WARNING = 2, //!< Errors and warnings
    INFO    = 3, //!< Errors, warnings and informations
    DEBUG   = 4, //!< Log everything
    DUMP    = 5  //!< Log everything and dump HAL serial traffic 
} HALLogLvl;

const char *HALErr_desc(HALErr err);

HALConnection *HALConn_open(const char *path);

#define HAL_ERROR(conn,fmt,...) HALConn_log(conn, ERROR, fmt, ##__VA_ARGS__)
#define HAL_WARN(conn,fmt,...) HALConn_log(conn, WARNING, fmt, ##__VA_ARGS__)
#define HAL_INFO(conn,fmt,...) HALConn_log(conn, INFO, fmt, ##__VA_ARGS__)
#define HAL_DEBUG(conn,fmt,...) HALConn_log(conn, DEBUG, fmt, ##__VA_ARGS__)
void HALConn_log(HALConnection *conn, HALLogLvl lvl, const char *fmt, ...);

void HALConn_dump(HALConnection *conn, const HALMsg *msg, const char *prefix);

/*!
 *  Set the loglevel
 */
void HALConn_loglevel(HALConnection *conn, HALLogLvl lvl);


/*!
 *  Close connection to Arduino
 */
void HALConn_close(HALConnection *conn);

/*!
 *  Read a message from Arduino
 *  @param conn The HAL connection to use
 *  @param msg Message received
 */
HALErr HALConn_read_message(HALConnection *conn, HALMsg *msg);


HALErr HALConn_write_message(HALConnection *conn, const HALMsg *msg);

/*!
 *  Send request to HAL and wait for response
 *  @param conn The HAL connection to use
 *  @param msg [in+out] Message to send. Contains the response if return value
                        is OK
 *  @return One of HALErr
 */
HALErr HALConn_request(HALConnection *conn, HALMsg *msg);

/*!
 *  Start the read thread, and call reader for each correctly received message
 *  @param conn The HAL connection to use
 *  @param reader Function to call on each correctly received message
 */
int HALConn_run_reader(HALConnection *conn);

#endif
