#ifndef NODE_NTPL_MODIFICATORS_MODULE
#define NODE_NTPL_MODIFICATORS_MODULE

#include <v8.h>
#include <stdio.h>

namespace ntpl {
	namespace mod {
	
		using namespace v8;
		
		Handle<Value> ops(const FunctionCallbackInfo<Value>& args);
		
		Handle<Value> add(const FunctionCallbackInfo<Value>& args);
		
		void init(Handle<Object> target);
	}
}

#endif // NODE_NTPL_MODIFICATORS_MODULE