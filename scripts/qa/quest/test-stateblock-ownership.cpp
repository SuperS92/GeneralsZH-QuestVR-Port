// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include "com_pointer.h"
#include <utility>
#include <cassert>
#include <cstdio>
struct Probe {unsigned refs=1;static unsigned alive;Probe(){++alive;}unsigned AddRef(){return ++refs;}
unsigned Release(){const auto n=--refs;if(!n)delete this;return n;}~Probe(){--alive;}};
unsigned Probe::alive=0;
struct Owner {dxvk::Com<Probe> state;explicit Owner(dxvk::Com<Probe>&& p):state(std::move(p)){} };
int main(){for(unsigned i=0;i<8192;++i){dxvk::Com<Probe> local;*(&local)=new Probe;{Owner owner(std::move(local));}assert(Probe::alive==0);}
std::puts("PASS 8192 actual COM move-ownership lifetime cases");}
