/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Josh Aas <josh@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "dfbadapter.h"
#include "nppalmdefs.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>            
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/wait.h>

#include "event.h"

#define PLUGIN_NAME	"DFBAdapter"
#define PLUGIN_DESCRIPTION PLUGIN_NAME " (Mozilla SDK)"
#define PLUGIN_VERSION     "1.0.0.0"
#define PLUGIN_MIMEDESCRIPTION "application/x-webosinternals-dfbadapter:dfbadt:DFBAdapter"

#define MAX_MEM_CHUNCKS 6
#define PRIMARY_MEM_SIZE MAX_MEM_CHUNCKS*1024*1024

static int QUIET = 1;

void logTimeStamp(FILE *out, int line);

static FILE *log = NULL;

#define myprintf(fmt...) 	    do {					\
									if (!QUIET) { 		\
										if (!log) \
											log = fopen("/tmp/dfbadapter-plugin.log", "w");\
										logTimeStamp(log, __LINE__); \
										fprintf(log, fmt); 	\
									}					\
								} while (0)

static NPNetscapeFuncs NPNFuncs;

static char	dfbprogram[50] = {0,};


struct MyObject : NPObject {
	NPP 			instance;
  /*should be removed begin*/
  int event_x;
  int event_y;
  int event_key_code;
  int solid_lines;
  /*should be removed end*/
	bool 			window_received;
	pthread_t		dfb_event_listener_tid;
	pid_t			dfb_prog_pid;
	char			dfb_primarymem_name[50];
 	int				plugin_socket_fd;
	char			dfb_socket_name[50];
	char			plugin_socket_name[50];
	pthread_mutex_t draw_mutex;
	pthread_cond_t 	drawn_cond;
	void 			*mapped_dfbmem_addr;
	bool 			dfbmem_mapped;
	DFBDrawRequest	dfb_draw_request;
	unsigned long	draw_serial_listener;
	unsigned long 	draw_serial_handler;
	int				input_fifo_fd;
	char			input_fifo_name[50];
	
	MyObject(NPP i);
	~MyObject();

	void start_dfb_event_listener_thread();
	static void *dfb_event_listener_thread(void *);
	void dfb_event_listener();
	
	int exec_dfb_program(int argc, char **argv);
};


int MyObject::exec_dfb_program(int argc, char **argv)
{
	pid_t pid;
	int 	i;

	pid = fork();

	if (pid == -1) {
		perror("can not fork dfb process\n");
		printf("can not fork dfb process\n");
		return -1;
	}	

	if (pid == 0) {

		if (execvp(argv[0], argv) < 0) {
		//if (execlp("dfbterm", "dfbterm", "--size=40x20", "--fontsize=15", (char*)NULL) < 0) {
			perror("can not exec dfb program\n");
			printf("can not exec dfb program\n");
			return -1;
		}
	}

	dfb_prog_pid = pid;
	
	return 0;
}

void MyObject::dfb_event_listener()
{
	plugin_socket_fd = socket( PF_LOCAL, SOCK_RAW, 0 );
	if (plugin_socket_fd < 0) {
		perror ("Error creating local socket!\n" );
	}
	struct sockaddr_un  addr;
	socklen_t           addr_len = sizeof(addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", plugin_socket_name);
	unlink(plugin_socket_name);
	int err = bind(plugin_socket_fd, (struct sockaddr*)&addr, sizeof(addr));
	assert(err == 0);

	fd_set set;
	char buf[1024];

	while (true) {
		int result;
		            
		FD_ZERO( &set );
		FD_SET( plugin_socket_fd, &set );

		myprintf("dfb:		 run select on plugin socket...\n");
		result = select( plugin_socket_fd + 1, &set, NULL, NULL, NULL );
		if (result < 0) {
     		switch (errno) {
          		case EINTR:
					continue;

          		default:
               		perror( "dfb_event_listener: select() failed!\n" );
  				return;	 
			}
		}

		if (FD_ISSET( plugin_socket_fd, &set ) && 
    		recvfrom( plugin_socket_fd, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addr_len ) > 0) {
     
     	DFBPBPEvent *e = (DFBPBPEvent *)buf;
		myprintf("plugin		-> event from '%s'...\n", addr.sun_path );

		switch (e->type) {
			case DFE_DrawRequest:
				myprintf("plugin:			get DFE_DrawRequest message \n");
				
				PluginEvent pe;
				PluginDrawResponse draw_res;
				draw_res.type   = PE_DrawResponse;
				draw_res.serial = e->eDFBDrawRequest.serial;

				//drawing
				if (window_received) {
					/*if (!dfbmem_mapped) {
						int dfbmem_fd = open(dfb_primarymem_name, O_RDONLY);
						if (dfbmem_fd < 0) {
							myprintf("plugin:			Couldn't open dfbmem file!\n");
							continue;
							//return;
						}
						mapped_dfbmem_addr = mmap( NULL, PRIMARY_MEM_SIZE, 
												PROT_READ, MAP_SHARED, dfbmem_fd, 0 );
						if (mapped_dfbmem_addr == MAP_FAILED) {
							myprintf( "plugin: 		Mapping dfbmem file failed!\n" );
							close( dfbmem_fd );
							return;
						}

						dfbmem_mapped = true;
						myprintf("plugin:		dfb mem has been mapped to addr %p", mapped_dfbmem_addr);
					}*/
					pthread_mutex_lock(&draw_mutex);

					dfb_draw_request = e->eDFBDrawRequest;
					
					draw_serial_listener++;	

     				//pthread_mutex_lock(&draw_mutex);
    				NPN_ForceRedraw(instance);
    				pthread_cond_wait(&drawn_cond,&draw_mutex);
    				pthread_mutex_unlock(&draw_mutex);	 						
				}					

				draw_res.done   = true;
				pe.ePluginDrawResponse = draw_res;
				while (sendto( plugin_socket_fd, &pe, sizeof(PluginEvent), 0, (struct sockaddr*)&addr, addr_len ) < 0) {
          			switch (errno) {
               			case EINTR:
							myprintf("plugin: 		sendto errno:EINTR\n");
                    		continue;
               			case ECONNREFUSED:
							myprintf("plugin:			sendto errno:ECONNREFUSEED\n");
			   				continue;
                    		//return DR_FUSION;
               			default:
                    		break;
        	 	 	}

          			myprintf( "plugin:		err sendto() dfb\n" );

     			}
				myprintf("plugin:		draw response event sent to dfb\n");
				break;
			default:
				myprintf("plugin:		can not resolve the dfb event\n");
				break;
		}

		}			   
	}
}


void *MyObject::dfb_event_listener_thread(void *obj_ptr)
{
	((MyObject *)obj_ptr)->dfb_event_listener();
	return 0;
}

void MyObject::start_dfb_event_listener_thread()
{
	pthread_mutex_init(&draw_mutex,NULL);
	pthread_cond_init(&drawn_cond,NULL);
	int rc = pthread_create(&dfb_event_listener_tid, NULL, MyObject::dfb_event_listener_thread, this);

	if (rc!=0) {
  		myprintf("Failed to create select thread.\n");
	} else {
  		myprintf("Created select thread\n");
  	}
}

MyObject::MyObject(NPP i)
:	instance(i),
	window_received(false),
	dfb_event_listener_tid(-1),
	dfb_prog_pid(-1),
	plugin_socket_fd(-1),
	dfbmem_mapped(false),
	draw_serial_listener(0),
	draw_serial_handler(0),
	input_fifo_fd(-1)
{
	
	// Start dfb program 
	// TODO : Now this is ugly hardcoded, we MUST get dfb program args in a general way 
	// (ie. from mojo js code via npruntime)
	/*if (strlen(dfbprogram))
		char *argv[] = {dfbprogram};
	else*/
	char *argv[] = {"dfbterm","--size=40x20","--fontsize=15", NULL};
	myprintf("dfbprogram: %s", argv[0]);
	
	// TODO: add error handling while exec_dfb_program return failed
	int err = exec_dfb_program(4, argv);

	//allocate resouces name used to communicate between dfbadpter and directfb lib
	sprintf(plugin_socket_name, "/tmp/shm/dfbadapter-plugin.%d", dfb_prog_pid);
	sprintf(dfb_socket_name, "/tmp/shm/dfbadapter-dfb.%d", dfb_prog_pid);
	sprintf(dfb_primarymem_name, "/tmp/shm/dfbadapter.mem.%d", dfb_prog_pid);
	sprintf(input_fifo_name, "/tmp/shm/dfbadapter-input.%d", dfb_prog_pid);
	unlink(input_fifo_name);
	unlink(dfb_primarymem_name);

	//listen for directfb draw request event 
	start_dfb_event_listener_thread();

	// wait for input fifo file being created by directfb side
	while ( (input_fifo_fd = open(input_fifo_name, O_WRONLY)) < 0 ) {
		perror("open /tmp/shm/dfbadapter-input failed\n");
		usleep(100);
	}	
	printf("plugin:		input fifo opened for write\n");
}	

MyObject::~MyObject()
{
	if (dfb_event_listener_tid) {
		pthread_cancel(dfb_event_listener_tid);
		void *thread_return = 0;
		pthread_join(dfb_event_listener_tid, &thread_return);
	}

	kill(dfb_prog_pid, SIGKILL);
	int ret_status;
	waitpid(dfb_prog_pid, &ret_status, 0);

	if(plugin_socket_fd)
		close(plugin_socket_fd);

	if(input_fifo_fd)
		close(input_fifo_fd);
	
	unlink(dfb_primarymem_name);
	unlink(dfb_socket_name);
	unlink(plugin_socket_name);
	unlink(input_fifo_name);
}

struct InstanceData {
//  NPP npp;
//  NPWindow window;
	MyObject *object;
	InstanceData() : object(0) {}
};

void logmsgv(const char* desc, NPPVariable variable)
{
	fprintf(stderr, "basic: %s - %s (#%i)\n", desc, varname(variable), variable);
}

void logTimeStamp(FILE *out, int line)
{
	struct tm *t;
	time_t now;

	if (!out)
		return;
	time(&now);
	t = localtime(&now);

	fprintf(out, "%ld %.4d-%.2d-%.2d %.2d:%.2d:%.2d	%d	plugin: ", (long)now,
		t->tm_year + 1900,
		t->tm_mon + 1,
		t->tm_mday,
		t->tm_hour,
		t->tm_min,
		t->tm_sec, line);
}

#define debugLogLine(x)								debugLog(__LINE__, x)
static void debugLogArgv(int line, int argc, char **argn, char **argv, const char *msg)
{
	int j;
	FILE *out = stderr;/*fopen("/tmp/basicPluginOutput-out.log", "a");*/
	const char *prefix = "args =";
	if (!out)
		return;
	logTimeStamp(out, line);
	fprintf(out, "%s(", msg);
	for (j = 0; argc > 0; argc--, argv++, argn++, j++, prefix = ",")
		fprintf(out, "%s %d:%s[%s]", prefix, j, *argn, *argv);
	fprintf(out, ")\n");
	fclose(out);
}

static void debugLog(int line, const char *format, ...)
{
	FILE *out = stderr;/*fopen("/tmp/basicPluginOutput-out.log", "a");*/
	if (!out)
		return;
	logTimeStamp(out, line);
	va_list(arglist);
	va_start(arglist, format);
	vfprintf(out, format, arglist);
	fprintf(out, "\n");
	fclose(out);
}

void debugLogVariant(int line, NPVariant *var, bool ok, const char *msg)
{
	if (ok) {
		if (NPVARIANT_IS_INT32(*var))
			debugLog(line, "%s = %d val:%d", msg, ok, NPVARIANT_TO_INT32(*var));
		else if (NPVARIANT_IS_DOUBLE(*var))
			debugLog(line, "%s = %d val:%f", msg, ok, NPVARIANT_TO_DOUBLE(*var));
		else if (NPVARIANT_IS_BOOLEAN(*var))
			debugLog(line, "%s = %d val:%s", msg, ok, NPVARIANT_TO_BOOLEAN(*var) ? "true" : "false");
		else if (NPVARIANT_IS_STRING(*var))
			debugLog(line, "%s = %d val:[%s]", msg, ok, NPVARIANT_TO_STRING(*var).UTF8Characters);
		else if (NPVARIANT_IS_OBJECT(*var))
			debugLog(line, "%s = %d val:%p", msg, ok, NPVARIANT_TO_OBJECT(*var));
		else
			debugLog(line, "%s = %d type:%d", msg, ok, var->type);
	} else
		debugLog(line, "%s = %d", msg, ok);
}

static void
fillPluginFunctionTable(NPPluginFuncs* pFuncs)
{
	pFuncs->version = 11;
	pFuncs->size = sizeof(*pFuncs);
	pFuncs->newp = NPP_New;
	pFuncs->destroy = NPP_Destroy;
	pFuncs->setwindow = NPP_SetWindow;
	pFuncs->newstream = NPP_NewStream;
	pFuncs->destroystream = NPP_DestroyStream;
	pFuncs->asfile = NPP_StreamAsFile;
	pFuncs->writeready = NPP_WriteReady;
	pFuncs->write = NPP_Write;
	pFuncs->print = NPP_Print;
	pFuncs->event = NPP_HandleEvent;
	pFuncs->urlnotify = NPP_URLNotify;
	pFuncs->getvalue = NPP_GetValue;
	pFuncs->setvalue = NPP_SetValue;
}

NP_EXPORT(NPError)
NP_Initialize(NPNetscapeFuncs* bFuncs, NPPluginFuncs* pFuncs)
{
	myprintf("NP_Initialize\n");

	NPNFuncs = *bFuncs;

	fillPluginFunctionTable(pFuncs);

	return NPERR_NO_ERROR;
}

NP_EXPORT(char*)
NP_GetPluginVersion()
{
	myprintf("NP_GetPluginVersion\n");
	return (char *)PLUGIN_VERSION;
}

NP_EXPORT(char*)
NP_GetMIMEDescription()
{
	myprintf("NP_GetMIMEDescription\n");
	return (char *)PLUGIN_MIMEDESCRIPTION;
}

NPObject *NPN_CreateObject(NPP npp, NPClass *aClass)
{
	return NPNFuncs.createobject(npp, aClass);
}

void NPN_ForceRedraw(NPP npp)
{
	return NPNFuncs.forceredraw(npp);
}

bool NPN_IdentifierIsString(NPIdentifier identifier)
{
	return NPNFuncs.identifierisstring(identifier);
}

NPUTF8 *NPN_UTF8FromIdentifier(NPIdentifier identifier)
{
	return NPNFuncs.utf8fromidentifier(identifier);
}

static NPObject *Object_Allocate(NPP npp,NPClass *aClass)
{
	return new MyObject(npp);
}

static void Object_Deallocate(NPObject *npobj)
{
	delete (MyObject *)npobj;
}

static bool
Object_Construct(
	NPObject *npobj,
	const NPVariant *args,
	uint32_t argCount,
	NPVariant *result
)
{
	myprintf("In Object_Construct\n");
	return false;
}

static bool ContainsName(const char **name_array,NPIdentifier name)
{
	bool is_string = NPN_IdentifierIsString(name);
	if (!is_string) {
		debugLogLine("  Identifier is not a string.");
		return false;
	}
	const char *name_str = NPN_UTF8FromIdentifier(name);
	debugLog(__LINE__, "  Identifier = %s",name_str);
	int index = 0;
	for (;;++index) {
		const char *one_name = name_array[index];
		if (!one_name) {
			break;
		}
		if (strcmp(name_str,one_name)==0) {
			return true;
		}
	}
	return false;
}


static bool Object_HasProperty(NPObject *npobj, NPIdentifier name)
{
	debugLogLine("In Object_HasProperty");
	static const char *property_names[] = {
		0
	};
	return ContainsName(property_names,name);
}


static bool Object_HasMethod(NPObject *npobj,NPIdentifier name)
{
	debugLogLine("In Object_HasMethod");
	static const char *method_names[] = {
		"sendTap","sendKey","getKey","getMouseDown","getMouseUp","getMouseMove",0
	};
	return ContainsName(method_names,name);
}


static void PrintValue(const NPVariant *arg)
{
	if (NPVARIANT_IS_VOID(*arg)) {
		fprintf(stderr,"void");
	} else if (NPVARIANT_IS_NULL(*arg)) {
		fprintf(stderr,"null");
	} else if (NPVARIANT_IS_BOOLEAN(*arg)) {
		if (NPVARIANT_TO_BOOLEAN(*arg)) {
			fprintf(stderr,"true");
		} else {
			fprintf(stderr,"false");
		}
	} else if (NPVARIANT_IS_INT32(*arg)) {
		fprintf(stderr,"%d",NPVARIANT_TO_INT32(*arg));
	} else if (NPVARIANT_IS_DOUBLE(*arg)) {
		fprintf(stderr,"%g",NPVARIANT_TO_DOUBLE(*arg));
	} else if (NPVARIANT_IS_STRING(*arg)) {
		NPString value = NPVARIANT_TO_STRING(*arg);
		const NPUTF8 *chars = value.UTF8Characters;
		uint32_t len = value.UTF8Length;
		fprintf(stderr,"\"");
		{
			uint32_t i = 0;
			for (;i!=len;++i) {
				fprintf(stderr,"%c",chars[i]);
			}
		}
		fprintf(stderr,"\"");
	} else if (NPVARIANT_IS_OBJECT(*arg)) {
		fprintf(stderr,"object");
	} else {
		fprintf(stderr,"unknown");
	}
}


static void PrintArgs(const NPVariant *args,int argCount)
{
	fprintf(stderr,"(");
	int argnum = 0;
	for (;argnum!=argCount;++argnum) {
		if (argnum!=0) {
			fprintf(stderr,",");
		}
		PrintValue(&args[argnum]);
	}
	fprintf(stderr,")");
}


static int IntValue(const NPVariant *value)
{
	if (NPVARIANT_IS_INT32(*value)) {
		int32_t value_int32 = NPVARIANT_TO_INT32(*value);
		return value_int32;
	} else if (NPVARIANT_IS_DOUBLE(*value)) {
		double value_double = NPVARIANT_TO_DOUBLE(*value);
		return (int)value_double;
	} else if (NPVARIANT_IS_BOOLEAN(*value)) {
		bool value_bool = NPVARIANT_TO_BOOLEAN(*value);
		return (int)value_bool;
	} else if (NPVARIANT_IS_STRING(*value)) {
		const NPString &value_str = NPVARIANT_TO_STRING(*value);
		const char *value_chars = value_str.UTF8Characters;
		debugLog(__LINE__, "String is %s",value_chars);
		return (int)value_chars[0];
	} else {
		debugLog(__LINE__, "Expecting int, but got type %d",value->type);
		return 0;
	}
}

void HandleKeyPress(MyObject *myobj)
{
	if (myobj->event_key_code == 'S')
		myobj->solid_lines = 1;
	else if (myobj->event_key_code == 'C')
		myobj->solid_lines = 0;
	else if (myobj->event_key_code == 'T')
		myobj->solid_lines = !myobj->solid_lines;
	else
		return;
	NPNFuncs.forceredraw(myobj->instance);
	debugLog(__LINE__, "HandleKeyPress(key:'%c' solid_lines:%d)", myobj->event_key_code, myobj->solid_lines);
}

bool
Object_Invoke(
	NPObject *npobj,
	NPIdentifier name,
	const NPVariant *args,
	uint32_t argCount,
	NPVariant *result
)
{
	MyObject *myobj = (MyObject *)npobj;
	bool is_string = NPN_IdentifierIsString(name);
	if (!is_string) {
		debugLogLine("  Object_Invoke: Method name is not a string");
		return false;
	}
	const char *name_str = NPN_UTF8FromIdentifier(name);
	fprintf(stderr,"  Object_Invoke: %s",name_str);
	PrintArgs(args,argCount);
	fprintf(stderr,"\n");
	/*if (strcmp(name_str,"sendTap")==0) {
		if (argCount<2) {
			debugLogLine("  wrong number of args");
			printf("  wrong number of args\n");
			return false;
		}
		myobj->event_x = IntValue(&args[0]);
		myobj->event_y = IntValue(&args[1]);
		//debugLog(__LINE__, "Object_Invoke(sendTap: event_x:%d, event_y:%d)", myobj->event_x, myobj->event_y);
		printf("Object_Invoke(sendTap: event_x:%d, event_y:%d)\n", myobj->event_x, myobj->event_y);
		//NPN_ForceRedraw(myobj->instance);
		return true;
	} else if (strcmp(name_str,"sendKey")==0) {
		if (argCount<1) {
			debugLogLine("  wrong number of args");
			return false;
		}
		myobj->event_key_code = IntValue(&args[0]);
		//HandleKeyPress(myobj);
		debugLog(__LINE__, "Object_Invoke(sendKey: event_key_code:%d)", myobj->event_key_code);
		printf("Object_Invoke(sendKey: event_key_code:%d)\n", myobj->event_key_code);
		return true;
	} else if (strcmp(name_str,"getKey")==0) {
		if (argCount<1) {
			debugLogLine("  wrong number of args");
			return false;
		}
		myobj->event_key_code = IntValue(&args[0]);
		//HandleKeyPress(myobj);
		//debugLog(__LINE__, "Object_Invoke(sendKey: event_key_code:%d)", myobj->event_key_code);
		printf("Object_Invoke(getKey: event_key_code:%d)\n", myobj->event_key_code);
		return true;
	} else */
	if (strcmp(name_str,"getMouseDown")==0) {
		if (argCount<6) {
			debugLogLine("  wrong number of args");
			printf("  wrong number of args\n");
			return false;
		}
		NpPalmEventType e;
		e.eventType = npPalmPenDownEvent;
		e.data.penEvent.xCoord = IntValue(&args[0]);
		e.data.penEvent.yCoord = IntValue(&args[1]);
		e.data.penEvent.modifiers = IntValue(&args[2]);

		printf("Object_Invoke(getMouseDown: x:%d, y:%d, alt:%d, ctrl:%d, shift:%d)\n", e.data.penEvent.xCoord, e.data.penEvent.yCoord, e.data.penEvent.modifiers, IntValue(&args[3]), IntValue(&args[4]), IntValue(&args[5]));
		if (myobj->input_fifo_fd) {
			write(myobj->input_fifo_fd, &e, sizeof(NpPalmEventType));
		}
		return true;
	} else if (strcmp(name_str,"getMouseUp")==0) {
		if (argCount<2) {
			debugLogLine("  wrong number of args");
			printf("  wrong number of args\n");
			return false;
		}
		NpPalmEventType e;
		e.eventType = npPalmPenDownEvent;
		e.data.penEvent.xCoord = IntValue(&args[0]);
		e.data.penEvent.yCoord = IntValue(&args[1]);
		printf("Object_Invoke(getMouseUp: x:%d, y:%d)\n", e.data.penEvent.xCoord, e.data.penEvent.yCoord);
		if (myobj->input_fifo_fd) {
			write(myobj->input_fifo_fd, &e, sizeof(NpPalmEventType));
		}
		return true;
	} else if (strcmp(name_str,"getMouseMove")==0) {
		if (argCount<2) {
			debugLogLine("  wrong number of args");
			printf("  wrong number of args\n");
			return false;
		}
		NpPalmEventType e;
		e.eventType = npPalmPenMoveEvent;
		e.data.penEvent.xCoord = IntValue(&args[0]);
		e.data.penEvent.yCoord = IntValue(&args[1]);
		printf("Object_Invoke(getMouseMove: x:%d, y:%d)\n", e.data.penEvent.xCoord, e.data.penEvent.yCoord);
		if (myobj->input_fifo_fd) {
			write(myobj->input_fifo_fd, &e, sizeof(NpPalmEventType));
		}
		return true;
	}

	return false;
}


static bool
Object_SetProperty(
	NPObject *npobj,
	NPIdentifier name,
	const NPVariant *value
)
{
	MyObject *myobj = (MyObject *)npobj;
	debugLogLine("In Object_SetProperty");
	bool is_string = NPN_IdentifierIsString(name);
	debugLog(__LINE__, "  IdentifierIsString = %d",is_string);
	if (is_string) {
		const char *name_str = NPN_UTF8FromIdentifier(name);
		debugLog(__LINE__, "  Identifier = %s",name_str);
		if (strcmp(name_str,"event_x")==0) {
			myobj->event_x = IntValue(value);
			debugLog(__LINE__, "  event_x set to %d",myobj->event_x);
		} else if (strcmp(name_str,"event_key_code")==0) {
			myobj->event_key_code = IntValue(value);
			debugLog(__LINE__, "  event_key_code set to %d",myobj->event_key_code);
		} else if (strcmp(name_str,"event_y")==0) {
			myobj->event_y = IntValue(value);
			debugLog(__LINE__, "  event_y set to %d",myobj->event_y);
		} else if (strcmp(name_str,"test_property")==0) {
			int command = IntValue(value);
			debugLog(__LINE__, "  command=%d",command);
			if (command==2) {
				NPN_ForceRedraw(myobj->instance);
			}
		} else {
			debugLogLine("Invalid property");
		}
	}
	return false;
}

static NPObject *GetScriptableObject(NPP instance)
{
	InstanceData *data = (InstanceData *)instance->pdata;
	if (!data->object) {
		static NPClass object_class;
		object_class.structVersion = NP_CLASS_STRUCT_VERSION;
		object_class.allocate = Object_Allocate;
		object_class.deallocate = Object_Deallocate;
		object_class.invalidate = 0;
		object_class.hasMethod = Object_HasMethod;
		object_class.invoke = Object_Invoke;
		object_class.invokeDefault = 0;
		object_class.hasProperty = Object_HasProperty;
		object_class.getProperty = 0;
		object_class.setProperty = Object_SetProperty;
		object_class.removeProperty = 0;
		object_class.enumerate = 0;
		object_class.construct = Object_Construct;

		data->object =
			(MyObject *)NPN_CreateObject(
				instance,
				&object_class
			);
	}

	return data->object;
}

NP_EXPORT(NPError)
NP_GetValue(NPP instance, NPPVariable aVariable, void* aValue)
{
	debugLog(__LINE__, "NP_GetValue: [%s (#%i)]", varname(aVariable), aVariable);

	switch (aVariable) {
	case NPPVpluginNameString:
		*((const char**)aValue) = PLUGIN_NAME;
		break;
	case NPPVpluginDescriptionString:
		*((const char**)aValue) = PLUGIN_DESCRIPTION;
		break;
	default:
		return NPERR_INVALID_PARAM;
		break;
	}
	return NPERR_NO_ERROR;
}

NP_EXPORT(NPError)
NP_Shutdown()
{
	myprintf("NP_Shutdown\n");
	return NPERR_NO_ERROR;
}

NPError
NPP_New(NPMIMEType pluginType, NPP instance, uint16_t mode, int16_t argc, char* argn[], char* argv[], NPSavedData* saved)
{
	debugLogArgv(__LINE__, argc, argn, argv, "NPP_New");
	myprintf("NPP_New([%s], mode:%d, argc:%d, instance:[pdata:%p ndata:%p])\n", pluginType, mode, argc, instance->pdata, instance->ndata);
	int i;
	char *title;
	for(i=0; i<argc; i++) {
		myprintf("%s:=%s \n", argn[i], argv[i]);
		if (strcmp(argn[i], "title") == 0)
			title = argv[i];
	}
	instance->pdata = new InstanceData;
	//	XXX - We are changing the size of the Plugin area
	//setSize(instance, /*width*/-1, /*height*/3000);
	return NPERR_NO_ERROR;
}

NPError
NPP_Destroy(NPP instance, NPSavedData** save)
{
	myprintf("NPP_Destroy(instance:[pdata:%p ndata:%p])", instance->pdata, instance->ndata);
	InstanceData* instanceData = (InstanceData*)(instance->pdata);
	if (instanceData) {
		delete instanceData;
	}
	return NPERR_NO_ERROR;
}

NPError
NPP_SetWindow(NPP instance, NPWindow* window)
{
	if (window && window->window) {
		InstanceData *data = (InstanceData *)instance->pdata;
  		MyObject *myobj_ptr = data->object;
  		if (!myobj_ptr) {
    		return NPERR_NO_ERROR;
  		}
		if (!myobj_ptr->window_received)
			myobj_ptr->window_received = true;
		myprintf("NPP_SetWindow(window:%p, ws_info %p, x:%d, y:%d, width:%d, height:%d, type:%d)", window->window, window->ws_info, window->x, window->y, window->width, window->height, window->type);}
	else		//	Destroying window
		debugLog(__LINE__, "NPP_SetWindow(window:%p)", window);
	return NPERR_NO_ERROR;
}

NPError
NPP_NewStream(NPP instance, NPMIMEType type, NPStream* stream, NPBool seekable, uint16_t* stype)
{
	debugLogLine("NPP_NewStream");
	return NPERR_GENERIC_ERROR;
}

NPError
NPP_DestroyStream(NPP instance, NPStream* stream, NPReason reason)
{
	debugLogLine("NPP_DestroyStream");
	return NPERR_GENERIC_ERROR;
}

int32_t
NPP_WriteReady(NPP instance, NPStream* stream)
{
	debugLogLine("NPP_WriteReady");
	return 0;
}

int32_t
NPP_Write(NPP instance, NPStream* stream, int32_t offset, int32_t len, void* buffer)
{
	debugLogLine("NPP_Write");
	return 0;
}

void
NPP_StreamAsFile(NPP instance, NPStream* stream, const char* fname)
{
	debugLogLine("NPP_StreamAsFile");
}

void
NPP_Print(NPP instance, NPPrint* platformPrint)
{
	debugLogLine("NPP_Print");
}

int setSize(NPP instance, int width, int height)
{
	NPError err;
	NPObject *obj = 0;
	//debugLog(__LINE__, "setSize: bSize:%d, bVersion:%d", NPNFuncs.size, NPNFuncs.version);

	err = NPNFuncs.getvalue(instance, NPNVPluginElementNPObject, &obj);
	NPIdentifier heightIdentifier	= NPNFuncs.getstringidentifier("height");
	NPIdentifier widthIdentifier	= NPNFuncs.getstringidentifier("width");
	debugLog(__LINE__, "	setSize: PluginElementNPObject:%p, heightId:%p, widthId:%p, err:%d", obj, heightIdentifier, widthIdentifier, err);
	if (err)
		return err;
	if (!obj || !heightIdentifier || !widthIdentifier)
		return NPERR_GENERIC_ERROR;

	bool hasPropertyH	= NPNFuncs.hasproperty(instance, obj, heightIdentifier);
	bool hasMethodH		= NPNFuncs.hasmethod(instance, obj, heightIdentifier);
	bool hasPropertyW	= NPNFuncs.hasproperty(instance, obj, widthIdentifier);
	bool hasMethodW		= NPNFuncs.hasmethod(instance, obj, widthIdentifier);
	debugLog(__LINE__, "	setSize: hasPropertyH:%d hasMethodH:%d hasPropertyW:%d hasMethodW:%d", hasPropertyH, hasMethodH, hasPropertyW, hasMethodW);

	if ((!hasPropertyH && !hasMethodH) || (!hasPropertyW && !hasMethodW))
		return NPERR_GENERIC_ERROR;
	NPVariant var;
	bool ok;

#if 1
	ok = NPNFuncs.getproperty(instance, obj, heightIdentifier, &var);
	debugLogVariant(__LINE__, &var, ok, "	setSize: 1 - NPN_GetProperty(height)");
	ok = NPNFuncs.getproperty(instance, obj, widthIdentifier, &var);
	debugLogVariant(__LINE__, &var, ok, "	setSize: 1 - NPN_GetProperty(width)");
#endif

	if (height >= 0) {
		INT32_TO_NPVARIANT(height, var);
		ok = NPNFuncs.setproperty(instance, obj, heightIdentifier, &var);
		debugLog(__LINE__, "	setSize: NPN_SetProperty(height:%d) = %s", height, ok ? "OK" : "FAILED");
		if (!ok)
			return NPERR_GENERIC_ERROR;
	}
	if (width >= 0) {
		INT32_TO_NPVARIANT(width, var);
		ok = NPNFuncs.setproperty(instance, obj, widthIdentifier, &var);
		debugLog(__LINE__, "	setSize: NPN_SetProperty(width:%d) = %s", width, ok ? "OK" : "FAILED");
		if (!ok)
			return NPERR_GENERIC_ERROR;
	}
#if 1
	ok = NPNFuncs.getproperty(instance, obj, heightIdentifier, &var);
	debugLogVariant(__LINE__, &var, ok, "	setSize: 2 - NPN_GetProperty(height)");
	ok = NPNFuncs.getproperty(instance, obj, widthIdentifier, &var);
	debugLogVariant(__LINE__, &var, ok, "	setSize: 2 - NPN_GetProperty(width)");
#endif
	return err;
}

void HandleDrawEvent(NpPalmDrawEvent *drawEvent,NPP instance)
{
	myprintf("HandleDrawEvent\n");
	myprintf("drawEvent sl: %i sr: %i st: %i sb: %i dl: %i dr: %i dt: %i db: %i\n", 
		drawEvent->srcLeft, 
		drawEvent->srcRight, 
		drawEvent->srcTop, 
		drawEvent->srcBottom, 
		drawEvent->dstLeft, 
		drawEvent->dstRight, 
		drawEvent->dstTop, 
		drawEvent->dstBottom
	); 
	// PGContextIface *iface = (PGContextIface *) drawEvent->graphicsContext ;
	// iface->clearRect(5,5,20,20);
	void *screenBuffer =  drawEvent->dstBuffer;

	InstanceData *data = (InstanceData *)instance->pdata;
	MyObject *myobj_ptr = data->object;
	if (!myobj_ptr) {
  		return;
	}
	MyObject &myobj = *myobj_ptr;
	pthread_mutex_lock(&myobj.draw_mutex);

	int dst_width = drawEvent->dstRight-drawEvent->dstLeft;
	int dst_height = drawEvent->dstBottom-drawEvent->dstTop;

	if (/*!myobj.dfbmem_mapped 
		||*/ !myobj.window_received 
		|| myobj.draw_serial_handler+1 != myobj.draw_serial_listener) {
		myprintf("dfbmem un-mapped yet or window not received yet\n\
				or serials of listener and handler not match\n\t \
				dfbmem_mapped:%d window_received %d listener:%lu handler:%lu\n", 
				myobj.dfbmem_mapped, myobj.window_received,
				myobj.draw_serial_listener, myobj.draw_serial_handler);
		if (myobj.draw_serial_listener > 0) {
			FILE *fmem;
			fmem = fopen(myobj.dfb_primarymem_name, "r");
			assert(fmem!=NULL);
	
			fseek(fmem, myobj.dfb_draw_request.offset, SEEK_SET);
			fread(drawEvent->dstBuffer, 4,  dst_width*dst_height, fmem);
			fclose(fmem);
		}
		goto exit;
	}

	/*if (!myobj.dfbmem_mapped) {
						int dfbmem_fd = open(myobj.dfb_primarymem_name, O_RDONLY);
						if (dfbmem_fd < 0) {
							myprintf("Couldn't open dfbmem file!\n");
							//continue;
							return;
						}
						myobj.mapped_dfbmem_addr = mmap( NULL, PRIMARY_MEM_SIZE, 
												PROT_READ, MAP_SHARED, dfbmem_fd, 0 );
						if (myobj.mapped_dfbmem_addr == MAP_FAILED) {
							myprintf( "Mapping dfbmem file failed!\n" );
							close( dfbmem_fd );
							return;
						}
						close( dfbmem_fd );
					
						myobj.dfbmem_mapped = true;
						myprintf("dfb mem has been mapped to addr %p", myobj.mapped_dfbmem_addr);
					}*/


	myprintf("come to memcpy to screenbuffer wow w:%d h:%d\n", dst_width, dst_height);
	//memcpy(screenBuffer, (unsigned long*)myobj.mapped_dfbmem_addr + myobj.dfb_draw_request.offset, 
	//		myobj.dfb_draw_request.pitch * 450 /*myobj.dfb_draw_request.h*/);
	
	myprintf("dfb surface pitch = %d offset = %d\n", myobj.dfb_draw_request.pitch, myobj.dfb_draw_request.offset);
	//unsigned int *src, *dst;
	//src = (unsigned int*)myobj.mapped_dfbmem_addr + myobj.dfb_draw_request.offset;
	FILE *fmem;
	fmem = fopen(myobj.dfb_primarymem_name, "r");
	assert(fmem!=NULL);
	
	fseek(fmem, myobj.dfb_draw_request.offset, SEEK_SET);
	fread(drawEvent->dstBuffer, 4,  dst_width*dst_height, fmem);
	fclose(fmem);

	//dst = (unsigned int*)drawEvent->dstBuffer;
	//int i;
	//for (i=0; i< dst_width*dst_height; i++) {
		//myprintf("0x%x ", src[i]);
		//dst[i] = src[i];
		//dst[i] = src[i] & 0x00FFFFFF;
		/*unsigned int h = 0;
		int j = 0;
		
		for(h = j = 0; j < 32; j++) {
			h = (h << 1) + (dst[i] & 1); 
			dst[i] >>= 1; 
 		}
		dst[i] = h;*/
		//dst[i] = (src[i] << 8) & 0xffffff00  + (src[i] >> 24) & 0x000000ff;
	//}

	myprintf("done memcpy to screenbuffer wow!\n");
	
	myobj.draw_serial_handler++;

	pthread_cond_signal(&myobj.drawn_cond);
	myprintf("Done drawing\n");

exit:	
	pthread_mutex_unlock(&myobj.draw_mutex);
}


int16_t
NPP_HandleEvent(NPP instance, void* aEvent)
{
	NpPalmEventType *palmEvent = (NpPalmEventType *) aEvent;
	/*switch (palmEvent->eventType) {
	case npPalmPenDownEvent:
		debugLog(__LINE__, "npPalmPenDownEvent(x:%d, y:%d, mod:%d)",	palmEvent->data.penEvent.xCoord, palmEvent->data.penEvent.yCoord, palmEvent->data.penEvent.modifiers);
		break;
	case npPalmPenUpEvent:
		debugLog(__LINE__, "npPalmPenUpEvent(x:%d, y:%d, mod:%d)",		palmEvent->data.penEvent.xCoord, palmEvent->data.penEvent.yCoord, palmEvent->data.penEvent.modifiers);
		break;
	case npPalmPenMoveEvent: {
		InstanceData *data = (InstanceData *)instance->pdata;
		MyObject *myobj = data->object;
		debugLog(__LINE__, "npPalmPenMoveEvent(x:%d, y:%d, mod:%d, event_x:%d, event_y:%d)",	palmEvent->data.penEvent.xCoord, palmEvent->data.penEvent.yCoord, palmEvent->data.penEvent.modifiers, myobj->event_x, myobj->event_y);
	}
	break;
	case npPalmKeyDownEvent:
		debugLog(__LINE__, "npPalmKeyDownEvent(chr:%d, mod:%d, rawKey:%d, rawMod:%d)",		palmEvent->data.keyEvent.chr, palmEvent->data.keyEvent.modifiers, palmEvent->data.keyEvent.rawkeyCode, palmEvent->data.keyEvent.rawModifier);
		break;
	case npPalmKeyUpEvent:
		debugLog(__LINE__, "npPalmKeyUpEvent(chr:%d, mod:%d, rawKey:%d, rawMod:%d)",		palmEvent->data.keyEvent.chr, palmEvent->data.keyEvent.modifiers, palmEvent->data.keyEvent.rawkeyCode, palmEvent->data.keyEvent.rawModifier);
		break;
	case npPalmKeyRepeatEvent:
		debugLog(__LINE__, "npPalmKeyRepeatEvent(chr:%d, mod:%d, rawKey:%d, rawMod:%d)",	palmEvent->data.keyEvent.chr, palmEvent->data.keyEvent.modifiers, palmEvent->data.keyEvent.rawkeyCode, palmEvent->data.keyEvent.rawModifier);
		break;
	case npPalmKeyPressEvent:
		debugLog(__LINE__, "npPalmKeyPressEvent(chr:%d, mod:%d, rawKey:%d, rawMod:%d)",	palmEvent->data.keyEvent.chr, palmEvent->data.keyEvent.modifiers, palmEvent->data.keyEvent.rawkeyCode, palmEvent->data.keyEvent.rawModifier);
		break;
	case npPalmDrawEvent:
		//debugLogLine("NPP_HandleEvent: npPalmDrawEvent");
		HandleDrawEvent(&palmEvent->data.drawEvent,instance);
		break;
	default:
		debugLog(__LINE__, "NPP_HandleEvent: unknown (%d)", palmEvent->eventType);
		break;
	}*/
	if (palmEvent->eventType == npPalmDrawEvent) {
		//debugLogLine("NPP_HandleEvent: npPalmDrawEvent");
		HandleDrawEvent(&palmEvent->data.drawEvent,instance);
	} else {
		printf("plugin NPP_HandleEvent: type %d\n", palmEvent->eventType);
		if (palmEvent->eventType == npPalmKeyDownEvent)
			printf("npPalmKeyDownEvent(chr:%d, mod:%d, rawKey:%d, rawMod:%d)",		palmEvent->data.keyEvent.chr, palmEvent->data.keyEvent.modifiers, palmEvent->data.keyEvent.rawkeyCode, palmEvent->data.keyEvent.rawModifier);

		if (palmEvent->eventType == npPalmKeyDownEvent || palmEvent->eventType == npPalmKeyUpEvent) {
			InstanceData *data = (InstanceData *)instance->pdata;
			MyObject *myobj = data->object;
			if (myobj->input_fifo_fd) {
				//printf("begin to write input fifo\n");
				write(myobj->input_fifo_fd, palmEvent, sizeof(NpPalmEventType));
				//printf("finish writing input fifo\n");
			}	
		}
	}	

	return 1;
}

void
NPP_URLNotify(NPP instance, const char* URL, NPReason reason, void* notifyData)
{
	debugLogLine("NPP_URLNotify");
}

const char * varname(NPPVariable variable)
{
	switch (variable) {
	case NPPVpluginNameString:
		return "NPPVpluginNameString";
		break;
	case NPPVpluginDescriptionString:
		return "NPPVpluginDescriptionString";
		break;
	case NPNVnetscapeWindow:
		return "NPNVnetscapeWindow";
		break;
	case NPPVpluginScriptableIID:
		return "NPPVpluginScriptableIID";
		break;
	case NPPVjavascriptPushCallerBool:
		return "NPPVjavascriptPushCallerBool";
		break;
	case NPPVpluginKeepLibraryInMemory:
		return "NPPVpluginKeepLibraryInMemory";
		break;
	case NPPVpluginScriptableNPObject:
		return "NPPVpluginScriptableNPObject";
		break;
	case NPPVpluginNeedsXEmbed:
		return "NPPVpluginNeedsXEmbed";
		break;
	case npPalmEventLoopValue:
		return "npPalmEventLoopValue";
		break;
	case npPalmCachePluginValue:
		return "npPalmCachePluginValue";
		break;
	case npPalmApplicationIdentifier:
		return "npPalmApplicationIdentifier";
		break;
	default:
		return "unknown";
		break;
	}
}

NPError
NPP_GetValue(NPP instance, NPPVariable variable, void *value)
{
	logmsgv("NPP_GetValue",variable);
	switch (variable) {
	case NPPVpluginScriptableNPObject:
		*(NPObject **)value = GetScriptableObject(instance);
		break;
	default:
		return NPERR_GENERIC_ERROR;
	}
	return NPERR_NO_ERROR;
}

NPError
NPP_SetValue(NPP instance, NPNVariable variable, void *value)
{
	debugLogLine("NPP_SetValue");
	// logmsgv("NPP_SetValue",variable);
	return NPERR_GENERIC_ERROR;
}
