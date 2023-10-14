#pragma once

// std
#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <queue>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <map>
#include <concepts>

// external
#include <xenium/ramalhete_queue.hpp>
#include <xenium/reclamation/generic_epoch_based.hpp>

class EventData;

template <typename T> 
concept EventDerived = std::is_base_of<EventData, T>::value;

class EventData
{
public:
    template <typename T, EventDerived E> 
    using mem_fun_t = void (T::*)(E*);
public:
    virtual ~EventData() {}
};

class Submittable
{
public:
    virtual ~Submittable() {};
    virtual void submit(EventData* event) = 0;
};

// Set this to a submittable to allow events to be "built" in event data
// And then submitted on construction 
extern Submittable* global_handler;

// Wrapper, allows for nice event submitting & being able to delay event submission all in 1
template <EventDerived E>
class Event
{
private:
    E built_event;
public:
    Event(const E& built_event)
        : built_event()
    {
        global_handler->submit(this->built_event);
    }

    Event(E&& built_event) 
        : built_event(std::move(built_event))
    {
        global_handler->submit(this->built_event);
    }
};

class HandlerFunctionBase 
{ 
public: 
	virtual ~HandlerFunctionBase() {}; 
	virtual void exec(EventData* event) = 0; 
    virtual std::type_index get_tinfo() = 0;
    virtual void* get_handler_ptr() = 0;
}; 

template <typename T, EventDerived E> 
class MemberFunctionHandler : public HandlerFunctionBase 
{ 
private:
    EventData::mem_fun_t<T, E> mem_fun;
    T* instance;

public:
	MemberFunctionHandler(T* instance, EventData::mem_fun_t<T, E> mem_fun) 
        : instance(instance), mem_fun(mem_fun) {}; 

    void exec(EventData* event) override
    {
        (instance->*mem_fun)(static_cast<E*>(event));
    }

    std::type_index get_tinfo() override 
    {
        return std::type_index(typeid(E));
    }

    void* get_handler_ptr() override
    {
        return (void*) instance;
    }
};

template <typename T>
using queue_type = xenium::ramalhete_queue<T,
      xenium::policy::reclaimer<xenium::reclamation::new_epoch_based<>>,
      xenium::policy::entries_per_node<1024>>;

class EventManager : public Submittable
{
private:
    // All of the handlers specific to a certain event, so the proper handlers functions can be called 
    std::multimap<std::type_index, HandlerFunctionBase*> handlers;

    // Where handlers get added 
    queue_type<HandlerFunctionBase*> handlers_queue;

    // Where events get added 
    queue_type<EventData*> event_queue;
public:
    EventManager() 
    {

    }

    ~EventManager()
    {

    }
     
    // Adding handler of different events onto the bus
    template <typename T, EventDerived E> 
    void subscribe(T* handler, EventData::mem_fun_t<T, E>);

    // Removing a handler from the bus entierly  
    template <typename T> 
    void unsubscribe(T* handler);

    // Add an event to the queue if a handler exists for it 
    void submit(EventData* event) override;

    // Process all events stored in the queue 
    size_t process_events();
private:
    // For use in both process events and unsubscribe
    // Puts all of the handlers in the queues into the maps
    size_t queue_to_handlers();
};

template <typename T, EventDerived E> 
void EventManager::subscribe(T* handler, EventData::mem_fun_t<T, E> mem_fun) 
{ 
    handlers_queue.push(new MemberFunctionHandler<T, E>(handler, mem_fun)); 
} 

void EventManager::submit(EventData* event) 
{ 
    event_queue.push(event);
}

// CANNOT be called when an unsubscription is happening
// See that class for reason
// Also may "fail" if try pop fails and return
// So this should be called once or twice with no pops 
size_t EventManager::process_events()
{
    while (queue_to_handlers() != 0);
    size_t valid_pops = 0;

    EventData* event;
    while (true)
    {
        if (!event_queue.try_pop(event)) break;
        auto range = handlers.equal_range(std::type_index(typeid(*event)));
        for (auto it = range.first; it != range.second; it++)
        {
            it->second->exec(event);
        }
        valid_pops++;
    }

    return valid_pops;
}

// CANNOT be called when process_events is being called
// This would be breaking anyway because a destroyed handler could be used as a handler in process_events
// Regardless of if this function is allowed in it or not
// Thus you cannot destroy event handlers while events are being processed
// However it is fine if events are not being processed
template <typename T> 
void EventManager::unsubscribe(T* handler)
{
    std::erase_if(handlers, 
        [&handler](const auto& item)
        {
            const auto& [k, v] = item;   
            if (v.get_handler_ptr() == (void*) handler) 
            {
                delete v;
                return true;
            } 
            return false;
        });
}

size_t EventManager::queue_to_handlers()
{
    size_t valid_pops = 0;

    while (true)
    {
        HandlerFunctionBase* handler;
        
        if (!handlers_queue.try_pop(handler)) break;
        handlers.insert({handler->get_tinfo(), handler});

        valid_pops++;
    }

    return valid_pops;
}

// An event handler where each created event goes to multiple handler groups  
// These groups could be on different threads so this could be used to implement event handling across threads 
// By calling the proper handler on the proper thread
// That is user tracked however
// The type used to identify the handlers

template <typename H>
class MultiEventManager : public Submittable
{
private: 
    queue_type<std::unique_ptr<std::pair<H, HandlerFunctionBase*>>> 
        manager_queue;

    queue_type<EventData*> events;

    std::unordered_map<H, std::multimap<std::type_index, 
        HandlerFunctionBase*>> managers;
public:
    // Adding handlers of different events onto a specific handler (could be new)
    template <typename T, EventDerived E>
    void subscribe(const H& manager, T* handler, 
            EventData::mem_fun_t<T, E> mem_fun);
     
    // Unsubscribes a handler
    template <typename T>
    void unsubscribe(const H& manager, T* handler);

    // Add an event to the queue
    void submit(EventData* event) override;

    // Process all events stored on one handler
    size_t process_events(const H& manager);
private:
    // For use in both process events and unsubscribe
    // Puts all of the handlers in the queues into the maps
    size_t queue_to_managers();
};

template <typename H>
template <typename T, EventDerived E> 
void MultiEventManager<H>::subscribe(const H& manager, T* handler, 
        EventData::mem_fun_t<T, E> mem_fun)
{
    manager_queue.push(std::make_unique<std::pair<H, HandlerFunctionBase*>>(
                std::pair(manager, new MemberFunctionHandler(handler, 
                        mem_fun))));
}

template <typename H>
template <typename T> 
void MultiEventManager<H>::unsubscribe(const H& manager, T* handler)
{
    while (queue_to_managers() != 0);
    std::erase_if(managers[manager], 
        [&handler](const auto& item)
        {
            const auto& [k, v] = item;   
            if (v.get_handler_ptr() == (void*) handler) 
            {
                delete v;
                return true;
            } 
            return false;
        });
}

template <typename H> 
void MultiEventManager<H>::submit(EventData* event) 
{
    events.push(event);
}

template <typename H> 
size_t MultiEventManager<H>::process_events(const H& manager)
{
    size_t valid_pops = 0;
    
    EventData* event;
    while (events.try_pop(event))
    {
        auto range = managers[manager].equal_range(
                std::type_index(typeid(*event)));
        for (auto it = range.first; it != range.second; it++)
        {
            it->second->exec(event);
        }
        valid_pops++;
    }

    return valid_pops;
}

template <typename H> 
size_t MultiEventManager<H>::queue_to_managers()
{
    size_t valid_pops = 0; 

    while (true)
    {
        std::unique_ptr<std::pair<H, HandlerFunctionBase*>> manager;  
        if (!manager_queue.try_pop(manager)) break;
        
        managers[manager->first].insert({manager->second->get_tinfo(), 
                manager->second});
        
        valid_pops++;
    }

    return valid_pops;
}
