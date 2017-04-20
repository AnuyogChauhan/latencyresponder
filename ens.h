/**
 * @file ens.h ENS Workload Runtime Library C/C++ API
 *
 * Project Edge
 * Copyright (C) 2016-17  Deutsche Telekom Capital Partners Strategic Advisory LLC
 *
 */

#ifndef _ENS_H__
#define _ENS_H__

#include <stdint.h>

/**
 * @page c-workload-runtime C/C++ Workload Runtime
 *
 * The ens.h header defines the C/C++ interface to the ENS workload runtime.  The interface
 * provides the workload with APIs for
 *
 * -   receiving session lifecycle and data transfer events from clients and/or
 *     other workloads via a dynamically linked event function (@ref ENSEventFn)
 * -   establishing sessions with other workloads (@ref ENSSessionStart)
 * -   transfering data to clients and other workloads (@ref ENSSessionRequest, @ref ENSSessionNotify,
 *     @ref ENSSessionAlloc and @ref ENSSessionFree)
 * -   managing the lifecycle of sessions (@ref ENSSessionEnd).
 *
 * Following sections provide more details on the API.
 *
 * Sessions and Session Lifecycle
 * ------------------------------
 *
 * Sessions are established between client and workloads in an application for the
 * purpose of exchanging data with low latency.  Establishing a session allows communication
 * channels and data structures to be put in place upfront and reused for multiple
 * data exchanges in order to achieve ultra low latency data transfers and processing
 * of requests.
 *
 * From the perspective of a workload, sessions may be incoming or outgoing.  An incoming
 * session is started by the runtime calling the @ref ENSEventFn "event function" associated with the
 * relevant event interface with event_type set to @ref EVENT_SESSION_START.  An outgoing
 * session is started by the workload calling @ref ENSSessionStart specifying the target
 * event interface for the session.
 *
 * Sessions are identified by a unique session identifier.  This identifier has
 * local significance - the workload or client at the other end of the session does
 * not see the same session identifier.  If the application requires exchange of a shared
 * session identification then it should exchange this using a data transfer API after
 * the session is established.  The ENS session identifier is used to identify the
 * session in usage reporting.
 *
 * Sessions are terminated when either end of the session calls @ref ENSSessionEnd (or the
 * equivalent for the appropriate workload or client runtime), or if the underlying
 * communication channel fails.  When a session is ended, the workload @ref ENSEventFn "event function" is
 * called with event_type set to @ref EVENT_SESSION_END.  If the communication channel
 * fails, the workload @ref ENSEventFn "event function" is called with event_type set to @ref EVENT_SESSION_DISCONNECT.
 *
 * Data Transfer
 * -------------
 *
 * The API provides two mechanisms for data transfer - two-way Request/Response
 * transactions, or one-way Notify transactions.
 *
 * Request/Response transactions are initiated with the @ref ENSSessionRequest API (or
 * the equivalent in a different workload runtime or client runtime) and result in
 * a call to the @ref ENSEventFn "event function" with event_type set to @ref EVENT_REQUEST.
 *
 * -  The workload initiating the request includes request data in the buffer referenced by
 *    the @ref ENSUserData "userdata" parameter.  The buffer must be allocated using the
 *    @ref ENSSessionAlloc API.  The @ref ENSSessionRequest API blocks until either a
 *    response is received or the session is ended.  If a response is received, the response
 *    data is referenced by the @ref ENSUserData "userdata" parameter.  The workload must
 *    free the response data buffer using @ref ENSSessionFree after it has processed the response.
 *
 * -  The workload receiving the request should process the request and modify the
 *    @ref ENSUserData structure to reference the data to be returned on the response before
 *    returning from the event function.  The workload may reuse the buffer supplied on
 *    the request, or it may free it back to the runtime and allocate a new buffer for
 *    the response.
 *
 * Notify transactions are initiated with the @ref ENSSessionNotify API (or equivalent)
 * and result in a call to the @ref ENSEventFn ["event function"] with event_type set to @ref EVENT_NOTIFY.
 *
 * -  The workload initiating the notify includes data in the buffer referenced by the
 *    @ref ENSUserData "userdata" parameter, which must be allocated using @ref ENSSessionAlloc.
 *
 * -  The workload receiving the notify should process the received data and either free
 *    the buffer using the @ref ENSSessionFree API, or pass it to the runtime on another
 *    data transfer API call.
 *
 * Buffers
 * -------
 *
 * All data passed across the API must be allocated by the runtime and freed back to the
 * runtime.  This allows the runtime to support zero-copy, ultra low-latency transfer of
 * data to/from the platform event API gateway.  Buffers come from a pool that is limited
 * in size, so the workload should not hold onto buffers when they are not being used for
 * data transfer API interactions.
 *
 * Threading
 * ---------
 *
 * The runtime uses a dynamically sized thread pool to invoke workload event functions
 * so workloads are free to make blocking calls (to the ENS API or other system APIs)
 * without risking thread starvation in the runtime.  Workloads may also create their
 * own threads to invoke ENS API functions.
 *
 * In general, event functions must be thread safe as the runtime may invoke the same
 * event function with the same session identifier concurrently with different events.
 */

extern "C" {

/**
 * Structure used to pass user data across the ENS API.
 *
 * This structure must always refer to memory allocated by the ENSSessionAlloc
 * API and freed using the ENSSessionFree API.
 *
 * @param  length          Length of the data in the buffer in bytes/octets.
 * @param  p               Address of the start of the buffer.
 */
typedef struct
{
  uint32_t length;
  uint8_t* p;
} ENSUserData;

/**
 * Event function.
 *
 * The event function is invoked by the runtime when a session lifecycle or data transfer
 * event happens on a session.  Each named interface provided by a workload has a bound
 * event function defined in metadata, and this event function is used for all sessions
 * established to that named interface.  For outgoing sessions from a workload the
 * workload can either specify its own function on the ENSSessionStart API, or use the
 * default event function defined in metadata.
 *
 * @param   session_id     Unique identifier for the session.
 * @param   event_type     Indicates the type of data transfer or session lifecycle event.
 *                         -   EVENT_REQUEST or EVENT_NOTIFY for incoming data transfer events.
 *                         -   EVENT_SESSION_START, EVENT_SESSION_END or EVENT_SESSION_DISCONNECT
 *                             for session lifecycle events.
 * @param   sqn            Sequence number (0 for session lifecycle events, unique non-zero value
 *                         for data transfer events.
 * @param   data           Pointer to ENSUserData structure referencing user data for the event.
 *                         -   If event_type is EVENT_REQUEST, this references the request data
 *                             when the event function is invoked and the response data when
 *                             the event function completes.
 *                         -   If event_type is EVENT_NOTIFY, this references the notify data
 *                             when the event function is invoked and is ignored when the
 *                             function completes.
 */
typedef void (*ENSEventFn)(uint32_t session_id, uint32_t event_type, uint32_t sqn, ENSUserData* data);

/**
 * @name Event types for event function callbacks.
 * @{
 */

#define EVENT_REQUEST             0     /**< Incoming request event */
#define EVENT_NOTIFY              1     /**< Incoming notify event */

#define EVENT_SESSION_START       10    /**< Incoming session start event */
#define EVENT_SESSION_END         20    /**< Incoming session end event */
#define EVENT_SESSION_DISCONNECT  21    /**< Incoming session disconnect event */

/** @} */

/**
 * Starts a new event session with the specified target interface.
 *
 * @param  interface_name  The name of the target interface for the session in the form
 *                         &lt;microservice name&gt;.&lt;interface name&gt;.
 * @param  event_fn        A pointer to the event function for session lifecycle and
 *                         notify events on this session.  If this is not specified
 *                         the first event interface defined on the workload is used.
 * @return                 Unique identifier for the session.
 */
uint32_t ENSSessionStart(const char* interface_name, ENSEventFn event_fn=NULL);

/**
 * Ends the event session.
 *
 * @param  session_id      Unique identifier for the session.
 */
void ENSSessionEnd(uint32_t session_id);

/**
 * Aborts the event session.
 * @deprecated
 *
 * @param  session_id      Unique identifier for the session.
 * @param  reason          Reason code for the session abort.
 * @param  info            Optional text explanation of the reason for the abort.
 */
void ENSSessionAbort(uint32_t session_id, uint32_t reason, const char* info);

/**
 * Sends a request on the event session and blocks waiting for a response.
 *
 * @param  session_id      Unique identifier for the session.
 * @param  sqn             Sequence number for the request.
 * @param  userdata        Pointer to ENSUserData structure referencing the request
 *                         and response data.
 * @return                 true/false depending on successful completion of
 *                         request.
 */
bool ENSSessionRequest(uint32_t session_id, uint32_t sqn, ENSUserData* userdata);

/**
 * Sends a notify on the event session.
 *
 * @param  session_id      Unique identifier for the session.
 * @param  sqn             Sequence number for the request.
 * @param  userdata        Pointer to ENSUserData structure referencing the notify data.
 * @return                 true/false depending on successful sending of notify.
 */
bool ENSSessionNotify(uint32_t session_id, uint32_t sqn, ENSUserData* userdata);

/**
 * Allocates a buffer suitable for passing across the API in an ENSUserData structure.
 *
 * @param  length          The length of the buffer requested in bytes.
 * @return                 A pointer to the allocated buffer, or NULL if allocation fails.
 */
uint8_t* ENSSessionAlloc(size_t length);

/**
 * Frees a buffer that has been received across the API from the ENS runtime.
 *
 * @param  data            A pointer to the buffer.
 */
void ENSSessionFree(uint8_t* data);

} /* extern "C" */

#endif



