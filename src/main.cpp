#include <iostream>
#include <chrono>
#include <concepts>
#include <thread>

#include "event.h" 

class TestEvent : public EventData
{
private:
     
public:
    virtual ~TestEvent() override {}
};

class YummyEvent : public EventData 
{
private:
     
public:
    virtual ~YummyEvent() override {}
};

class TestEventHandler
{
public:
    int tests = 0;

    void handle(TestEvent* event)
    {
        tests++;
    }
};

class TestEventHandler2
{
public:
    int yummies = 0;

    void handle(YummyEvent* event)
    {
        yummies++;
    }
};

Submittable* global_handler;

void create_yummies()
{
    for (size_t i = 0; i < 10000; i++)
    {
        global_handler->submit(new YummyEvent);
    }
}

void create_tests()
{
    for (size_t i = 0; i < 10000000; i++)
    {
        global_handler->submit(new TestEvent);
    }
}

size_t tenmil_additions()
{
    volatile size_t result;
    for (size_t i = 0; i < 100000000; i++)
    {
        result += 10; 
    }
    return result;
}

template <typename T>
concept Clock = std::same_as<T, std::chrono::high_resolution_clock> || std::same_as<T, std::chrono::steady_clock> || std::same_as<T, std::chrono::system_clock>;

template <Clock T>
class Timer
{
private:
    std::chrono::time_point<T> start;
public:
    Timer() { reset_timer(); }

    void reset_timer() { start = T::now(); }
    float get_time() const { return std::chrono::duration_cast<std::chrono::seconds>(T::now() - start).count(); }
    std::chrono::nanoseconds get_time_ns() const { return std::chrono::duration_cast<std::chrono::nanoseconds>(T::now() - start); }
};

int main(void)
{
    EventManager manager;
    global_handler = &manager; 
    TestEventHandler handler;
    TestEventHandler2 handler2;
    manager.subscribe<TestEventHandler, TestEvent>(&handler, &TestEventHandler::handle);
    manager.subscribe<TestEventHandler2, YummyEvent>(&handler2, &TestEventHandler2::handle);
    std::thread thread(create_tests);
    Timer<std::chrono::high_resolution_clock> timer;
    manager.process_events();
    thread.join();
    manager.process_events();
    std::cout << timer.get_time_ns().count() << std::endl; 
}
