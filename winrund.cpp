//g++ winrund.cpp -o winrund -pthread

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>
#include <unistd.h>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <syslog.h>
#include <fstream>
#include <sstream>
#include <climits>
#include <thread>
#include <signal.h>

const int bufsize=4096;
const int ms=1000;
std::string path="/dev/shm/winrund/";

//For security purposes, we don't allow any arguments to be passed into the daemon
const char* getProcName(int procID)
{
	char* name = (char*)calloc(1024,sizeof(char));
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
	return name;
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
void writeOutput(std::string cmdID, std::string filepath, std::string output, int s)
{
	std::ofstream outstream;

	while(!outstream.is_open())
	{
		try
		{
			if(!fexists(filepath.c_str()))
			{
				outstream.open((filepath+".lock").c_str());
				outstream.close();
				outstream.open(filepath);
				outstream<<output<<std::endl;
				outstream.close();
				remove((filepath+".lock").c_str());
				break;
			}
		}
		catch(...)
		{
			syslog(LOG_WARNING,("Can't open \""+filepath+"\", waiting 100ms").c_str());
			usleep(100*ms);
		}
	}

	//Send data process completion signal so output stream can continue server-side
	if(output.find(cmdID+cmdID+cmdID+cmdID+cmdID)==std::string::npos)
	{
		send(s,(cmdID+"-1").c_str(),(std::string(cmdID+"1").size()+1),0);
	}
}
void sendData(std::string cmdID, std::string commandstr, std::string bCode, int s, int p)
{
	int sendRes, bytesReceived, rv;
	char dataBuffer[bufsize];
	std::string recvStr;
	std::string outputFileName=(path+"_"+cmdID);
	fd_set readfds, masterfds;
	timeval timeout;
	timeout.tv_sec=5;
	timeout.tv_usec=0;

	//Send command
        sendRes=send(s,(bCode+cmdID+commandstr).c_str(),((bCode+cmdID+commandstr).length()+1),0);

        if(sendRes==-1)
        {
              	syslog(LOG_ERR,"Could not send data to server! Sleeping for 100ms...");
                usleep(100*ms);
        }

	//Wait for response and exit program when break code is recieved.
	while(true)
	{	        
		do
		{
			//If the relavent instance of winrun is still running
			if(kill(stoi(cmdID),0)==0&&std::string(getProcName(stoi(cmdID))).find(("winrun"+commandstr).c_str()))
			{
				memset(dataBuffer,0,bufsize);

				FD_ZERO(&masterfds);
				FD_SET(s,&masterfds);
				memcpy(&readfds,&masterfds,sizeof(fd_set));
				rv=select(s+1,&readfds,NULL,NULL,&timeout);

				if(rv==SO_ERROR)
				{
					syslog(LOG_ERR,("Socket error during select() on PID \""+cmdID+"\"").c_str());
					remove((path+"_"+cmdID).c_str());
					remove((path+std::to_string(p)+".lock").c_str());
					return;

				}
				else if(rv==0)
				{
					syslog(LOG_ERR,("Timeout (>%ld.%06ld seconds) while waiting for continue signal for PID \"%s\"",timeout.tv_sec,timeout.tv_usec,cmdID.c_str()));
					remove((path+"_"+cmdID).c_str());
					remove((path+std::to_string(p)+".lock").c_str());
					return;
				}
				else
				{
					bytesReceived=recv(s,dataBuffer,bufsize,0);
					recvStr=std::string(dataBuffer,bytesReceived);
				}
			}
			else
			{
				syslog(LOG_NOTICE,("winrun not found for pid "+cmdID+" stopping command output").c_str());
				remove((path+"_"+cmdID).c_str());
				remove((path+std::to_string(p)+".lock").c_str());
				return;
			}
		}while(recvStr.substr(0,recvStr.find("-"))!=cmdID);

		//Create file with name equal to id (which should be the first string recieved)
		if(recvStr.find(bCode)!=std::string::npos)//If find break code, write PID 5 times for end of command signal
		{
			writeOutput(cmdID,outputFileName,cmdID+cmdID+cmdID+cmdID+cmdID,s);
			break;
		}
		else
		{
	        	//Write response and parse out the winrun pid from the output line
		        if(bytesReceived>0)
			{
				writeOutput(cmdID,outputFileName,recvStr.substr(recvStr.find("-")+1,(recvStr.length()-recvStr.substr(0,recvStr.find("-")+1).length())),s);
	        	}
		}
	}
}
int winrund_check(std::string IP,int port)
{
	int sock=socket(AF_INET,SOCK_STREAM,0);
	int connectRes=INT16_MAX;
	int bytesReceived,rv;
	char buf[bufsize];
	char dataBuffer[bufsize];
	std::string check_test_val,check_result_val;
	std::string check_outpath=(path+"check");
	std::ifstream readstream;
	std::ofstream writestream;
	fd_set readfds, masterfds;
	timeval timeout;
	timeout.tv_sec=0;
	timeout.tv_usec=10*ms;

	//Create a hint structure for the server we're connecting with
	sockaddr_in hint;
	hint.sin_family=AF_INET;
	hint.sin_port=htons(port);
	inet_pton(AF_INET,IP.c_str(),&hint.sin_addr);

	if(sock==-1)
	{
		syslog(LOG_ERR,"Unable to create socket");
		exit(EXIT_FAILURE);
	}
	else
	{
		syslog(LOG_NOTICE,"Successfully initialized socket");
	}

	//Connect to the server on the socket
	connectRes=connect(sock,(sockaddr*)&hint,sizeof(hint));

	if(connectRes==-1)
	{
		syslog(LOG_ERR,("Could not connect to \""+IP+"\":"+std::to_string(port)).c_str());
		exit(EXIT_FAILURE);
	}
	else
	{
		syslog(LOG_NOTICE,("Successfully connected to "+IP+":"+std::to_string(port)).c_str());
	}

	//Enter daemon loop
	while(1)
	{
		check_test_val="";

		while(!fexists((check_outpath+".out").c_str()))
		{
			usleep(10*ms);
		}
		while(fexists((check_outpath+".lock").c_str()))
		{
			usleep(ms);
		}

		//Get value thread needs to check
		readstream.open((check_outpath+".out").c_str());
		getline(readstream,check_test_val);
		readstream.close();
		remove((check_outpath+".out").c_str());

		//Send value to check, and wait for response
		send(sock,check_test_val.c_str(),check_test_val.size()+1,0);

		memset(dataBuffer,0,bufsize);

	/*	FD_ZERO(&masterfds);
		FD_SET(sock,&masterfds);
		memcpy(&readfds,&masterfds,sizeof(fd_set));
		timeout.tv_sec=1;	//Reset these, because otherwise they tend towards zero for some weird reason.
		timeout.tv_usec=10*ms;	//
		rv=select(sock,&readfds,NULL,NULL,&timeout);

		if(rv==SO_ERROR)
		{
			syslog(LOG_ERR,("Socket error during select() for ready test on port "+std::to_string(port)).c_str());
		}
		else if(rv==0)
		{
			syslog(LOG_ERR,"Timeout (>%ld.%06ld seconds) while waiting for ready signal on port %d",timeout.tv_sec,timeout.tv_usec,(port));
		}
		else
		{*/
			bytesReceived=recv(sock,dataBuffer,bufsize,0);
			check_result_val=std::string(dataBuffer,bytesReceived);

			writestream.open((path+check_test_val+"_.lock").c_str());
			writestream.close();
			writestream.open((path+check_test_val+"_").c_str());
			writestream<<check_result_val;
			writestream.close();
			remove((path+check_test_val+"_.lock").c_str());
		//}
	}
}
int winrund_child(std::string IP,int port)
{
	int sock=socket(AF_INET,SOCK_STREAM,0);
	int connectRes=INT16_MAX;
	int id;
	char buf[bufsize];
	std::string child_idstr;
	std::string child_outpath=(path+std::to_string(port)+".out");
	std::string command;
	std::ifstream readstream;
	std::ofstream writestream;

	//Create a hint structure for the server we're connecting with
	sockaddr_in hint;
	hint.sin_family=AF_INET;
	hint.sin_port=htons(port);
	inet_pton(AF_INET,IP.c_str(),&hint.sin_addr);

	if(sock==-1)
	{
		syslog(LOG_ERR,"Unable to create socket");
		exit(EXIT_FAILURE);
	}
	else
	{
		syslog(LOG_NOTICE,"Successfully initialized socket");
	}

	//Connect to the server on the socket
	connectRes=connect(sock,(sockaddr*)&hint,sizeof(hint));

	if(connectRes==-1)
	{
		syslog(LOG_ERR,("Could not connect to \""+IP+"\":"+std::to_string(port)).c_str());
		exit(EXIT_FAILURE);
	}
	else
	{
		syslog(LOG_NOTICE,("Successfully connected to "+IP+":"+std::to_string(port)).c_str());
	}

	//Recieve break code
	std::string breakCode=std::string(buf,recv(sock,buf,bufsize,0));

	//Enter daemon loop
	while(1)
	{
		id=0;
		command="";

		if(fexists(child_outpath.c_str()))
		{
			//Attempt to open requested commands list and repeat every 100 milliseconds until it opens.
			while(!readstream.is_open())
			{
				try
				{
					readstream.open(child_outpath);
					break;
				}
				catch(...)
				{
					syslog(LOG_WARNING,("Can't open \""+child_outpath+"\", waiting 100ms").c_str());
					usleep(100*ms);
				}
			}

			//Read in requested commands
			while(!readstream.eof())
			{
				try
				{
					getline(readstream,child_idstr);
					id=stoi(child_idstr);
					getline(readstream,command);
				}
				catch(...)
				{
					break;
				}
			}
			readstream.close();
			remove(child_outpath.c_str());

			//Run commands if they exist
			if(id!=0)
			{
				syslog(LOG_INFO,("Sending command \""+command+"\" for pid "+std::to_string(id)+" over port "+std::to_string(port)).c_str());

				writestream.open((path+std::to_string(port)+".lock").c_str());
				writestream.close();
				sendData(std::to_string(id),("\""+command+"\""),breakCode,sock,port);
				remove((path+std::to_string(port)+".lock").c_str());

				syslog(LOG_INFO,("\""+command+"\" has completed for pid "+std::to_string(id)).c_str());
			}
		}
		usleep(100*ms);
	}
}
int main(void)
{
	//Define variables
	pid_t pid, sid;
	int ctr;
	int id[UCHAR_MAX];
	char dataBuffer[bufsize];
	std::string user=getlogin();
	std::string ip="";
	std::string configpath="/home/"+user+"/.config/winrun/config";
	std::string outpath=path+"out";
	std::string idstr;
	std::string recvStr;
	std::string writeBuffer[UCHAR_MAX];
	std::string command[UCHAR_MAX];
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

	//Get IP address
	configReader.open(configpath);
	getline(configReader,ip);
	configReader.close();

	//Initialize ingoing and outgoing files, along with containing directory
	if(!dexists(path.c_str()))
	{
		mkdir(path.c_str(),S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	}
	if(!fexists(outpath.c_str()))
	{
		outWriter.open(outpath);
		outWriter.close();
	}

	//Spawn child threads
	std::thread winrund_check_thread(winrund_check,ip,55000);
	winrund_check_thread.detach();

	for(int i=1; i<=8; i++)
	{
		std::thread winrund_child_thread(winrund_child,ip,(55000+i));
		winrund_child_thread.detach();
	}

	while(true)
	{
		ctr=0;

		//Attempt to open requested commands list and repeat every 100 milliseconds until it opens.
		while(!outReader.is_open())
		{
			usleep(100*ms);

			while(fexists((outpath+".lock").c_str()))
			{
				usleep(ms);
			}

			outWriter.open((outpath+".lock").c_str());
			outReader.open(outpath);
			break;
		}

		//Read in requested commands
		while(!outReader.eof())
		{
			try
			{
				getline(outReader,idstr);
				id[ctr]=stoi(idstr);
				getline(outReader,command[ctr]);
				ctr++;
			}
			catch(...)
			{
				break;
			}
		}
		outReader.close();
		remove(outpath.c_str());
		remove((outpath+".lock").c_str());

		outWriter.open(outpath);
		outWriter.close();

		//If there are commands to run, dish them out to idle threads.
		if(!id[0]==0)
		{
			for(int i=0; i<ctr; i++)
			{
				for(int j=1; j<=8; j++)
				{
					if(!fexists((path+std::to_string(55000+j)+".lock").c_str()))
					{
						//Check to see if server thread is idle
						outWriter.open((path+"check.lock").c_str());
						outWriter.close();
						outWriter.open((path+"check.out").c_str());
						outWriter<<j;
						outWriter.close();
						remove((path+"check.lock").c_str());

						while(!fexists((path+std::to_string(j)+"_").c_str()))
						{
							usleep(ms);
						}
						while(fexists((path+std::to_string(j)+"_.lock").c_str()))
						{
							usleep(ms);
						}

						outReader.open((path+std::to_string(j)+"_").c_str());
						getline(outReader,recvStr);
						outReader.close();
						remove((path+std::to_string(j)+"_").c_str());

						if(recvStr=="0")
						{
							syslog(LOG_INFO,("Delegating command \""+command[i]+"\" to thread on port "+std::to_string(55000+j)).c_str());
							outWriter.open(path+std::to_string(55000+j)+".out");
							outWriter<<id[i]<<std::endl;
							outWriter<<command[i]<<std::endl;
							outWriter.close();
							break;
						}
					}
					if(j==8)
					{
						j=0;
					}
				}
			}
		}

		//Reset arrays
		for(int i=0; i<UCHAR_MAX; i++)
		{
			id[i]=0;
			command[i]="";
		}

		usleep(100*ms);
	}

	//Close system logs for the child process
	syslog(LOG_NOTICE, "Stopping winrund");
	closelog();

	//Terminate the child process when the daemon completes
	exit(EXIT_SUCCESS);
}
