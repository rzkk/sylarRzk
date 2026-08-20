
// #include"logger.h"
#include"log.h"
#include<iostream>
#include<memory>
#include<thread>
using namespace std;

void test1(){
    using namespace sylar;

    std::shared_ptr<Logger>logger = std::make_shared<Logger>("system");

    std::shared_ptr<StdOutLogAppender>stdappender = std::make_shared<StdOutLogAppender>();
    stdappender->setFormatter(std::make_shared<LogFormatter>("[%p]%T%d{%H:%M:%S}%T%c%T%m%n"));
    logger->addAppender(stdappender);


    std::shared_ptr<FileLogAppender>fileappender = std::make_shared<FileLogAppender>("log1.log", LogLevel::ERROR);
    logger->addAppender(fileappender);

    
    MINI_LOG_DEBUG(logger) << "hidden";
    MINI_LOG_INFO(logger) << "port=" << 8080 << ", server started";
    MINI_LOG_ERROR(logger) << "accept failed, errno=" << 11;

}
void test2(){
    using namespace sylar;
    MINI_LOG_INFO(MINI_LOG_ROOT()) << "root logger";
    auto system = MINI_LOG_NAME("system");
    MINI_LOG_INFO(system) 
    << "named logger has no appender, so it falls back to root";

    auto fileFormatter = std::make_shared<FileLogAppender>("system.log");
    fileFormatter->setFormatter(std::make_shared<LogFormatter>("%d{%Y-%m-%d %H:%M:%S}%T[%p]%T[%t]%T[%c]%T%f:%l%T%m%n"));
    system->addAppender(fileFormatter);
    MINI_LOG_INFO(system) 
    << "now system has its own file appender";
    
    //MINI_LOG_FMT_INFO(ogger , "hello worf %d \n" , 10 );
}

void test3(){
    using namespace sylar;

    auto logger = MINI_LOG_NAME("system");

    MINI_LOG_INFO(logger) 
    << "named logger has no appender, so it falls back to logger";

    auto fileFormatter = std::make_shared<FileLogAppender>("system.log");
    fileFormatter->setFormatter(std::make_shared<LogFormatter>("%d{%Y-%m-%d %H:%M:%S}%T[%p]%T[%t]%T[%c]%T%f:%l%T%m%n"));
    logger->addAppender(fileFormatter);

    MINI_LOG_INFO(logger) << "now system has its own file appender";
    
    std::vector<std::thread> threads;
    for(int i = 0; i < 20; ++i) {
        threads.emplace_back(
            [logger, i]() {
                for(int j = 0;j < 100;++j) {
                    MINI_LOG_INFO(logger)<< "worker="<< i
                        << " j="<< j;
                }
            }
        );
    }
    for(auto& t : threads) {
        t.join();
    }
}
int main(){
    
    // ? dsds
    test3();

}
/*mark 完整流程

Worker Thread
      │
      ▼
MINI_LOG_INFO
      │
      ├── Logger level 检查
      │
      ▼
GetThreadId()
      │
      ▼
创建 LogEvent
      │
      ├── logger
      ├── level
      ├── file
      ├── line
      ├── thread id
      └── time
      │
      ▼
创建 LogEventWrap
      │
      ▼
getSS()
      │
      ▼
<< "from worker thread"
      │
      ▼
语句结束
      │
      ▼
~LogEventWrap()
      │
      ▼
Logger::log()
      │
      ├── 获取 Logger mutex
      │
      ├── 复制 appenders
      │
      └── 释放 Logger mutex
      │
      ▼
StdoutLogAppender::log()
      │
      ├── 获取 Appender mutex
      │
      ▼
LogFormatter::format()
      │
      ├── DateTimeItem
      ├── ThreadIdItem
      ├── LevelItem
      ├── NameItem
      ├── FileItem
      ├── LineItem
      └── MessageItem
      │
      ▼
std::cout
      │
      ▼
释放 Appender mutex
*/