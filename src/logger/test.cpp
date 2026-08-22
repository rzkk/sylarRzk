
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

    auto fileAppender = std::make_shared<FileLogAppender>("system.log");
    fileAppender->setFormatter(std::make_shared<LogFormatter>("%d{%Y-%m-%d %H:%M:%S}%T[%p]%T[%t]%T[%c]%T%f:%l%T%m%n"));
    logger->addAppender(fileAppender);

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

void test4(){
    using namespace sylar;

    auto logger = MINI_LOG_NAME("system");

    MINI_LOG_INFO(logger) 
    << "named logger has no appender, so it falls back to logger";

    auto fileAppender = std::make_shared<FileLogAppender>("service.log");
//     fileAppender->setFormatter(std::make_shared<LogFormatter>("%d{%Y-%m-%d %H:%M:%S}%T[%p]%T[%t]%T[%c]%T%f:%l%T%m%n"));
    logger->addAppender(fileAppender);

    MINI_LOG_INFO(logger) << "now system has its own file appender";
    
    for(int i = 0; i < 20; ++i) {
      MINI_LOG_INFO(logger)<< "tick = "<< i;

      fileAppender->flush();

      std::this_thread::sleep_for(
            std::chrono::seconds(1)
      );   
    }
    
}
void test5(){ //V 15
      using namespace sylar;

      LoggerConfig system;
      system.name = "system";
      system.level = LogLevel::INFO;  // 直接过滤掉了 DEBUG 
     // system.formatter = "%d{%H:%M:%S}%T[%p]%T[%c]%T%m%n";
//
      //这是用户想要的 appender ,只包含数据
      //叫做： configuration description
      //又叫：  DTO ，  Data Transfer Object
      AppenderConfig file_appender;
      file_appender.type = AppenderConfig::Type::File;
      file_appender.file = "system.log";
      file_appender.level = LogLevel::ERROR;
      file_appender.formatter = "%d{%H:%M:%S}[FILE][%p] %m%n";
      AppenderConfig console_appender;
      console_appender.type = AppenderConfig::Type::Stdout;
      console_appender.level = LogLevel::DEBUG;
      // console_appender.formatter = "%d{%H:%M:%S}%T[%p]%T[%c]%Ttid=%t%Tthread=%N%Tfiber=%F"
                                          // "%Telapse=%rms%T%m%n";

      system.appenders= {file_appender ,console_appender };

      LogManager::GetInstance().applyConfig( {system} );

      auto logger = MINI_LOG_NAME("system");
      MINI_LOG_DEBUG(logger)<< "filtered by logger level";
      MINI_LOG_INFO(logger)<< "Debbuf";
      MINI_LOG_INFO(logger)<<  "console only";
      MINI_LOG_ERROR(logger)<< "console + system.log";
}
void test6(){ //V 16 // 手动模拟上下文
      using namespace sylar;

      LogContext::setThreadName("main");
      LogContext::setFiberId(7); 

      auto logger = MINI_LOG_NAME("context"); // logger 名字
      // auto a =  std::make_shared<StdOutLogAppender>();
      logger->addAppender( std::make_shared<StdOutLogAppender>());
      logger->setFormatter(
        "%d{%H:%M:%S}%T[%p]%T[%c]%T"
        "tid=%t%Tthread=%N%Tfiber=%F%Telapse=%rms%T%m%n");
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      MINI_LOG_INFO(logger) << "metadata now matches Sylar's logging context shape";
}
void test7(){ //V 16 // 手动模拟上下文
      using namespace sylar;

      auto logger = MINI_LOG_NAME("context"); // logger 名字
      // auto a =  std::make_shared<StdOutLogAppender>();
      logger->addAppender( std::make_shared<StdOutLogAppender>());
      logger->setFormatter(
        "%d{%H:%M:%S}%T[%p]%T[%c]%T"
        "tid=%t%Tthread=%N%Tfiber=%F%Telapse=%rms%T%m%n");
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      std::thread a([&] {
            LogContext::setThreadName("worker_a");
            LogContext::setFiberId(7); 
            MINI_LOG_INFO(logger)<< "from A";
      });

      std::thread b([&] {
            LogContext::setThreadName("worker_b");
            LogContext::setFiberId(8); 
            MINI_LOG_INFO(logger)<< "from B";
      });


      
      MINI_LOG_INFO(logger) << "metadata now matches Sylar's logging context shape";
      a.join();
      b.join();

}


int main(){
    
    // ? dsds
    test7();

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