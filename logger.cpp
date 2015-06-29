

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

#include <logger.hpp>




/*
 * -----------------------------------------------------------------------------
 * Tests
 * -----------------------------------------------------------------------------
 */




void test_worker(int id, log::Severity level, log::Logger* logger) {
    // this is a simple working thread which should 
    // write something to the log

    std::this_thread::sleep_for(std::chrono::seconds(id % 2));

    int N = 5000;

    std::stringstream ss;

    for (int i = 0; i < N; ++i) {
        // prepare message
        ss << "worker #" << id << " is writing to the log: iteration " << i;
        logger->Log(level, ss.str());
        ss.str("");
    }

    ss << "worker #" << id << " is exiting";
    logger->Log(level, ss.str());
}


int main(int argc, const char* argv[]) {

    log::Logger logger(0, log::Trace);
    logger.SetLogFile("log_file0.txt");
    logger.StartBackGroundThread();


    logger.Log(log::Debug, "Start Logging");

    // create N worker threades which will write to the log
    int N = 50;
    std::thread* workers[N];
    for (int i = 0; i < N; ++i) {
        workers[i] = new std::thread(test_worker, i, log::Info, &logger);
    }
// The sum of bytes in all log files must converge to this number:  18646931
    for (int i = 1; i < 10; ++i) { 
        logger.ExitLogger();

        std::stringstream ss;
        ss << "log_file" << i << ".txt";
        logger.SetLogFile(ss.str());
        logger.StartBackGroundThread();
    }

    // wait untill all workers finished
    for (int i = 0; i < N; ++i) {
        workers[i]->join();
        delete workers[i];
    }

    // shutdown the logger
    logger.ExitLogger();


    //
    // Testing step 2
    //
    try
    {
        log::Logger l1 (50, (log::Severity)10);
    }
    catch (const std::exception& ex) {
        std::cerr << "STEP 2 ERROR: " << ex.what() << std::endl;
    }
    catch (...) {
        std::cerr << "STEP 2 ERROR: unknown" << std::endl;
    }

    return 0;
}
