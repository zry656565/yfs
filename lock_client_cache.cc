// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

#define THREAD_POOL_SIZE 1


int lock_client_cache::last_port = 0;

squeue::squeue()
{
	VERIFY(pthread_mutex_init(&m, NULL) == 0);
	VERIFY(pthread_cond_init(&c, NULL) == 0);
}

squeue::~squeue()
{
	VERIFY(pthread_mutex_destroy(&m) == 0);
	VERIFY(pthread_cond_destroy(&c) == 0);
}

void 
squeue::push(lock_protocol::lockid_t lid)
{
	pthread_mutex_lock(&m);
	tasks.push(lid);
	VERIFY(pthread_cond_broadcast(&c) == 0);
	pthread_mutex_unlock(&m);
}

lock_protocol::lockid_t
squeue::pop()
{
	pthread_mutex_lock(&m);
	while(tasks.empty())
		pthread_cond_wait(&c, &m);
	lock_protocol::lockid_t lid = tasks.front();
	tasks.pop();
	pthread_mutex_unlock(&m);
	return lid;
}

void* release_lock(void * clt)
{
	//std::cout << pthread_self() << std::endl;
	lock_client_cache* cc = (lock_client_cache*) clt;
	cc->release_handler();
	pthread_exit(NULL);
}

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
	srand(time(NULL)^last_port);
	rlock_port = ((rand()%32000) | (0x1 << 10));
	const char *hname;
	// VERIFY(gethostname(hname, 100) == 0);
	hname = "127.0.0.1";
	std::ostringstream host;
	host << hname << ":" << rlock_port;
	id = host.str();
	last_port = rlock_port;
	rpcs *rlsrpc = new rpcs(rlock_port);
	rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
	rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

	VERIFY(pthread_mutex_init(&locks_cache_m, NULL) == 0);  //init the mutex for the map
	//start a thread to finish the tasks in the sync queue
	pthread_t release_t;
	for(int i = 0; i < THREAD_POOL_SIZE; ++i)            
		VERIFY(pthread_create(&release_t, NULL, release_lock, (void *)this) == 0);
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;
	int r;
	pthread_mutex_lock(&locks_cache_m);
	
	if(locks_cache.find(lid) == locks_cache.end())
		locks_cache[lid].status = NONE;
	
	while(locks_cache[lid].status != FREE && locks_cache[lid].status != RETRY)
	{
		if(locks_cache[lid].status == NONE) 
		{
			locks_cache[lid].status = ACQUIRING;
			pthread_mutex_unlock(&locks_cache_m);  //remember unlock the mutex before call RPC
			ret = cl->call(lock_protocol::acquire, lid, id, r);
			VERIFY (ret == lock_protocol::OK || ret == lock_protocol::RETRY);//status may be changed
			pthread_mutex_lock(&locks_cache_m); 
			if(ret == lock_protocol::OK || locks_cache[lid].status == RETRY || locks_cache[lid].status == FREE) //retry RPC may come before RETRY value
			{
				break;
			}else if(ret == lock_protocol::RETRY){		
				pthread_cond_wait(&(locks_cache[lid].c), &locks_cache_m); //wait on the lock
			}
		}else if(locks_cache[lid].status == ACQUIRING || locks_cache[lid].status == RELEASING || locks_cache[lid].status == LOCKED){
			pthread_cond_wait(&(locks_cache[lid].c), &locks_cache_m); //wait on the lock
		}
	}
	locks_cache[lid].status = LOCKED; //two situation can reach here: 1.status is FREE 2.status is NONE and acquire successfully from the server
	pthread_mutex_unlock(&locks_cache_m);
  return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
	pthread_mutex_lock(&locks_cache_m);
	if(locks_cache[lid].revoke)  // check whether revoke is true.
	{
		int r;
		locks_cache[lid].status = RELEASING;
		pthread_mutex_unlock(&locks_cache_m);
		
		int ret = cl->call(lock_protocol::release, lid, id, r);
		VERIFY (ret == lock_protocol::OK);
		pthread_mutex_lock(&locks_cache_m);
		locks_cache[lid].status = NONE;
		locks_cache[lid].revoke = false;
	}else{
		locks_cache[lid].status = FREE;
	}
	VERIFY(pthread_cond_broadcast(&(locks_cache[lid].c)) == 0);
	pthread_mutex_unlock(&locks_cache_m);

  return lock_protocol::OK;

}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
	//If revoke is true, means that other clients need the lock, but this client is using it now
	//use the lock at least and at most once when receiving revoke before using the lock
  int ret = rlock_protocol::OK;
	VERIFY(locks_cache.find(lid) != locks_cache.end() && locks_cache[lid].status != NONE); 
	pthread_mutex_lock(&locks_cache_m);
	if(locks_cache[lid].status == LOCKED || locks_cache[lid].status == ACQUIRING || locks_cache[lid].status == RETRY)
	{
		locks_cache[lid].revoke = true;
	}else if(locks_cache[lid].status == FREE){
		locks_cache[lid].status = RELEASING;
		tasks.push(lid);  //like producer, and the lock will be released by consumer
	}
	pthread_mutex_unlock(&locks_cache_m);
  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
  int ret = rlock_protocol::OK;
	VERIFY(locks_cache.find(lid) != locks_cache.end()); //status is acquiring
	pthread_mutex_lock(&locks_cache_m);
	locks_cache[lid].status = RETRY;
	VERIFY(pthread_cond_broadcast(&(locks_cache[lid].c)) == 0);
	pthread_mutex_unlock(&locks_cache_m);
  return ret;
}

void
lock_client_cache::release_handler()
{
	while(true)
	{
		lock_protocol::lockid_t lid = tasks.pop();
		int r;
		int ret = cl->call(lock_protocol::release, lid, id, r);
		VERIFY (ret == lock_protocol::OK);
		pthread_mutex_lock(&locks_cache_m);
		locks_cache[lid].status = NONE;
		locks_cache[lid].revoke = false;
		VERIFY(pthread_cond_broadcast(&(locks_cache[lid].c)) == 0);
		pthread_mutex_unlock(&locks_cache_m);
	}
}
