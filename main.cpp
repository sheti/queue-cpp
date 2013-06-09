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
#include <signal.h>
#define BUF_SIZE 5

struct start_arg {
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
	log(MakeString() << "Start child: \"" << ((start_arg *)vptr_args)->task_name << "\" number " << ((start_arg *)vptr_args)->num);
	std::string command = ((start_arg *)vptr_args)->command;
	int num = ((start_arg *)vptr_args)->num;
	delete (start_arg *)vptr_args;
	int return_value = system(command.c_str());
	log(MakeString() << "Child number " << num << " exited with value " << return_value);
}

struct accept_arg {
	int sock;
	pthread_t *thread;
	int *num;
	std::map<std::string,std::string> *tasks;
};

static void *accept_com(void *vptr_args) {
	char buf[BUF_SIZE];
	std::string heads_str = "";
	int bytes;
	std::map<std::string,std::string> *tasks = ((accept_arg *)vptr_args)->tasks;
	std::map<std::string,std::string> heads;
	std::map<std::string,std::string>::iterator heads_it;
	std::map<std::string,std::string>::iterator tasks_it;

	while( (bytes = recv(((accept_arg *)vptr_args)->sock, buf, sizeof(buf), 0)) > 0) {
		if(bytes <= 0)
			return (void *)EXIT_FAILURE;
		if(bytes >= 2) {
			if(buf[bytes - 1] == '\n' && buf[bytes - 2] == '\n') {
				heads_str.append("\n");
				break;
			} else
				heads_str.append(buf, bytes);
		} else
			heads_str.append(buf, bytes);
	}
	close(((accept_arg *)vptr_args)->sock);
	
	std::size_t head_start_pos = 0, head_end_pos, colon_pos;
	while( (head_end_pos = heads_str.find("\n", head_start_pos)) != std::string::npos) {
		colon_pos = heads_str.find(":", head_start_pos);
		if (colon_pos != std::string::npos) {
			std::string head_name = heads_str.substr(head_start_pos, colon_pos - head_start_pos);
			head_name = trim(head_name);
			std::string head_value = heads_str.substr(colon_pos + 1, head_end_pos - colon_pos);
			head_value = trim(head_value);
			heads.insert(std::pair<std::string,std::string>(head_name, head_value));
		}
		head_start_pos = head_end_pos + 1;
	}
	int *num = ((accept_arg *)vptr_args)->num;
	if(heads.size() > 0)  {
		if( (heads_it = heads.find("Exec")) != heads.end() ) {
			if( (tasks_it = tasks->find((*heads_it).second)) != tasks->end()) {
				struct start_arg *sa = new start_arg;
				sa->task_name = (*tasks_it).first;
				sa->command = (*tasks_it).second;
				sa->heads = heads;
				*num += 1;
				sa->num = *num;
				pthread_create(((accept_arg *)vptr_args)->thread, NULL, start_com, sa);
			} else {
				log(MakeString() << "Error command: " << (*heads_it).second);
			}
		}
	}
	delete (accept_arg *)vptr_args;
}

struct listen_arg {
	int sock;
	pthread_t *thread;
	std::map<std::string,std::string> *tasks;
};

static void *listen_com(void *vptr_args)  {
	int sock = ((listen_arg *)vptr_args)->sock, sock_acpt;
	pthread_t *thread = ((listen_arg *)vptr_args)->thread;
	int num = 0;
	while(1) {
		if( (sock_acpt = accept(sock, NULL, NULL)) < 0 ) 
		{
			log("Accept failed");
			delete (listen_arg *)vptr_args;
			break;
		}
		struct accept_arg *aa = new accept_arg;
		aa->sock = sock_acpt;
		aa->thread = thread;
		aa->num = &num;
		aa->tasks = ((listen_arg *)vptr_args)->tasks;
		pthread_create(thread, NULL, accept_com, aa);
	}
	raise(SIGTERM);
	return (void *)EXIT_FAILURE;
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
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
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
	if(listen(sock, 3) < 0) {
		log("Listen failed");
		return EXIT_FAILURE;
	}

	chmod(sSocket.c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);

	log("Queue start");
	pthread_t thread;

	struct listen_arg *la = new listen_arg;
	la->sock = sock;
	la->thread = &thread;
	la->tasks = &tasks;
	pthread_create(&thread, NULL, listen_com, la);
	
	sigset_t sigset;
	siginfo_t siginfo;
	// настраиваем сигналы которые будем обрабатывать
	sigemptyset(&sigset);
	// сигнал остановки процесса пользователем
	sigaddset(&sigset, SIGQUIT);
	// сигнал для остановки процесса пользователем с терминала
	sigaddset(&sigset, SIGINT);
	// сигнал запроса завершения процесса
	sigaddset(&sigset, SIGTERM);
	sigprocmask(SIG_BLOCK, &sigset, NULL);
	int exit_status = 1;
	while(exit_status) {
		sigwaitinfo(&sigset, &siginfo);
		switch(siginfo.si_signo) {
			case SIGQUIT:
			case SIGINT:
			case SIGTERM:
				exit_status = 0;
				break;
		}
	}
	close(sock);
	unlink(sSocket.c_str());
	log("Queue stop");
	outlog.close();
	return EXIT_SUCCESS;
}
