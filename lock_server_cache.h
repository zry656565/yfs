#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>


#include <map>
#include <deque>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"


class lock_server_cache {
 private:
  int nacquire;
	enum lock_srv_status{ FREE, LOCKED };
	struct lock_srv_t{
		lock_srv_t(){
			status = FREE;
			owner = "";
		}
		lock_srv_status status;
		std::string owner;
		std::deque<std::string> queue;
	};
	
	std::map<lock_protocol::lockid_t, struct lock_srv_t> locks;
	pthread_mutex_t locks_m;

 public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
};

#endif
