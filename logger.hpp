
/*
 * Lock free, multithreaded Logger class implementation
 *
 * author: Maxim Alekseykin
 *
 * Requirements:
 *  1) Output should be done asynchromously
 *  2) Logging should not block working threads
 *  3) Should be as fast as possible
 *
 *
 */




#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <chrono>
#include <ctime>
#include <fstream>
#include <string>
#include <cstring>
#include <sstream>

#include <iostream>
#include <iomanip>

/* 
 * -----------------------------------------------------------------------------
 * This namespace contains everything I need for implementing lock-free
 * request queing
 * -----------------------------------------------------------------------------
 */
namespace lock_free {

// I need double-linked list here 
// to be able to print messages in a correct order
// NOTE: I only set up "prev" pointer when I dump it to the file
template<class T>
struct node {
    std::shared_ptr<T> val;
    node* prev = nullptr;
    node* next = nullptr;
};


/*
 * Simple linked-list based queue
 * lock free for pushing new items
 */
template<class T>
class que {
    std::atomic<node<T>*> head = {nullptr};

public:
    que() = default;
    que(const que&) = delete;
    que& operator=(const que&) = delete;

    void push(std::shared_ptr<T>& pVal) {
        auto p = new node<T>();
        p->val = pVal;
        p->next = head.load();  

        while (!head.compare_exchange_weak(p->next, p)) 
        { /*some logic to stop the loop if necessary*/ }
    }

    /* this method refreshes the internal que to empty one
     * and return already filled one for further processing, so that the
     * clients are able to continue filling in the new empty que
     */
    node<T>* dump_que() {
        auto p = head.load();
        while (!head.compare_exchange_weak(p, nullptr)) {}
        return p;
    }
};


}





/*
 * -----------------------------------------------------------------------------
 * Logger class' logic
 * -----------------------------------------------------------------------------
 */

namespace log {

// just defines a type I use in the lock-free queue
typedef const char CONSTSTR;


enum Severity {
    Trace = 0,
    Debug,
    Info,
    Error,
    SeverityNumber,
};

const char* severity_to_str(Severity level) {
    static const char* severity_to_str [] = {
        "Trace", "Debug", "Info", "Error"
        };

    // I do not check parameters in API functions
    // if (Trace <= level & ...

    return severity_to_str [level];
}




class Logger {

    // my lock-free queue for messages
    lock_free::que<CONSTSTR> que;

    // atomic flag to shutdown the working background thread
    std::atomic<bool> shutdown = {false};

    
    // so it are consts so that I'm not planning to set it in run time
    const Severity current_level = Debug;
    const int current_timeout    = 2;        // in seconds

    // background working thread stuff
    // I use this mutex to be able to start / stop it from any thread
    // i.e. it'll be a thread safe access
    std::mutex worker_mutex;
    std::atomic<std::thread*> worker = {nullptr};

    // shared pointer to the output stream allows me to change output file from any thread
    // here I have to use mutex bcoz atomic functions for shared_ptr are not supported by my compiler
    std::mutex log_stream_mutex;
    std::shared_ptr<std::ofstream> log_stream;


    // deleter for output stream
    static void log_file_closer(std::ofstream* fout) {

        // this guy will call to "delete fout" at the end of the method
        std::unique_ptr<std::ofstream> finally_guard(fout);
        
        try {
            // as soon as it's called from shared_ptr object I _do_not_ check parameters
            if (fout->is_open()) {
                fout->flush();
                fout->close();
            }
        }
        catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
        }
        catch (...) {
            std::cerr << "unknown exception happened on closing log file" << std::endl;
        }
        
    }

    // method does two important things:
    // 1) set "prev" porinter, so I can traverse it backward
    // 2) returns the last item
    static lock_free::node<CONSTSTR>* get_tail(lock_free::node<CONSTSTR>* head) {
        lock_free::node<CONSTSTR>* p = head;
        while (p->next) {
            p->next->prev = p;
            p = p->next;
        }
        return p;
    }

    void write(const char* msg) {
        // I protect calling thread from exception coming from this function
        try {
            std::shared_ptr<std::ofstream> tmp(log_stream);
            if (tmp && tmp->is_open()) {
                *tmp << msg << std::endl;
            }
            else {
                // file is not opened or already closed
                // so let's write everything to the standard output
                std::cout << msg << std::endl;
            }   
        }
        catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
        }
        catch (...) {
            std::cerr << "unknown exception on writing" << std::endl;   
        }
    }


    // writes msg que to the log file in a correct order
    void dump_que(lock_free::node<CONSTSTR>* p) {
        // to traverser the que in the right direction I need to go to the ent of it
        if (p) {
            p = get_tail(p);

            // protect access to the output stream durint writing
            std::lock_guard<std::mutex> lock(log_stream_mutex);
            while (p) {
                write(p->val.get());
                lock_free::node<CONSTSTR>* tmp = p;
                p = p->prev;

                // release the current node
                delete tmp;
            }
        }
    }


    // thread function
    // in case of any exception during writing to the log
    // this function just exits, so it should be started again
    // NOTE: "write" is nothrow, so in general this method should not have any exception inside
    static void get_it_done(Logger* This) {
        try {
            while (false == This->shutdown.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(This->current_timeout));
    
                // get what ever we've already got in the buffer
                lock_free::node<CONSTSTR>* p = This->que.dump_que();
                This->dump_que(p);
            }

            // check if there is anything else left in the queue
            lock_free::node<CONSTSTR>* p = This->que.dump_que();
            This->dump_que(p);
        }
        //
        // by the PLAN I must not go into any of this catch blocks !!!
        //
        catch(const std::exception& ex) {
            // some error processing should be here
            std::cerr << ex.what() << std::endl;
        }
        catch(...) {
            // some error processing should be here
            std::cerr << "unexpected error in background thread" << std::endl;
        }
    }


    // utility method which should be moved to some library
    static std::string datetime() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        char tmp[128];
        std::strftime(&tmp[0], sizeof(tmp), "%d.%m.%Y %H:%M:%S", std::localtime(&in_time_t));

        return std::string(tmp);
    }

public:
    Logger() = default;
    Logger(int timeout, Severity level) :
        current_timeout(timeout),
        current_level(level)
    {
        // sanity check for parameters

        if (level < Trace || Error < level)
            throw std::runtime_error("Log level is invalid");
        if (timeout < 0)
            throw std::runtime_error("timeout cannot be negative");
    }
    Logger(const Logger&) = delete;
    Logger& operator= (const Logger&) = delete;

    ~Logger() {}

    void SetLogFile(const std::string& fname) {
        // protect output stream during changing
        std::lock_guard<std::mutex> lock(log_stream_mutex);

        if (fname.empty()) {
            std::shared_ptr<std::ofstream> tmp;
            log_stream.swap(tmp);
        }
        else {
            std::shared_ptr<std::ofstream> fout(new std::ofstream(fname), log_file_closer);
            if (fout && fout->is_open()) {
                log_stream.swap(fout);
            }
            else {
                // error processing should be her
                std::cerr << "cannot open log file \"" << fname << std::endl;
            }
        }
    }

    void StartBackGroundThread() {
        /*
         * This essentially implements logger as a singleton
         */
        std::thread* tmp = worker.load();
        if (nullptr == tmp) {
            std::lock_guard<std::mutex> lock(worker_mutex);
            tmp = worker.load();
            if (nullptr == tmp) {
                shutdown.exchange(false);
                tmp = new std::thread(Logger::get_it_done, this);
                worker.store(tmp);
            }
        }
    }

    void ExitLogger() {
        std::thread* tmp = worker.load();
        if (tmp != nullptr) {
            std::lock_guard<std::mutex> lock(worker_mutex);
            tmp = worker.load();
            if (tmp != nullptr) {
                shutdown.exchange(true);
                tmp->join();
                delete tmp;
                worker.store(nullptr, std::memory_order_relaxed);

                // flush log file if any
                SetLogFile("");
            }
        }
    }


    /*
     * In this method I fully take control over
     * allocating memory for strings which are going to the log
     * 
     */
    void Log(Severity level, const std::string& msg) {
        // sanity check for parameters
        if (level < log::Trace || log::Error < level ||
            msg.empty()) {
            std::cerr << "invalid parameters for Log function: " << level << "; " << msg << std::endl;
            return;
        }
    

        if (level >= current_level) {
            std::stringstream ss;
            ss << datetime() << " " << severity_to_str(level) << ": " << msg;

            std::shared_ptr<char> tmp(new char [ss.str().length() + 1]);
            strcpy(tmp.get(), ss.str().c_str());

            std::shared_ptr<const char> tmp_const = std::const_pointer_cast<const char>(tmp);
            que.push(tmp_const);
        }
    }

};

}





