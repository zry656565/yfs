// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache()
{
	VERIFY(pthread_mutex_init(&locks_m, NULL) == 0);	
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
                               int &)
{
  lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&locks_m);
	if(locks.find(lid) == locks.end())
		locks[lid].status = FREE;

	if(locks[lid].status == FREE)
	{
		locks[lid].status = LOCKED;
		locks[lid].owner = id;
		pthread_mutex_unlock(&locks_m);
	}else{
		//push and return retry, if queue is empty, revoke the owner of the current lock.
		if(locks[lid].queue.empty())
		{
			locks[lid].queue.push_back(id);
			std::string owner = locks[lid].owner;
			pthread_mutex_unlock(&locks_m);
 
			handle h(owner);
			rlock_protocol::status re;
			int r;
			if (h.safebind()) {
				re = h.safebind()->call(rlock_protocol::revoke, lid, r);
			}
			if (!h.safebind() || re != rlock_protocol::OK) {
				tprintf("lock_server_cache::acquire revoke failed \n");
				abort();
			}	
		}else{
			locks[lid].queue.push_back(id);
			pthread_mutex_unlock(&locks_m);
		}
		ret = lock_protocol::RETRY;
	}
		
  return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &)
{
  lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&locks_m);
	
	VERIFY(!locks[lid].queue.empty() && locks[lid].owner == id && locks[lid].status == LOCKED);
	std::string new_owner = locks[lid].queue.front();
	locks[lid].queue.pop_front();
	locks[lid].owner = new_owner;
	int need_revoke = 0;
	if(!locks[lid].queue.empty())
		need_revoke = 1;
	pthread_mutex_unlock(&locks_m); 
	//send retry to the peek client of current queue
	handle h(new_owner);
	rlock_protocol::status re;
	int r;
	if (h.safebind()) {
		re = h.safebind()->call(rlock_protocol::retry, lid, r);
	}
	if (!h.safebind() || re != rlock_protocol::OK) {
		tprintf("lock_server_cache::release retry failed \n");
		abort();
	}
	//if the queue is not empty, send a more revoke and allow the client use that lock at most one time and release it. 
	if(need_revoke){
		if (h.safebind()) {
			re = h.safebind()->call(rlock_protocol::revoke, lid, r);
		}
		if (!h.safebind() || re != rlock_protocol::OK) {
			tprintf("lock_server_cache::release revoke failed \n");
			abort();
		}
	}
	
  return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}