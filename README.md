# Logger
lock free Logger for low latency apps

1) Logger class interface

class Logger{
  Logger()
  public bool SetLogFile(std::string filename)
  public void StartBackGroundThread()
  public bool ExitLogger()
  public void Log(int log_level, .......)
  ~Logger()
}

2) public interface of the Logger class is _fully_ thread-safe
3) writing to the log is fully asynchronous and lock-free
4) I _did_not_ implement it as a Singleton (in common meaning of this pattern) just because I think that Singleton must be implemented on the level of the application architecture. i.e. as it is implemented in the test part of this project: an instance of Logger is created and initialized  _before_ any client, then all clients (test threads) receive a pointer to this instance.
5) tests are included into to the solution.
6) to make it easier all code is included into a single file
7) memory management is out of scope (for example it's possible to implement small-objects allocator for log messages. this will prevent app's heap from fragmentation)
