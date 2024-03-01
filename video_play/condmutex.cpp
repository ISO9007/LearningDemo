#include "condmutex.h"


CondMutex::CondMutex()
{
    _cond = SDL_CreateCond();
    _mutex = SDL_CreateMutex();
}

CondMutex::~CondMutex() {
    SDL_DestroyCond(_cond);
    SDL_DestroyMutex(_mutex);
}

void CondMutex::lock() {
    SDL_LockMutex(_mutex);
}
void CondMutex::unlock() {
    SDL_UnlockMutex(_mutex);
}
void CondMutex::signal() {
    SDL_CondSignal(_cond);
}
void CondMutex::broadcast() {
    SDL_CondBroadcast(_cond);
}
void CondMutex::wait() {
    SDL_CondWait(_cond, _mutex);
}
