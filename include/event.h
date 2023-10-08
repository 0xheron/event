#pragma once

// std
#include <algorithm>
#include <deque>
#include <mutex>
#include <queue>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

template <typename N, typename T> 
concept Derived = std::is_base_of<N, T>;

class EventData
{
public:
    template <typename T, Derived<decltype(*this)> E>
    using mem_fun_t = void (T::*)(E*);
public:
    virtual ~EventData() {}
};

class Submittable
{
public
    virtual ~Submittable() {};
    virtual submit(const EventData* event) override;
};

// Set this to a submittable to allow events to be "built" in event data
// And then submitted on construction 
extern Submittable* global_handler;

// Wrapper, allows for nice event submitting & being able to delay event submission all in 1
template <Derived<EventData> E>
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
	virtual void exec(const EventData* event) = 0; 
}; 

template <typename T, Derived<EventData> E> 
class MemberFunctionHandler : public HandlerFunctionBase 
{ 
private:
    EventData::mem_fun_t<T, E> mem_fun;
    T* instance;

public:
	MemberFunctionHandler(T* instance, EventData::mem_fun_t<T, E> mem_fun) 
        : instance(instance), mem_fun(mem_fun) {}; 

    void exec(const EventData* event) override;
};

template <typename T, Derived<EventData> E>
MemberFunctionHandler<T, E>::exec(const EventData* event)
{
    (instance->*mem_fun)(static_cast<E>(event));
}

class EventManager : public Submittable
{
private:
    // All of the handlers specific to a certain event, so the proper handlers functions can be called 
    std::unordered_multimap<std::type_index, HandlerFunctionBase*> handlers;

    // All of the handlers specific to an object (so when it gets deleted those can be removed)
    std::unordered_multimap<void*, HandlerFunctionBase*> instance_handlers; 

    // "Double buffering" of queues so events can be added while they are being processed 
    std::queue<EventData*, std::deque<EventData*>>* current_event_queue;
    std::queue<EventData*, std::deque<EventData*>>* process_event_queue;

    // Handle multiple threads adding events to this handler
    std::mutex handlers_mutex;
    std::mutex queue_mutex;
public:
    EventManager() 
    {
        current_event_queue = new decltype(current_event_queue);
        process_event_queue = new decltype(process_event_queue);
    }

    ~EventManager()
    {
        delete back_buffer_queue;
        delete current_event_queue;
    }
     
    // Adding handler of different events onto the bus
    template <typename T, Derived<EventData> E> 
    void subscribe(T* handler, mem_fun_t<T, E>);

    // Removing a handler from the bus entierly  
    template <typename T> 
    void unsubscribe(T* handler);

    // Add an event to the queue if a handler exists for it 
    void submit(const EventData* event) override;

    // Process all events stored in the queue 
    void process_events();
};

template <typename T, Derived<EventData> E> 
void EventManager::subscribe(T* handler, mem_fun_t<T, E> mem_fun) 
{ 
    std::lock_guard queue_guard(queue_mutex); 
    std::lock_guard guard(handlers_mutex);
    auto wrapped_handler = new MemberFunctionHandler<T, E>(handler, mem_fun); 
    handlers.insert({std::type_index(typeid(E)), wrapped_handler});
    instance_handlers.insert({(void*) handler, wrapped_handler});
} 
	
void EventManager::submit(const EventData* event) 
{ 
    std::lock_guard(handlers_mutex);
    if (handlers.count(std::type_index(typeid(*event)))) 
        current_event_queue->push(event);
}

void EventManager::process_events()
{
    std::lock_guard queue_guard(queue_mutex);

    for (; current_event_queue->size() != || 
            process_event_queue->size() != 0; )
    {
        {
            std::lock_guard handlers_guard(handlers_mutex);
            std::swap(current_event_queue, process_event_queue);
        }

        for (; process_event_queue->size() != 0; ) 
        {
            EventData* event = process_event_queue->pop();
            auto range = handlers.equal_range(std::type_index(typeid(event)));
            for (auto it = range.first; it != range.second; it++)
            {
                it->second->exec(event);
            }
        }
    }
}

template <typename T> 
void EventManager::unsubscribe(T* handler)
{
    std::lock_guard queue_guard(queue_mutex); 
    std::lock_guard guard(handlers_mutex);

    // Paranoia
    // Maybe ill test without this
    std::vector<decltype(handlers.begin())> erase;

    for (auto it1 = handlers.begin(); it1 != handlers.end(); it1++)
    {
        auto range = instance_handlers.equal_range(handler);
        for (auto it = range.first; it != range.second; it++)
        {
            if (it.second == it1.second) erase.push_back(it1);
        }
    }
    
    for (auto x : erase)
    {
        delete x.second;
        handlers.erase(x);
    }

    instance_handlers.erase(handler);
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
    std::unordered_map<H, EventManager> managers;
     
    std::mutex managers_mutex;
public:
    // Adding handlers of different events onto a specific handler (could be new)
    template <typename T, Derived<EventData> E>
    void subscribe(const H& manager, T* handler, mem_fun_t<T, E> mem_fun);
     
    // Unsubscribes a handler
    template <typename T>
    void unsubscibe(const H& manager, T* handler);

    // Add an event to the queue
    void submit(const EventData* event) override;

    // Add an event to one handler 
    void submit(const H& manager, const EventData* event); 

    // Process all events stored on one handler
    void process_events(const H& manager);
};

template <typename H, typename T, Derived<EventData> E>
void MultiEventManager<H>::subscribe(const H& manager, T* handler, 
        mem_fun<T, E> mem_fun)
{
    std::lock_guard(managers_mutex);
    if (!managers.contains(manager)) managers.insert({manager, EventData()});
    managers[manager].subscribe(handler, mem_fun);
}

template <typename H, typename T> 
void MultiEventManager<H>::unsubscribe(const H& manager, T* handler)
{
    std::lock_guard(managers_mutex);
    managers[manager].unsubscibe(handler);
}

template <typename H> 
void MultiEventManager<H>::submit(const EventData* event) 
{
    std::lock_guard(managers_mutex);
    for (auto [k, v] : managers)
    {
        v.submit(event);
    }
}

template <typename H>
void MultiEventManager<H>::submit(const H& manager, const EventData* event)
{
    std::lock_guard(managers_mutex);
    managers[manager].submit(event);
}

template <typename H> MultiEventManager<H>::process_events(const H& manager)
{
    std::lock_guard(managers_mutex);
    managers[manager].process_events();
}

