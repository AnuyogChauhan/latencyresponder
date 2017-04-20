/**
 * @file enscppruntime.cpp
 *
 * Project Edge
 * Copyright (C) 2016-17  Deutsche Telekom Capital Partners Strategic Advisory LLC
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <dlfcn.h>
#include <memory.h>
#include <time.h>
#include <semaphore.h>
#include <list>
#include <string>
#include <json/json.h>
#include "enslog.h"
#include "ensiwcworkload.h"
#include "ensiwcmsg.h"
#include "ens.h"
#include "enscppruntime.h"

static ENSRuntime* runtime = NULL;

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    exit(1);
  }

  Json::Reader r;
  Json::Value config;
  LOG_INFO("Parsing configuration: %s", argv[1]);
  r.parse(std::string(argv[1]), config);

  runtime = new ENSRuntime(config);
  runtime->run();
}

extern "C" {

uint32_t ENSSessionStart(const char* interface_name, ENSEventFn event_fn)
{
  ENSSession* session = runtime->session();
  if (session)
  {
    session->start(interface_name, event_fn);
    return session->id();
  }
  return 0;
}

void ENSSessionEnd(uint32_t session_id)
{
  ENSSession* session = runtime->session(session_id);
  if (session)
  {
    session->end();
  }
}

void ENSSessionAbort(uint32_t session_id, uint32_t reason, const char* info)
{
}

bool ENSSessionRequest(uint32_t session_id, uint32_t sqn, ENSUserData* userdata)
{
  ENSSession* session = runtime->session(session_id);
  if (session)
  {
    return session->send_request(sqn, userdata);
  }
  return false;
}

bool ENSSessionNotify(uint32_t session_id, uint32_t sqn, ENSUserData* userdata)
{
  LOG_DEBUG("ENSSessionNotify(%d, %d, \"%.*s\")", session_id, sqn,
            (userdata != NULL) ? (userdata->length) : 0,
            ((userdata != NULL) && (userdata->length != 0)) ? (char*)userdata->p : "");
  ENSSession* session = runtime->session(session_id);
  if (session)
  {
    return session->send_notify(sqn, userdata);
  }
  return false;
}

uint8_t* ENSSessionAlloc(size_t length)
{
  return runtime->alloc(length);
}

void ENSSessionFree(uint8_t* data)
{
  runtime->free(data);
}

} // extern "C"

ENSSession::ENSSession(ENSRuntime& runtime, uint32_t session_id) :
  _id(session_id),
  _runtime(runtime),
  _active(false),
  _event_fn(NULL),
  _pending_req()
{
  pthread_spin_init(&_lock, PTHREAD_PROCESS_PRIVATE);
}

ENSSession::~ENSSession()
{
}

void ENSSession::start(const std::string& interface_name, ENSEventFn event_fn)
{
  if (event_fn != NULL)
  {
    // Use user supplied event function for reporting notifys and session
    // lifecycle events for this session.
    _event_fn = event_fn;
  }
  else
  {
    // Get the default event function for the workload.
    _event_fn = _runtime.event_fn();
  }

  // Create a pending request object for the session-start request.
  pthread_spin_lock(&_lock);
  PendingReq& w = _pending_req[0];
  pthread_spin_unlock(&_lock);
  sem_init(&w.sem_wait, 0, 0);

  // Send the SESSION_START message.
  LOG_DEBUG("Send SESSION_START(%s) for session %d", interface_name.c_str(), _id);
  ENSUserData userdata;
  userdata.length = interface_name.length();
  userdata.p = _runtime.alloc(userdata.length);
  memcpy(userdata.p, interface_name.data(), userdata.length);
  _runtime.send(_id, MSG_SESSION_START, 0, &userdata);

  // Wait for the response.
  sem_wait(&w.sem_wait);

  // Free the response and the pending request object.
  _runtime.free(w.rsp.p);
  sem_destroy(&w.sem_wait);
  pthread_spin_lock(&_lock);
  _pending_req.erase(0);
  pthread_spin_unlock(&_lock);

  return;
}

void ENSSession::end()
{
  if (_active)
  {
    disconnect();
    LOG_DEBUG("Send SESSION_END for session %d", _id);
    _runtime.send(_id, MSG_SESSION_STOP, 0, NULL);
  }
  _runtime.remove_session(_id);
  delete this;
}

void ENSSession::disconnect()
{
  _active = false;
  for (std::map<uint32_t, PendingReq>::iterator i = _pending_req.begin();
       i != _pending_req.end();
       ++i)
  {
    i->second.disconnect = true;
    sem_post(&i->second.sem_wait);
  }
}

bool ENSSession::send_request(uint32_t sqn, ENSUserData* userdata)
{
  if (!_active)
  {
    LOG_ERROR("Session not active");
    return false;
  }

  bool rc = false;

  // Create a pending request object for the request.
  pthread_spin_lock(&_lock);
  PendingReq& w = _pending_req[sqn];
  pthread_spin_unlock(&_lock);
  sem_init(&w.sem_wait, 0, 0);

  // Send the request.
  LOG_DEBUG("Send REQUEST for session %d", _id);
  _runtime.send(_id, MSG_REQUEST, sqn, userdata);

  // Wait for the response.
  sem_wait(&w.sem_wait);
  if (!w.disconnect)
  {
    // Received a valid response.
    *userdata = w.rsp;
    rc = true;
  }

  // Delete the pending request object.
  sem_destroy(&w.sem_wait);
  pthread_spin_lock(&_lock);
  _pending_req.erase(sqn);
  pthread_spin_unlock(&_lock);
  return rc;
}

bool ENSSession::send_notify(uint32_t sqn, ENSUserData* userdata)
{
  if (!_active)
  {
    LOG_ERROR("Session not active");
    return false;
  }

  LOG_DEBUG("Send NOTIFY for session %d", _id);
  _runtime.send(_id, MSG_NOTIFY, sqn, userdata);
  return true;
}

void ENSSession::process_msg(uint32_t msg_id, uint32_t sqn, uint32_t data_length, uint8_t* data)
{
  if (msg_id == MSG_REQUEST)
  {
    LOG_DEBUG("Received REQUEST for session %d: %.*s", _id, data_length, (data != NULL) ? (char*)data : "");
    ENSUserData userdata = {data_length, data};
    if (_event_fn)
    {
      _event_fn(_id, EVENT_REQUEST, sqn, &userdata);
    }
    LOG_DEBUG("Sending RESPONSE for session %d: %.*s", _id, userdata.length, (userdata.p != NULL) ? (char*)userdata.p : "");
    _runtime.send(_id, MSG_RESPONSE, sqn, &userdata);
  }
  else if (msg_id == MSG_NOTIFY)
  {
    LOG_DEBUG("Received NOTIFY for session %d: %.*s", _id, data_length, (data != NULL) ? (char*)data : "");
    ENSUserData userdata = {data_length, data};
    if (_event_fn)
    {
      _event_fn(_id, EVENT_NOTIFY, sqn, &userdata);
    }
  }
  else if (msg_id == MSG_RESPONSE)
  {
    LOG_DEBUG("Received RESPONSE for session %d: %.*s", _id, data_length, (data != NULL) ? (char*)data : "");
    pthread_spin_lock(&_lock);
    std::map<uint32_t, PendingReq>::iterator i = _pending_req.find(sqn);
    if (i != _pending_req.end())
    {
      i->second.rsp = {data_length, data};
      sem_post(&i->second.sem_wait);
    }
    pthread_spin_unlock(&_lock);
  }
  else if (msg_id == MSG_SESSION_START)
  {
    LOG_INFO("Received START message for session %d", _id);
    _active = true;
    _event_fn = _runtime.event_fn(std::string((char*)data, data_length));
    _runtime.free(data);
    if (_event_fn)
    {
      _event_fn(_id, EVENT_SESSION_START, sqn, NULL);
    }
    _runtime.send(_id, MSG_SESSION_STARTED, sqn, NULL);
  }
  else if (msg_id == MSG_SESSION_STARTED)
  {
    LOG_INFO("Received SESSION_STARTED message for session %d", _id);
    _active = true;
    pthread_spin_lock(&_lock);
    std::map<uint32_t, PendingReq>::iterator i = _pending_req.find(0);
    if (i != _pending_req.end())
    {
      i->second.rsp = {data_length, data};
      sem_post(&i->second.sem_wait);
    }
    pthread_spin_unlock(&_lock);
  }
  else if (msg_id == MSG_SESSION_STOP)
  {
    LOG_INFO("Received STOP message for session %d", _id);
    disconnect();
    if (_event_fn)
    {
      _event_fn(_id, EVENT_SESSION_END, sqn, NULL);
    }
  }
  else if (msg_id == MSG_SESSION_DISCONNECTED)
  {
    LOG_INFO("Received DISCONNECTED message for session %d", _id);
    disconnect();
    if (_event_fn)
    {
      _event_fn(_id, EVENT_SESSION_DISCONNECT, sqn, NULL);
    }
  }

  if (!_active)
  {
    _runtime.remove_session(_id);
    delete this;
  }
}

ENSReactor::ENSReactor(ENSRuntime& runtime) :
  _runtime(runtime),
  _threads()
{
}

ENSReactor::~ENSReactor()
{
}

void ENSReactor::start_thread()
{
  pthread_t thread_id;
  int rc = pthread_create(&thread_id, NULL, &thread, (void*)this);
  if (rc != 0)
  {
    LOG_ERROR("Failed to create new ENSReactor thread");
    return;
  }
  _threads.push_back(thread_id);
}

void* ENSReactor::thread(void* p)
{
  ((ENSReactor*)p)->run();
  return NULL;

}

void ENSReactor::run()
{
  LOG_INFO("New Reactor thread");
  while (_runtime.poll())
  {
  }
  LOG_INFO("Reactor thread exiting");
}

ENSRuntime::ENSRuntime(const Json::Value& config) :
  _next_session_id(1),
  _sessions(),
  _events()
{
  pthread_spin_init(&_lock, PTHREAD_PROCESS_PRIVATE);
  _last_active = time(NULL);

  // Get the UUID.
  _id = config["id"].asInt();

  // Parse the event interface mappings.
  const std::string& microservice = config["microservice"].asString();
  const Json::Value& events = config["events"];
  for (Json::ValueConstIterator i = events.begin(); i != events.end(); i++)
  {
    if ((!i.key().isString()) || (!(*i).isObject()) ||
        (!(*i).isMember("fn")) || (!(*i)["fn"].isString()))
    {
      LOG_ERROR("Invalid event entry - %s: %s",
                i.key().toStyledString().c_str(), (*i).toStyledString().c_str());
    }
    else
    {
      const std::string& event_name = microservice + "." + i.key().asString();
      const std::string& fn = (*i)["fn"].asString();
      const std::string module_name = fn.substr(0, fn.find('.')) + ".so";
      const std::string fn_name = fn.substr(fn.find('.')+1, -1);

      // Dynamically link to the event entry point.
      dlerror();
      void* mod_handle = dlopen(module_name.c_str(), RTLD_NOW);
      if (mod_handle == NULL)
      {
        LOG_ERROR("Failed to load module %s: %s", module_name.c_str(), dlerror());
      }
      else
      {
        ENSEventFn event_fn = reinterpret_cast<ENSEventFn>(dlsym(mod_handle, fn_name.c_str()));
        if (event_fn == NULL)
        {
          LOG_ERROR("Failed to link function %s in module %s: %s",
                    fn_name.c_str(), module_name.c_str(), dlerror());
        }
        else
        {
          LOG_INFO("Linked event interface %s to event function %s:%s, address %p",
                   event_name.c_str(), module_name.c_str(), fn_name.c_str(), event_fn);
          _events[event_name] = event_fn;
        }
      }
    }
  }
}

ENSRuntime::~ENSRuntime()
{
}

void ENSRuntime::run()
{
  _iwc = new ENSIWCWorkload(_id, 4, 100000);
  _iwc->start();
  _reactor = new ENSReactor(*this);
  _reactor->start_thread();

  //while(!idle())
  while (true)
  {
    sleep(1);
  }

  LOG_INFO("Exiting workload");
}

bool ENSRuntime::poll()
{
  ENSIWCMsg msg;
  if (!_iwc->recv(msg))
  {
    return false;
  }

  LOG_DEBUG("Receive message %d, session=%d, sqn=%d, data=%.*s",
            msg.msg_id, msg.session_id, msg.sqn,
            msg.length, (msg.length != 0) ? (char*)_iwc->msg_data(msg) : "");

  if (_iwc->waiters() == 0)
  {
    // Create a new thread in the reactor.
    _reactor->start_thread();
  }

  LOG_DEBUG("Find session %d", msg.session_id);
  ENSSession* s = session(msg.session_id);
  LOG_DEBUG("Process message");
  s->process_msg(msg.msg_id, msg.sqn, msg.length, _iwc->msg_data(msg));
  return true;
}

void ENSRuntime::send(uint32_t session_id, uint32_t msg_id, uint32_t sqn, ENSUserData* userdata)
{
  ENSIWCMsg msg;
  msg.session_id = session_id;
  msg.msg_id = msg_id;
  msg.sqn = sqn;
  if (userdata != NULL)
  {
    msg.length = userdata->length;
    msg.data = _iwc->handle(userdata->p);
  }
  else
  {
    msg.length = 0;
    msg.data = 0;
  }
  LOG_DEBUG("Send message %d, session=%d, sqn=%d, data=%.*s",
            msg.msg_id, msg.session_id, msg.sqn,
            msg.length, (msg.length != 0) ? (char*)_iwc->msg_data(msg) : "");
  _iwc->send(msg);
}

ENSSession* ENSRuntime::session(uint32_t session_id)
{
  ENSSession* s = NULL;
  pthread_spin_lock(&_lock);
  if (session_id == 0)
  {
    session_id = new_session_id();
    s = new ENSSession(*this, session_id);
    _sessions[session_id] = s;
  }
  else
  {
    std::map<uint32_t, ENSSession*>::const_iterator i = _sessions.find(session_id);

    if (i == _sessions.end())
    {
      s = new ENSSession(*this, session_id);
      _sessions[session_id] = s;
    }
    else
    {
      s = i->second;
    }
  }
  pthread_spin_unlock(&_lock);
  return s;
}

void ENSRuntime::remove_session(uint32_t session_id)
{
  LOG_DEBUG("Remove session %d", session_id);
  pthread_spin_lock(&_lock);
  _sessions.erase(session_id);
  if (_sessions.empty())
  {
    LOG_DEBUG("No active sessions");
    _last_active = time(NULL);
  }
  pthread_spin_unlock(&_lock);
}

ENSEventFn ENSRuntime::event_fn(const std::string& interface_name)
{
  LOG_DEBUG("Look up address for interface %s", interface_name.c_str());
  if (interface_name.length() == 0)
  {
    LOG_DEBUG("Returing function %p for default interface", _events.begin()->second);
    return _events.begin()->second;
  }
  std::map<std::string, ENSEventFn>::const_iterator i = _events.find(interface_name);
  LOG_DEBUG("Returning function %p for interface %s", (i != _events.end()) ? i->second : NULL, interface_name.c_str());
  return (i != _events.end()) ? i->second : NULL;
}

bool ENSRuntime::idle()
{
  pthread_spin_lock(&_lock);
  bool rc = ((_sessions.empty()) && (time(NULL) - _last_active) > 10);
  pthread_spin_unlock(&_lock);
  return rc;
}

uint32_t ENSRuntime::new_session_id()
{
  uint32_t session_id = _next_session_id++;
  return session_id;
}

