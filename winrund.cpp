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

int bytesReceived,sendRes,sock,ctr;
int connectRes=INT16_MAX;
int id[UCHAR_MAX];
const int bufsize=4096;
const int ms=1000;
char buf[bufsize];
std::string idstr;
std::string breakCode;
std::string lastOutput;
std::string user=getlogin();
std::string ip="";
std::string configpath="/home/"+user+"/.config/winrun/config";
std::string path="/dev/shm/winrund/";
std::string outpath=path+"out";
std::string command[UCHAR_MAX];
std::string writeBuffer[UCHAR_MAX];
std::ifstream readstream;
std::ofstream writestream;

//For security purposes, we don't allow any arguments to be passed into the daemon
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
				syslog(LOG_INFO,("Writing \""+output+"\" to \""+filepath+"\"").c_str());
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
		send(s,"1",(std::string("1").size()+1),0);
	}
}
void sendData(std::string cmdID, std::string d, int s)
{
	//Send command
        sendRes=send(s,(cmdID+d).c_str(),(cmdID.size()+d.size()+1),0);

        if(sendRes==-1)
        {
              	syslog(LOG_ERR,"Could not send data to server! Sleeping for 100ms...");
                usleep(100*ms);
        }

	//Wait for response and exit program when break code is recieved.
	while(true)
	{
       	        memset(buf,0,bufsize);
	        bytesReceived=recv(s,buf,bufsize,0);

		//Create file with name equal to id (which should be the first string recieved)
		if(std::string(buf,bytesReceived).find(breakCode)!=std::string::npos)//If find break code, write PID 5 times for end of command signal
		{
			writeOutput(cmdID,(path+cmdID).c_str(),cmdID+cmdID+cmdID+cmdID+cmdID,sock);
			break;
		}
		else if(std::string(buf,bytesReceived).find(cmdID)==std::string::npos)
		{
	        	//Write response
		        if(bytesReceived>0)
			{
				writeOutput(cmdID,(path+cmdID).c_str(),std::string(buf,bytesReceived),sock);
	        	}
		}
	}
}
int main(void)
{
	//Define variables
	pid_t pid, sid;

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

	//--------------------------------------//
	//	Daemon-specific intialization	//
	//--------------------------------------//

	//Get IP address
	readstream.open(configpath);
	getline(readstream,ip);
	readstream.close();

	//Create a hint structure for the server we're connecting with
	sock=socket(AF_INET,SOCK_STREAM,0);
	int port=55000;
	std::string ipAddress=ip;
	sockaddr_in hint;
	hint.sin_family=AF_INET;
	hint.sin_port=htons(port);
	inet_pton(AF_INET,ipAddress.c_str(),&hint.sin_addr);

	if(sock==-1)
	{
		syslog(LOG_ERR,"Unable to create socket!");
	}
	else
	{
		syslog(LOG_NOTICE,"Successfully initialized socket");
	}

	//Connect to the server on the socket
	connectRes=connect(sock,(sockaddr*)&hint,sizeof(hint));

	if(connectRes==-1)
	{
		syslog(LOG_ERR,("Could not connect to \""+ip+"\"").c_str());
		exit(EXIT_FAILURE);
	}
	else
	{
		syslog(LOG_NOTICE,("Successfully connected to "+ip+":"+std::to_string(port)).c_str());
	}

	//Recieve break code
	breakCode=std::string(buf,recv(sock,buf,bufsize,0));

	//Initialize ingoing and outgoing files, along with containing directory
	if(!dexists(path.c_str()))
	{
		mkdir(path.c_str(),S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	}
	if(!fexists(outpath.c_str()))
	{
		writestream.open(outpath);
		writestream.close();
	}

	//Enter daemon loop
	while(1)
	{
		ctr=0;

		//Attempt to open requested commands list and repeat every 100 milliseconds until it opens.
		while(!readstream.is_open())
		{
			try
			{
				readstream.open(outpath);
				break;
			}
			catch(...)
			{
				syslog(LOG_WARNING,("Can't open \""+outpath+"\", waiting 100ms").c_str());
				usleep(100*ms);
			}
		}

		//Read in requested commands
		//syslog(LOG_DEBUG,("Opened \""+outpath+"\"").c_str());
		while(!readstream.eof())
		{
			try
			{
				getline(readstream,idstr);
				id[ctr]=stoi(idstr);
				getline(readstream,command[ctr]);
				ctr++;
			}
			catch(...)
			{
				break;
			}
		}
		readstream.close();
		remove(outpath.c_str());

		writestream.open(outpath);
		writestream.close();

		//syslog(LOG_DEBUG,("Closed \""+outpath+"\"").c_str());

		//Run commands if they exist
		if(!id[0]==0)
		{
			for(int i=0; i<ctr; i++)
			{
				syslog(LOG_INFO,("Sending command \""+command[i]+"\" for instance id: "+std::to_string(id[i])).c_str());
				sendData(std::to_string(id[i]),("\""+std::string(command[i])+"\"").c_str(),sock);
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
