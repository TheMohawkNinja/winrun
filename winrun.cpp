//g++ winrun.cpp -o winrun -lstdc++fs -std=c++17

#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <cstdlib>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <fstream>
#include <time.h>
#include <filesystem>
#include <climits>
#include <signal.h>

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
std::string getOutputFile(std::string filepath, const char *filename)
{
	std::string entry8;
	for (const auto & entry : std::filesystem::directory_iterator(filepath))
	{
		entry8=entry.path().u8string();
		if(entry8.find(filename)!=std::string::npos)
		{
			return entry8;
		}
	}
	return "";
}
int main(int argc, char** argv)
{
	bool verbose=false;
	int ms=1000;
	int timeout=5;
	int dpid;
	pid_t pid=getpid();
	std::string path="/dev/shm/winrund/";
	std::string outputpath=(path+"_"+std::to_string(pid)).c_str();
	std::string line;
	std::string command;
	std::ifstream readstream;
	std::ofstream writestream;

	//Check if daemon is running before continuing
	try
	{
		readstream.open((path+"pid").c_str());
		getline(readstream,line);
		dpid=stoi(line);

		if(!kill(dpid,0)==0||(kill(dpid,0)==0&&std::string(getProcName(dpid)).find("winrund")))
		{
			fprintf(stderr,"winrund not running\n");
			return -4;
		}
	}
	catch(...)
	{
		fprintf(stderr,"winrund not running\n");
		return -4;
	}

	//If user just types in "winrun"
	if(!argv[1])
	{
		fprintf(stderr,"No command entered\n");
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
						fprintf(stderr,"No timeout value entered\n");
						return -2;
					}
				}
				catch(...)
				{
					fprintf(stderr,"Invalid timeout value \"%s\". Must be between 0 and %d\n",argv[i],ULLONG_MAX);
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

		//Attempt to open "out" file and wait 1ms if it cannot be opened
		while(!writestream.is_open())
		{
			while(getOutputFile(path,"out.lock")!="")
			{
				usleep(ms);
			}
			writestream.open((path+"out.lock").c_str());
			writestream.close();
			writestream.open((path+"out").c_str());
		}

		writestream<<pid<<std::endl;
		writestream<<command<<std::endl;
		if(!verbose)
		{
			writestream<<timeout<<std::endl;
		}
		else
		{
			writestream<<timeout<<",verbose"<<std::endl;
			fprintf(stdout,"Verbose output color codes: \e[31;1mError\e[00m, \e[33;1mWarning\e[00m, \e[36;1mInfo\e[00m, \e[32;1mNotice\e[00m\n\n");
		}
		writestream.close();
		remove((path+"out.lock").c_str());

		//Attempt to open response file and dump contents to stdout
		while(!readstream.is_open())
		{
			while(getOutputFile(path,std::to_string(pid).c_str())=="")
			{
				usleep(ms);
			}
			while(getOutputFile(path,(std::to_string(pid)+".lock").c_str())!="")
			{
				usleep(ms);
			}

			readstream.open(outputpath);
			std::getline(readstream,line);
			if(line!=(std::to_string(pid)+std::to_string(pid)+std::to_string(pid)+std::to_string(pid)+std::to_string(pid)))
			{
				fprintf(stdout,"%s\n",line.c_str());
				readstream.close();
				remove(outputpath.c_str());
			}
			else
			{
				remove(outputpath.c_str());
				return 0;
			}
		}
	}
	return 0;
}
