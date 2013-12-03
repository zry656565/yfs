// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include <map>
#include <queue>
#include <pthread.h>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"
#include "lang/verify.h"



// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 5.
class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

class squeue{
	private:
		pthread_mutex_t m;
		pthread_cond_t c;
		std::queue<lock_protocol::lockid_t> tasks;

	public:
		squeue();
		~squeue();
		void push(lock_protocol::lockid_t);
		lock_protocol::lockid_t pop();
};
	
class lock_client_cache : public lock_client {
 private:
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;

	enum lock_clt_status{ NONE, FREE, LOCKED, ACQUIRING, RELEASING, RETRY };
	struct lock_clt_t{
		lock_clt_t(){
			status = NONE;
			revoke = false;
			VERIFY(pthread_cond_init(&c, NULL) == 0);
		}
		~lock_clt_t(){
			VERIFY(pthread_cond_destroy(&c) == 0);
		}
		//condition for acquiring and locked
		pthread_cond_t c;
		lock_clt_status status;
		bool revoke;
	};

	std::map<lock_protocol::lockid_t, struct lock_clt_t> locks_cache;
	pthread_mutex_t locks_cache_m;

 public:
  static int last_port;
  squeue tasks;
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t, 
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t, 
                                       int &);

	void release_handler();
};


#endif
