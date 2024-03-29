/* @@@LICENSE
*
*      Copyright (c) 2009-2013 LG Electronics, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */




#ifndef __EventReporter_h__
#define __EventReporter_h__

#include "Common.h"

#include <luna-service2/lunaservice.h>
#include "Mutex.h"

class EventReporter
{
public:
	static EventReporter* instance();
	static void init(GMainLoop* loop);
	
	bool report( const char* eventString, const char* eventData );
 
private:
	EventReporter(GMainLoop* loop);
	~EventReporter();
	
	LSHandle* m_service;
	Mutex m_mutex;
};

 
 #endif // __EventReporter_h__
 

