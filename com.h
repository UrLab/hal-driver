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

HALConnection *HALConn_open(const char *path, const char *sock_path);

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
int HALConn_run_reader(HALConnection *conn, const char **trigger_names, size_t n_triggers);

void HALConn_stop_reader(HALConnection *conn);

int HALConn_is_running(HALConnection *conn);

int HALConn_uptime(HALConnection *conn);

size_t HALConn_rx_bytes(HALConnection *conn);

size_t HALConn_tx_bytes(HALConnection *conn);

const char *HALConn_sock_path(HALConnection *conn);

#endif
