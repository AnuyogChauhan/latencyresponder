/**
 * @file latencyresponder.cpp
 *
 * Project Edge
 * Copyright (C) 2016-17  Deutsche Telekom Capital Partners Strategic Advisory LLC
 *
 */

#include <stdio.h>
#include <ens.h>

extern "C" {

void event_handler(uint32_t session_id, uint32_t event_type, uint32_t sqn, ENSUserData* data)
{
  if (event_type == EVENT_NOTIFY)
  {
    // Send a notify in response, using the same sequence number and data.
    ENSSessionNotify(session_id, sqn, data);
  }

  // For all other event types return unchanged.  For EVENT_REQUEST this will
  // send a response with the data from the response unchanged.
}

} /* extern "C" */


