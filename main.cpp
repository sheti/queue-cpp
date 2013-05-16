#include <yaml-cpp/yaml.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cstring>
#include <errno.h>
#include <pthread.h>
#include <ctime>
#include <sys/stat.h>
#define BUF_SIZE 256

struct thread_arg {
	std::string task_name;
	std::string command;
	std::map<std::string,std::string> heads;
	int num;
};

class MakeString {
	public:
		template<class T>
		MakeString& operator<< (const T& arg) {
			m_stream << arg;
			return *this;
		}
	operator std::string() const {
		return m_stream.str();
	}
	protected:
		std::stringstream m_stream;
};

std::string& trim(std::string& s){
	static const std::string SPACES(" \t\n");
	size_t head = s.find_first_not_of(SPACES);
	if ( head == std::string::npos )
		return s = "";
	else if ( head > 0 )
		s.erase(0, head);
	size_t tail = s.find_last_not_of(SPACES);
	if ( tail != s.size() - 1 )
		s.erase(tail + 1);
	return s;
}

void log(std::string str) {
	time_t rawtime;
	struct tm * timeinfo;
	char buffer [80];

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, 80, "%d-%m-%Y %X ", timeinfo);
	std::clog << buffer << str << std::endl;
}

static void *start_com(void *vptr_args) {
	log(MakeString() << "Start child: \"" << ((thread_arg *)vptr_args)->task_name << "\" number " << ((thread_arg *)vptr_args)->num);
	std::string command = ((thread_arg *)vptr_args)->command;
	int num = ((thread_arg *)vptr_args)->num;
	delete (thread_arg *)vptr_args;
	int return_value = system(command.c_str());
	log(MakeString() << "Child number " << num << " exited with value " << return_value);
}

int main() {
	std::string sLog;
	std::string sPid;
	std::string sSocket;
	std::map<std::string,std::string> tasks;
  
	try {
		YAML::Node config = YAML::LoadFile("/etc/queue.conf.yaml");
		sLog = config["log"].as<std::string>();
		sPid = config["pid"].as<std::string>();
		sSocket = config["socket"].as<std::string>();
		YAML::Node tasks_node = config["tasks"];

		for(YAML::const_iterator it=tasks_node.begin();it!=tasks_node.end();++it) {
			tasks.insert(std::pair<std::string,std::string>(it->first.as<std::string>(), it->second.as<std::string>()));
		}
	} catch(YAML::ParserException& e) {
		std::cerr << e.what() << std::endl;
	}

	std::ofstream outfilepid;
	try {
		outfilepid.open(sPid.c_str());
		outfilepid << getpid();
	} catch (std::ofstream::failure e) {
		std::cerr << "Exception opening pid file" << std::endl;
		return EXIT_FAILURE;
	}
	outfilepid.close();

	std::ofstream outlog;
	try {
		outlog.open(sLog.c_str(), std::ios_base::app);
	} catch (std::ofstream::failure e) {
		std::cerr << "Exception opening log file" << std::endl;
		return EXIT_FAILURE;
	}
	std::clog.rdbuf(outlog.rdbuf());

	unlink(sSocket.c_str());
	int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	struct sockaddr srvr_name;
	if (sock < 0) {
		log("Socket failed");
		return EXIT_FAILURE;
	}
	srvr_name.sa_family = AF_UNIX;
	strcpy(srvr_name.sa_data, sSocket.c_str());
	if (bind(sock, &srvr_name, strlen(srvr_name.sa_data) + sizeof(srvr_name.sa_family)) < 0) {
		log("Bind failed");
		return EXIT_FAILURE;
	}

	chmod(sSocket.c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);

	char buf[BUF_SIZE];
	std::string head;
	std::map<std::string,std::string> heads;
	std::map<std::string,std::string>::iterator heads_it;
	std::map<std::string,std::string>::iterator tasks_it;
	pthread_t thread;
	int tasck_i = 0;
	int bytes, ln_count;
	log("Queue start");
	while(1) {
		bytes = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
		if(bytes < 0) 
			goto exit;
		ln_count = 0;
		head.clear();
		heads.clear();
		for(int i = 0; i < bytes; i++) {
			if(buf[i] == '\n') {
				ln_count += 1;
				if(ln_count == 2)
					break;
				// Разделение параметров
				if(ln_count == 1) {
					std::size_t found_pos = head.find(':');
					if (found_pos != std::string::npos) {
						std::string head_name = head.substr(0, found_pos);
						head_name = trim(head_name);
						std::string head_value = head.substr(found_pos + 1, head.length());
						head_value = trim(head_value);
						heads.insert(std::pair<std::string,std::string>(head_name, head_value));
					}
					if(head.compare("exit") == 0)
						goto exit;
					head.clear();
				}
			} else {
				head += buf[i];
				ln_count = 0;
			}
		}
		if(heads.size() > 0)  {
			if( (heads_it = heads.find("Exec")) != heads.end() ) {
				if( (tasks_it = tasks.find((*heads_it).second)) != tasks.end()) {
					struct thread_arg *ta = new thread_arg;
					ta->task_name = (*tasks_it).first;
					ta->command = (*tasks_it).second;
					ta->heads = heads;
					ta->num = ++tasck_i;
					pthread_create(&thread, NULL, start_com, ta);
				}
			}
		}
	}
	exit:
	close(sock);
	unlink(sSocket.c_str());
	log("Queue stop");
	outlog.close();
	return EXIT_SUCCESS;
}
