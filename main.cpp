#include <algorithm>
#include <iostream>

#define EVENT_IMPLEMENTATION
#include "event.h"

class Event1 : public Event
{
public:
    static const size_t id;
    constexpr size_t get_id() override;
};

EVENT_GEN(Event1)

class Event2 : public Event 
{
public:
    static const size_t id;
    constexpr size_t get_id() override;
};

EVENT_GEN(Event2)

MAX_EVENT_INIT

class Handler
{
public:
    size_t i = 0;

    void handler(Event1* event) 
    {
        std::cout << "handled event" << std::endl;
    }

    void event_2_handler(Event2* event)
    {
        i++;
    }
};

void create_events(size_t processor, MultiEventManager& manager)
{
    for (size_t i = 0; i < 10000000; i++)
    {
        manager.submit(processor, new Event2);
    }
}

int main(void)
{
    MultiEventManager manager;
    size_t processor = manager.get_processor();
    Handler handler;
    manager.subscribe(processor, &handler, &Handler::handler);
    manager.subscribe(processor, &handler, &Handler::event_2_handler);
    Timer timer;
    create_events(processor, manager);
    std::cout << timer.get_time_ns().count() << std::endl;
    timer.reset_timer();
    manager.move_to_processors();
    std::cout << "Move to processor timer " << timer.get_time_ns().count() << std::endl;
    timer.reset_timer();
    manager.process_events(processor);
    std::cout << "Timer: " << timer.get_time_ns().count() << std::endl;
}
