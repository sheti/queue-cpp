#include <yaml-cpp/yaml.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include <errno.h>
#include <ctime>
#include <sys/stat.h>
#define BUF_SIZE 5

std::map<std::string,std::string> tasks;
std::string slog;

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
	// Открывает Log-файл
	std::ofstream outlog;
	try {
		outlog.open(slog.c_str(), std::ios_base::app);
	} catch (std::ofstream::failure e) {
		std::cerr << "Exception opening log file" << std::endl;
		return;
	}
	time_t rawtime;
	struct tm * timeinfo;
	char buffer [80];

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, 80, "[%d-%m-%Y %X] ", timeinfo);
	outlog << buffer << str << std::endl;
	outlog.close();
}

void accept_command() {
	char buf[BUF_SIZE];
	std::string heads_str = "";
	int bytes;
	std::map<std::string,std::string>::iterator tasks_it;

	while( (bytes = read(fileno(stdin), buf, sizeof(buf))) > 0) {
		if(bytes <= 0)
			return;
		if(bytes >= 2) {
			if(buf[bytes - 1] == '\n' && buf[bytes - 2] == '\n') {
				heads_str.append(buf, bytes - 1);
				break;
			} else
				heads_str.append(buf, bytes);
		} else
			heads_str.append(buf, bytes);
	}
	
	std::size_t head_start_pos = 0, head_end_pos, colon_pos;
	while( (head_end_pos = heads_str.find("\n", head_start_pos)) != std::string::npos) {
		colon_pos = heads_str.find(":", head_start_pos);
		if (colon_pos != std::string::npos) {
			std::string head_name = heads_str.substr(head_start_pos, colon_pos - head_start_pos);
			head_name = trim(head_name);
			std::string head_value = heads_str.substr(colon_pos + 1, head_end_pos - colon_pos);
			head_value = trim(head_value);
			if( strcmp(head_name.c_str(), "Exec") == 0 ) {
				if( (tasks_it = tasks.find(head_value.c_str())) != tasks.end()) {
					// Старт комманды
					log(MakeString() << "Start child: \"" << (*tasks_it).first << "\"");
					std::string command = (*tasks_it).second;
					int return_value = system(command.c_str());
					log(MakeString() << "Child: \"" << (*tasks_it).first << "\" exited with value " << return_value);
					// --
				} else {
					log(MakeString() << "Error command: " << head_value);
				}
			}
		}
		head_start_pos = head_end_pos + 1;
	}
}

int main() {
	try {
		YAML::Node config = YAML::LoadFile("/etc/queue.conf.yaml");
		slog = config["log"].as<std::string>();
		YAML::Node tasks_node = config["tasks"];

		for(YAML::const_iterator it=tasks_node.begin();it!=tasks_node.end();++it) {
			tasks.insert(std::pair<std::string,std::string>(it->first.as<std::string>(), it->second.as<std::string>()));
		}
	} catch(YAML::ParserException& e) {
		std::cerr << e.what() << std::endl;
	}
	
	log("Queue start");
	accept_command();
	log("Queue stop");
	return EXIT_SUCCESS;
}

