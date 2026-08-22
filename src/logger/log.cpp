#include"log.h"
// #include"logger.h"
#include<iostream>
#include <utility>
#include<ctime>
#include<chrono>
#include<iomanip>
#include<tuple>
#include<vector>
#include<functional>
#include<map>

// 日志绝对时间 ： 使用 std::time_t 
// 日志累计运行时间 ： 是使用 steady_clock

namespace sylar{
namespace { // 匿名 :这些变量只属于当前 log.cpp 翻译单元，不暴露为整个工程可直接访问的全局符号
// 记录：  log.cpp 这组静态对象初始化时的 steady_clock 时间点
// g_process_start 在main 之前初始化  ， 非常接近系统启动时间，但还是有差别的
const auto g_process_start = std::chrono::steady_clock::now(); 
thread_local std::string t_thread_name = "unnamed";//线程存在但是还没有被命名
thread_local std::uint64_t t_fiber_id = 0;  //todo: 尚未接入 Fiber 
/*mark : thread_local
*  g_process_start : 所有线程共享一个变量
*  t_thread_name ：  每个 OS 线程各自拥有一份独立实例
*   t_thread_name ： 是属于线程的独自私有的 ， 所以必须独占
*/
}
}

//class LogContext
namespace sylar{
// 不使用 std::time(nullptr) ： 其 wall clock / 日历时间 ， 受系统时间调整影响
std::uint64_t LogContext::ElapsedMs(){
    const auto now = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - g_process_start).count() );
}

//mark pass-by-value + move 的意义：
//
void LogContext::setThreadName(std::string name){ 
    t_thread_name = std::move(name);
}

const std::string& LogContext::getThreadName(){ // 调用者 不能通过返回值修改 加const 
    return t_thread_name;
}

void LogContext::setFiberId(std::uint64_t id) {  // 每次Fiber切换时，要切换
    t_fiber_id = id;
}

std::uint64_t LogContext::getFiberId() {
    return t_fiber_id;
}

}


// class LogLevel 
namespace sylar{ 
const std::string LogLevel::LevelToString(Level level){
    switch (level)
    {
    case DEBUG: return "DEBUG";
    case INFO: return "INFO";
    case WARN: return "WARN";
    case ERROR: return "ERROR";
    case FATAL: return "FATAL";
    default: return "UNKNOW";
    }
}
LogLevel::Level LogLevel::StringToLevel(const std::string & text){
    if (text == "debug" || text == "DEBUG") return DEBUG;
    if (text == "info"  || text == "INFO")  return INFO;
    if (text == "warn"  || text == "WARN")  return WARN;
    if (text == "error" || text == "ERROR") return ERROR;
    if (text == "fatal" || text == "FATAL") return FATAL;
    return DEBUG; // 作为未知字符串的兜底，
    //若原来是error 拼错 ， 会导致输出大量的 DEBUG  // logger 或者 LogAppender的级别设置错误 
}

}// end of LogLevel  namespace 

// class LogEvent
namespace sylar
{
    void LogEvent::format(const char * fmt , ...){
        va_list ap;// 声明变量
        va_start(ap ,fmt); // 将fmt 后面的参数都塞进去
        format(fmt ,ap);
        va_end(ap);
    }
    void LogEvent::format(const char* fmt, va_list ap){
        va_list copy;// 声明变量
        va_copy(copy , ap);//克隆一个备用的
        int len = std::vsnprintf(nullptr , 0 , fmt, copy);
        va_end(copy);
        if(len <= 0) return ;
        std::vector<char> buf(len +1);
        /*mark ：vsnprintf()
        *   buf: 要写入的目标数组
        *   buf.size : 允许写入的最大字节数
        *   fmt ： 带占位符的格式化字符串
        *   vlist 可变参数列表
        *   返回写入的字符串
        */
        std::vsnprintf( buf.data() , buf.size() , fmt ,ap );//
        m_ss.write(buf.data() , len);
    }
        /*mark  调用格式 format("port=%d name=%s",8080,"sylar");
        * 
        *    对外接口
                ↓
            内部统一实现
        *   format(const char* fmt, ...) ： 负责接收不定参数
        *   format(const char* fmt,va_list ap) ： 完成真正格式化
        *       
        */
} // namespace LogEvent


// class Logger 
namespace sylar{ 
void Logger::log( LogLevel::Level level ,LogEvent::ptr event  ){
    //mark  读操作要不要加锁取决于 读的同时有没人在写
    // 在读的过程中 别的线程执行一个 clearAppender , 会改变 list, 导致迭代器失效
    // 因此 appender->log时一定要加锁 => 导致 IO时间过长

    if(level < m_level.load()){  //要输出的级别不够  // 别人也有可能要setlevel()
            return ;
    }

    
    /* mark 如果全都加锁的话 ， 整个IO时间太长 如写文件时间很慢
    *  所以 复制一份之后， 使用复制的新的局部变量去IO ， 
    * 不用去加锁 ， 不会影响
    *
     */
    // 备份
    std::list<LogAppender::ptr>appenders;
    Logger::ptr root; 

    {
        
        std::lock_guard<std::mutex>lock(m_mutex); 
        appenders = m_appenders;
        root = m_root; 
        /*mark ： 复制的是智能指针， 只是计数器加1 
        * 即时 另外一个线程 clearAppender ， 局部变量中的 shared_ptr 还会活着 
        * 
        * 复制一个 list , 这样在输出时即便是不加锁 也不会导致冲突
        * 复制一个 root ， 防止在进行 root-> log() 时 ， 别的线程将此logger的root改了
        *  ， 执行 set->root  
        */
    }
    /*mark : 这里并没有对 appender->log 加锁
    * 因为在 appender->log 内部加了， 并且在每个修改appender的函数中都有锁
    */
    if(!appenders.empty()){   // 自己有 appender 就自己输出
         // logger的 log() 将要输出的event 传给 appender
        // appender的 log 需要 logger 本身， level 以及 event
        for(auto & appender : appenders){
            appender->log( shared_from_this() ,level , event ); // 
            // 对appender->log的锁在其内部加
        }
    }else{  // 自己没有的话 就使用root的  , 防止丢失日志
        root->log(level , event);
    }
}
void Logger::debug(LogEvent::ptr event ) {
    log(LogLevel::DEBUG , event );
}
void Logger::info(LogEvent::ptr event ) {
    log(LogLevel::INFO , event );
}
void Logger::warn(LogEvent::ptr event ) {
    log(LogLevel::WARN  , event);
}
void Logger::error(LogEvent::ptr event ) {
    log(LogLevel::ERROR , event);
}
void Logger::fatal(LogEvent::ptr event ) {
    log(LogLevel::FATAL , event );
}


// Logger 中的setFormatter
// Logger 中修改formatter 
// 因为修改 Logger formatter 的同时，还要把新的 formatter 传播给那些没有专属 appender 的目的地
void Logger::setFormatter(LogFormatter::ptr formatter){
    std::lock_guard<std::mutex>lock(m_mutex);
    if(!formatter || formatter->isError()) return ;  // 格式初始化有问题

    m_formatter = std::move(formatter);
    
    for(auto & appender:m_appenders){
         std::lock_guard<std::mutex>app_lock(appender->m_mutex);
            //? : M_hasFormatter 是否有必要， 
            //? 都是改appender的formater ,为什么与 Logger::addAppender 中的不一样， 
            //? 为什么不直接调用appender->setFormatter
        if(!appender->m_hasFormatter){ 
            appender->m_formatter = m_formatter;
            //appender->setFormatter(m_formatter);
            // mark 这里不能用appender->setFormatter , 会导致同时对mutex加锁两次，死锁
            /* mark : m_haFormatter 专门表示有无专属的Formatter 
                    没有的时候 ， 其formatter依赖 logger 
            */
        }
    }
}
void Logger::setFormatter(const std::string& pattern ){
    auto formatter = std::make_shared<LogFormatter>(pattern);
    if(!formatter->isError()){
        setFormatter(formatter);
    }
}
// addAppender 需要加两层锁
// 
void Logger::addAppender(std::shared_ptr<LogAppender>appender){
    // 保护 m_appenders , 多个线程使用这同一个 logger 
    // 只能保证 使用同一个 logger的线程会阻塞到这个m_mutex
    std::lock_guard<std::mutex>lock(m_mutex);

    {
        //1  此处appender->m_mutex 是appender的
        //   有可能有多个线程 都对这个 appender 进行setFormatter
        //   多个线程使用不同的 logger 对同一个appender 进行修改
        //2  Logger设成LogAppender 的友元， 所以在Logger中能使用appender的protected成员 m_mutex
        std::lock_guard<std::mutex>app_lock(appender->m_mutex);
        // TODO 加一个标志位 实现这个appender ,每次就使用 每一个Logger 的formatter
        /* mark：这个标志 m_hasFormatter,  专门给 Logger 使用的
        *  用来区分用户 
        *        1 没给appender设置 formatter
        *        2 用户专门设置了专属 formatter 
        */
        if(!appender->m_hasFormatter)  // 如果目的地没有 formatter ， 就是用Logger的
            appender->m_formatter = m_formatter;
    }
    

    m_appenders.push_back( std::move( appender));
}
}// end of Logger  namespace 

/* mark :
Logger::log()
    加锁 → 复制 m_appenders / m_root → 立即解锁
    目的：保护 Logger 的配置，并缩短临界区

StdOutLogAppender::log()
    加锁 → 判断级别 → 格式化 → 输出 → 解锁
    目的：保护 Appender 自身状态，并保证一整条日志不会和其他线程交叉输出
*/
// class LogAppender 
namespace sylar{ 

    void LogAppender::setlevel(LogLevel::Level level){
        m_level.store(level);
    }
    void LogAppender::setFormatter(LogFormatter::ptr formatter){
        std::lock_guard<std::mutex>lock(m_mutex);
        m_formatter = std::move(formatter);
        m_hasFormatter = static_cast<bool>(m_formatter);
       // std::cout<<__LINE__<<"  "<<m_hasFormatter<<std::endl;
        //mark 专门给用户用的 ，设置此appender 专属 formatter
    }
    LogFormatter::ptr LogAppender::getFormatter(){
        // 防止同时 有人改，有人取
        std::lock_guard<std::mutex>lock(m_mutex);
        return m_formatter;
    }

void StdOutLogAppender::log(std::shared_ptr<Logger>logger ,  //为了输出logger 名字
                    LogLevel::Level level , 
                    std::shared_ptr<LogEvent>event) {
    
    //多个线程很可能同时执行 StdOutLogAppender::log()。
    // 都需要 使用 std::cout 
    // 再往下 每个 FormatItem 中的format  都没有加锁了
    // 同时 还保护 m_formatter ，  防止别人 setFormatter 
    if(level < m_level.load()) return; // 级别不够这个目的地的
    std::lock_guard<std::mutex>lock(m_mutex);  // 这是加给 cout 
    if(m_formatter)
        std::cout<<m_formatter->format(logger , level , event);

  
}

void FileLogAppender::log(std::shared_ptr<Logger>logger ,  //为了输出logger 名字
                    LogLevel::Level level , 
                    std::shared_ptr<LogEvent>event) {
   

    if(level < m_level.load()) return; // 级别不够这个目的地的

    //锁保护m_fileStream  // 这是加给 m_fileStream  ， 防止输出紊乱的
    std::lock_guard<std::mutex>lock(m_mutex); 
    
    // 不是真正的异步
    const std::time_t now = event->getTime(); // 使用日志记录时间
    if (now >= m_lastReopen + 3) {//经典工程折中
        //即使没有轮转，也会周期 open/close
        reopenUnlocked();
    }

    //if(!m_fileStream && !reopen() ) return; 
    //todo  m_formatter 还有必要设置吗 
    //答 ： 仍做防御性检查。
    // 每个Logger 有默认格式 
    // 每个appender 在加入 logger时都会 被设置 formatter //有自己的就用自己的
    // 如果logger 自己没有 appender 就用 root的
    // root logger一定有 appender // 否则会报错
    // 综上 ： ！ m_formatter 无需判断 
    if(! m_fileStream.is_open() || !m_formatter ){  
        std::cerr << "[sylarlog] cannot write log file: " << m_fileName << '\n';
        return;
    }

    
    m_fileStream << m_formatter->format( logger , level , event);
    // m_fileStream.flush(); // 刷新缓冲区 // 不要再每条日志都 flush
    //打开成功：不等于 每次写入一定成功。
    if(!m_fileStream) {
        std::cerr << "[sylarlog] write failed: " << m_fileName << '\n';

    }
}

// 这是生产者调用的
// 新日志入队
void AsyncFileLogAppender::log(std::shared_ptr<Logger>logger ,  //为了输出logger 名字
                    LogLevel::Level level , 
                    std::shared_ptr<LogEvent>event) {
   

    if(level < m_level.load()) return; // 级别不够这个目的地的
    //std::lock_guard<std::mutex>lock(m_mutex); // 异步这里根本不需要，因为没有真正的写缓冲区

    if(!m_accepting.load()){
        ++m_rejectAfterStop;
        return ;
    }

    // 这里面加了锁 
    //因为整个log 没有加m_mutex ， 所以外部有可能set m_formater ,因此使用getFormatter得到一个副本 
    // shared_ptr snapshot
        /*业务线程取到旧 shared_ptr
        ↓
        配置线程替换 formatter_
        ↓
        旧 Formatter 只要业务线程还持有
        就不会析构
        ↓
        本次日志安全完成格式化*/
    auto fmt = getFormatter(); 
    if(!fmt){
        return ;
    }

    // 这里不需要加锁，都是局部变量
    std::string line = fmt->format(logger , level , event);
    std::size_t bytes = line.size();

    // 单个的日志直接冲破了限制 ，
    // 后面的wait( fitsUnlocked(bytes) )  //如果不处理， 死锁
    // 在进入阻塞等待之前，先排除永远不可能满足的条件。
    if(bytes > m_options.max_queue_bytes){
        ++m_oversized;
        ++m_dropped;
        return;
    }

    std::unique_lock<std::mutex>lock(m_queuemutex);

    if(!m_accepting.load() || m_stop){
        ++m_rejectAfterStop;
        return;
    }

    // FATAL 默认不走 drop-new：即使普通日志采用丢弃策略，也等待一个队列槽位。
    const bool preserve_record = level >= LogLevel::FATAL;
    if(m_options.overflow_policy == OverflowPolicy::BlockProducer || preserve_record){
        m_notFullcv.wait(lock , [&]{
            return m_stop || m_accepting.load() || fitsUnlocked(bytes);  // 有槽位时唤醒
        }  );
        if(m_stop || !m_accepting.load()){ // 是因为 异步appender 关闭而唤醒
            ++m_rejectAfterStop;
            return ;
        }
    }else if(!fitsUnlocked(bytes)){ // DropNewest 是因为
        ++m_dropped;
        return;
    }
    
    // 你的下面就是入队 : 如果为空 ，说明worker 很可能在wait 
    // 新日志入队之后, 这时候应该唤醒
    const bool need_wakeup = m_queue.empty(); 

    std::uint64_t seq = ++m_nextSequence;
    m_queue.push_back({seq, std::move(line)});
    m_queue_bytes += bytes;
    // 成功进入共享队列的总记录数；当前实现中与最后已分配 sequence 数值同步增长。
    ++m_enqueued; //todo  入队条数++ ?
    
    // 已经达到批量阈值唤醒门槛
    //todo worker 有可能在等待特定时间刷新 
    bool reached_batch = m_queue.size() >= m_options.batch_items;
    bool high_priority = level >= m_options.flush_on_level; //有大于这个level的日志出现立刻唤醒worker

    if(high_priority || reached_batch){
        m_forceFlush = true;
    }
    lock.unlock();


    if(need_wakeup || reached_batch ||high_priority){
        m_cv.notify_one();
    }

    if(m_options.sync_flush_fatal && level >= LogLevel::FATAL){
        flush();
    }

    // 唤醒 worker ,在生产者释放m_queuemutex，之后再唤醒worker 
    // 并且只有一个 worker
    //每一都会写： 不好 
    // 避免频繁唤醒 worker 
    // if(logCnt >= m_capacity * 0.5)
    //     m_cv.notify_one(); 
}

// 等待调用这一刻之前“已经成功入队”的日志被 worker 处理到目标 sequence。
// queue.empty() 不能作为完成条件：worker 可能已经 swap 到本地 batch，但尚未写文件。
// 因此先取 target=enqueued_，强制唤醒 worker，再等 flushed_sequence_ >= target。
//
// 注意：这里的 flush 仍是 ostream flush 级语义，不是 fsync 断电持久化保证。
void AsyncFileLogAppender::flush(){
    // todo 
    std::uint64_t target = m_enqueued.load(); // 当前的入队数
    {
        std::lock_guard<std::mutex>lock(m_queuemutex);
        m_forceFlush = true;
    }

    m_cv.notify_one(); // 唤醒worker

    std::unique_lock<std::mutex>lock(m_queuemutex);
    m_drainedCv.wait(lock , [&]{
        return m_flushedSequence.load() >= target || ( m_stop && m_queue.empty() ) ;
    });
    

}
bool AsyncFileLogAppender::fitsUnlocked(std::size_t bytes) const {  //先过滤
    return  m_queue.size() < m_options.max_queue_items 
            && m_queue_bytes + bytes < m_options.max_queue_bytes;     
    // /条数没超过并且当前字节数 + 新日志字节数没超过 才能真的入队
}
// -----------------------------
// 单消费者后台线程主循环
// -----------------------------
// 核心思想：共享队列只在很短临界区访问；真正文件 I/O 在锁外。
// wait -> 可选短聚合 -> 判断停止 -> swap 到本地 batch -> 释放 queue lock
// -> writeBatch -> clear -> 下一轮。
void AsyncFileLogAppender::workerLoop(){
    // 共享数据快速搬到线程本地，然后锁外做慢工作。

    LogContext::setThreadName("Loger_worker");
    std::deque<std::string> batch;
    while(true){
        bool force_flush = false;

        {        // 对队列的操作
            std::unique_lock<std::mutex> lock(m_queuemutex);
            // predicate 版本会检查：
                //真有日志吗？
                //正在停止吗？
            // m_stop放入 predicate中检查： 避免了系统停止， 但是没人告诉woker 
            // m_stop 告诉worker , 系统有没有停
            m_cv.wait(lock , [this](){
                // queue empty且not stopping ,阻塞: 
                // 
                return m_stop || m_forceFlush ||  !m_queue.empty();  // 返回true 则不阻塞 
                //m_forceFlush : 需要强制刷新了 ， 唤醒
            });

            //第一条日志来了之后 ，给生产者一个短暂聚合窗口， 减少“一条日志一次 flush”。
            // 系统没停止 && 没有强制flush && 队列非空 && 队列没有达到 batch_items
            // 没有达到最大聚合窗口时间
            if(!m_stop &&!m_forceFlush &&
               !m_queue.empty() &&  m_queue.size() < m_options.batch_items  &&
                m_options.flush_interval.count() > 0){ // todo 如何实现的 

                m_cv.wait_for(lock ,m_options.flush_interval , [&](){
                    return m_stop || m_forceFlush || m_queue.size() >= m_options.batch_items;
                }  );
            } 
            
            
            batch.swap(m_queue); // m_queue 变空了 // 交换队列 
            m_queue_bytes = 0;
        }
        // 写磁盘很慢， 将共享的资源 m_queue 的副本去写
        //避免生产者长时间 不能enqueue
        for(const auto & line:batch){ // 只有这一个线程在往文件里面写
            m_fileStream <<line;
        }
        m_fileStream.flush();
        batch.clear();

    }
    m_fileStream.flush();

}

}// end of LogAppender  namespace 



// class LogFormatter  
//解析 "%p%T%c%T%m%n" 
namespace sylar{ 

class MessageItem:public LogFormatter::FormatItem {
public:
    explicit MessageItem(const std::string& = /*unused*/""){} 
    virtual void format( std::ostream & os ,
                        Logger::ptr /* logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr event ) override {
        os << event->getContent();
    }
};
class LevelItem:public LogFormatter::FormatItem{
public:
    explicit LevelItem(const std::string& = /*unused*/""){} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level level, 
                        LogEvent::ptr /*event*/ )override {
        os << LogLevel::LevelToString(level);
    }
};
// logger的 name 
class NameItem:public LogFormatter::FormatItem {  
public:
    explicit NameItem(const std::string& = /*unused*/""){} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  logger,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr /*event*/ )override{
        os << logger->getName();
    }
};

class FileItem:public LogFormatter::FormatItem {  
public:
    explicit FileItem(const std::string& = /*unused*/""){} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr event )override{
        os << event->getFile();
    }
};
class LineItem:public LogFormatter::FormatItem {  
public:
    explicit LineItem(const std::string& = /*unused*/""){} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr event )override{
        os << event->getLine();
    }
};

class NewLineItem:public LogFormatter::FormatItem {  
public:
    explicit NewLineItem(const std::string& = /*unused*/""){} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr /*event*/ )override{
        os << "\n";
    }
};
class TableItem:public LogFormatter::FormatItem {  
public:
    explicit TableItem(const std::string&  = /*unused*/""){} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr /*event*/ )override{
        os << "\t";
    }
};

// 格式中的 一些符号 ：[] 等
class StringItem:public LogFormatter::FormatItem {  
public:
    explicit StringItem(const std::string  str):m_string(str) {} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr /*event*/ )override{
        os << m_string;
    }
private:
    std::string m_string;
};

class DateTimeItem:public LogFormatter::FormatItem {  
public:
    explicit DateTimeItem(const std::string fmt ):  
        m_format(fmt.empty() ? "%Y-%m-%d %H:%M:%S" : std::move(fmt)  ){}  // datatime 的格式
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr event )override{
            std::time_t t = event->getTime();

            std::tm localTime{};
#if defined(_WIN32)
            // windows 版 线程安全
            localtime_s(&localTime, &t);
#else
            // POSIX 线程安全的
            localtime_r(&t, &localTime);
#endif
            os << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S"); // iomanip
    }
private:
    std::string m_format;
};

class ThreadIdItem:public LogFormatter::FormatItem {  
public:
    explicit ThreadIdItem(const std::string = "" /*unused */ ){} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr event )override{
        os << event->getThreadId();
    }
};

class ElapsedItem:public LogFormatter::FormatItem {  
public:
    explicit ElapsedItem(const std::string = "" /*unused */ ){} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr event )override{
        os << event->getElapsedms();
    }
};
class FiberItem:public LogFormatter::FormatItem {  
public:
    explicit FiberItem(const std::string = "" /*unused */ ){} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr event )override{
        os << event->getFiberId();
    }
};
class ThreadNameItem :public LogFormatter::FormatItem {  
public:
    explicit ThreadNameItem(const std::string = "" /*unused */ ){} 
    void virtual format( std::ostream & os ,
                        Logger::ptr  /*logger*/,  // 这种写法代表接口需要这个参数，但是实际上没有用到
                        LogLevel::Level /*level*/, 
                        LogEvent::ptr event )override{
        os << event->getThreadName();
    }
};
void LogFormatter::init(){


    using Tuple = std::tuple<std::string , std::string , int>; // text/token,  fmt, type(0 text,1 token)
    // 将 text 和 token 都放进来 
    // 用三元组表示 ： 0 text  
    //                1 token
    std::vector<Tuple> parsed;
    std::string normalText;

    for(std::size_t i = 0 ; i < m_pattern.size() ; ++i){
            
            // 判断第i个 是不是字符 
            if( m_pattern[i] != '%'){
                normalText.push_back(m_pattern[i]) ; 
                continue;
            }

            // m_pattern[i] == % 
            //下面研究 m_pattern[i+1]

            // 判断 %%
            //首先保证 i 不是最后一个 
            if(i +1 < m_pattern.size() && m_pattern[i+1] == '%'){ // 
                normalText.push_back(m_pattern[i+1]) ;
                i++;
                continue;  // 跳过 %%
            }

            // 先保存% 之前的普通字符
            if(!normalText.empty()){
                parsed.emplace_back(normalText , "" , 0 );
                normalText.clear();
            }
            // i是最后一个
            // 单独的一个 % 在结尾 ， 错误格式
            if(i + 1 >= m_pattern.size()){
                m_error = true;
                parsed.emplace_back("<<pattern_error % >>" , "" , 0 );
                break;
            }
            

            // 目前只读取 % 后一个字符  
            std::string token(1, m_pattern[i + 1]);
            // i ->% 
            // 如： %d
            i+=1; // i-> d 
            std::string subFormat;
            // i->d 
                /* 如果格式后面紧跟 {  } 
                *例如：  %d{%Y-%m-%d %H:%M:%S}
                *token = d 
                * subFormat = "%Y-%m-%d %H:%M:%S"
                */
            //if(token[0] == 'd' && )  // 不一定只有 %d 后面有{ } 子格式i+
            if( i+1 < m_pattern.size() && m_pattern[i+1] == '{'){
                std::size_t leftBrace = i+1; // '{'
                std::size_t rightBrace= m_pattern.find('}' , leftBrace +1 );
                if(rightBrace == std::string::npos){  // 没找到 ' } ' , 错误格式 
                    m_error = true;
                    parsed.emplace_back("<<pattern_error: missing } >>" , "" , 0);
                    break;
                }
                subFormat = m_pattern.substr(leftBrace +1 , rightBrace - leftBrace -1 );

                i = rightBrace; // 跳过 { ....... }
            }
            parsed.emplace_back( token , subFormat , 1);
    }// end of for    

    
    if(!normalText.empty()){
        parsed.emplace_back(normalText , "" , 0 );
        normalText.clear();
    }
      
    // 参数为了 统一 ， StringFormatItem的构造使用了
     // 一个用于创建 FormatItem 对象的函数
    using ItemFactory = std::function<FormatItem::ptr(const std::string)>;
    //字符串 → 创建 FormatItem 的函数 
    // token -> 创建 FormatItem子类 

    static const std::map<std::string , ItemFactory> factories = {
        {
            "m" , [](const std::string & f){ 
                return std::make_shared<MessageItem>(f);   // 这就是为什么要 统一 模式子类的构造函数
            }  
        },
         {
            "p" , [](const std::string & f){ 
                return std::make_shared<LevelItem>(f); 
            }  
         },
         {
            "c" , [](const std::string & f){ 
                return std::make_shared<NameItem>(f); 
            }  
         },{
            "f" , [](const std::string & f){ 
                return std::make_shared<FileItem>(f); 
            }  
         },{
            "l" , [](const std::string & f){ 
                return std::make_shared<LineItem>(f); 
            }  
         },
         {
            "n" , [](const std::string & f){ 
                return std::make_shared<NewLineItem>(f); 
            }  
         },
         {
            "T" , [](const std::string & f){ 
                return std::make_shared<TableItem>(f); 
            }  
         },
         {
            "d" , [](const std::string & f){ 
                return std::make_shared<DateTimeItem>(f); 
            }  
         },{
            "t" , [](const std::string & f){ 
                return std::make_shared<ThreadIdItem>(f); 
            }  
         },{
            "r" , [](const std::string & f){ 
                return std::make_shared<ElapsedItem>(f); 
            }  
         },{
            "F" , [](const std::string & f){ 
                return std::make_shared<FiberItem>(f); 
            }  
         },{
            "N" , [](const std::string & f){ 
                return std::make_shared<ThreadNameItem >(f); 
            }  
         }
    };

    for(auto & item : parsed){
        if(std::get<2>(item) == 0){
            m_items.push_back(std::make_shared<StringItem>( std::get<0>(item)));
            continue;
        }

        //下面 ： std::get<2>(item) == 1 
        auto it = factories.find(std::get<0>(item));
        if(it    == factories.end()){
            m_error = true;
            m_items.push_back(std::make_shared<StringItem>("<<unknow_format>>"));
        }else{
            m_items.push_back(it->second( std::get<1>(item)));
        }

    }


}




std::string LogFormatter::format(const std::shared_ptr<Logger>&logger , 
                                    LogLevel::Level level,   // 这是要输出的 level
                                    const LogEvent::ptr& event){  // event 中不包含 level
    std::ostringstream out;
    for(auto& item : m_items)   {
        item->format(out , logger , level , event);
    }                                 
    return out.str();
}

// void LogFormatter::format(const std::shared_ptr<Logger>&logger , 
//                                     LogLevel::Level level,   // 这是要输出的 level
//                                     const LogEvent::ptr& event){  // event 中不包含 level 
//     for(auto& item : m_items)   {
//         item->format(os , logger , level , event);
//     }                                 
// }



}// end of LogFormatter  namespace 




