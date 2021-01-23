//g++ winrund.cpp -fno-inline -o winrund -pthread -lstdc++fs -std=c++17

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <fstream>
#include <sstream>
#include <climits>
#include <thread>
#include <signal.h>
#include <filesystem>

bool* isBusy;
int basePort;
const int bufsize=4096;
const int ms=1000;

std::string getProcName(int procID)
{
	char* name = (char*)calloc(1024,sizeof(char));
	std::string returnName;

	if(name)
	{
		sprintf(name, "/proc/%d/cmdline",procID);
		FILE* f = fopen(name,"r");

		if(f)
		{
			size_t size;
			size = fread(name, sizeof(char), 1024, f);

			if(size>0)
			{
				if(name[size-1]=='\n')
				{
					name[size-1]='\0';
				}
				fclose(f);
			}
		}
	}
	returnName=std::string(name);
	free(name);
	return returnName;
}
bool fexists(const char *filename)
{
	std::ifstream ifile(filename);
	return (bool)ifile;
}
bool dexists(const char *directory)
{
	struct stat st;
	if(stat(directory,&st) == 0)
	{
		return !(bool)(st.st_mode & S_IFDIR);
	}
	else
	{
		return false;
	}
}
int initClient(std::string addr, int p)
{
	int sock=socket(AF_INET,SOCK_STREAM,0);
	int connectRes=INT16_MAX;
	const int one=1;

	//Create a hint structure for the server we're connecting with
	sockaddr_in hint;
	hint.sin_family=AF_INET;
	hint.sin_port=htons(p);
	inet_pton(AF_INET,addr.c_str(),&hint.sin_addr);

	if(sock==-1)
	{
		syslog(LOG_ERR,"Unable to create socket on port %d",p);
		exit(EXIT_FAILURE);
	}
	else
	{
		syslog(LOG_NOTICE,"Successfully initialized socket %d on port %d",sock,p);
	}

	//Connect to the server on the socket
	connectRes=connect(sock,(sockaddr*)&hint,sizeof(hint));
	if(connectRes==-1)
	{
		syslog(LOG_ERR,"Could not connect to %s:%d",addr.c_str(),p);
		exit(EXIT_FAILURE);
	}
	else
	{
		syslog(LOG_NOTICE,"Successfully connected to %s:%d",addr.c_str(),p);
	}

	return sock;
}
int initServer(int p)
{
	sockaddr_in hint;
	sockaddr_in client;
	socklen_t clientSize=sizeof(client);
	int listening,clientSocket;
	const int one=1;
	char host[NI_MAXHOST];		//Client name
	char service[NI_MAXSERV];	//Service the client is connected on	

	//Initialize listening socket
	listening=socket(AF_INET, SOCK_STREAM, 0);
	if(listening==-1)
	{
		syslog(LOG_ERR, "Unable to create socket! Quitting...");
		exit(EXIT_FAILURE);
	}
	else
	{
		syslog(LOG_NOTICE, "Created socket %d for port %d", listening, p);
	}

	//Set socket to be reusable
	setsockopt(listening,SOL_SOCKET,SO_REUSEADDR,&one,(socklen_t)sizeof(one));

	hint.sin_family=AF_INET;
	hint.sin_port=htons(p);
	inet_pton(AF_INET, "0.0.0.0", &hint.sin_addr);

	bind(listening, (sockaddr*)&hint, sizeof(hint));

	//Set socket for listening
	syslog(LOG_NOTICE, "Initialized listening server on port %d...", p);
	listen(listening, SOMAXCONN);

	clientSocket=accept(listening, (sockaddr*)&client, &clientSize);

	if(clientSocket==-1)
	{
		syslog(LOG_ERR, "Invalid socket! Quitting...");
		exit(EXIT_FAILURE);
	}
	memset(host,0,NI_MAXHOST);
	memset(service,0,NI_MAXSERV);

	//Attempt to resolve client machine name, otherwise resort to IP
	if(getnameinfo((sockaddr*)&client, sizeof(client), host, NI_MAXHOST, service, NI_MAXSERV, 0)==0)
	{
		syslog(LOG_NOTICE, "%s connected on port %s",host,service);
	}
	else
	{
		inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);

		syslog(LOG_NOTICE, "%s connected on port %d", host, ntohs(client.sin_port));
	}

	//Close listening socket
	close(listening);

	return clientSocket;
}
void writeLog(int winrun_sock,bool verbose, int priority, std::string id, const char* format, ...)
{
	std::string header;
	va_list args;
	va_start(args, format);

	char text[256];
	vsprintf(text,format,args);
	syslog(priority,text);

	if(verbose)
	{
		if(priority==LOG_ERR)		//Red
		{
			header="\e[31;1m";
		}
		else if(priority==LOG_WARNING)	//Yellow
		{
			header="\e[33;1m";
		}
		else if(priority==LOG_INFO)	//White
		{
			header="\e[37;1m";
		}
		else				//LOG_NOTICE, Green
		{
			header="\e[32;1m";
		}

		send(winrun_sock,(header+text+"\e[00m").c_str(),((header+text+"\e[00m").length()+1),0);
	}

	va_end(args);
}
int waitForTimeout(std::string id, int svr_sock,int winrun_sock, unsigned long long int secs, std::string action, bool v)
{
	int rv;
	fd_set readfds, masterfds;
	timeval timeout;

	timeout.tv_sec=secs;
	timeout.tv_usec=0;
	FD_ZERO(&masterfds);
	FD_SET(svr_sock,&masterfds);
	memcpy(&readfds,&masterfds,sizeof(fd_set));
	rv=select(svr_sock+1,&readfds,NULL,NULL,&timeout);

	if(rv==SO_ERROR)
	{
		writeLog(winrun_sock,v,LOG_ERR,id,"Socket error during select() on PID %s",id.c_str());
	}
	else if(rv==0)
	{
		writeLog(winrun_sock,v,LOG_ERR,id,"Timeout (>%d seconds) while waiting for %s for PID %s",secs,action.c_str(),id.c_str());
	}

	return rv;
}
void sendData(std::string cmdID, std::string commandstr, std::string bCode, int svr_sock, int winrun_sock, int p, unsigned long long int t, bool v)
{
	int sendRes, bytesReceived, timeoutRes;
	int err=0;
	char dataBuffer[bufsize];
	std::string recvStr;

	//Send command
	do
	{
        	sendRes=send(svr_sock,(bCode+cmdID+commandstr).c_str(),((bCode+cmdID+commandstr).length()+1),0);
              	if(sendRes==-1)
		{
			writeLog(winrun_sock,v,LOG_WARNING,cmdID,"Could not send command to server! Sleeping for 100ms...");
                	usleep(100*ms);
		}
        }while(sendRes==-1);

	//Wait for response and exit program when break code is recieved.
	while(true)
	{
		//If the relavent instance of winrun is still running
		if(kill(stoi(cmdID),0)==0&&std::string(getProcName(stoi(cmdID))).find("winrun")!=std::string::npos)
		{
			memset(dataBuffer,0,bufsize);

			timeoutRes=waitForTimeout(cmdID,svr_sock,winrun_sock,t,"output data from server",v);

			if(timeoutRes==SO_ERROR || timeoutRes==0)
			{
				return;
			}
			else
			{
				bytesReceived=recv(svr_sock,dataBuffer,bufsize,0);
				if(bytesReceived>=0)
				{
					recvStr=std::string(dataBuffer,bytesReceived);
				}
				else
				{
					err=errno;
					std::ofstream writestream;
					writestream.open(("/dev/shm/winrund-"+cmdID+".dump").c_str());
					for(int i=0; i<bytesReceived; i++)
					{
						writestream<<dataBuffer[i];
					}
					writestream.close();
					syslog(LOG_ERR,"recv() error when getting output data from server for PID %s. Error code: %d",cmdID.c_str(),err);
				}

				if(recvStr.find(bCode)==std::string::npos)
				{
					//Send output to winrun
					send(winrun_sock,recvStr.c_str(),recvStr.length()+1,0);

					//Forward continue signal from winrun to winrun_svr
					memset(dataBuffer,0,bufsize);
					timeoutRes=waitForTimeout(cmdID,winrun_sock,winrun_sock,t,"continue signal",v);
					bytesReceived=recv(winrun_sock,dataBuffer,bufsize,0);
					recvStr=std::string(dataBuffer,bytesReceived);

					if(timeoutRes==SO_ERROR || timeoutRes==0)
					{
						return;
					}

					send(svr_sock,recvStr.c_str(),recvStr.length()+1,0);
				}
				else
				{
					//If find break code, write PID 5 times for end of command signal
					send(winrun_sock,bCode.c_str(),bCode.length()+1,0);
					return;
				}
			}
		}
		else
		{
			syslog(LOG_NOTICE,"winrun not found for PID %s, stopping command output",cmdID.c_str());
			return;
		}
	}
}
int winrund_check(std::string IP,int svr_port,int operator_port,int maxThreads)
{
	int checkSock,winrunSock;
	int bytesReceived,timeoutRes,pid;
	int threadSock[maxThreads];
	char buf[bufsize];
	char dataBuffer[bufsize];

	//Initialize connection to winrun_svr
	checkSock=initClient(IP,svr_port);

	//Enter daemon loop
	while(1)
	{
		winrunSock=initServer(operator_port);
		memset(dataBuffer,0,bufsize);
		pid=stoi(std::string(dataBuffer,0,recv(winrunSock,dataBuffer,bufsize,0)));

		for(int i=1; i<=maxThreads; i++)
		{
			if(!isBusy[i])
			{
				syslog(LOG_INFO,"Assigning %d to thread %d (port %d)",pid,i,(svr_port+i));
				isBusy[i]=true;
				send(winrunSock,std::to_string(operator_port+i).c_str(),std::to_string(operator_port+i).length()+1,0);
				close(winrunSock);
				break;
			}

			if(i==maxThreads)
			{
				i=0;
				syslog(LOG_WARNING,"All child threads busy, waiting 100ms...");
				sleep(100*ms);
			}
		}
	}
}
int winrund_child(std::string IP,int svr_port,int operator_port)
{
	bool verbose=false;
	int childSock, winrunSock;
	int id;
	unsigned long long int timeout;
	char buf[bufsize];
	std::string command, breakCode,timeoutstr;
	std::ifstream readstream;
	std::ofstream writestream;

	//Initialize connection to winrun_svr
	childSock=initClient(IP,svr_port);

	//Recieve break code
	breakCode=std::string(buf,recv(childSock,buf,bufsize,0));

	//Enter daemon loop
	while(1)
	{
		winrunSock=initServer(operator_port);
		send(winrunSock,breakCode.c_str(),(breakCode.length()+1),0);

		//Get command information
		memset(buf,0,bufsize);
		id=stoi(std::string(buf,0,recv(winrunSock,buf,bufsize,0)));
		send(winrunSock,std::string("1").c_str(),(std::string("1").length()+1),0);

		memset(buf,0,bufsize);
		command=std::string(buf,0,recv(winrunSock,buf,bufsize,0));
		send(winrunSock,std::string("1").c_str(),(std::string("1").length()+1),0);

		memset(buf,0,bufsize);
		timeout=stoull(std::string(buf,recv(winrunSock,buf,bufsize,0)));
		send(winrunSock,std::string("1").c_str(),(std::string("1").length()+1),0);

		memset(buf,0,bufsize);
		std::istringstream(std::string(buf,0,recv(winrunSock,buf,bufsize,0)))>>std::boolalpha>>verbose;

		syslog(LOG_INFO,"Sending command \"%s\" for PID %d over port %d",command.c_str(),id,svr_port);
		sendData(std::to_string(id),("\""+command+"\""),breakCode,childSock,winrunSock,svr_port,timeout,verbose);
		syslog(LOG_INFO,"\"%s\" has completed for PID %d",command.c_str(),id);

		close(winrunSock);	

		isBusy[svr_port-basePort]=false;
	}
}
int main(void)
{
	//	FILE NOMENCLATURE
	//
	//	"number"  = For child thread
	//	"_number" = For winrun
	//	"number_" = For controller thread

	//Define variables
	bool* verbose;
	pid_t pid, sid;
	int ctr, maxThreads,socket,operatorPort;
	int* id;
	unsigned long long int* timeout;
	char dataBuffer[bufsize]="";
	std::string ip="";
	std::string configpath="/etc/winrund/config";
	std::string idstr="";
	std::string recvStr="";
	std::string line="";
	std::string timeoutstr="";
	std::string* writeBuffer;
	std::string* command;
	std::ifstream configReader;
	std::ifstream outReader;
	std::ofstream outWriter;
	
	//Fork the current process
	pid = fork();

	//The parent process continues with a process ID greater than 0
	if(pid > 0)
	{
		exit(EXIT_SUCCESS);
	}
	//A process ID lower than 0 indicates a failure in either process
	else if(pid < 0)
	{
		exit(EXIT_FAILURE);
	}
	//The parent process has now terminated, and the forked child process will continue (the pid of the child process was 0)

	//Since the child process is a daemon, the umask needs to be set so files and logs can be written
	umask(0);

	//Open system logs for the child process
	openlog("winrund", LOG_NOWAIT | LOG_PID, LOG_USER);
	syslog(LOG_NOTICE, "Successfully started winrund");
	
	//Generate a session ID for the child process
	sid = setsid();

	//Ensure a valid SID for the child process
	if(sid < 0)
	{
		//Log failure and exit
		syslog(LOG_ERR, "Could not generate session ID for child process");

		//If a new session ID could not be generated, we must terminate the child process or it will be orphaned
		exit(EXIT_FAILURE);
	}

	//Change the current working directory to a directory guaranteed to exist
	if((chdir("/")) < 0)
	{
		//Log failure and exit
		syslog(LOG_ERR, "Could not change working directory to \"/\"");

		//If our guaranteed directory does not exist, terminate the child process to ensure
		//the daemon has not been hijacked
		exit(EXIT_FAILURE);
	}

	//A daemon cannot use the terminal, so close standard file descriptors for security reasons
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	//---------------------------------------------------------------------------------------//
	//				Daemon-specific intialization				 //
	//---------------------------------------------------------------------------------------//

	//Get configuration information
	try
	{
		configReader.open(configpath);

		while(!configReader.eof())
		{
			getline(configReader,line);

			if(line.substr(0,1)!="#")//Hashtag denotes comments
			{
				if(line.find("ip")==0)
				{
					ip=line.substr(line.find("=")+1,line.length()-line.find("="));
				}
				else if(line.find("threads")==0)
				{
					maxThreads=stoi(line.substr(line.find("=")+1,line.length()-line.find("=")));

					verbose=new bool[maxThreads];
					id=new int[maxThreads];
					timeout=new unsigned long long int[maxThreads];
					writeBuffer=new std::string[maxThreads];
					command=new std::string[maxThreads];
					isBusy=new bool[maxThreads];

					for(int i=0; i<maxThreads; i++)
					{
						verbose[i]=false;
						isBusy[i]=false;
					}
				}
				else if(line.find("baseport")==0)
				{
					basePort=stoi(line.substr(line.find("=")+1,line.length()-line.find("=")));
				}
				else if(line.find("operatorport")==0)
				{
					operatorPort=stoi(line.substr(line.find("=")+1,line.length()-line.find("=")));
				}
			}
		}
	}
	catch(...)
	{
		syslog(LOG_ERR,"Error while attempting to read config file at \"%s\"",configpath.c_str());
		exit(EXIT_FAILURE);
	}
	configReader.close();

	//Spawn child threads
	std::thread winrund_check_thread(winrund_check,ip,basePort,operatorPort,maxThreads);
	winrund_check_thread.detach();
	for(int i=1; i<=maxThreads; i++)
	{
		std::thread winrund_child_thread(winrund_child,ip,(basePort+i),(operatorPort+i));
		winrund_child_thread.detach();
	}
	
	while(true)
	{
		sleep(1);
	}

	//Close system logs for the child process
	syslog(LOG_NOTICE, "Stopping winrund");
	closelog();

	//Terminate the child process when the daemon completes
	exit(EXIT_SUCCESS);
}
