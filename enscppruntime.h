/**
 * @file enscppruntime.h
 *
 * Project Edge
 * Copyright (C) 2016-17  Deutsche Telekom Capital Partners Strategic Advisory LLC
 *
 */

#ifndef ENSCPPRUNTIME_H__
#define ENSCPPRUNTIME_H__

#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <map>
#include <list>
#include <string>
#include "ensiwcworkload.h"
#include "ens.h"

class ENSRuntime;

class ENSSession
{
public:
  ENSSession(ENSRuntime& runtime, uint32_t session_id);
  ~ENSSession();

  void start(const std::string& interface_name, ENSEventFn event_fn);
  void end();
  void disconnect();

  bool send_request(uint32_t sqn, ENSUserData* data);

  bool send_notify(uint32_t sqn, ENSUserData* data);

  void process_msg(uint32_t msg_id, uint32_t sqn, uint32_t data_length, uint8_t* data);

  inline uint32_t id() const { return _id; }

private:
  const uint32_t _id;
  ENSRuntime& _runtime;

  pthread_spinlock_t _lock;
  bool _active;
  ENSEventFn _event_fn;

  typedef struct
  {
    sem_t sem_wait;
    bool disconnect;
    ENSUserData rsp;
  } PendingReq;
  std::map<uint32_t, PendingReq> _pending_req;
};

class ENSReactor
{
public:
  ENSReactor(ENSRuntime& runtime);
  ~ENSReactor();

  void start_thread();

private:

  static void* thread(void* p);
  void run();

  ENSRuntime& _runtime;
  std::list<pthread_t> _threads;
};

class ENSRuntime
{
public:
  ENSRuntime(const Json::Value& config);
  ~ENSRuntime();

  void run();

  bool poll();

  void send(uint32_t session_id, uint32_t msg_id, uint32_t sqn, ENSUserData* data);

  ENSSession* session(uint32_t session_id=0);
  void remove_session(uint32_t session_id);

  ENSEventFn event_fn(const std::string& interface_name=std::string());

  inline uint8_t* alloc(size_t length) { return _iwc->mem(_iwc->alloc(length)); }
  inline void free(uint8_t* buf) { _iwc->free(_iwc->handle(buf)); }

private:

  bool idle();
  uint32_t new_session_id();

  int _id;

  ENSIWCWorkload* _iwc;
  ENSReactor* _reactor;

  pthread_spinlock_t _lock;
  uint32_t _next_session_id;
  std::map<uint32_t, ENSSession*> _sessions;
  int _last_active;

  std::map<std::string, ENSEventFn> _events;
};

#endif
