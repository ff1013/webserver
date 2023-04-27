CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

MYPUB = -I /usr/include/mysql
MYLIB = -L /usr/lib64/mysql -l mysqlclient
server: main.cpp  ./timer/lst_timer.cpp ./http_conn/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp  ./webserver/webserver.cpp ./config/config.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) $(MYPUB) $(MYLIB) -lpthread  -lmysqlclient

clean:
	rm  -r server
