#pragma once 
// #include"logger.h"

#include <cstdarg> // 这是va_start 

#include<string>
#include<sstream>
#include<memory>
#include<ctime>
#include<list>
#include <fstream>
#include<vector>
#include<map>
#include<thread>
#include<mutex>
#include <atomic> // 对Level 的原子操作

//  mark  : 设计原则 ， 锁属于对象 
/*  mark: 线程安全判断法:  T m_xxx; 要不要加锁
*       问题 1： 它会不会被多个线程访问？
            如果不会：不用锁

        问题 2： 如果多个线程访问，是不是全部只读？
            如果构造后永远只读，通常不用锁

        问题 3： 如果存在读写或写写并发？
            需要同步 ，比如：
            m_loggers 多线程读写 → mutex
            m_appenders 多线程遍历 + 修改 → mutex
            m_os 多线程写 → mutex

            LogEvent::m_ss 一条日志自己的 → 不需要 mutex
            LogFormatter::m_items 构造后只读 → 不需要 mutex
*/
/*mark 
    LoggerManager 锁“Logger 的注册表”，
    Logger 锁“输出目标列表”，
    Appender 锁“真正的输出设备”。  
                          ┌─────────────┐
                          │ LogEvent    │
                          │ 每条日志独有 │
                          │ 不需要锁     │
                          └──────┬──────┘
                                 │
                                 ▼
多个线程 ───────────────→ Logger
                          │ m_mutex
                          │
                    保护 appenders
                          │
               ┌──────────┴──────────┐
               ▼                     ▼
        StdoutAppender          FileAppender
          │ m_mutex               │ m_mutex
          │                       │
      保护 cout               保护 ofstream
          │                       │
          ▼                       ▼
      terminal                  file


多个线程 ─────────────→ LoggerManager
                         │ m_mutex
                         │
                     保护 map
*/

namespace sylar{

// inline
// static uint64_t GetThreadId_01(){
//     return std::hash<std::thread::id>{}(std::this_thread::get_id());
//     // 获取当前线程的 std::thread::id 对象，然后利用标准库的 std::hash 将其哈希转换成一个整数
//     // mark 缺点：  1 不是真正的线程ID ,若死锁或者cpu 无响应 ， 无法与真实的线程ID相对应
//     //              2 存在hash 冲突
//     //              3 hash返回的是 size_t , 64位系统是64位， 与uint64对应 ，但是32位系统size_t就是32位了 
// }
// // 这是头文件中
// inline
// std::thread::id GetThreadId(){
//     return std::this_thread::get_id();
//     // 获取当前线程的 std::thread::id 对象，然后利用标准库的 std::hash 将其哈希转换成一个整数
//     // mark 缺点：  1 不是真正的线程ID ,若死锁或者cpu 无响应 ， 无法与真实的线程ID相对应
//     //              2 存在hash 冲突
//     //              3 hash返回的是 size_t , 64位系统是64位， 与uint64对应 ，但是32位系统size_t就是32位了 
// }



class Logger;


//日志的重要程度
//mark ： 天然是一次日志调用的局部数据 ， 只归单个线程 所有
class LogLevel{
public: 
    // 已经是在 局部作用域中定义 enum 了 
    enum Level{ 
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        FATAL = 5
    };

    static const std::string LevelToString(Level level);
    static Level StringToLevel(const std::string & text);//依旧是教学简化
};

// 一条日志携带的原始数据 //原始数据中是不应该有 level 的
class LogEvent{
public:
    using ptr = std::shared_ptr<LogEvent>;
public:
    LogEvent( std::shared_ptr<Logger> logger, LogLevel::Level level , 
        const char * filename ,int line , 
        std::time_t time ,std::thread::id threadId = std::this_thread::get_id()):
    m_logger(logger),m_level(level),
    m_fileName(filename),m_line(line),
    m_time(time),m_threadId(threadId){}

    std::stringstream & getSS() {return m_ss;} // 会对SS做修改 
    LogLevel::Level getLevel() const { return m_level ;}
    const std::string  getFile()  const{ return m_fileName ;}
    int getLine() const { return m_line ;}
    std::string getContent() const { return m_ss.str(); }
    std::shared_ptr<Logger>  getLogger()const{ return m_logger; }
    std::thread::id getThreadId()const{ return m_threadId; }
    std::time_t getTime()  const{ return m_time ;}

    // 实现printf风格
    void format(const char * fmt , ...);
    void format(const char* fmt, va_list ap);

private:
    std::shared_ptr<Logger> m_logger;   // Logger 可以被多个线程使用
    LogLevel::Level m_level;
    
    std::string m_fileName;
    int32_t m_line;  // signed int ->  int

    //uint64_t  m_threadId; // unsigned long int // 描述的是 哪条线程产生了 这条日志， 所以 threadId 属于 event , 不属于 Logger 
    std::time_t m_time;
    std::thread::id   m_threadId; 
    std::stringstream m_ss; // 可以用来实现 流式 日志
}; // end of LogEvent

//“这些数据最终排成什么样？”
/*
%m 消息
%p 日志级别
%r 累计毫秒
%c Logger 名称
%t 线程ID
%n 换行
%d 时间
%f 文件名
%l 行号
%T Tab
%F 协程ID
%N 线程名称
*/
//mark : formatter 中的数据在初始化之后 是只读的 // 除了 cout等
class LogFormatter{
public:
    using ptr = std::shared_ptr<LogFormatter>;
public:
    class FormatItem{
    public:
        using ptr = std::shared_ptr<FormatItem>;
    public:
        virtual ~FormatItem() = default;
        virtual void format(std::ostream&os , std::shared_ptr<Logger> logger ,
                             LogLevel::Level level ,  LogEvent::ptr event) = 0;
    };
public:
    explicit LogFormatter(const std::string  pattern = "%d{%Y-%m-%d %H:%M:%S}%T[%p]%T[%c]%T%f:%l%T%t%T%m%n")
    :m_pattern( std::move(pattern) ){
        init(); // 在这个class formatter 创建时执行 
    }
    void format(std::ostream & os,const std::shared_ptr<Logger>&logger , 
                        LogLevel::Level level, const LogEvent::ptr& event ) ;
    bool isError(){ return m_error;}
    const std::string& getPattern() const { return m_pattern; }
private:
    void init();
private:
    std::string m_pattern;
    std::vector<FormatItem::ptr>m_items;
    bool m_error = false;
};

class LogAppender{
    friend class Logger; 
public:
    using ptr = std::shared_ptr<LogAppender>;
public:
    LogAppender(LogLevel::Level level):m_level(level){}
    virtual ~LogAppender() = default;

    virtual void log(std::shared_ptr<Logger>logger , 
                    LogLevel::Level level , 
                    std::shared_ptr<LogEvent>event) = 0; // 每一个 destinations 的专属输出器

  
    void setlevel(LogLevel::Level level);
    LogLevel::Level getlevel() const { return m_level.load(); }
    void setFormatter(LogFormatter::ptr formatter);
    LogFormatter::ptr getFormatter();
protected: // 子类需要这个成员
    //LogLevel::Level m_level;//每一个目的地都有自己的 level 
    std::atomic<LogLevel::Level> m_level{LogLevel::DEBUG};
    LogFormatter::ptr m_formatter; // 每一个 目的地都有自己的 格式化器
    // 可以通过logger中的默认格式化器 初始化 ， 也可以在创建目的地的时候直接setFormatter
    bool m_hasFormatter = false;
    std::mutex m_mutex;
};


// logger 的调度中心
// 允许 构建自己的 shared_ptr
// root logger ： 如果自己的 logger 的appender 不存在就 使用 root logger的
class Logger : public std::enable_shared_from_this<Logger>{
public:
    using ptr = std::shared_ptr<Logger>;
public: 
    
    explicit Logger(const std::string& name = "root")
    :m_name(std::move(name) ),                    //日志器的默认格式                     
    m_formatter( std::make_shared<LogFormatter>() ){}                            
                        //%d{%Y-%m-%d %H:%M:%S}%T[%p]%T[%c]%T%f:%l%T%m%n

    void log(LogLevel::Level level ,LogEvent::ptr event ) ;
    void debug(LogEvent::ptr event) ;
    void info(LogEvent::ptr event ) ;
    void warn(LogEvent::ptr event ) ;
    void error(LogEvent::ptr event ) ;
    void fatal(LogEvent::ptr event ) ;

    void setLevel(LogLevel::Level level ){
        
         m_level.store(level); 
    }
    LogLevel::Level getLevel(){ return m_level.load(); }
    std::string & getName(){ 
        // 在完成logger初始化之后 m_name 不会再改变
        return m_name;
    }
    void addAppender(std::shared_ptr<LogAppender>appender);
    void setRoot(Logger::ptr root){
        //在 LoggerManager::getLogger()：中调用 setRoot
        //这是最开始的初始化阶段
        //logger 被其他线程拿到之前，root 就已经设置好了。
        // mark : 初始化阶段完成状态设置，然后只读
        m_root = root;
    }

    //目的地有加入有清空 有使用， 所以就需要互斥， 否则有人在使用 有人再修改
    // 配置线程 会实时监控 YAML配置文件
    void clearAppender(){ 
        std::lock_guard<std::mutex>lock(m_mutex);
        m_appenders.clear(); 
    } // 目的地清空
   void setFormatter(LogFormatter::ptr formatter);
   void setFormatter(const std::string& pattern );
   LogFormatter::ptr getFormatter() {
        return m_formatter;
   }

private:
    std::string m_name;
    std::atomic<LogLevel::Level> m_level{LogLevel::DEBUG};
    // LogLevel::Level m_level = LogLevel::DEBUG ;
    std::list< std::shared_ptr<LogAppender> > m_appenders;
    LogFormatter::ptr m_formatter;
    Logger::ptr m_root;
    std::mutex m_mutex;
};


// 利用 RAII 思想 管理日志LogEvent 
class LogEventWarp{
public:
    explicit LogEventWarp(std::shared_ptr<LogEvent> event): m_event(event){}
    
    ~LogEventWarp(){ // 析构时 真正打印
        m_event->getLogger()->log( m_event->getLevel() , m_event );
        // 一条日志就是一个 event ，  用临时变量 EventWarp 管理 
        // 每一条event 日志都包含一个 logger
        // 从 event 的 getLogger得到 logger , 再调用 logger 的 log () 函数
        // logger 的 log ()  需要日志的 level ， 与 event 本身  
        // log( LogLevel::Level level ,LogEvent::ptr event  )
    }

    std::stringstream& getSS(){ return m_event->getSS(); }
    LogEvent::ptr getEvent(){return m_event ;}
private:
    std::shared_ptr<LogEvent> m_event;

};



class StdOutLogAppender : public LogAppender{
//private: // mark 这里的调用属于多态调用， 父类的log是public，通过父类指针是能调用到这个private的log的
public:
    StdOutLogAppender(LogLevel::Level level = LogLevel::DEBUG):LogAppender(level){}
    virtual void log(std::shared_ptr<Logger>logger , 
                    LogLevel::Level level , 
                    std::shared_ptr<LogEvent>event) override;
    
};
class FileLogAppender : public LogAppender{
public:
    explicit FileLogAppender(std::string filename,LogLevel::Level level = LogLevel::DEBUG):
                    LogAppender(level),
                    m_fileName(std::move(filename))
                    {
        // 对象创建之后 就要满足可以使用
        //对象构造完成=资源已经进入可用状态
        reopen();
    }
    /*mark : 
        数据：m_fileStream

        访问它的函数：
            reopen()
            log()

        所以：
            两个函数都应该遵守同一套锁规则
    */
    bool reopen(){
        // 拆成一个有锁的 调用没锁的
        std::lock_guard<std::mutex> lock(m_mutex);
        return  reopenUnlocked();
    }
    // 日志本身不能成为整个服务器吞吐量的主要瓶颈。
    // 因为不能每一条日志都flush ，会严重降低缓冲批量写入的效果。
    //m_fileStream 在刷新时会被读 ， 要加锁 保护一下
    void flush() {  //没有用到 
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_fileStream.is_open()) {
            m_fileStream.flush();
        }
    }

    virtual void log(std::shared_ptr<Logger>logger , 
                    LogLevel::Level level , 
                    std::shared_ptr<LogEvent>event) override;
    const std::string& getfileNmae(){
        return m_fileName; //在以后可以知道 日志写去了哪里
    }
private:
    bool reopenUnlocked(){
        if(m_fileStream.is_open()){
            m_fileStream.close();
        }
        // 如果失败了 依旧更新时间 ， 如果失败不更新时间 后面每一条日志都会去尝试 open ， 系统调用风暴
        //
        m_fileStream.open(m_fileName , std::ios::out|std::ios::app); // 追加的形式 std::ios::app
        return m_fileStream.is_open(); // m_fileStream转 bool 
    }
private:
    std::string m_fileName;
    std::ofstream m_fileStream;
    std::time_t m_lastReopen = 0;
};

//描述： 我希望创建一个什么样的 Appender。
struct AppenderConfig{
    enum class Type{
        Stdout , File
    };
    Type type = Type::Stdout;
    LogLevel::Level level = LogLevel::DEBUG; // Appender 就有过滤的能力 ， 也有级别
    std::string formatter;                  // 保存的是 formatter的描述
    std::string file;
};

struct LoggerConfig {
    std::string name;
    LogLevel::Level level = LogLevel::DEBUG;
    std::string formatter;
    std::vector<AppenderConfig> appenders;
};



// 管理多个 logger 
// 会建立 root 
class LogManager{
public:
    static LogManager& GetInstance(){
        static LogManager instance; // 局部对象
        return instance;
    }
   
    Logger::ptr getRoot(){ 
        return m_root;
    }
    // 这里两个线程同时访问 ， 会同时创建两个 同名的logger
    // 需要对 map做互斥访问
    Logger::ptr getLogger(const std::string & name){ 
        //需要锁住 ： check-than-act 整个过程
        // 否则：在check之后 act之前cpu调度了， 之后别人也check ， 那么别人也会act 
        std::lock_guard<std::mutex>lock(m_mutex); // 这样整个函数都是一个整体
        auto it = m_loggers.find(name);
        if(it != m_loggers.end()){
            return it->second;  // 找到了
        }
        // 没有 就从新创建
        auto logger = std::make_shared<Logger>(name);
        logger->setRoot(m_root);
        m_loggers[name] = logger;
        return logger;
    }
    //读完配置之后 开始构造Logger
    void applyConfig(const std::vector<LoggerConfig>& configs){
        for(auto&  config : configs ){
            if(config.name.empty()){ // 名字是空的跳过
                continue;//todo : 报错 抛异常
            }
            // 从名字创建 logger  
            // 还能判断是否已经存在了
            // 如果已经存在时 重新创建一个新的，那么旧的要销毁，但是使用的是shared_ptr ， 无法立刻真正销毁
            // 别的地方拿到的还有可能是旧的logger ,所以不如 直接在旧的基础上改
            auto logger = getLogger(config.name); 
            logger->setLevel(config.level);
            //logger 在创建时就会配置默认的 logformatter 
            // 默认的logformatter 有默认的格式
            if(!config.formatter.empty()){
                //如果是空的 ， logger 
                //LogFormatter
                logger->setFormatter(config.formatter); // 
            }

            //对logger的appender: clear + rebuild
            // 选择清空appender 而不是在原来基础上改： 旧的无法去除， 
            //  stale runtime state : 旧运行状态残留
            // 但是在配置文件迭代中 即使只改一个level 也会引发清空重建 
            logger->clearAppender(); //不管logger是不是已经存在，都要从新刷新配置
            // note : 简单性 优先于 增量更新性能
            

            /*mark appender 的formatter : 
            *   1  如果appender 在初始化时 没有设置 formatter ,
            *       在加入logger是时，使用Logger的formatter
            *   2 如果初始化时有自己的formatter ,那就用自己的
            *   3 如果logger中 没有加入 appender , 那就使用 root的appender 
            *   4 所以 root 一定要给  appender  ， 强制性的 // 给的时候就会设置 formatter
            */  
            // 如果appender没有
            for(const auto & appender_config : config.appenders){
                //工厂式创建 
                LogAppender::ptr appender;
                if(appender_config.type == AppenderConfig::Type::Stdout){
                    appender = std::make_shared<StdOutLogAppender>();
                }else{
                    if(appender_config.file.empty()){ //文件目的地空
                        continue;  // todo: config error


                    }
                    appender = std::make_shared<FileLogAppender>(appender_config.file);
                }
                appender->setlevel(appender_config.level);
                if(!appender_config.formatter.empty()){
                    auto formatter = std::make_shared<LogFormatter>(appender_config.formatter);
            
                    if(!formatter->isError())           // todo : 后续记录错误 并反馈 ，要让用户知道           
                        appender->setFormatter(std::move(formatter) );
                }
                logger->addAppender(appender);
            }

        }
    }

private:
    //LogManager();
    explicit LogManager(){
        m_root = std::make_shared<Logger>("root");
        m_root->addAppender(std::make_shared<StdOutLogAppender>());
        m_loggers[ m_root->getName()] = m_root; 
    }
    mutable std::mutex m_mutex;
    /*"root"     -> Logger*
    "system"   -> Logger*
    "http"     -> Logger*
    "scheduler"-> Logger*
    */
    std::map<std::string ,Logger::ptr>m_loggers ; // 通过名字找 logger 
    Logger::ptr m_root;
};






// 宏定义
// (logger) -> getLevel() 日志器的 level
// 构建一个 event 
//级别足够高 才能输出
//1 先与日志器的等级比较一下 ， 大于日志器的等级 才能输出
//2 创建临时变量 LogEventWarp ， 用来管理 event 
//   一个event 就是一个输出 ， 在LogEventWarp析构时 执行输出
//    析构时： m_event->getLogger()->log( m_event->getLevel() , m_event );
//3 创建 event： 日志本身的 logger  level  ，  __FILE__ ， __LINE__ ， time 
//   getSS 返回的是 event 中的 stringstream 类型的 m_ss , 用来接收 正式的 日志内容
//   event中的 getLevel 是日志信息本身的 level 
#define MINI_LOG_LEVEL(logger , level) \
    if( (logger) -> getLevel() <= (level)  )\
        LogEventWarp( std::make_shared<LogEvent>(  \
            (logger) , (level) , __FILE__ , __LINE__ , \
                 std::time(nullptr) \
        ) ) .getSS()

#define MINI_LOG_ROOT()     LogManager::GetInstance().getRoot()
#define MINI_LOG_NAME(name) LogManager::GetInstance().getLogger(name) 

#define MINI_LOG_INFO(logger)   MINI_LOG_LEVEL( logger , LogLevel::INFO) 
#define MINI_LOG_DEBUG(logger)  MINI_LOG_LEVEL( logger , LogLevel::DEBUG)
#define MINI_LOG_WARN(logger)   MINI_LOG_LEVEL( logger , LogLevel::WARN) 
#define MINI_LOG_ERROR(logger)  MINI_LOG_LEVEL( logger , LogLevel::ERROR) 
#define MINI_LOG_FATAL(logger)  MINI_LOG_LEVEL( logger , LogLevel::FATAL) 

#define MINI_LOG_FMT_LEVEL(logger , level , fmt , ...) \
    if( (logger) -> getLevel() <= (level)  )\
        LogEventWarp( std::make_shared<LogEvent>(  \
            (logger) , (level) , __FILE__ , __LINE__ , \
                 std::time(nullptr) \
        ) ).getEvent()->format((fmt), __VA_ARGS__)

#define MINI_LOG_FMT_INFO(logger, fmt, ...) \
    MINI_LOG_FMT_LEVEL((logger), LogLevel::INFO, (fmt), __VA_ARGS__)

} // end of sylar 

