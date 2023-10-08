#pragma once

#include <algorithm>
#include <deque>
#include <queue>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

class Event
{
public:
    virtual ~Event() {}
};

template <typename N, typename T> 
concept Derived = std::is_base_of<N, T>;

template <typename T, Derived<Event> E>
using mem_fun_t = void (T::*)(E*);

class HandlerFunctionBase 
{ 
public: 
	virtual ~HandlerFunctionBase() {}; 
	virtual void exec(const Event* event) = 0; 
}; 

template <typename T, Derived<Event> E> 
class MemberFunctionHandler : public HandlerFunctionBase 
{ 
private:
    mem_fun_t<T, E> mem_fun;
    T* instance;

public:
	MemberFunctionHandler(T* instance, mem_fun_t<T, E> mem_fun) 
        : instance(instance), mem_fun(mem_fun) {}; 

    void exec(const Event* event) override;
};

template <typename T, Derived<Event> E>
MemberFunctionHandler<T, E>::exec(const Event* event)
{
    (instance->*mem_fun)(static_cast<E>(event));
}

class EventHandler
{
private:
    // All of the handlers specific to a certain event, so the proper handlers functions can be called 
    std::unordered_multimap<std::type_index, HandlerFunctionBase*> handlers;

    // All of the handlers specific to an object (so when it gets deleted those can be removed)
    std::unordered_multimap<void*, HandlerFunctionBase*> instance_handlers; 

    std::queue<Event*, std::deque<Event*>> events;
public:
    // Adding handler of different events onto the bus
    template <typename T, Derived<Event> E> 
    void subscribe(T* handler, mem_fun_t<T, E>);

    // Removing a handler from the bus entierly  
    void unsubscribe(T* handler);

    // Add an event to the queue if a handler exists for it 
    void submit(const Event* event);

    // Process all events stored in the queue 
    void process_events();
};

template <typename T, Derived<Event> E> 
void EventHandler::subscribe(T* handler, mem_fun_t<T, E> mem_fun) 
{ 
    auto wrapped_handler = new MemberFunctionHandler<T, E>(handler, mem_fun); 
    handlers[std::type_index(typeid(E)] = wrapped_handler; 
    instanced_handlers[(void*) handler] = wrapped_handler;
} 
	
void EventHandler::submit(const Event* event) 
{ 
    if (handlers.count(std::type_index(typeid(*event)))) queue.push(event);
}

void EventHandler::process_events()
{
    for (; events.size() != 0; ) 
    {
        Event* event = events.pop();
        auto range = handlers.equal_range(std::type_index(typeid(event)));
        for (auto it = range.first; it != range.second; it++)
        {
            it->second->exec(event);
        }
    }
}

void EventHandler::unsubscribe(T* handler)
{
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
