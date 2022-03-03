CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CXXFLAGS += -g
else
	CXXFLAGS += -O2
endif

server: main.cpp ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CgiMysql/sql_connection_pool.cpp ./config/connfig.cpp webserver.cpp
	$(CXX) -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm -rf server