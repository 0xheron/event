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

MAX_EVENT_INIT

class Handler
{
public:
    void handler(Event1* event) 
    {
        std::cout << "handled event" << std::endl;
    }
};

int main(void)
{
    MultiEventManager manager;
    size_t processor = manager.get_processor();
    Handler handler;
    manager.subscribe(processor, &handler, &Handler::handler);
    manager.submit(processor, new Event1);
    manager.move_to_processors();
    manager.process_events(processor);
}
