//g++ winrun.cpp -o winrun -lstdc++fs -std=c++17

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstdarg>
#include <fstream>
#include <time.h>
#include <filesystem>
#include <climits>
#include <signal.h>
#include <netdb.h>
#include <arpa/inet.h>

bool verbose=false;
const int ms=1000;
const int bufsize=4096;
enum level { error, warning, info, notice};

bool dexists(const char *directory)
{
	struct stat st;
	if(stat(directory,&st) == 0)
	{
		return true;
	}
	else
	{
		return false;
	}
}
bool fexists(const char *filename)
{
	std::ifstream ifile(filename);
	return (bool)ifile;
}
void output(level priority, const char* format, ...)
{
	std::string header;
	va_list args;
	va_start(args, format);

	char text[256];
	vsprintf(text,format,args);

	if(verbose)
	{
		if(priority==warning)		//Yellow
		{
			header="\e[33;1m";
		}
		else if(priority==info)		//Cyan
		{
			header="\e[37;1m";
		}
		else				//LOG_NOTICE, Green
		{
			header="\e[32;1m";
		}
	}
	if(priority==error)
	{
		header="\e[31;1m";
		fprintf(stderr,"%s%s\e[00m",header.c_str(),text);
	}
	else if(verbose)
	{
		fprintf(stdout,"%s%s\e[00m",header.c_str(),text);
	}

	va_end(args);
}
int initClient(int p)
{
	int sock=socket(AF_INET,SOCK_STREAM,0);
	int connectRes=INT16_MAX;
	int err=0;
	std::string addr="127.0.0.1";

	//Create a hint structure for the server we're connecting with
	sockaddr_in hint;
	hint.sin_family=AF_INET;
	hint.sin_port=htons(p);
	inet_pton(AF_INET,addr.c_str(),&hint.sin_addr);

	if(sock==-1)
	{
		err=errno;
		output(error,"Unable to create socket on port %d. Error: %d\n",p,err);
		exit(EXIT_FAILURE);
	}
	else
	{
		output(notice,"Successfully initialized socket %d on port %d\n",sock,p);
	}

	//Connect to the server on the socket
	connectRes=connect(sock,(sockaddr*)&hint,sizeof(hint));
	if(connectRes==-1)
	{
		err=errno;
		output(error,"Could not connect to winrund on port %d. Error: %d\n",p,err);
		exit(EXIT_FAILURE);
	}
	else
	{
		output(notice,"Successfully connected to winrund on port %d\n",addr.c_str(),p);
	}

	return sock;
}
int waitForTimeout(int s, unsigned long long int secs, std::string action)
{
	int rv;
	fd_set readfds, masterfds;
	timeval timeout;
	timeout.tv_sec=secs;
	timeout.tv_usec=0;
	FD_ZERO(&masterfds);
	FD_SET(s,&masterfds);
	memcpy(&readfds,&masterfds,sizeof(fd_set));
	rv=select(s+1,&readfds,NULL,NULL,&timeout);

	if(rv==SO_ERROR)
	{
		output(error,"Socket error during select()\n");
		exit(EXIT_FAILURE);
	}
	else if(rv==0)
	{
		output(error,"Timeout (>%d seconds) while waiting for %s\n",secs,action.c_str());
		exit(EXIT_FAILURE);
	}

	return rv;
}
int main(int argc, char** argv)
{
	pid_t pid=getpid();
	int timeout=5;
	int socket=0;
	int port=0;
	int operatorPort=0;
	int timeoutRes=0;
	int bytesReceived=0;
	char dataBuffer[bufsize]="";
	std::string line;
	std::string command;
	std::string breakCode;
	std::string continueSig=std::string(std::to_string(pid)+"-1");
	std::string configpath="/etc/winrund/config";
	std::ifstream readstream;
	std::ofstream writestream;

	//Check if daemon is running before continuing
	system("systemctl status winrund | grep 'Active:' > /dev/shm/winrun_status");
	readstream.open("/dev/shm/winrun_status");
	getline(readstream,line);
	readstream.close();
	remove("/dev/shm/winrun_status");

	if(line.find("active (running)")==std::string::npos)
	{
		output(error,"winrund not active\n");
		return -4;
	}

	//Get configuration information
	try
	{
		readstream.open(configpath);

		while(!readstream.eof())
		{
			getline(readstream,line);

			if(line.substr(0,1)!="#")//Hashtag denotes comments
			{
				if(line.find("operatorport")==0)
				{
					operatorPort=stoi(line.substr(line.find("=")+1,line.length()-line.find("=")));
				}
			}
		}
	}
	catch(...)
	{
		output(error,"Error while attempting to read config file at \"%s\"\n",configpath.c_str());
		return -5;
	}

	//Input validation
	if(!argv[1])
	{
		output(error,"No command entered\n");
		return -1;
	}
	else
	{
		for(int i=1; i<argc; i++)
		{
			if(!argv[2])
			{
				command=argv[1];
				break;
			}

			if(std::string(argv[i])=="-t")
			{
				try
				{
					if(argv[i+1])
					{
						timeout=std::stoull(argv[i+1]);
						i++;
					}
					else
					{
						output(error,"No timeout value entered\n");
						return -2;
					}
				}
				catch(...)
				{
					output(error,"Invalid timeout value \"%s\". Must be between 0 and %llu\n",argv[i],ULLONG_MAX);
					return -3;
				}
			}
			else if(std::string(argv[i])=="-v")
			{
				verbose=true;
			}
			else
			{
				command=argv[i];
			}
		}

		//Display verbose color codes if applicable
		if(verbose)
		{
			fprintf(stdout,"Verbose output color codes: \e[31;1mError\e[00m, \e[33;1mWarning\e[00m, \e[37;1mInfo\e[00m, \e[32;1mNotice\e[00m\n\n");
			output(info,"PID: %d\n\n",pid);

		}

		//Connect to daemon over loopback
		if(verbose)
		{
			output(notice,"Opening connection for initial request\n");
		}
		socket=initClient(operatorPort);

		//Send information to winrund
		send(socket,std::to_string(pid).c_str(),(std::to_string(pid).length()+1),0);
	
		timeoutRes=waitForTimeout(socket,timeout,"initial request");
		port=stoi(std::string(dataBuffer,recv(socket,dataBuffer,bufsize,0)));

		if(verbose)
		{
			output(notice,"Closing connection for initial request\n");
		}
		close(socket);

		//Connect to winrund thread as advised by daemon
		if(verbose)
		{
			output(notice,"Opening connection for command execution over port %d\n",port);
		}
		socket=initClient(port);

		memset(dataBuffer,0,bufsize);
		breakCode=std::string(dataBuffer,0,recv(socket,dataBuffer,bufsize,0));
		if(verbose)
		{
			output(info,"Set break code to %s\n",breakCode.c_str());
		}

		output(info,"Sending PID (%s), command (%s), timeout (%s), and verbosity (%s)\n",std::to_string(pid).c_str(),command.c_str(),std::to_string(timeout).c_str(),std::to_string(verbose).c_str());
		send(socket,std::to_string(pid).c_str(),(std::to_string(pid).length()+1),0);
		recv(socket,dataBuffer,bufsize,0);
		send(socket,command.c_str(),(command.length()+1),0);
		recv(socket,dataBuffer,bufsize,0);
		send(socket,std::to_string(timeout).c_str(),(std::to_string(timeout).length()+1),0);
		recv(socket,dataBuffer,bufsize,0);
		send(socket,std::to_string(verbose).c_str(),(std::to_string(verbose).length()+1),0);

		//Recieve command output
		while(true)
		{
			memset(dataBuffer,0,bufsize);
			timeoutRes=waitForTimeout(socket,timeout,"output");
			line=std::string(dataBuffer,0,recv(socket,dataBuffer,bufsize,0));

			if(line!=breakCode)
			{
				fprintf(stdout,"%s",line.c_str());
				send(socket,continueSig.c_str(),(continueSig.length()+1),0);
			}
			else
			{
				if(verbose)
				{
					output(notice,"Closing connection for command execution\n");
				}
				close(socket);

				return 0;
			}
		}
	}
	return 0;
}
